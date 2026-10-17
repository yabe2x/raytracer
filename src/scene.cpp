#include "scene.h"
#include "rtweekend.h"

#include "bvh.h"
#include "camera.h"
#include "hittable_list.h"
#include "material.h"
#include "scene_desc.h"
#include "scenes.h"
#include "sphere.h"
#include "triangle.h"

static int image_width, image_height;
static camera cam;
static SceneDesc desc;
static int version = 0;
static shared_ptr<hittable> world_bvh;
static shared_ptr<hittable> world_list;
static bool use_bvh = true;

static color background(const ray& r) {
    if (!desc.sky_gradient)
        return desc.background;
    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1.0);
    return (1.0 - a) * color(1.0, 1.0, 1.0) + a * color(0.5, 0.7, 1.0);
}

static color ray_color(const ray& r, int depth, const hittable& world) {
    if (depth <= 0)
        return color(0, 0, 0);

    hit_record rec;
    // t_min > 0 avoids shadow acne
    if (!world.hit(r, interval(0.001, infinity), rec))
        return background(r);

    color emitted = rec.mat->emitted();

    ray scattered;
    color attenuation;
    if (!rec.mat->scatter(r, rec, attenuation, scattered))
        return emitted;

    return emitted + attenuation * ray_color(scattered, depth - 1, world);
}

static void build_world() {
    std::vector<shared_ptr<material>> mats;
    for (const auto& m : desc.materials) {
        switch (m.type) {
        case MAT_LAMBERTIAN: mats.push_back(make_shared<lambertian>(m.albedo)); break;
        case MAT_METAL: mats.push_back(make_shared<metal>(m.albedo, m.fuzz)); break;
        case MAT_DIELECTRIC: mats.push_back(make_shared<dielectric>(m.ior)); break;
        case MAT_LIGHT: mats.push_back(make_shared<diffuse_light>(m.emit)); break;
        }
    }

    hittable_list list;
    for (const auto& s : desc.spheres)
        list.add(make_shared<sphere>(s.center, s.radius, mats[s.material]));
    for (const auto& t : desc.triangles) {
        if (t.has_normals)
            list.add(make_shared<triangle>(t.v[0], t.v[1], t.v[2], t.n[0], t.n[1], t.n[2], mats[t.material]));
        else
            list.add(make_shared<triangle>(t.v[0], t.v[1], t.v[2], mats[t.material]));
    }

    world_list = make_shared<hittable_list>(list);
    world_bvh = make_shared<bvh_node>(list);
}

void scene_init(int w, int h) {
    image_width = w;
    image_height = h;
    scene_load(1);
}

void scene_resize(int w, int h) {
    image_width = w;
    image_height = h;
    cam.update(image_width, image_height);
}

bool scene_load(int index) {
    SceneDesc next;
    if (!build_scene(index, next))
        return false;
    desc = std::move(next);
    build_world();

    cam = camera();
    cam.vfov = desc.vfov;
    cam.look_at(desc.lookfrom, desc.lookat);
    cam.defocus_angle = desc.defocus_angle;
    cam.focus_dist = desc.focus_dist;
    cam.update(image_width, image_height);
    version++;
    return true;
}

const char* scene_name() { return desc.name.c_str(); }

const SceneDesc& scene_desc() { return desc; }

int scene_version() { return version; }

void scene_set_bvh(bool on) { use_bvh = on; }

bool scene_bvh() { return use_bvh; }

bool scene_update(const CameraInput& in, double dt) {
    if (!in.forward && !in.right && !in.up && !in.yaw && !in.pitch)
        return false;

    const double turn_speed = 1.2;
    double move = desc.move_speed * dt;

    // keep forward movement level with the ground
    vec3 fwd = cam.forward();
    vec3 flat_forward = unit_vector(vec3(fwd.x(), 0, fwd.z()));
    cam.lookfrom += (in.forward * move) * flat_forward;
    cam.lookfrom += (in.right * move) * cam.right();
    cam.lookfrom += (in.up * move) * vec3(0, 1, 0);

    cam.yaw += in.yaw * turn_speed * dt;
    cam.pitch += in.pitch * turn_speed * dt;
    cam.pitch = std::fmax(-1.5, std::fmin(1.5, cam.pitch));

    cam.update(image_width, image_height);
    return true;
}

void scene_look_at(const double from[3], const double at[3]) {
    cam.look_at(point3(from[0], from[1], from[2]), point3(at[0], at[1], at[2]));
    cam.update(image_width, image_height);
}

static void to_floats(const vec3& v, float out[3]) {
    out[0] = float(v.x());
    out[1] = float(v.y());
    out[2] = float(v.z());
}

void scene_camera(CameraParams& out) {
    to_floats(cam.lookfrom, out.origin);
    to_floats(cam.first_pixel(), out.pixel00);
    to_floats(cam.pixel_step_u(), out.delta_u);
    to_floats(cam.pixel_step_v(), out.delta_v);
    to_floats(cam.lens_u(), out.defocus_u);
    to_floats(cam.lens_v(), out.defocus_v);
    out.defocus = cam.defocus_angle > 0 ? 1 : 0;
}

void scene_sample(int x, int y, uint32_t frame, float out_rgb[3]) {
    seed_random(uint32_t(x), uint32_t(y), frame);

    ray r = cam.get_ray(x, y);
    color c = ray_color(r, MAX_DEPTH, use_bvh ? *world_bvh : *world_list);

    out_rgb[0] = float(c.x());
    out_rgb[1] = float(c.y());
    out_rgb[2] = float(c.z());
}
