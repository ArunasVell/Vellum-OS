# Host build: desktop simulator and unit tests.
#   cmake -S sim -B build/sim && cmake --build build/sim && ctest --test-dir build/sim
cmake_minimum_required(VERSION 3.20)
project(vellum_host C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_subdirectory(../components/vellum_core vellum_core)

find_package(SDL2 REQUIRED)
add_executable(vellum-sim main.cpp png_writer.cpp)
target_link_libraries(vellum-sim PRIVATE vellum_core SDL2::SDL2)
target_compile_options(vellum-sim PRIVATE -Wall -Wextra -Werror)
# The free book store: the test feeds as a demo catalogue, and the real
# catalogues too when libcurl is installed (--demo-store keeps the demo).
target_compile_definitions(vellum-sim PRIVATE VELLUM_DEMO_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../tests/data")
find_package(CURL QUIET)
if(CURL_FOUND)
  target_link_libraries(vellum-sim PRIVATE CURL::libcurl)
  target_compile_definitions(vellum-sim PRIVATE VELLUM_SIM_CURL)
endif()

enable_testing()
add_executable(vellum-tests ../tests/test_main.cpp)
target_link_libraries(vellum-tests PRIVATE vellum_core)
target_compile_options(vellum-tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vellum-tests PRIVATE VELLUM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../tests/data")
add_test(NAME core COMMAND vellum-tests)
# Home, open the current book, turn pages, back home, library, settings.
add_test(NAME sim-headless COMMAND vellum-sim --headless --time 14:05 --keys onnnpnhnnnnohnnnnnono
         --screenshot sim-test.png)
# The same by touch: tap the current book, tap and swipe pages, then Settings.
add_test(NAME sim-touch COMMAND vellum-sim --headless --time 14:05
         --keys "[240,200][400,400][400,400>100,410]h[378,620][240,153]" --screenshot sim-touch.png)
# Hold a word in the book: the dictionary panel, then Highlight and a tap on the passage's end.
add_test(NAME sim-words COMMAND vellum-sim --headless --time 14:05 --keys "o[400,240~][60,762][130,315]"
         --screenshot sim-words.png)
# The free book store's demo catalogue: All books, the store link (Prev from the
# top), Gutenberg, Most popular, a book, Download.
add_test(NAME sim-store COMMAND vellum-sim --headless --demo-store --time 14:05 --card sim-store-card
         --keys nnnonnnnoonooo --screenshot sim-store.png)
# Its card has settings saved, as a card in use does, so it starts at home
# rather than in the first-time setup.
add_test(NAME sim-store-card COMMAND ${CMAKE_COMMAND} -E make_directory sim-store-card/vellum)
add_test(NAME sim-store-settings COMMAND ${CMAKE_COMMAND} -E touch sim-store-card/vellum/settings.txt)
set_tests_properties(sim-store-card PROPERTIES FIXTURES_SETUP store-card)
set_tests_properties(sim-store-settings PROPERTIES FIXTURES_SETUP store-card DEPENDS sim-store-card)
set_tests_properties(sim-store PROPERTIES FIXTURES_REQUIRED store-card)