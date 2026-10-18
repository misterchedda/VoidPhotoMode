#include "ui/hud_patch.h"
#include <cstring>
#include "generated/hud_patch_data.inc"
namespace HudPatch {
namespace {
uint64_t fingerprint(const unsigned char *p, size_t size) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
} // namespace
bool decode(const unsigned char *original, size_t size, const unsigned char *patch, size_t patchSize,
            std::vector<unsigned char> &output) {
    if (patchSize < 28 || memcmp(patch, "VPD1", 4))
        return false;
    size_t pos = 4;
    uint32_t sourceSize, targetSize;
    uint64_t sourceHash, targetHash;
    memcpy(&sourceSize, patch + pos, 4);
    pos += 4;
    memcpy(&targetSize, patch + pos, 4);
    pos += 4;
    memcpy(&sourceHash, patch + pos, 8);
    pos += 8;
    memcpy(&targetHash, patch + pos, 8);
    pos += 8;
    if (size != sourceSize || targetSize > 8 * 1024 * 1024 || fingerprint(original, size) != sourceHash)
        return false;
    std::vector<unsigned char> result;
    result.reserve(targetSize);
    while (pos < patchSize) {
        unsigned char op = patch[pos++];
        uint32_t count = 0, offset = 0;
        if (op == 0) {
            if (patchSize - pos < 8)
                return false;
            memcpy(&offset, patch + pos, 4);
            pos += 4;
            memcpy(&count, patch + pos, 4);
            pos += 4;
            if (offset > size || count > size - offset || count > targetSize - result.size())
                return false;
            result.insert(result.end(), original + offset, original + offset + count);
        } else if (op == 1) {
            if (patchSize - pos < 4)
                return false;
            memcpy(&count, patch + pos, 4);
            pos += 4;
            if (count > patchSize - pos || count > targetSize - result.size())
                return false;
            result.insert(result.end(), patch + pos, patch + pos + count);
            pos += count;
        } else
            return false;
    }
    if (result.size() != targetSize || fingerprint(result.data(), result.size()) != targetHash)
        return false;
    output.swap(result);
    return true;
}
bool apply(const unsigned char *original, size_t size, std::vector<unsigned char> &output) {
    return decode(original, size, kHudPatch, sizeof(kHudPatch), output);
}
} // namespace HudPatch
