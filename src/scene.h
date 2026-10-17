#pragma once

#include <cstdint>

struct SceneDesc;

const int MAX_DEPTH = 20;

// each field is -1, 0, or +1
struct CameraInput {
    int forward;
    int right;
    int up;
    int yaw;
    int pitch;
};

struct CameraParams {
    float origin[3];
    float pixel00[3];
    float delta_u[3];
    float delta_v[3];
    float defocus_u[3];
    float defocus_v[3];
    int defocus;
};

void scene_init(int image_width, int image_height);

void scene_resize(int image_width, int image_height);

bool scene_load(int index);

const char* scene_name();

// scene_version() changes whenever a new scene is loaded
const SceneDesc& scene_desc();
int scene_version();

// returns true if the camera moved
bool scene_update(const CameraInput& input, double dt);

void scene_camera(CameraParams& out);

void scene_look_at(const double from[3], const double at[3]);

void scene_set_bvh(bool on);
bool scene_bvh();

// one linear-RGB sample for pixel (x, y); called from many threads
void scene_sample(int x, int y, uint32_t frame, float out_rgb[3]);
