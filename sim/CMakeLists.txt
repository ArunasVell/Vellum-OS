# Host build: desktop simulator and unit tests.
#   cmake -S sim -B build/sim && cmake --build build/sim && ctest --test-dir build/sim
cmake_minimum_required(VERSION 3.20)
project(vellum_host CXX)

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

enable_testing()
add_executable(vellum-tests ../tests/test_main.cpp)
target_link_libraries(vellum-tests PRIVATE vellum_core)
target_compile_options(vellum-tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME core COMMAND vellum-tests)
# Home, open the current book, turn pages, back home, library, settings.
add_test(NAME sim-headless COMMAND vellum-sim --headless --time 14:05 --keys onnnpnhnnnnohnnnnnono
         --screenshot sim-test.png)
