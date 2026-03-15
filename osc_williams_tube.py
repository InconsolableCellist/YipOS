#!/usr/bin/env python3
"""
Williams Tube OSC Controller

Virtual framebuffer that drives the Williams Tube display on a VRChat avatar
via OSC. Supports text mode (character buffer) and image mode (bitmap display).

Dual-mode system with normalized cursors (0.0–1.0):
    WT_CursorX  (float 0–1) - horizontal position, normalized
    WT_CursorY  (float 0–1) - vertical position, normalized
    WT_CharLo   (float 0–255) - glyph index low byte
    WT_CharHi   (float 0–255) - glyph index high byte
    WT_Mode     (float 0 or 1, synced as bool) - 0=image (4×4 block atlas), 1=text (font atlas)

Modes:
    test     - Static text test pattern with rolling refresh
    demo     - Typewriter animation
    keyboard - Interactive typing
    image    - Display a monochrome bitmap image using block encoding
    text     - Display text using the font atlas

Usage:
    pip install python-osc Pillow
    python osc_williams_tube.py --mode test --cols 80 --rows 4
    python osc_williams_tube.py --mode image path/to/image.png --width 128 --height 128
    python osc_williams_tube.py --mode text --cols 80 --rows 8 --message "Hello, world!"
"""

import argparse
import sys
import time
import threading

try:
    from pythonosc import udp_client
except ImportError:
    print("python-osc is required: pip install python-osc")
    sys.exit(1)

COLS = 80
ROWS = 4
PARAM_PREFIX = "/avatar/parameters/"

# Settling delay between cursor move and char write (seconds)
SETTLE_DELAY = 0.04
# Delay between successive writes (seconds) — ~15 updates/sec VRC limit
WRITE_DELAY = 0.07


