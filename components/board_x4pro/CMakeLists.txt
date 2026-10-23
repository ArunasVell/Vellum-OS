# What's new in this version (written by CI before the build, see
# tools/release_notes.py), shown once after an update. Local builds have
# none: an empty file stands in.
set(release_notes "${CMAKE_CURRENT_LIST_DIR}/../../release-notes.md")
if(NOT EXISTS "${release_notes}")
  set(release_notes "${CMAKE_CURRENT_BINARY_DIR}/release-notes.md")
  file(WRITE "${release_notes}" "")
endif()

idf_component_register(
  SRCS src/board_x4pro.cpp src/epd_bus.cpp src/epd_probe.cpp src/ssd1677.cpp src/uc81xx.cpp src/cw2017.cpp src/bm8563.cpp src/sd_card.cpp src/gt911.cpp src/wifi_transfer.cpp src/wifi_common.cpp src/ota_updater.cpp src/http_network.cpp src/esp_now_peers.cpp
  INCLUDE_DIRS include
  PRIV_INCLUDE_DIRS src
  EMBED_TXTFILES ${release_notes}
  REQUIRES vellum_core
  PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_spi esp_driver_i2c esp_driver_sdmmc fatfs sdmmc esp_timer esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_pm app_update esp_https_ota esp_http_client mbedtls
)
