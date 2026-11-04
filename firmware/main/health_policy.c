#include "health_policy.h"

health_decision_t health_decide(const health_inputs_t *in, const health_limits_t *limits)
{
    bool healthy;

    if (in->commissioned) {
        healthy = in->matter_started && in->network_attached && in->attached_s >= limits->stable_s;
    } else {
        healthy = in->matter_started && in->uptime_s >= limits->stable_s;
    }

    if (healthy) {
        return HEALTH_CONFIRM;
    }
    return in->uptime_s >= limits->deadline_s ? HEALTH_ROLLBACK : HEALTH_WAIT;
}