class WilliamsTubeBuffer:
    """Virtual framebuffer for the Williams Tube display.

    Cursors are normalized 0.0–1.0 so the animator can address any grid
    resolution.  The buffer maps grid positions (col, row) to normalized
    floats:  x_norm = col / (cols - 1),  y_norm = row / (rows - 1).
    """

    def __init__(self, client, cols=COLS, rows=ROWS, split_char=False, mode=0):
        self.client = client
        self.cols = cols
        self.rows = rows
        self.split_char = split_char  # 16-bit mode: send WT_CharLo + WT_CharHi
        self.mode = mode  # 0 = image (block atlas), 1 = text (font atlas)
        # Buffer: 2D array of glyph indices
        self.buffer = [[0] * cols for _ in range(rows)]
        # Track dirty positions for efficient refresh
        self.dirty = set()
        # Current hardware cursor position (normalized floats)
        self.hw_cursor_x = -1.0
        self.hw_cursor_y = -1.0
        self.hw_mode = -1  # last sent WT_Mode
        self.lock = threading.Lock()

    def set_cell(self, x, y, value):
        """Set a cell in the buffer and mark dirty."""
        if 0 <= x < self.cols and 0 <= y < self.rows:
            with self.lock:
                if self.buffer[y][x] != value:
                    self.buffer[y][x] = value
                    self.dirty.add((x, y))

    def set_char(self, x, y, char_value):
        """Set a character in the buffer (for text mode)."""
        self.set_cell(x, y, char_value)

    def write_string(self, x, y, text):
        """Write a string starting at (x, y), wrapping to next row."""
        for ch in text:
            if x >= self.cols:
                x = 0
                y += 1
            if y >= self.rows:
                break
            self.set_char(x, y, ord(ch) if 32 <= ord(ch) <= 126 else 32)
            x += 1

    def clear(self, value=0):
        """Clear the entire buffer."""
        with self.lock:
            for y in range(self.rows):
                for x in range(self.cols):
                    self.buffer[y][x] = value
                    self.dirty.add((x, y))

    def _send_param(self, name, value):
        """Send an OSC parameter to VRChat."""
        self.client.send_message(f"{PARAM_PREFIX}{name}", value)

    def _normalize_x(self, x):
        """Convert grid column to normalized 0–1 cursor position.

        Maps to cell center: (col + 0.5) / total_cols.
        col 0 → 0.5/cols (half cell from left edge)
        last col → (cols-0.5)/cols (half cell from right edge)
        """
        return (x + 0.5) / self.cols

    def _normalize_y(self, y):
        """Convert grid row to normalized 0–1 cursor position.

        Maps to cell center: (row + 0.5) / total_rows.
        row 0 → 0.5/rows (half cell from top edge)
        last row → (rows-0.5)/rows (half cell from bottom edge)
        """
        return (y + 0.5) / self.rows

    def _move_cursor(self, x, y):
        """Move the hardware cursor, sending normalized OSC floats."""
        nx = self._normalize_x(x)
        ny = self._normalize_y(y)
        if nx != self.hw_cursor_x:
            self._send_param("WT_CursorX", float(nx))
            self.hw_cursor_x = nx
        if ny != self.hw_cursor_y:
            self._send_param("WT_CursorY", float(ny))
            self.hw_cursor_y = ny

    def _send_mode(self):
        """Send WT_Mode if it changed.

        WT_Mode is a Bool: False=image, True=text.
        """
        if self.mode != self.hw_mode:
            self._send_param("WT_Mode", bool(self.mode))
            self.hw_mode = self.mode

    def _write_cell_at(self, x, y, value):
        """Write a single cell: move cursor, set mode, then write value.

        Send all params as fast as possible to minimize ghosting.
        """
        self._send_mode()
        self._move_cursor(x, y)
        if self.split_char:
            lo = value & 0xFF
            hi = (value >> 8) & 0xFF
            self._send_param("WT_CharLo", float(lo))
            self._send_param("WT_CharHi", float(hi))
        else:
            self._send_param("WT_CharLo", float(value & 0xFF))
            self._send_param("WT_CharHi", float((value >> 8) & 0xFF))
        time.sleep(WRITE_DELAY)

    def flush_dirty(self):
        """Send all dirty positions to the avatar."""
        with self.lock:
            dirty_list = list(self.dirty)
            self.dirty.clear()

        for x, y in dirty_list:
            value = self.buffer[y][x]
            self._write_cell_at(x, y, value)

    def flush_all(self, skip_values=None, progressive=False, randomize=False):
        """Write every cell in order (full refresh).

        skip_values: set of cell values to skip (e.g. {0} to skip all-black
        cells when the RT is pre-cleared to black).
        progressive: if True, write in coarse-to-fine order (interlaced).
        randomize: if True, write cells in random order.
        """
        if randomize:
            import random
            order = [(x, y) for y in range(self.rows) for x in range(self.cols)]
            random.shuffle(order)
        elif progressive:
            order = self._progressive_order()
        else:
            order = [(x, y) for y in range(self.rows) for x in range(self.cols)]

        total = len(order)
        skipped = 0
        written = 0
        for x, y in order:
            value = self.buffer[y][x]
            if skip_values and value in skip_values:
                skipped += 1
                continue
            written += 1
            self._write_cell_at(x, y, value)
        if skipped:
            print(f"  Skipped {skipped}/{total} cells ({100*skipped/total:.0f}%)")

    def _progressive_order(self):
        """Generate cell coordinates in coarse-to-fine interlaced order.

        Pass 0: stride=max_dim/2 (e.g. every 16th cell for 32×32 → 2×2 grid)
        Pass 1: stride/2, filling gaps
        ...until stride=1 (every cell)

        Within each pass, cells are scanned left-to-right, top-to-bottom.
        Already-visited cells are skipped in later passes.
        """
        import math
        max_dim = max(self.cols, self.rows)
        # Start with largest power-of-2 stride that fits
        stride = 1
        while stride * 2 < max_dim:
            stride *= 2

        visited = set()
        order = []

        while stride >= 1:
            for y in range(0, self.rows, stride):
                for x in range(0, self.cols, stride):
                    if (x, y) not in visited:
                        visited.add((x, y))
                        order.append((x, y))
            stride //= 2

        return order

    def rolling_refresh(self):
        """Continuously cycle through all positions, refreshing them."""
        while True:
            self.flush_dirty()
            for y in range(self.rows):
                for x in range(self.cols):
                    if self.dirty:
                        self.flush_dirty()
                    value = self.buffer[y][x]
                    self._write_cell_at(x, y, value)

    def dump(self):
        """Print the current buffer to stdout."""
        print("+" + "-" * self.cols + "+")
        for row in self.buffer:
            line = "".join(chr(c) if 32 <= c <= 126 else "." for c in row)
            print("|" + line + "|")
        print("+" + "-" * self.cols + "+")


