#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>
#include <tlhelp32.h>

#include "LogitechHidInspector.hpp"
#include "LogitechG923HidAutocenter.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cwchar>
#include <iomanip>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace rvwheel::tools::probe {
namespace {

constexpr USHORT kLogitechVendorId = 0x046D;

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

[[nodiscard]] bool IsAnotherDeviceProbeRunning() noexcept {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        // Fail closed: a real HID write must not proceed if we cannot prove
        // that the bridge/another diagnostic is absent.
        return true;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(static_cast<HANDLE>(snapshot.get()), &entry)) {
        return true;
    }

    const DWORD currentProcessId = GetCurrentProcessId();
    do {
        if (entry.th32ProcessID != currentProcessId &&
            _wcsicmp(entry.szExeFile, L"rvwheel_device_probe.exe") == 0) {
            return true;
        }
    } while (Process32NextW(static_cast<HANDLE>(snapshot.get()), &entry));
    return false;
}

[[nodiscard]] std::wstring ReadHidString(HANDLE handle, BOOLEAN(__stdcall* reader)(HANDLE, PVOID, ULONG)) {
    wchar_t buffer[256]{};
    if (!reader(handle, buffer, sizeof(buffer))) {
        return {};
    }
    return buffer;
}

[[nodiscard]] std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return "(UTF-16 conversion failed)";
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            WC_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            result.data(),
                            required,
                            nullptr,
                            nullptr) != required) {
        return "(UTF-16 conversion failed)";
    }
    return result;
}

[[nodiscard]] std::set<unsigned int> ReadOutputReportIds(PHIDP_PREPARSED_DATA preparsed, const HIDP_CAPS& caps) {
    std::set<unsigned int> ids;

    if (caps.NumberOutputButtonCaps > 0) {
        USHORT count = caps.NumberOutputButtonCaps;
        std::vector<HIDP_BUTTON_CAPS> buttonCaps(count);
        if (HidP_GetButtonCaps(HidP_Output, buttonCaps.data(), &count, preparsed) == HIDP_STATUS_SUCCESS) {
            for (USHORT i = 0; i < count; ++i) {
                ids.insert(buttonCaps[i].ReportID);
            }
        }
    }

    if (caps.NumberOutputValueCaps > 0) {
        USHORT count = caps.NumberOutputValueCaps;
        std::vector<HIDP_VALUE_CAPS> valueCaps(count);
        if (HidP_GetValueCaps(HidP_Output, valueCaps.data(), &count, preparsed) == HIDP_STATUS_SUCCESS) {
            for (USHORT i = 0; i < count; ++i) {
                ids.insert(valueCaps[i].ReportID);
            }
        }
    }

    return ids;
}

void PrintOutputValueCaps(std::ostream& output, PHIDP_PREPARSED_DATA preparsed, const HIDP_CAPS& caps) {
    if (caps.NumberOutputValueCaps == 0) {
        return;
    }
    USHORT count = caps.NumberOutputValueCaps;
    std::vector<HIDP_VALUE_CAPS> valueCaps(count);
    if (HidP_GetValueCaps(HidP_Output, valueCaps.data(), &count, preparsed) != HIDP_STATUS_SUCCESS) {
        output << "  value caps   unavailable\n";
        return;
    }
    for (USHORT i = 0; i < count; ++i) {
        const auto& value = valueCaps[i];
        output << "  value cap #" << (i + 1) << " reportId=0x" << std::hex << std::uppercase << std::setw(2)
               << std::setfill('0') << static_cast<unsigned int>(value.ReportID) << " usagePage=0x" << std::setw(4)
               << value.UsagePage << std::dec << std::setfill(' ') << " bitSize=" << value.BitSize
               << " reportCount=" << value.ReportCount << " logical=" << value.LogicalMin << ".."
               << value.LogicalMax << "\n";
    }
}

void PrintReportIds(std::ostream& output, const std::set<unsigned int>& ids) {
    if (ids.empty()) {
        output << "(none declared by button/value caps)";
        return;
    }
    bool first = true;
    for (const unsigned int id : ids) {
        if (!first) {
            output << ", ";
        }
        output << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << id << std::dec;
        first = false;
    }
    output << std::setfill(' ');
}

} // namespace

