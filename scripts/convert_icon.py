#!/usr/bin/env python3
"""Convert PNG to ICO or ICO to PNG for build icon assets."""
import sys
from PIL import Image

def main():
    if len(sys.argv) != 3:
        print("Usage: convert_icon.py <input> <output>", file=sys.stderr)
        sys.exit(1)
    inp = sys.argv[1]
    out = sys.argv[2]
    img = Image.open(inp).convert("RGBA")
    ext = out.lower().rsplit(".", 1)[-1] if "." in out else ""
    if ext == "ico":
        img.save(out, format="ICO", sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
    else:
        img.save(out, format="PNG")
    print(f"Saved {out}")

if __name__ == "__main__":
    main()
