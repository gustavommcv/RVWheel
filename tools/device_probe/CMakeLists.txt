# Split into a pure "core" static library (parsing, formatting, JSONL
# serialization, device-selection policy -- no Win32 windowing, no console
# I/O) and the actual console executable. This lets tests/unit link only
# the core lib, without duplicating main() or depending on a hidden window
# / console being creatable in a test run.
add_library(rvwheel_device_probe_core STATIC
    CalibrationWizard.cpp
    CliOptions.cpp
    DeviceSelection.cpp
    JsonlFormatter.cpp
    MonitorFrameFormatter.cpp
    ProbeFormatting.cpp
)

target_compile_features(rvwheel_device_probe_core PUBLIC cxx_std_20)
target_include_directories(rvwheel_device_probe_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# PUBLIC: ProbeFormatting.hpp (a public header of this target) includes
# rvwheel/profiles/ProfileResolver.hpp to format ProfileOrigin, so anything
# linking this target's headers needs rvwheel::profiles too.
target_link_libraries(rvwheel_device_probe_core PUBLIC rvwheel::dal rvwheel::profiles)

if(MSVC)
    target_compile_options(rvwheel_device_probe_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rvwheel_device_probe_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rvwheel_device_probe
    main.cpp
    CaptureWriter.cpp
    ConsoleRenderer.cpp
    DeviceProbeApp.cpp
    HiddenWindow.cpp
    ProfileLocations.cpp
)

target_link_libraries(rvwheel_device_probe PRIVATE rvwheel_device_probe_core rvwheel::dal rvwheel::profiles)

if(MSVC)
    target_compile_options(rvwheel_device_probe PRIVATE /W4 /permissive-)
else()
    target_compile_options(rvwheel_device_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()
