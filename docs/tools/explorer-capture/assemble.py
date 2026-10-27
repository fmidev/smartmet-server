#!/usr/bin/env python3
"""Makes the captures of capture.js into the images of the documentation.

python3 assemble.py [--out DIR] [--dest DIR] [--scale S] [--fps F] [name ...]

DIR/<name>/ (frames.json and PNG frames) becomes DEST/<name>.webp, an animation timed as
it was recorded, and DIR/<name>.png becomes DEST/<name>.png. Identical frames are merged,
and the last frame is held before the loop starts again. Needs Pillow with WebP.
"""
import argparse
import json
import os

from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))


def scaled(img, scale):
    if scale == 1.0:
        return img
    return img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)


def animation(src, dst, scale, fps, quality, hold):
    times = json.load(open(os.path.join(src, 'frames.json')))
    crop = None
    if isinstance(times, dict):
        c = times['crop']
        crop = (c['x'], c['y'], c['x'] + c['width'], c['y'] + c['height'])
        times = times['frames']
    step = 1000 / fps
    frames, durations = [], []
    previous = None
    next_time = 0
    for i, f in enumerate(times):
        # Frames closer than 1/fps to the last one kept are dropped, except the last one
        if f['t'] < next_time and i != len(times) - 1:
            continue
        img = Image.open(os.path.join(src, f['file'])).convert('RGB')
        if crop:
            img = img.crop(crop)
        if previous is not None and ImageChops.difference(img, previous).getbbox() is None:
            continue
        if frames:
            # A pause of the recording (a page loaded again) is not shown as a long still
            durations.append(min(4000, max(20, f['t'] - start)))
        frames.append(scaled(img, scale))
        previous = img
        start = f['t']
        next_time = f['t'] + step
    durations.append(hold)
    frames[0].save(dst, save_all=True, append_images=frames[1:], duration=durations, loop=0,
                   quality=quality, method=6)
    print(f'{os.path.basename(dst)}: {len(frames)} frames, {sum(durations) / 1000:.1f} s, '
          f'{os.path.getsize(dst) // 1024} KB')


def still(src, dst, scale):
    img = scaled(Image.open(src).convert('RGB'), scale)
    img.save(dst, optimize=True)
    print(f'{os.path.basename(dst)}: {img.width}x{img.height}, {os.path.getsize(dst) // 1024} KB')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', default=os.path.join(HERE, 'out'))
    p.add_argument('--dest', default=os.path.join(HERE, '..', '..', 'images', 'explorer'))
    p.add_argument('--scale', type=float, default=0.8)
    p.add_argument('--fps', type=float, default=10)
    p.add_argument('--quality', type=int, default=70)
    p.add_argument('--hold', type=int, default=2500, help='ms of the last frame')
    p.add_argument('names', nargs='*')
    a = p.parse_args()
    os.makedirs(a.dest, exist_ok=True)
    names = a.names or sorted(n[:-4] if n.endswith('.png') else n for n in os.listdir(a.out)
                              if n.endswith('.png') or os.path.isdir(os.path.join(a.out, n)))
    for name in names:
        d = os.path.join(a.out, name)
        if os.path.isdir(d):
            animation(d, os.path.join(a.dest, name + '.webp'), a.scale, a.fps, a.quality, a.hold)
        else:
            still(d + '.png', os.path.join(a.dest, name + '.png'), a.scale)


if __name__ == '__main__':
    main()
