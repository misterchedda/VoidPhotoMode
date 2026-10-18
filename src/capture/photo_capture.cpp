#include "capture/photo_capture.h"
#include "capture/capture_io.h"
#include "core/runtime.h"
#include <dxgi.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <sstream>
#include <iomanip>
using Microsoft::WRL::ComPtr;
namespace PhotoCapture {
namespace {
using Present = HRESULT(WINAPI *)(IDXGISwapChain *, UINT, UINT);
Present originalPresent = nullptr;
enum class State { Idle, Waiting, Reading, Encoding };
std::atomic<State> state{State::Idle};
std::mutex mutex;
std::filesystem::path directory, lastPath;
std::string lastError;
unsigned frames = 0, sequence = 0, successes = 0, failures = 0;
ULONGLONG started = 0, finished = 0;
std::atomic<unsigned long long> presents{0};
std::atomic<UINT> lastWidth{0}, lastHeight{0}, lastFormat{0};
void fail(const std::string &reason) {
    Runtime::log("Capture failed: " + reason);
    lastError = reason;
    ++failures;
    finished = GetTickCount64();
    state = State::Idle;
}
std::string quoted(const std::string &value) {
    std::ostringstream s;
    s << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            s << '\\' << c;
        else if (c < 32)
            s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
        else
            s << c;
    }
    s << '"';
    return s.str();
}
void encode(Pixels pixels, std::filesystem::path path) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    std::string error;
    try {
        if (SUCCEEDED(com)) {
            std::filesystem::create_directories(path.parent_path());
            ok = writePng(pixels, path, error);
        } else
            error = "PNG COM initialization failed";
    } catch (const std::exception &e) {
        error = e.what();
    } catch (...) {
        error = "PNG encoding failed";
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    std::lock_guard<std::mutex> lock(mutex);
    if (ok) {
        Runtime::log("Photo saved: " + path.u8string());
        lastPath = path;
        lastError.clear();
        ++successes;
        finished = GetTickCount64();
        state = State::Idle;
    } else
        fail(error);
}
HRESULT WINAPI present(IDXGISwapChain *swap, UINT interval, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) {
        DXGI_SWAP_CHAIN_DESC desc{};
        DWORD pid = 0;
        if (SUCCEEDED(swap->GetDesc(&desc)) && desc.OutputWindow) {
            GetWindowThreadProcessId(desc.OutputWindow, &pid);
            if (pid == GetCurrentProcessId() && IsWindowVisible(desc.OutputWindow) &&
                desc.BufferDesc.Width >= 320) {
                ++presents;
                lastWidth = desc.BufferDesc.Width;
                lastHeight = desc.BufferDesc.Height;
                lastFormat = desc.BufferDesc.Format;
                if (state.load() == State::Waiting) {
                    std::lock_guard<std::mutex> lock(mutex);
                    // Drain already-recorded UI command lists before reading a clean frame.
                    if (state.load() == State::Waiting && ++frames >= 5) {
                        state = State::Reading;
                        try {
                            ComPtr<ID3D11Texture2D> backBuffer;
                            Pixels pixels;
                            std::string error;
                            if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
                                fail("Cannot access game back buffer");
                            else if (!readTexture(backBuffer.Get(), pixels, error))
                                fail(error);
                            else {
                                lastWidth = pixels.width;
                                lastHeight = pixels.height;
                                SYSTEMTIME time{};
                                GetLocalTime(&time);
                                wchar_t name[128];
                                swprintf_s(name, L"Dishonored2-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%04u.png",
                                           time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                                           time.wSecond, time.wMilliseconds, GetCurrentProcessId(),
                                           ++sequence);
                                state = State::Encoding;
                                std::thread(encode, std::move(pixels), directory / name).detach();
                            }
                        } catch (const std::exception &e) {
                            fail(e.what());
                        } catch (...) {
                            fail("Frame capture failed");
                        }
                    }
                }
            }
        }
    }
    return originalPresent(swap, interval, flags);
}
} // namespace
bool install(HMODULE module, std::string &error) {
    directory = Runtime::photos();
    HWND window = CreateWindowExW(0, L"STATIC", L"Void photo capture probe", WS_POPUP, 0, 0, 8, 8, nullptr,
                                  nullptr, module, nullptr);
    if (!window) {
        error = "Cannot create DXGI probe window";
        return false;
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 8;
    desc.BufferDesc.Height = 8;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    HRESULT result =
        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                      D3D11_SDK_VERSION, &desc, &swap, &device, nullptr, &context);
    bool ok = false;
    if (SUCCEEDED(result)) {
        void *target = (*reinterpret_cast<void ***>(swap.Get()))[8];
        MH_STATUS hook = MH_CreateHook(target, reinterpret_cast<void *>(present),
                                       reinterpret_cast<void **>(&originalPresent));
        ok = hook == MH_OK;
        if (!ok)
            error = std::string("Present hook: ") + MH_StatusToString(hook);
    } else
        error = "Cannot inspect D3D11 Present";
    swap.Reset();
    context.Reset();
    device.Reset();
    DestroyWindow(window);
    return ok;
}
bool request() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.load() != State::Idle)
        return false;
    lastError.clear();
    frames = 0;
    started = GetTickCount64();
    state = State::Waiting;
    return true;
}
void cancel() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.load() == State::Waiting)
        fail("Capture cancelled");
}
void tick(bool active, bool foreground) {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.load() == State::Waiting && (!active || !foreground || GetTickCount64() - started > 5000))
        fail(active && foreground ? "No rendered frame received within five seconds" : "Capture cancelled");
}
bool hideUi() {
    auto s = state.load();
    return s == State::Waiting || s == State::Reading;
}
int feedback() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.load() != State::Idle)
        return 1;
    if (!finished || GetTickCount64() - finished > 8000)
        return 0;
    return lastError.empty() ? 2 : 3;
}
std::string telemetry() {
    std::lock_guard<std::mutex> lock(mutex);
    std::ostringstream s;
    s << "\"capture_busy\":" << (state.load() != State::Idle ? "true" : "false")
      << ",\"capture_hidden\":" << (hideUi() ? "true" : "false") << ",\"capture_count\":" << successes
      << ",\"capture_failures\":" << failures << ",\"capture_presents\":" << presents.load()
      << ",\"capture_width\":" << lastWidth.load() << ",\"capture_height\":" << lastHeight.load()
      << ",\"capture_format\":" << lastFormat.load() << ",\"capture_path\":" << quoted(lastPath.u8string())
      << ",\"capture_directory\":" << quoted(directory.u8string())
      << ",\"capture_error\":" << quoted(lastError);
    return s.str();
}
} // namespace PhotoCapture
