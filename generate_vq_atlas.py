#!/usr/bin/env python3
"""Generate a universal VQ codebook atlas for the Williams Tube display.

Trains a 256-entry codebook of 8×8 binary patterns by generating synthetic
dithered images at many grey levels and clustering the resulting blocks.

Usage:
    pip install Pillow numpy scikit-learn
    python generate_vq_atlas.py --output WilliamsTube_VQAtlas.png
    python generate_vq_atlas.py --output WilliamsTube_VQAtlas.png --preview
"""

import argparse
import numpy as np
from PIL import Image


def generate_training_blocks(block_size=8, num_grey_levels=64, patches_per_level=200):
    """Generate training data: 8×8 blocks from dithered synthetic images.

    Creates gradient and flat images at many grey levels, dithers them,
    and extracts all 8×8 blocks as 64-dimensional binary vectors.
    """
    blocks = []
    bw, bh = block_size, block_size

    for grey in np.linspace(0, 255, num_grey_levels):
        # Flat grey patch, large enough to get many blocks
        patch_w = bw * 16
        patch_h = bh * 16
        img = Image.new("L", (patch_w, patch_h), int(grey))
        img = img.convert("1")  # Floyd-Steinberg dither
        pixels = np.array(img, dtype=np.float32).flatten()

        for by in range(0, patch_h - bh + 1, bh):
            for bx in range(0, patch_w - bw + 1, bw):
                block = []
                for row in range(bh):
                    for col in range(bw):
                        px = bx + col
                        py = by + row
                        block.append(pixels[py * patch_w + px])
                blocks.append(block)

    # Also add gradient images for edge patterns
    for angle in range(0, 180, 15):
        patch_w = bw * 20
        patch_h = bh * 20
        img = Image.new("L", (patch_w, patch_h))
        pixels_arr = np.zeros((patch_h, patch_w), dtype=np.uint8)
        rad = np.radians(angle)
        for y in range(patch_h):
            for x in range(patch_w):
                val = (np.cos(rad) * x + np.sin(rad) * y) / (patch_w + patch_h) * 255
                pixels_arr[y, x] = int(np.clip(val, 0, 255))
        img = Image.fromarray(pixels_arr, "L")
        img = img.convert("1")
        pixels = np.array(img, dtype=np.float32).flatten()

        for by in range(0, patch_h - bh + 1, bh):
            for bx in range(0, patch_w - bw + 1, bw):
                block = []
                for row in range(bh):
                    for col in range(bw):
                        px = bx + col
                        py = by + row
                        block.append(pixels[py * patch_w + px])
                blocks.append(block)

    # Add real image blocks if available
    import glob
    import os
    image_dir = os.path.dirname(os.path.abspath(__file__))
    for path in glob.glob(os.path.join(image_dir, "*.png")) + glob.glob(os.path.join(image_dir, "*.jpg")):
        if "Atlas" in os.path.basename(path):
            continue
        try:
            real_img = Image.open(path).convert("L")
            # Resize to something reasonable
            real_img.thumbnail((256, 256), Image.Resampling.LANCZOS)
            real_img = real_img.convert("1")
            w, h = real_img.size
            pixels = np.array(real_img, dtype=np.float32).flatten()
            for by in range(0, h - bh + 1, bh):
                for bx in range(0, w - bw + 1, bw):
                    block = []
                    for row in range(bh):
                        for col in range(bw):
                            block.append(pixels[(by + row) * w + (bx + col)])
                    blocks.append(block)
        except Exception:
            pass

    return np.array(blocks, dtype=np.float32)


def train_codebook(blocks, n_codes=256):
    """K-means cluster blocks into n_codes centroids, then binarize."""
    from sklearn.cluster import MiniBatchKMeans

    print(f"Training codebook: {len(blocks)} blocks → {n_codes} codes...")
    kmeans = MiniBatchKMeans(n_clusters=n_codes, random_state=42, batch_size=1000,
                             n_init=3, max_iter=300)
    kmeans.fit(blocks)

    # Binarize centroids (threshold at 0.5)
    codebook = (kmeans.cluster_centers_ > 0.5).astype(np.uint8)

    # Sort by density (number of lit pixels) for predictable ordering
    densities = codebook.sum(axis=1)
    order = np.argsort(densities)
    codebook = codebook[order]

    print(f"  Density range: {densities.min()} to {densities.max()} lit pixels per block")
    return codebook


def codebook_to_atlas(codebook, block_w=8, block_h=8, cell_px=8, output="atlas.png"):
    """Render codebook entries into a 16×16 atlas image."""
    n_codes = len(codebook)
    atlas_cols = 16
    atlas_rows = (n_codes + atlas_cols - 1) // atlas_cols

    scale_x = cell_px // block_w
    scale_y = cell_px // block_h
    img_w = atlas_cols * cell_px
    img_h = atlas_rows * cell_px

    print(f"Atlas: {atlas_cols}x{atlas_rows} grid, {cell_px}px cells, {img_w}x{img_h} pixels")

    img = Image.new("RGBA", (img_w, img_h), (0, 0, 0, 0))
    pixels = img.load()

    for idx in range(n_codes):
        grid_col = idx % atlas_cols
        grid_row = idx // atlas_cols
        cx = grid_col * cell_px
        cy = grid_row * cell_px

        pattern = codebook[idx]
        for bit_idx in range(len(pattern)):
            if pattern[bit_idx]:
                row = bit_idx // block_w
                col = bit_idx % block_w
                for sy in range(scale_y):
                    for sx in range(scale_x):
                        px = cx + col * scale_x + sx
                        py = cy + row * scale_y + sy
                        if px < img_w and py < img_h:
                            pixels[px, py] = (255, 255, 255, 255)

    img.save(output)
    print(f"Saved: {output}")
    return atlas_cols, atlas_rows


def save_codebook(codebook, path="vq_codebook.npy"):
    """Save codebook as numpy array for the OSC encoder."""
    np.save(path, codebook)
    print(f"Codebook saved: {path} ({codebook.shape})")


def preview_codebook(codebook, block_w=8, block_h=8):
    """Print codebook entries in terminal."""
    n = len(codebook)
    cols = 16
    for start in range(0, n, cols):
        end = min(start + cols, n)
        for row in range(block_h):
            line = ""
            for idx in range(start, end):
                for col in range(block_w):
                    bit = row * block_w + col
                    line += "\u2588" if codebook[idx][bit] else " "
                line += " "
            print(line)
        # Density labels
        labels = ""
        for idx in range(start, end):
            d = int(codebook[idx].sum())
            labels += f"{idx:>3}({d:>2}) "
        print(labels)
        print()


def main():
    parser = argparse.ArgumentParser(description="Generate VQ codebook atlas")
    parser.add_argument("--output", "-o", default="WilliamsTube_VQAtlas.png")
    parser.add_argument("--codebook-out", default="vq_codebook.npy")
    parser.add_argument("--n-codes", type=int, default=256)
    parser.add_argument("--block-size", type=int, default=8)
    parser.add_argument("--cell-px", type=int, default=8, help="Pixels per cell in atlas")
    parser.add_argument("--preview", action="store_true")
    args = parser.parse_args()

    blocks = generate_training_blocks(block_size=args.block_size)
    codebook = train_codebook(blocks, n_codes=args.n_codes)
    save_codebook(codebook, args.codebook_out)
    codebook_to_atlas(codebook, block_w=args.block_size, block_h=args.block_size,
                      cell_px=args.cell_px, output=args.output)

    if args.preview:
        preview_codebook(codebook, block_w=args.block_size, block_h=args.block_size)


if __name__ == "__main__":
    main()
