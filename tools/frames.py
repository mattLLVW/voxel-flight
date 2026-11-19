"""Turn a Voxel Flight capture (from capture.ps1) into an animated GIF and/or PNG frames.

Each frame in the capture is the 4 bytes "VFRM" followed by 128x68 RGB565 pixels (big-endian).
Anything else in the file (the text log) is skipped.

    python tools/frames.py capture.bin --list
    python tools/frames.py capture.bin --gif demo.gif --start 12 --length 10 --scale 6
    python tools/frames.py capture.bin --png frames_dir --start 12 --length 2

Times are in seconds of capture at 30 frames per second. Needs Pillow (pip install pillow).
"""
import argparse
import os
import sys

W, H = 128, 68
FRAME = W * H * 2
FPS = 30


def read_frames(path):
    data = open(path, "rb").read()
    frames, i = [], 0
    while True:
        i = data.find(b"VFRM", i)
        if i < 0 or i + 4 + FRAME > len(data):
            break
        frames.append(data[i + 4:i + 4 + FRAME])
        i += 4 + FRAME
    return frames


def to_rgb(frame):
    out = bytearray(W * H * 3)
    for p in range(W * H):
        c = (frame[2 * p] << 8) | frame[2 * p + 1]
        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
        out[3 * p] = (r << 3) | (r >> 2)
        out[3 * p + 1] = (g << 2) | (g >> 4)
        out[3 * p + 2] = (b << 3) | (b >> 2)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--list", action="store_true", help="just say how many frames there are")
    ap.add_argument("--start", type=float, default=0, help="start time, seconds")
    ap.add_argument("--length", type=float, default=10, help="length, seconds")
    ap.add_argument("--scale", type=int, default=6, help="pixel size in the output (sharp, no blur)")
    ap.add_argument("--step", type=int, default=1, help="keep every Nth frame (2 halves the file size)")
    ap.add_argument("--gif", help="write an animated GIF")
    ap.add_argument("--png", help="write PNG frames into this folder")
    a = ap.parse_args()

    frames = read_frames(a.capture)
    print(f"{len(frames)} frames ({len(frames) / FPS:.1f} s)")
    if a.list or not (a.gif or a.png):
        return
    from PIL import Image

    s0 = int(a.start * FPS)
    s1 = min(len(frames), s0 + int(a.length * FPS))
    images = []
    for k in range(s0, s1, a.step):
        img = Image.frombytes("RGB", (W, H), to_rgb(frames[k]))
        images.append(img.resize((W * a.scale, H * a.scale), Image.NEAREST))
    if not images:
        sys.exit("no frames in that range")
    if a.png:
        os.makedirs(a.png, exist_ok=True)
        for n, img in enumerate(images):
            img.save(os.path.join(a.png, f"frame_{n:04d}.png"))
        print(f"wrote {len(images)} PNGs to {a.png}")
    if a.gif:
        pal = [im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for im in images]
        pal[0].save(a.gif, save_all=True, append_images=pal[1:], duration=int(1000 * a.step / FPS),
                    loop=0, optimize=False, disposal=1)
        print(f"wrote {a.gif}: {len(images)} frames, {os.path.getsize(a.gif) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
