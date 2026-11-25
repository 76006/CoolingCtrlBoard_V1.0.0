#include "relief_cycle.h"

#include <stddef.h>
#include <string.h>

#define HISTORY_CAPACITY 101U

static int in_range(int32_t value, int32_t lo, int32_t hi)
{
    return value >= lo && value <= hi;
}

const char *relief_validate(const relief_config_t *c, int32_t hard_p)
{
    if (!c || hard_p < 2 || c->max_p < 1 || c->max_p >= hard_p)
        return "MAX_P";
    if (!in_range(c->tolerance, 1, 999) || c->tolerance >= c->max_p)
        return "TOLERANCE";
    if (c->initial_p < -1 || c->initial_p >= c->max_p - c->tolerance)
        return "INITIAL_P";
    if (!in_range(c->window, 1, 100)) return "WINDOW";
    if (!in_range(c->drop, 1, 1000)) return "DROP";
    if (!in_range(c->start_hold, 1, 600)) return "START_HOLD";
    if (!in_range(c->rise, 1, 1000)) return "RISE";
    if (!in_range(c->stop_hold, 1, 600)) return "STOP_HOLD";
    if (!in_range(c->stable, 1, 600)) return "STABLE";
    if (!in_range(c->observe, 1, 6000)) return "OBSERVE";
    if (!in_range(c->timeout, 1, 864000)) return "TIMEOUT";
    if (!in_range(c->rounds, 1, 999)) return "ROUNDS";
    return NULL;
}

const char *relief_state_name(relief_state_t state)
{
    static const char *const names[] = {
        "IDLE", "BASE", "HEAT", "LIMIT", "CHECK", "VENT", "RETURN", "DONE", "ABORT"
    };
    return (unsigned)state < sizeof(names)/sizeof(names[0]) ? names[state] : "UNKNOWN";
}

int relief_active(const relief_cycle_t *cycle)
{
    return cycle->state >= RELIEF_BASE && cycle->state <= RELIEF_RETURN;
}

void relief_begin(relief_cycle_t *c, const relief_config_t *cfg,
                  uint32_t now_ms, relief_emit_t emit, void *context)
{
    relief_config_t config = *cfg;
    memset(c, 0, sizeof(*c));
    c->cfg = config;
    c->emit = emit;
    c->context = context;
    c->state = RELIEF_BASE;
    c->round = 1;
    c->round_ms = now_ms;
}

static void report(const relief_cycle_t *c, const char *kind,
                   const relief_sample_t *sample, uint16_t peak)
{
    relief_event_t event;
    event.kind = kind;
    event.sample = *sample;
    event.round = c->round;
    event.peak = peak;
    event.state = c->state;
    if (c->emit)
        c->emit(c->context, &event);
}

static void note(const relief_cycle_t *c, const char *kind)
{
    report(c, kind, &c->last, c->peak);
}

void relief_abort(relief_cycle_t *c, const char *reason, uint32_t now_ms,
                  const relief_sample_t *sample)
{
    relief_sample_t end;
    if (!relief_active(c)) return;
    end = sample ? *sample : c->last;
    if (!sample) end.ms = now_ms;
    if ((end.valid & 4U) && end.p > c->peak) c->peak = end.p;
    if ((end.valid & 4U) && end.raw_p > c->peak) c->peak = end.raw_p;
    report(c, reason, &end, c->peak);
    c->state = RELIEF_ABORT;
}

static int elapsed(uint32_t now, uint32_t since, int32_t duration)
{
    return now - since >= (uint32_t)duration * 100U;
}

/* Keep the newest reference at or before the window boundary, plus every
 * newer point. At 100 ms sampling the longest supported window is 10 s. */
static int remember_temperature(relief_cycle_t *c, int16_t *reference)
{
    unsigned next, tail;
    while (c->count > 1)
    {
        next = (c->head + 1U) % HISTORY_CAPACITY;
        if (!elapsed(c->last.ms, c->history_ms[next], c->cfg.window)) break;
        c->head = (uint8_t)next;
        --c->count;
    }
    if (c->count == HISTORY_CAPACITY)
    {
        c->head = (uint8_t)((c->head + 1U) % HISTORY_CAPACITY);
        --c->count;
    }
    tail = (c->head + c->count) % HISTORY_CAPACITY;
    c->history_t0[tail] = c->last.t0;
    c->history_ms[tail] = c->last.ms;
    ++c->count;
    *reference = c->history_t0[c->head];
    return elapsed(c->last.ms, c->history_ms[c->head], c->cfg.window);
}

static void reset_temperature_window(relief_cycle_t *c)
{
    int16_t ignored;
    c->head = c->count = 0;
    (void)remember_temperature(c, &ignored);
}

static int in_baseline_band(const relief_cycle_t *c, int32_t target)
{
    int32_t difference = c->last.p - target;
    return in_range(difference, -c->cfg.tolerance, c->cfg.tolerance);
}

static int sustained(relief_cycle_t *c, int condition, int32_t duration)
{
    if (!condition)
    {
        c->holding = 0;
        return 0;
    }
    if (!c->holding)
    {
        c->holding = 1;
        c->hold_ms = c->last.ms;
    }
    return elapsed(c->last.ms, c->hold_ms, duration);
}

