#pragma once

class Window;
class GpuRenderer;
struct CpuRenderer;

// Writes reel frames + reel.txt to out_dir; see tools/make_video.py
int run_demo_reel(Window& window, GpuRenderer& gpu, CpuRenderer& cpu, const char* out_dir);
