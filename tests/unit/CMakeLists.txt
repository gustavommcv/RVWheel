find_package(Catch2 3 CONFIG QUIET)

if(NOT Catch2_FOUND)
    message(STATUS
        "RVWheel: Catch2 3 not found via find_package; unit tests will not be built. "
        "Install it (e.g. 'vcpkg install catch2') or point CMAKE_PREFIX_PATH at an existing install, "
        "then reconfigure.")
    return()
endif()

# Provided by Catch2's own package config; adds catch_discover_tests().
include(Catch)

# The device_probe tool's pure/hardware-free components (rvwheel_device_probe_core)
# are only available when RVWHEEL_BUILD_TOOLS built that target before this
# directory was processed. Tests for it are additive and never required.
set(_rvwheel_probe_test_sources "")
if(TARGET rvwheel_device_probe_core)
    list(APPEND _rvwheel_probe_test_sources
        DeviceProbeParsingTests.cpp
        DeviceProbeFormattingTests.cpp
    )
endif()

add_executable(rvwheel_dal_tests
    AxisNormalizerTests.cpp
    AxisSourceTests.cpp
    DeviceManagerTests.cpp
    DeviceReadinessTests.cpp
    LogitechDeviceTests.cpp
    ProfileLoaderTests.cpp
    ProfileRepositoryTests.cpp
    ProfileResolverTests.cpp
    Win32HeaderSmokeTest.cpp
    ${_rvwheel_probe_test_sources}
)

target_include_directories(rvwheel_dal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(rvwheel_dal_tests PRIVATE cxx_std_20)
# rvwheel::profiles (unlike rvwheel_device_probe_core) is always built --
# see the top-level CMakeLists.txt's unconditional add_subdirectory(src/Profiles)
# -- so profile system tests link it directly rather than through the tool.
target_link_libraries(rvwheel_dal_tests PRIVATE rvwheel::dal rvwheel::profiles Catch2::Catch2WithMain)

if(TARGET rvwheel_device_probe_core)
    target_link_libraries(rvwheel_dal_tests PRIVATE rvwheel_device_probe_core)
    target_sources(rvwheel_dal_tests PRIVATE DeviceProbeCalibrationTests.cpp)
endif()

if(MSVC)
    target_compile_options(rvwheel_dal_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(rvwheel_dal_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

catch_discover_tests(rvwheel_dal_tests)
