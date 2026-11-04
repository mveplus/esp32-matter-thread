/* Host test: cc -I firmware/main tests/test_health_policy.c firmware/main/health_policy.c */
#include <stdio.h>

#include "health_policy.h"

static int failures;
static const health_limits_t limits = {.stable_s = 120, .deadline_s = 900};

static void check(const char *name, health_inputs_t in, health_decision_t want)
{
    health_decision_t got = health_decide(&in, &limits);
    printf("%-52s %s\n", name, got == want ? "ok" : "FAIL");
    failures += got != want;
}

int main(void)
{
    check("fresh boot waits",
          (health_inputs_t){.uptime_s = 0}, HEALTH_WAIT);
    check("no fabric, stable uptime confirms",
          (health_inputs_t){.uptime_s = 120, .matter_started = true}, HEALTH_CONFIRM);
    check("no fabric, Matter never started rolls back",
          (health_inputs_t){.uptime_s = 900}, HEALTH_ROLLBACK);
    check("commissioned, attached long enough confirms",
          (health_inputs_t){.uptime_s = 200, .matter_started = true, .commissioned = true,
                            .network_attached = true, .attached_s = 120}, HEALTH_CONFIRM);
    check("commissioned, attached too briefly waits",
          (health_inputs_t){.uptime_s = 200, .matter_started = true, .commissioned = true,
                            .network_attached = true, .attached_s = 119}, HEALTH_WAIT);
    check("commissioned, uptime alone is not enough",
          (health_inputs_t){.uptime_s = 899, .matter_started = true, .commissioned = true}, HEALTH_WAIT);
    check("commissioned, no network by deadline rolls back",
          (health_inputs_t){.uptime_s = 900, .matter_started = true, .commissioned = true}, HEALTH_ROLLBACK);
    check("commissioned, just lost network keeps waiting",
          (health_inputs_t){.uptime_s = 300, .matter_started = true, .commissioned = true,
                            .network_attached = false, .attached_s = 500}, HEALTH_WAIT);
    check("healthy at the deadline confirms, not rolls back",
          (health_inputs_t){.uptime_s = 900, .matter_started = true, .commissioned = true,
                            .network_attached = true, .attached_s = 120}, HEALTH_CONFIRM);
    return failures ? 1 : 0;
}
