# Separate from rvwheel_dal on purpose (see ForceFeedbackEngine.hpp): the
# vehicle-facing math here must never depend on DIEFFECT or any other
# DirectInput type, and the DAL must never depend on this library. The only
# thing this library takes from rvwheel_dal is the already
# backend-agnostic IWheelDevice/ForceFeedbackCommand contract.
add_library(rvwheel_ffb STATIC
    src/ForceFeedbackMixer.cpp
    src/ForceFeedbackSafetyController.cpp
    src/ForceFeedbackEngine.cpp
    src/SpringDamperSource.cpp
)
add_library(rvwheel::ffb ALIAS rvwheel_ffb)

target_compile_features(rvwheel_ffb PUBLIC cxx_std_20)

target_include_directories(rvwheel_ffb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(rvwheel_ffb PUBLIC rvwheel::dal)

if(MSVC)
    target_compile_options(rvwheel_ffb PRIVATE /W4 /permissive-)
else()
    target_compile_options(rvwheel_ffb PRIVATE -Wall -Wextra -Wpedantic)
endif()
