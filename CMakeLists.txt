# doritos_crash_course - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(doritos_crash_course LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(DORITOSCRASHCOURSE_SOURCES
    src/main.cpp
    src/usbcam_stubs.cpp
    src/avatar_standin.cpp
    src/avatar_real.cpp
    src/frame_stats.cpp
)

# Xbox avatar asset loading, from gibbed's xenia avatars branch (BSD licence,
# see src/avatars/LICENSE).
file(GLOB DCC_AVATAR_SOURCES CONFIGURE_DEPENDS
    src/avatars/xenia/avatars/*.cc src/avatars/xenia/base/*.cc)
list(APPEND DORITOSCRASHCOURSE_SOURCES ${DCC_AVATAR_SOURCES})

# Test-only scripted controller for automated runs (see src/test_pad.cpp).
option(DCC_TEST_PAD "Replace controller input with a scripted pad (testing only)" OFF)
if(DCC_TEST_PAD)
    list(APPEND DORITOSCRASHCOURSE_SOURCES src/test_pad.cpp)
else()
    list(APPEND DORITOSCRASHCOURSE_SOURCES src/input_fix.cpp)
endif()

if(WIN32)
    add_executable(doritos_crash_course WIN32 ${DORITOSCRASHCOURSE_SOURCES})
else()
    add_executable(doritos_crash_course ${DORITOSCRASHCOURSE_SOURCES})
endif()

target_include_directories(doritos_crash_course PRIVATE src src/avatars)

# Stage the Xenos GPU emulation plugin next to the exe (it is loaded at runtime).
rexglue_setup_target(doritos_crash_course GPU_PLUGINS xenos)

# Stand-in avatar (game/standin_avatar.bin), made from your data.pak by
# tools/make_standin_avatar.py. Needs Python 3 with Pillow.
set(DCC_STANDIN "${CMAKE_SOURCE_DIR}/game/standin_avatar.bin")
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/game/data.pak")
    add_custom_command(
        OUTPUT "${DCC_STANDIN}"
        COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/make_standin_avatar.py"
                "${CMAKE_SOURCE_DIR}/game/data.pak" "${DCC_STANDIN}"
        DEPENDS "${CMAKE_SOURCE_DIR}/game/data.pak"
                "${CMAKE_SOURCE_DIR}/tools/make_standin_avatar.py"
        COMMENT "Making the stand-in avatar from data.pak")
    add_custom_target(doritos_crash_course_standin ALL DEPENDS "${DCC_STANDIN}")
elseif(NOT EXISTS "${DCC_STANDIN}")
    message(WARNING "game/standin_avatar.bin can't be made (needs Python 3 and "
                    "game/data.pak); without it and the Xbox Original Avatars "
                    "app the player character is invisible.")
endif()
