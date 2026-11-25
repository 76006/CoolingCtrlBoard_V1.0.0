/* Build on the host with relief_cycle.c and the pressure include directory. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "relief_cycle.h"

static relief_cycle_t cycle;
static relief_config_t cfg;
static relief_sample_t sample;
static relief_event_t events[48];
static unsigned event_count;

static void record(void *context, const relief_event_t *event)
{
    assert(context == &events);
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = *event;
}

static unsigned count(const char *kind)
{
    unsigned i, n = 0;
    for (i = 0; i < event_count; ++i)
        n += strcmp(events[i].kind, kind) == 0;
    return n;
}

static const relief_event_t *event_at(const char *kind, unsigned occurrence)
{
    unsigned i;
    for (i = 0; i < event_count; ++i)
        if (!strcmp(events[i].kind, kind) && --occurrence == 0)
            return &events[i];
    assert(0);
    return NULL;
}

static void prepare(void)
{
    const relief_config_t config = {900, -1, 5, 2, 10, 2, 5, 2, 2, 3, 100, 1};
    cfg = config;
    memset(&sample, 0, sizeof(sample));
    sample.valid = 7;
    sample.t1 = 455;
    event_count = 0;
}

static void begin(void)
{
    assert(!relief_validate(&cfg, 1400));
    relief_begin(&cycle, &cfg, sample.ms, record, &events);
    assert(cycle.state == RELIEF_BASE);
}

static void point_raw(unsigned p, unsigned raw, int t0)
{
    sample.ms += 100;
    ++sample.seq;
    sample.p = (uint16_t)p;
    sample.raw_p = (uint16_t)raw;
    sample.t0 = (int16_t)t0;
    relief_step(&cycle, &sample);
}

static void point(unsigned p, int t0)
{
    point_raw(p, p, t0);
}

static void baseline(void)
{
    point(299, 250);
    point(301, 250);
    point(300, 250);
    assert(cycle.state == RELIEF_HEAT && cycle.initial_p == 300);
}

static void trigger(void)
{
    point(800, 250);
    point(810, 250);
    point(755, 240);
    assert(cycle.state == RELIEF_CHECK);
    point(702, 239);
    point(601, 238);
    assert(cycle.state == RELIEF_VENT);
}

static void close_valve(void)
{
    point(302, 240);
    point(300, 243);
    point(300, 242); /* small interruption resets stop timer */
    point(300, 243);
    point(300, 244);
    assert(cycle.state == RELIEF_VENT);
    point(300, 245);
    assert(cycle.state == RELIEF_RETURN);
}

static void test_two_rounds(void)
{
    unsigned i;
    prepare();
    cfg.rounds = 2;
    /* A run crossing the uint32 clock boundary still measures durations. */
    sample.ms = UINT32_MAX - 850U;
    begin();
    baseline();
    for (i = 1; i <= 2; ++i)
    {
        const relief_event_t *e;
        trigger();
        e = event_at("OPEN", i);
        assert(e->round == i && e->sample.p == 755 && e->sample.t0 == 240);
        assert(e->sample.t1 == 455 && e->peak == 810);
        assert(e->state == RELIEF_VENT && e->sample.valid == 7);
        assert(e->sample.ms == event_at("TRIGGER", i)->sample.ms);
        point(500, 238);
        close_valve();
        point(301, 247);
        point(300, 248);
        assert(event_at("ROUND", i)->peak == 810);
    }
    assert(cycle.state == RELIEF_DONE && count("DONE") == 1);
    assert(count("HEAT") == 2 && count("OPEN") == 2 && count("CLOSE") == 2);
}

static void test_limit_and_late_open(void)
{
    prepare(); begin(); baseline();
    point_raw(850, 901, 250); /* raw reaches limit first */
    assert(cycle.state == RELIEF_LIMIT);
    point(855, 249);
    point(845, 240);
    point(850, 239);
    point(850, 238); /* started in time; confirmation after observe deadline */
    assert(cycle.state == RELIEF_VENT && count("OPEN") == 1);
    assert(event_at("OPEN", 1)->peak == 901);
    point(935, 238);
    close_valve(); point(300, 246); point(300, 247);
    assert(event_at("ROUND", 1)->peak == 935);
    assert(count("LIMIT") == 1);
}

static void test_bad_runs(void)
{
    prepare(); begin(); baseline();
    point_raw(790, 900, 239);
    assert(cycle.state == RELIEF_CHECK && count("LIMIT") == 1);
    point(790, 245);
    assert(cycle.state == RELIEF_ABORT && count("FALSE_DROP") == 1 && !count("OPEN"));
    assert(event_at("FALSE_DROP", 1)->state == RELIEF_CHECK);
    prepare(); begin(); baseline(); point(900, 250);
    point(850, 250); point(820, 250); point(790, 250);
    assert(cycle.state == RELIEF_ABORT && count("NO_RELIEF") == 1);
    assert(event_at("NO_RELIEF", 1)->state == RELIEF_LIMIT);
    prepare(); begin(); point(897, 250); point(897, 250); point(897, 250);
    assert(cycle.state == RELIEF_ABORT && count("BAD_BASE") == 1);
    prepare(); cfg.timeout = 5; begin(); baseline();
    point(320, 250); point(330, 250); point(340, 250);
    point(350, 250); point(360, 250);
    assert(cycle.state == RELIEF_ABORT && count("TIMEOUT") == 1);
}