static int baseline_ready(relief_cycle_t *c, int window_full)
{
    uint16_t p = c->last.p;
    if (c->cfg.initial_p >= 0)
    {
        c->initial_p = (uint16_t)c->cfg.initial_p;
        return sustained(c, in_baseline_band(c, c->initial_p), c->cfg.stable)
               && window_full;
    }
    /* A longer temperature warm-up must not dilute the requested baseline
     * average with pressure values predating the stability period. */
    if (!window_full && c->cfg.window > c->cfg.stable &&
        !elapsed(c->last.ms, c->history_ms[c->head], c->cfg.window - c->cfg.stable))
        return 0;
    if (!c->base_count || (int32_t)p > c->base_min + c->cfg.tolerance ||
        (int32_t)p < c->base_max - c->cfg.tolerance)
    {
        c->base_min = c->base_max = p;
        c->hold_ms = c->last.ms;
        c->base_count = c->base_sum = 0;
    }
    if (p < c->base_min) c->base_min = p;
    if (p > c->base_max) c->base_max = p;
    ++c->base_count;
    c->base_sum += p;
    if (!window_full || !elapsed(c->last.ms, c->hold_ms, c->cfg.stable)) return 0;
    c->initial_p = (uint16_t)((c->base_sum + c->base_count / 2U) / c->base_count);
    return 1;
}

static void mark_limit(relief_cycle_t *c)
{
    c->limit_seen = 1;
    c->limit_ms = c->last.ms;
    if (c->state == RELIEF_HEAT)
        c->state = RELIEF_LIMIT;
    note(c, "LIMIT");
}

static void begin_round(relief_cycle_t *c, uint16_t current_p)
{
    c->state = RELIEF_HEAT;
    c->round_ms = c->last.ms;
    c->peak = current_p;
    c->holding = c->limit_seen = 0;
    if (current_p >= c->cfg.max_p)
        mark_limit(c);
    else
        note(c, "HEAT");
}

static void begin_candidate(relief_cycle_t *c, int16_t reference)
{
    c->state = RELIEF_CHECK;
    c->reference_t0 = reference;
    c->minimum_t0 = c->last.t0;
    c->candidate = c->last;
    c->candidate_peak = c->peak;
    c->holding = 0;
    note(c, "TRIGGER");
}

void relief_step(relief_cycle_t *c, const relief_sample_t *sample)
{
    int16_t reference;
    int full_window, dropped;
    uint16_t current_p;
    if (!relief_active(c)) return;
    if (c->seen_sample && sample->seq == c->last.seq) return;
    if ((sample->valid & 7U) != 7U ||
        (c->seen_sample && (sample->seq - c->last.seq != 1U ||
                            sample->ms - c->last.ms > 500U)))
    {
        relief_abort(c, "SENSOR", sample->ms, sample);
        return;
    }
    c->last = *sample;
    c->seen_sample = 1;
    current_p = sample->raw_p > sample->p ? sample->raw_p : sample->p;
    if (current_p > c->peak) c->peak = current_p;
    if (elapsed(sample->ms, c->round_ms, c->cfg.timeout))
    {
        relief_abort(c, "TIMEOUT", sample->ms, sample);
        return;
    }
    full_window = remember_temperature(c, &reference);
    dropped = reference - sample->t0 >= c->cfg.drop;
    if (c->state != RELIEF_BASE && !c->limit_seen && current_p >= c->cfg.max_p)
        mark_limit(c);

    switch (c->state)
    {
    case RELIEF_BASE:
        if (!baseline_ready(c, full_window)) break;
        if (c->initial_p + c->cfg.tolerance >= c->cfg.max_p)
        {
            relief_abort(c, "BAD_BASE", sample->ms, sample);
            break;
        }
        {
            relief_sample_t base = *sample;
            base.p = c->initial_p;
            report(c, "BASE", &base, c->peak);
        }
        begin_round(c, current_p);
        if (dropped) begin_candidate(c, reference);
        break;
    case RELIEF_LIMIT:
        /* A trigger arriving exactly at the observation deadline is in time. */
        if (sample->ms - c->limit_ms > (uint32_t)c->cfg.observe * 100U)
        {
            relief_abort(c, "NO_RELIEF", sample->ms, sample);
            break;
        }
        if (!dropped && elapsed(sample->ms, c->limit_ms, c->cfg.observe))
        {
            relief_abort(c, "NO_RELIEF", sample->ms, sample);
            break;
        }
        /* fall through */
    case RELIEF_HEAT:
        if (dropped) begin_candidate(c, reference);
        break;
    case RELIEF_CHECK:
        if (c->reference_t0 - sample->t0 < c->cfg.drop)
        {
            relief_abort(c, "FALSE_DROP", sample->ms, sample);
            break;
        }
        if (sample->t0 < c->minimum_t0) c->minimum_t0 = sample->t0;
        if (elapsed(sample->ms, c->candidate.ms, c->cfg.start_hold))
        {
            c->state = RELIEF_VENT;
            report(c, "OPEN", &c->candidate, c->candidate_peak);
        }
        break;
    case RELIEF_VENT:
        if (sample->t0 < c->minimum_t0) c->minimum_t0 = sample->t0;
        if (sustained(c, sample->t0 - c->minimum_t0 >= c->cfg.rise,
                      c->cfg.stop_hold))
        {
            c->state = RELIEF_RETURN;
            note(c, "CLOSE");
            reset_temperature_window(c);
            c->holding = 0;
            (void)sustained(c, in_baseline_band(c, c->initial_p), c->cfg.stable);
        }
        break;
    case RELIEF_RETURN:
        if (dropped)
        {
            begin_candidate(c, reference);
            break;
        }
        if (sustained(c, in_baseline_band(c, c->initial_p), c->cfg.stable))
        {
            note(c, "ROUND");
            if (c->round == c->cfg.rounds)
            {
                c->state = RELIEF_DONE;
                note(c, "DONE");
            }
            else
            {
                ++c->round;
                begin_round(c, current_p);
            }
        }
        break;
    default:
        break;
    }
}
