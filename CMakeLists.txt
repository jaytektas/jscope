cmake_minimum_required(VERSION 3.16)
project(jscope CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The suite is a suite: enable_testing() must precede any add_test, or ctest
# silently reports "No tests were found" and every check has to be run by hand.
enable_testing()
add_custom_target(scope_tests COMMENT "build every scope test binary")

#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
find_package(JFramework 0.1 CONFIG REQUIRED)
# libj_platform.a is a static archive, so it exports none of its own private
# dependencies — JHttpClient's OpenSSL among them. The application links them
# WHERE THEY EXIST: a static archive only yields the objects something actually
# references, and this application never calls JHttpClient, so on a target
# without OpenSSL the link simply does not need it. Required would make a
# cross-build fail for a dependency it does not use.
find_package(OpenSSL)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
# The app's first hard third-party runtime dependency. It is unavoidable: the
# 1008C is a vendor-specific device with no kernel driver, and there is no route
# to it short of usbfs ioctls, which is reimplementing libusb badly.
# Windows has no pkg-config for it, so the paths are given directly there; on
# Linux pkg-config knows.
if(WIN32)
    if(NOT LIBUSB_WIN_INCLUDE_DIR OR NOT LIBUSB_WIN_LIBRARY)
        message(FATAL_ERROR
            "cross-compiling needs libusb: pass -DLIBUSB_WIN_INCLUDE_DIR and "
            "-DLIBUSB_WIN_LIBRARY pointing at a MinGW build of libusb-1.0")
    endif()
    add_library(libusb_target INTERFACE)
    target_include_directories(libusb_target INTERFACE ${LIBUSB_WIN_INCLUDE_DIR})
    target_link_libraries(libusb_target INTERFACE ${LIBUSB_WIN_LIBRARY})
    add_library(PkgConfig::LIBUSB ALIAS libusb_target)

    # The GCC runtime goes INSIDE the binary. A MinGW link leaves imports on
    # libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll, which live in
    # the cross toolchain and are not on any Windows machine -- the .exe refuses
    # to start with "Importing dlls ... failed" and names no useful culprit. The
    # instrument is carried to a vehicle on a laptop, so what has to come out of
    # this build is ONE file that runs, not a file plus three DLLs that have to
    # be found and kept beside it.
    add_link_options(-static-libgcc -static-libstdc++ -static)
else()
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
endif()

file(GLOB SCOPE_USB_SRC     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/usb/*.cpp)
file(GLOB SCOPE_HAL_SRC     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/scope/*.cpp)
# Generated and file-backed sources. Not tied to either instrument, so both
# applications and every test can have them.
file(GLOB SCOPE_SOURCE_SRC  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/sources/*.cpp)

# The instrument's own driver. Kept beside its application rather than in src/,
# because it is the one part of this tree that is about a particular scope.
file(GLOB SCOPE1008_DRIVER_SRC CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/apps/scope1008/drivers/*.cpp)
file(GLOB SCOPE_MEASURE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/measure/*.cpp)
file(GLOB SCOPE_CAPTURE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/capture/*.cpp)
file(GLOB SCOPE_UI_SRC      CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)
file(GLOB SCOPE_SHELL_SRC   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)
# The headless half of the app shell — the control path and settings persistence.
# Split out so tests can link it without dragging in the window, and so it is
# compiled once rather than once per test binary.
set(SCOPE_SHELL_CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/app/JScopeActions.cpp
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/app/JScopeSettings.cpp)
list(REMOVE_ITEM SCOPE_SHELL_SRC ${SCOPE_SHELL_CORE_SRC})

# PRECOMPILED HEADERS. The framework's core header pulls ~372 files behind it, so
# without this every translation unit spends ~2s on the same soup before
# compiling a line of its own. Two lists deliberately: the headless libraries
# must not pull the widget soup in just to get a PCH.
set(SCOPE_PCH_CORE <j/core/Log.h> <j/core/Signal.h> <j/core/MainThreadDispatcher.h>
                   <string> <vector> <memory> <array> <atomic> <cstdint> <cmath>
                   <optional> <functional> <algorithm> <mutex> <thread>)
set(SCOPE_PCH_UI   ${SCOPE_PCH_CORE} <j/core/JWidget.h> <j/core/JStyle.h>
                   <j/graphics/VectorGraphics.h>)

# OBJECT libraries, not STATIC. Drivers self-register from file-scope
# initialisers (J_REGISTER_SCOPE_DRIVER) and a static archive drops any object
# nothing references — the drivers would silently vanish from the link. Object
# libraries hand every object to the linker. They are also compiled ONCE and
# shared by the app and every test binary; studio-jf measured a 21.7x compile
# duplication factor before it made that change.
add_library(scope_core OBJECT ${SCOPE_USB_SRC} ${SCOPE_HAL_SRC} ${SCOPE_SOURCE_SRC}
                              ${SCOPE_MEASURE_SRC} ${SCOPE_CAPTURE_SRC}
                              ${SCOPE_SHELL_CORE_SRC})
target_include_directories(scope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(scope_core PUBLIC JFramework::platform PkgConfig::LIBUSB
                                        Threads::Threads)
target_precompile_headers(scope_core PRIVATE ${SCOPE_PCH_CORE})

add_library(scope_ui OBJECT ${SCOPE_UI_SRC})
target_include_directories(scope_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(scope_ui PUBLIC JFramework::platform scope_core)
target_precompile_headers(scope_ui PRIVATE ${SCOPE_PCH_UI})

# ---- one executable per instrument ------------------------------------------
function(scope_app name entry driver_src app_dir)
    add_executable(${name} ${entry} ${driver_src} ${SCOPE_SHELL_SRC})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${app_dir})
    target_link_libraries(${name} PRIVATE scope_ui scope_core JFramework::platform
                                          PkgConfig::LIBUSB Threads::Threads)
    # WM_CLASS is an X11 property, so the code that sets it and the library it
    # needs belong to the Linux build alone.
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE xcb)
    endif()
    if(OPENSSL_FOUND)
        target_link_libraries(${name} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
    target_precompile_headers(${name} PRIVATE ${SCOPE_PCH_UI})
endfunction()

scope_app(jscope ${CMAKE_CURRENT_SOURCE_DIR}/apps/scope1008/main.cpp
                 "${SCOPE1008_DRIVER_SRC}" ${CMAKE_CURRENT_SOURCE_DIR}/apps/scope1008)
# No -Wa,-mbig-obj here. Translation units stay small by construction; if that
# flag is ever needed it is a bug report about the structure, not a fix.

# ---- bench probes: real targets that need real hardware ---------------------
# EXCLUDE_FROM_ALL and never add_test: these talk to an instrument, so they can
# neither run on a machine without one nor belong in a suite that must pass
# without one.
function(scope_probe name)
    add_executable(${name} EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/probes/${name}.cpp
                   ${SCOPE1008_DRIVER_SRC})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                               ${CMAKE_CURRENT_SOURCE_DIR}/apps/scope1008)
    target_link_libraries(${name} PRIVATE scope_core JFramework::platform
                                          PkgConfig::LIBUSB Threads::Threads)
    if(OPENSSL_FOUND)
        target_link_libraries(${name} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endfunction()

scope_probe(usb_enum)
scope_probe(hantek_probe)
scope_probe(switch_probe)

# ---- tests: plain int main() + <cassert>, no GTest/Catch2, no hardware -------
function(scope_test name)
    add_executable(${name} EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.cpp
                   ${SCOPE1008_DRIVER_SRC})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                               ${CMAKE_CURRENT_SOURCE_DIR}/tests
                                               ${CMAKE_CURRENT_SOURCE_DIR}/apps/scope1008)
    target_link_libraries(${name} PRIVATE scope_core JFramework::platform
                                          PkgConfig::LIBUSB Threads::Threads)
    if(OPENSSL_FOUND)
        target_link_libraries(${name} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
    add_dependencies(scope_tests ${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

scope_test(test_frame_queue)
scope_test(test_frame_pool)
scope_test(test_decimator)
scope_test(test_registry)
scope_test(test_synthetic_stability)
scope_test(test_actions)
scope_test(test_measurements)
scope_test(test_cursor_model)
scope_test(test_capture_roundtrip)
scope_test(test_replay_driver)
scope_test(test_usb_enumeration)
scope_test(test_session_connect)
scope_test(test_hantek1008_codec)
scope_test(test_hantek1008_protocol)
scope_test(test_driver_close)
scope_test(test_generator)
scope_test(test_squ_file)
scope_test(test_sweep_label)
