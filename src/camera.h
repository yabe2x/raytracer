#pragma once

#include "rtweekend.h"

// Orientation is stored as yaw/pitch (yaw 0 looks down -z) so it's easy to steer.
class camera {
  public:
    double vfov = 90;
    point3 lookfrom = point3(0, 0, 0);
    double yaw = 0;
    double pitch = 0;
    vec3 vup = vec3(0, 1, 0);
    double defocus_angle = 0;
    double focus_dist = 10;

    void look_at(const point3& from, const point3& at) {
        lookfrom = from;
        vec3 d = unit_vector(at - from);
        pitch = std::asin(d.y());
        yaw = std::atan2(d.x(), -d.z());
    }

    vec3 forward() const {
        return vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
    }

    // call after changing any setting
    void update(int image_width, int image_height) {
        double theta = degrees_to_radians(vfov);
        double h = std::tan(theta / 2);
        double viewport_height = 2 * h * focus_dist;
        double viewport_width = viewport_height * (double(image_width) / image_height);

        w = -forward();
        u = unit_vector(cross(vup, w));
        v = cross(w, u);

        vec3 viewport_u = viewport_width * u;
        vec3 viewport_v = viewport_height * -v;

        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        point3 viewport_upper_left = lookfrom - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;
        pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

        double defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
        defocus_disk_u = u * defocus_radius;
        defocus_disk_v = v * defocus_radius;
    }

    // jittered inside the pixel for anti-aliasing
    ray get_ray(int x, int y) const {
        double offset_x = random_double() - 0.5;
        double offset_y = random_double() - 0.5;
        point3 pixel_sample = pixel00_loc + ((x + offset_x) * pixel_delta_u) + ((y + offset_y) * pixel_delta_v);

        point3 ray_origin = (defocus_angle <= 0) ? lookfrom : defocus_disk_sample();
        return ray(ray_origin, pixel_sample - ray_origin);
    }

    vec3 right() const { return u; }
    vec3 up() const { return v; }

    point3 first_pixel() const { return pixel00_loc; }
    vec3 pixel_step_u() const { return pixel_delta_u; }
    vec3 pixel_step_v() const { return pixel_delta_v; }
    vec3 lens_u() const { return defocus_disk_u; }
    vec3 lens_v() const { return defocus_disk_v; }

  private:
    point3 pixel00_loc;
    vec3 pixel_delta_u, pixel_delta_v;
    vec3 u, v, w;
    vec3 defocus_disk_u, defocus_disk_v;

    point3 defocus_disk_sample() const {
        vec3 p = random_in_unit_disk();
        return lookfrom + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
    }
};
