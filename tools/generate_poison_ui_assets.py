#!/usr/bin/env python3

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
BRAND_SOURCES = (
    ROOT / "docs/assets/poisoned_os.svg",
    ROOT / "docs/assets/poison.svg",
)
ANIMATION_ROOT = ROOT / "assets/poison"
ICON_ROOT = ROOT / "assets/icons/Poison"
SLIDESHOW_ROOT = ROOT / "assets/slideshow/poison_update"
LEVELUP_ROOT = ROOT / "assets/icons/Animations"


@dataclass(frozen=True)
class AnimationSpec:
    category: str
    name: str
    source_index: int
    frame_count: int
    frame_rate: int
    duration: int
    min_level: int
    max_level: int
    weight: int
    mode: str


ANIMATIONS = (
    AnimationSpec("internal", "L1_Tv_128x47", 0, 4, 3, 45, 1, 3, 3, "assay"),
    AnimationSpec("internal", "L1_BadBattery_128x47", 1, 4, 3, 45, 1, 3, 3, "battery"),
    AnimationSpec("internal", "L1_NoSd_128x49", 0, 4, 3, 45, 1, 3, 6, "storage"),
    AnimationSpec("blocking", "L0_NoDb_128x51", 1, 3, 2, 0, 0, 0, 0, "database"),
    AnimationSpec("blocking", "L0_SdBad_128x51", 0, 3, 2, 0, 0, 0, 0, "storage_bad"),
    AnimationSpec("blocking", "L0_SdOk_128x51", 0, 3, 2, 0, 0, 0, 0, "storage_ok"),
    AnimationSpec("blocking", "L0_Url_128x51", 1, 3, 2, 0, 0, 0, 0, "link"),
    AnimationSpec("blocking", "L0_NewMail_128x51", 0, 3, 2, 0, 0, 0, 0, "message"),
    AnimationSpec("external", "P1_Assay_128x64", 0, 6, 4, 50, 1, 3, 5, "assay"),
    AnimationSpec("external", "P1_Containment_128x64", 1, 6, 4, 50, 1, 3, 5, "containment"),
    AnimationSpec("external", "P2_Link_128x64", 0, 6, 4, 50, 2, 3, 5, "link"),
    AnimationSpec("external", "P3_Signal_128x64", 1, 6, 4, 50, 3, 3, 5, "signal"),
)


def render_svg(svg: Path, output: Path, size: int = 96) -> None:
    converter = shutil.which("rsvg-convert")
    if not converter:
        raise RuntimeError("rsvg-convert is required to rasterize the brand SVGs")
    subprocess.run(
        [converter, "-w", str(size), "-h", str(size), "-o", str(output), str(svg)],
        check=True,
    )


def logo_mask(source: Image.Image, size: tuple[int, int]) -> Image.Image:
    rgba = source.convert("RGBA")
    alpha = rgba.getchannel("A").resize(size, Image.Resampling.LANCZOS)
    return alpha.point(lambda value: 255 if value >= 64 else 0).convert("1")


def paste_logo(canvas: Image.Image, logo: Image.Image, x: int, y: int) -> None:
    canvas.paste(0, (x, y), logo)


def draw_brackets(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int]) -> None:
    left, top, right, bottom = box
    length = 6
    for x, x_direction in ((left, 1), (right, -1)):
        for y, y_direction in ((top, 1), (bottom, -1)):
            draw.line((x, y, x + x_direction * length, y), fill=0)
            draw.line((x, y, x, y + y_direction * length), fill=0)


