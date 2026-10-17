#include "gpu_renderer.h"
#include "gpu_shader.h"
#include "scene.h"
#include "scene_desc.h"
#include "window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// Windows only ships GL 1.1 headers, so everything newer is loaded at runtime.
#if defined(_WIN32) && !defined(_WIN64)
#define GLCALL __stdcall
#else
#define GLCALL
#endif

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLboolean, GLubyte;
typedef ptrdiff_t GLsizeiptr;

enum : GLenum {
    GL_TEXTURE_2D = 0x0DE1, GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406, GL_RGBA = 0x1908,
    GL_NEAREST = 0x2600, GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801,
    GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_BGRA = 0x80E1, GL_RGBA8 = 0x8058, GL_RGBA32F = 0x8814, GL_STATIC_DRAW = 0x88E4,
    GL_READ_WRITE = 0x88BA, GL_WRITE_ONLY = 0x88B9, GL_COMPUTE_SHADER = 0x91B9,
    GL_SHADER_STORAGE_BUFFER = 0x90D2, GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82,
    GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9, GL_COLOR_ATTACHMENT0 = 0x8CE0,
    GL_PACK_ALIGNMENT = 0x0D05, GL_UNPACK_ALIGNMENT = 0x0CF5, GL_UNPACK_ROW_LENGTH = 0x0CF2,
};
const GLbitfield GL_ALL_BARRIER_BITS = 0xFFFFFFFF;

#define GL_FUNCTIONS(X)                                                                                   \
    X(const GLubyte*, GetString, (GLenum))                                                                \
    X(void, Finish, ())                                                                                   \
    X(void, GenTextures, (GLsizei, GLuint*))                                                              \
    X(void, BindTexture, (GLenum, GLuint))                                                                \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                       \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))  \
    X(void, GetTexImage, (GLenum, GLint, GLenum, GLenum, void*))                                          \
    X(void, PixelStorei, (GLenum, GLint))                                                                 \
    X(void, TexStorage2D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))                                    \
    X(GLuint, CreateShader, (GLenum))                                                                     \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                          \
    X(void, CompileShader, (GLuint))                                                                      \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                        \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                       \
    X(GLuint, CreateProgram, ())                                                                          \
    X(void, AttachShader, (GLuint, GLuint))                                                               \
    X(void, LinkProgram, (GLuint))                                                                        \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                       \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                      \
    X(void, UseProgram, (GLuint))                                                                         \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                                 \
    X(void, Uniform1i, (GLint, GLint))                                                                    \
    X(void, Uniform1ui, (GLint, GLuint))                                                                  \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                                \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                               \
    X(void, BindBuffer, (GLenum, GLuint))                                                                 \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                        \
    X(void, BindBufferBase, (GLenum, GLuint, GLuint))                                                     \
    X(void, BindImageTexture, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum))                  \
    X(void, DispatchCompute, (GLuint, GLuint, GLuint))                                                    \
    X(void, MemoryBarrier, (GLbitfield))                                                                  \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                                          \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                            \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

static struct {
#define DECLARE(ret, name, args) ret(GLCALL* name) args;
    GL_FUNCTIONS(DECLARE)
#undef DECLARE
} gl;

