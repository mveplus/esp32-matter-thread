/*
 * Universal board firmware. The product manifest decides which endpoints exist;
 * see the JSON files in products/ and tools/gen_product.py.
 */
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <common_macros.h>
#include <platform/ESP32/OpenthreadLauncher.h>

#include "drivers.h"
#include "ota_health.h"
#include "product_config.h"

#if CONFIG_APP_OTA_TEST_NEVER_CONFIRM && CONFIG_SECURE_BOOT
#error "CONFIG_APP_OTA_TEST_NEVER_CONFIRM must not be used in a production build"
#endif

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static const char *TAG = "app_main";

static constexpr uint16_t kCommissioningWindowSeconds = 300;
static constexpr uint8_t kMultiPressMax = 3;

struct relay_endpoint_t {
    uint16_t endpoint_id;
    driver_handle_t driver;
};

static relay_endpoint_t s_relays[sizeof(kProductEndpoints) / sizeof(kProductEndpoints[0])];
static unsigned s_relay_count;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed, fail-safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            auto &window = chip::Server::GetInstance().GetCommissioningWindowManager();
            if (!window.IsCommissioningWindowOpen()) {
                CHIP_ERROR err = window.OpenBasicCommissioningWindow(
                    chip::System::Clock::Seconds16(kCommissioningWindowSeconds),
                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to open commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;

    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify: type %u, effect %u, variant %u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == PRE_UPDATE && priv_data != nullptr && cluster_id == OnOff::Id &&
        attribute_id == OnOff::Attributes::OnOff::Id) {
        return driver_relay_set(priv_data, val->val.b);
    }
    return ESP_OK;
}

static esp_err_t create_relay(node_t *node, const product_endpoint_t *product)
{
    driver_handle_t driver = driver_relay_init(product);
    if (driver == nullptr) {
        return ESP_FAIL;
    }
    on_off_plug_in_unit::config_t config;
    endpoint_t *endpoint = on_off_plug_in_unit::create(node, &config, ENDPOINT_FLAG_NONE, driver);
    if (endpoint == nullptr) {
        return ESP_FAIL;
    }
    s_relays[s_relay_count++] = {endpoint::get_id(endpoint), driver};
    return ESP_OK;
}

static esp_err_t create_switch(node_t *node, const product_endpoint_t *product)
{
    generic_switch::config_t config;
    config.switch_cluster.feature_flags = cluster::switch_cluster::feature::momentary_switch::get_id();
    endpoint_t *endpoint = generic_switch::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    if (endpoint == nullptr) {
        return ESP_FAIL;
    }

    cluster_t *cluster = cluster::get(endpoint, Switch::Id);
    cluster::switch_cluster::feature::action_switch::add(cluster);
    cluster::switch_cluster::feature::momentary_switch_multi_press::config_t multi_press;
    multi_press.multi_press_max = kMultiPressMax;
    cluster::switch_cluster::feature::momentary_switch_multi_press::add(cluster, &multi_press);

    return driver_button_init(product, endpoint::get_id(endpoint));
}

static esp_err_t create_endpoints(node_t *node)
{
    for (unsigned i = 0; i < kProductEndpointCount; i++) {
        const product_endpoint_t *product = &kProductEndpoints[i];
        esp_err_t err = ESP_ERR_NOT_SUPPORTED;
        switch (product->type) {
        case PRODUCT_EP_ON_OFF_PLUGIN_UNIT:
            err = create_relay(node, product);
            break;
        case PRODUCT_EP_GENERIC_SWITCH:
            err = create_switch(node, product);
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

/* Applies the stored OnOff value, which Matter restored during startup. */
static void sync_relays(void)
{
    lock::ScopedChipStackLock lock(portMAX_DELAY);
    for (unsigned i = 0; i < s_relay_count; i++) {
        attribute_t *attribute = attribute::get(s_relays[i].endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
        esp_matter_attr_val_t val = esp_matter_invalid(nullptr);
        if (attribute != nullptr && attribute::get_val(attribute, &val) == ESP_OK) {
            driver_relay_set(s_relays[i].driver, val.val.b);
        }
    }
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    /* Never erase here: NVS holds the fabric and Thread credentials. */
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err)));
}

#if CONFIG_PM_ENABLE
static void init_power_management(void)
{
    esp_pm_config_t config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#endif
    };
    esp_err_t err = esp_pm_configure(&config);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Power management failed: %s", esp_err_to_name(err)));
}
#endif

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Product %s", PRODUCT_NAME);
    init_nvs();
#if CONFIG_PM_ENABLE
    init_power_management();
#endif
    ota_health_start();

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    esp_err_t err = create_endpoints(node);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to create endpoints: %s", esp_err_to_name(err)));

    err = driver_factory_reset_button_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Factory reset button unavailable: %s", esp_err_to_name(err));
    }

    esp_openthread_platform_config_t thread_config = {
        .radio_config = {.radio_mode = RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = HOST_CONNECTION_MODE_NONE},
        .port_config = {.storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10},
    };
    set_openthread_platform_config(&thread_config);

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err)));

    sync_relays();
    ota_health_set_matter_started();

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
