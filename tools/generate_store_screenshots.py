#!/usr/bin/env python3
"""Generate unframed appstore screenshots at exact platform sizes."""

from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
IMG = ROOT / "resources" / "images"
ROUND = IMG / "round"
OUT = ROOT / "screenshots" / "store"
PUB = ROOT / "screenshots"

TOD_BANDS = {
    "day": None,
    "morning": [
        (80, 180, 230),
        (160, 220, 240),
        (255, 200, 80),
        (255, 160, 100),
        (255, 120, 60),
        (230, 140, 70),
    ],
    "evening": [
        (10, 20, 60),
        (40, 20, 90),
        (90, 30, 120),
        (180, 40, 120),
        (230, 100, 40),
        (255, 180, 40),
    ],
    "night": [
        (0, 0, 0),
        (0, 0, 0),
        (10, 20, 50),
        (15, 30, 80),
        (20, 50, 110),
        (25, 60, 130),
    ],
}


def load_font(size: int, bold: bool = True) -> ImageFont.ImageFont:
    candidates = [
        r"C:\Windows\Fonts\arialbd.ttf" if bold else r"C:\Windows\Fonts\arial.ttf",
        r"C:\Windows\Fonts\segoeuib.ttf" if bold else r"C:\Windows\Fonts\segoeui.ttf",
        r"C:\Windows\Fonts\arial.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def apply_tod(im: Image.Image, tod: str) -> Image.Image:
    bands = TOD_BANDS[tod]
    base = im.convert("RGBA")
    if not bands:
        return base
    w, h = base.size
    sky_h = int(h * 0.42)
    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    band_h = max(1, sky_h // len(bands))
    for i, color in enumerate(bands):
        draw.rectangle([0, i * band_h, w, (i + 1) * band_h + 1], fill=color + (235,))
    if tod == "night":
        cx, cy, radius = int(w * 0.72), sky_h // 3, 8
        draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=(240, 240, 240, 255))
        for point in [
            (w // 5, sky_h // 5),
            (w * 2 // 5, sky_h // 8),
            (w * 3 // 5, sky_h // 4),
            (w * 4 // 5, sky_h // 6),
            (w // 2, sky_h // 3),
        ]:
            draw.point(point, fill=(255, 255, 255, 255))
    else:
        cx, cy, radius = int(w * 0.28), sky_h // 3, 10
        draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=(255, 230, 80, 255))
    return Image.alpha_composite(base, overlay)


def outlined_text(draw: ImageDraw.ImageDraw, xy, text, font, fill) -> None:
    x, y = xy
    outline = (0, 0, 0, 200) if fill[0] > 128 else (255, 255, 255, 160)
    for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1)]:
        draw.text((x + dx, y + dy), text, font=font, fill=outline)
    draw.text((x, y), text, font=font, fill=fill)


def draw_clock(im: Image.Image, tod: str, time_str: str, date_str: str) -> Image.Image:
    draw = ImageDraw.Draw(im)
    w, h = im.size
    time_size = 42 if w < 230 else 48
    date_size = 18 if w < 230 else 20
    time_font = load_font(time_size, True)
    date_font = load_font(date_size, True)
    fill = (0, 0, 0, 255) if tod == "morning" else (255, 255, 255, 255)
    time_y = 8 if w < 230 else int(h * 0.12)
    date_y = time_y + (46 if w < 230 else 50)
    bbox = draw.textbbox((0, 0), time_str, font=time_font)
    outlined_text(draw, ((w - (bbox[2] - bbox[0])) // 2, time_y), time_str, time_font, fill)
    bbox = draw.textbbox((0, 0), date_str, font=date_font)
    outlined_text(draw, ((w - (bbox[2] - bbox[0])) // 2, date_y), date_str, date_font, fill)
    return im


def place_jeep(im: Image.Image, jeep_path: Path, x: int, road_y: int) -> Image.Image:
    jeep = Image.open(jeep_path).convert("RGBA")
    _jw, jh = jeep.size
    y = road_y - jh + max(4, jh // 10)
    im.paste(jeep, (x, y), jeep)
    return im


def save_shot(im: Image.Image, name: str) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    PUB.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    im.convert("RGB").save(path, optimize=True)
    pub_path = PUB / name
    im.convert("RGB").save(pub_path, optimize=True)
    print(f"wrote {name} {im.size} ({path.stat().st_size} bytes)")
    return path


def make_banner(
    outfile: Path,
    scene_path: Path,
    jeep_path: Path | None,
    tod: str,
    subtitle: str,
    round_face: bool,
) -> None:
    banner = Image.new("RGB", (720, 320), (18, 20, 24))
    scene = apply_tod(Image.open(scene_path).convert("RGBA"), tod)
    scale = 320 / scene.size[1]
    scene_r = scene.resize((int(scene.size[0] * scale), 320), Image.NEAREST)
    banner.paste(scene_r.convert("RGB"), (0, 0))

    fade = Image.new("RGBA", (720, 320), (0, 0, 0, 0))
    fade_draw = ImageDraw.Draw(fade)
    for x in range(280, 720):
        alpha = min(255, int((x - 280) / 280 * 230))
        fade_draw.line([(x, 0), (x, 319)], fill=(12, 14, 18, alpha))
    banner = Image.alpha_composite(banner.convert("RGBA"), fade)

    preview = scene.copy()
    if jeep_path is not None:
        road = 200 if round_face else 175
        jx = 90 if round_face else 60
        preview = place_jeep(preview, jeep_path, jx, road)
    preview = draw_clock(preview, tod, "10:42", "Sat Nov 14")

    if round_face:
        pw = 200
        prev = preview.resize((pw, pw), Image.NEAREST)
        mask = Image.new("L", preview.size, 0)
        ImageDraw.Draw(mask).ellipse([0, 0, preview.size[0] - 1, preview.size[1] - 1], fill=255)
        mask = mask.resize((pw, pw), Image.NEAREST)
        px, py = 48, (320 - pw) // 2
        bezel = Image.new("RGBA", (pw + 16, pw + 16), (0, 0, 0, 0))
        ImageDraw.Draw(bezel).ellipse([0, 0, pw + 15, pw + 15], fill=(28, 30, 34, 255))
        banner.paste(bezel, (px - 8, py - 8), bezel)
        banner.paste(prev, (px, py), mask)
    else:
        scale = 200 / preview.size[1]
        pw, ph = int(preview.size[0] * scale), 200
        prev = preview.resize((pw, ph), Image.NEAREST)
        px, py = 56, (320 - ph) // 2
        bezel = Image.new("RGBA", (pw + 16, ph + 16), (0, 0, 0, 0))
        ImageDraw.Draw(bezel).rounded_rectangle([0, 0, pw + 15, ph + 15], radius=14, fill=(28, 30, 34, 255))
        banner.paste(bezel, (px - 8, py - 8), bezel)
        banner.paste(prev.convert("RGB"), (px, py))

    draw = ImageDraw.Draw(banner)
    draw.text((360, 110), "Jeep Trails", font=load_font(54, True), fill=(255, 255, 255, 255))
    draw.text((360, 175), subtitle, font=load_font(22, False), fill=(200, 205, 210, 255))
    banner.convert("RGB").save(outfile, optimize=True)
    banner.convert("RGB").save(PUB / outfile.name, optimize=True)
    print(f"wrote banner {outfile.name} ({outfile.stat().st_size} bytes)")


def main() -> None:
    emery_specs = [
        ("emery_01_desert_day.png", "scene_desert.png", "jeep_1.png", "day", "10:42", "Sat Nov 14", None, 175),
        ("emery_02_forest_evening.png", "scene_forest.png", "jeep_2.png", "evening", "18:05", "Sat Nov 14", 55, 175),
        ("emery_03_mountain_morning.png", "scene_mountain.png", "jeep_0.png", "morning", "7:18", "Sun Nov 15", 90, 178),
        ("emery_04_moab_night.png", "scene_moab.png", "jeep_1.png", "night", "22:23", "Sat Nov 14", None, 170),
        ("emery_05_beach_drive.png", "scene_beach.png", "jeep_3.png", "day", "14:00", "Mon Nov 16", 70, 178),
    ]
    for name, scene, jeep, tod, tstr, dstr, jx, road in emery_specs:
        im = Image.open(IMG / scene).convert("RGBA")
        assert im.size == (200, 228), im.size
        im = apply_tod(im, tod)
        if jx is not None:
            im = place_jeep(im, IMG / jeep, jx, road)
        im = draw_clock(im, tod, tstr, dstr)
        save_shot(im, name)

    gabbro_specs = [
        ("gabbro_01_desert_day.png", "scene_desert.png", "jeep_1.png", "day", "10:42", "Sat Nov 14", None, 200),
        ("gabbro_02_forest_evening.png", "scene_forest.png", "jeep_2.png", "evening", "18:05", "Sat Nov 14", 70, 200),
        ("gabbro_03_mountain_morning.png", "scene_mountain.png", "jeep_0.png", "morning", "7:18", "Sun Nov 15", 110, 205),
        ("gabbro_04_moab_night.png", "scene_moab.png", "jeep_1.png", "night", "22:23", "Sat Nov 14", None, 195),
        ("gabbro_05_snow_drive.png", "scene_snow.png", "jeep_3.png", "night", "21:10", "Tue Nov 17", 95, 190),
    ]
    for name, scene, jeep, tod, tstr, dstr, jx, road in gabbro_specs:
        im = Image.open(ROUND / scene).convert("RGBA")
        assert im.size == (260, 260), im.size
        im = apply_tod(im, tod)
        if jx is not None:
            im = place_jeep(im, ROUND / jeep, jx, road)
        im = draw_clock(im, tod, tstr, dstr)
        save_shot(im, name)

    make_banner(
        OUT / "banner_emery.png",
        IMG / "scene_desert.png",
        IMG / "jeep_1.png",
        "day",
        "Pebble Time 2 · daily trails · hourly drive",
        round_face=False,
    )
    make_banner(
        OUT / "banner_gabbro.png",
        ROUND / "scene_beach.png",
        ROUND / "jeep_1.png",
        "evening",
        "Pebble Round 2 · daily trails · hourly drive",
        round_face=True,
    )


if __name__ == "__main__":
    main()
