idf_component_register(SRCS "ble_hid.cc"
                            "esp_hidd_prf_api.c"
                            "hid_dev.c"
                            "hid_device_le_prf.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "bt" "esp_driver_gpio" "nvs_flash"
                    )

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)
