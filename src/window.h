#pragma once

#include <cstdint>
#include <string>

constexpr int KEY_ESCAPE = 0x1B;
constexpr int KEY_LEFT = 0x25;
constexpr int KEY_UP = 0x26;
constexpr int KEY_RIGHT = 0x27;
constexpr int KEY_DOWN = 0x28;

class Window {
public:
    Window(int width, int height, int scale, const std::string& title);
    ~Window();

    // returns false once the window is closed
    bool poll();

    void present(const uint32_t* pixels);

    void set_title(const std::string& title);

    // Win32 virtual-key codes
    bool key_pressed(int key);
    bool key_down(int key) const;

    bool create_gl();

    void* gl_proc(const char* name) const;

    void swap_buffers();

    void client_size(int& w, int& h) const;

    struct Impl;

private:
    Impl* impl;
};
