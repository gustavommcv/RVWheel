add_library(rvwheel_dal STATIC)
add_library(rvwheel::dal ALIAS rvwheel_dal)

target_compile_features(rvwheel_dal PUBLIC cxx_std_20)

target_sources(rvwheel_dal
    PRIVATE
        Core/src/AxisNormalizer.cpp
        Core/src/AxisSource.cpp
        Core/src/DeviceManager.cpp
        Core/src/DeviceManagerFactory.cpp
        Devices/DirectInput/src/DirectInputDevice.cpp
        Devices/DirectInput/src/DirectInputDeviceEnumerator.cpp
        Devices/Logitech/src/LogitechDevice.cpp
        Devices/Logitech/src/LogitechDeviceEnumerator.cpp
)

target_include_directories(rvwheel_dal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Devices/DirectInput/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Devices/Logitech/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Backend-internal headers (enumerators, the Logitech vendor
        # adapter) live alongside their .cpp files rather than under
        # include/, since nothing outside each backend needs them.
        Devices/DirectInput/src
        Devices/Logitech/src
)

if(MSVC)
    # Strict but not fatal: external SDK headers (dinput.h and, when
    # enabled, the Logitech SDK) may warn under /W4 on toolchains we don't
    # control, and those warnings must not break the build. /permissive-
    # only affects our own conformance, not third-party headers' warnings.
    target_compile_options(rvwheel_dal PRIVATE /W4 /permissive-)
else()
    target_compile_options(rvwheel_dal PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(rvwheel_dal PRIVATE dinput8 dxguid)

if(RVWHEEL_ENABLE_LOGITECH_SDK)
    list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
    find_package(LogitechSteeringWheelSDK MODULE REQUIRED)

    target_sources(rvwheel_dal PRIVATE Devices/Logitech/src/LogitechGamingSdkAdapter.cpp)
    target_link_libraries(rvwheel_dal PRIVATE LogitechSteeringWheelSDK::LogitechSteeringWheelSDK)
    # PRIVATE: this is an internal build-time switch consumed only by this
    # library's own .cpp files (DeviceManagerFactory.cpp); it must not leak
    # into consumers, who only ever see the backend-agnostic IWheelDevice.
    target_compile_definitions(rvwheel_dal PRIVATE RVWHEEL_ENABLE_LOGITECH_SDK=1)

    message(STATUS "RVWheel: Logitech Gaming SDK backend enabled "
                    "(include: ${RVWHEEL_LOGITECH_SDK_INCLUDE_DIR}, lib: ${RVWHEEL_LOGITECH_SDK_LIBRARY}). "
                    "Note: the vendor-calling adapter is currently a documented skeleton -- see "
                    "src/Devices/Logitech/src/LogitechGamingSdkAdapter.cpp.")
else()
    message(STATUS "RVWheel: Logitech Gaming SDK backend disabled (RVWHEEL_ENABLE_LOGITECH_SDK=OFF); building DirectInput-only.")
endif()
