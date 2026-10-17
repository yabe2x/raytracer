#include "window.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <stdexcept>

// prefer the discrete GPU on hybrid laptops
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

struct Window::Impl {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC gl = nullptr;
    HMODULE opengl32 = nullptr;
    int width = 0, height = 0;
    bool open = true;
    bool down[256] = {};
    bool pressed[256] = {};
};

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* impl = reinterpret_cast<Window::Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (impl) {
        switch (msg) {
        case WM_CLOSE:
            impl->open = false;
            return 0;
        case WM_KEYDOWN:
            if (wp < 256) {
                if (!impl->down[wp]) impl->pressed[wp] = true;
                impl->down[wp] = true;
            }
            return 0;
        case WM_KEYUP:
            if (wp < 256) impl->down[wp] = false;
            return 0;
        case WM_KILLFOCUS:
            for (bool& d : impl->down) d = false;
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

Window::Window(int width, int height, int scale, const std::string& title) : impl(new Impl) {
    impl->width = width;
    impl->height = height;

    HINSTANCE inst = GetModuleHandle(nullptr);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "RaytracerWindow";
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT r = {0, 0, width * scale, height * scale};
    AdjustWindowRect(&r, style, FALSE);

    impl->hwnd = CreateWindowA(wc.lpszClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!impl->hwnd) throw std::runtime_error("CreateWindow failed");

    SetWindowLongPtr(impl->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
    ShowWindow(impl->hwnd, SW_SHOW);
}

Window::~Window() {
    if (impl->gl) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(impl->gl);
    }
    if (impl->dc) ReleaseDC(impl->hwnd, impl->dc);
    if (impl->hwnd) DestroyWindow(impl->hwnd);
    if (impl->opengl32) FreeLibrary(impl->opengl32);
    delete impl;
}

bool Window::create_gl() {
    impl->dc = GetDC(impl->hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    int format = ChoosePixelFormat(impl->dc, &pfd);
    if (!format || !SetPixelFormat(impl->dc, format, &pfd))
        return false;

    // legacy context first, to get wglCreateContextAttribsARB
    HGLRC temp = wglCreateContext(impl->dc);
    if (!temp || !wglMakeCurrent(impl->dc, temp))
        return false;

    typedef HGLRC(WINAPI * CreateContextAttribsFn)(HDC, HGLRC, const int*);
    auto create_attribs = (CreateContextAttribsFn)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
    if (create_attribs) {
        const int attribs[] = {
            0x2091, 4,    // WGL_CONTEXT_MAJOR_VERSION_ARB
            0x2092, 3,    // WGL_CONTEXT_MINOR_VERSION_ARB
            0x9126, 0x1,  // WGL_CONTEXT_PROFILE_MASK_ARB = core
            0,
        };
        HGLRC modern = create_attribs(impl->dc, nullptr, attribs);
        if (modern) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(temp);
            temp = modern;
            wglMakeCurrent(impl->dc, temp);
        }
    }
    impl->gl = temp;
    impl->opengl32 = LoadLibraryA("opengl32.dll");

    typedef BOOL(WINAPI * SwapIntervalFn)(int);
    auto swap_interval = (SwapIntervalFn)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap_interval) swap_interval(0);
    return true;
}

void* Window::gl_proc(const char* name) const {
    void* p = (void*)wglGetProcAddress(name);
    // GL 1.1 functions only come from opengl32.dll; some drivers return 1-3 or -1 for missing
    if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1)
        p = impl->opengl32 ? (void*)GetProcAddress(impl->opengl32, name) : nullptr;
    return p;
}

void Window::swap_buffers() { SwapBuffers(impl->dc); }

void Window::client_size(int& w, int& h) const {
    RECT client;
    GetClientRect(impl->hwnd, &client);
    w = client.right;
    h = client.bottom;
}

bool Window::poll() {
    for (bool& p : impl->pressed) p = false;
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return impl->open;
}

void Window::present(const uint32_t* pixels) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = impl->width;
    bmi.bmiHeader.biHeight = -impl->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    RECT client;
    GetClientRect(impl->hwnd, &client);
    HDC dc = GetDC(impl->hwnd);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, impl->width, impl->height, pixels, &bmi,
                  DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(impl->hwnd, dc);
}

void Window::set_title(const std::string& title) { SetWindowTextA(impl->hwnd, title.c_str()); }

bool Window::key_pressed(int key) { return key >= 0 && key < 256 && impl->pressed[key]; }

bool Window::key_down(int key) const { return key >= 0 && key < 256 && impl->down[key]; }
