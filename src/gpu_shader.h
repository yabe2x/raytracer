#pragma once

// Compute shader port of the CPU path tracer. Iterative bounce loop instead of
// recursion, flat BVH walked with an explicit stack.
static const char* GPU_SHADER_SOURCE = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba32f, binding = 0) uniform image2D accum_img;
layout(rgba8, binding = 1) uniform writeonly image2D display_img;

// must match the structs in gpu_renderer.cpp
struct Material { vec3 albedo; int type; vec3 emit; float fuzz; float ior; float pad0; float pad1; float pad2; };
struct Sphere   { vec3 center; float radius; int mat; int pad0; int pad1; int pad2; };
struct Triangle { vec3 v0; int mat; vec3 v1; int has_normals; vec3 v2; float pad0;
                  vec3 n0; float pad1; vec3 n1; float pad2; vec3 n2; float pad3; };
struct Node     { vec3 bmin; int left_first; vec3 bmax; int count; };

layout(std430, binding = 0) readonly buffer Materials { Material materials[]; };
layout(std430, binding = 1) readonly buffer Spheres   { Sphere spheres[]; };
layout(std430, binding = 2) readonly buffer Triangles { Triangle triangles[]; };
layout(std430, binding = 3) readonly buffer Nodes     { Node nodes[]; };
layout(std430, binding = 4) readonly buffer Prims     { int prim_refs[]; };

uniform int u_width;
uniform int u_height;
uniform uint u_frame;
uniform uint u_seed;
uniform int u_samples;
uniform int u_max_depth;
uniform int u_sky;
uniform vec3 u_background;
uniform vec3 u_origin;
uniform vec3 u_pixel00;
uniform vec3 u_du;
uniform vec3 u_dv;
uniform vec3 u_lens_u;
uniform vec3 u_lens_v;
uniform int u_defocus;

const int MAT_LAMBERTIAN = 0;
const int MAT_METAL = 1;
const int MAT_DIELECTRIC = 2;
const float T_MIN = 0.001;
const float FAR = 1e30;

uint rng_state;

uint hash_u32(uint h) {
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

uint random_u32() {
    uint state = rng_state;
    rng_state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random_float() {
    return float(random_u32() >> 8) * (1.0 / 16777216.0);
}

vec3 random_unit_vector() {
    float z = random_float() * 2.0 - 1.0;
    float a = random_float() * 6.28318530718;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), r * sin(a), z);
}

vec2 random_in_unit_disk() {
    float r = sqrt(random_float());
    float a = random_float() * 6.28318530718;
    return vec2(r * cos(a), r * sin(a));
}

struct Hit {
    float t;
    vec3 p;
    vec3 normal;
    bool front_face;
    int mat;
};

void set_face_normal(vec3 rd, vec3 outward_normal, inout Hit h) {
    h.front_face = dot(rd, outward_normal) < 0.0;
    h.normal = h.front_face ? outward_normal : -outward_normal;
}

// Numerically stable form so float precision holds up on the radius-1000 ground sphere.
// rd must be normalized.
bool hit_sphere(Sphere s, vec3 ro, vec3 rd, float t_max, inout Hit h) {
    vec3 oc = s.center - ro;
    float b = dot(rd, oc);
    float len = length(oc);
    float c = (len - s.radius) * (len + s.radius);
    float disc = b * b - c;
    if (disc < 0.0) return false;

    float q = b + (b >= 0.0 ? sqrt(disc) : -sqrt(disc));
    if (q == 0.0) return false;
    float near_t = (b >= 0.0) ? c / q : q;
    float far_t  = (b >= 0.0) ? q : c / q;

    float t = near_t;
    if (t <= T_MIN || t >= t_max) {
        t = far_t;
        if (t <= T_MIN || t >= t_max) return false;
    }
    h.t = t;
    h.p = ro + t * rd;
    set_face_normal(rd, (h.p - s.center) / s.radius, h);
    h.mat = s.mat;
    return true;
}

bool hit_triangle(Triangle tri, vec3 ro, vec3 rd, float t_max, inout Hit h) {
    vec3 e1 = tri.v1 - tri.v0;
    vec3 e2 = tri.v2 - tri.v0;
    vec3 pvec = cross(rd, e2);
    float det = dot(e1, pvec);
    if (abs(det) < 1e-9) return false;
    float inv_det = 1.0 / det;

    vec3 tvec = ro - tri.v0;
    float u = dot(tvec, pvec) * inv_det;
    if (u < 0.0 || u > 1.0) return false;
    vec3 qvec = cross(tvec, e1);
    float v = dot(rd, qvec) * inv_det;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(e2, qvec) * inv_det;
    if (t <= T_MIN || t >= t_max) return false;

    h.t = t;
    h.p = ro + t * rd;
    vec3 n = (tri.has_normals != 0) ? normalize((1.0 - u - v) * tri.n0 + u * tri.n1 + v * tri.n2)
                                    : normalize(cross(e1, e2));
    set_face_normal(rd, n, h);
    h.mat = tri.mat;
    return true;
}