def draw_status_symbol(draw: ImageDraw.ImageDraw, mode: str, frame: int) -> None:
    pulse = frame % 3
    if mode.startswith("storage"):
        draw.polygon(((92, 17), (105, 17), (112, 24), (112, 37), (92, 37)), outline=0)
        draw.line((105, 17, 105, 24, 112, 24), fill=0)
        if mode.endswith("ok"):
            draw.line((97, 29, 101, 33, 108, 24), fill=0, width=2)
        else:
            draw.line((97, 24, 108, 34), fill=0, width=2)
            draw.line((108, 24, 97, 34), fill=0, width=2)
    elif mode == "battery":
        draw.rectangle((91, 20, 112, 34), outline=0)
        draw.rectangle((113, 24, 115, 30), fill=0)
        draw.rectangle((94, 23, 98 + pulse * 4, 31), fill=0)
    elif mode == "database":
        draw.ellipse((92, 17, 114, 25), outline=0)
        draw.line((92, 21, 92, 36), fill=0)
        draw.line((114, 21, 114, 36), fill=0)
        draw.arc((92, 29, 114, 39), 0, 180, fill=0)
        draw.line((97, 26, 109, 34), fill=0)
        draw.line((109, 26, 97, 34), fill=0)
    elif mode == "message":
        draw.rectangle((91, 19, 115, 36), outline=0)
        draw.line((91, 19, 103, 29, 115, 19), fill=0)
        draw.line((91, 36, 99, 28), fill=0)
        draw.line((115, 36, 107, 28), fill=0)
    elif mode == "link":
        for offset in range(3):
            x = 91 + offset * 9 + ((frame + offset) % 2) * 2
            draw.rectangle((x, 24, x + 6, 29), outline=0)


