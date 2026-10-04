idf_component_register(
  SRCS src/board_x4pro.cpp src/epd_bus.cpp src/epd_probe.cpp src/ssd1677.cpp src/uc81xx.cpp src/cw2017.cpp src/bm8563.cpp
  INCLUDE_DIRS include
  PRIV_INCLUDE_DIRS src
  REQUIRES vellum_core
  PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_spi esp_driver_i2c esp_timer
)
