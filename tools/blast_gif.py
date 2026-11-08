"""Builds assets/blast.gif from assets/blast.jpg: a white flash, the cloud coming through it while the ground
shakes, then a slow push in. Run once with Pillow installed; the GIF is kept in the repository.

    python3 tools/blast_gif.py
"""
import random
from pathlib import Path

from PIL import Image, ImageEnhance

root = Path(__file__).resolve().parent.parent
photo = Image.open(root / "assets" / "blast.jpg").convert("RGB")
width = 360
height = round(photo.height * width / photo.width)
# The fireball, where the zoom closes in.
centre = (photo.width * 0.5, photo.height * 0.42)
random.seed(37)


def framed(zoom, shake=0, brightness=1.0):
    """The photo zoomed on the fireball, shaken by up to shake pixels, at the given brightness."""
    w = photo.width / zoom
    h = photo.height / zoom
    dx = random.uniform(-shake, shake) * photo.width / width
    dy = random.uniform(-shake, shake) * photo.width / width
    left = min(max(centre[0] - w / 2 + dx, 0), photo.width - w)
    top = min(max(centre[1] - h / 2 + dy, 0), photo.height - h)
    frame = photo.crop((round(left), round(top), round(left + w), round(top + h))).resize((width, height), Image.LANCZOS)
    return ImageEnhance.Brightness(frame).enhance(brightness)


white = Image.new("RGB", (width, height), (255, 252, 240))
frames, durations = [], []
# The flash.
for _ in range(3):
    frames.append(white)
    durations.append(70)
# The cloud comes through the light; the shock wave shakes everything.
for step in range(10):
    fade = step / 9
    zoom = 1.35 - 0.3 * fade
    frames.append(Image.blend(white, framed(zoom, shake=14 * (1 - fade), brightness=1.6 - 0.6 * fade), fade))
    durations.append(80)
# The cloud stands, glowing, as the eye moves closer.
for step in range(8):
    pulse = 1.0 + 0.06 * (1 if step % 4 < 2 else -1) * (1 - step / 8)
    frames.append(framed(1.05 + 0.1 * step / 7, brightness=pulse))
    durations.append(160)
durations[-1] = 1500

palette_frames = [frame.quantize(colors=96, method=Image.Quantize.MEDIANCUT) for frame in frames]
out = root / "assets" / "blast.gif"
palette_frames[0].save(out, save_all=True, append_images=palette_frames[1:], duration=durations, loop=0,
                       optimize=True)
print(out, out.stat().st_size, "bytes", len(frames), "frames")
