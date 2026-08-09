# Minimum version rationale: 3.21 gives us PROJECT_IS_TOP_LEVEL (used below
# to default RVWHEEL_BUILD_TESTS the same way most modern CMake projects
# do) plus robust generator-expression/target-based ergonomics used
# throughout src/CMakeLists.txt. Nothing here needs a newer policy set.
cmake_minimum_required(VERSION 3.21)

# Must run BEFORE project(): when configuring through vcpkg's toolchain
# file, manifest dependencies are installed during the project() call, and
# which optional manifest features get installed is controlled by
# VCPKG_MANIFEST_FEATURES at that point -- RVWHEEL_BUILD_TESTS as an
# option() (declared below, after project()) does not exist yet. Reading
# the plain variable/cache value here still sees an explicit
# -DRVWHEEL_BUILD_TESTS=... from the command line (CMake populates the
# cache before any listfile code runs), so this only guesses when the user
# passed nothing at all, in which case defaulting to "request tests" makes
# a first-time `cmake -S . -B build` with no flags just work without a
# separate `vcpkg install`.
if(NOT DEFINED RVWHEEL_BUILD_TESTS OR RVWHEEL_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(RVWheel
    VERSION 0.1.0
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR
        "RVWheel's Device Abstraction Layer targets Windows x64 (DirectInput 8 / Logitech Gaming SDK). "
        "The pure, hardware-independent pieces (AxisNormalizer, WheelTypes, Status) have no Windows "
        "dependency in their own source, but this top-level build does not currently offer a way to "
        "build just those in isolation on another platform. Configure this project on Windows.")
endif()

option(RVWHEEL_ENABLE_LOGITECH_SDK "Enable the Logitech Gaming SDK backend (requires a user-supplied SDK installation; OFF builds DirectInput-only)" OFF)
option(RVWHEEL_BUILD_TESTS "Build RVWheel unit tests" ${PROJECT_IS_TOP_LEVEL})
option(RVWHEEL_BUILD_TOOLS "Build RVWheel developer tools (device_probe, etc.)" ${PROJECT_IS_TOP_LEVEL})

add_subdirectory(src)
add_subdirectory(src/Profiles)

# Tools before tests: tests/unit conditionally links against
# rvwheel_device_probe_core (its pure, hardware-free components) when that
# target exists, so it must already be defined by the time that
# subdirectory is processed.
if(RVWHEEL_BUILD_TOOLS)
    add_subdirectory(tools/device_probe)
    add_subdirectory(tools/launcher)
endif()

if(RVWHEEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()
