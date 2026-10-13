cmake_minimum_required(VERSION 3.20)
project(starpost C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

add_compile_options(-Wall -Wextra -Werror)

add_library(starpost_game STATIC src/game/game.c)
target_include_directories(starpost_game PUBLIC src)

add_executable(starpost
    src/platform/main.c
    src/render/render.c)
target_link_libraries(starpost PRIVATE starpost_game PkgConfig::SDL3 OpenGL::GL ZLIB::ZLIB m)

add_executable(starpost_tests tests/test_game.c)
target_link_libraries(starpost_tests PRIVATE starpost_game)

enable_testing()
add_test(NAME game COMMAND starpost_tests)
