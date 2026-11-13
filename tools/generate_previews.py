#!/usr/bin/env python3
"""Capture both documentation previews from a freshly built application."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    project = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="openkeys-preview-") as temporary:
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_STYLE_OVERRIDE="Fusion",
                           QT_SCALE_FACTOR="1", QT_FONT_DPI="96", QT_AUTO_SCREEN_SCALE_FACTOR="0",
                           XDG_CONFIG_HOME=temporary, XDG_DATA_HOME=temporary)
        for language, name in [("en", "preview.png"), ("zh_CN", "preview.zh-CN.png")]:
            output = Path(temporary) / name
            subprocess.run([str(executable), "--language", language, "--screenshot", str(output)],
                           env=environment, cwd=temporary, check=True, timeout=30)
            (project / "docs" / name).write_bytes(output.read_bytes())
            print(f"Captured {language}: docs/{name}")


if __name__ == "__main__":
    main()
