#pragma once

#include "rtweekend.h"

#include <string>
#include <vector>

// Plain-data scene shared by the CPU and GPU renderers.

enum MaterialType { MAT_LAMBERTIAN = 0, MAT_METAL = 1, MAT_DIELECTRIC = 2, MAT_LIGHT = 3 };

struct MaterialDesc {
    MaterialType type;
    color albedo;
    double fuzz = 0;
    double ior = 1.5;
    color emit;
};

struct SphereDesc {
    point3 center;
    double radius;
    int material;
};

struct TriangleDesc {
    point3 v[3];
    vec3 n[3];
    bool has_normals = false;
    int material;
};

struct SceneDesc {
    std::string name;
    std::vector<MaterialDesc> materials;
    std::vector<SphereDesc> spheres;
    std::vector<TriangleDesc> triangles;

    double vfov = 90;
    point3 lookfrom, lookat;
    double defocus_angle = 0;
    double focus_dist = 10;
    double move_speed = 1;

    bool sky_gradient = true;
    color background;

    int lambertian(const color& albedo) { return add({MAT_LAMBERTIAN, albedo, 0, 1.5, color()}); }
    int metal(const color& albedo, double fuzz) { return add({MAT_METAL, albedo, fuzz < 1 ? fuzz : 1, 1.5, color()}); }
    int dielectric(double ior) { return add({MAT_DIELECTRIC, color(1, 1, 1), 0, ior, color()}); }
    int light(const color& emit) { return add({MAT_LIGHT, color(), 0, 1.5, emit}); }

    void sphere(const point3& center, double radius, int material) {
        spheres.push_back({center, radius, material});
    }

  private:
    int add(const MaterialDesc& m) {
        materials.push_back(m);
        return int(materials.size()) - 1;
    }
};
