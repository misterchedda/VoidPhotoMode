#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace HudPatch {
bool apply(const unsigned char *original, size_t size, std::vector<unsigned char> &output);
bool decode(const unsigned char *original, size_t size, const unsigned char *patch, size_t patchSize,
            std::vector<unsigned char> &output);
} // namespace HudPatch
