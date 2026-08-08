#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace rvwheel::tools::probe {

// Writes JSON Lines to a ".tmp" sidecar next to the requested final path,
// flushing periodically (not per line) to bound both I/O overhead and data
// loss on an abrupt process kill. On a clean Finalize(), the sidecar is
// flushed, closed, and atomically renamed to the final path.
//
// If the process is killed without calling Finalize(), the ".tmp" sidecar
// is left in place; every line already flushed to it remains individually
// valid JSON, even though the file as a whole was never renamed to its
// final name. That is the documented, honest limit of "safe capture" for
// a tool with no crash-recovery journal -- a clean Ctrl+C, which this tool
// handles, still finalizes normally.
class CaptureWriter {
public:
    explicit CaptureWriter(std::filesystem::path finalPath);
    ~CaptureWriter();

    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    [[nodiscard]] bool IsOpen() const noexcept;

    void WriteLine(const std::string& jsonLine);

    // Flushes the underlying stream only if at least `interval` has
    // elapsed since the last flush; cheap to call on every sample.
    void FlushIfDue(std::chrono::steady_clock::time_point now, std::chrono::milliseconds interval);

    // Flushes, closes, and renames the sidecar to its final path. Safe to
    // call at most once; a second call is a no-op.
    void Finalize();

    [[nodiscard]] const std::filesystem::path& FinalPath() const noexcept { return finalPath_; }

private:
    std::filesystem::path finalPath_;
    std::filesystem::path tempPath_;
    std::ofstream stream_;
    std::chrono::steady_clock::time_point lastFlush_{};
    bool finalized_ = false;
};

} // namespace rvwheel::tools::probe
