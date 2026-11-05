/*
 * Types shared by the generated product_config.h and the firmware.
 * tools/gen_product.py emits the enum values below, so keep them in step with
 * ENDPOINTS in that file. tests/test_gen_product.py compiles the result.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRODUCT_EP_ON_OFF_PLUGIN_UNIT = 0,
    PRODUCT_EP_GENERIC_SWITCH,
} product_endpoint_type_t;

typedef struct {
    product_endpoint_type_t type;
    int gpio;
    bool active_low;
} product_endpoint_t;

#ifdef __cplusplus
}
#endif
