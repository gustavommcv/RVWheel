#include <catch2/catch_test_macros.hpp>

#include "LogitechG923HidAutocenter.hpp"

using rvwheel::dal::DeviceInfo;
using rvwheel::devices::IsLogitechG923PsPc;

TEST_CASE("Logitech G923 HID autocenter targeting accepts only exact VID/PID",
          "[Logitech][G923][Autocenter]") {
    DeviceInfo info;

    REQUIRE_FALSE(IsLogitechG923PsPc(info));

    info.vendorId = 0x046D;
    info.productId = 0xC266;
    REQUIRE(IsLogitechG923PsPc(info));

    info.productId = 0xC267;
    REQUIRE_FALSE(IsLogitechG923PsPc(info));

    info.vendorId = 0x1234;
    info.productId = 0xC266;
    REQUIRE_FALSE(IsLogitechG923PsPc(info));
}
