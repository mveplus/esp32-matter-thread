#include "ota_health.h"

#include <atomic>

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

#include "health_policy.h"

using chip::DeviceLayer::ConnectivityMgr;
using chip::DeviceLayer::PlatformMgr;

static const char *TAG = "ota_health";

static constexpr uint32_t kSampleIntervalMs = 5000;
static constexpr uint32_t kStackStaleAfterS = 30;
static constexpr uint32_t kTaskStackBytes = 4096;

static std::atomic<bool> s_matter_started{false};

typedef struct {
    bool commissioned;
    bool attached;
    uint32_t sampled_at_s;
    bool valid;
} stack_sample_t;

static uint32_t uptime_s(void)
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}

/*
 * Never blocks on the stack lock. A Matter task that has hung must not stop
 * this task from reaching its deadline, so a sample that cannot be taken just
 * leaves the old one to go stale.
 */
static void sample_stack(stack_sample_t *sample)
{
    if (!s_matter_started.load() || !PlatformMgr().TryLockChipStack()) {
        return;
    }
    sample->commissioned = chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
    sample->attached = ConnectivityMgr().IsThreadAttached();
    PlatformMgr().UnlockChipStack();
    sample->sampled_at_s = uptime_s();
    sample->valid = true;
}

static void confirm_image(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not confirm image: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Image confirmed");
}

static void roll_back(void)
{
    if (!esp_ota_check_rollback_is_possible()) {
        /* Nothing to go back to. An unconfirmed image also refuses every later
         * OTA, so keeping this one is the only way to stay updatable. */
        ESP_LOGE(TAG, "Image failed its health check but no other image exists; keeping it");
        confirm_image();
        return;
    }
    ESP_LOGE(TAG, "Image failed its health check, rolling back");
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    /* Only returns on failure. A reset still rolls back in the bootloader. */
    ESP_LOGE(TAG, "Rollback failed (%s), resetting", esp_err_to_name(err));
    esp_restart();
}

static void health_task(void *arg)
{
    const health_limits_t limits = {
        .stable_s = CONFIG_APP_HEALTH_STABLE_S,
        .deadline_s = CONFIG_APP_HEALTH_DEADLINE_S,
    };
    stack_sample_t sample = {};
    uint32_t attached_since_s = 0;
    bool was_attached = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
        sample_stack(&sample);

        uint32_t now_s = uptime_s();
        bool fresh = sample.valid && now_s - sample.sampled_at_s <= kStackStaleAfterS;
        bool attached = fresh && sample.attached;
        if (attached && !was_attached) {
            attached_since_s = now_s;
        }
        was_attached = attached;

        const health_inputs_t inputs = {
            .uptime_s = now_s,
            /* A stack too stuck to sample has not really started. */
            .matter_started = fresh,
            .commissioned = sample.commissioned,
            .network_attached = attached,
            .attached_s = attached ? now_s - attached_since_s : 0,
        };

        health_decision_t decision = health_decide(&inputs, &limits);
#if CONFIG_APP_OTA_TEST_NEVER_CONFIRM
        if (decision == HEALTH_CONFIRM) {
            decision = now_s >= limits.deadline_s ? HEALTH_ROLLBACK : HEALTH_WAIT;
        }
#endif
        if (decision == HEALTH_CONFIRM) {
            confirm_image();
            break;
        }
        if (decision == HEALTH_ROLLBACK) {
            roll_back();
            break;
        }
    }
    vTaskDelete(nullptr);
}

void ota_health_start(void)
{
    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
    if (err != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    ESP_LOGW(TAG, "New image: confirm within %d s or roll back", CONFIG_APP_HEALTH_DEADLINE_S);
    if (xTaskCreate(health_task, "ota_health", kTaskStackBytes, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        /* Without the gate nothing can confirm this image, so fail it now. */
        ESP_LOGE(TAG, "Could not start the health task");
        roll_back();
    }
}

void ota_health_set_matter_started(void)
{
    s_matter_started.store(true);
}