# ---------- VQ (vector quantization) encoding ----------

def load_vq_codebook(codebook_path):
    """Load a VQ codebook from .npy file."""
    import numpy as np
    codebook = np.load(codebook_path)
    print(f"Loaded VQ codebook: {codebook.shape[0]} codes, {codebook.shape[1]}-dim")
    return codebook


def _find_nearest_code(block, codebook_scaled, code_sq):
    """Find nearest codebook entry for a single block via L2 distance."""
    import numpy as np
    block = block.reshape(1, -1)
    block_sq = (block ** 2).sum()
    dists = block_sq - 2 * block @ codebook_scaled.T + code_sq
    return int(dists.argmin())


def image_to_vq(image_path, codebook, target_w=128, target_h=128, block_size=8,
                dither=True, invert=False):
    """Convert an image to VQ-encoded cells using a codebook.

    Uses block-level error diffusion: each greyscale block is matched against
    binary codebook entries. The quantization error (difference between the
    greyscale block and chosen binary pattern) is diffused to neighboring
    blocks using Floyd-Steinberg weights, producing smooth gradients.

    Returns (cells, grid_cols, grid_rows) where cells[row][col] = codebook index (0-255).
    """
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        print("Pillow and numpy required: pip install Pillow numpy")
        sys.exit(1)

    img = Image.open(image_path)
    img.thumbnail((target_w, target_h), Image.Resampling.LANCZOS)

    padded = Image.new("L", (target_w, target_h), 0)
    offset_x = (target_w - img.width) // 2
    offset_y = (target_h - img.height) // 2
    padded.paste(img.convert("L"), (offset_x, offset_y))
    img = padded

    if invert:
        img = Image.eval(img, lambda x: 255 - x)

    pixels = np.array(img, dtype=np.float32)

    grid_cols = target_w // block_size
    grid_rows = target_h // block_size

    # Extract all blocks into a mutable grid of greyscale vectors
    block_grid = np.zeros((grid_rows, grid_cols, block_size * block_size), dtype=np.float32)
    for by_idx in range(grid_rows):
        for bx_idx in range(grid_cols):
            bx = bx_idx * block_size
            by = by_idx * block_size
            block_grid[by_idx, bx_idx] = pixels[by:by + block_size, bx:bx + block_size].flatten()

    # Precompute codebook scaled to 0-255
    codebook_scaled = codebook.astype(np.float32) * 255.0
    code_sq = (codebook_scaled ** 2).sum(axis=1, keepdims=True).T

    # Process blocks in scan order with Floyd-Steinberg error diffusion
    cells = []
    for by_idx in range(grid_rows):
        row = []
        for bx_idx in range(grid_cols):
            block = np.clip(block_grid[by_idx, bx_idx], 0, 255)
            best_idx = _find_nearest_code(block, codebook_scaled, code_sq)
            row.append(best_idx)

            # Quantization error: what we wanted minus what we got
            error = block - codebook_scaled[best_idx]

            # Floyd-Steinberg diffusion to neighboring blocks
            #   * 7/16 → right
            #   * 3/16 → below-left
            #   * 5/16 → below
            #   * 1/16 → below-right
            if bx_idx + 1 < grid_cols:
                block_grid[by_idx, bx_idx + 1] += error * (7.0 / 16.0)
            if by_idx + 1 < grid_rows:
                if bx_idx - 1 >= 0:
                    block_grid[by_idx + 1, bx_idx - 1] += error * (3.0 / 16.0)
                block_grid[by_idx + 1, bx_idx] += error * (5.0 / 16.0)
                if bx_idx + 1 < grid_cols:
                    block_grid[by_idx + 1, bx_idx + 1] += error * (1.0 / 16.0)
        cells.append(row)

    return cells, grid_cols, grid_rows


