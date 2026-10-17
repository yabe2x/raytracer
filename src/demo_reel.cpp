#include "demo_reel.h"

#include "cpu_renderer.h"
#include "gpu_renderer.h"
#include "image_io.h"
#include "scene.h"
#include "window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static const int FPS = 30;
static const double PI = 3.14159265358979;

namespace {

struct Reel {
    Window& window;
    GpuRenderer& gpu;
    CpuRenderer& cpu;
    std::string dir;
    FILE* manifest = nullptr;
    int next_frame = 0;
    uint32_t seed = 1000;
    bool aborted = false;

    std::string frame_path(int n, const char* suffix) {
        char name[64];
        std::snprintf(name, sizeof(name), "f%05d%s.ppm", n, suffix);
        return name;
    }

    void gpu_render(int spp, bool fresh, int w = GPU_WIDTH, int h = GPU_HEIGHT) {
        scene_resize(w, h);
        CameraParams cam;
        scene_camera(cam);
        int done = 0;
        while (done < spp) {
            // small chunks so a dispatch never runs long enough to trip the Windows GPU watchdog
            int chunk = std::min(16, spp - done);
            gpu.render(cam, w, h, (fresh && done == 0) ? 0 : 1, seed++, chunk, MAX_DEPTH);
            done += chunk;
        }
        gpu.finish();
    }

    std::string save_gpu(const char* suffix = "", int w = GPU_WIDTH, int h = GPU_HEIGHT) {
        std::vector<uint32_t> img;
        gpu.read_pixels(w, h, img);
        gpu.present_gpu(w, h);
        std::string name = frame_path(next_frame, suffix);
        save_ppm((dir + "/" + name).c_str(), img, w, h, true);
        return name;
    }

    void write_full(const std::string& file, const char* caption, const char* sub) {
        std::fprintf(manifest, "full\t%s\t%s\t%s\n", file.c_str(), caption, sub);
        next_frame++;
        if (!window.poll()) aborted = true;
    }
};

double ease(double u) { return 0.5 - 0.5 * std::cos(PI * u); }

void orbit_camera(const double center[3], double radius, double height, double angle) {
    double from[3] = {center[0] + radius * std::cos(angle), center[1] + height, center[2] + radius * std::sin(angle)};
    scene_look_at(from, center);
}

struct OrbitShot {
    int scene;
    double center[3];
    double radius, height, angle0, angle1;
    int frames, spp;
    const char* caption;
    const char* sub;
    const char* caption2 = nullptr;
    const char* sub2 = nullptr;
};

void orbit_shot(Reel& r, const OrbitShot& s) {
    scene_load(s.scene);
    r.gpu.upload_scene(scene_desc());
    for (int i = 0; i < s.frames && !r.aborted; i++) {
        double u = ease(double(i) / (s.frames - 1));
        orbit_camera(s.center, s.radius, s.height, s.angle0 + (s.angle1 - s.angle0) * u);
        r.gpu_render(s.spp, true);
        bool second = s.caption2 && i >= s.frames / 2;
        r.write_full(r.save_gpu(), second ? s.caption2 : s.caption, second ? s.sub2 : s.sub);
    }
    std::printf("  shot done: %s (%d frames)\n", s.caption, s.frames);
}

// CPU vs GPU at the same resolution and the same elapsed time
void race_shot(Reel& r, int scene, int frames) {
    scene_load(scene);
    r.gpu.upload_scene(scene_desc());

    using Clock = std::chrono::steady_clock;
    const int w = CPU_WIDTH, h = CPU_HEIGHT;
    scene_resize(w, h);
    auto t = Clock::now();
    for (int f = 0; f < 8; f++) r.cpu.render(uint32_t(f), r.seed++);
    double cpu_ms = std::chrono::duration<double, std::milli>(Clock::now() - t).count() / 8;
    r.gpu_render(2, true, w, h);
    t = Clock::now();
    r.gpu_render(256, true, w, h);
    double gpu_ms = std::chrono::duration<double, std::milli>(Clock::now() - t).count() / 256;
    std::printf("  race: CPU %.1f ms/sample, GPU %.2f ms/sample\n", cpu_ms, gpu_ms);

    std::string gpu_name = r.gpu.device().substr(0, r.gpu.device().find('/'));
    int cpu_done = 0, gpu_done = 0;
    for (int i = 0; i < frames && !r.aborted; i++) {
        double elapsed_ms = 1000.0 * (i + 1) / FPS;
        int cpu_target = int(elapsed_ms / cpu_ms);
        int gpu_target = std::min(4096, int(elapsed_ms / gpu_ms));

        scene_resize(w, h);
        while (cpu_done < cpu_target) {
            r.cpu.render(uint32_t(cpu_done), r.seed++);
            cpu_done++;
        }
        if (gpu_target > gpu_done) {
            r.gpu_render(gpu_target - gpu_done, gpu_done == 0, w, h);
            gpu_done = gpu_target;
        }

        std::string cpu_file = r.frame_path(r.next_frame, "_cpu");
        if (cpu_done > 0)
            save_ppm((r.dir + "/" + cpu_file).c_str(), r.cpu.pixels, w, h, true);
        else
            save_ppm((r.dir + "/" + cpu_file).c_str(), std::vector<uint32_t>(w * h, 0), w, h, true);
        std::string gpu_file = gpu_done > 0 ? r.save_gpu("_gpu", w, h) : cpu_file;

        char left[128], right[160], sub[96];
        std::snprintf(left, sizeof(left), "CPU  %u threads  -  %d samples", r.cpu.thread_count, cpu_done);
        std::snprintf(right, sizeof(right), "GPU  %s  -  %d samples", gpu_name.c_str(), gpu_done);
        std::snprintf(sub, sizeof(sub), "both at %dx%d  |  after %.1f seconds", w, h, elapsed_ms / 1000.0);
        std::fprintf(r.manifest, "split\t%s\t%s\tSame scene, same time\t%s\t%s\t%s\n", cpu_file.c_str(),
                     gpu_file.c_str(), sub, left, right);
        r.next_frame++;
        if (!r.window.poll()) r.aborted = true;
    }
    std::printf("  shot done: CPU vs GPU race (%d frames)\n", frames);
}

void accumulate_shot(Reel& r, int scene, int frames, int max_spp) {
    scene_load(scene);
    r.gpu.upload_scene(scene_desc());
    int done = 0;
    for (int i = 0; i < frames && !r.aborted; i++) {
        int target = int(std::round(std::pow(double(max_spp), double(i) / (frames - 1))));
        if (target > done) {
            r.gpu_render(target - done, done == 0);
            done = target;
        }
        char sub[64];
        std::snprintf(sub, sizeof(sub), "%d sample%s per pixel", done, done == 1 ? "" : "s");
        r.write_full(r.save_gpu(), "Progressive rendering", sub);
    }
    std::printf("  shot done: accumulation (%d frames)\n", frames);
}

void still_shot(Reel& r, int scene, int frames, int spp, const char* caption, const char* sub) {
    scene_load(scene);
    r.gpu.upload_scene(scene_desc());
    r.gpu_render(spp, true);
    std::string file = r.save_gpu();
    for (int i = 0; i < frames && !r.aborted; i++) r.write_full(file, caption, sub);
}

}  // namespace

