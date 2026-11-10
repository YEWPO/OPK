#!/usr/bin/env python3
"""Export the editable application mark. Requires rsvg-convert and Pillow.

These are asset-authoring tools only; generated PNG/ICO files are checked in.
Run: python3 tools/generate_icons.py
"""

from pathlib import Path
import subprocess

from PIL import Image


def main():
    directory = Path(__file__).resolve().parents[1] / "assets" / "icons"
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = []
    for size in sizes:
        output = directory / f"openkeys-{size}.png"
        subprocess.run(
            ["rsvg-convert", "--width", str(size), "--height", str(size),
             "--output", str(output), str(directory / "openkeys.svg")],
            check=True,
        )
        with Image.open(output) as image:
            images.append(image.convert("RGBA"))
    images[-1].save(
        directory / "openkeys.ico",
        sizes=[(size, size) for size in sizes],
        append_images=images[:-1],
    )
    print(f"Exported {len(sizes)} PNG sizes and openkeys.ico in {directory}")


if __name__ == "__main__":
    main()