def preview_vq(cells, grid_cols, grid_rows, codebook, block_size=8):
    """Preview VQ-encoded image in terminal."""
    img_w = grid_cols * block_size
    img_h = grid_rows * block_size
    pixels = [[False] * img_w for _ in range(img_h)]

    for by_idx, row in enumerate(cells):
        for bx_idx, code_idx in enumerate(row):
            pattern = codebook[code_idx]
            for bit_idx in range(len(pattern)):
                r = bit_idx // block_size
                c = bit_idx % block_size
                py = by_idx * block_size + r
                px = bx_idx * block_size + c
                if py < img_h and px < img_w:
                    pixels[py][px] = bool(pattern[bit_idx] > 0.5)

    for y in range(0, img_h, 2):
        line = ""
        for x in range(img_w):
            top = pixels[y][x] if y < img_h else False
            bot = pixels[y + 1][x] if y + 1 < img_h else False
            if top and bot:
                line += "\u2588"
            elif top:
                line += "\u2580"
            elif bot:
                line += "\u2584"
            else:
                line += " "
        print(line)


# ---------- Braille bitmap encoding ----------

def encode_block(pixels, bx, by, img_w, img_h, block_w, block_h):
    """Encode a block of pixels as a bit pattern index.

    Bit mapping: row-major, left-to-right, top-to-bottom.
    Bit 0 = top-left, last bit = bottom-right.
    """
    pattern = 0
    bit = 0
    for row in range(block_h):
        for col in range(block_w):
            px = bx + col
            py = by + row
            if px < img_w and py < img_h:
                if pixels[py * img_w + px]:
                    pattern |= (1 << bit)
            bit += 1
    return pattern


def image_to_blocks(image_path, target_w=128, target_h=128, block_w=4, block_h=4,
                    dither=True, invert=False):
    """Convert an image to a grid of block-encoded cells.

    Returns (cells, grid_cols, grid_rows) where cells[row][col] = pattern index.
    """
    try:
        from PIL import Image
    except ImportError:
        print("Pillow is required for image mode: pip install Pillow")
        sys.exit(1)

    img = Image.open(image_path)

    # Resize to target, maintaining aspect ratio with padding
    img.thumbnail((target_w, target_h), Image.Resampling.LANCZOS)

    # Pad to exact target size (center the image)
    padded = Image.new("L", (target_w, target_h), 0)
    offset_x = (target_w - img.width) // 2
    offset_y = (target_h - img.height) // 2
    padded.paste(img.convert("L"), (offset_x, offset_y))
    img = padded

    if invert:
        img = Image.eval(img, lambda x: 255 - x)

    # Dither to 1-bit
    if dither:
        img = img.convert("1")  # Floyd-Steinberg dithering
    else:
        img = img.point(lambda x: 255 if x > 127 else 0, mode="1")

    # Get pixel data as flat list of 0/1
    pixels = list(img.getdata())
    pixels = [1 if p else 0 for p in pixels]

    # Encode into blocks
    grid_cols = (target_w + block_w - 1) // block_w
    grid_rows = (target_h + block_h - 1) // block_h

    cells = []
    for by_idx in range(grid_rows):
        row = []
        for bx_idx in range(grid_cols):
            bx = bx_idx * block_w
            by = by_idx * block_h
            pattern = encode_block(pixels, bx, by, target_w, target_h, block_w, block_h)
            row.append(pattern)
        cells.append(row)

    return cells, grid_cols, grid_rows


def preview_blocks(cells, grid_cols, grid_rows, block_w, block_h):
    """Preview the bitmap in terminal using block characters."""
    # Reconstruct pixel grid from blocks
    img_w = grid_cols * block_w
    img_h = grid_rows * block_h
    pixels = [[False] * img_w for _ in range(img_h)]

    for by_idx, row in enumerate(cells):
        for bx_idx, pattern in enumerate(row):
            bit = 0
            for r in range(block_h):
                for c in range(block_w):
                    py = by_idx * block_h + r
                    px = bx_idx * block_w + c
                    if py < img_h and px < img_w:
                        pixels[py][px] = bool(pattern & (1 << bit))
                    bit += 1

    # Print using half-block characters (2 rows per line)
    for y in range(0, img_h, 2):
        line = ""
        for x in range(img_w):
            top = pixels[y][x] if y < img_h else False
            bot = pixels[y + 1][x] if y + 1 < img_h else False
            if top and bot:
                line += "\u2588"  # full block
            elif top:
                line += "\u2580"  # upper half
            elif bot:
                line += "\u2584"  # lower half
            else:
                line += " "
        print(line)


# ---------- Modes ----------

