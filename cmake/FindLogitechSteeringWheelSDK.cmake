#[=======================================================================[.rst:
FindLogitechSteeringWheelSDK
-----------------------------

Locates a user-provided installation of the Logitech Gaming SDK (Steering
Wheel SDK). This module NEVER downloads, bundles, or redistributes the SDK
in any way -- it only looks at cache variables the user (or the build
environment) sets to point at their own, separately-obtained copy.

Cache variables
^^^^^^^^^^^^^^^

``RVWHEEL_LOGITECH_SDK_INCLUDE_DIR``
  Directory containing the SDK's public header(s). Not set by default.

``RVWHEEL_LOGITECH_SDK_LIBRARY``
  Path to the import library (.lib) to link against. Not set by default.

``RVWHEEL_LOGITECH_SDK_RUNTIME_DLL``
  Optional path to the runtime DLL, for local packaging/testing
  convenience only (e.g. copying next to a test executable). Not required
  for the library target itself to link.

Result variables
^^^^^^^^^^^^^^^^^

``LogitechSteeringWheelSDK_FOUND``
  True if both the include directory and the import library were found.

Imported targets
^^^^^^^^^^^^^^^^^

``LogitechSteeringWheelSDK::LogitechSteeringWheelSDK``
  Interface + import library target, available when found.
#]=======================================================================]

set(RVWHEEL_LOGITECH_SDK_INCLUDE_DIR "" CACHE PATH
    "Directory containing the Logitech Gaming SDK public header(s). Obtain this SDK directly from Logitech; it is not redistributed by this project.")
set(RVWHEEL_LOGITECH_SDK_LIBRARY "" CACHE FILEPATH
    "Path to the Logitech Gaming SDK import library (.lib) to link against. Obtain this SDK directly from Logitech; it is not redistributed by this project.")
set(RVWHEEL_LOGITECH_SDK_RUNTIME_DLL "" CACHE FILEPATH
    "Optional path to the Logitech Gaming SDK runtime DLL, used only for local packaging/testing convenience.")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LogitechSteeringWheelSDK
    REQUIRED_VARS RVWHEEL_LOGITECH_SDK_INCLUDE_DIR RVWHEEL_LOGITECH_SDK_LIBRARY
    FAIL_MESSAGE
        "RVWHEEL_ENABLE_LOGITECH_SDK is ON, but the Logitech Gaming SDK was not found. This project does not download or redistribute the SDK: obtain it directly from Logitech and set -DRVWHEEL_LOGITECH_SDK_INCLUDE_DIR=<path to headers> and -DRVWHEEL_LOGITECH_SDK_LIBRARY=<path to .lib> to your own installation."
)

if(LogitechSteeringWheelSDK_FOUND AND NOT TARGET LogitechSteeringWheelSDK::LogitechSteeringWheelSDK)
    add_library(LogitechSteeringWheelSDK::LogitechSteeringWheelSDK UNKNOWN IMPORTED)
    set_target_properties(LogitechSteeringWheelSDK::LogitechSteeringWheelSDK PROPERTIES
        IMPORTED_LOCATION "${RVWHEEL_LOGITECH_SDK_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${RVWHEEL_LOGITECH_SDK_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(RVWHEEL_LOGITECH_SDK_INCLUDE_DIR RVWHEEL_LOGITECH_SDK_LIBRARY RVWHEEL_LOGITECH_SDK_RUNTIME_DLL)
