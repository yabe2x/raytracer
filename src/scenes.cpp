#include "scenes.h"
#include "obj_loader.h"

#ifndef MODELS_DIR
#define MODELS_DIR "models"
#endif

static void scene_book_cover(SceneDesc& s) {
    s.name = "Book cover";
    s.sphere(point3(0, -1000, 0), 1000, s.lambertian(color(0.5, 0.5, 0.5)));

    seed_random(1, 2, 3);
    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            double choose_mat = random_double();
            point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

            if ((center - point3(4, 0.2, 0)).length() > 0.9) {
                int mat;
                if (choose_mat < 0.8) {
                    mat = s.lambertian(color::random() * color::random());
                } else if (choose_mat < 0.95) {
                    color albedo = color::random(0.5, 1);
                    double fuzz = random_double(0, 0.5);
                    mat = s.metal(albedo, fuzz);
                } else {
                    mat = s.dielectric(1.5);
                }
                s.sphere(center, 0.2, mat);
            }
        }
    }

    s.sphere(point3(0, 1, 0), 1.0, s.dielectric(1.5));
    s.sphere(point3(-4, 1, 0), 1.0, s.lambertian(color(0.4, 0.2, 0.1)));
    s.sphere(point3(4, 1, 0), 1.0, s.metal(color(0.7, 0.6, 0.5), 0.0));

    s.vfov = 20;
    s.lookfrom = point3(13, 2, 3);
    s.lookat = point3(0, 0, 0);
    s.defocus_angle = 0.6;
    s.focus_dist = 10.0;
    s.move_speed = 4;
}

static void scene_materials(SceneDesc& s) {
    s.name = "Materials";
    s.sphere(point3(0.0, -100.5, -1.0), 100.0, s.lambertian(color(0.8, 0.8, 0.0)));
    s.sphere(point3(0.0, 0.0, -1.2), 0.5, s.lambertian(color(0.1, 0.2, 0.5)));
    s.sphere(point3(-1.0, 0.0, -1.0), 0.5, s.dielectric(1.50));
    s.sphere(point3(-1.0, 0.0, -1.0), 0.4, s.dielectric(1.00 / 1.50));
    s.sphere(point3(1.0, 0.0, -1.0), 0.5, s.metal(color(0.8, 0.6, 0.2), 0.2));

    s.vfov = 40;
    s.lookfrom = point3(-2, 1.5, 1.5);
    s.lookat = point3(0, 0, -1);
    s.focus_dist = 3.4;
    s.move_speed = 1.5;
}

static void scene_glow(SceneDesc& s) {
    s.name = "Glow";
    s.sphere(point3(0, -1000, 0), 1000, s.lambertian(color(0.7, 0.7, 0.7)));

    s.sphere(point3(-2.2, 0.8, 0), 0.8, s.lambertian(color(0.9, 0.2, 0.2)));
    s.sphere(point3(0, 1, 0), 1.0, s.metal(color(0.9, 0.9, 0.9), 0.05));
    s.sphere(point3(2.2, 0.8, 0), 0.8, s.lambertian(color(0.2, 0.4, 0.9)));
    s.sphere(point3(1.1, 0.4, 1.6), 0.4, s.dielectric(1.5));
    s.sphere(point3(-1.1, 0.35, 1.7), 0.35, s.lambertian(color(0.2, 0.8, 0.3)));

    s.sphere(point3(0, 7, 2), 3.0, s.light(color(3, 2.8, 2.4)));
    s.sphere(point3(-4, 1.2, 3), 0.4, s.light(color(8, 3, 1)));

    s.vfov = 40;
    s.lookfrom = point3(0, 2, 8);
    s.lookat = point3(0, 0.8, 0);
    s.focus_dist = 8;
    s.sky_gradient = false;
    s.background = color(0.01, 0.01, 0.02);
    s.move_speed = 3;
}

static void scene_mesh(SceneDesc& s) {
    s.name = "Mesh";
    s.sphere(point3(0, -1000, 0), 1000, s.lambertian(color(0.45, 0.45, 0.5)));

    int gold = s.metal(color(0.95, 0.7, 0.3), 0.08);
    if (load_obj(std::string(MODELS_DIR) + "/torus_knot.obj", s, gold, point3(0, 0, 0), 3.2) == 0) {
        // fallback if the model file is missing
        s.sphere(point3(0, 1.3, 0), 1.3, gold);
    }

    s.sphere(point3(-2.6, 0.7, 1.0), 0.7, s.dielectric(1.5));
    s.sphere(point3(2.5, 0.6, 0.8), 0.6, s.lambertian(color(0.8, 0.15, 0.15)));
    s.sphere(point3(1.4, 0.3, 2.2), 0.3, s.lambertian(color(0.15, 0.35, 0.85)));

    s.vfov = 30;
    s.lookfrom = point3(0, 2.4, 8);
    s.lookat = point3(0, 1.2, 0);
    s.focus_dist = 8;
    s.move_speed = 3;
}

bool build_scene(int index, SceneDesc& out) {
    out = SceneDesc();
    switch (index) {
    case 1: scene_book_cover(out); return true;
    case 2: scene_materials(out); return true;
    case 3: scene_glow(out); return true;
    case 4: scene_mesh(out); return true;
    default: return false;
    }
}
