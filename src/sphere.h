#pragma once

#include "hittable.h"

class sphere : public hittable {
  public:
    sphere(const point3& center, double radius, shared_ptr<material> mat)
        : center(center), radius(std::fmax(0, radius)), mat(mat) {
        vec3 rvec = vec3(radius, radius, radius);
        bbox = aabb(center - rvec, center + rvec);
    }

    bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
        vec3 oc = center - r.origin();
        double a = dot(r.direction(), r.direction());
        double b = -2.0 * dot(r.direction(), oc);
        double c = dot(oc, oc) - radius * radius;
        double discriminant = b * b - 4 * a * c;
        if (discriminant < 0)
            return false;

        double sqrtd = std::sqrt(discriminant);
        double root = (-b - sqrtd) / (2.0 * a);
        if (!ray_t.surrounds(root)) {
            root = (-b + sqrtd) / (2.0 * a);
            if (!ray_t.surrounds(root))
                return false;
        }

        rec.t = root;
        rec.p = r.at(rec.t);
        vec3 outward_normal = (rec.p - center) / radius;
        rec.set_face_normal(r, outward_normal);
        rec.mat = mat;
        return true;
    }

    aabb bounding_box() const override { return bbox; }

  private:
    point3 center;
    double radius;
    shared_ptr<material> mat;
    aabb bbox;
};
