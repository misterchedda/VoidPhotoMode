#include "core/runtime.h"
#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <fstream>
#include <mutex>
#include <array>
#include <vector>
namespace Runtime {
namespace {
std::filesystem::path photoPath, settingsPath;
std::ofstream logfile;
std::mutex logMutex;
std::filesystem::path known(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw)))
        return {};
    std::filesystem::path p(raw);
    CoTaskMemFree(raw);
    return p;
}
} // namespace
bool initialize(std::string &error) {
    try {
        auto local = known(FOLDERID_LocalAppData), pictures = known(FOLDERID_Pictures);
        if (local.empty() || pictures.empty()) {
            error = "Windows user folders unavailable";
            return false;
        }
        settingsPath = local / L"Dishonored2PhotoMode";
        std::filesystem::create_directories(settingsPath);
        auto previous = settingsPath / L"photo-mode.previous.log", current = settingsPath / L"photo-mode.log";
        std::error_code ec;
        if (std::filesystem::exists(current, ec)) {
            std::filesystem::remove(previous, ec);
            ec.clear();
            std::filesystem::rename(current, previous, ec);
        }
        logfile.open(current, std::ios::trunc);
        if (!logfile) {
            error = "Cannot open photo-mode log";
            return false;
        }
        auto config = settingsPath / L"settings.ini";
        if (!std::filesystem::exists(config)) {
            std::ofstream out(config);
            out << "; Optional absolute output folder. Leave empty to use your Windows Pictures "
                   "folder.\n[PhotoMode]\nCaptureDirectory=\n";
        }
        wchar_t configured[32768]{};
        GetPrivateProfileStringW(L"PhotoMode", L"CaptureDirectory", L"", configured, 32768, config.c_str());
        photoPath = pictures / L"Dishonored 2" / L"Photo Mode";
        if (configured[0]) {
            wchar_t expanded[32768]{};
            DWORD n = ExpandEnvironmentStringsW(configured, expanded, 32768);
            if (!n || n > 32768 || !std::filesystem::path(expanded).is_absolute())
                log("Invalid CaptureDirectory; using Windows Pictures folder");
            else
                photoPath = std::filesystem::path(expanded).lexically_normal();
        }
        log("Photos: " + photoPath.u8string());
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
const std::filesystem::path &photos() {
    return photoPath;
}
const std::filesystem::path &settings() {
    return settingsPath;
}
void log(const std::string &message) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logfile)
        logfile << GetTickCount64() << " " << message << std::endl;
}
bool supportedExecutable() {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    if (!n || n >= 32768)
        return false;
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input)
        return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::vector<unsigned char> buffer(1024 * 1024);
    std::array<unsigned char, 32> digest{};
    while (ok && input) {
        input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        auto count = input.gcount();
        if (count)
            ok = BCryptHashData(hash, buffer.data(), ULONG(count), 0) >= 0;
    }
    if (input.bad())
        ok = false;
    if (ok)
        ok = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    constexpr unsigned char expected[] = {0xb6, 0x71, 0xf8, 0x24, 0x94, 0x22, 0x86, 0x2b, 0xf5, 0x57, 0xd0,
                                          0x0a, 0xab, 0x2f, 0x4b, 0xb6, 0x71, 0xed, 0x45, 0x15, 0xcc, 0x10,
                                          0xad, 0x8a, 0x8a, 0x9d, 0xec, 0xad, 0x84, 0x51, 0x61, 0x80};
    return ok && memcmp(digest.data(), expected, 32) == 0;
}
} // namespace Runtime
