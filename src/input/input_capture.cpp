#define DIRECTINPUT_VERSION 0x0800
#include "input/input_capture.h"
#include "input/discrete_input.h"
#include <dinput.h>
#include <Xinput.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cstdlib>

namespace InputCapture {
namespace {
std::atomic<bool> active{false}, draining{false}, exitRequested{false};
std::atomic<long long> mouseX{0}, mouseY{0}, mouseWheel{0};
std::atomic<long long> bufferedX{0}, bufferedY{0}, bufferedWheel{0};
std::atomic<bool> bufferedSample{false};
std::atomic<unsigned long long> stateBlocks{0}, dataBlocks{0}, messageBlocks{0}, padBlocks{0},
    mouseSamples{0};
std::atomic<DWORD> inputEpoch{0};
using KeyFunction = SHORT(WINAPI *)(int);
KeyFunction originalAsync = nullptr, originalKey = nullptr;
using KeyboardFunction = BOOL(WINAPI *)(PBYTE);
KeyboardFunction originalKeyboard = nullptr;
using PadFunction = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
PadFunction originalPad = nullptr;
using PeekFunction = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT, UINT);
using MessageFunction = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT);
PeekFunction originalPeekA = nullptr, originalPeekW = nullptr;
MessageFunction originalMessageA = nullptr, originalMessageW = nullptr;
using DeviceState = HRESULT(WINAPI *)(IDirectInputDevice8W *, DWORD, LPVOID);
using DeviceData = HRESULT(WINAPI *)(IDirectInputDevice8W *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
std::array<DeviceState, 4> originalState{};
std::array<DeviceData, 4> originalData{};
std::array<void *, 4> stateTargets{}, dataTargets{};
unsigned stateCount = 0, dataCount = 0;
DiscreteInput presses[256];
bool discrete(int n) {
    return n == VK_F6 || n == VK_F7 || n == VK_F8 || n == VK_F9 || n == VK_F10 || n == VK_TAB ||
           n == VK_LBUTTON;
}

bool blocked() {
    return active.load(std::memory_order_relaxed) || draining.load(std::memory_order_relaxed);
}
bool foreground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
bool sampleMouse() {
    return active.load(std::memory_order_relaxed) && foreground();
}
void mouse(LONG x, LONG y, LONG z) {
    if (sampleMouse()) {
        mouseX.fetch_add(x);
        mouseY.fetch_add(y);
        mouseWheel.fetch_add(z);
        ++mouseSamples;
    }
}
void bufferedMouse(DWORD offset, DWORD value) {
    if (!sampleMouse())
        return;
    LONG delta = static_cast<LONG>(value);
    if (offset == DIMOFS_X)
        bufferedX.fetch_add(delta);
    else if (offset == DIMOFS_Y)
        bufferedY.fetch_add(delta);
    else if (offset == DIMOFS_Z)
        bufferedWheel.fetch_add(delta);
    else
        return;
    bufferedSample = true;
    ++mouseSamples;
}
void clearMouse() {
    mouseX = 0;
    mouseY = 0;
    mouseWheel = 0;
    bufferedX = 0;
    bufferedY = 0;
    bufferedWheel = 0;
    bufferedSample = false;
}
SHORT WINAPI asyncKey(int n) {
    return blocked() ? 0 : originalAsync(n);
}
SHORT WINAPI keyState(int n) {
    return blocked() ? 0 : originalKey(n);
}
BOOL WINAPI keyboard(PBYTE keys) {
    BOOL result = originalKeyboard(keys);
    if (result && blocked() && keys)
        std::memset(keys, 0, 256);
    return result;
}
DWORD WINAPI pad(DWORD index, XINPUT_STATE *s) {
    DWORD result = originalPad(index, s);
    if (result == ERROR_SUCCESS && s) {
        // Preserve a changing packet number so clients notice entry/exit transitions.
        s->dwPacketNumber += inputEpoch.load();
        if (blocked()) {
            std::memset(&s->Gamepad, 0, sizeof(s->Gamepad));
            ++padBlocks;
        }
    }
    return result;
}
template <unsigned I> HRESULT WINAPI deviceState(IDirectInputDevice8W *device, DWORD size, LPVOID data) {
    HRESULT result = originalState[I](device, size, data);
    if (SUCCEEDED(result) && data && blocked()) {
        if (size == sizeof(DIMOUSESTATE) || size == sizeof(DIMOUSESTATE2)) {
            auto m = static_cast<DIMOUSESTATE *>(data);
            mouse(m->lX, m->lY, m->lZ);
        }
        std::memset(data, 0, size);
        ++stateBlocks;
    }
    return result;
}
template <unsigned I>
HRESULT WINAPI deviceData(IDirectInputDevice8W *device, DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count,
                          DWORD flags) {
    HRESULT result = originalData[I](device, size, data, count, flags);
    if (SUCCEEDED(result) && data && count && blocked() && size >= sizeof(DIDEVICEOBJECTDATA)) {
        DIDEVICEINSTANCEW info{};
        info.dwSize = sizeof(info);
        HRESULT infoResult = device->GetDeviceInfo(&info);
        DWORD deviceType = info.dwDevType;
        if (FAILED(infoResult)) {
            DIDEVICEINSTANCEA ansi{};
            ansi.dwSize = sizeof(ansi);
            infoResult = reinterpret_cast<IDirectInputDevice8A *>(device)->GetDeviceInfo(&ansi);
            deviceType = ansi.dwDevType;
        }
        bool isMouse = SUCCEEDED(infoResult) && GET_DIDEVICE_TYPE(deviceType) == DI8DEVTYPE_MOUSE;
        DWORD kept = 0;
        for (DWORD i = 0; i < *count; ++i) {
            auto item = reinterpret_cast<DIDEVICEOBJECTDATA *>(reinterpret_cast<BYTE *>(data) + size * i);
            if (isMouse && !(flags & DIGDD_PEEK)) {
                bufferedMouse(item->dwOfs, item->dwData);
            }
            // Let releases clear any game state held before capture started.
            bool release =
                isMouse ? (item->dwOfs >= DIMOFS_BUTTON0 && item->dwData == 0) : (item->dwData == 0);
            if (release) {
                std::memmove(reinterpret_cast<BYTE *>(data) + size * kept, item, size);
                ++kept;
            }
        }
        *count = kept;
        ++dataBlocks;
    }
    return result;
}
void filter(LPMSG message, bool removed) {
    if (!removed || !message || !message->hwnd)
        return;
    DWORD pid = 0;
    GetWindowThreadProcessId(message->hwnd, &pid);
    if (pid != GetCurrentProcessId())
        return;
    // Latch brief presses that can begin and end between engine-pump polls.
    if ((message->message == WM_KEYDOWN || message->message == WM_KEYUP ||
         message->message == WM_SYSKEYDOWN || message->message == WM_SYSKEYUP) &&
        message->wParam < 256 && discrete(int(message->wParam)))
        presses[message->wParam].message(message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN);
    if (message->message == WM_LBUTTONDOWN || message->message == WM_LBUTTONDBLCLK)
        presses[VK_LBUTTON].message(true);
    if (message->message == WM_LBUTTONUP)
        presses[VK_LBUTTON].message(false);
    if (!blocked())
        return;
    bool swallow = false;
    switch (message->message) {
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        swallow = message->wParam == VK_F10;
        break;
    case WM_KEYDOWN:
        if (message->wParam == VK_ESCAPE && active.load())
            exitRequested = true;
        swallow = true;
        break;
    case WM_CHAR:
    case WM_DEADCHAR:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        swallow = true;
        break;
    default:
        break; // Keep key/button releases, focus changes, system keys and window-management messages.
    }
    if (swallow) {
        message->message = WM_NULL;
        ++messageBlocks;
    }
}
BOOL WINAPI peekA(LPMSG m, HWND w, UINT lo, UINT hi, UINT flags) {
    BOOL r = originalPeekA(m, w, lo, hi, flags);
    if (r)
        filter(m, (flags & PM_REMOVE) != 0);
    return r;
}
BOOL WINAPI peekW(LPMSG m, HWND w, UINT lo, UINT hi, UINT flags) {
    BOOL r = originalPeekW(m, w, lo, hi, flags);
    if (r)
        filter(m, (flags & PM_REMOVE) != 0);
    return r;
}
BOOL WINAPI messageA(LPMSG m, HWND w, UINT lo, UINT hi) {
    BOOL r = originalMessageA(m, w, lo, hi);
    if (r > 0)
        filter(m, true);
    return r;
}
BOOL WINAPI messageW(LPMSG m, HWND w, UINT lo, UINT hi) {
    BOOL r = originalMessageW(m, w, lo, hi);
    if (r > 0)
        filter(m, true);
    return r;
}

template <typename T>
bool api(const wchar_t *module, const char *name, T detour, T &original, std::string &error) {
    MH_STATUS r = MH_CreateHookApi(module, name, reinterpret_cast<void *>(detour),
                                   reinterpret_cast<void **>(&original));
    if (r != MH_OK) {
        error = std::string(name) + ": " + MH_StatusToString(r);
        return false;
    }
    return true;
}
bool deviceHooks(IDirectInputDevice8W *device, std::string &error) {
    auto table = *reinterpret_cast<void ***>(device);
    if (std::find(stateTargets.begin(), stateTargets.end(), table[9]) == stateTargets.end()) {
        if (stateCount >= 4) {
            error = "Too many DirectInput state implementations";
            return false;
        }
        unsigned i = stateCount++;
        DeviceState detours[] = {deviceState<0>, deviceState<1>, deviceState<2>, deviceState<3>};
        auto detour = detours[i];
        if (MH_CreateHook(table[9], reinterpret_cast<void *>(detour),
                          reinterpret_cast<void **>(&originalState[i])) != MH_OK) {
            error = "DirectInput GetDeviceState hook failed";
            return false;
        }
        stateTargets[i] = table[9];
    }
    if (std::find(dataTargets.begin(), dataTargets.end(), table[10]) == dataTargets.end()) {
        if (dataCount >= 4) {
            error = "Too many DirectInput data implementations";
            return false;
        }
        unsigned i = dataCount++;
        DeviceData detours[] = {deviceData<0>, deviceData<1>, deviceData<2>, deviceData<3>};
        auto detour = detours[i];
        if (MH_CreateHook(table[10], reinterpret_cast<void *>(detour),
                          reinterpret_cast<void **>(&originalData[i])) != MH_OK) {
            error = "DirectInput GetDeviceData hook failed";
            return false;
        }
        dataTargets[i] = table[10];
    }
    return true;
}
} // namespace
bool install(std::string &error) {
    if (!api(L"user32.dll", "GetAsyncKeyState", asyncKey, originalAsync, error) ||
        !api(L"user32.dll", "GetKeyState", keyState, originalKey, error) ||
        !api(L"user32.dll", "GetKeyboardState", keyboard, originalKeyboard, error) ||
        !api(L"user32.dll", "PeekMessageA", peekA, originalPeekA, error) ||
        !api(L"user32.dll", "PeekMessageW", peekW, originalPeekW, error) ||
        !api(L"user32.dll", "GetMessageA", messageA, originalMessageA, error) ||
        !api(L"user32.dll", "GetMessageW", messageW, originalMessageW, error) ||
        !api(L"XINPUT9_1_0.dll", "XInputGetState", pad, originalPad, error))
        return false;
    IDirectInput8W *di = nullptr;
    HRESULT result = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                        reinterpret_cast<void **>(&di), nullptr);
    if (FAILED(result)) {
        error = "Could not inspect DirectInput implementation";
        return false;
    }
    bool ok = true;
    for (const GUID *guid : {&GUID_SysMouse, &GUID_SysKeyboard}) {
        IDirectInputDevice8W *device = nullptr;
        if (FAILED(di->CreateDevice(*guid, &device, nullptr))) {
            ok = false;
            error = "Could not inspect input device";
            break;
        }
        ok = deviceHooks(device, error);
        device->Release();
        if (!ok)
            break;
    }
    di->Release();
    if (!ok)
        return false;
    IDirectInput8A *ansi = nullptr;
    result = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
                                reinterpret_cast<void **>(&ansi), nullptr);
    if (FAILED(result)) {
        error = "Could not inspect ANSI DirectInput implementation";
        return false;
    }
    for (const GUID *guid : {&GUID_SysMouse, &GUID_SysKeyboard}) {
        IDirectInputDevice8A *device = nullptr;
        if (FAILED(ansi->CreateDevice(*guid, &device, nullptr))) {
            ok = false;
            error = "Could not inspect ANSI input device";
            break;
        }
        ok = deviceHooks(reinterpret_cast<IDirectInputDevice8W *>(device), error);
        device->Release();
        if (!ok)
            break;
    }
    ansi->Release();
    return ok;
}
bool down(int n) {
    return originalAsync && (originalAsync(n) & 0x8000) != 0;
}
bool pressed(int n) {
    return n >= 0 && n < 256 && presses[n].poll(down(n));
}
void begin() {
    clearMouse();
    exitRequested = false;
    draining = false;
    active = true;
    ++inputEpoch;
}
void end() {
    draining = true;
    active = false;
    clearMouse();
    exitRequested = false;
    ++inputEpoch;
}
void frame(bool focused) {
    if (!focused)
        clearMouse();
    if (draining.load()) {
        bool held = false;
        for (int n = 1; n < 256; ++n) {
            if (n == VK_F6 || n == VK_F7 || n == VK_F8 || n == VK_CAPITAL || n == VK_NUMLOCK ||
                n == VK_SCROLL)
                continue;
            if (down(n)) {
                held = true;
                break;
            }
        }
        for (DWORD i = 0; i < XUSER_MAX_COUNT && !held; ++i) {
            XINPUT_STATE s{};
            if (originalPad(i, &s) != ERROR_SUCCESS)
                continue;
            auto &g = s.Gamepad;
            held = g.wButtons || g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
                   g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
                   std::abs(int(g.sThumbLX)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
                   std::abs(int(g.sThumbLY)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
                   std::abs(int(g.sThumbRX)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ||
                   std::abs(int(g.sThumbRY)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        }
        if (!held) {
            draining = false;
            ++inputEpoch;
        }
    }
}
bool takeExitRequest() {
    return exitRequested.exchange(false);
}
void takeMouse(long long &x, long long &y, long long &wheel) {
    x = mouseX.exchange(0);
    y = mouseY.exchange(0);
    wheel = mouseWheel.exchange(0);
    bool buffered = bufferedSample.exchange(false);
    auto bx = bufferedX.exchange(0), by = bufferedY.exchange(0), bz = bufferedWheel.exchange(0);
    // Some clients poll both APIs; consume one stream so motion is not doubled.
    if (buffered) {
        x = bx;
        y = by;
        wheel = bz;
    }
}
std::string telemetry() {
    std::ostringstream s;
    s << "\"input_captured\":" << (active.load() ? "true" : "false")
      << ",\"input_draining\":" << (draining.load() ? "true" : "false")
      << ",\"blocked_device_polls\":" << stateBlocks.load()
      << ",\"blocked_device_batches\":" << dataBlocks.load()
      << ",\"blocked_messages\":" << messageBlocks.load()
      << ",\"blocked_controller_polls\":" << padBlocks.load() << ",\"mouse_samples\":" << mouseSamples.load();
    return s.str();
}
} // namespace InputCapture
