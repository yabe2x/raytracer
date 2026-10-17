#pragma once

#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

static const int CPU_WIDTH = 640, CPU_HEIGHT = 360;
static const int GPU_WIDTH = 1280, GPU_HEIGHT = 720;

inline uint8_t to_byte(float c) {
    // gamma 2
    if (c > 0)
        c = std::sqrt(c);
    else
        c = 0;
    c = std::min(c, 0.999f);
    return uint8_t(256.0f * c);
}

struct CpuRenderer {
    std::vector<float> accum = std::vector<float>(CPU_WIDTH * CPU_HEIGHT * 3, 0.0f);
    std::vector<uint32_t> pixels = std::vector<uint32_t>(CPU_WIDTH * CPU_HEIGHT, 0);
    unsigned thread_count = std::max(1u, std::thread::hardware_concurrency());

    // frame == 0 resets the accumulation
    void render(uint32_t frame, uint32_t seed) {
        if (frame == 0) std::fill(accum.begin(), accum.end(), 0.0f);
        float inv_samples = 1.0f / float(frame + 1);

        auto render_rows = [&](unsigned first_row) {
            for (int y = int(first_row); y < CPU_HEIGHT; y += int(thread_count)) {
                for (int x = 0; x < CPU_WIDTH; ++x) {
                    float s[3];
                    scene_sample(x, y, seed, s);
                    for (float& v : s)
                        if (!(v == v)) v = 0.0f;

                    float* a = &accum[(y * CPU_WIDTH + x) * 3];
                    a[0] += s[0];
                    a[1] += s[1];
                    a[2] += s[2];
                    pixels[y * CPU_WIDTH + x] = (uint32_t(to_byte(a[0] * inv_samples)) << 16) |
                                                (uint32_t(to_byte(a[1] * inv_samples)) << 8) |
                                                uint32_t(to_byte(a[2] * inv_samples));
                }
            }
        };
        std::vector<std::thread> workers;
        for (unsigned t = 0; t < thread_count; ++t) workers.emplace_back(render_rows, t);
        for (auto& w : workers) w.join();
    }
};
