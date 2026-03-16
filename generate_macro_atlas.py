#!/usr/bin/env python3
"""Generate the macro glyph atlas for the Williams Tube PDA.

Produces a 4096x4096 grayscale atlas (8x8 grid, each cell 512x512 pixels).
Each cell contains a pre-rendered full-screen layout that can be stamped
onto the render texture in a single write via _AtlasMode=0 (block atlas).

The atlas is a luminance mask: white=foreground, black=background.
The WriteHead shader applies _Color/_BgColor tinting from the mask value.

Usage:
    python3 generate_macro_atlas.py [--output WilliamsTube_MacroAtlas.png]

Requires the PDA ROM atlas glyphs from generate_pda_rom.py.
"""

import argparse
import os
import shutil

from PIL import Image

# Import glyph data from the ROM generator
from generate_pda_rom import (
    build_cp437_font,
    build_custom_icons,
    UI_MAP,
    CELL_W,
    CELL_H,
)

# --- Display grid constants (must match pda_poc.py) ---
COLS = 40
ROWS = 8

# --- Macro atlas layout ---
MACRO_GRID_COLS = 8
MACRO_GRID_ROWS = 8
MACRO_CELL_W = 512   # matches RT resolution
MACRO_CELL_H = 512
ATLAS_W = MACRO_GRID_COLS * MACRO_CELL_W   # 4096
ATLAS_H = MACRO_GRID_ROWS * MACRO_CELL_H   # 4096

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

COPY_DEST_DIR = os.path.join(
    os.path.expanduser("~"),
    "Documents/unity/3d/unity/Rexipso Dark Test 1/"
    "Assets/Foxipso/Assets/Williams Tube/Textures",
)

# --- PDA ROM glyph constants (from pda_poc.py) ---
G_EMPTY = 0
G_HLINE = 1
G_VLINE = 2
G_TL_CORNER = 3
G_TR_CORNER = 4
G_BL_CORNER = 5
G_BR_CORNER = 6
G_L_TEE = 7
G_R_TEE = 8
G_T_TEE = 9
G_B_TEE = 10
G_CROSS = 11
G_SOLID = 12
G_UPPER = 13
G_LOWER = 14
G_LEFT = 15
G_RIGHT = 16
G_SHADE1 = 17
G_SHADE2 = 18
G_SHADE3 = 19
G_BULLET = 20
G_HEART = 21
G_NOTE = 22
G_DNOTE = 23
G_UP = 24
G_DOWN = 25
G_RIGHT_A = 26
G_LEFT_A = 27
G_GEAR = 28
G_HOME = 29

G_SIGNAL = 128
G_BATT_FULL = 129
G_LOCK = 133
G_UNLOCK = 134
G_CHECK = 141
G_XMARK = 142

INVERT_OFFSET = 128

# Touch zone fractional rows (same as pda_poc.py)
ZONE_ROWS = [
    ROWS * 1 / 6 - 0.5,   # 0.833
    ROWS * 3 / 6 - 0.5,   # 3.5
    ROWS * 5 / 6 - 0.5,   # 6.167
]

