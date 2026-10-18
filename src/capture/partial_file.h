#pragma once
#include <windows.h>
#include <filesystem>
namespace PhotoCapture {
// Owns cleanup only after the caller successfully reserves its own temporary file.
class PartialFile {
  public:
    explicit PartialFile(const std::filesystem::path &path) noexcept : path_(path) {}
    PartialFile(const PartialFile &) = delete;
    PartialFile &operator=(const PartialFile &) = delete;
    ~PartialFile() {
        if (!published_)
            DeleteFileW(path_.c_str());
    }
    void published() noexcept {
        published_ = true;
    }

  private:
    const std::filesystem::path &path_;
    bool published_ = false;
};
} // namespace PhotoCapture
