/* GPIO drivers for the endpoint types a product manifest can ask for. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#include "product.h"

typedef void *driver_handle_t;

/* Drives the output to its off level before anything else touches it. */
driver_handle_t driver_relay_init(const product_endpoint_t *endpoint);
esp_err_t driver_relay_set(driver_handle_t handle, bool on);

/* A momentary button that reports Generic Switch events on endpoint_id. */
esp_err_t driver_button_init(const product_endpoint_t *endpoint, uint16_t endpoint_id);

/* Holding the XIAO boot button factory-resets the device. */
esp_err_t driver_factory_reset_button_init(void);
