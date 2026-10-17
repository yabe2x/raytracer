// Usage: raytracer [--bench | --reel <dir>]

#include "cpu_renderer.h"
#include "demo_reel.h"
#include "gpu_renderer.h"
#include "image_io.h"
#include "scene.h"
#include "scenes.h"
#include "window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static const int WINDOW_SCALE = 2;

using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

static int run_benchmark(GpuRenderer* gpu, CpuRenderer& cpu) {
    std::printf("\n%-12s %10s %14s %10s %14s %8s\n", "scene", "CPU ms", "CPU Msamples/s", "GPU ms", "GPU Msamples/s",
                "speedup");
    uint32_t seed = 1;
    for (int sc = 1; sc <= SCENE_COUNT; sc++) {
        scene_load(sc);
        char path[64];

        scene_resize(CPU_WIDTH, CPU_HEIGHT);
        const int cpu_frames = 20;
        auto t = Clock::now();
        for (int f = 0; f < cpu_frames; f++) cpu.render(uint32_t(f), seed++);
        double cpu_ms = ms_since(t) / cpu_frames;
        double cpu_rate = CPU_WIDTH * CPU_HEIGHT / (cpu_ms * 1000.0);
        std::snprintf(path, sizeof(path), "bench_scene%d_cpu.ppm", sc);
        save_ppm(path, cpu.pixels, CPU_WIDTH, CPU_HEIGHT);

        if (!gpu) {
            std::printf("%-12s %10.1f %14.1f\n", scene_name(), cpu_ms, cpu_rate);
            continue;
        }

        scene_resize(GPU_WIDTH, GPU_HEIGHT);
        gpu->upload_scene(scene_desc());
        CameraParams cam;
        scene_camera(cam);
        gpu->render(cam, GPU_WIDTH, GPU_HEIGHT, 0, seed++, 1, MAX_DEPTH);  // warm-up
        gpu->finish();
        const int gpu_frames = 50;
        t = Clock::now();
        for (int f = 0; f < gpu_frames; f++) gpu->render(cam, GPU_WIDTH, GPU_HEIGHT, uint32_t(f), seed++, 1, MAX_DEPTH);
        gpu->finish();
        double gpu_ms = ms_since(t) / gpu_frames;
        double gpu_rate = GPU_WIDTH * GPU_HEIGHT / (gpu_ms * 1000.0);

        for (int f = 0; f < 64; f++) gpu->render(cam, GPU_WIDTH, GPU_HEIGHT, uint32_t(f), seed++, 8, MAX_DEPTH);
        gpu->finish();
        gpu->present_gpu(GPU_WIDTH, GPU_HEIGHT);
        std::vector<uint32_t> img;
        gpu->read_pixels(GPU_WIDTH, GPU_HEIGHT, img);
        std::snprintf(path, sizeof(path), "bench_scene%d_gpu.ppm", sc);
        save_ppm(path, img, GPU_WIDTH, GPU_HEIGHT);

        std::printf("%-12s %10.1f %14.1f %10.2f %14.1f %7.0fx\n", scene_name(), cpu_ms, cpu_rate, gpu_ms, gpu_rate,
                    gpu_rate / cpu_rate);
    }
    return 0;
}

