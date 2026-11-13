#!/usr/bin/env python3
"""Verify bilingual documentation, translation coverage and checked-in image resources."""

from collections import Counter
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from PIL import Image

PROJECT = Path(__file__).resolve().parents[1]


def check(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    documents = [*PROJECT.glob("*.md"), *PROJECT.glob("docs/*.md")]
    for path in documents:
        chinese = path.name.endswith(".zh-CN.md")
        other = path.with_name(path.name.replace(".zh-CN.md", ".md") if chinese
                               else path.stem + ".zh-CN.md")
        content = path.read_text(encoding="utf-8")
        check(other.is_file(), f"Missing translation: {other}")
        check(f"]({other.name})" in "\n".join(content.splitlines()[:5]),
              f"Missing top language link: {path}")
        if not chinese:
            # Chinese is allowed only in the language navigation and stable bilingual UI names.
            body = content.replace("简体中文", "").replace("Language / 语言", "")
            check(not re.search(r"[\u3400-\u9fff]", body), f"Chinese text in English document: {path}")
        for target in re.findall(r"!?\[[^\]]*\]\(([^\s)]+)(?:\s+[^)]*)?\)", content):
            if "://" in target or target.startswith(("#", "mailto:")):
                continue
            target = target.split("#", 1)[0]
            check((path.parent / target).exists(), f"Broken link in {path}: {target}")

    tree = ET.parse(PROJECT / "translations/openkeys_zh_CN.ts")
    catalog = {}
    for context in tree.getroot().findall("context"):
        name = context.findtext("name")
        for message in context.findall("message"):
            source, translation = message.findtext("source"), message.find("translation")
            check(translation is not None and translation.get("type") not in {"unfinished", "vanished", "obsolete"},
                  f"Unfinished translation: {source}")
            translated = "".join(translation.itertext())
            check(bool(translated.strip()), f"Empty translation: {source}")
            placeholders = lambda text: Counter(re.findall(r"%(?:L?\d+|n|p%)", text))
            check(placeholders(source) == placeholders(translated), f"Placeholder mismatch: {source}")
            check((name, source) not in catalog, f"Duplicate translation: {source}")
            catalog[name, source] = translated
    literal = r'"(?:[^"\\]|\\.)*"'
    expression = re.compile(r'(?:QCoreApplication::translate|QT_TRANSLATE_NOOP)\(\s*"([^"\n]+)"\s*,\s*(' + literal + r'(?:\s*' + literal + r')*)')
    for path in (PROJECT / "src").rglob("*.cpp"):
        for context, raw in expression.findall(path.read_text(encoding="utf-8")):
            source = "".join(json.loads(part) for part in re.findall(literal, raw))
            check((context, source) in catalog, f"Missing Chinese translation in {path}: {source}")

    sizes = {16, 24, 32, 48, 64, 128, 256}
    for size in sizes:
        with Image.open(PROJECT / f"assets/icons/openkeys-{size}.png") as icon:
            check(icon.size == (size, size) and icon.mode == "RGBA", f"Invalid {size}px icon")
            check(icon.getpixel((0, 0))[3] == 0, f"Icon corner must be transparent: {size}px")
    with Image.open(PROJECT / "assets/icons/openkeys.ico") as icon:
        check(icon.ico.sizes() == {(size, size) for size in sizes}, "ICO dimensions are incomplete")
    previews = []
    for name in ["preview.png", "preview.zh-CN.png"]:
        with Image.open(PROJECT / "docs" / name) as preview:
            check(preview.size == (1200, 960), f"Unexpected preview dimensions: {name}: {preview.size}")
            previews.append(preview.tobytes())
    check(previews[0] != previews[1], "English and Chinese previews must differ")
    print(f"Verified {len(documents)} documents, {len(catalog)} translations, 7 PNG icons, ICO and both previews.")


if __name__ == "__main__":
    main()
