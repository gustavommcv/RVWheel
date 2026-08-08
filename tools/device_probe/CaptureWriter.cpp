#include "CaptureWriter.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rvwheel::tools::probe {

namespace {
std::filesystem::path MakeTempPath(const std::filesystem::path& finalPath) {
    std::filesystem::path temp = finalPath;
    temp += L".tmp";
    return temp;
}
} // namespace

CaptureWriter::CaptureWriter(std::filesystem::path finalPath)
    : finalPath_(std::move(finalPath)),
      tempPath_(MakeTempPath(finalPath_)),
      stream_(tempPath_, std::ios::out | std::ios::trunc),
      lastFlush_(std::chrono::steady_clock::now()) {}

CaptureWriter::~CaptureWriter() {
    if (!finalized_ && stream_.is_open()) {
        stream_.flush();
        stream_.close();
        // Deliberately not renamed here: a destructor running because of
        // an exception or an early return is exactly the case this class
        // documents as leaving the ".tmp" sidecar in place with
        // valid-but-unfinalized content.
    }
}

bool CaptureWriter::IsOpen() const noexcept { return stream_.is_open(); }

void CaptureWriter::WriteLine(const std::string& jsonLine) { stream_ << jsonLine << '\n'; }

void CaptureWriter::FlushIfDue(std::chrono::steady_clock::time_point now, std::chrono::milliseconds interval) {
    if (now - lastFlush_ >= interval) {
        stream_.flush();
        lastFlush_ = now;
    }
}

void CaptureWriter::Finalize() {
    if (finalized_) {
        return;
    }
    finalized_ = true;

    stream_.flush();
    stream_.close();

    MoveFileExW(tempPath_.c_str(), finalPath_.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace rvwheel::tools::probe
