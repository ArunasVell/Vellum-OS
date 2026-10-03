idf_component_register(
  SRCS src/board_x4pro.cpp
  INCLUDE_DIRS include
  REQUIRES vellum_core
  PRIV_REQUIRES esp_driver_gpio esp_driver_ledc
)
