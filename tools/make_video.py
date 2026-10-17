"""Usage: python tools/make_video.py reel_frames images/demo.mp4 images/demo.gif

Uses ffmpeg if it's on PATH or in $FFMPEG, otherwise OpenCV.
"""

import os
import shutil
import subprocess
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 720
FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
BG = (13, 13, 16)
FADE_FRAMES = 8


def font(name, size):
    try:
        return ImageFont.truetype(os.path.join(FONT_DIR, name), size)
    except OSError:
        return ImageFont.load_default()


TITLE = font("segoeuib.ttf", 40)
SUB = font("segoeui.ttf", 24)
LABEL = font("segoeuib.ttf", 22)


def read_manifest(path):
    fps, rows = 30, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if parts[0] == "fps":
                fps = int(parts[1])
            else:
                rows.append(parts)
    return fps, rows


def caption_band(img, title, sub, alpha):
    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    band = 190
    for i in range(band):
        a = int(170 * (i / band) ** 1.6)
        draw.line([(0, H - band + i), (W, H - band + i)], fill=(0, 0, 0, a))
    t = int(255 * alpha)
    draw.text((48, H - 118), title, font=TITLE, fill=(255, 255, 255, t))
    draw.text((50, H - 66), sub, font=SUB, fill=(215, 215, 222, t))
    return Image.alpha_composite(img.convert("RGBA"), overlay).convert("RGB")


def split_frame(dir_, cpu_file, gpu_file, title, sub, left, right):
    canvas = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(canvas)
    draw.text((48, 52), title, font=TITLE, fill=(255, 255, 255))
    draw.text((50, 104), sub, font=SUB, fill=(190, 190, 200))

    pw, ph, gap = 624, 351, 16
    x0 = (W - 2 * pw - gap) // 2
    y0 = 190
    cpu = Image.open(os.path.join(dir_, cpu_file)).resize((pw, ph), Image.LANCZOS)
    gpu = Image.open(os.path.join(dir_, gpu_file)).resize((pw, ph), Image.LANCZOS)
    canvas.paste(cpu, (x0, y0))
    canvas.paste(gpu, (x0 + pw + gap, y0))
    draw.text((x0, y0 + ph + 18), left, font=fit(draw, left, pw), fill=(235, 235, 240))
    draw.text((x0 + pw + gap, y0 + ph + 18), right, font=fit(draw, right, pw), fill=(120, 220, 140))
    return canvas


def fit(draw, text, max_width, size=22):
    while size > 12:
        f = font("segoeuib.ttf", size)
        if draw.textlength(text, font=f) <= max_width:
            return f
        size -= 1
    return font("segoeuib.ttf", size)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    dir_, out_mp4, out_gif = sys.argv[1], sys.argv[2], sys.argv[3]
    fps, rows = read_manifest(os.path.join(dir_, "reel.txt"))

    ffmpeg = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if ffmpeg:
        proc = subprocess.Popen(
            [ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
             "-r", str(fps), "-i", "-", "-c:v", "libx264", "-preset", "slow", "-crf", "24",
             "-pix_fmt", "yuv420p", "-movflags", "+faststart", out_mp4],
            stdin=subprocess.PIPE)
        write = lambda frame: proc.stdin.write(frame.tobytes())
    else:
        writer = cv2.VideoWriter(out_mp4, cv2.CAP_MSMF, cv2.VideoWriter_fourcc(*"H264"), fps, (W, H))
        if not writer.isOpened():  # not on Windows: fall back to plain MPEG-4
            writer = cv2.VideoWriter(out_mp4, cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))
        write = lambda frame: writer.write(cv2.cvtColor(np.asarray(frame), cv2.COLOR_RGB2BGR))

    gif_frames = []
    last_title, since_change = None, 0
    cache = {}
    for n, row in enumerate(rows):
        kind = row[0]
        if kind == "full":
            _, file, title, sub = row
            if title != last_title:
                last_title, since_change = title, 0
            alpha = min(1.0, (since_change + 1) / FADE_FRAMES)
            since_change += 1
            if file not in cache:
                cache.clear()
                cache[file] = Image.open(os.path.join(dir_, file)).convert("RGB")
            frame = caption_band(cache[file], title, sub, alpha)
        else:
            _, cpu_file, gpu_file, title, sub, left, right = row
            last_title = title
            frame = split_frame(dir_, cpu_file, gpu_file, title, sub, left, right)

        write(frame)
        if not ffmpeg and n % 3 == 0:  # GIF at 10 fps, small size
            gif_frames.append(frame.resize((480, 270), Image.LANCZOS))
        if n % 100 == 0:
            print(f"  {n}/{len(rows)} frames")

    if ffmpeg:
        proc.stdin.close()
        proc.wait()
    else:
        writer.release()
    print(f"Wrote {out_mp4} ({len(rows)} frames, {len(rows) / fps:.1f} s)")

    if ffmpeg:
        filters = "fps=10,scale=480:-1:flags=lanczos"
        subprocess.run(
            [ffmpeg, "-y", "-loglevel", "error", "-i", out_mp4, "-filter_complex",
             f"[0:v]{filters},split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
             out_gif],
            check=True)
    else:
        pal = [f.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in gif_frames]
        pal[0].save(out_gif, save_all=True, append_images=pal[1:], duration=100, loop=0, optimize=True)
    print(f"Wrote {out_gif}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
