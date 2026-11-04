/*
 * Decides whether a newly updated firmware image is healthy.
 *
 * This file has no ESP-IDF or Matter dependencies so it can be unit tested
 * on a host machine (see tests/test_health_policy.c). All hardware access
 * lives in ota_health.cpp.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HEALTH_WAIT = 0, /* Keep watching. */
    HEALTH_CONFIRM,  /* Keep this image: cancel rollback. */
    HEALTH_ROLLBACK, /* Return to the previous image. */
} health_decision_t;

typedef struct {
    uint32_t stable_s;   /* Connected time required before confirming. */
    uint32_t deadline_s; /* Give up and roll back after this long. */
} health_limits_t;

typedef struct {
    uint32_t uptime_s;      /* Seconds since this image started. */
    bool matter_started;    /* The Matter stack started successfully. */
    bool commissioned;      /* The device belongs to at least one fabric. */
    bool network_attached;  /* The device is attached to its network now. */
    uint32_t attached_s;    /* Seconds of unbroken attachment so far. */
} health_inputs_t;

/*
 * A commissioned device must prove it still has its network, because an image
 * that boots but cannot connect can never receive another update. A device
 * with no fabric has no network to prove, so stable uptime is enough.
 */
health_decision_t health_decide(const health_inputs_t *in, const health_limits_t *limits);

#ifdef __cplusplus
}
#endif
