#include "effects/post_fx.h"
#include "camera/focus_controls.h"
#include "effects/color_fx.h"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace PostFx {
namespace {
uintptr_t base;
constexpr uintptr_t Effects[] = {0x32f7f40, 0x32f8040, 0x32f3d50};
constexpr uintptr_t ExposureRva = 0x32f82c0, DebugDofRva = 0x32f4050, AdaptationRva = 0x32f7dc0;
std::atomic<bool> owned{false}, manualFocus{false};
std::atomic<float> focusDistance{5}, focusWidth{1}, observedDistance{0};
std::atomic<unsigned long long> focusReads{0}, overrides{0};
int saved[3]{};
bool changed[3]{};
std::atomic<float> compensation{0}, bloomScale{1}, bokehScale{1};
std::atomic<int> bloomIndex{-1};
std::atomic<unsigned long long> bloomDraws{0};
std::atomic<unsigned long long> exposureDraws{0};
using GetFloat = float (*)(void *, void *, void *);
GetFloat originalGetFloat;
int integer(uintptr_t rva) {
    return *reinterpret_cast<int *>(base + rva + 0x28);
}
float scalar(uintptr_t rva) {
    return *reinterpret_cast<float *>(base + rva + 0x2c);
}
void setInt(uintptr_t rva, int n) {
    reinterpret_cast<void (*)(void *, int, bool)>(base + 0x1028e0)(reinterpret_cast<void *>(base + rva), n,
                                                                   true);
}
int findBloomIndex() {
    auto param = *reinterpret_cast<unsigned char **>(base + 0x22b2920);
    if (!param)
        return -1;
    auto type = *reinterpret_cast<unsigned char **>(param + 0x10);
    if (type != reinterpret_cast<unsigned char *>(base + 0x2433f90))
        return -1;
    auto entries = *reinterpret_cast<unsigned char ***>(type + 0x38);
    unsigned count = *reinterpret_cast<unsigned *>(type + 0x40) & 0xffffff;
    if (!entries || count > 10000)
        return -1;
    for (unsigned i = 0; i < count; ++i) {
        auto entry = entries[i];
        if (!entry)
            continue;
        auto name = *reinterpret_cast<const char **>(entry + 8);
        if (name && std::strcmp(name, "env/bloom/veil_strength") == 0) {
            int index = *reinterpret_cast<int *>(entry + 0x48);
            return index >= 0 && index < 4000 ? index : -1;
        }
    }
    return -1;
}
using Draw = uintptr_t (*)(void *, void *, void *, void *, bool, int, int, int, int);
Draw originalDraw;
uintptr_t draw(void *context, void *material, void *surface, void *pass, bool flag, int a, int b, int c,
               int d) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
    float *slot = nullptr;
    float prior[4]{};
    const float stops = compensation.load();
    if (caller == 0x3271a9 && owned.load(std::memory_order_acquire) && stops != 0) {
        auto parameter = *reinterpret_cast<unsigned char **>(base + 0x22b2920);
        if (parameter) {
            int index = *reinterpret_cast<int *>(parameter + 0x48);
            if (index >= 0 && index < 4000) {
                slot = reinterpret_cast<float *>(static_cast<unsigned char *>(context) + 0x10 +
                                                 16 * (index + 1));
                std::memcpy(prior, slot, sizeof(prior));
                for (int i = 0; i < 4; ++i)
                    slot[i] = prior[i] - stops;
                ++exposureDraws;
            }
        }
    }
    float *bloomSlot = nullptr;
    float bloomPrior[4]{};
    if (caller == 0x3271a9 && owned.load()) {
        auto cube = *reinterpret_cast<unsigned char **>(base + 0x22b3420);
        if (cube) {
            int index = *reinterpret_cast<int *>(cube + 0x48);
            if (index >= 0 && index < 4000)
                ColorFx::trackCube(*reinterpret_cast<void **>(static_cast<unsigned char *>(context) + 0x10 +
                                                              16 * (index + 1)));
        }
        int index = bloomIndex.load();
        float scale = bloomScale.load();
        if (index >= 0 && scale != 1) {
            bloomSlot =
                reinterpret_cast<float *>(static_cast<unsigned char *>(context) + 0x10 + 16 * (index + 1));
            std::memcpy(bloomPrior, bloomSlot, sizeof(bloomPrior));
            for (int i = 0; i < 4; ++i)
                bloomSlot[i] = bloomPrior[i] * scale;
            ++bloomDraws;
        }
    }
    auto result = originalDraw(context, material, surface, pass, flag, a, b, c, d);
    if (bloomSlot)
        std::memcpy(bloomSlot, bloomPrior, sizeof(bloomPrior));
    if (slot)
        std::memcpy(slot, prior, sizeof(prior));
    return result;
}
float getFloat(void *values, void *parameter, void *context) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
    float original = originalGetFloat(values, parameter, context);
    if (caller < 0x31409f || caller > 0x314408)
        return original;
    if (caller == 0x3140bb) {
        observedDistance = original;
        ++focusReads;
    }
    if (!owned.load(std::memory_order_acquire) || integer(DebugDofRva) != 0)
        return original;
    if (caller == 0x314327)
        return original * bokehScale.load();
    if (!manualFocus.load())
        return original;
    float result = FocusControls::parameter(caller, original, focusDistance.load(), focusWidth.load());
    if (result != original)
        ++overrides;
    return result;
}
} // namespace
bool install(uintptr_t imageBase, std::string &error) {
    base = imageBase;
    auto nt =
        reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    struct Named {
        uintptr_t rva;
        const char *name;
    };
    for (auto v : {Named{Effects[0], "r_skipBloom"}, Named{Effects[1], "r_skipColorGrading"},
                   Named{Effects[2], "r_skipDOF"}, Named{ExposureRva, "r_postProcessCustomEV"},
                   Named{DebugDofRva, "r_DofDebugMode"},
                   Named{AdaptationRva, "r_skipAutoExposureTemporalAdapatation"}}) {
        auto name = *reinterpret_cast<uintptr_t *>(base + v.rva + 0x30);
        if (name < base || name + std::strlen(v.name) + 1 >= base + nt->OptionalHeader.SizeOfImage ||
            std::memcmp(reinterpret_cast<void *>(name), v.name, std::strlen(v.name) + 1)) {
            error = "Post-FX cvar layout mismatch";
            return false;
        }
    }
    const unsigned char getter[] = {0x40, 0x56, 0x48, 0x83, 0xec, 0x40};
    const unsigned char setter[] = {0x40, 0x57, 0x48, 0x83, 0xec, 0x50};
    if (std::memcmp(reinterpret_cast<void *>(base + 0x342640), getter, sizeof(getter)) ||
        std::memcmp(reinterpret_cast<void *>(base + 0x102880), setter, sizeof(setter))) {
        error = "Post-FX instruction mismatch";
        return false;
    }
    for (uintptr_t site : {0x3140b6, 0x3140cd, 0x3140e4, 0x3140fb, 0x314118, 0x31412f, 0x314296, 0x314322}) {
        auto code = reinterpret_cast<unsigned char *>(base + site);
        int displacement;
        std::memcpy(&displacement, code + 1, 4);
        if (code[0] != 0xe8 || site + 5 + displacement != 0x342640) {
            error = "DOF parameter call mismatch";
            return false;
        }
    }
    MH_STATUS result =
        MH_CreateHook(reinterpret_cast<void *>(base + 0x342640), reinterpret_cast<void *>(getFloat),
                      reinterpret_cast<void **>(&originalGetFloat));
    if (result != MH_OK) {
        error = MH_StatusToString(result);
        return false;
    }
    const unsigned char drawPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10};
    auto call = reinterpret_cast<unsigned char *>(base + 0x3271a4);
    int rel;
    std::memcpy(&rel, call + 1, 4);
    if (std::memcmp(reinterpret_cast<void *>(base + 0x2bb470), drawPrologue, sizeof(drawPrologue)) ||
        call[0] != 0xe8 || 0x3271a9 + rel != 0x2bb470) {
        error = "Tone-map draw instruction mismatch";
        return false;
    }
    result = MH_CreateHook(reinterpret_cast<void *>(base + 0x2bb470), reinterpret_cast<void *>(draw),
                           reinterpret_cast<void **>(&originalDraw));
    if (result != MH_OK) {
        error = MH_StatusToString(result);
        return false;
    }
    return ColorFx::install(error);
}
void begin() {
    for (int i = 0; i < 3; ++i) {
        saved[i] = integer(Effects[i]);
        changed[i] = false;
    }
    compensation = 0;
    exposureDraws = 0;
    bloomScale = 1;
    bokehScale = 1;
    bloomDraws = 0;
    bloomIndex = findBloomIndex();
    ColorFx::begin();
    manualFocus = false;
    focusDistance = 5;
    focusWidth = 1;
    overrides = 0;
    owned.store(true, std::memory_order_release);
}
void reset() {
    if (!owned.load())
        return;
    manualFocus = false;
    for (int i = 0; i < 3; ++i)
        if (changed[i]) {
            setInt(Effects[i], saved[i]);
            changed[i] = false;
        }
    compensation = 0;
    focusDistance = 5;
    focusWidth = 1;
    bloomScale = 1;
    bokehScale = 1;
    ColorFx::reset();
}
void end() {
    // Disable render overrides first; only restore cvars this session changed.
    manualFocus = false;
    reset();
    owned.store(false, std::memory_order_release);
    ColorFx::end();
}
bool enabled(int effect) {
    return effect >= 0 && effect < 3 && integer(Effects[effect]) == 0;
}
void setEnabled(int effect, bool on) {
    if (owned.load() && effect >= 0 && effect < 3) {
        changed[effect] = true;
        setInt(Effects[effect], on ? 0 : 1);
    }
}
float exposure() {
    return compensation;
}
void setExposure(float stops) {
    if (!owned.load() || !std::isfinite(stops))
        return;
    compensation = std::clamp(stops, -5.0f, 5.0f);
    // Applied only while submitting the final tone-map draw. The histogram and
    // persistent cvars retain their original bias; the draw-local slot is restored.
}
bool manual() {
    return manualFocus.load();
}
bool manualAvailable() {
    return integer(DebugDofRva) == 0;
}
bool setManual(bool on) {
    if (!owned.load() || (on && !manualAvailable()))
        return false;
    manualFocus = on;
    return true;
}
float distance() {
    return focusDistance.load();
}
float width() {
    return focusWidth.load();
}
void setDistance(float v) {
    if (owned.load() && std::isfinite(v))
        focusDistance = std::clamp(v, 0.1f, 1000.0f);
}
void setWidth(float v) {
    if (owned.load() && std::isfinite(v))
        focusWidth = std::clamp(v, 0.05f, 100.0f);
}
float bloomStrength() {
    return bloomScale.load();
}
bool bloomAvailable() {
    return bloomIndex.load() >= 0;
}
void setBloomStrength(float v) {
    if (owned.load() && bloomAvailable() && std::isfinite(v))
        bloomScale = std::clamp(v, 0.0f, 4.0f);
}
float bokeh() {
    return bokehScale.load();
}
void setBokeh(float v) {
    if (owned.load() && manualAvailable() && std::isfinite(v))
        bokehScale = std::clamp(v, 0.0f, 4.0f);
}
unsigned flags() {
    return (enabled(0) ? 1 : 0) | (enabled(1) ? 2 : 0) | (enabled(2) ? 4 : 0) | (manual() ? 8 : 0) |
           (manualAvailable() ? 16 : 0) | (bloomAvailable() ? 32 : 0);
}
std::string telemetry() {
    std::ostringstream s;
    s << "\"post_fx_owned\":" << (owned.load() ? "true" : "false")
      << ",\"bloom\":" << (enabled(0) ? "true" : "false")
      << ",\"grading\":" << (enabled(1) ? "true" : "false") << ",\"dof\":" << (enabled(2) ? "true" : "false")
      << ",\"exposure_compensation\":" << compensation.load() << ",\"custom_ev\":" << scalar(ExposureRva)
      << ",\"exposure_draws\":" << exposureDraws.load()
      << ",\"adaptation_frozen\":" << (integer(AdaptationRva) ? "true" : "false")
      << ",\"manual_focus\":" << (manual() ? "true" : "false")
      << ",\"manual_focus_available\":" << (manualAvailable() ? "true" : "false")
      << ",\"focus_distance\":" << distance() << ",\"focus_width\":" << width()
      << ",\"authored_focus_distance\":" << observedDistance.load()
      << ",\"bloom_strength\":" << bloomStrength() << ",\"bloom_parameter_index\":" << bloomIndex.load()
      << ",\"bloom_draws\":" << bloomDraws.load() << ",\"bokeh_size\":" << bokeh()
      << ",\"focus_reads\":" << focusReads.load() << ",\"focus_overrides\":" << overrides.load() << ','
      << ColorFx::telemetry();
    return s.str();
}
} // namespace PostFx
