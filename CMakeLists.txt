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
target_link_libraries(starpost_game PUBLIC m)

add_library(starpost_fx STATIC src/fx/fx.c)
target_include_directories(starpost_fx PUBLIC src)
target_link_libraries(starpost_fx PUBLIC m)

add_library(starpost_audio STATIC src/audio/synth.c)
target_include_directories(starpost_audio PUBLIC src)
target_link_libraries(starpost_audio PUBLIC m)

add_library(starpost_input STATIC src/input/pad.c)
target_include_directories(starpost_input PUBLIC src)

add_executable(starpost
    src/platform/main.c
    src/render/render.c)
target_link_libraries(starpost PRIVATE starpost_game starpost_fx starpost_audio starpost_input PkgConfig::SDL3 OpenGL::GL ZLIB::ZLIB)

add_executable(starpost_tests tests/test_game.c)
target_link_libraries(starpost_tests PRIVATE starpost_game)

add_executable(starpost_fx_tests tests/test_fx.c)
target_link_libraries(starpost_fx_tests PRIVATE starpost_fx)

add_executable(starpost_audio_tests tests/test_audio.c)
target_link_libraries(starpost_audio_tests PRIVATE starpost_audio starpost_game)

add_executable(starpost_pad_tests tests/test_pad.c)
target_link_libraries(starpost_pad_tests PRIVATE starpost_input starpost_game)

enable_testing()
add_test(NAME game COMMAND starpost_tests)
add_test(NAME fx COMMAND starpost_fx_tests)
add_test(NAME audio COMMAND starpost_audio_tests)
add_test(NAME pad COMMAND starpost_pad_tests)
