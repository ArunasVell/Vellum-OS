# Portable core: no ESP-IDF or SDL headers allowed here.
# Built as an ESP-IDF component on device and as a plain static library on
# the host (simulator and tests).
set(VELLUM_CORE_SRCS
  src/app.cpp
  src/books/alice.cpp
  src/books/catalog.cpp
  src/books/father_brown.cpp
  src/books/moby_dick.cpp
  src/books/persuasion.cpp
  src/display_queue.cpp
  src/framebuffer.cpp
  src/hyphenation.cpp
  src/hyphenation/en_us.cpp
  src/hyphenation/lt.cpp
  src/library.cpp
  src/reader.cpp
  src/settings.cpp
  src/strings.cpp
  src/text.cpp
  src/ui.cpp
  src/fonts/literata_16.cpp
  src/fonts/literata_20.cpp
  src/fonts/literata_23.cpp
  src/fonts/literata_26.cpp
  src/fonts/literata_30.cpp
  src/fonts/literata_34.cpp
  src/fonts/literata_italic_20.cpp
  src/fonts/literata_italic_26.cpp
  src/fonts/literata_italic_40.cpp
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${VELLUM_CORE_SRCS} INCLUDE_DIRS include)
else()
  add_library(vellum_core STATIC ${VELLUM_CORE_SRCS})
  target_include_directories(vellum_core PUBLIC include)
  target_compile_features(vellum_core PUBLIC cxx_std_20)
  target_compile_options(vellum_core PRIVATE -Wall -Wextra -Werror)
endif()
