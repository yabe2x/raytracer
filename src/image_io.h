#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

inline void save_ppm(const char* path, const std::vector<uint32_t>& pixels, int width, int height,
                     bool quiet = false) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    std::vector<unsigned char> row(size_t(width) * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t p = pixels[size_t(y) * width + x];
            row[x * 3 + 0] = uint8_t(p >> 16);
            row[x * 3 + 1] = uint8_t(p >> 8);
            row[x * 3 + 2] = uint8_t(p);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    if (!quiet) std::printf("Saved %s\n", path);
}
