idf_component_register(
    SRCS
        "test_main.c"
        "../../../main/safety.c"
        "../../../main/choreography.c"
        "../../../main/automation.c"
        "../../../main/calibration.c"
        "../../../main/command_protocol.c"
        "../../../main/display_model.c"
        "../../../main/ota_policy.c"
        "../../../main/panel_io_model.c"
        "../../../main/profile_catalog.c"
        "../../../main/servo_bus.c"
        "../../../main/status_led.c"
        "../../../main/walle_core.c"
        "../../../main/wifi_manager.c"
    INCLUDE_DIRS "." "../../../main"
    REQUIRES driver nvs_flash json esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE WALLE_PROTOCOL_ONLY=1)
