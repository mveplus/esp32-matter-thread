idf_component_register(SRCS "app_main.cpp" "drivers.cpp" "ota_health.cpp" "health_policy.c"
                       PRIV_INCLUDE_DIRS "." "${PRODUCT_GEN_DIR}" "${ESP_MATTER_PATH}/examples/common/utils"
                       PRIV_REQUIRES app_update esp_driver_gpio esp_timer nvs_flash esp_pm app_reset
                                     esp_matter esp_matter_console openthread)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

# Same workaround as esp-matter examples/icd_app for the C5 link order.
if(CONFIG_IDF_TARGET_ESP32C5)
    idf_component_get_property(openthread_lib openthread COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PUBLIC $<TARGET_FILE:${openthread_lib}>)
endif()