def animation_frame(
    spec: AnimationSpec, logos: tuple[Image.Image, Image.Image], frame: int
) -> Image.Image:
    height = int(spec.name.rsplit("x", 1)[1])
    image = Image.new("1", (128, height), 1)
    draw = ImageDraw.Draw(image)
    phase = frame % spec.frame_count
    source = logos[spec.source_index]

    if spec.category == "blocking":
        logo = logo_mask(source, (36, 36))
        paste_logo(image, logo, 29, max(1, (height - 36) // 2))
        draw.line((72, 8, 72, height - 8), fill=0)
        draw_status_symbol(draw, spec.mode, phase)
        for x in range(76 + phase, 124, 8):
            draw.point((x, height - 5), fill=0)
        return image

    if spec.category == "internal":
        logo = logo_mask(source, (34 + phase % 2, 38 + phase % 2))
        paste_logo(image, logo, 35, max(1, (height - logo.height) // 2))
        draw_brackets(draw, (28 - phase % 2, 3, 77 + phase % 2, height - 4))
        draw_status_symbol(draw, spec.mode, phase)
        return image

    if spec.mode == "assay":
        logo = logo_mask(source, (32, 39))
        paste_logo(image, logo, 87, 10)
        scan_y = 12 + phase * 7
        draw.line((80, scan_y, 124, scan_y), fill=0)
        draw_brackets(draw, (80, 5, 124, 55))
    elif spec.mode == "containment":
        size = 36 + phase % 3
        logo = logo_mask(source, (size, size))
        paste_logo(image, logo, 84, (58 - size) // 2)
        inset = phase % 3
        draw_brackets(draw, (79 + inset, 6 + inset, 124 - inset, 54 - inset))
    elif spec.mode == "link":
        left = logo_mask(logos[0], (18, 23))
        right = logo_mask(logos[1], (22, 22))
        paste_logo(image, left, 81, 20)
        paste_logo(image, right, 105, 21)
        for x in range(98 + phase % 2, 106, 4):
            draw.rectangle((x, 31, x + 2, 32), fill=0)
        draw_brackets(draw, (78, 10, 126, 51))
    else:
        logo = logo_mask(source, (30, 30))
        paste_logo(image, logo, 91, 16)
        for ring in range(1, 4):
            offset = ring * 3 + phase % 2
            draw.arc((91 - offset, 16 - offset, 120 + offset, 45 + offset), 200, 340, fill=0)
    return image


def write_meta(
    path: Path,
    frame_count: int,
    frame_rate: int,
    duration: int,
    size: tuple[int, int],
) -> None:
    width, height = size
    path.write_text(
        "\n".join(
            (
                "Filetype: Flipper Animation",
                "Version: 1",
                "",
                f"Width: {width}",
                f"Height: {height}",
                f"Passive frames: {frame_count}",
                "Active frames: 0",
                "Frames order: " + " ".join(str(index) for index in range(frame_count)),
                "Active cycles: 0",
                f"Frame rate: {frame_rate}",
                f"Duration: {duration}",
                "Active cooldown: 0",
                "",
                "Bubble slots: 0",
                "",
            )
        )
    )


def write_manifests() -> None:
    for category in ("internal", "blocking", "external"):
        lines = ["Filetype: Flipper Animation Manifest", "Version: 1", ""]
        for spec in (item for item in ANIMATIONS if item.category == category):
            lines.extend(
                (
                    f"Name: {spec.name}",
                    "Min butthurt: 0",
                    "Max butthurt: 14",
                    f"Min level: {spec.min_level}",
                    f"Max level: {spec.max_level}",
                    f"Weight: {spec.weight}",
                    "",
                )
            )
        manifest = ANIMATION_ROOT / category / "manifest.txt"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text("\n".join(lines))


def generate_animations(logos: tuple[Image.Image, Image.Image]) -> None:
    write_manifests()
    for spec in ANIMATIONS:
        directory = ANIMATION_ROOT / spec.category / spec.name
        directory.mkdir(parents=True, exist_ok=True)
        frames = [animation_frame(spec, logos, frame) for frame in range(spec.frame_count)]
        for index, frame in enumerate(frames):
            frame.save(directory / f"frame_{index}.png", optimize=True)
        write_meta(
            directory / "meta.txt",
            spec.frame_count,
            spec.frame_rate,
            spec.duration,
            frames[0].size,
        )


def generate_icons(logos: tuple[Image.Image, Image.Image]) -> None:
    ICON_ROOT.mkdir(parents=True, exist_ok=True)
    for name, source in (("PoisonFlask", logos[0]), ("PoisonMark", logos[1])):
        for width, height in ((10, 10), (14, 14), (32, 32), (48, 48)):
            canvas = Image.new("1", (width, height), 1)
            paste_logo(canvas, logo_mask(source, (width, height)), 0, 0)
            canvas.save(ICON_ROOT / f"{name}_{width}x{height}.png", optimize=True)


def draw_label(draw: ImageDraw.ImageDraw, text: str, xy: tuple[int, int]) -> None:
    draw.text(xy, text, font=ImageFont.load_default(), fill=0)


def generate_levelups(logos: tuple[Image.Image, Image.Image]) -> None:
    for level, source in ((1, logos[0]), (2, logos[1])):
        directory = LEVELUP_ROOT / f"PoisonLevelup{level}_128x64"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "frame_rate").write_text("6\n")
        for frame in range(8):
            image = Image.new("1", (128, 64), 1)
            draw = ImageDraw.Draw(image)
            size = 24 + frame * 4
            logo = logo_mask(source, (size, size))
            paste_logo(image, logo, (128 - size) // 2, (60 - size) // 2)
            draw_brackets(draw, (4 + frame, 3 + frame // 2, 123 - frame, 58 - frame // 2))
            draw_label(draw, f"CLEARANCE {level}", (4, 53))
            image.save(directory / f"frame_{frame:02d}.png", optimize=True)


def generate_update_slideshow(logos: tuple[Image.Image, Image.Image]) -> None:
    SLIDESHOW_ROOT.mkdir(parents=True, exist_ok=True)
    messages = ("POISONEDOS", "VERIFYING", "FIELD READY", "INSTALL COMPLETE")
    for index, message in enumerate(messages):
        image = Image.new("1", (128, 64), 1)
        draw = ImageDraw.Draw(image)
        logo = logo_mask(logos[index % 2], (34, 42))
        paste_logo(image, logo, 7, 9)
        draw.line((47, 7, 47, 56), fill=0)
        draw_brackets(draw, (2, 3, 125, 60))
        draw_label(draw, message, (53, 15))
        draw_label(draw, f"PHASE {index + 1}/4", (53, 31))
        draw.line((53, 48, 53 + (index + 1) * 16, 48), fill=0)
        image.save(SLIDESHOW_ROOT / f"frame_{index:02d}.png", optimize=True)


def generate() -> None:
    for source in BRAND_SOURCES:
        if not source.is_file():
            raise FileNotFoundError(source)
    with tempfile.TemporaryDirectory(prefix="poison-ui-assets-") as temporary:
        temporary_root = Path(temporary)
        rendered = []
        for index, source in enumerate(BRAND_SOURCES):
            output = temporary_root / f"brand-{index}.png"
            render_svg(source, output)
            rendered.append(Image.open(output).copy())
        logos = (rendered[0], rendered[1])
        generate_animations(logos)
        generate_icons(logos)
        generate_levelups(logos)
        generate_update_slideshow(logos)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate deterministic PoisonedOS 1-bit firmware UI assets"
    )
    parser.parse_args()
    generate()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