bool GpuRenderer::load_functions() {
    bool ok = true;
#define LOAD(ret, name, args)                                         \
    gl.name = (ret(GLCALL*) args)window->gl_proc("gl" #name);         \
    if (!gl.name) {                                                   \
        err = "OpenGL function missing: gl" #name;                    \
        ok = false;                                                   \
    }
    GL_FUNCTIONS(LOAD)
#undef LOAD
    return ok;
}

// std430 layouts, must match gpu_shader.h
struct GpuMaterial { float albedo[3]; int32_t type; float emit[3]; float fuzz; float ior; float pad[3]; };
struct GpuSphere { float center[3]; float radius; int32_t mat; int32_t pad[3]; };
struct GpuTriangle {
    float v0[3]; int32_t mat;
    float v1[3]; int32_t has_normals;
    float v2[3]; float pad0;
    float n0[3]; float pad1;
    float n1[3]; float pad2;
    float n2[3]; float pad3;
};
struct GpuNode { float bmin[3]; int32_t left_first; float bmax[3]; int32_t count; };

static_assert(sizeof(GpuMaterial) == 48, "layout must match shader");
static_assert(sizeof(GpuSphere) == 32, "layout must match shader");
static_assert(sizeof(GpuTriangle) == 96, "layout must match shader");
static_assert(sizeof(GpuNode) == 32, "layout must match shader");

static void copy3(float out[3], const vec3& v) {
    out[0] = float(v.x());
    out[1] = float(v.y());
    out[2] = float(v.z());
}

// Flattened BVH: children of a node are stored at left_first and left_first + 1.
struct PrimInfo {
    int ref;  // >= 0 sphere index, < 0 triangle (-ref - 1)
    vec3 lo, hi;
    vec3 centroid;
};

static void build_flat_bvh(std::vector<PrimInfo>& prims, std::vector<GpuNode>& nodes, int node_index, int start,
                           int end) {
    vec3 lo = prims[start].lo, hi = prims[start].hi;
    vec3 clo = prims[start].centroid, chi = prims[start].centroid;
    for (int i = start; i < end; i++) {
        for (int k = 0; k < 3; k++) {
            lo[k] = std::fmin(lo[k], prims[i].lo[k]);
            hi[k] = std::fmax(hi[k], prims[i].hi[k]);
            clo[k] = std::fmin(clo[k], prims[i].centroid[k]);
            chi[k] = std::fmax(chi[k], prims[i].centroid[k]);
        }
    }
    copy3(nodes[node_index].bmin, lo);
    copy3(nodes[node_index].bmax, hi);

    int count = end - start;
    if (count <= 2) {
        nodes[node_index].left_first = start;
        nodes[node_index].count = count;
        return;
    }

    // median split on the axis with the widest centroid spread
    vec3 spread = chi - clo;
    int axis = (spread.x() > spread.y()) ? (spread.x() > spread.z() ? 0 : 2) : (spread.y() > spread.z() ? 1 : 2);
    int mid = start + count / 2;
    std::nth_element(prims.begin() + start, prims.begin() + mid, prims.begin() + end,
                     [axis](const PrimInfo& a, const PrimInfo& b) { return a.centroid[axis] < b.centroid[axis]; });

    int left = int(nodes.size());
    nodes.push_back(GpuNode{});
    nodes.push_back(GpuNode{});
    nodes[node_index].left_first = left;
    nodes[node_index].count = 0;
    build_flat_bvh(prims, nodes, left, start, mid);
    build_flat_bvh(prims, nodes, left + 1, mid, end);
}

bool GpuRenderer::init(Window& w, int max_width, int max_height) {
    window = &w;
    max_w = max_width;
    max_h = max_height;

    if (!w.create_gl()) {
        err = "Could not create an OpenGL context";
        return false;
    }
    if (!load_functions())
        return false;

    const char* version = (const char*)gl.GetString(GL_VERSION);
    const char* renderer = (const char*)gl.GetString(GL_RENDERER);
    gpu_name = renderer ? renderer : "unknown GPU";
    int major = version ? version[0] - '0' : 0;
    int minor = version ? version[2] - '0' : 0;
    if (major < 4 || (major == 4 && minor < 3)) {
        err = std::string("Needs OpenGL 4.3, this GPU has ") + (version ? version : "none");
        return false;
    }

    if (!compile_shader())
        return false;

    // rgb = running sum, a = sample count
    gl.GenTextures(1, &accum_tex);
    gl.BindTexture(GL_TEXTURE_2D, accum_tex);
    gl.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, max_w, max_h);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    gl.GenTextures(1, &display_tex);
    gl.BindTexture(GL_TEXTURE_2D, display_tex);
    gl.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, max_w, max_h);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    gl.GenFramebuffers(1, &read_fbo);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, display_tex, 0);

    gl.GenBuffers(5, buffers);
    return true;
}

bool GpuRenderer::compile_shader() {
    GLuint shader = gl.CreateShader(GL_COMPUTE_SHADER);
    gl.ShaderSource(shader, 1, &GPU_SHADER_SOURCE, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        err = std::string("Shader failed to compile:\n") + log;
        return false;
    }

    program = gl.CreateProgram();
    gl.AttachShader(program, shader);
    gl.LinkProgram(program);
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        err = std::string("Shader failed to link:\n") + log;
        return false;
    }
    return true;
}

template <typename T>
static void upload_buffer(GLuint buffer, GLuint binding, const std::vector<T>& data) {
    std::vector<T> copy = data;
    if (copy.empty()) copy.push_back(T{});
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    gl.BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(copy.size() * sizeof(T)), copy.data(), GL_STATIC_DRAW);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
}

