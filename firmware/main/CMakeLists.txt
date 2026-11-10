idf_component_register(
    SRCS "main.c" "voice_loop.c" "microsd_manager.c" "power_manager.c" "audio_driver.c" "assistant.c" "voice_client.c" "bug_fonts.c" "battery.c" "clock.c" "touch_driver.c" "settings_store.c" "ui_nav.c" "ui_text_entry.c" "ui_wifi.c" "ui_weather.c" "stopwatch.c" "ui_stopwatch.c" "ui_triqui.c" "ui_games.c" "games_action.c" "games_mind.c" "games_words.c" "imu.c" "rtc.c" "ota_update.c" "wake_local.c" "clips_player.c" "local_skills.c" "local_timers.c" "local_lists.c" "command_local.c" "mem_report.c" "wake_verify.c" "local_holidays.c" "server_finder.c" "local_weather.c" "local_math.c" "local_radio.c" "local_calendar.c" "local_style.c" "local_context.c" "local_claude.c" "local_mail.c" "local_online.c" "local_more.c" "config_serial.c" "dev_bridge.c" "tts_online.c" "local_prices.c" "music_player.c" "ui_music.c" "ui_rows.c" "ui_headlines.c" "ui_article.c" "alarms.c" "ui_alarm.c" "network_manager.c" "wake_word_detector.c" "audio_circular_buffer.c" "power_profile_manager.c" "periodic_listener.c" "speaker_identification.c" "speaker_context_manager.c" "ui_core.c" "ui_home.c" "ui_home_lumina.c" "ui_voice_feedback.c" "ui_email.c" "ui_calendar.c" "ui_settings.c" "ui_skills.c" "display_driver.c" "ui_voice_integration.c" "power_optimizer.c" "voice_activity_detection.c" "microphone_duty_cycle.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_common esp_system esp_timer esp_event esp_netif esp_pm freertos soc driver esp_adc sdmmc fatfs esp_http_client esp-tls esp_wifi esp_lcd esp_psram lwip nvs_flash json esp_https_ota app_update mbedtls esp_partition mdns
    EMBED_FILES "fonts/Montserrat-Medium.ttf"
)

# Existing logging uses %u for uint32_t and size_t throughout.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

# The board's own voice (words and phrases) goes into the storage partition with the firmware
esptool_py_flash_to_partition(flash "storage" "${CMAKE_CURRENT_SOURCE_DIR}/../voice_clips.bin")
