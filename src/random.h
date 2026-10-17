#pragma once

#include <cstdint>

// Per-thread PCG state, reseeded per pixel from (x, y, frame).

inline thread_local uint32_t rng_state = 1;

inline uint32_t hash_u32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

inline void seed_random(uint32_t x, uint32_t y, uint32_t frame) {
    rng_state = hash_u32(x + hash_u32(y + hash_u32(frame)));
}

inline uint32_t random_u32() {
    uint32_t state = rng_state;
    rng_state = state * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// [0, 1)
inline double random_double() {
    return random_u32() * (1.0 / 4294967296.0);
}

inline double random_double(double min, double max) {
    return min + (max - min) * random_double();
}

inline int random_int(int min, int max) {
    return int(random_double(min, max + 1));
}