# Home screen tile labels
TILE_COLS = 5
TILE_ROWS = 3
TILE_LABELS = [
    ["STATS", "NET", "TRACK", "SPVR", "CONF"],
    ["VRCX", "HEART", "MAP", "DBG", "-----"],
    ["-----", "-----", "-----", "-----", "-----"],
]
CHARS_PER_TILE = COLS // TILE_COLS  # 8
# Column centers for even spacing across 40 cols (contact grid alignment)
TILE_CENTERS = [(2 * i + 1) * (COLS // (TILE_COLS * 2)) for i in range(TILE_COLS)]
# → [4, 12, 20, 28, 36]


# ---------------------------------------------------------------------------
# Glyph loader — builds a lookup table of ROM index → 8x16 bitmap
# ---------------------------------------------------------------------------

def build_rom_glyphs():
    """Build a dict mapping PDA ROM index → list of 16 row-bytes.

    Combines CP437 remapping (0-31), ASCII (32-127), custom icons (128-159),
    and inverted ASCII (160-255).
    """
    cp437 = build_cp437_font()
    icons = build_custom_icons()
    glyphs = {}

    # 0-31: UI primitives remapped from CP437
    for pda_idx, cp437_idx in UI_MAP.items():
        if cp437[cp437_idx] is not None:
            glyphs[pda_idx] = cp437[cp437_idx]

    # 32-127: printable ASCII from CP437
    for code in range(32, 128):
        if cp437[code] is not None:
            glyphs[code] = cp437[code]

    # 128-159: custom icons
    for pda_idx, bitmap in icons.items():
        glyphs[pda_idx] = bitmap

    # 160-255: inverted ASCII (handled at render time, not stored separately)
    # The renderer checks for INVERT_OFFSET and swaps fg/bg.

    return glyphs


# ---------------------------------------------------------------------------
# Screen buffer — lightweight 40x8 grid for building screen layouts
# ---------------------------------------------------------------------------

class MacroScreenBuffer:
    """40x8 grid of glyph indices for composing screen layouts."""

    def __init__(self):
        self.grid = [[0] * COLS for _ in range(ROWS)]

    def put(self, col, row, glyph_idx):
        """Place a glyph index at integer grid position."""
        r = round(row)
        if 0 <= col < COLS and 0 <= r < ROWS:
            self.grid[r][col] = glyph_idx

    def put_text(self, col, row, text, inverted=False):
        """Place a string of ASCII characters."""
        for i, ch in enumerate(text):
            char_idx = ord(ch) if 32 <= ord(ch) <= 126 else 32
            if inverted:
                char_idx += INVERT_OFFSET
            self.put(col + i, row, char_idx)

    def put_glyph(self, col, row, glyph_idx):
        """Place a ROM glyph index."""
        self.put(col, row, glyph_idx)

    def put_frame(self, title):
        """Draw the standard border frame with title on row 0."""
        self.put_glyph(0, 0, G_TL_CORNER)
        title_str = f" {title} "
        pad_left = (COLS - 2 - len(title_str)) // 2
        for c in range(1, 1 + pad_left):
            self.put_glyph(c, 0, G_HLINE)
        self.put_text(1 + pad_left, 0, title_str)
        for c in range(1 + pad_left + len(title_str), COLS - 1):
            self.put_glyph(c, 0, G_HLINE)
        self.put_glyph(COLS - 1, 0, G_TR_CORNER)

        # Side borders
        for r in range(1, 7):
            self.put_glyph(0, r, G_VLINE)
            self.put_glyph(COLS - 1, r, G_VLINE)

    def put_status_bar(self):
        """Draw the bottom border on row 7. Cursor and clock are dynamic."""
        self.put_glyph(0, 7, G_BL_CORNER)
        for c in range(1, COLS - 1):
            self.put_glyph(c, 7, G_HLINE)
        self.put_glyph(COLS - 1, 7, G_BR_CORNER)

    def put_hline(self, row, left_glyph=G_L_TEE, right_glyph=G_R_TEE):
        """Draw a horizontal separator line across the full width."""
        self.put_glyph(0, row, left_glyph)
        for c in range(1, COLS - 1):
            self.put_glyph(c, row, G_HLINE)
        self.put_glyph(COLS - 1, row, right_glyph)


# ---------------------------------------------------------------------------
# Screen layout definitions
# ---------------------------------------------------------------------------

def layout_home(buf):
    """Home screen: borders + title + tile labels."""
    buf.put_frame("YIP-BOI OS")

    # Tile labels at zone-center rows, centered on contact grid columns.
    # Active tiles are rendered inverted to indicate they are touchable.
    for ty in range(TILE_ROWS):
        row = round(ZONE_ROWS[ty])
        buf.put_glyph(0, row, G_VLINE)
        for tx in range(TILE_COLS):
            label = TILE_LABELS[ty][tx]
            is_active = label[0] != '-'
            center = TILE_CENTERS[tx]
            start_col = center - len(label) // 2
            buf.put_text(start_col, row, label, inverted=is_active)
        buf.put_glyph(COLS - 1, row, G_VLINE)

    buf.put_status_bar()


def layout_stats(buf):
    """Stats screen: borders + labels + empty bars + units."""
    buf.put_frame("SYSTEM STATUS")

    # Row 1: CPU
    buf.put_text(1, 1, "CPU")
    # Percentage placeholder (4 chars) — left blank for dynamic
    # Empty bar background
    bar_col, bar_width = 10, 20
    for c in range(bar_width):
        buf.put_glyph(bar_col + c, 1, G_SHADE1)
    buf.put_glyph(33, 1, G_GEAR)
    buf.put_text(34, 1, "C")

    # Row 2: MEM
    buf.put_text(1, 2, "MEM")
    for c in range(bar_width):
        buf.put_glyph(bar_col + c, 2, G_SHADE1)

    # Row 3: GPU
    buf.put_text(1, 3, "GPU")
    for c in range(bar_width):
        buf.put_glyph(bar_col + c, 3, G_SHADE1)
    buf.put_glyph(33, 3, G_GEAR)
    buf.put_text(34, 3, "C")

    # Row 4: NET
    buf.put_text(1, 4, "NET")
    buf.put_glyph(5, 4, G_UP)
    buf.put_glyph(11, 4, G_DOWN)
    # Units (k/M) are dynamic — values span different magnitudes

    # Row 4-5: DISK (inverted "DSK" button at col 36 row 4 — aligns with touch 52)
    buf.put_text(35, 4, "DSK", inverted=True)
    buf.put_text(1, 5, "DISK")
    for c in range(bar_width):
        buf.put_glyph(bar_col + c, 5, G_SHADE1)

    # Row 6: UPTIME (system + YIP OS session)
    buf.put_text(1, 6, "UP")
    buf.put_text(24, 6, "YIP")

    buf.put_status_bar()


def layout_stay(buf):
    """StayPutVR screen: borders + separator + body part tiles (unlocked)."""
    buf.put_frame("STAYPUTVR")

    # Row 1: connection status placeholder (dynamic)
    # Just the static label positions
    buf.put_text(24, 1, "DRIFT:")

    # Row 2: separator
    buf.put_hline(2)

    # Body part tiles — labels inverted (touchable), state text normal
    tile_positions = [TILE_CENTERS[0] - 3, TILE_CENTERS[1] - 3, TILE_CENTERS[3] - 3]
    # → [1, 9, 25]
    parts_row1 = ["LW", "RW", "HEAD"]
    parts_row2 = ["LF", "RF", "PLAY"]

    for i, pos in enumerate(tile_positions):
        # Row 1 of parts (display row 3)
        part = parts_row1[i]
        buf.put_glyph(pos, 3, G_UNLOCK)
        label = f" {part:4s} "
        buf.put_text(pos + 1, 3, label, inverted=True)
        state = " FREE ".center(8)[:8]
        buf.put_text(pos, 4, state)

        # Row 2 of parts (display row 5)
        part = parts_row2[i]
        buf.put_glyph(pos, 5, G_UNLOCK)
        label = f" {part:4s} "
        buf.put_text(pos + 1, 5, label, inverted=True)
        state = " FREE ".center(8)[:8]
        buf.put_text(pos, 6, state)

    buf.put_status_bar()


def layout_net(buf):
    """Network screen: frame + status bar only — all content is dynamic."""
    buf.put_frame("NETWORK")
    buf.put_status_bar()


def layout_heart(buf):
    """Heart rate monitor: frame + static labels + scale."""
    buf.put_frame("HEARTBEAT")
    # Row 1 static labels
    buf.put_text(8, 1, "BPM")
    buf.put_text(14, 1, "HI:")
    buf.put_text(21, 1, "LO:")
    buf.put_text(28, 1, "AVG:")
    # Y-axis scale labels
    buf.put_text(1, 2, "130")
    buf.put_glyph(4, 2, G_VLINE)
    buf.put_glyph(4, 3, G_VLINE)
    buf.put_glyph(4, 4, G_VLINE)
    buf.put_glyph(4, 5, G_VLINE)
    buf.put_text(1, 6, " 60")
    buf.put_glyph(4, 6, G_VLINE)
    buf.put_status_bar()


def layout_vrcx(buf):
    """VRCX landing: frame + inverted tiles + status bar."""
    buf.put_frame("VRCX")
    btn_cols = [4, 20, 36]

    # Row 1 tiles (touch row 1): WORLDS, FEED, STATUS
    labels_r1 = [("WORLDS", True), ("FEED", False), ("STATUS", False)]
    row1 = round(ZONE_ROWS[0])  # row 1
    for (label, active), col in zip(labels_r1, btn_cols):
        start = col - len(label) // 2
        buf.put_text(start, row1, label, inverted=active)

    # Row 2 tiles (touch row 2): NOTIF, -----, -----
    labels_r2 = [("NOTIF", False), ("-----", False), ("-----", False)]
    row2 = round(ZONE_ROWS[1])  # row 4
    for (label, active), col in zip(labels_r2, btn_cols):
        start = col - len(label) // 2
        buf.put_text(start, row2, label, inverted=active)

    buf.put_status_bar()


def layout_vrcx_worlds(buf):
    """VRCX Worlds: frame + TR select hint + status bar."""
    buf.put_frame("WORLDS")
    # Right border arrow to indicate TR selects the highlighted item
    buf.put_glyph(COLS - 1, 1, G_RIGHT_A)
    buf.put_status_bar()


def layout_vrcx_world_detail(buf):
    """VRCX World Detail: frame + REJOIN button (2 rows) + status bar."""
    buf.put_frame("WORLD")
    # Rows 5-6: REJOIN button (inverted, right-aligned near touch 53)
    r1 = "REJOIN"
    r2 = "(OPN BRWSR)"
    buf.put_text(COLS - 1 - len(r1), 5, r1, inverted=True)
    buf.put_text(COLS - 1 - len(r2), 6, r2, inverted=True)
    buf.put_status_bar()


def _layout_conf_page(buf, title, labels_row1, labels_row2):
    """Shared layout for config pages: frame + inverted labels + separator + status bar.

    Labels are inverted on rows 1,4 (aligned with touch contacts).
    Dynamic values (plain text) on rows 2,5 written by RenderDynamic().
    """
    buf.put_frame(title)
    btn_cols = [4, 20, 36]

    # Row 1: inverted labels = touchable buttons (touch row 1)
    for label, col in zip(labels_row1, btn_cols):
        start = col - len(label) // 2
        buf.put_text(start, 1, label, inverted=True)

    # Row 3: horizontal separator
    for c in range(1, COLS - 1):
        buf.put_glyph(c, 3, G_HLINE)

    # Row 4: inverted labels = touchable buttons (touch row 2)
    for label, col in zip(labels_row2, btn_cols):
        start = col - len(label) // 2
        buf.put_text(start, 4, label, inverted=True)

    buf.put_status_bar()


def layout_conf_p1(buf):
    """Config page 1: BOOT/WRITE/SETTL + LOG/DBNCE/NVRAM."""
    _layout_conf_page(buf, "CONFIG 1/2",
                      ["BOOT", "WRITE", "SETTL"],
                      ["LOG", "DBNCE", "NVRAM"])


def layout_conf_p2(buf):
    """Config page 2: REFR/REBOOT."""
    _layout_conf_page(buf, "CONFIG 2/2",
                      ["REFR", "REBOOT"],
                      [])


# ---------------------------------------------------------------------------
# Screen layout registry
# ---------------------------------------------------------------------------

def layout_boot(buf):
    """Boot screen: logo placeholder, title, copyright, empty progress bar, BOOTING."""
    # Row 1: fox logo placeholder (centered)
    logo = "[FOX LOGO]"
    buf.put_text((COLS - len(logo)) // 2, 1, logo)
    # Row 2: title
    title = "YIP-BOI OS V1.0"
    buf.put_text((COLS - len(title)) // 2, 2, title)
    # Row 3: copyright
    copy = "(C) FOXIPSO 2026"
    buf.put_text((COLS - len(copy)) // 2, 3, copy)
    # Row 5: empty progress bar (20 wide, centered)
    bar_width = 20
    bar_col = (COLS - bar_width) // 2
    for c in range(bar_width):
        buf.put_glyph(bar_col + c, 5, G_SHADE1)
    # Row 6: BOOTING label
    label = "BOOTING"
    buf.put_text((COLS - len(label)) // 2, 6, label)


SCREEN_LAYOUTS = {
    0: ("HOME", layout_home),
    1: ("STATS", layout_stats),
    2: ("STAY", layout_stay),
    3: ("NET", layout_net),
    4: ("HEART", layout_heart),
    # 5: BOOT — custom screen, preserved from existing atlas (do NOT regenerate)
    6: ("CONF1", layout_conf_p1),
    7: ("CONF2", layout_conf_p2),
    8: ("VRCX", layout_vrcx),
    9: ("WORLDS", layout_vrcx_worlds),
    10: ("WORLD", layout_vrcx_world_detail),
    # Future:
    # 11: ("FEED", layout_vrcx_feed),
    # 11: ("STATUS", layout_vrcx_status),
    # 12: ("NOTIF", layout_vrcx_notif),
}


# ---------------------------------------------------------------------------
# Renderer — converts a MacroScreenBuffer into a 512x512 image
# ---------------------------------------------------------------------------

def render_buffer_to_image(buf, rom_glyphs):
    """Render a 40x8 grid of glyph indices into a 512x512 grayscale image.

    Each cell in the grid maps to a region of the output image.
    ROM glyphs are 8x16 pixels; they're scaled up to fill the cell region
    using nearest-neighbor (point) sampling to preserve pixel art.

    The output is a luminance mask: 255=foreground, 0=background.
    """
    img = Image.new("L", (MACRO_CELL_W, MACRO_CELL_H), 0)

    # Cell dimensions in the output image
    cell_w = MACRO_CELL_W / COLS      # 512/40 = 12.8
    cell_h = MACRO_CELL_H / ROWS      # 512/8  = 64.0

    for row in range(ROWS):
        for col in range(COLS):
            glyph_idx = buf.grid[row][col]
            if glyph_idx == 0:
                continue  # empty/transparent, skip

            # Determine if this is an inverted glyph
            inverted = False
            source_idx = glyph_idx
            if glyph_idx >= 160:
                inverted = True
                source_idx = glyph_idx - INVERT_OFFSET

            glyph_data = rom_glyphs.get(source_idx)
            if glyph_data is None:
                continue

            # Output region for this cell
            x0 = int(col * cell_w)
            x1 = int((col + 1) * cell_w)
            y0 = int(row * cell_h)
            y1 = int((row + 1) * cell_h)

            # Scale the 8x16 glyph into the cell region
            for py in range(y0, y1):
                # Map output pixel y to glyph row (0-15)
                gy = int((py - y0) / (y1 - y0) * CELL_H)
                gy = min(gy, CELL_H - 1)
                byte_val = glyph_data[gy]

                for px in range(x0, x1):
                    # Map output pixel x to glyph column (0-7)
                    gx = int((px - x0) / (x1 - x0) * CELL_W)
                    gx = min(gx, CELL_W - 1)

                    bit_set = bool(byte_val & (0x80 >> gx))
                    if inverted:
                        # Inverted: bg=white, fg=black
                        img.putpixel((px, py), 0 if bit_set else 255)
                    else:
                        # Normal: fg=white, bg=black
                        if bit_set:
                            img.putpixel((px, py), 255)

    return img


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate macro glyph atlas for Williams Tube PDA")
    parser.add_argument("--output", "-o", default="WilliamsTube_MacroAtlas.png",
                        help="Output PNG filename (default: WilliamsTube_MacroAtlas.png)")
    parser.add_argument("--no-copy", action="store_true",
                        help="Don't auto-copy to Unity project textures directory")
    parser.add_argument("--preview", action="store_true",
                        help="Also save individual screen previews as separate PNGs")
    args = parser.parse_args()

    output_path = os.path.join(SCRIPT_DIR, args.output)

    print("Loading ROM glyphs...")
    rom_glyphs = build_rom_glyphs()
    print(f"  Loaded {len(rom_glyphs)} glyphs")

    atlas = Image.new("L", (ATLAS_W, ATLAS_H), 0)

    # Paste custom boot screen at slot 5 (hand-made, not generated)
    boot_path = os.path.join(SCRIPT_DIR, "boot_screen.png")
    if os.path.exists(boot_path):
        boot_img = Image.open(boot_path).convert("L")
        boot_x = (5 % MACRO_GRID_COLS) * MACRO_CELL_W
        boot_y = (5 // MACRO_GRID_COLS) * MACRO_CELL_H
        atlas.paste(boot_img, (boot_x, boot_y))
        print(f"\n  [5] BOOT (custom) → grid ({5 % MACRO_GRID_COLS}, {5 // MACRO_GRID_COLS}), "
              f"offset ({boot_x}, {boot_y}) from {boot_path}")
    else:
        print(f"\n  WARNING: Custom boot screen not found: {boot_path}")

    print(f"\nAtlas: {ATLAS_W}x{ATLAS_H} ({MACRO_GRID_COLS}x{MACRO_GRID_ROWS} grid, "
          f"{MACRO_CELL_W}x{MACRO_CELL_H} per cell)")

    for idx, (name, layout_fn) in sorted(SCREEN_LAYOUTS.items()):
        print(f"\n  [{idx}] {name}...")
        buf = MacroScreenBuffer()
        layout_fn(buf)

        # Render to image
        cell_img = render_buffer_to_image(buf, rom_glyphs)

        # Place in atlas grid
        grid_col = idx % MACRO_GRID_COLS
        grid_row = idx // MACRO_GRID_COLS
        x0 = grid_col * MACRO_CELL_W
        y0 = grid_row * MACRO_CELL_H
        atlas.paste(cell_img, (x0, y0))
        print(f"       → grid ({grid_col}, {grid_row}), offset ({x0}, {y0})")

        # Optional preview
        if args.preview:
            preview_path = os.path.join(SCRIPT_DIR, f"macro_preview_{name.lower()}.png")
            cell_img.save(preview_path)
            print(f"       → preview: {preview_path}")

        # Print ASCII dump for verification
        print(f"       Buffer dump:")
        for r in range(ROWS):
            line = ""
            for c in range(COLS):
                g = buf.grid[r][c]
                if g == 0:
                    line += " "
                elif 32 <= g <= 126:
                    line += chr(g)
                elif g >= 160 and (g - 128) >= 32 and (g - 128) <= 126:
                    line += chr(g - 128)  # show inverted as normal for debug
                else:
                    line += "\u2588"  # block for non-ASCII glyphs

            print(f"       {r}|{line}|")

    atlas.save(output_path)
    print(f"\nSaved: {output_path}")
    print(f"  Size: {ATLAS_W}x{ATLAS_H}")
    print(f"  Grid: {MACRO_GRID_COLS}x{MACRO_GRID_ROWS} = {MACRO_GRID_COLS * MACRO_GRID_ROWS} slots")
    print(f"  Screens rendered: {len(SCREEN_LAYOUTS)}")
    print(f"  Slots remaining: {MACRO_GRID_COLS * MACRO_GRID_ROWS - len(SCREEN_LAYOUTS)}")

    # Auto-copy to Unity project
    if not args.no_copy and os.path.isdir(COPY_DEST_DIR):
        dest = os.path.join(COPY_DEST_DIR, args.output)
        shutil.copy2(output_path, dest)
        print(f"\nCopied to Unity: {dest}")
    elif not args.no_copy:
        print(f"\nUnity textures dir not found, skipping auto-copy: {COPY_DEST_DIR}")


if __name__ == "__main__":
    main()
