#!/usr/bin/env python3
import os
import subprocess
import tempfile
import re
from pathlib import Path
from PIL import Image

# Configuration
BROWSER = "/usr/bin/brave"
HTML_PROTO = "docs/ui-prototypes/remote.html"
OUTPUT_CPP = "src/icons.cpp"
OUTPUT_H = "src/icons.h"

# Icons to generate: (id_in_html, size, cpp_name)
ICONS = [
    ("mdi-volume-minus", 32, "kMdiVolumeMinus32Bmp"),
    ("mdi-volume-plus", 32, "kMdiVolumePlus32Bmp"),
    ("mdi-skip-previous", 32, "kMdiSkipPrev32Bmp"),
    ("mdi-skip-next", 32, "kMdiSkipNext32Bmp"),
    ("mdi-play-pause", 38, "kMdiPlayPause38Bmp"),
    ("mdi-radiobox-marked", 28, "kMdiRadioMarked28Bmp"),
    # Media Dashboard Icons
    ("mdi-rewind", 30, "kMdiRewind30Bmp"),
    ("mdi-fast-forward", 30, "kMdiFastForward30Bmp"),
    ("mdi-play-pause", 30, "kMdiPlayPause30Bmp"),
    ("mdi-set-top-box", 23, "kMdiBox23Bmp"),
    ("mdi-music", 23, "kMdiMusic23Bmp"),
    ("mdi-television-classic", 23, "kMdiTv23Bmp"),
    ("mdi-speaker", 23, "kMdiSpeaker23Bmp"),
    ("mdi-video-input-hdmi", 36, "kMdiHdmi36Bmp"),
    ("mdi-record-player", 36, "kMdiRecord36Bmp"),
    ("mdi-arrow-u-left-top-bold", 36, "kMdiBack36Bmp"),
    ("mdi-wifi", 36, "kMdiWifi36Bmp"),
    ("mdi-bluetooth", 36, "kMdiBluetooth36Bmp"),
    ("mdi-volume-minus", 42, "kMdiVolMinus42Bmp"),
    ("mdi-volume-mute", 42, "kMdiMute42Bmp"),
    ("mdi-volume-plus", 42, "kMdiVolPlus42Bmp"),
]

def get_svg_symbols(html_path):
    with open(html_path, 'r') as f:
        content = f.read()
    symbols = {}
    for match in re.finditer(r'<symbol id="([^"]+)" viewBox="([^"]+)">(.+?)</symbol>', content, re.DOTALL):
        symbols[match.group(1)] = {
            'viewBox': match.group(2),
            'content': match.group(3)
        }
    return symbols

def generate_bmp_bytes(symbol_id, symbol_data, size):
    with tempfile.TemporaryDirectory() as td:
        temp_html = Path(td) / "icon.html"
        temp_png = Path(td) / "icon.png"
        
        # Create a tiny HTML with just the SVG
        # We use a wrapper to ensure it centers and fills the size perfectly
        html_content = f"""
        <!DOCTYPE html>
        <html>
        <style>
            body {{ margin: 0; padding: 0; background: white; }}
            svg {{ width: {size}px; height: {size}px; display: block; }}
        </style>
        <body>
            <svg viewBox="{symbol_data['viewBox']}" fill="black">
                {symbol_data['content']}
            </svg>
        </body>
        </html>
        """
        temp_html.write_text(html_content)
        
        # Screenshot with Brave
        subprocess.run([
            BROWSER,
            "--headless=new",
            "--no-sandbox",
            "--disable-gpu",
            f"--window-size={size},{size}",
            f"--screenshot={temp_png}",
            temp_html.as_uri()
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        # Process PNG to 1-bit BMP
        img = Image.open(temp_png).convert("1")
        # Ensure it is exactly the target size
        img = img.resize((size, size), Image.Resampling.NEAREST)
        
        bmp_path = Path(td) / "icon.bmp"
        img.save(bmp_path)
        
        return bmp_path.read_bytes()

def format_cpp_array(name, data):
    lines = [f"const uint8_t {name}[] PROGMEM = {{"]
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_chunk = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"  {hex_chunk},")
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};")
    lines.append(f"const size_t {name}Size = sizeof({name});\n")
    return "\n".join(lines)

def main():
    symbols = get_svg_symbols(HTML_PROTO)
    
    # We want to APPEND to icons.cpp or replace? 
    # Let's read existing ones and append new ones if not present.
    with open(OUTPUT_H, 'r') as f:
        existing_h = f.read()
    
    with open(OUTPUT_CPP, 'r') as f:
        existing_cpp = f.read()

    new_h_entries = []
    new_cpp_entries = []

    for sym_id, size, cpp_name in ICONS:
        if cpp_name in existing_h:
            print(f"Skipping {cpp_name}, already exists.")
            continue
            
        print(f"Generating {cpp_name} from {sym_id} at {size}x{size}...")
        bmp_data = generate_bmp_bytes(sym_id, symbols[sym_id], size)
        
        new_h_entries.append(f"extern const uint8_t {cpp_name}[] PROGMEM;")
        new_h_entries.append(f"extern const size_t {cpp_name}Size;")
        
        new_cpp_entries.append(format_cpp_array(cpp_name, bmp_data))

    if new_h_entries:
        # Insert before the last #endif if it's there, or just at the end
        if "#pragma once" in existing_h:
             updated_h = existing_h.strip() + "\n" + "\n".join(new_h_entries) + "\n"
        else:
             updated_h = existing_h + "\n".join(new_h_entries) + "\n"
             
        with open(OUTPUT_H, 'w') as f:
            f.write(updated_h)
            
        with open(OUTPUT_CPP, 'a') as f:
            f.write("\n" + "\n".join(new_cpp_entries))
        
        print("Updated icons.h and icons.cpp")
    else:
        print("No new icons to add.")

if __name__ == "__main__":
    main()
