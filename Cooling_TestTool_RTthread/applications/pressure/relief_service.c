/* Serial relief-test session. The pressure task never writes the console. */
#include "cooling_config.h"
#if COOLING_TEST_MODE
#include "relief_service.h"
#include "relief_cycle.h"
#include "cooling_pressure.h"
#include "heater/cooling_heater.h"
#include <stdarg.h>
#include <shell.h>

#define EVENT_CAPACITY 32U
#define LINK_TIMEOUT_MS 3000U

typedef struct
{
    relief_sample_t sample;
    uint32_t run;
    relief_state_t state;
    uint16_t round;
    uint8_t heater;
} wire_sample_t;

static struct rt_mutex session_lock;
static struct rt_mutex output_lock;
static volatile rt_bool_t ready;
static relief_cycle_t cycle;
static relief_config_t config;
static uint32_t run_id, next_event, acknowledged, start_ms;
static uint32_t run_trip, last_contact, heartbeat;
static rt_bool_t started, streaming, overflow;
static wire_sample_t latest;
static relief_event_t events[EVENT_CAPACITY];
static rt_uint32_t saved_echo, saved_prompt;

static uint32_t millis(rt_tick_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000U) / RT_TICK_PER_SECOND);
}

static uint32_t now_ms(void)
{
    return millis(rt_tick_get());
}

/* Serialize application frames independently of the state mutex. Prefix
 * newline also separates a response from an existing FinSH prompt. */
static void send_line(const char *format, ...)
{
    char line[RT_CONSOLEBUF_SIZE];
    va_list args;
    va_start(args, format);
    rt_vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    rt_mutex_take(&output_lock, RT_WAITING_FOREVER);
    rt_kputs(line);
    rt_mutex_release(&output_lock);
}

static void append_event(const relief_event_t *event)
{
    events[next_event % EVENT_CAPACITY] = *event;
    ++next_event;
}

static void on_event(void *ctx, const relief_event_t *event)
{
    (void)ctx;
    /* Reserve the last slot for an explicit terminal overflow record. */
    if (overflow) return;
    if (next_event - acknowledged < EVENT_CAPACITY - 1U)
    {
        append_event(event);
        return;
    }
    {
        /* The engine may emit several events during one step. Capture the
         * first lost event before subsequent transitions change its stage. */
        relief_event_t terminal = *event;
        terminal.kind = "OVERFLOW";
        append_event(&terminal);
        overflow = RT_TRUE;
    }
}

static void check_overflow(void)
{
    if (overflow)
    {
        cycle.state = RELIEF_ABORT;
        overflow = RT_FALSE;
    }
}

static void abort_test(const char *why, uint32_t now, const relief_sample_t *sample)
{
    relief_abort(&cycle, why, now, sample);
    check_overflow();
    cooling_safety_set_test_heat(RT_FALSE, run_trip);
}

rt_bool_t relief_service_control(const cooling_adc_snapshot_t *adc,
                                 rt_uint32_t blocks, rt_int16_t ntc1,
                                 rt_uint16_t *target)
{
    relief_sample_t s = {0};
    const char *fault = RT_NULL;
    rt_bool_t allow;
    int16_t raw_temp;
    uint32_t now = now_ms();
    if (!ready) return RT_FALSE;
    s.seq = adc->sequence;
    s.ms = millis(adc->tick);
    s.t1 = ntc1;
    if ((adc->valid_mask & COOLING_ADC_VALID_BIT(COOLING_ADC_NTC0)) &&
        now - s.ms <= COOLING_PRESSURE_ADC_MAX_AGE_MS &&
        cooling_ntc_from_adc(COOLING_ADC_NTC0, adc->value[COOLING_ADC_NTC0].raw, &raw_temp) == 0 &&
        cooling_ntc_from_adc(COOLING_ADC_NTC0, adc->value[COOLING_ADC_NTC0].filtered, &s.t0) == 0)
        s.valid |= 1U;
    if (!(blocks & COOLING_PRESSURE_BLOCK_TEMP_INVALID))
        s.valid |= 2U;
    if (!(blocks & COOLING_PRESSURE_BLOCK_ADC_INVALID) &&
        cooling_sensor_pressure_mpa1000(adc->value[COOLING_ADC_HYSN2].filtered, &s.p) == 0 &&
        cooling_sensor_pressure_mpa1000(adc->value[COOLING_ADC_HYSN2].raw, &s.raw_p) == 0)
        s.valid |= 4U;

    rt_mutex_take(&session_lock, RT_WAITING_FOREVER);
    now = now_ms();
    latest.sample = s;
    if (relief_active(&cycle))
    {
        cooling_safety_snapshot_t safety;
        cooling_safety_get_snapshot(&safety);
        if (blocks & COOLING_PRESSURE_BLOCK_OVERPRESSURE) fault = "OVERPRESS";
        else if (blocks & COOLING_PRESSURE_BLOCK_OVERTEMPERATURE) fault = "OVERTEMP";
        else if (s.valid != 7U) fault = "SENSOR";
        else if (blocks || safety.trip_sequence != run_trip) fault = "INTERLOCK";
        else if (now - heartbeat >= LINK_TIMEOUT_MS) fault = "LINK";

        if (fault)
        {
            /* A failure before the run has a sample gets no readings from
             * the preceding idle session. A stale frame dates the failure
             * at detection, since its ADC stamp no longer describes it. */
            rt_bool_t predates_run = !cycle.seen_sample && (int32_t)(s.ms - start_ms) < 0;
            if (!s.valid) s.ms = now;
            abort_test(fault, now, predates_run ? RT_NULL : &s);
        }
        else if ((int32_t)(s.ms - start_ms) >= 0 || cycle.seen_sample)
        {
            relief_step(&cycle, &s);
            check_overflow();
        }
    }
    latest.run = run_id;
    latest.state = cycle.state;
    latest.round = cycle.round;
    latest.heater = adc->heater_on;
    allow = cycle.state == RELIEF_HEAT;
    cooling_safety_set_test_heat(allow, run_trip);
    if (run_id) *target = (rt_uint16_t)config.max_p;
    rt_mutex_release(&session_lock);
    return allow;
}

