#!/usr/bin/env python3
"""Export Lily Remote HTML prototypes as PNGs for GIMP iteration.

Requires a Chromium-based browser; defaults to Brave on this machine.
"""
from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs" / "ui-prototypes" / "remote-kisss.html"
OUT = ROOT / "docs" / "ui-prototypes" / "exports"
BROWSER = "/usr/bin/brave"

SCREENS = [
    ("home", None, "remote-kisss-home"),
    ("media", "telia", "remote-kisss-media-telia"),
    ("media", "wiim", "remote-kisss-media-wiim"),
    ("media", "tv", "remote-kisss-media-tv"),
    ("media", "ls60", "remote-kisss-media-ls60"),
    ("lights", None, "remote-kisss-lights"),
    ("info", None, "remote-kisss-info"),
    ("more", None, "remote-kisss-more"),
]

EXPORT_STYLE = """
<style id="gimp-export-style">
  html, body {
    width: 540px !important;
    height: 960px !important;
    min-height: 0 !important;
    margin: 0 !important;
    padding: 0 !important;
    overflow: hidden !important;
    display: block !important;
    background: #e8e9dc !important;
  }
  .rail { display: none !important; }
  .screen {
    position: absolute !important;
    left: 0 !important;
    top: 0 !important;
    width: 540px !important;
    height: 960px !important;
    max-width: none !important;
    transform: none !important;
    margin: 0 !important;
    box-shadow: none !important;
  }
</style>
"""


def make_temp_html(source: str, page: str, deck: str | None) -> str:
    deck_js = ""
    if deck:
        deck_js = f"""
        document.querySelectorAll('[data-target]').forEach(b => b.classList.toggle('active', b.dataset.target === '{deck}'));
        document.querySelectorAll('[data-deck]').forEach(d => d.classList.toggle('active', d.dataset.deck === '{deck}'));
        if (window.targetPill) targetPill.textContent = document.querySelector('[data-target="{deck}"]').textContent;
        """

    export_script = f"""
{EXPORT_STYLE}
<script id="gimp-export-script">
  window.addEventListener('load', () => {{
    showPage('{page}');
    {deck_js}
  }});
</script>
"""
    return source.replace("</body>", export_script + "\n</body>")


def screenshot(html_path: Path, out_path: Path, scale: int) -> None:
    subprocess.run(
        [
            BROWSER,
            "--headless=new",
            "--no-sandbox",
            "--disable-gpu",
            "--hide-scrollbars",
            "--disable-dev-shm-usage",
            f"--force-device-scale-factor={scale}",
            "--window-size=540,960",
            f"--screenshot={out_path}",
            html_path.as_uri(),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def make_contact_sheet(paths: list[Path], out_path: Path) -> None:
    thumbs = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        im.thumbnail((270, 480), Image.Resampling.LANCZOS)
        canvas = Image.new("RGB", (300, 535), "#11120f")
        canvas.paste(im, ((300 - im.width) // 2, 12))
        thumbs.append(canvas)

    sheet = Image.new("RGB", (600, 2140), "#050505")
    for idx, thumb in enumerate(thumbs):
        x = (idx % 2) * 300
        y = (idx // 2) * 535
        sheet.paste(thumb, (x, y))
    sheet.save(out_path)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    source = SOURCE.read_text()
    exact_paths: list[Path] = []

    with tempfile.TemporaryDirectory(prefix="lily-remote-export-") as td:
        temp_dir = Path(td)
        for page, deck, stem in SCREENS:
            html = make_temp_html(source, page, deck)
            temp_html = temp_dir / f"{stem}.html"
            temp_html.write_text(html)

            exact = OUT / f"{stem}-540x960.png"
            big = OUT / f"{stem}-2160x3840-gimp.png"
            screenshot(temp_html, exact, scale=1)
            screenshot(temp_html, big, scale=4)
            exact_paths.append(exact)
            print(exact.relative_to(ROOT))
            print(big.relative_to(ROOT))

    sheet = OUT / "remote-kisss-contact-sheet.png"
    make_contact_sheet(exact_paths, sheet)
    print(sheet.relative_to(ROOT))


if __name__ == "__main__":
    main()
