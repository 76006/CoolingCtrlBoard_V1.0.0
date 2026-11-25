#ifndef RELIEF_CYCLE_H
#define RELIEF_CYCLE_H

#include <stdint.h>

/* Wire order. Durations are tenths of a second, temperatures tenths of a
 * degree C, pressures thousandths of an MPa. */
typedef struct
{
    int32_t max_p, initial_p, tolerance, window, drop, start_hold;
    int32_t rise, stop_hold, stable, observe, timeout, rounds;
} relief_config_t;

typedef enum
{
    RELIEF_IDLE, RELIEF_BASE, RELIEF_HEAT, RELIEF_LIMIT, RELIEF_CHECK,
    RELIEF_VENT, RELIEF_RETURN, RELIEF_DONE, RELIEF_ABORT
} relief_state_t;

typedef struct
{
    uint32_t seq, ms;
    uint16_t p, raw_p;
    int16_t t0, t1;
    uint8_t valid; /* bits 0, 1, 2: NTC0, NTC1, pressure */
} relief_sample_t;

typedef struct
{
    const char *kind; /* points to a string literal */
    relief_sample_t sample;
    uint16_t round, peak;
    relief_state_t state; /* phase that generated this event; pre-abort on fault */
} relief_event_t;

typedef void (*relief_emit_t)(void *context, const relief_event_t *event);

/* All storage is owned by the caller. Serialize every call. The callback
 * copies events synchronously and must not call back into the engine. */
typedef struct
{
    relief_state_t state;
    uint16_t round, peak, initial_p;
    relief_config_t cfg;
    relief_sample_t last, candidate;
    relief_emit_t emit;
    void *context;
    uint32_t round_ms, hold_ms, limit_ms;
    uint32_t base_sum, base_count;
    uint16_t base_min, base_max, candidate_peak;
    int16_t reference_t0, minimum_t0;
    uint8_t seen_sample, holding, limit_seen;
    uint8_t head, count;
    int16_t history_t0[101];
    uint32_t history_ms[101];
} relief_cycle_t;

/* Returns NULL for a legal configuration, otherwise a wire-safe error. */
const char *relief_validate(const relief_config_t *cfg, int32_t hard_p);
const char *relief_state_name(relief_state_t state);
int relief_active(const relief_cycle_t *cycle);
/* Caller validates cfg, enforces idle/start idempotence and hardware
 * interlocks. Zero-initialized engine is IDLE. Start always resets history. */
void relief_begin(relief_cycle_t *cycle, const relief_config_t *cfg,
                  uint32_t now_ms, relief_emit_t emit, void *context);
void relief_step(relief_cycle_t *cycle, const relief_sample_t *sample);
/* Copy a fresh sample before external fault abort if available. If sample
 * is NULL, uses last sample with its timestamp advanced to now_ms. */
void relief_abort(relief_cycle_t *cycle, const char *reason, uint32_t now_ms,
                  const relief_sample_t *sample);

#endif
