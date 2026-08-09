add_library(rvwheel_launcher_core STATIC
    LauncherCore.cpp
)
target_compile_features(rvwheel_launcher_core PUBLIC cxx_std_20)
target_include_directories(rvwheel_launcher_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(rvwheel_launcher WIN32
    main.cpp
    LauncherApp.cpp
)
target_compile_features(rvwheel_launcher PRIVATE cxx_std_20)
target_link_libraries(rvwheel_launcher PRIVATE rvwheel_launcher_core Shell32 Advapi32)
add_dependencies(rvwheel_launcher rvwheel_device_probe)

if(MSVC)
    target_compile_options(rvwheel_launcher_core PRIVATE /W4 /permissive-)
    target_compile_options(rvwheel_launcher PRIVATE /W4 /permissive-)
endif()

# A build output is directly runnable and already has the layout intended for
# a future zip/release artifact.
add_custom_command(TARGET rvwheel_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:rvwheel_device_probe>
        $<TARGET_FILE_DIR:rvwheel_launcher>/rvwheel_device_probe.exe
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/mods/RVWheel
        $<TARGET_FILE_DIR:rvwheel_launcher>/RVWheel
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/configs/default_profiles
        $<TARGET_FILE_DIR:rvwheel_launcher>/configs/default_profiles
    VERBATIM
)

install(TARGETS rvwheel_launcher rvwheel_device_probe RUNTIME DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/mods/RVWheel DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/configs/default_profiles DESTINATION configs)
