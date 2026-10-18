// Dishonored 2 photo-mode backend, pinned to the executable in README.txt.
// Ansel entry points and camera layout researched using Frans Bouma's IGCS.
// See THIRD_PARTY_NOTICES.txt. No Ansel entry-check patches are applied.
#include <windows.h>
#include <DirectXMath.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "input/input_capture.h"
#include "ui/native_ui.h"
#include "camera/camera_controls.h"
#include "effects/post_fx.h"
#include "effects/color_fx.h"
#include "camera/pointer_controls.h"
#include "capture/photo_capture.h"
#include "core/runtime.h"
#include "generated/ui_contract.h"
using namespace UiContract;

using namespace DirectX;
namespace {
uintptr_t base;
HMODULE self;
using Pump = void(__fastcall *)(void *);
Pump originalPump;
std::atomic<unsigned long long> ticks{0};
std::atomic<DWORD> pumpThread{0};
struct Camera {
    float position[3];
    float quaternion[4];
    float fov;
};
static_assert(sizeof(Camera) == 32);
Camera *camera;
Camera pose{}, origin{};
enum class State { Off, Starting, Active } state = State::Off;
unsigned startingFrames = 0;
bool haveOrigin = false;
int savedFull = 0, saved3d = 0, savedResolution = 0;
double lastSeconds = 0;
bool uiPassEnabled = false;
bool captureWorldHidden = false;
int captureWorldSaved = 0;
float movementSpeed = 1.0f;
bool thirds = false;
PointerControls pointer;

constexpr uintptr_t StartRva = 0x8b0160, StopRva = 0x8b02c0, PumpRva = 0xff8f0;
constexpr uintptr_t CameraRva = 0x397bc60, SessionRva = 0x39a529c;
constexpr uintptr_t FullGuiRva = 0x32f53e0, WorldGuiRva = 0x32f5460, ResolutionRva = 0x397bec0;
constexpr uintptr_t StopTimeRva = 0x39a0e10, RunFramesRva = 0x39a0e90, FreezeTimeRva = 0x39a60e0;

void log(const std::string &s) {
    Runtime::log(s);
}
int value(uintptr_t rva) {
    return *reinterpret_cast<int *>(base + rva + 0x28);
}
void setInt(uintptr_t rva, int n) {
    // idCVar integer setter; updates string, integer, float, and change state together.
    reinterpret_cast<void(__fastcall *)(void *, int, bool)>(base + 0x1028e0)(
        reinterpret_cast<void *>(base + rva), n, true);
}
void setBool(uintptr_t rva, bool b) {
    reinterpret_cast<void(__fastcall *)(void *, bool, bool)>(base + 0x102520)(
        reinterpret_cast<void *>(base + rva), b, true);
}
bool session() {
    return *reinterpret_cast<unsigned char *>(base + SessionRva) != 0;
}
bool valid(const Camera &c) {
    for (float v : c.position)
        if (!std::isfinite(v) || std::abs(v) > 10000000)
            return false;
    float length = 0;
    for (float q : c.quaternion) {
        if (!std::isfinite(q))
            return false;
        length += q * q;
    }
    return length > 0.8f && length < 1.2f && std::isfinite(c.fov) && c.fov >= 1 && c.fov <= 179;
}
double seconds() {
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return double(t.QuadPart) / double(f.QuadPart);
}
const char *stateName() {
    return state == State::Off ? "off" : state == State::Starting ? "starting" : "active";
}
std::string status() {
    Camera c = *camera;
    std::ostringstream s;
    s << std::setprecision(9) << "{\"ok\":true,\"version\":\"0.8.0-rc3\",\"state\":\"" << stateName()
      << "\",\"ticks\":" << ticks.load() << ",\"engine_thread\":" << pumpThread.load()
      << ",\"ansel\":" << (session() ? "true" : "false")
      << ",\"camera_valid\":" << (valid(c) ? "true" : "false") << ",\"position\":[" << c.position[0] << ','
      << c.position[1] << ',' << c.position[2] << "],\"quaternion\":[" << c.quaternion[0] << ','
      << c.quaternion[1] << ',' << c.quaternion[2] << ',' << c.quaternion[3] << "],\"fov\":" << c.fov
      << ",\"roll\":" << CameraControls::rollDegrees(c.quaternion) << ",\"movement_speed\":" << movementSpeed
      << ",\"thirds\":" << (thirds ? "true" : "false") << ",\"r_skipIggyFullscreen\":" << value(FullGuiRva)
      << ",\"r_skipIggy3D\":" << value(WorldGuiRva) << ",\"rs_enable\":" << value(ResolutionRva)
      << ",\"g_stopTime\":" << value(StopTimeRva) << ",\"g_freezeTime\":" << value(FreezeTimeRva)
      << ",\"g_runFrames\":" << value(RunFramesRva) << ',' << InputCapture::telemetry() << ','
      << NativeUi::telemetry() << ',' << PostFx::telemetry() << ',' << PhotoCapture::telemetry() << '}';
    return s.str();
}
std::string error(const char *s) {
    return std::string("{\"ok\":false,\"error\":\"") + s + "\"}";
}
void leave() {
    if (state == State::Off)
        return;
    PhotoCapture::cancel();
    captureWorldHidden = false;
    PostFx::end();
    if (session()) {
        if (haveOrigin)
            *camera = origin;
        reinterpret_cast<void (*)()>(base + StopRva)();
    }
    setBool(FullGuiRva, savedFull != 0);
    setBool(WorldGuiRva, saved3d != 0);
    setInt(ResolutionRva, savedResolution);
    state = State::Off;
    haveOrigin = false;
    InputCapture::end();
    NativeUi::end();
    uiPassEnabled = false;
    log("Photo mode exited; prior GUI and resolution values restored: " + status());
}
std::string enter() {
    if (state != State::Off)
        return error("Photo mode is already enabled");
    if (session())
        return error("Another Ansel session is active");
    if (!NativeUi::ready())
        return error("Photo UI unavailable; load a supported level/HUD");
    if (value(StopTimeRva) || value(FreezeTimeRva))
        return error("Another time override is active");
    savedFull = value(FullGuiRva);
    saved3d = value(WorldGuiRva);
    savedResolution = value(ResolutionRva);
    // Retain every game-owned guard, including unloaded-world, menu, and scene checks.
    int result = reinterpret_cast<int (*)()>(base + StartRva)();
    if (!result || !session())
        return error("Game rejected Ansel entry; load a level and close menus");
    PostFx::begin();
    state = State::Starting;
    startingFrames = 0;
    haveOrigin = false;
    InputCapture::begin();
    NativeUi::begin();
    uiPassEnabled = false;
    log("Native Ansel session entered without bypassing entry checks");
    return "{\"ok\":true,\"state\":\"starting\"}";
}
void move(float forward, float right, float up) {
    XMVECTOR q = XMLoadFloat4(reinterpret_cast<XMFLOAT4 *>(pose.quaternion));
    XMVECTOR delta = XMVector3Rotate(XMVectorSet(forward, -right, up, 0), q);
    XMFLOAT3 v;
    XMStoreFloat3(&v, delta);
    pose.position[0] += v.x;
    pose.position[1] += v.y;
    pose.position[2] += v.z;
}
void rotate(float pitch, float yaw, float roll) {
    XMVECTOR q = XMLoadFloat4(reinterpret_cast<XMFLOAT4 *>(pose.quaternion));
    XMVECTOR local =
        XMQuaternionMultiply(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), XMConvertToRadians(pitch)),
                             XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), XMConvertToRadians(-roll)));
    q = XMQuaternionMultiply(local, q);
    q = XMQuaternionMultiply(q, XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), XMConvertToRadians(-yaw)));
    XMStoreFloat4(reinterpret_cast<XMFLOAT4 *>(pose.quaternion), XMQuaternionNormalize(q));
}
bool key(int n) {
    return InputCapture::down(n);
}
bool edge(int n) {
    return InputCapture::pressed(n);
}
void update() {
    auto now = seconds();
    float dt = static_cast<float>(std::clamp(now - lastSeconds, 0.0, 0.05));
    lastSeconds = now;
    if (state != State::Off && !session()) {
        log("Session ended externally; relinquishing camera");
        leave();
    }
    if (state == State::Starting) {
        ++startingFrames;
        if (startingFrames >= 2 && valid(*camera)) {
            origin = pose = *camera;
            haveOrigin = true;
            state = State::Active;
            log("Camera initialized: " + status());
        } else if (startingFrames > 120) {
            log("Camera initialization timed out");
            leave();
        }
    }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    bool focused = foregroundPid == GetCurrentProcessId();
    // The game opens its pause menu on deactivation. Relinquish the whole
    // photo session so our HUD-only draw filter cannot hide that menu.
    if (!focused && state != State::Off) {
        log("Focus lost; restoring game UI and ending photo mode");
        leave();
    }
    PhotoCapture::tick(state == State::Active, focused);
    InputCapture::frame(focused);
    long long mouseX = 0, mouseY = 0, wheel = 0;
    InputCapture::takeMouse(mouseX, mouseY, wheel);
    bool escape = InputCapture::takeExitRequest();
    bool toggle = edge(VK_F6), hud = edge(VK_F7), home = edge(VK_F8), panel = edge(VK_F9),
         capture = edge(VK_F10), click = edge(VK_LBUTTON);
    const auto motion = pointer.frame(focused && state == State::Active, key(VK_RBUTTON), mouseX, mouseY);
    NativeUi::frame(pose.fov, CameraControls::rollDegrees(pose.quaternion), movementSpeed, thirds,
                    PostFx::flags(), PostFx::exposure(), PostFx::distance(), PostFx::width(),
                    PostFx::bloomStrength(), PostFx::bokeh(), ColorFx::get(0), ColorFx::get(1),
                    ColorFx::get(2), ColorFx::get(3), motion.pointerX, motion.pointerY, focused,
                    motion.looking, panel, click, key(VK_LBUTTON));
    if (state == State::Active && NativeUi::ready() && !uiPassEnabled) {
        setBool(FullGuiRva, false);
        uiPassEnabled = true;
    }
    int packedAction = NativeUi::takeAction();
    int uiAction = packedAction & CommandMask;
    if (state == State::Active) {
        if (uiAction >= FovSlider && uiAction <= TintSlider) {
            float t = std::clamp(float(packedAction >> ValueShift) / float(ValueSteps), 0.0f, 1.0f);
            int index = uiAction - FovSlider;
            const auto &slider = Sliders[index];
            float v = slider.logarithmic ? slider.low * std::pow(slider.high / slider.low, t)
                                         : slider.low + t * (slider.high - slider.low);
            v = std::clamp(v, slider.low, slider.high);
            if (index == 0)
                pose.fov = v;
            else if (index == 1)
                rotate(0, 0, v - CameraControls::rollDegrees(pose.quaternion));
            else if (index == 2)
                movementSpeed = v;
            else if (index == 3)
                PostFx::setExposure(v);
            else if (index == 4)
                PostFx::setDistance(v);
            else if (index == 5)
                PostFx::setWidth(v);
            else if (index == 6)
                PostFx::setBloomStrength(v);
            else if (index == 7)
                PostFx::setBokeh(v);
            else
                ColorFx::set(index - 8, v);
        } else if (uiAction == FovUp)
            pose.fov = std::min(150.0f, pose.fov + 2);
        else if (uiAction == FovDown)
            pose.fov = std::max(5.0f, pose.fov - 2);
        else if (uiAction == ResetView)
            pose = origin;
        else if (uiAction == ReturnGame)
            leave();
        else if (uiAction == TakePhoto)
            PhotoCapture::request();
        else if (uiAction == HideUi)
            NativeUi::hidePanel();
        else if (uiAction == RollUp)
            rotate(0, 0, 2);
        else if (uiAction == RollDown)
            rotate(0, 0, -2);
        else if (uiAction == SpeedUp)
            movementSpeed = CameraControls::nextSpeed(movementSpeed, true);
        else if (uiAction == SpeedDown)
            movementSpeed = CameraControls::nextSpeed(movementSpeed, false);
        else if (uiAction == ToggleThirds)
            thirds = !thirds;
        else if (uiAction >= ToggleBloom && uiAction <= ToggleDof)
            PostFx::setEnabled(uiAction - ToggleBloom, !PostFx::enabled(uiAction - ToggleBloom));
        else if (uiAction == ExposureDown)
            PostFx::setExposure(PostFx::exposure() - 0.25f);
        else if (uiAction == ExposureUp)
            PostFx::setExposure(PostFx::exposure() + 0.25f);
        else if (uiAction == ToggleManual)
            PostFx::setManual(!PostFx::manual());
        else if (uiAction == DistanceDown)
            PostFx::setDistance(PostFx::distance() / 1.25f);
        else if (uiAction == DistanceUp)
            PostFx::setDistance(PostFx::distance() * 1.25f);
        else if (uiAction == WidthDown)
            PostFx::setWidth(PostFx::width() / 1.25f);
        else if (uiAction == WidthUp)
            PostFx::setWidth(PostFx::width() * 1.25f);
        else if (uiAction == ResetEffects)
            PostFx::reset();
        else if (uiAction == BloomDown || uiAction == BloomUp)
            PostFx::setBloomStrength(PostFx::bloomStrength() + (uiAction == BloomUp ? 0.1f : -0.1f));
        else if (uiAction == BokehDown || uiAction == BokehUp)
            PostFx::setBokeh(PostFx::bokeh() + (uiAction == BokehUp ? 0.1f : -0.1f));
        else if (uiAction >= SaturationDown && uiAction <= TintUp) {
            int c = (uiAction - SaturationDown) / 2;
            float lo = c < 2 ? (c == 0 ? 0.0f : 0.5f) : -1.0f, hi = c == 0 ? 2.0f : c == 1 ? 1.5f : 1.0f;
            ColorFx::set(c, std::clamp(ColorFx::get(c) + (uiAction % 2 == 0 ? 0.05f : -0.05f), lo, hi));
        }
    }
    if (focused) {
        if (toggle) {
            if (state == State::Off)
                log(enter());
            else
                leave();
        }
        if (state != State::Off && (escape || key(VK_ESCAPE))) {
            leave();
            return;
        }
        if (state == State::Active) {
            if (hud) {
                if (NativeUi::ready()) {
                    bool show = !NativeUi::hud();
                    NativeUi::setHud(show);
                    setBool(WorldGuiRva, !show);
                } else {
                    bool show = value(FullGuiRva) != 0;
                    setBool(FullGuiRva, !show);
                    setBool(WorldGuiRva, !show);
                }
            }
            if (capture)
                PhotoCapture::request();
            if (home) {
                if (NativeUi::ready())
                    NativeUi::requestReset();
                else
                    pose = origin;
            }
            if (!PhotoCapture::hideUi()) {
                movementSpeed = PointerControls::wheelSpeed(movementSpeed, wheel);
                float speed = 2.0f * movementSpeed * dt *
                              (key(VK_SHIFT)     ? 5.0f
                               : key(VK_CONTROL) ? 0.15f
                                                 : 1.0f);
                float f = float((key('W') || key(VK_NUMPAD8)) - (key('S') || key(VK_NUMPAD5)));
                float r = float((key('D') || key(VK_NUMPAD6)) - (key('A') || key(VK_NUMPAD4)));
                float u = float((key('E') || key(VK_NUMPAD9)) - (key('Q') || key(VK_NUMPAD7)));
                float length = std::sqrt(f * f + r * r + u * u);
                if (length > 0) {
                    move(f * speed / length, r * speed / length, 0);
                    pose.position[2] += u * speed / length; // Vertical lift stays aligned with the world.
                }
                rotate(float(std::clamp(motion.lookY, -10000LL, 10000LL)) * 0.08f +
                           (key(VK_DOWN) - key(VK_UP)) * 45.0f * dt,
                       float(std::clamp(motion.lookX, -10000LL, 10000LL)) * 0.08f +
                           (key(VK_RIGHT) - key(VK_LEFT)) * 45.0f * dt,
                       ((key('Z') || key(VK_NUMPAD1)) - (key('C') || key(VK_NUMPAD3))) * 45.0f * dt);
                pose.fov = std::clamp(pose.fov + (key(VK_ADD) - key(VK_SUBTRACT)) * 20.0f * dt, 5.0f, 150.0f);
            }
        }
    }
    if (state == State::Active) {
        bool hidden = PhotoCapture::hideUi();
        if (hidden && !captureWorldHidden) {
            captureWorldSaved = value(WorldGuiRva);
            setBool(WorldGuiRva, true);
            captureWorldHidden = true;
        } else if (!hidden && captureWorldHidden) {
            setBool(WorldGuiRva, captureWorldSaved != 0);
            captureWorldHidden = false;
        }
        *camera = pose;
    }
}
void __fastcall pump(void *object) {
    originalPump(object);
    DWORD expected = 0;
    pumpThread.compare_exchange_strong(expected, GetCurrentThreadId());
    if (GetCurrentThreadId() != pumpThread.load())
        return;
    ++ticks;
    update();
}
bool equalBytes(uintptr_t rva, std::initializer_list<unsigned char> bytes) {
    return std::memcmp(reinterpret_cast<void *>(base + rva), bytes.begin(), bytes.size()) == 0;
}
bool validate() {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        return false;
    if (!equalBytes(PumpRva, {0x40, 0x53, 0x41, 0x55, 0xb8, 0x48, 0x14, 0, 0, 0xe8, 0xc2, 0xde, 0x0a, 0x01,
                              0x48, 0x2b, 0xe0}))
        return false;
    if (!equalBytes(StartRva, {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x0d, 0x25, 0x51, 0x0f, 0x03}))
        return false;
    if (!equalBytes(StopRva, {0x48, 0x83, 0xec, 0x28, 0x8b, 0x15, 0xde, 0x4f, 0x0f, 0x03}))
        return false;
    if (!equalBytes(0x102520, {0x40, 0x57, 0x48, 0x83, 0xec, 0x50}) ||
        !equalBytes(0x1028e0, {0x40, 0x57, 0x48, 0x83, 0xec, 0x50}))
        return false;
    struct Named {
        uintptr_t rva;
        const char *name;
    };
    for (auto v : {Named{FullGuiRva, "r_skipIggyFullscreen"}, Named{WorldGuiRva, "r_skipIggy3D"},
                   Named{ResolutionRva, "rs_enable"}, Named{StopTimeRva, "g_stopTime"},
                   Named{RunFramesRva, "g_runFrames"}, Named{FreezeTimeRva, "g_freezeTime"}}) {
        auto name = *reinterpret_cast<uintptr_t *>(base + v.rva + 0x30);
        if (name < base || name + std::strlen(v.name) + 1 >= base + nt->OptionalHeader.SizeOfImage ||
            std::memcmp(reinterpret_cast<void *>(name), v.name, std::strlen(v.name) + 1))
            return false;
    }
    return true;
}
DWORD WINAPI worker(void *) {
    std::string initializationError;
    if (!Runtime::initialize(initializationError)) {
        OutputDebugStringA(initializationError.c_str());
        return 1;
    }
    log("Starting Void Photo Mode 0.8.0-rc3");
    base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"Dishonored2.exe"));
    if (!base || !Runtime::supportedExecutable() || !validate()) {
        log("REFUSED: unsupported executable or runtime layout; game left unchanged");
        return 1;
    }
    camera = reinterpret_cast<Camera *>(base + CameraRva);
    if (MH_Initialize() != MH_OK) {
        log("MinHook initialization failed");
        return 1;
    }
    std::string inputError;
    if (MH_CreateHook(reinterpret_cast<void *>(base + PumpRva), pump,
                      reinterpret_cast<void **>(&originalPump)) != MH_OK ||
        !InputCapture::install(inputError) || !NativeUi::install(base, self, inputError) ||
        !PostFx::install(base, inputError) || !PhotoCapture::install(self, inputError) ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        log("Hook setup failed: " + inputError);
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        return 1;
    }
    log("Ready v0.8.0-rc3. F6 photo mode, F9 panel, F10 PNG capture, hold RMB to look. Native post-FX "
        "controls installed.");
    return 0;
}
} // namespace
extern "C" __declspec(dllexport) void WINAPI VoidPhotoStart() {
    static std::atomic<bool> started{false};
    if (started.exchange(true))
        return;
    HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else {
        started = false;
        OutputDebugStringW(L"Void Photo Mode: failed to create startup thread; startup may be retried\n");
    }
}
BOOL WINAPI DllMain(HMODULE module, DWORD reason, void *) {
    if (reason == DLL_PROCESS_ATTACH)
        self = module;
    return TRUE;
}
