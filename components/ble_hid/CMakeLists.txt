set(ble_hid_srcs "ble_hid.cc")
set(ble_hid_requires "esp_driver_gpio")

if(CONFIG_BT_ENABLED)
    list(APPEND ble_hid_srcs
        "esp_hidd_prf_api.c"
        "hid_dev.c"
        "hid_device_le_prf.c"
    )
    list(APPEND ble_hid_requires "bt" "nvs_flash")
endif()

idf_component_register(SRCS ${ble_hid_srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES ${ble_hid_requires}
                    )

# ESP-IDF 5.5 no longer exposes these legacy Bluedroid API headers
# to this out-of-tree component via transitive include paths.
idf_build_get_property(idf_path IDF_PATH)
if(CONFIG_BT_ENABLED)
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${idf_path}/components/bt/host/bluedroid/api/include/api"
        "${idf_path}/components/bt/common/include"
        "${idf_path}/components/bt/controller/esp32"
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)
