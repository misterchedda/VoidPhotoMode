#include "capture/capture_io.h"
#include "capture/partial_file.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <limits>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace PhotoCapture {
namespace {
bool hr(HRESULT result, const char *operation, std::string &error) {
    if (SUCCEEDED(result))
        return true;
    std::ostringstream s;
    s << operation << " failed (0x" << std::hex << unsigned(result) << ")";
    error = s.str();
    return false;
}
} // namespace
bool readTexture(ID3D11Texture2D *texture, Pixels &pixels, std::string &error) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (!rgba && !bgra) {
        error = "Capture requires an 8-bit SDR back buffer";
        return false;
    }
    if (!desc.Width || !desc.Height || desc.ArraySize != 1 || desc.MipLevels != 1 ||
        uint64_t(desc.Width) * desc.Height * 4 > std::numeric_limits<UINT>::max()) {
        error = "Unsupported capture dimensions";
        return false;
    }
    ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    ComPtr<ID3D11Texture2D> source = texture, resolved, staging;
    if (desc.SampleDesc.Count > 1) {
        desc.SampleDesc = {1, 0};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        if (!hr(device->CreateTexture2D(&desc, nullptr, &resolved), "Resolve texture", error))
            return false;
        context->ResolveSubresource(resolved.Get(), 0, texture, 0, desc.Format);
        source = resolved;
    }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (!hr(device->CreateTexture2D(&desc, nullptr, &staging), "Readback texture", error))
        return false;
    // Allocate before Map so allocation failure cannot leave a mapped resource.
    Pixels result;
    result.width = desc.Width;
    result.height = desc.Height;
    result.rgb.resize(size_t(desc.Width) * desc.Height * 3);
    context->CopyResource(staging.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (!hr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Readback map", error))
        return false;
    if (map.RowPitch < desc.Width * 4) {
        context->Unmap(staging.Get(), 0);
        error = "Invalid GPU row pitch";
        return false;
    }
    for (UINT y = 0; y < desc.Height; ++y) {
        auto src = static_cast<const BYTE *>(map.pData) + size_t(y) * map.RowPitch;
        auto dst = result.rgb.data() + size_t(y) * desc.Width * 3;
        for (UINT x = 0; x < desc.Width; ++x) {
            dst[3 * x] = src[4 * x + (bgra ? 2 : 0)];
            dst[3 * x + 1] = src[4 * x + 1];
            dst[3 * x + 2] = src[4 * x + (bgra ? 0 : 2)];
        }
    }
    context->Unmap(staging.Get(), 0);
    pixels = std::move(result);
    return true;
}
bool writePng(const Pixels &pixels, const std::filesystem::path &destination, std::string &error) {
    uint64_t size = uint64_t(pixels.width) * pixels.height * 3;
    if (!pixels.width || !pixels.height || size != pixels.rgb.size() ||
        size > std::numeric_limits<UINT>::max()) {
        error = "Invalid PNG pixel buffer";
        return false;
    }
    auto temporary = destination;
    temporary += L".partial";
    HANDLE reservation =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (reservation == INVALID_HANDLE_VALUE) {
        error = "Cannot create capture file";
        return false;
    }
    CloseHandle(reservation);
    PartialFile cleanup(temporary);
    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ok = hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&factory)),
                "PNG factory", error) &&
             hr(factory->CreateStream(&stream), "PNG stream", error) &&
             hr(stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE), "PNG file", error) &&
             hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder", error) &&
             hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG initialize", error) &&
             hr(encoder->CreateNewFrame(&frame, nullptr), "PNG frame", error) &&
             hr(frame->Initialize(nullptr), "PNG frame initialize", error) &&
             hr(frame->SetSize(pixels.width, pixels.height), "PNG dimensions", error);
        std::vector<BYTE> bgr = pixels.rgb;
        for (size_t i = 0; i < bgr.size(); i += 3)
            std::swap(bgr[i], bgr[i + 2]);
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        if (ok)
            ok = hr(frame->SetPixelFormat(&format), "PNG format", error) &&
                 IsEqualGUID(format, GUID_WICPixelFormat24bppBGR);
        if (ok) {
            ComPtr<IWICMetadataQueryWriter> metadata;
            ok = hr(frame->GetMetadataQueryWriter(&metadata), "PNG metadata", error);
            if (ok) {
                PROPVARIANT value{};
                value.vt = VT_UI1;
                value.bVal = 0;
                ok = hr(metadata->SetMetadataByName(L"/sRGB/RenderingIntent", &value), "PNG sRGB", error);
            }
        }
        if (ok)
            ok = hr(frame->WritePixels(pixels.height, pixels.width * 3, UINT(size), bgr.data()), "PNG pixels",
                    error) &&
                 hr(frame->Commit(), "PNG frame commit", error) && hr(encoder->Commit(), "PNG commit", error);
    }
    if (ok && !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot publish PNG (destination exists or disk unavailable)";
        ok = false;
    }
    if (ok)
        cleanup.published();
    if (!ok && error.empty())
        error = "PNG format unsupported";
    return ok;
}
} // namespace PhotoCapture
