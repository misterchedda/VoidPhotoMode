#include "ui/native_ui.h"
#include "capture/photo_capture.h"
#include "ui/hud_patch.h"
#include "core/runtime.h"
#include "generated/ui_contract.h"
using namespace UiContract;
#include <MinHook.h>
#include <atomic>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <filesystem>

namespace NativeUi {
namespace {
uintptr_t base;
std::vector<unsigned char> movie;
std::once_flag movieOnce;
std::atomic<void *> player{nullptr};
std::atomic<bool> photo{false}, panel{true}, focus{false}, showHud{false}, clickPending{false},
    mouseHeld{false}, resetPending{false};
std::atomic<float> fov{60}, roll{0}, speed{1}, cursorX{1640}, cursorY{670};
std::atomic<bool> thirds{false};
std::atomic<unsigned> effects{0};
std::atomic<float> exposure{0}, distance{5}, width{1};
std::atomic<float> bloomStrength{1}, bokeh{1}, saturation{1}, contrast{1}, warmth{0}, tint{0};
std::atomic<int> action{0}, lastResult{-1}, styleAssets{0};
std::atomic<unsigned long long> substitutions{0}, updates{0}, callbacks{0}, filteredDraws{0};
std::atomic<DWORD> uiThread{0};
struct Path {
    void *player;
    Path *parent;
    void *name;
    void *ref;
    int index, type;
};
struct Value {
    int type = 1;
    int reserved[3]{};
    union {
        double number;
        void *pointer;
        long long integer;
    } data{};
    int length = 0, pad = 0;
};
static_assert(sizeof(Path) == 40 && sizeof(Value) == 32);
using Load = bool (*)(void *, void *, void *, void *, size_t, void *, bool);
using Tick = void (*)(void *);
Load originalLoad;
Tick originalTick, originalDraw, originalDestroy;
using Call = int (*)(void *, Value *, Path *, void *, int, Value *);
using Name = void *(*)(void *, const char *, int);
bool copy(uintptr_t at, void *out, size_t size) {
    SIZE_T n;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), out, size, &n) && n == size;
}
unsigned long long fingerprint(const unsigned char *data, size_t size) {
    unsigned long long h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    return h;
}
bool isHud(void *raw, size_t size) {
    // Gate the exact original asset by size and full-content fingerprint.
    if (size != 1277798)
        return false;
    auto bytes = static_cast<unsigned char *>(raw);
    if (std::memcmp(bytes, "Ig\x0a\xed", 4) || bytes[9] != 64)
        return false;
    // The patched copy is selected only when the loader requests this exact HUD file.
    return fingerprint(bytes, size) == 0xd0f37a0ba15e1993ull;
}
bool loadHook(void *object, void *decl, void *name, void *raw, size_t size, void *config, bool borrowed) {
    char path[12]{};
    bool match = copy(reinterpret_cast<uintptr_t>(name), path, sizeof(path)) &&
                 std::memcmp(path, "ui/hud.iggy", 12) == 0;
    bool replace = match && isHud(raw, size);
    if (replace) {
        std::call_once(movieOnce, [&]() {
            try {
                HudPatch::apply(static_cast<unsigned char *>(raw), size, movie);
            } catch (...) {
            }
            Runtime::log(movie.empty() ? "Native HUD patch failed; original HUD preserved"
                                       : "Native HUD patch applied in memory");
        });
        replace = !movie.empty();
    }
    bool result = originalLoad(object, decl, name, replace ? movie.data() : raw,
                               replace ? movie.size() : size, config, borrowed);
    if (result && replace) {
        void *current = nullptr;
        copy(reinterpret_cast<uintptr_t>(object) + 0x1608, &current, sizeof(current));
        player = current;
        ++substitutions;
        lastResult = -1;
    }
    return result;
}
void update(void *p) {
    auto root = reinterpret_cast<Path *(*)(void *)>(base + 0x1009a90)(p);
    if (!root)
        return;
    auto name = reinterpret_cast<Name>(base + 0x1009960)(p, "_PM_Update", -1);
    if (!name)
        return;
    Value args[ArgumentCount]{};
    auto boolean = [&](int i, bool b) {
        args[i].type = 3;
        args[i].data.integer = b ? 1 : 0;
    };
    auto number = [&](int i, double n) {
        args[i].type = 4;
        args[i].data.number = n;
    };
    boolean(Arg_active, photo.load());
    number(Arg_fov, fov.load());
    boolean(Arg_focused, focus.load() && !PhotoCapture::hideUi());
    boolean(Arg_shown, panel.load());
    number(Arg_cx, cursorX.load());
    number(Arg_cy, cursorY.load());
    boolean(Arg_pressed, clickPending.exchange(false));
    boolean(Arg_showHUD, showHud.load());
    number(Arg_roll, roll.load());
    number(Arg_speed, speed.load());
    boolean(Arg_thirds, thirds.load());
    number(Arg_effects, effects.load());
    number(Arg_exposure, exposure.load());
    number(Arg_distance, distance.load());
    number(Arg_width, width.load());
    number(Arg_bloomStrength, bloomStrength.load());
    number(Arg_bokeh, bokeh.load());
    number(Arg_saturation, saturation.load());
    number(Arg_contrast, contrast.load());
    number(Arg_warmth, warmth.load());
    number(Arg_tint, tint.load());
    boolean(Arg_held, mouseHeld.load());
    number(Arg_captureStatus, PhotoCapture::feedback());
    boolean(Arg_resetPressed, resetPending.exchange(false));
    // Return a scalar command from AS3 instead of entering the game's ExternalInterface dispatcher.
    // UI execution completes before publishing work for the next engine pump.
    Value result{};
    int rc = reinterpret_cast<Call>(base + 0x100c4c0)(p, &result, root, name, ArgumentCount, args);
    lastResult = rc;
    ++updates;
    uiThread = GetCurrentThreadId();
    if (rc == 0 && result.type == 4 && photo.load()) {
        double packed = result.data.number;
        if (packed >= 0 && packed <= MaxPackedValue && packed == int(packed)) {
            int value = int(packed);
            styleAssets = (value >> AssetShift) & AssetMask;
            int command = value & CommandMask;
            if (command >= FovUp && command <= HideUi) {
                action = value;
                ++callbacks;
            }
        }
    }
}
void tickHook(void *p) {
    originalTick(p);
    if (p == player.load())
        update(p);
}
void drawHook(void *p) {
    if (PhotoCapture::hideUi()) {
        ++filteredDraws;
        return;
    }
    if (photo.load() && !showHud.load() && p != player.load()) {
        ++filteredDraws;
        return;
    }
    originalDraw(p);
}
void destroyHook(void *p) {
    void *expected = p;
    if (player.compare_exchange_strong(expected, nullptr)) {
        lastResult = -1;
        focus = false;
        clickPending = false;
    }
    originalDestroy(p);
}
bool hook(uintptr_t rva, void *detour, void **original, std::initializer_list<unsigned char> bytes,
          std::string &error) {
    if (std::memcmp(reinterpret_cast<void *>(base + rva), bytes.begin(), bytes.size())) {
        error = "UI instruction check failed";
        return false;
    }
    MH_STATUS result = MH_CreateHook(reinterpret_cast<void *>(base + rva), detour, original);
    if (result != MH_OK) {
        error = MH_StatusToString(result);
        return false;
    }
    return true;
}
} // namespace
bool install(uintptr_t imageBase, HMODULE module, std::string &error) {
    base = imageBase;
    (void)module;
    return hook(0x2c620, reinterpret_cast<void *>(loadHook), reinterpret_cast<void **>(&originalLoad),
                {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x54}, error) &&
           hook(0x100ccd0, reinterpret_cast<void *>(tickHook), reinterpret_cast<void **>(&originalTick),
                {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57}, error) &&
           hook(0x100c7c0, reinterpret_cast<void *>(drawHook), reinterpret_cast<void **>(&originalDraw),
                {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57}, error) &&
           hook(0x100c6d0, reinterpret_cast<void *>(destroyHook), reinterpret_cast<void **>(&originalDestroy),
                {0x48, 0x85, 0xc9, 0x0f, 0x85}, error);
}
bool ready() {
    return player.load() != nullptr && lastResult.load() == 0;
}
void begin() {
    photo = true;
    panel = true;
    focus = true;
    showHud = false;
    clickPending = false;
    cursorX = 1640;
    cursorY = 670;
}
void end() {
    resetPending = false;
    photo = false;
    focus = false;
    clickPending = false;
    action = 0;
}
bool focused() {
    return focus.load() && ready();
}
bool hud() {
    return showHud.load();
}
void setHud(bool show) {
    showHud = show;
}
void hidePanel() {
    panel = false;
    focus = false;
    clickPending = false;
}
void requestReset() {
    resetPending = true;
}
void frame(float value, float rollValue, float speedValue, bool thirdsValue, unsigned effectValue,
           float exposureValue, float distanceValue, float widthValue, float bloomValue, float bokehValue,
           float saturationValue, float contrastValue, float warmthValue, float tintValue, long long x,
           long long y, bool foreground, bool looking, bool togglePanel, bool click, bool held) {
    fov = value;
    roll = rollValue;
    speed = speedValue;
    thirds = thirdsValue;
    effects = effectValue;
    exposure = exposureValue;
    distance = distanceValue;
    width = widthValue;
    bloomStrength = bloomValue;
    bokeh = bokehValue;
    saturation = saturationValue;
    contrast = contrastValue;
    warmth = warmthValue;
    tint = tintValue;
    mouseHeld = foreground && !looking && held;
    if (!photo.load() || !ready())
        return;
    if (!foreground) {
        focus = false;
        clickPending = false;
    }
    if (foreground) {
        if (togglePanel)
            panel = !panel.load();
        focus = panel.load() && !looking;
        if (focus.load()) {
            cursorX = std::clamp(cursorX.load() + float(x), 0.0f, 1920.0f);
            cursorY = std::clamp(cursorY.load() + float(y), 0.0f, 1080.0f);
            if (click)
                clickPending = true;
        }
    }
}
int takeAction() {
    return action.exchange(0);
}
std::string telemetry() {
    std::ostringstream s;
    s << "\"ui_ready\":" << (ready() ? "true" : "false")
      << ",\"ui_focus\":" << (focus.load() ? "true" : "false")
      << ",\"ui_substitutions\":" << substitutions.load() << ",\"ui_updates\":" << updates.load()
      << ",\"ui_result\":" << lastResult.load() << ",\"ui_callbacks\":" << callbacks.load()
      << ",\"ui_filtered_draws\":" << filteredDraws.load() << ",\"ui_style_assets\":" << styleAssets.load()
      << ",\"ui_thread\":" << uiThread.load();
    return s.str();
}
} // namespace NativeUi