int main(int argc, char** argv) {
    bool bench = argc > 1 && std::strcmp(argv[1], "--bench") == 0;

    Window window(CPU_WIDTH, CPU_HEIGHT, WINDOW_SCALE, "raytracer");
    scene_init(CPU_WIDTH, CPU_HEIGHT);

    GpuRenderer gpu;
    bool gpu_ok = gpu.init(window, GPU_WIDTH, GPU_HEIGHT);
    if (gpu_ok)
        std::printf("GPU: %s\n", gpu.device().c_str());
    else
        std::printf("GPU renderer off: %s\nUsing the CPU renderer.\n", gpu.error().c_str());

    CpuRenderer cpu;
    if (bench) return run_benchmark(gpu_ok ? &gpu : nullptr, cpu);
    if (argc > 2 && std::strcmp(argv[1], "--reel") == 0) {
        if (!gpu_ok) {
            std::printf("The demo reel needs the GPU renderer.\n");
            return 1;
        }
        return run_demo_reel(window, gpu, cpu, argv[2]);
    }

    bool use_gpu = gpu_ok;
    uint32_t frame = 0;
    uint32_t samples = 0;
    uint32_t total_frames = 0;  // used as the RNG seed, so it never resets
    int gpu_samples_per_frame = 1;
    int uploaded_version = -1;

    auto restart = [&]() {
        frame = 0;
        samples = 0;
    };
    auto set_mode = [&](bool gpu_mode) {
        use_gpu = gpu_mode;
        if (use_gpu)
            scene_resize(GPU_WIDTH, GPU_HEIGHT);
        else
            scene_resize(CPU_WIDTH, CPU_HEIGHT);
        restart();
    };
    set_mode(use_gpu);

    auto last_time = Clock::now();

    while (window.poll()) {
        auto start = Clock::now();
        double dt = std::chrono::duration<double>(start - last_time).count();
        last_time = start;
        dt = std::min(dt, 0.1);

        if (window.key_pressed(KEY_ESCAPE)) break;
        if (window.key_pressed('R')) restart();
        if (window.key_pressed('G') && gpu_ok) set_mode(!use_gpu);
        if (window.key_pressed('B')) {
            scene_set_bvh(!scene_bvh());
            restart();
        }
        for (int i = 1; i <= SCENE_COUNT; ++i) {
            if (window.key_pressed('0' + i) && scene_load(i)) {
                set_mode(use_gpu);
            }
        }
        if (window.key_pressed('P')) {
            if (use_gpu) {
                std::vector<uint32_t> img;
                gpu.read_pixels(GPU_WIDTH, GPU_HEIGHT, img);
                save_ppm("image.ppm", img, GPU_WIDTH, GPU_HEIGHT);
            } else {
                save_ppm("image.ppm", cpu.pixels, CPU_WIDTH, CPU_HEIGHT);
            }
        }

        CameraInput input;
        input.forward = window.key_down('W') - window.key_down('S');
        input.right = window.key_down('D') - window.key_down('A');
        input.up = window.key_down('E') - window.key_down('Q');
        input.yaw = window.key_down(KEY_RIGHT) - window.key_down(KEY_LEFT);
        input.pitch = window.key_down(KEY_UP) - window.key_down(KEY_DOWN);
        if (scene_update(input, dt)) restart();

        if (use_gpu) {
            if (uploaded_version != scene_version()) {
                gpu.upload_scene(scene_desc());
                uploaded_version = scene_version();
            }
            CameraParams cam;
            scene_camera(cam);
            gpu.render(cam, GPU_WIDTH, GPU_HEIGHT, frame, total_frames, gpu_samples_per_frame, MAX_DEPTH);
            gpu.finish();
            samples += gpu_samples_per_frame;
            gpu.present_gpu(GPU_WIDTH, GPU_HEIGHT);

            // adapt samples per frame to hold ~60 fps
            double ms = ms_since(start);
            if (ms < 12 && gpu_samples_per_frame < 64) gpu_samples_per_frame++;
            else if (ms > 20 && gpu_samples_per_frame > 1) gpu_samples_per_frame--;
        } else {
            cpu.render(frame, total_frames);
            samples += 1;
            if (gpu_ok)
                gpu.present_cpu(cpu.pixels.data(), CPU_WIDTH, CPU_HEIGHT);
            else
                window.present(cpu.pixels.data());
        }
        ++frame;
        ++total_frames;

        double ms = ms_since(start);
        char title[200];
        std::snprintf(title, sizeof(title),
                      "raytracer  |  %s  |  %s%s  |  %u samples  |  %.0f fps  |  1-4 scenes, G cpu/gpu, WASD+arrows",
                      scene_name(), use_gpu ? "GPU" : "CPU", (!use_gpu && !scene_bvh()) ? " (no BVH)" : "", samples,
                      1000.0 / std::max(ms, 0.1));
        window.set_title(title);
    }
    return 0;
}
