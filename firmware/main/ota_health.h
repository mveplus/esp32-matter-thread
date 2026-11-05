/*
 * Runs the post-update health gate. See docs/ota.md.
 *
 * When the running image is in the bootloader's pending-verify state, a task
 * samples the Matter stack and either confirms the image or rolls it back, as
 * decided by health_policy.c. On any other image state it does nothing.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Call before starting Matter, so a hang inside startup still rolls back. */
void ota_health_start(void);

/* Call once esp_matter::start() has succeeded. */
void ota_health_set_matter_started(void);

#ifdef __cplusplus
}
#endif
