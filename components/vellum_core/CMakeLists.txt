# Portable core: no ESP-IDF or SDL headers allowed here.
# Built as an ESP-IDF component on device and as a plain static library on
# the host (simulator and tests).
set(VELLUM_CORE_SRCS
  src/app.cpp
  src/app_reading.cpp
  src/app_setup.cpp
  src/app_controls.cpp
  src/app_quick.cpp
  src/app_store.cpp
  src/app_sync.cpp
  src/app_sleep.cpp
  src/app_views.cpp
  src/app_words.cpp
  src/covers.cpp
  src/dictionary.cpp
  src/battery_log.cpp
  src/catalogues.cpp
  src/bmp.cpp
  src/charset.cpp
  src/controls.cpp
  src/books/alice.cpp
  src/books/catalog.cpp
  src/books/father_brown.cpp
  src/books/moby_dick.cpp
  src/books/persuasion.cpp
  src/display_queue.cpp
  src/epub.cpp
  src/font_library.cpp
  src/font_raster.cpp
  src/framebuffer.cpp
  src/highlights.cpp
  src/keyboard.cpp
  src/kosync.cpp
  src/hyphenation.cpp
  src/hyphenation/en_us.cpp
  src/hyphenation/lt.cpp
  src/library.cpp
  src/opds.cpp
  src/peer_transfer.cpp
  src/phone_settings.cpp
  src/reader.cpp
  src/reading_stats.cpp
  src/screenshot.cpp
  src/settings.cpp
  src/shelves.cpp
  src/sleep_pictures.cpp
  src/stdio_storage.cpp
  src/store.cpp
  src/strings.cpp
  src/text.cpp
  src/text_book.cpp
  src/textfile.cpp
  src/touch.cpp
  src/ui.cpp
  src/upload.cpp
  src/updates.cpp
  src/upload_page.cpp
  src/xml.cpp
  src/zip.cpp
  third_party/miniz/miniz_tinfl.c
  third_party/qrcodegen/qrcodegen.c
  third_party/tjpgd/tjpgd.c
  src/fonts/atkinson_ttf.cpp
  src/fonts/ebgaramond_ttf.cpp
  src/fonts/literata_16.cpp
  src/fonts/literata_19.cpp
  src/fonts/literata_20.cpp
  src/fonts/literata_23.cpp
  src/fonts/literata_24.cpp
  src/fonts/literata_26.cpp
  src/fonts/literata_30.cpp
  src/fonts/literata_34.cpp
  src/fonts/literata_italic_20.cpp
  src/fonts/literata_italic_26.cpp
  src/fonts/literata_italic_40.cpp
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${VELLUM_CORE_SRCS} INCLUDE_DIRS include PRIV_INCLUDE_DIRS third_party/miniz third_party/qrcodegen third_party/tjpgd third_party/stb)
else()
  add_library(vellum_core STATIC ${VELLUM_CORE_SRCS})
  target_include_directories(vellum_core PUBLIC include PRIVATE third_party/miniz third_party/qrcodegen third_party/tjpgd third_party/stb)
  target_compile_features(vellum_core PUBLIC cxx_std_20)
  target_compile_options(vellum_core PRIVATE -Wall -Wextra -Werror)
endif()

# Vendored code keeps its own style; do not hold it to our warning flags.
set_source_files_properties(third_party/miniz/miniz_tinfl.c third_party/qrcodegen/qrcodegen.c third_party/tjpgd/tjpgd.c PROPERTIES COMPILE_OPTIONS "-w")
