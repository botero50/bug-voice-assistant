idf_component_register(
    SRCS "main.c" "voice_loop.c" "microsd_manager.c" "power_manager.c" "audio_driver.c" "network_manager.c" "wake_word_detector.c" "audio_circular_buffer.c" "power_profile_manager.c" "periodic_listener.c" "speaker_identification.c" "speaker_context_manager.c" "ui_core.c" "ui_home.c" "ui_voice_feedback.c" "ui_email.c" "ui_calendar.c" "ui_settings.c" "ui_skills.c" "display_driver.c" "ui_voice_integration.c" "power_optimizer.c" "voice_activity_detection.c" "microphone_duty_cycle.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_common esp_system esp_timer esp_event esp_netif esp_pm freertos soc driver esp_adc sdmmc fatfs esp_http_client esp-tls esp_wifi esp_lcd esp_psram lwip nvs_flash
)

# Existing logging uses %u for uint32_t and size_t throughout.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)