static void stream_entry(void *parameter)
{
    uint32_t sent_seq = 0, sent_event = 0, sent_run = 0, retry_ms = 0;
    rt_bool_t have_sample = RT_FALSE;
    (void)parameter;
    for (;;)
    {
        uint32_t id = 0;
        uint32_t event_run = 0;
        wire_sample_t report;
        relief_event_t event;
        rt_bool_t sample_due = RT_FALSE;
        uint32_t now;
        rt_mutex_take(&session_lock, RT_WAITING_FOREVER);
        now = now_ms();
        if (streaming && now - last_contact >= LINK_TIMEOUT_MS)
        {
            streaming = RT_FALSE;
            have_sample = RT_FALSE;
            finsh_set_echo(saved_echo);
            finsh_set_prompt_mode(saved_prompt);
        }
        if (streaming)
        {
            report = latest;
            sample_due = report.run == run_id &&
                         (!have_sample || report.sample.seq != sent_seq || report.run != sent_run);
            if (run_id != sent_run)
            {
                sent_event = acknowledged;
                sent_run = run_id;
            }
            if (sent_event < acknowledged || now - retry_ms >= 500U)
            {
                sent_event = acknowledged;
                retry_ms = now;
            }
            if (sent_event < next_event)
            {
                event = events[sent_event % EVENT_CAPACITY];
                id = ++sent_event;
                event_run = run_id;
            }
        }
        rt_mutex_release(&session_lock);
        if (sample_due)
        {
            const relief_sample_t *s = &report.sample;
            send_line("\n@R,S,%u,%u,%u,%u,%s,%u,%d,%d,%u,%u\n",
                (unsigned)report.run, (unsigned)s->seq, (unsigned)s->ms, report.round,
                relief_state_name(report.state), s->p, s->t0, s->t1, s->valid, report.heater);
            sent_seq = s->seq;
            have_sample = RT_TRUE;
        }
        if (id)
        {
            send_line("\n@R,E,%u,%u,%u,%u,%s,%u,%d,%d,%u,%s,%u\n",
                (unsigned)event_run, (unsigned)id, (unsigned)event.sample.ms, event.round,
                event.kind, event.sample.p, event.sample.t0, event.sample.t1, event.peak,
                relief_state_name(event.state), event.sample.valid);
        }
        rt_thread_mdelay(20);
    }
}

rt_err_t relief_service_init(void)
{
    rt_thread_t tx;
    rt_err_t result;
    rt_mutex_init(&session_lock, "rctest", RT_IPC_FLAG_PRIO);
    rt_mutex_init(&output_lock, "rcout", RT_IPC_FLAG_PRIO);
    tx = rt_thread_create("rctx", stream_entry, RT_NULL, 1280, 21, 10);
    if (!tx) return -RT_ENOMEM;
    result = rt_thread_startup(tx);
    if (result != RT_EOK)
        rt_thread_delete(tx);
    else
        ready = RT_TRUE;
    return result;
}

/* Parse decimal fields strictly so a malformed command cannot apply a
 * prefix of a configuration. All protocol integers fit signed 32 bits. */