int InspectLogitechHidDevices(std::ostream& output, std::ostream& errors) {
    output << "RVWheel Logitech HID capability inspection (READ ONLY)\n"
              "Handles use desiredAccess=0. No input, output, or feature report is sent.\n\n";

    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    UniqueDeviceInfoSet devices(
        SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (devices.get() == INVALID_HANDLE_VALUE) {
        errors << "SetupDiGetClassDevsW failed (Win32 " << GetLastError() << ").\n";
        return 1;
    }

    std::size_t logitechCount = 0;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &hidGuid, index, &interfaceData)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                errors << "SetupDiEnumDeviceInterfaces failed at index " << index << " (Win32 " << error << ").\n";
                return 1;
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
        SP_DEVINFO_DATA deviceData{};
        deviceData.cbSize = sizeof(deviceData);
        if (!SetupDiGetDeviceInterfaceDetailW(
                devices.get(), &interfaceData, detail, requiredSize, nullptr, &deviceData)) {
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
        if (!HidD_GetAttributes(handle.get(), &attributes) || attributes.VendorID != kLogitechVendorId) {
            continue;
        }
        ++logitechCount;

        PHIDP_PREPARSED_DATA rawPreparsed = nullptr;
        if (!HidD_GetPreparsedData(handle.get(), &rawPreparsed)) {
            errors << "A Logitech HID collection was found, but HidD_GetPreparsedData failed (Win32 "
                   << GetLastError() << ").\n";
            continue;
        }
        UniquePreparsedData preparsed(rawPreparsed);
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed.get(), &caps) != HIDP_STATUS_SUCCESS) {
            errors << "A Logitech HID collection was found, but HidP_GetCaps failed.\n";
            continue;
        }

        output << "Collection #" << logitechCount << "\n"
               << "  product      " << WideToUtf8(ReadHidString(handle.get(), &HidD_GetProductString)) << "\n"
               << "  manufacturer " << WideToUtf8(ReadHidString(handle.get(), &HidD_GetManufacturerString)) << "\n"
               << "  VID/PID      0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
               << attributes.VendorID << "/0x" << std::setw(4) << attributes.ProductID << std::dec
               << std::setfill(' ') << "\n"
               << "  version      0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
               << attributes.VersionNumber << std::dec << std::setfill(' ') << "\n"
               << "  usage        page=0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
               << caps.UsagePage << " usage=0x" << std::setw(4) << caps.Usage << std::dec << std::setfill(' ')
               << "\n"
               << "  report bytes input=" << caps.InputReportByteLength
               << " output=" << caps.OutputReportByteLength << " feature=" << caps.FeatureReportByteLength << "\n"
               << "  output caps  buttons=" << caps.NumberOutputButtonCaps
               << " values=" << caps.NumberOutputValueCaps << " reportIds=";
        PrintReportIds(output, ReadOutputReportIds(preparsed.get(), caps));
        output << "\n";
        PrintOutputValueCaps(output, preparsed.get(), caps);
        output << "  device path  " << WideToUtf8(detail->DevicePath) << "\n\n";
    }

    if (logitechCount == 0) {
        errors << "No present Logitech HID collections (VID 0x046D) were readable.\n";
        return 1;
    }

    output << "Found " << logitechCount << " Logitech HID collection(s). No reports were sent.\n";
    return 0;
}

int RunLogitechG923AutocenterHardwareTest(std::atomic<bool>& stopRequested,
                                          std::ostream& output,
                                          std::ostream& errors) {
    constexpr auto kTestDuration = std::chrono::seconds{5};

    if (IsAnotherDeviceProbeRunning()) {
        errors << "Refusing the G923 HID write because another rvwheel_device_probe process is running.\n"
                  "Stop the bridge/other diagnostic first; raw HID autocenter writes must never compete with "
                  "an active DirectInput FFB session. Nothing was sent.\n";
        return 1;
    }

    output << "RVWheel REAL Logitech G923 autocenter hardware test\n"
              "Target is locked to VID 046D / PID C266 and the verified 16-byte output payload.\n"
              "No DirectInput effect is created. The firmware autocenter is disabled for 5 seconds, then restored.\n";

    output << "Sending Logitech autocenter OFF now via HID WriteFile...\n";
    const rvwheel::dal::Status disableStatus = rvwheel::devices::SetLogitechG923AutocenterEnabled(false);
    if (!disableStatus) {
        // A failed user-mode write normally means the device rejected the
        // report, but the API cannot prove the firmware did not observe it.
        // Always make a best-effort restore before returning.
        errors << "Autocenter OFF failed: " << disableStatus.Message()
               << "\nOFF was not confirmed; attempting an immediate best-effort restore.\n";
        const rvwheel::dal::Status restoreStatus = rvwheel::devices::SetLogitechG923AutocenterEnabled(true);
        if (!restoreStatus) {
            errors << "Best-effort restore failed: " << restoreStatus.Message() << "\n";
            errors << "RESTORE IS UNCONFIRMED. Keep clear of the wheel, reconnect its USB cable, and restart G HUB.\n";
        }
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + kTestDuration;
    while (!stopRequested.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }

    output << "Sending Logitech autocenter ON restore now...\n";
    const rvwheel::dal::Status restoreStatus = rvwheel::devices::SetLogitechG923AutocenterEnabled(true);
    if (!restoreStatus) {
        errors << "Autocenter restore failed: " << restoreStatus.Message() << "\n";
        errors << "RESTORE IS UNCONFIRMED. Keep clear of the wheel, reconnect its USB cable, and restart G HUB.\n";
        return 2;
    }

    output << "Restore command accepted. Test finished; no DirectInput effect was created.\n";
    return 0;
}

} // namespace rvwheel::tools::probe
