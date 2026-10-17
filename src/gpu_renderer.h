#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Window;
struct SceneDesc;
struct CameraParams;

// OpenGL compute-shader path tracer. Also presents CPU frames.
class GpuRenderer {
  public:
    // returns false if OpenGL 4.3 isn't available; see error()
    bool init(Window& window, int max_width, int max_height);

    const std::string& error() const { return err; }
    const std::string& device() const { return gpu_name; }

    void upload_scene(const SceneDesc& scene);

    // frame == 0 resets the accumulation; seed must differ every call
    void render(const CameraParams& cam, int width, int height, uint32_t frame, uint32_t seed, int samples,
                int max_depth);

    void finish();

    void present_gpu(int width, int height);

    // pixels are 0x00RRGGBB, top row first
    void present_cpu(const uint32_t* pixels, int width, int height);

    void read_pixels(int width, int height, std::vector<uint32_t>& out);

  private:
    Window* window = nullptr;
    std::string err, gpu_name;
    int max_w = 0, max_h = 0;
    unsigned program = 0;
    unsigned accum_tex = 0, display_tex = 0, read_fbo = 0;
    unsigned buffers[5] = {};
    int sky_gradient = 1;
    float background[3] = {0, 0, 0};

    bool load_functions();
    bool compile_shader();
    void blit(int src_w, int src_h);
};
