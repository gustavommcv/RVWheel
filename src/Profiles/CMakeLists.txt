# Separate from rvwheel_dal on purpose: the DAL never depends on a JSON
# library (see src/Core/include/rvwheel/dal/WheelInputLayout.hpp), while
# this profile system's whole job is parsing/validating JSON into the
# DAL's backend-agnostic config types. Depends on rvwheel::dal for those
# shared types (DeviceInfo, WheelInputLayout, AxisSource, ...), never the
# other way around.
find_package(nlohmann_json CONFIG REQUIRED)

add_library(rvwheel_profiles STATIC
    src/ProfileLoader.cpp
    src/ProfileRepository.cpp
    src/ProfileResolver.cpp
)
add_library(rvwheel::profiles ALIAS rvwheel_profiles)

target_compile_features(rvwheel_profiles PUBLIC cxx_std_20)

target_include_directories(rvwheel_profiles
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(rvwheel_profiles
    PUBLIC
        rvwheel::dal
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(rvwheel_profiles PRIVATE /W4 /permissive-)
else()
    target_compile_options(rvwheel_profiles PRIVATE -Wall -Wextra -Wpedantic)
endif()