void GpuRenderer::upload_scene(const SceneDesc& scene) {
    std::vector<GpuMaterial> mats;
    for (const auto& m : scene.materials) {
        GpuMaterial g = {};
        copy3(g.albedo, m.albedo);
        g.type = m.type;
        copy3(g.emit, m.emit);
        g.fuzz = float(m.fuzz);
        g.ior = float(m.ior);
        mats.push_back(g);
    }

    std::vector<GpuSphere> spheres;
    std::vector<PrimInfo> prims;
    for (size_t i = 0; i < scene.spheres.size(); i++) {
        const auto& s = scene.spheres[i];
        GpuSphere g = {};
        copy3(g.center, s.center);
        g.radius = float(s.radius);
        g.mat = s.material;
        spheres.push_back(g);
        vec3 r(s.radius, s.radius, s.radius);
        prims.push_back({int(i), s.center - r, s.center + r, s.center});
    }

    std::vector<GpuTriangle> tris;
    for (size_t i = 0; i < scene.triangles.size(); i++) {
        const auto& t = scene.triangles[i];
        GpuTriangle g = {};
        copy3(g.v0, t.v[0]);
        copy3(g.v1, t.v[1]);
        copy3(g.v2, t.v[2]);
        copy3(g.n0, t.n[0]);
        copy3(g.n1, t.n[1]);
        copy3(g.n2, t.n[2]);
        g.has_normals = t.has_normals ? 1 : 0;
        g.mat = t.material;
        tris.push_back(g);

        PrimInfo p;
        p.ref = -int(i) - 1;
        p.lo = p.hi = t.v[0];
        for (int c = 1; c < 3; c++)
            for (int k = 0; k < 3; k++) {
                p.lo[k] = std::fmin(p.lo[k], t.v[c][k]);
                p.hi[k] = std::fmax(p.hi[k], t.v[c][k]);
            }
        vec3 pad(1e-4, 1e-4, 1e-4);
        p.lo = p.lo - pad;
        p.hi = p.hi + pad;
        p.centroid = (t.v[0] + t.v[1] + t.v[2]) / 3;
        prims.push_back(p);
    }

    std::vector<GpuNode> nodes(1);
    if (!prims.empty())
        build_flat_bvh(prims, nodes, 0, 0, int(prims.size()));

    std::vector<int32_t> refs;
    for (const auto& p : prims) refs.push_back(p.ref);

    upload_buffer(buffers[0], 0, mats);
    upload_buffer(buffers[1], 1, spheres);
    upload_buffer(buffers[2], 2, tris);
    upload_buffer(buffers[3], 3, nodes);
    upload_buffer(buffers[4], 4, refs);

    sky_gradient = scene.sky_gradient ? 1 : 0;
    copy3(background, scene.background);

    std::printf("GPU scene: %zu spheres, %zu triangles, %zu BVH nodes\n", spheres.size(), tris.size(), nodes.size());
}

void GpuRenderer::render(const CameraParams& cam, int width, int height, uint32_t frame, uint32_t seed, int samples,
                         int max_depth) {
    gl.UseProgram(program);
    auto loc = [&](const char* name) { return gl.GetUniformLocation(program, name); };
    auto set3 = [&](const char* name, const float v[3]) { gl.Uniform3f(loc(name), v[0], v[1], v[2]); };

    gl.Uniform1i(loc("u_width"), width);
    gl.Uniform1i(loc("u_height"), height);
    gl.Uniform1ui(loc("u_frame"), frame);
    gl.Uniform1ui(loc("u_seed"), seed);
    gl.Uniform1i(loc("u_samples"), samples);
    gl.Uniform1i(loc("u_max_depth"), max_depth);
    gl.Uniform1i(loc("u_sky"), sky_gradient);
    set3("u_background", background);
    set3("u_origin", cam.origin);
    set3("u_pixel00", cam.pixel00);
    set3("u_du", cam.delta_u);
    set3("u_dv", cam.delta_v);
    set3("u_lens_u", cam.defocus_u);
    set3("u_lens_v", cam.defocus_v);
    gl.Uniform1i(loc("u_defocus"), cam.defocus);

    gl.BindImageTexture(0, accum_tex, 0, 0, 0, GL_READ_WRITE, GL_RGBA32F);
    gl.BindImageTexture(1, display_tex, 0, 0, 0, GL_WRITE_ONLY, GL_RGBA8);

    gl.DispatchCompute(GLuint((width + 7) / 8), GLuint((height + 7) / 8), 1);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);
}

void GpuRenderer::finish() { gl.Finish(); }

void GpuRenderer::blit(int src_w, int src_h) {
    int win_w, win_h;
    window->client_size(win_w, win_h);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // our images are top-row-first, GL is bottom-row-first, so flip on the blit
    gl.BlitFramebuffer(0, 0, src_w, src_h, 0, win_h, win_w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    window->swap_buffers();
}

void GpuRenderer::present_gpu(int width, int height) { blit(width, height); }

void GpuRenderer::present_cpu(const uint32_t* pixels, int width, int height) {
    gl.BindTexture(GL_TEXTURE_2D, display_tex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    blit(width, height);
}

void GpuRenderer::read_pixels(int width, int height, std::vector<uint32_t>& out) {
    std::vector<uint32_t> full(size_t(max_w) * max_h);
    gl.BindTexture(GL_TEXTURE_2D, display_tex);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.GetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, full.data());
    out.resize(size_t(width) * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            out[size_t(y) * width + x] = full[size_t(y) * max_w + x] & 0x00FFFFFF;
}
