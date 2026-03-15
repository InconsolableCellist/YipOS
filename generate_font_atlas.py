#!/usr/bin/env python3
"""Generate font atlas PNGs for the Williams Tube display.

Modes:
  braille  - 16×16 grid (256 entries) of 2×4 braille dot patterns
  block4x4 - 256×256 grid (65536 entries) of 4×4 pixel block patterns

Usage:
  pip install Pillow
  python generate_font_atlas.py --mode braille --output WilliamsTube_BrailleAtlas.png
  python generate_font_atlas.py --mode block4x4 --output WilliamsTube_4x4Atlas.png
"""

import argparse
from PIL import Image, ImageDraw


def generate_block_atlas(block_w, block_h, cell_px=4, output="atlas.png"):
    """Generate an atlas of all possible block patterns.

    For block_w×block_h blocks, there are 2^(block_w*block_h) patterns.
    Atlas is arranged in a grid of atlas_cols × atlas_rows cells.
    Each cell is cell_px × cell_px pixels (scaled up from block_w × block_h).
    """
    num_bits = block_w * block_h
    num_patterns = 1 << num_bits  # 2^bits

    # Grid dimensions: square root, rounded up
    import math
    atlas_cols = int(math.ceil(math.sqrt(num_patterns)))
    atlas_rows = int(math.ceil(num_patterns / atlas_cols))

    print(f"Generating {block_w}x{block_h} block atlas:")
    print(f"  {num_patterns} patterns in {atlas_cols}x{atlas_rows} grid")
    print(f"  Cell size: {cell_px}x{cell_px} pixels")
    print(f"  Atlas size: {atlas_cols * cell_px}x{atlas_rows * cell_px} pixels")

    # RGBA: lit pixels = white with alpha=1, unlit = black with alpha=0
    # The shader uses glyph.a as the mask when alpha > 0.01
    img = Image.new("RGBA", (atlas_cols * cell_px, atlas_rows * cell_px), (0, 0, 0, 0))

    scale_x = cell_px / block_w
    scale_y = cell_px / block_h

    for pattern_idx in range(num_patterns):
        grid_col = pattern_idx % atlas_cols
        grid_row = pattern_idx // atlas_cols

        # Cell origin in the atlas
        cx = grid_col * cell_px
        cy = grid_row * cell_px

        # Decode pattern bits (row-major, matching encode_block)
        bit = 0
        for row in range(block_h):
            for col in range(block_w):
                if pattern_idx & (1 << bit):
                    # Fill the scaled pixel rectangle
                    x0 = cx + int(col * scale_x)
                    y0 = cy + int(row * scale_y)
                    x1 = cx + int((col + 1) * scale_x)
                    y1 = cy + int((row + 1) * scale_y)
                    for py in range(y0, y1):
                        for px in range(x0, x1):
                            img.putpixel((px, py), (255, 255, 255, 255))
                bit += 1

        if pattern_idx % 10000 == 0 and pattern_idx > 0:
            print(f"  Progress: {pattern_idx}/{num_patterns}")

    img.save(output)
    print(f"  Saved: {output}")
    return atlas_cols, atlas_rows


def generate_braille_atlas(cell_px=32, output="braille_atlas.png"):
    """Generate 16×16 grid of 2×4 braille dot patterns (256 entries)."""
    atlas_cols = 16
    atlas_rows = 16

    img = Image.new("L", (atlas_cols * cell_px, atlas_rows * cell_px), 0)
    draw = ImageDraw.Draw(img)

    dot_radius = cell_px // 8
    margin_x = cell_px // 4
    margin_y = cell_px // 8
    spacing_x = (cell_px - 2 * margin_x)
    spacing_y = (cell_px - 2 * margin_y) // 3

    for pattern_idx in range(256):
        grid_col = pattern_idx % atlas_cols
        grid_row = pattern_idx // atlas_cols
        cx = grid_col * cell_px
        cy = grid_row * cell_px

        # Braille bit layout: col0 rows 0-3, then col1 rows 0-3
        # But we use row-major: bit 0 = (0,0), bit 1 = (1,0), bit 2 = (0,1), etc.
        bit = 0
        for row in range(4):
            for col in range(2):
                if pattern_idx & (1 << bit):
                    dx = cx + margin_x + col * spacing_x
                    dy = cy + margin_y + row * spacing_y
                    draw.ellipse([dx - dot_radius, dy - dot_radius,
                                  dx + dot_radius, dy + dot_radius], fill=255)
                bit += 1

    img.save(output)
    print(f"Braille atlas saved: {output} ({atlas_cols}x{atlas_rows} grid, {cell_px}px cells)")


def main():
    parser = argparse.ArgumentParser(description="Generate Williams Tube font atlas")
    parser.add_argument("--mode", choices=["braille", "block4x4"], default="block4x4")
    parser.add_argument("--output", "-o", help="Output PNG path")
    parser.add_argument("--cell-px", type=int, default=None,
                        help="Pixels per cell (default: 32 for braille, 4 for block4x4)")
    args = parser.parse_args()

    if args.mode == "braille":
        cell_px = args.cell_px or 32
        output = args.output or "WilliamsTube_BrailleAtlas.png"
        generate_braille_atlas(cell_px=cell_px, output=output)
    elif args.mode == "block4x4":
        cell_px = args.cell_px or 4
        output = args.output or "WilliamsTube_4x4Atlas.png"
        generate_block_atlas(4, 4, cell_px=cell_px, output=output)


if __name__ == "__main__":
    main()