static void test_reopen(void)
{
    prepare(); begin(); baseline(); trigger(); close_valve();
    point(300, 244);
    point(300, 234); /* pressure would now allow restart; prioritize trigger */
    assert(cycle.state == RELIEF_CHECK && !count("ROUND"));
    point(299, 233); point(298, 233);
    assert(count("OPEN") == 2 && event_at("OPEN",2)->round == 1);
    point(298, 238); point(298, 238); point(298, 239);
    point(305, 240); point(340, 240); /* recovery interrupted */
    point(300, 241); point(300, 242);
    assert(cycle.state == RELIEF_RETURN);
    point(300, 243);
    assert(cycle.state == RELIEF_DONE);
}

static void test_temperature_window_spans_round_transition(void)
{
    prepare(); cfg.rounds = 2;
    begin(); baseline(); trigger(); close_valve();
    point(300, 250);
    point(300, 243);
    assert(cycle.state == RELIEF_HEAT && cycle.round == 2);
    point(350, 239);
    assert(cycle.state == RELIEF_CHECK);
    assert(event_at("TRIGGER", 2)->round == 2);
    assert(event_at("TRIGGER", 2)->sample.p == 350);
}

static void test_baseline_and_faults(void)
{
    unsigned i;
    prepare(); cfg.initial_p = 300; begin();
    point(302, 250); point(340, 250); point(301, 250); point(303, 250);
    for (i = 0; i < 10; ++i)
        relief_step(&cycle, &sample);
    assert(cycle.state == RELIEF_BASE);
    point(305, 250);
    assert(cycle.state == RELIEF_HEAT && cycle.initial_p == 300);
    sample.valid = 6; point(350, 100);
    assert(cycle.state == RELIEF_ABORT && count("SENSOR") == 1);
    assert(event_at("SENSOR", 1)->state == RELIEF_HEAT && event_at("SENSOR", 1)->sample.valid == 6);
    relief_abort(&cycle, "STOP", sample.ms, NULL);
    assert(!count("STOP"));
    prepare(); begin(); baseline();
    relief_abort(&cycle, "LINK", sample.ms + 50, NULL);
    assert(cycle.state == RELIEF_ABORT && event_at("LINK",1)->sample.ms == sample.ms+50);
    assert(event_at("LINK", 1)->state == RELIEF_HEAT && event_at("LINK", 1)->sample.valid == 7);
    prepare(); begin(); baseline();
    relief_abort(&cycle, "INTERLOCK", sample.ms, &sample);
    assert(cycle.state == RELIEF_ABORT && count("INTERLOCK") == 1);
    prepare(); cfg.window = 101; assert(relief_validate(&cfg, 1400));
    prepare(); cfg.max_p = 1400; assert(relief_validate(&cfg, 1400));
    prepare(); cfg.initial_p = 895; assert(relief_validate(&cfg, 1400));
    prepare(); cfg.initial_p = -2; assert(relief_validate(&cfg, 1400));
}

static void test_long_window_and_sample_integrity(void)
{
    unsigned i;
    prepare(); cfg.window = 100; cfg.timeout = 2000;
    sample.seq = UINT32_MAX - 50U;
    begin();
    for (i = 0; i < 100; ++i)
    {
        point(300, 260);
        assert(cycle.state == RELIEF_BASE);
    }
    point(300, 260);
    assert(cycle.state == RELIEF_HEAT);
    /* Gradual decline for exactly 10 s exercises both ends of the ring. */
    for (i = 1; i <= 99; ++i)
    {
        point(500, 260 - (int)(i / 10));
        assert(cycle.state == RELIEF_HEAT);
    }
    point(510, 250);
    assert(cycle.state == RELIEF_CHECK);
    assert(event_at("TRIGGER",1)->sample.p == 510);
    sample.seq += 1; point(510, 250);
    assert(cycle.state == RELIEF_ABORT && count("SENSOR") == 1);

    prepare(); begin(); baseline();
    sample.ms += 550; point(300, 250);
    assert(cycle.state == RELIEF_ABORT && count("SENSOR") == 1);
    prepare(); begin();
    point(300, 260); point(300, 260); point(300, 248);
    assert(cycle.state == RELIEF_CHECK && count("TRIGGER") == 1);
}

static void test_initial_average_uses_stability_period(void)
{
    unsigned i;
    prepare(); cfg.window = 10; cfg.stable = 2;
    begin();
    for (i = 0; i < 8; ++i) point(300, 249);
    point(305, 249); point(305, 249); point(305, 249);
    assert(cycle.state == RELIEF_HEAT);
    assert(cycle.initial_p == 305 && event_at("BASE",1)->sample.p == 305);
}

int main(void)
{
    test_two_rounds();
    test_limit_and_late_open();
    test_bad_runs();
    test_reopen();
    test_temperature_window_spans_round_transition();
    test_baseline_and_faults();
    test_long_window_and_sample_integrity();
    test_initial_average_uses_stability_period();
    puts("Relief cycle replay passed");
    return 0;
}