static int decimal(const char **cursor, char delimiter, int32_t *value)
{
    const char *p = *cursor;
    int negative = *p == '-';
    int32_t n = 0;
    if (negative) ++p;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9')
    {
        int digit = *p++ - '0';
        if (n > (INT32_MAX - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    if (*p != delimiter) return 0;
    *cursor = p + (delimiter != 0);
    *value = negative ? -n : n;
    return 1;
}

static const char *parse_cfg(const char *p, relief_config_t *cfg)
{
    int32_t *fields[] = {&cfg->max_p, &cfg->initial_p, &cfg->tolerance,
        &cfg->window, &cfg->drop, &cfg->start_hold, &cfg->rise, &cfg->stop_hold,
        &cfg->stable, &cfg->observe, &cfg->timeout, &cfg->rounds};
    unsigned i;
    for (i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i)
        if (!decimal(&p, i == 11 ? 0 : ',', fields[i])) return "FORMAT";
    return relief_validate(cfg, COOLING_PRESSURE_CUTOFF_MPA1000);
}

static void rc(int argc, char **argv)
{
    const char *error = RT_NULL;
    const char *cmd;
    const char *p;
    char reply[RT_CONSOLEBUF_SIZE] = {0};
    int32_t run = 0, ack = 0;
    uint32_t now;
    if (!ready)
    {
        rt_kprintf("\n@R,X,0,NOT_READY\n");
        return;
    }
    if (argc < 2) { send_line("\n@R,X,0,FORMAT\n"); return; }
    cmd = argv[1];
    if (argc >= 3)
    {
        p = argv[2];
        if (!decimal(&p, 0, &run) || run < 0)
        { send_line("\n@R,X,0,FORMAT\n"); return; }
    }
    rt_mutex_take(&session_lock, RT_WAITING_FOREVER);
    now = now_ms();
    if (!rt_strcmp(cmd, "hello") && argc == 2)
    {
        if (!streaming)
        {
            saved_echo = finsh_get_echo();
            saved_prompt = finsh_get_prompt_mode();
            finsh_set_echo(0);
            finsh_set_prompt_mode(0);
        }
        streaming = RT_TRUE;
        last_contact = now;
        rt_snprintf(reply, sizeof(reply), "\n@R,H,1,%u,%u,%s\n",
            COOLING_PRESSURE_CUTOFF_MPA1000, (unsigned)run_id, relief_state_name(cycle.state));
    }
    else if (!rt_strcmp(cmd, "ping") && argc == 4)
    {
        p = argv[3];
        if (!decimal(&p, 0, &ack) || ack < 0) error = "ACK";
        else if ((uint32_t)run != run_id) error = "RUN";
        else if ((uint32_t)ack > next_event) error = "ACK";
        else
        {
            if ((uint32_t)ack > acknowledged) acknowledged = (uint32_t)ack;
            /* A delayed ping cannot extend an already expired heating lease. */
            if (relief_active(&cycle) && now - heartbeat >= LINK_TIMEOUT_MS)
            {
                abort_test("LINK", now, RT_NULL);
                cooling_heater_set(RT_FALSE);
            }
            heartbeat = last_contact = now;
        }
    }
    else if (!rt_strcmp(cmd, "sync") && argc == 3)
    {
        if ((uint32_t)run != run_id) error = "RUN";
        else rt_snprintf(reply, sizeof(reply), "\n@R,Y,%u,%u,%u\n",
            (unsigned)run, (unsigned)acknowledged, (unsigned)next_event);
    }
    else if (!rt_strcmp(cmd, "cfg") && argc == 4 && run > 0)
    {
        relief_config_t parsed;
        error = parse_cfg(argv[3], &parsed);
        if (!error && (uint32_t)run == run_id)
        {
            if (rt_memcmp(&parsed, &config, sizeof(config))) error = "RUN_USED";
        }
        else if (!error)
        {
            if (relief_active(&cycle)) error = "BUSY";
            else if (next_event != acknowledged) error = "PENDING";
            else
            {
                config = parsed;
                run_id = (uint32_t)run;
                next_event = acknowledged = 0;
                started = RT_FALSE;
                rt_memset(&cycle, 0, sizeof(cycle));
            }
        }
        if (!error)
            rt_snprintf(reply, sizeof(reply), "\n@R,C,%u,%s\n", (unsigned)run, argv[3]);
    }
    else if (argc == 3 && (!rt_strcmp(cmd, "start") || !rt_strcmp(cmd, "stop")))
    {
        if (!run || (uint32_t)run != run_id) error = "RUN";
        else if (!rt_strcmp(cmd, "start"))
        {
            if (!started)
            {
                cooling_safety_snapshot_t safety;
                cooling_safety_set_power(RT_TRUE);
                cooling_safety_get_snapshot(&safety);
                run_trip = safety.trip_sequence;
                heartbeat = last_contact = start_ms = now;
                relief_begin(&cycle, &config, now, on_event, RT_NULL);
                started = RT_TRUE;
            }
            rt_snprintf(reply, sizeof(reply), "\n@R,A,%u,START\n", (unsigned)run);
        }
        else
        {
            if (!started)
            {
                /* Cancel an accepted configuration even if START is still in flight. */
                relief_begin(&cycle, &config, now, on_event, RT_NULL);
                started = RT_TRUE;
            }
            if (relief_active(&cycle)) abort_test("STOP", now, RT_NULL);
            cooling_heater_set(RT_FALSE);
            rt_snprintf(reply, sizeof(reply), "\n@R,A,%u,STOP\n", (unsigned)run);
        }
    }
    else error = "FORMAT";
    rt_mutex_release(&session_lock);
    if (error) send_line("\n@R,X,%u,%s\n", (unsigned)run, error);
    else if (*reply) send_line("%s", reply);
}
MSH_CMD_EXPORT(rc, Qt relief cycle test interface);
#endif
