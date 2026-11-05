#include "drivers.h"

#include <stdlib.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <sdkconfig.h>

#include <app_reset.h>
#include <button_gpio.h>
#include <iot_button.h>

#if CONFIG_ENABLE_ICD_SERVER
#include <app/icd/server/ICDNotifier.h>
#endif

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::cluster;

static const char *TAG = "drivers";

/* GPIO28 on the XIAO ESP32-C5. See BOARDS in tools/gen_product.py. */
static constexpr int kBootButtonGpio = 28;
static constexpr uint8_t kPositionIdle = 0;
static constexpr uint8_t kPositionPressed = 1;

struct relay_t {
    gpio_num_t gpio;
    bool active_low;
};

struct button_t {
    uint16_t endpoint_id;
    bool press_reported;
};

static bool power_save_wanted()
{
#if CONFIG_PM_ENABLE
    return true;
#else
    return false;
#endif
}

driver_handle_t driver_relay_init(const product_endpoint_t *endpoint)
{
    auto *relay = static_cast<relay_t *>(calloc(1, sizeof(relay_t)));
    if (relay == nullptr) {
        return nullptr;
    }
    relay->gpio = static_cast<gpio_num_t>(endpoint->gpio);
    relay->active_low = endpoint->active_low;

    /* Latch the off level first so the pin never drives the relay on. */
    gpio_set_level(relay->gpio, relay->active_low ? 1 : 0);
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << endpoint->gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&config) != ESP_OK) {
        free(relay);
        return nullptr;
    }
    gpio_set_level(relay->gpio, relay->active_low ? 1 : 0);
    return relay;
}

esp_err_t driver_relay_set(driver_handle_t handle, bool on)
{
    auto *relay = static_cast<relay_t *>(handle);
    if (relay == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Relay on GPIO%d: %s", relay->gpio, on ? "on" : "off");
    return gpio_set_level(relay->gpio, on != relay->active_low ? 1 : 0);
}

static void set_position(uint16_t endpoint_id, uint8_t position)
{
    esp_matter_attr_val_t val(position);
    esp_err_t err = attribute::update(endpoint_id, Switch::Id, Switch::Attributes::CurrentPosition::Id, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CurrentPosition update failed: %s", esp_err_to_name(err));
    }
}

static void keep_device_awake()
{
#if CONFIG_ENABLE_ICD_SERVER
    chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
#endif
}

/* The button callbacks run on the button timer task, so hand off to Matter. */
static void on_press_down(void *arg, void *data)
{
    auto *button = static_cast<button_t *>(data);
    if (button->press_reported) {
        return;
    }
    button->press_reported = true;
    uint16_t endpoint_id = button->endpoint_id;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id]() {
        keep_device_awake();
        set_position(endpoint_id, kPositionPressed);
        switch_cluster::event::send_initial_press(endpoint_id, kPositionPressed);
    });
}

static void on_press_up(void *arg, void *data)
{
    uint16_t endpoint_id = static_cast<button_t *>(data)->endpoint_id;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id]() { set_position(endpoint_id, kPositionIdle); });
}

static void on_presses_finished(void *arg, void *data)
{
    auto *button = static_cast<button_t *>(data);
    uint16_t endpoint_id = button->endpoint_id;
    uint8_t presses = iot_button_get_repeat(static_cast<button_handle_t>(arg));
    button->press_reported = false;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, presses]() {
        switch_cluster::event::send_multi_press_complete(endpoint_id, kPositionPressed, presses);
    });
}

static esp_err_t new_button(int gpio, bool active_low, button_handle_t *handle)
{
    const button_config_t config = {};
    const button_gpio_config_t gpio_config = {
        .gpio_num = gpio,
        .active_level = static_cast<uint8_t>(active_low ? 0 : 1),
        .enable_power_save = power_save_wanted(),
    };
    return iot_button_new_gpio_device(&config, &gpio_config, handle);
}

esp_err_t driver_button_init(const product_endpoint_t *endpoint, uint16_t endpoint_id)
{
    auto *button = static_cast<button_t *>(calloc(1, sizeof(button_t)));
    if (button == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    button->endpoint_id = endpoint_id;

    button_handle_t handle = nullptr;
    esp_err_t err = new_button(endpoint->gpio, endpoint->active_low, &handle);
    if (err != ESP_OK) {
        free(button);
        return err;
    }
    err = iot_button_register_cb(handle, BUTTON_PRESS_DOWN, nullptr, on_press_down, button);
    err |= iot_button_register_cb(handle, BUTTON_PRESS_UP, nullptr, on_press_up, button);
    err |= iot_button_register_cb(handle, BUTTON_PRESS_REPEAT_DONE, nullptr, on_presses_finished, button);
    return err;
}

esp_err_t driver_factory_reset_button_init(void)
{
    button_handle_t handle = nullptr;
    esp_err_t err = new_button(kBootButtonGpio, true, &handle);
    if (err != ESP_OK) {
        return err;
    }
    return app_reset_button_register(handle);
}
