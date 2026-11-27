set(common_sources "http_origin.cpp" "local_name.cpp" "network_identity.cpp" "ota/boot.cpp" "ota/upload.cpp" "ota/web_network.cpp")
set(common_requires esp_driver_gpio esp_http_server esp_hw_support esp_wifi esp_event
                    esp_netif esp_timer nvs_flash lwip bootloader_support app_update)
if(OPENOSC_COMPANION_RECOVERY)
    idf_component_register(SRCS ${common_sources} "ota/recovery.cpp"
        INCLUDE_DIRS "." "../../../include" REQUIRES ${common_requires})
else()
    idf_component_register(
        SRCS ${common_sources} "main.cpp" "network.cpp" "portal.cpp" "file_portal.cpp" "bluetooth.cpp"
             "scpi_websocket.cpp"
             "swd/dap_probe.cpp" "swd/swd_service.cpp" "swd/arm/DAP.c" "swd/arm/SW_DP.c"
        INCLUDE_DIRS "." "../../../include" "swd" "swd/arm"
        REQUIRES ${common_requires} esp_driver_uart bt)
endif()
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