bool hit_box(vec3 bmin, vec3 bmax, vec3 ro, vec3 inv, float t_max) {
    vec3 t0 = (bmin - ro) * inv;
    vec3 t1 = (bmax - ro) * inv;
    vec3 lo = min(t0, t1);
    vec3 hi = max(t0, t1);
    float enter = max(max(lo.x, lo.y), max(lo.z, T_MIN));
    float leave = min(min(hi.x, hi.y), min(hi.z, t_max));
    return enter <= leave;
}

bool hit_world(vec3 ro, vec3 rd, out Hit best) {
    best.t = FAR; best.p = vec3(0); best.normal = vec3(0, 1, 0); best.front_face = true; best.mat = 0;
    bool hit_anything = false;
    float closest = FAR;
    vec3 inv = 1.0 / rd;

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        Node n = nodes[stack[--sp]];
        if (!hit_box(n.bmin, n.bmax, ro, inv, closest)) continue;

        if (n.count > 0) {
            for (int i = 0; i < n.count; i++) {
                int ref = prim_refs[n.left_first + i];
                bool hit = (ref >= 0) ? hit_sphere(spheres[ref], ro, rd, closest, best)
                                      : hit_triangle(triangles[-ref - 1], ro, rd, closest, best);
                if (hit) { hit_anything = true; closest = best.t; }
            }
        } else if (sp < 62) {
            stack[sp++] = n.left_first;
            stack[sp++] = n.left_first + 1;
        }
    }
    return hit_anything;
}

float reflectance(float cosine, float ri) {
    float r0 = (1.0 - ri) / (1.0 + ri);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

bool scatter(Material m, vec3 rd, Hit h, out vec3 attenuation, out vec3 dir) {
    attenuation = vec3(0);
    dir = vec3(0, 1, 0);

    if (m.type == MAT_LAMBERTIAN) {
        dir = h.normal + random_unit_vector();
        if (all(lessThan(abs(dir), vec3(1e-6)))) dir = h.normal;
        attenuation = m.albedo;
        return true;
    }
    if (m.type == MAT_METAL) {
        dir = reflect(rd, h.normal) + m.fuzz * random_unit_vector();
        attenuation = m.albedo;
        return dot(dir, h.normal) > 0.0;
    }
    if (m.type == MAT_DIELECTRIC) {
        attenuation = vec3(1);
        float ri = h.front_face ? (1.0 / m.ior) : m.ior;
        float cos_theta = min(dot(-rd, h.normal), 1.0);
        float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
        bool cannot_refract = ri * sin_theta > 1.0;
        if (cannot_refract || reflectance(cos_theta, ri) > random_float())
            dir = reflect(rd, h.normal);
        else
            dir = refract(rd, h.normal, ri);
        return true;
    }
    return false;
}

vec3 background(vec3 rd) {
    if (u_sky == 0) return u_background;
    float a = 0.5 * (rd.y + 1.0);
    return mix(vec3(1.0), vec3(0.5, 0.7, 1.0), a);
}

vec3 trace(vec3 ro, vec3 rd) {
    vec3 throughput = vec3(1);
    vec3 radiance = vec3(0);

    for (int depth = 0; depth < u_max_depth; depth++) {
        Hit h;
        if (!hit_world(ro, rd, h)) {
            radiance += throughput * background(rd);
            break;
        }
        Material m = materials[h.mat];
        radiance += throughput * m.emit;

        vec3 attenuation, dir;
        if (!scatter(m, rd, h, attenuation, dir)) break;

        throughput *= attenuation;
        ro = h.p;
        rd = normalize(dir);
    }
    return radiance;
}

void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    if (pix.x >= u_width || pix.y >= u_height) return;

    rng_state = hash_u32(uint(pix.x) + hash_u32(uint(pix.y) + hash_u32(u_seed)));

    vec3 sum = vec3(0);
    for (int s = 0; s < u_samples; s++) {
        vec2 offset = vec2(random_float(), random_float()) - 0.5;
        vec3 target = u_pixel00 + (float(pix.x) + offset.x) * u_du + (float(pix.y) + offset.y) * u_dv;
        vec3 ro = u_origin;
        if (u_defocus != 0) {
            vec2 d = random_in_unit_disk();
            ro += d.x * u_lens_u + d.y * u_lens_v;
        }
        vec3 c = trace(ro, normalize(target - ro));
        if (any(isnan(c)) || any(isinf(c))) c = vec3(0);
        sum += c;
    }

    vec4 acc = vec4(sum, float(u_samples));
    if (u_frame != 0u) acc += imageLoad(accum_img, pix);
    imageStore(accum_img, pix, acc);

    vec3 avg = acc.rgb / acc.a;
    vec3 shown = min(sqrt(max(avg, vec3(0))), vec3(0.999));
    imageStore(display_img, pix, vec4(shown, 1.0));
}
)GLSL";
