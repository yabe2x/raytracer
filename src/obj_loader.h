#pragma once

#include "scene_desc.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Minimal Wavefront OBJ loader (v, vn, f). The model is scaled so its largest
// dimension equals `size` and its bottom-center sits at `position`.
// Returns the number of triangles added.
inline size_t load_obj(const std::string& path, SceneDesc& scene, int material, const point3& position, double size,
                       double rotate_y_degrees = 0) {
    std::ifstream file(path);
    if (!file) {
        std::printf("Could not open model: %s\n", path.c_str());
        return 0;
    }

    std::vector<point3> verts;
    std::vector<vec3> normals;
    struct Corner { int v, n; };
    std::vector<std::vector<Corner>> faces;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        if (tag == "v") {
            double x, y, z;
            in >> x >> y >> z;
            verts.push_back(point3(x, y, z));
        } else if (tag == "vn") {
            double x, y, z;
            in >> x >> y >> z;
            normals.push_back(vec3(x, y, z));
        } else if (tag == "f") {
            std::vector<Corner> face;
            std::string token;
            while (in >> token) {
                // v, v/vt, v//vn, or v/vt/vn
                int v = 0, n = 0;
                size_t s1 = token.find('/');
                v = std::stoi(token.substr(0, s1));
                if (s1 != std::string::npos) {
                    size_t s2 = token.find('/', s1 + 1);
                    if (s2 != std::string::npos && s2 + 1 < token.size())
                        n = std::stoi(token.substr(s2 + 1));
                }
                // negative indices are relative to the end
                if (v < 0) v = int(verts.size()) + v + 1;
                if (n < 0) n = int(normals.size()) + n + 1;
                face.push_back({v - 1, n - 1});
            }
            if (face.size() >= 3)
                faces.push_back(face);
        }
    }

    if (verts.empty() || faces.empty()) {
        std::printf("Model has no triangles: %s\n", path.c_str());
        return 0;
    }

    double angle = degrees_to_radians(rotate_y_degrees);
    double cos_a = std::cos(angle), sin_a = std::sin(angle);
    auto rotate = [&](const vec3& p) {
        return vec3(cos_a * p.x() + sin_a * p.z(), p.y(), -sin_a * p.x() + cos_a * p.z());
    };
    for (auto& p : verts) p = rotate(p);
    for (auto& n : normals) n = rotate(n);

    point3 lo = verts[0], hi = verts[0];
    for (const auto& p : verts) {
        for (int i = 0; i < 3; i++) {
            lo[i] = std::fmin(lo[i], p[i]);
            hi[i] = std::fmax(hi[i], p[i]);
        }
    }
    vec3 extent = hi - lo;
    double biggest = std::fmax(extent.x(), std::fmax(extent.y(), extent.z()));
    double scale = size / biggest;
    point3 bottom_center((lo.x() + hi.x()) / 2, lo.y(), (lo.z() + hi.z()) / 2);

    auto place = [&](const point3& p) { return position + scale * (p - bottom_center); };

    size_t added = 0;
    for (const auto& face : faces) {
        // fan triangulation
        for (size_t k = 1; k + 1 < face.size(); k++) {
            const Corner c[3] = {face[0], face[k], face[k + 1]};
            TriangleDesc tri;
            tri.material = material;
            tri.has_normals = true;
            bool ok = true;
            for (int i = 0; i < 3; i++) {
                if (c[i].v < 0 || c[i].v >= int(verts.size())) { ok = false; break; }
                tri.v[i] = place(verts[c[i].v]);
                if (c[i].n >= 0 && c[i].n < int(normals.size()))
                    tri.n[i] = unit_vector(normals[c[i].n]);
                else
                    tri.has_normals = false;
            }
            if (!ok) continue;
            scene.triangles.push_back(tri);
            added++;
        }
    }

    std::printf("Loaded %s: %zu triangles\n", path.c_str(), added);
    return added;
}
