#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>

#include "LogitechG923HidAutocenter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rvwheel::devices {
namespace {

constexpr USHORT kLogitechVendorId = 0x046D;
constexpr USHORT kG923PsPcProductId = 0xC266;
constexpr USAGE kJoystickUsagePage = 0x0001;
constexpr USAGE kJoystickUsage = 0x0004;
constexpr USHORT kOutputReportBytes = 17;
constexpr USHORT kOutputPayloadBytes = 16;

struct DeviceInfoSetCloser {
    void operator()(void* handle) const noexcept {
        if (handle != INVALID_HANDLE_VALUE) {
            SetupDiDestroyDeviceInfoList(static_cast<HDEVINFO>(handle));
        }
    }
};

struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(static_cast<HANDLE>(handle));
        }
    }
};

struct PreparsedDataCloser {
    void operator()(_HIDP_PREPARSED_DATA* data) const noexcept {
        if (data != nullptr) {
            HidD_FreePreparsedData(data);
        }
    }
};

using UniqueDeviceInfoSet = std::unique_ptr<void, DeviceInfoSetCloser>;
using UniqueHandle = std::unique_ptr<void, HandleCloser>;
using UniquePreparsedData = std::unique_ptr<_HIDP_PREPARSED_DATA, PreparsedDataCloser>;

[[nodiscard]] bool HasExactControlLayout(HANDLE handle, const HIDD_ATTRIBUTES& attributes) {
    if (attributes.VendorID != kLogitechVendorId || attributes.ProductID != kG923PsPcProductId) {
        return false;
    }

    PHIDP_PREPARSED_DATA rawPreparsed = nullptr;
    if (!HidD_GetPreparsedData(handle, &rawPreparsed)) {
        return false;
    }
    UniquePreparsedData preparsed(rawPreparsed);
    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed.get(), &caps) != HIDP_STATUS_SUCCESS || caps.UsagePage != kJoystickUsagePage ||
        caps.Usage != kJoystickUsage || caps.OutputReportByteLength != kOutputReportBytes ||
        caps.NumberOutputValueCaps != 1) {
        return false;
    }

    USHORT count = caps.NumberOutputValueCaps;
    HIDP_VALUE_CAPS valueCaps{};
    return HidP_GetValueCaps(HidP_Output, &valueCaps, &count, preparsed.get()) == HIDP_STATUS_SUCCESS && count == 1 &&
           valueCaps.ReportID == 0 && valueCaps.UsagePage == 0xFF01 && valueCaps.BitSize == 8 &&
           valueCaps.ReportCount == kOutputPayloadBytes;
}

[[nodiscard]] rvwheel::dal::Status FindExactControlPath(std::wstring& outPath) {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    UniqueDeviceInfoSet devices(
        SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (devices.get() == INVALID_HANDLE_VALUE) {
        return rvwheel::dal::Status::BackendError("SetupDiGetClassDevsW failed: " + std::to_string(GetLastError()));
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &hidGuid, index, &interfaceData)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                return rvwheel::dal::Status::BackendError("HID enumeration failed: " + std::to_string(error));
            }
            break;
        }

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.get(), &interfaceData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<std::byte> detailStorage(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailStorage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(
                devices.get(), &interfaceData, detail, requiredSize, nullptr, nullptr)) {
            continue;
        }

        UniqueHandle handle(CreateFileW(detail->DevicePath,
                                        0,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        0,
                                        nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) {
            continue;
        }
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(handle.get(), &attributes) || !HasExactControlLayout(handle.get(), attributes)) {
            continue;
        }
        if (!outPath.empty()) {
            return rvwheel::dal::Status::BackendError(
                "More than one exact G923 control collection was found; refusing an ambiguous write");
        }
        outPath = detail->DevicePath;
    }

    return outPath.empty() ? rvwheel::dal::Status::NotConnected("Exact Logitech G923 PS/PC HID collection not found")
                           : rvwheel::dal::Status::Ok();
}

} // namespace

bool IsLogitechG923PsPc(const rvwheel::dal::DeviceInfo& info) noexcept {
    return info.vendorId == kLogitechVendorId && info.productId == kG923PsPcProductId;
}

rvwheel::dal::Status SetLogitechG923AutocenterEnabled(bool enabled) noexcept {
    try {
        std::wstring path;
        const rvwheel::dal::Status findStatus = FindExactControlPath(path);
        if (!findStatus) {
            return findStatus;
        }

        UniqueHandle handle(CreateFileW(path.c_str(),
                                        GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        0,
                                        nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) {
            return rvwheel::dal::Status::BackendError("Failed to open exact G923 HID collection for write: " +
                                                      std::to_string(GetLastError()));
        }

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(handle.get(), &attributes) || !HasExactControlLayout(handle.get(), attributes)) {
            return rvwheel::dal::Status::BackendError(
                "G923 identity/layout changed between discovery and write; report refused");
        }

        const std::array<unsigned char, 7> command =
            enabled ? std::array<unsigned char, 7>{0x14, 0, 0, 0, 0, 0, 0}
                    : std::array<unsigned char, 7>{0xF5, 0, 0, 0, 0, 0, 0};
        std::array<unsigned char, kOutputReportBytes> report{};
        std::copy(command.begin(), command.end(), report.begin() + 1);
        DWORD bytesWritten = 0;
        const BOOL succeeded = WriteFile(
            handle.get(), report.data(), static_cast<DWORD>(report.size()), &bytesWritten, nullptr);
        if (!succeeded || bytesWritten != static_cast<DWORD>(report.size())) {
            const DWORD error = succeeded ? ERROR_WRITE_FAULT : GetLastError();
            return rvwheel::dal::Status::BackendError("G923 autocenter HID WriteFile failed: " +
                                                      std::to_string(error) + ", bytes=" +
                                                      std::to_string(bytesWritten));
        }
        return rvwheel::dal::Status::Ok();
    } catch (...) {
        return rvwheel::dal::Status::BackendError("Unexpected failure while controlling G923 autocenter");
    }
}

} // namespace rvwheel::devices
