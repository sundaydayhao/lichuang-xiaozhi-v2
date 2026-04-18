set(ble_hid_requires
    "esp_hid"
    "bt"
)

idf_component_register(
    SRCS "ble_hid.cc"
    INCLUDE_DIRS "include"
    REQUIRES ${ble_hid_requires}
)