def mode_clear(buf, **kwargs):
    """Instantly clear the RT by scaling the write head to full view.

    In dual mode, WT_Mode=2 scales to full view; WT_Clear=1 sets _ClearMode.
    In text-only mode, WT_Clear alone handles both scale and _ClearMode.
    """
    print("Clearing RT (instant)...")

    # Center the cursor so the full-view quad covers the entire RT
    buf._send_param("WT_CursorX", 0.5)
    buf._send_param("WT_CursorY", 0.5)
    buf.hw_cursor_x = -1.0  # invalidate so next write re-sends
    buf.hw_cursor_y = -1.0
    # Activate clear (Bool: True scales to full view + _ClearMode=1)
    buf._send_param("WT_Clear", True)
    # Wait for animator to fully interpolate
    time.sleep(0.3)

    # Restore
    buf._send_param("WT_Clear", False)
    time.sleep(0.3)

    print("Done! RT cleared.")


def mode_test(buf):
    """Write a static text test pattern and refresh continuously."""
    buf.mode = 1  # text atlas
    buf.write_string(0, 0, "WILLIAMS TUBE TEST PATTERN")
    buf.write_string(0, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789")
    buf.write_string(0, 2, "The quick brown fox jumps over the lazy dog.")
    buf.write_string(0, 3, "STATUS: OK")
    buf.dump()
    print("\nFlushing initial content...")
    buf.flush_dirty()
    print("Starting rolling refresh (Ctrl+C to stop)...")
    buf.rolling_refresh()


def mode_stats(buf, interval=5.0, minimal=False):
    """Display live system stats with delta updates.

    Only repaints cells that changed since the last frame.
    minimal=True shows only CPU/MEM, TX, and timestamp (3 lines).
    """
    import psutil
    import datetime

    buf.mode = 1  # text atlas

    net_prev = psutil.net_io_counters()
    time_prev = time.time()
    prev_lines = []

    def fmt_bytes(b):
        if b >= 1 << 30:
            return f"{b / (1 << 30):.1f}G"
        if b >= 1 << 20:
            return f"{b / (1 << 20):.0f}M"
        if b >= 1 << 10:
            return f"{b / (1 << 10):.0f}K"
        return f"{b}B"

    def fmt_rate(bps):
        if bps >= 1 << 20:
            return f"{bps / (1 << 20):.1f}MB/s"
        if bps >= 1 << 10:
            return f"{bps / (1 << 10):.0f}KB/s"
        return f"{bps:.0f}B/s"

    print(f"Stats mode: {buf.cols}x{buf.rows}, delta updates")
    print("Ctrl+C to stop\n")

    # Set text mode before anything else, then clear
    buf._send_mode()
    time.sleep(WRITE_DELAY)
    mode_clear(buf)

    while True:
        cpu = psutil.cpu_percent(interval=0.5)
        mem = psutil.virtual_memory()
        disk = psutil.disk_usage("/")
        net = psutil.net_io_counters()
        now = time.time()
        dt = now - time_prev
        if dt > 0:
            tx_rate = (net.bytes_sent - net_prev.bytes_sent) / dt
            rx_rate = (net.bytes_recv - net_prev.bytes_recv) / dt
        else:
            tx_rate = rx_rate = 0
        net_prev = net
        time_prev = now

        uptime_s = now - psutil.boot_time()
        uptime = str(datetime.timedelta(seconds=int(uptime_s)))
        timestamp = datetime.datetime.now().strftime("%H:%M:%S")

        if minimal:
            lines = [
                f"CPU {cpu:4.1f}% M{mem.percent:.0f}%",
                f"TX{fmt_rate(tx_rate)}",
                f"{timestamp}",
            ]
        else:
            lines = [
                f"CPU  {cpu:5.1f}%",
                f"MEM  {mem.percent:.0f}% {fmt_bytes(mem.used)}",
                f"DISK {disk.percent:.0f}% {fmt_bytes(disk.used)}",
                f"TX {fmt_rate(tx_rate)}",
                f"RX {fmt_rate(rx_rate)}",
                f"UP {uptime}",
                f"",
                f"{timestamp}",
            ]

        # Pad content lines to full width; only include unused rows
        # after the first pass (when there's stale data to erase)
        padded = []
        for i in range(buf.rows):
            if i < len(lines):
                padded.append(lines[i][:buf.cols].ljust(buf.cols))
            elif prev_lines:
                padded.append(" " * buf.cols)
            else:
                padded.append(None)  # skip — RT already blank

        # Diff against previous frame — only write changed cells
        changed = 0
        for row_idx, line in enumerate(padded):
            if line is None:
                continue
            prev = prev_lines[row_idx] if row_idx < len(prev_lines) else ""
            for col_idx, ch in enumerate(line):
                prev_ch = prev[col_idx] if col_idx < len(prev) else "\x00"
                if ch != prev_ch:
                    char_val = ord(ch) if 32 <= ord(ch) <= 126 else 32
                    buf._send_mode()
                    buf._move_cursor(col_idx, row_idx)
                    buf._send_param("WT_CharLo", float(char_val))
                    buf._send_param("WT_CharHi", float(0))
                    time.sleep(WRITE_DELAY)
                    changed += 1

        prev_lines = [l if l is not None else "" for l in padded]

        # Terminal preview
        print(f"\n--- {changed} cells updated ---")
        for line in padded[:len(lines)]:
            print(f"  {line.rstrip()}")

        time.sleep(interval)


def mode_text(buf, message=None, loop=False):
    """Display text using the font atlas (WT_Mode=1)."""
    buf.mode = 1  # text atlas
    buf._send_mode()
    time.sleep(0.3)
    if message:
        lines = message.split("\\n")
    else:
        lines = [
            "WILLIAMS TUBE DISPLAY",
            "> TEXT MODE ACTIVE",
            f"> {buf.cols}x{buf.rows} grid",
            "> Ready.",
        ]

    for row_idx, line in enumerate(lines):
        if row_idx >= buf.rows:
            break
        buf.write_string(0, row_idx, line[:buf.cols])

    buf.dump()
    print(f"\nPainting {buf.cols}x{buf.rows} text cells...")
    skip_values = {0, 32}  # skip null and space (both blank on text atlas)
    buf.flush_all(skip_values=skip_values)

    if loop:
        print("Starting rolling refresh...")
        buf.rolling_refresh()
    else:
        print("Done! Text painted.")


def mode_demo(buf):
    """Animated demo: typewriter effect, then rolling refresh."""
    buf.mode = 1  # text atlas
    lines = [
        "WILLIAMS TUBE v0.1",
        "> INIT DISPLAY...",
        "> ATLAS LOADED",
        "> LOOP ACTIVE",
    ]
    # Truncate lines to buffer width
    for row_idx, line in enumerate(lines):
        if row_idx >= buf.rows:
            break
        for col_idx, ch in enumerate(line[:buf.cols]):
            buf.set_char(col_idx, row_idx, ord(ch))
            buf.flush_dirty()
            sys.stdout.write(f"\rWriting row {row_idx}, col {col_idx}: '{ch}'  ")
            sys.stdout.flush()

    print("\n\nInitial write complete. Starting rolling refresh...")
    buf.dump()
    buf.rolling_refresh()


def mode_keyboard(buf):
    """Interactive keyboard input mode."""
    buf.mode = 1  # text atlas
    cursor_x, cursor_y = 0, 0
    print("Williams Tube Keyboard Mode")
    print("Type characters to write. Enter = new line. Ctrl+C = quit.")
    print(f"Display: {buf.cols}x{buf.rows}")
    print()

    refresh_thread = threading.Thread(target=buf.rolling_refresh, daemon=True)
    refresh_thread.start()

    try:
        while True:
            ch = sys.stdin.read(1)
            if not ch:
                break
            if ch == "\n":
                cursor_x = 0
                cursor_y = (cursor_y + 1) % buf.rows
                if cursor_y == 0:
                    buf.clear(32)
            elif ch == "\x7f" or ch == "\x08":  # Backspace
                cursor_x = max(0, cursor_x - 1)
                buf.set_char(cursor_x, cursor_y, 32)
            elif 32 <= ord(ch) <= 126:
                buf.set_char(cursor_x, cursor_y, ord(ch))
                cursor_x += 1
                if cursor_x >= buf.cols:
                    cursor_x = 0
                    cursor_y = (cursor_y + 1) % buf.rows
            buf.dump()
    except KeyboardInterrupt:
        pass


def mode_image(buf, image_path, target_w, target_h, block_w=2, block_h=4,
               dither=True, invert=False, loop=False,
               vq_codebook=None, vq_block_size=8, progressive=False,
               randomize=False):
    """Display an image as a block-encoded or VQ-encoded bitmap."""
    buf.mode = 0  # image (block atlas)
    print(f"Loading image: {image_path}")

    if vq_codebook is not None:
        cells, grid_cols, grid_rows = image_to_vq(
            image_path, vq_codebook, target_w, target_h,
            block_size=vq_block_size, dither=dither, invert=invert
        )
        print(f"VQ: {target_w}x{target_h} pixels → {grid_cols}x{grid_rows} cells ({vq_block_size}x{vq_block_size} blocks, 256 codes)")
    else:
        cells, grid_cols, grid_rows = image_to_blocks(
            image_path, target_w, target_h, block_w=block_w, block_h=block_h,
            dither=dither, invert=invert
        )
        print(f"Bitmap: {target_w}x{target_h} pixels → {grid_cols}x{grid_rows} cells ({block_w}x{block_h} blocks)")

    print(f"Buffer: {buf.cols}x{buf.rows} cells")

    if grid_cols != buf.cols or grid_rows != buf.rows:
        print(f"WARNING: Image grid ({grid_cols}x{grid_rows}) != buffer ({buf.cols}x{buf.rows})")
        print(f"  Use --cols {grid_cols} --rows {grid_rows} to match")

    # Preview in terminal
    print("\nTerminal preview:")
    if vq_codebook is not None:
        preview_vq(cells, grid_cols, grid_rows, vq_codebook, vq_block_size)
    else:
        preview_blocks(cells, grid_cols, grid_rows, block_w, block_h)
    print()

    # Load into buffer
    for y in range(min(grid_rows, buf.rows)):
        for x in range(min(grid_cols, buf.cols)):
            buf.set_cell(x, y, cells[y][x])

    # Skip cells matching the RT clear color (black = pattern 0).
    # Only skip black — the RT is pre-cleared to black, so all-black
    # cells are already correct. All-white cells must still be written.
    skip_values = {0}

    total = buf.cols * buf.rows
    write_count = sum(1 for y in range(buf.rows) for x in range(buf.cols)
                      if buf.buffer[y][x] not in skip_values)
    est_local = write_count * WRITE_DELAY
    saved = total - write_count
    print(f"Painting {write_count}/{total} cells (~{est_local:.0f}s), skipping {saved} solid cells ({100*saved/total:.0f}%)")
    print("Ctrl+C to stop\n")

    if progressive:
        print("  Progressive mode: coarse-to-fine ordering")
    if randomize:
        print("  Random mode: shuffled cell order")
    buf.flush_all(skip_values=skip_values, progressive=progressive, randomize=randomize)

    if loop:
        print("Starting rolling refresh...")
        buf.rolling_refresh()
    else:
        print("Done! Image painted.")


def main():
    parser = argparse.ArgumentParser(description="Williams Tube OSC Controller")
    parser.add_argument("--mode", choices=["test", "keyboard", "demo", "image", "text", "clear", "stats", "stats_min"],
                        default="test", help="Operating mode (default: test)")
    parser.add_argument("--ip", default="127.0.0.1", help="VRChat OSC IP")
    parser.add_argument("--port", type=int, default=9000, help="VRChat OSC port")
    parser.add_argument("--cols", type=int, default=None, help="Display columns")
    parser.add_argument("--rows", type=int, default=None, help="Display rows")

    # Image mode options
    parser.add_argument("image_path", nargs="?", help="Image file for --mode image")
    parser.add_argument("--width", type=int, default=128, help="Target bitmap width (default: 128)")
    parser.add_argument("--height", type=int, default=128, help="Target bitmap height (default: 128)")
    parser.add_argument("--block-size", default="4x4",
                        help="Block size WxH (default: 4x4). Use 2x4 for braille mode")
    parser.add_argument("--split-char", action="store_true",
                        help="Use 16-bit split char (WT_CharLo + WT_CharHi) for >256 patterns")
    parser.add_argument("--vq", metavar="CODEBOOK",
                        help="Use VQ encoding with .npy codebook (e.g. vq_codebook.npy)")
    parser.add_argument("--vq-block", type=int, default=8,
                        help="VQ block size (default: 8)")
    parser.add_argument("--progressive", action="store_true",
                        help="Write cells coarse-to-fine (interlaced) for faster preview")
    parser.add_argument("--random", action="store_true",
                        help="Write cells in random order")
    parser.add_argument("--no-dither", action="store_true", help="Disable Floyd-Steinberg dithering")
    parser.add_argument("--invert", action="store_true", help="Invert image (white bg → black bg)")
    parser.add_argument("--loop", action="store_true", help="Continuously refresh after painting")

    # Text mode options
    parser.add_argument("--message", "-m", help="Text message for --mode text (use \\\\n for newlines)")

    # Clear mode options
    parser.add_argument("--color", choices=["black", "white"], default="black",
                        help="Clear color for --mode clear (default: black)")

    # Timing
    parser.add_argument("--settle", type=float, default=SETTLE_DELAY,
                        help=f"Settle delay in seconds (default: {SETTLE_DELAY})")
    parser.add_argument("--write-delay", type=float, default=WRITE_DELAY,
                        help=f"Write delay in seconds (default: {WRITE_DELAY})")

    args = parser.parse_args()

    # Apply timing overrides — update the module globals so all functions see them
    import sys
    sys.modules[__name__].SETTLE_DELAY = args.settle
    sys.modules[__name__].WRITE_DELAY = args.write_delay

    # Load VQ codebook if specified
    vq_codebook = None
    if args.vq:
        vq_codebook = load_vq_codebook(args.vq)

    # Parse block size
    try:
        block_w, block_h = (int(x) for x in args.block_size.split("x"))
    except ValueError:
        parser.error(f"Invalid --block-size: {args.block_size} (use WxH, e.g. 4x4)")

    # VQ mode overrides: 8-bit single CharValue, no split needed
    if vq_codebook is not None:
        block_w = args.vq_block
        block_h = args.vq_block
        args.split_char = False

    # Auto-enable split_char for blocks needing >8 bits
    bits_needed = block_w * block_h
    if bits_needed > 8 and not args.split_char and vq_codebook is None:
        print(f"Note: {block_w}x{block_h} blocks need {bits_needed} bits, enabling --split-char")
        args.split_char = True

    # Auto-detect cols/rows
    if args.mode == "image":
        if args.image_path is None:
            parser.error("image mode requires an image path")
        cols = args.cols or (args.width + block_w - 1) // block_w
        rows = args.rows or (args.height + block_h - 1) // block_h
    elif args.mode == "clear":
        # Clear scan-writes all cells, so needs the actual grid dimensions
        cols = args.cols or (args.width + block_w - 1) // block_w
        rows = args.rows or (args.height + block_h - 1) // block_h
    elif args.mode in ("text", "stats", "stats_min"):
        cols = args.cols or 80
        rows = args.rows or 8
    else:
        cols = args.cols or COLS
        rows = args.rows or ROWS

    print(f"Williams Tube OSC Controller")
    print(f"  Target: {args.ip}:{args.port}")
    print(f"  Display: {cols}x{rows} cells")
    print(f"  Mode: {args.mode}")
    if vq_codebook is not None:
        print(f"  VQ: {args.vq_block}x{args.vq_block} blocks, 256 codes (8-bit)")
    else:
        print(f"  Block size: {block_w}x{block_h} ({bits_needed} bits)")
        print(f"  Split char: {args.split_char}")
    print(f"  Timing: settle={SETTLE_DELAY}s, write={WRITE_DELAY}s")
    print()

    client = udp_client.SimpleUDPClient(args.ip, args.port)
    buf = WilliamsTubeBuffer(client, cols=cols, rows=rows, split_char=args.split_char)

    try:
        if args.mode == "clear":
            mode_clear(buf, color=args.color, randomize=args.random)
        elif args.mode == "test":
            mode_test(buf)
        elif args.mode == "demo":
            mode_demo(buf)
        elif args.mode == "keyboard":
            mode_keyboard(buf)
        elif args.mode == "text":
            mode_text(buf, message=args.message, loop=args.loop)
        elif args.mode == "image":
            mode_image(buf, args.image_path, args.width, args.height,
                       block_w=block_w, block_h=block_h,
                       dither=not args.no_dither, invert=args.invert,
                       loop=args.loop,
                       vq_codebook=vq_codebook, vq_block_size=args.vq_block,
                       progressive=args.progressive, randomize=args.random)
        elif args.mode == "stats":
            mode_stats(buf)
        elif args.mode == "stats_min":
            mode_stats(buf, minimal=True)
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
