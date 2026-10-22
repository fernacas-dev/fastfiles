cmake_minimum_required(VERSION 3.22)
project(FastFiles VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(FF_SANITIZERS "Enable ASan+UBSan" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(FREETYPE REQUIRED freetype2)

add_executable(fastfiles
    src/main.c
    src/app.c
    src/fs.c
    src/ui.c
    src/input.c
    src/config.c
    src/font.c
)

target_include_directories(fastfiles PRIVATE src ${SDL3_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_compile_definitions(fastfiles PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
target_link_directories(fastfiles PRIVATE ${SDL3_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS})
target_link_libraries(fastfiles PRIVATE ${SDL3_LIBRARIES} ${FREETYPE_LIBRARIES} m)

target_compile_options(fastfiles PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wformat=2
)
# -Werror enforced for regular builds but not forced here to keep bootstrap simple;
# CI/dev can add -Werror. We enable it:
target_compile_options(fastfiles PRIVATE -Werror)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(fastfiles PRIVATE -O0 -g -fno-omit-frame-pointer)
else()
    target_compile_options(fastfiles PRIVATE -O2)
endif()

if(FF_SANITIZERS)
    target_compile_options(fastfiles PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(fastfiles PRIVATE -fsanitize=address,undefined)
endif()

# Tests
enable_testing()
add_executable(test_fs tests/test_fs.c src/fs.c src/config.c)
target_include_directories(test_fs PRIVATE src)
target_compile_definitions(test_fs PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
target_compile_options(test_fs PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wformat=2 -Werror -O0 -g)
if(FF_SANITIZERS)
    target_compile_options(test_fs PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_fs PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME fs_tests COMMAND test_fs)

# App-layer test (needs SDL3+FreeType; runs headless via offscreen driver)
add_executable(test_app tests/test_app.c src/app.c src/fs.c src/config.c src/font.c)
target_include_directories(test_app PRIVATE src ${SDL3_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_compile_definitions(test_app PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
target_link_directories(test_app PRIVATE ${SDL3_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS})
target_link_libraries(test_app PRIVATE ${SDL3_LIBRARIES} ${FREETYPE_LIBRARIES} m)
target_compile_options(test_app PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wformat=2 -Werror -O0 -g)
if(FF_SANITIZERS)
    target_compile_options(test_app PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_app PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME app_tests COMMAND test_app)
# SDL_Init leaks benign third-party singletons (dbus/drm); suppress only those.
set_tests_properties(app_tests PROPERTIES
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/lsan.supp")

install(TARGETS fastfiles DESTINATION bin)
install(FILES fastfiles.desktop DESTINATION share/applications)
