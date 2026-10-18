#include "effects/color_fx.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <mutex>
#include <sstream>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace ColorFx {
namespace {
std::atomic<bool> active{false};
std::atomic<void *> wanted{nullptr};
std::atomic<float> controls[4]{{1}, {1}, {0}, {0}};
std::atomic<unsigned long long> matches{0}, processed{0}, failures{0};
std::atomic<unsigned> contextType{999};
std::atomic<unsigned long long> bindCalls{0}, cubeViews{0};
std::atomic<int> liveHook{-999};
void *initialTarget = nullptr;
std::mutex gpuMutex;
ComPtr<ID3DBlob> code;
struct Resources {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture3D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
} gpu;
const char *hlsl = R"(
Texture3D<float4> Source : register(t0);
RWTexture3D<float4> Result : register(u0);
SamplerState PointClamp : register(s0);
cbuffer Controls : register(b0) { float saturation; float contrast; float warmth; float tint; };
[numthreads(4,4,4)] void main(uint3 id : SV_DispatchThreadID) {
 float4 v=Source.SampleLevel(PointClamp,(float3(id)+0.5)/32.0,0);
 float3 gain=exp2(float3(0.35*warmth+0.125*tint,-0.25*tint,-0.35*warmth+0.125*tint));
 gain/=dot(gain,float3(0.2126,0.7152,0.0722));
 float3 c=v.rgb*gain;
 float l=dot(c,float3(0.2126,0.7152,0.0722));
 c=lerp(l.xxx,c,saturation);
 // Log contrast around linear middle gray retains shadow detail.
 c=0.18*pow(max(c,0.0)/0.18,contrast);
 Result[id]=float4(saturate(c),v.a);
})";
bool prepare(ID3D11DeviceContext *context) {
    ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    if (gpu.device != device || !gpu.sampler) {
        gpu = {};
        gpu.device = device;
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 16;
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC samp{};
        samp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samp.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                               &gpu.shader)) ||
            FAILED(device->CreateBuffer(&cb, nullptr, &gpu.constants)) ||
            FAILED(device->CreateSamplerState(&samp, &gpu.sampler)))
            return false;
    }
    // A prior result may still be bound in any stage or recorded command list.
    // Never write to that resource again; D3D retains it until its consumers release it.
    gpu.texture.Reset();
    gpu.srv.Reset();
    gpu.uav.Reset();
    D3D11_TEXTURE3D_DESC desc{};
    desc.Width = desc.Height = desc.Depth = 32;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    return SUCCEEDED(device->CreateTexture3D(&desc, nullptr, &gpu.texture)) &&
           SUCCEEDED(device->CreateShaderResourceView(gpu.texture.Get(), nullptr, &gpu.srv)) &&
           SUCCEEDED(device->CreateUnorderedAccessView(gpu.texture.Get(), nullptr, &gpu.uav));
}
using Bind = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, ID3D11ShaderResourceView *const *);
Bind originalBind;
Bind originalLiveBind;
Bind originalCsBind;
Bind originalDeferredPs, originalDeferredCs;
std::atomic<unsigned> targetDimension{0};
thread_local bool transforming = false;
// All touched compute state is restored. No engine texture or environment resource
// is written; only our private 32^3 cube becomes the temporary shader input.
bool transform(ID3D11DeviceContext *c, ID3D11ShaderResourceView *source, const float *values) {
    contextType = c->GetType();
    if (!prepare(c))
        return false;
    ComPtr<ID3D11ComputeShader> shader;
    ID3D11ClassInstance *classes[256]{};
    UINT numClasses = 256;
    c->CSGetShader(&shader, classes, &numClasses);
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11SamplerState> sampler;
    ID3D11UnorderedAccessView *uavs[8]{};
    ID3D11UnorderedAccessView *nullUavs[8]{};
    UINT keep[8];
    for (auto &k : keep)
        k = UINT(-1);
    c->CSGetShaderResources(0, 1, &srv);
    c->CSGetConstantBuffers(0, 1, &cb);
    c->CSGetSamplers(0, 1, &sampler);
    c->CSGetUnorderedAccessViews(0, 8, uavs);
    c->CSSetUnorderedAccessViews(0, 8, nullUavs, keep);
    c->UpdateSubresource(gpu.constants.Get(), 0, nullptr, values, 0, 0);
    c->CSSetShader(gpu.shader.Get(), nullptr, 0);
    c->CSSetConstantBuffers(0, 1, gpu.constants.GetAddressOf());
    c->CSSetSamplers(0, 1, gpu.sampler.GetAddressOf());
    c->CSSetShaderResources(0, 1, &source);
    c->CSSetUnorderedAccessViews(0, 1, gpu.uav.GetAddressOf(), keep);
    c->Dispatch(8, 8, 8);
    ID3D11ShaderResourceView *nullSrv = nullptr;
    c->CSSetShaderResources(0, 1, &nullSrv);
    c->CSSetUnorderedAccessViews(0, 8, nullUavs, keep);
    c->CSSetShader(shader.Get(), classes, numClasses);
    c->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
    c->CSSetSamplers(0, 1, sampler.GetAddressOf());
    c->CSSetShaderResources(0, 1, srv.GetAddressOf());
    c->CSSetUnorderedAccessViews(0, 8, uavs, keep);
    for (UINT i = 0; i < numClasses; ++i)
        if (classes[i])
            classes[i]->Release();
    for (auto u : uavs)
        if (u)
            u->Release();
    return true;
}
void bindShared(Bind forwardBind, ID3D11DeviceContext *context, UINT start, UINT count,
                ID3D11ShaderResourceView *const *views) {
    ++bindCalls;
    if (transforming || !active.load(std::memory_order_acquire) || !views || count > 128) {
        forwardBind(context, start, count, views);
        return;
    }
    const float v[] = {controls[0].load(), controls[1].load(), controls[2].load(), controls[3].load()};
    if (v[0] == 1 && v[1] == 1 && v[2] == 0 && v[3] == 0) {
        forwardBind(context, start, count, views);
        return;
    }
    void *target = wanted.load();
    for (UINT i = 0; i < count; ++i)
        if (views[i] && views[i] == target) {
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            views[i]->GetDesc(&desc);
            if (desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE3D)
                continue;
            ++cubeViews;
            ComPtr<ID3D11Resource> resource;
            views[i]->GetResource(&resource);
            ++matches;
            std::lock_guard<std::mutex> guard(gpuMutex);
            ComPtr<ID3D11Texture3D> texture;
            if (FAILED(resource.As(&texture)))
                break;
            D3D11_TEXTURE3D_DESC td{};
            texture->GetDesc(&td);
            if (td.Width != 32 || td.Height != 32 || td.Depth != 32)
                break;
            transforming = true;
            bool transformed = transform(context, views[i], v);
            transforming = false;
            if (transformed) {
                std::array<ID3D11ShaderResourceView *, 128> replacement{};
                for (UINT j = 0; j < count; ++j)
                    replacement[j] = views[j];
                replacement[i] = gpu.srv.Get();
                forwardBind(context, start, count, replacement.data());
                ++processed;
                return;
            }
            ++failures;
            break;
        }
    forwardBind(context, start, count, views);
}
void STDMETHODCALLTYPE bind(ID3D11DeviceContext *c, UINT s, UINT n, ID3D11ShaderResourceView *const *v) {
    bindShared(originalBind, c, s, n, v);
}
void STDMETHODCALLTYPE bindCs(ID3D11DeviceContext *c, UINT s, UINT n, ID3D11ShaderResourceView *const *v) {
    bindShared(originalCsBind, c, s, n, v);
}
void STDMETHODCALLTYPE bindDeferredPs(ID3D11DeviceContext *c, UINT s, UINT n,
                                      ID3D11ShaderResourceView *const *v) {
    bindShared(originalDeferredPs, c, s, n, v);
}
void STDMETHODCALLTYPE bindDeferredCs(ID3D11DeviceContext *c, UINT s, UINT n,
                                      ID3D11ShaderResourceView *const *v) {
    bindShared(originalDeferredCs, c, s, n, v);
}
void STDMETHODCALLTYPE bindLive(ID3D11DeviceContext *c, UINT s, UINT n, ID3D11ShaderResourceView *const *v) {
    bindShared(originalLiveBind, c, s, n, v);
}
} // namespace
bool install(std::string &error) {
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(hlsl, std::strlen(hlsl), "VoidPhotoColor", nullptr, nullptr, "main", "cs_5_0",
                          D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                          &errors))) {
        error = "Color compute shader compilation failed";
        return false;
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                 &device, &level, &context))) {
        error = "Cannot inspect D3D11 context";
        return false;
    }
    auto table = *reinterpret_cast<void ***>(context.Get());
    initialTarget = table[8];
    auto r =
        MH_CreateHook(table[8], reinterpret_cast<void *>(bind), reinterpret_cast<void **>(&originalBind));
    if (r != MH_OK) {
        error = MH_StatusToString(r);
        return false;
    }
    r = MH_CreateHook(table[67], reinterpret_cast<void *>(bindCs),
                      reinterpret_cast<void **>(&originalCsBind));
    if (r != MH_OK) {
        error = MH_StatusToString(r);
        return false;
    }
    ComPtr<ID3D11DeviceContext> deferred;
    if (FAILED(device->CreateDeferredContext(0, &deferred))) {
        error = "Cannot inspect deferred context";
        return false;
    }
    auto dt = *reinterpret_cast<void ***>(deferred.Get());
    if (dt[8] != table[8]) {
        r = MH_CreateHook(dt[8], reinterpret_cast<void *>(bindDeferredPs),
                          reinterpret_cast<void **>(&originalDeferredPs));
        if (r != MH_OK) {
            error = MH_StatusToString(r);
            return false;
        }
    }
    if (dt[67] != table[67]) {
        r = MH_CreateHook(dt[67], reinterpret_cast<void *>(bindDeferredCs),
                          reinterpret_cast<void **>(&originalDeferredCs));
        if (r != MH_OK) {
            error = MH_StatusToString(r);
            return false;
        }
    }
    return true;
}
void begin() {
    reset();
    wanted = nullptr;
    active.store(true, std::memory_order_release);
}
void end() {
    active = false;
    reset();
    wanted = nullptr;
}
void reset() {
    controls[0] = 1;
    controls[1] = 1;
    controls[2] = 0;
    controls[3] = 0;
}
void trackCube(void *wrapper) {
    // Native idImage::imageData: width/height/depth +0x18; D3D shader-resource view +0x30.
    if (!active.load() || !wrapper)
        return;
    auto bytes = static_cast<unsigned char *>(wrapper);
    if (*reinterpret_cast<int *>(bytes + 0x18) == 32 && *reinterpret_cast<int *>(bytes + 0x1c) == 32 &&
        *reinterpret_cast<int *>(bytes + 0x20) == 32) {
        auto view = *reinterpret_cast<ID3D11ShaderResourceView **>(bytes + 0x30);
        wanted = view;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        if (view)
            view->GetDesc(&vd);
        targetDimension = vd.ViewDimension;
        if (view && liveHook.load() == -999) {
            ComPtr<ID3D11Device> device;
            view->GetDevice(&device);
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            auto target = (*reinterpret_cast<void ***>(context.Get()))[8];
            if (target == initialTarget)
                liveHook = 0;
            else {
                auto result = MH_CreateHook(target, reinterpret_cast<void *>(bindLive),
                                            reinterpret_cast<void **>(&originalLiveBind));
                if (result == MH_OK)
                    result = MH_EnableHook(target);
                liveHook = static_cast<int>(result) + 1;
            }
        }
    }
}
float get(int i) {
    return i >= 0 && i < 4 ? controls[i].load() : 0;
}
bool set(int i, float v) {
    if (!active.load() || i < 0 || i > 3 || !std::isfinite(v))
        return false;
    const float lo[] = {0, 0.5f, -1, -1}, hi[] = {2, 1.5f, 1, 1};
    if (v < lo[i] || v > hi[i])
        return false;
    controls[i] = v;
    return true;
}
std::string telemetry() {
    std::ostringstream s;
    s << "\"color_target_dimension\":" << targetDimension.load()
      << ",\"color_bind_calls\":" << bindCalls.load() << ",\"color_cube_views\":" << cubeViews.load()
      << ",\"color_live_hook\":" << liveHook.load() << ",\"color_matches\":" << matches.load()
      << ",\"color_dispatches\":" << processed.load() << ",\"color_failures\":" << failures.load()
      << ",\"color_context_type\":" << contextType.load()
      << ",\"color_source_found\":" << (wanted.load() ? "true" : "false") << ",\"saturation\":" << get(0)
      << ",\"contrast\":" << get(1) << ",\"warmth\":" << get(2) << ",\"tint\":" << get(3);
    return s.str();
}
} // namespace ColorFx
