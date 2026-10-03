# Portable core: no ESP-IDF or SDL headers allowed here.
# Built as an ESP-IDF component on device and as a plain static library on
# the host (simulator and tests).
set(VELLUM_CORE_SRCS
  src/app.cpp
  src/display_queue.cpp
  src/framebuffer.cpp
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${VELLUM_CORE_SRCS} INCLUDE_DIRS include)
else()
  add_library(vellum_core STATIC ${VELLUM_CORE_SRCS})
  target_include_directories(vellum_core PUBLIC include)
  target_compile_features(vellum_core PUBLIC cxx_std_20)
  target_compile_options(vellum_core PRIVATE -Wall -Wextra -Werror)
endif()
