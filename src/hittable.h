#pragma once

#include "rtweekend.h"
#include "aabb.h"

class material;

class hit_record {
  public:
    point3 p;
    vec3 normal;  // always faces against the ray
    shared_ptr<material> mat;
    double t;
    bool front_face;

    // outward_normal must have length 1
    void set_face_normal(const ray& r, const vec3& outward_normal) {
        front_face = dot(r.direction(), outward_normal) < 0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};

class hittable {
  public:
    virtual ~hittable() = default;

    virtual bool hit(const ray& r, interval ray_t, hit_record& rec) const = 0;

    virtual aabb bounding_box() const = 0;
};
