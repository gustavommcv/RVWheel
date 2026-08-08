#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace rvwheel::dal {

// Small, explicit result code shared by every DAL contract that can fail
// (Poll(), ApplyForceFeedback(), axis normalization, device initialization).
// The DAL never throws across its public boundary; every failure mode is
// represented here instead.
enum class StatusCode : std::uint8_t {
    Success = 0,
    NotConnected,    // The device is not currently connected/acquired.
    NotSupported,    // The requested capability is not supported by this device/backend.
    InvalidArgument, // The caller supplied invalid/degenerate input (e.g. calibration data).
    BackendError,    // The underlying driver/SDK reported an error.
};

// Lightweight status/result type. Ok() is the hot-path default and never
// allocates. Error variants carry an optional diagnostic message; since
// std::string has a small-buffer optimization, short messages also avoid
// allocation, but this is not guaranteed for longer ones. Error paths are
// expected to be rare (disconnects, unsupported features), not per-frame.
class Status {
public:
    Status() noexcept = default;

    [[nodiscard]] static Status Ok() noexcept { return Status{}; }

    [[nodiscard]] static Status NotConnected(std::string message = {}) noexcept {
        return Status{StatusCode::NotConnected, std::move(message)};
    }

    [[nodiscard]] static Status NotSupported(std::string message = {}) noexcept {
        return Status{StatusCode::NotSupported, std::move(message)};
    }

    [[nodiscard]] static Status InvalidArgument(std::string message = {}) noexcept {
        return Status{StatusCode::InvalidArgument, std::move(message)};
    }

    [[nodiscard]] static Status BackendError(std::string message = {}) noexcept {
        return Status{StatusCode::BackendError, std::move(message)};
    }

    [[nodiscard]] bool IsOk() const noexcept { return code_ == StatusCode::Success; }
    explicit operator bool() const noexcept { return IsOk(); }

    [[nodiscard]] StatusCode Code() const noexcept { return code_; }
    [[nodiscard]] const std::string& Message() const noexcept { return message_; }

private:
    Status(StatusCode code, std::string message) noexcept : code_(code), message_(std::move(message)) {}

    StatusCode code_ = StatusCode::Success;
    std::string message_;
};

} // namespace rvwheel::dal