int run_demo_reel(Window& window, GpuRenderer& gpu, CpuRenderer& cpu, const char* out_dir) {
    Reel r{window, gpu, cpu, out_dir};
    r.manifest = std::fopen((r.dir + "/reel.txt").c_str(), "w");
    if (!r.manifest) {
        std::printf("Can't write to %s\n", out_dir);
        return 1;
    }
    std::fprintf(r.manifest, "fps\t%d\n", FPS);
    auto start = std::chrono::steady_clock::now();

    OrbitShot opening = {1, {0, 0, 0}, 13.34, 2, 0.2268, 0.2268 + 0.5, 6 * FPS, 32,
                         "Real-time path tracer", "C++  |  OpenGL compute shaders  |  no graphics libraries",
                         "486 spheres", "matte, metal, and glass  |  depth of field"};
    orbit_shot(r, opening);

    if (!r.aborted) accumulate_shot(r, 3, 5 * FPS, 4096);

    OrbitShot glow = {3, {0, 0.8, 0}, 8, 1.2, PI / 2, PI / 2 - 0.5, 4 * FPS, 256,
                      "Emissive lights", "lit only by glowing spheres"};
    if (!r.aborted) orbit_shot(r, glow);

    if (!r.aborted) race_shot(r, 3, 4 * FPS);

    OrbitShot mesh = {4, {0, 1.2, 0}, 8, 1.2, PI / 2, PI / 2 + 1.3, 8 * FPS, 48,
                      "46,080-triangle mesh", "OBJ loader  |  smooth normals  |  BVH on CPU and GPU",
                      "BVH: 1,340x faster", "172 s to 0.13 s per frame on the CPU"};
    if (!r.aborted) orbit_shot(r, mesh);

    OrbitShot mats = {2, {0, 0, -1}, 3.2, 1.5, 2.245, 2.245 - 0.8, 5 * FPS, 128,
                      "Glass, metal, matte", "refraction  |  fuzzy reflection  |  Lambertian diffuse"};
    if (!r.aborted) orbit_shot(r, mats);

    if (!r.aborted)
        still_shot(r, 1, int(2.5 * FPS), 1024, "Real-time GPU path tracer",
                   "47-205x faster than the CPU version  |  C++  |  OpenGL 4.3");

    std::fclose(r.manifest);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%s: %d frames (%.1f s of video) in %.0f s\n", r.aborted ? "Stopped early" : "Done", r.next_frame,
                double(r.next_frame) / FPS, secs);
    return r.aborted ? 1 : 0;
}
