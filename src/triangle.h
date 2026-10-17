#pragma once

#include "hittable.h"

class triangle : public hittable {
  public:
    triangle(const point3& a, const point3& b, const point3& c, shared_ptr<material> mat)
        : v0(a), v1(b), v2(c), mat(mat), smooth(false) {
        set_bbox();
    }

    // per-vertex normals for smooth shading
    triangle(const point3& a, const point3& b, const point3& c, const vec3& na, const vec3& nb, const vec3& nc,
             shared_ptr<material> mat)
        : v0(a), v1(b), v2(c), n0(na), n1(nb), n2(nc), mat(mat), smooth(true) {
        set_bbox();
    }

    // Moller-Trumbore
    bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
        vec3 edge1 = v1 - v0;
        vec3 edge2 = v2 - v0;
        vec3 pvec = cross(r.direction(), edge2);
        double det = dot(edge1, pvec);

        if (std::fabs(det) < 1e-12)
            return false;
        double inv_det = 1.0 / det;

        vec3 tvec = r.origin() - v0;
        double u = dot(tvec, pvec) * inv_det;
        if (u < 0 || u > 1)
            return false;

        vec3 qvec = cross(tvec, edge1);
        double v = dot(r.direction(), qvec) * inv_det;
        if (v < 0 || u + v > 1)
            return false;

        double t = dot(edge2, qvec) * inv_det;
        if (!ray_t.surrounds(t))
            return false;

        rec.t = t;
        rec.p = r.at(t);
        vec3 outward_normal = smooth ? unit_vector((1 - u - v) * n0 + u * n1 + v * n2)
                                     : unit_vector(cross(edge1, edge2));
        rec.set_face_normal(r, outward_normal);
        rec.mat = mat;
        return true;
    }

    aabb bounding_box() const override { return bbox; }

  private:
    point3 v0, v1, v2;
    vec3 n0, n1, n2;
    shared_ptr<material> mat;
    bool smooth;
    aabb bbox;

    void set_bbox() {
        bbox = aabb(aabb(v0, v1), aabb(v2, v2));
    }
};
