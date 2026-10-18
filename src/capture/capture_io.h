#pragma once
#include <d3d11.h>
#include <filesystem>
#include <vector>
#include <string>
namespace PhotoCapture {
struct Pixels {
    UINT width = 0, height = 0;
    std::vector<BYTE> rgb;
};
// Call on the immediate context's render thread. No pipeline bindings are changed.
bool readTexture(ID3D11Texture2D *texture, Pixels &pixels, std::string &error);
// Caller initializes COM. Publishes a complete PNG without replacing an existing file.
bool writePng(const Pixels &pixels, const std::filesystem::path &destination, std::string &error);
} // namespace PhotoCapture
