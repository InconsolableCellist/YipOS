#!/usr/bin/env python3
"""
PDA Controller PoC — Williams Tube wrist CRT

Renders a home screen with labeled tiles on the Williams Tube display (40x8
text mode) and responds to VRChat touch/button input via OSC.

- Sends OSC on port 9000 (render to Williams Tube)
- Listens on port 9001 (receive VRChat CRT_Wrist_* params)
- Keyboard input for testing (type tile coords like "11", or "TL"/"ML")
- Touch grid: 5x3 tiles, tap toggles highlight (bracket indicator)
- TL button: redraw home screen
- ML button: clear screen

Usage:
    python3 pda_poc.py [--ip 127.0.0.1] [--send-port 9000] [--listen-port 9001]
"""

import argparse
import collections
import datetime
import queue
import select
import sys
import threading
import time

try:
    from pythonosc import udp_client
    from pythonosc.osc_server import ThreadingOSCUDPServer
    from pythonosc.dispatcher import Dispatcher
except ImportError:
    print("python-osc is required: pip install python-osc")
    sys.exit(1)

# --- Constants ---

COLS = 40
ROWS = 8
PARAM_PREFIX = "/avatar/parameters/"

# Defaults — overridden by CLI args
SETTLE_DELAY = 0.04
WRITE_DELAY = 0.07

TILE_COLS = 5
TILE_ROWS = 3
CHARS_PER_TILE = COLS // TILE_COLS  # 8
# Column centers for even spacing across 40 cols (contact grid alignment)
TILE_CENTERS = [(2 * i + 1) * (COLS // (TILE_COLS * 2)) for i in range(TILE_COLS)]
# → [4, 12, 20, 28, 36]

TILE_LABELS = [
    ["STATS", "NET", "TRACK", "SPVR", "CONFG"],
    ["VRCX", "HEART", "MAP", "-----", "-----"],
    ["-----", "-----", "-----", "-----", "-----"],
]

# --- PDA ROM Glyph Indices ---
# These match the atlas layout in generate_pda_rom.py / PDA_ARCHITECTURE.md

# Box-drawing (indices 0-11)
G_EMPTY = 0
G_HLINE = 1      # ─
G_VLINE = 2      # │
G_TL_CORNER = 3  # ┌
G_TR_CORNER = 4  # ┐
G_BL_CORNER = 5  # └
G_BR_CORNER = 6  # ┘
G_L_TEE = 7      # ├
G_R_TEE = 8      # ┤
G_T_TEE = 9      # ┬
G_B_TEE = 10     # ┴
G_CROSS = 11     # ┼

# Block elements (12-19)
G_SOLID = 12     # █
G_UPPER = 13     # ▀
G_LOWER = 14     # ▄
G_LEFT = 15      # ▌
G_RIGHT = 16     # ▐
G_SHADE1 = 17    # ░ 25%
G_SHADE2 = 18    # ▒ 50%
G_SHADE3 = 19    # ▓ 75%

# Symbols (20-31)
G_BULLET = 20    # ●
G_HEART = 21     # ♥
G_NOTE = 22      # ♪
G_DNOTE = 23     # ♫
G_UP = 24        # ↑
G_DOWN = 25      # ↓
G_RIGHT_A = 26   # →
G_LEFT_A = 27    # ←
G_GEAR = 28      # ☼
G_HOME = 29      # ⌂

# Custom PDA icons (128-143)
G_SIGNAL = 128
G_BATT_FULL = 129
G_BATT_HALF = 130
G_BATT_LOW = 131
G_BATT_EMPTY = 132
G_LOCK = 133
G_UNLOCK = 134
G_SETTINGS = 135
G_PLAY = 136
G_PAUSE = 137
G_SKIP_FWD = 138
G_SKIP_BACK = 139
G_WIFI = 140
G_CHECK = 141
G_XMARK = 142
G_TRACKER = 143

# Inversion offset: add to any ASCII char (32-127) to get inverted variant
INVERT_OFFSET = 128

# --- Screen Buffer (for terminal preview) ---


class ScreenBuffer:
    """In-memory 40x8 text buffer for terminal preview."""

    def __init__(self):
        self.grid = [[' '] * COLS for _ in range(ROWS)]

    def put(self, col, row, ch):
        if 0 <= col < COLS and 0 <= row < ROWS:
            self.grid[row][col] = ch

    def clear(self):
        self.grid = [[' '] * COLS for _ in range(ROWS)]

    def dump(self):
        """Print the buffer to terminal as a boxed display."""
        print("+" + "-" * COLS + "+")
        for row in self.grid:
            print("|" + "".join(row) + "|")
        print("+" + "-" * COLS + "+")


# --- OSC Display Writer ---


class PDADisplay:
    """Minimal Williams Tube text-mode writer for the PDA.

    Supports a buffered mode for background refresh: when buffered, writes
    are queued instead of sent immediately. The main loop drains the queue
    one write at a time, checking for input between each.
    """

    def __init__(self, client, screen, y_offset=0.0, y_scale=1.0, y_curve=1.0):
        self.client = client
        self.screen = screen
        self.y_offset = y_offset
        self.y_scale = y_scale
        self.y_curve = y_curve
        self.hw_cursor_x = -1.0
        self.hw_cursor_y = -1.0
        # Buffered write queue: list of (col, row, char_idx)
        self._write_queue = []
        self._buffered = False

    def send_param(self, name, value):
        self.client.send_message(f"{PARAM_PREFIX}{name}", value)

    def row_to_y(self, row):
        """Convert row index to normalized Y cursor position.

        Applies offset, scale, and curvature correction:
          t = (row + 0.5) / ROWS          # linear 0–1
          ny = y_offset + t^y_curve * y_scale

        y_curve > 1: pushes middle rows downward (spreads toward bottom)
        y_curve < 1: pushes middle rows upward (compresses toward bottom)
        y_curve = 1: linear (default)
        """
        t = (row + 0.5) / ROWS
        return self.y_offset + (t ** self.y_curve) * self.y_scale

    def move_cursor(self, col, row):
        nx = (col + 0.5) / COLS
        ny = self.row_to_y(row)
        if nx != self.hw_cursor_x:
            self.send_param("WT_CursorX", float(nx))
            self.hw_cursor_x = nx
        if ny != self.hw_cursor_y:
            self.send_param("WT_CursorY", float(ny))
            self.hw_cursor_y = ny

    def begin_buffered(self):
        """Start buffering writes instead of sending them immediately."""
        self._buffered = True
        self._write_queue.clear()

    def cancel_buffered(self):
        """Discard buffered writes and return to immediate mode."""
        self._buffered = False
        self._write_queue.clear()

    def is_buffered(self):
        """True if a buffered refresh is in progress."""
        return self._buffered and len(self._write_queue) > 0

    def buffered_remaining(self):
        """Number of writes remaining in the buffer."""
        return len(self._write_queue)

    def flush_one(self):
        """Send one buffered write. Returns True if more remain."""
        if not self._write_queue:
            self._buffered = False
            return False
        col, row, char_idx = self._write_queue.pop(0)
        self._send_write(col, row, char_idx)
        if not self._write_queue:
            self._buffered = False
            return False
        return True

    def _send_write(self, col, row, char_idx):
        """Actually send one character write over OSC."""
        self.move_cursor(col, row)
        self.send_param("WT_CharLo", float(char_idx & 0xFF))
        self.send_param("WT_CharHi", float((char_idx >> 8) & 0xFF))
        time.sleep(WRITE_DELAY)
        ch = chr(char_idx) if 32 <= char_idx <= 126 else ' '
        self.screen.put(col, round(row), ch)

    def write_char(self, col, row, char_idx):
        """Write a character. Row can be float for sub-row positioning.

        In buffered mode, queues the write instead of sending immediately.
        """
        if self._buffered:
            self._write_queue.append((col, row, char_idx))
        else:
            self._send_write(col, row, char_idx)

    def write_text(self, col, row, text, inverted=False):
        """Write text. Row can be float. Skips spaces if screen already blank.

        If inverted=True, adds INVERT_OFFSET to each character index to use
        the inverted ASCII glyphs (160-255) from the PDA ROM atlas.
        """
        buf_row = round(row)
        for i, ch in enumerate(text):
            if ch == ' ' and not inverted:
                if 0 <= col + i < COLS and 0 <= buf_row < ROWS:
                    if self.screen.grid[buf_row][col + i] == ' ':
                        continue
                self.write_char(col + i, row, 32)
                continue
            char_idx = ord(ch) if 32 <= ord(ch) <= 126 else 32
            if inverted:
                char_idx += INVERT_OFFSET
            self.write_char(col + i, row, char_idx)

    def write_glyph(self, col, row, glyph_idx):
        """Write a raw ROM glyph index at position (col, row).

        Use this for box-drawing, icons, and other non-ASCII glyphs.
        """
        self.write_char(col, row, glyph_idx)

    def write_box(self, col, row, w, h):
        """Draw a box-drawing rectangle using ROM corner/edge glyphs."""
        # Corners
        self.write_glyph(col, row, G_TL_CORNER)
        self.write_glyph(col + w - 1, row, G_TR_CORNER)
        self.write_glyph(col, row + h - 1, G_BL_CORNER)
        self.write_glyph(col + w - 1, row + h - 1, G_BR_CORNER)
        # Top and bottom edges
        for c in range(col + 1, col + w - 1):
            self.write_glyph(c, row, G_HLINE)
            self.write_glyph(c, row + h - 1, G_HLINE)
        # Left and right edges
        for r in range(row + 1, row + h - 1):
            self.write_glyph(col, r, G_VLINE)
            self.write_glyph(col + w - 1, r, G_VLINE)

    def write_hline(self, col, row, length):
        """Draw a horizontal line of ROM ─ glyphs."""
        for c in range(col, col + length):
            self.write_glyph(c, row, G_HLINE)

    # --- Mode switching (WT_ScaleA + WT_ScaleB bools) ---
    #   A=F B=F → TEXT:  scale=fine (0.025×0.125), _AtlasMode=1
    #   A=T B=F → MACRO: scale=full (1.0×1.0),     _AtlasMode=0
    #   A=F B=T → CLEAR: scale=full (1.0×1.0),     _ClearMode=1

    MODE_TEXT = 0
    MODE_MACRO = 1
    MODE_CLEAR = 2

    def set_mode(self, mode):
        """Switch write head mode via WT_ScaleA/WT_ScaleB bools."""
        self.send_param("WT_ScaleA", mode == self.MODE_MACRO)
        self.send_param("WT_ScaleB", mode == self.MODE_CLEAR)
        self._current_mode = mode
        time.sleep(SETTLE_DELAY)

    def set_text_mode(self):
        """Switch to text mode (fine scale, text atlas)."""
        self.set_mode(self.MODE_TEXT)

    def set_macro_mode(self):
        """Switch to macro mode (full scale, block atlas)."""
        self.set_mode(self.MODE_MACRO)

    def set_clear_mode(self):
        """Switch to clear mode (full scale, transparent fill)."""
        self.set_mode(self.MODE_CLEAR)

    def stamp_macro(self, macro_index):
        """Stamp a full-screen macro glyph. Must be in macro mode."""
        self.send_param("WT_CursorX", 0.5)
        self.send_param("WT_CursorY", 0.5)
        self.hw_cursor_x = 0.5
        self.hw_cursor_y = 0.5
        self.send_param("WT_CharLo", float(macro_index))
        time.sleep(WRITE_DELAY)

    def clear_screen(self):
        """Clear the RT using clear mode."""
        print("Clearing screen...")
        self.set_clear_mode()
        self.send_param("WT_CursorX", 0.5)
        self.send_param("WT_CursorY", 0.5)
        self.hw_cursor_x = -1.0
        self.hw_cursor_y = -1.0
        self.send_param("WT_CharLo", 0.0)
        time.sleep(0.3)
        self.screen.clear()
        print("Screen cleared.")


# --- Screen System ---

# Touch zone centers as fractional row positions.
# 3 zones divide 8 rows into thirds: centers at 8/6, 8*3/6, 8*5/6
# Subtract 0.5 because row_to_y adds 0.5 internally.
ZONE_ROWS_FRAC = [
    ROWS * 1 / 6 - 0.5,   # 0.833  — zone 1 center (fractional)
    ROWS * 3 / 6 - 0.5,   # 3.5    — zone 2 center (fractional)
    ROWS * 5 / 6 - 0.5,   # 6.167  — zone 3 center (fractional)
]
# Rounded to integer rows — must match macro atlas layout
ZONE_ROWS = [round(r) for r in ZONE_ROWS_FRAC]
# → [1, 4, 6]


class Screen:
    """Base class for PDA screens."""

    name = "screen"
    # Per-screen refresh interval (seconds). 0 = use global default.
    # Screens with fast-changing content can set a shorter interval.
    refresh_interval = 0
    # How often update() is called (seconds). 0 = never.
    update_interval = 0
    # Macro glyph index in the block atlas. -1 = no macro, full char render.
    MACRO_INDEX = -1

    def __init__(self, pda):
        self.pda = pda
        self.display = pda.display

    def render(self):
        """Draw the full screen via character writes (fallback if no macro)."""
        self.render_frame(self.name)
        self.render_content()
        self.render_status_bar()

    def render_content(self):
        """Override to draw all content within the frame."""
        pass

    def render_dynamic(self):
        """Draw only dynamic content (used after macro glyph stamp).

        Override per screen. Only write cells that change: values, bars,
        clock, cursor. Static labels/borders are in the macro glyph.
        """
        self.render_clock()
        self.render_cursor()

    def on_input(self, key):
        """Handle input. key is e.g. "11", "TL", "ML".
        Return True if handled."""
        return False

    def update(self):
        """Delta-update dynamic content. Only writes changed cells."""
        pass

    def render_clock(self):
        """Write the clock portion of the status bar."""
        now = datetime.datetime.now().strftime("%H:%M:%S")
        col = COLS - 1 - len(now)
        self.display.write_text(col, 7, now)

    def render_cursor(self):
        """Write the current spinner frame."""
        ch = self.pda.SPINNER[self.pda.cursor_visible]
        self.display.write_char(1, 7, ord(ch))

    # -- Shared rendering helpers --

    def render_frame(self, title):
        """Draw the standard border frame with title on row 0."""
        d = self.display
        d.write_glyph(0, 0, G_TL_CORNER)
        title_str = f" {title} "
        pad_left = (COLS - 2 - len(title_str)) // 2
        for c in range(1, 1 + pad_left):
            d.write_glyph(c, 0, G_HLINE)
        d.write_text(1 + pad_left, 0, title_str)
        for c in range(1 + pad_left + len(title_str), COLS - 1):
            d.write_glyph(c, 0, G_HLINE)
        d.write_glyph(COLS - 1, 0, G_TR_CORNER)

        # Side borders on rows 1-6
        for r in range(1, 7):
            d.write_glyph(0, r, G_VLINE)
            d.write_glyph(COLS - 1, r, G_VLINE)

    def render_status_bar(self):
        """Render the bottom border / status bar on row 7."""
        d = self.display
        now = datetime.datetime.now().strftime("%H:%M:%S")
        d.write_glyph(0, 7, G_BL_CORNER)
        ch = self.pda.SPINNER[self.pda.cursor_visible]
        d.write_char(1, 7, ord(ch))
        time_start = COLS - 1 - len(now)
        for c in range(2, time_start):
            d.write_glyph(c, 7, G_HLINE)
        d.write_text(time_start, 7, now)
        d.write_glyph(COLS - 1, 7, G_BR_CORNER)


class HomeScreen(Screen):
    """Home screen with 5x3 tile grid."""

    name = "HOME"
    MACRO_INDEX = 0

    def __init__(self, pda):
        super().__init__(pda)
        self.tile_highlighted = [[False] * TILE_COLS for _ in range(TILE_ROWS)]

    def _tile_text(self, tx, ty):
        label = TILE_LABELS[ty][tx]
        return label, self.tile_highlighted[ty][tx]

    def _write_tile(self, tx, ty):
        """Write a single tile centered on its contact column."""
        label, inverted = self._tile_text(tx, ty)
        center = TILE_CENTERS[tx]
        start_col = center - len(label) // 2
        row = ZONE_ROWS[ty]
        for i, ch in enumerate(label):
            c = start_col + i
            if c < 1 or c >= COLS - 1:
                continue
            char_idx = ord(ch) if 32 <= ord(ch) <= 126 else 32
            if inverted:
                char_idx += INVERT_OFFSET
            self.display.write_char(c, row, char_idx)

    def render_content(self):
        # Tile labels at fractional zone-center rows
        for ty in range(TILE_ROWS):
            row = ZONE_ROWS[ty]
            self.display.write_glyph(0, row, G_VLINE)
            for tx in range(TILE_COLS):
                self._write_tile(tx, ty)
            self.display.write_glyph(COLS - 1, row, G_VLINE)

    def render(self):
        self.render_frame("YIP-BOI OS")
        self.render_content()
        self.render_status_bar()
        print("Home screen rendered.")
        self.display.screen.dump()

    def render_dynamic(self):
        """Only clock and cursor — tile labels are in the macro glyph."""
        self.render_clock()
        self.render_cursor()
        print("Home screen dynamic rendered.")
        self.display.screen.dump()

    def on_input(self, key):
        if len(key) == 2 and key.isdigit():
            tx = int(key[0]) - 1
            ty = int(key[1]) - 1
            if 0 <= tx < TILE_COLS and 0 <= ty < TILE_ROWS:
                label = TILE_LABELS[ty][tx]
                if label.startswith("-"):
                    # Empty tile — just flash highlight
                    print(f">> Tile ({tx},{ty}) is empty")
                    return True

                # Highlight the tapped tile — force immediate writes
                # so the flash is visible before navigation delay
                self.display.cancel_buffered()
                self.tile_highlighted[ty][tx] = True
                self._write_tile(tx, ty)
                self.display.screen.dump()
                print(f">> Tile ({tx},{ty}) '{label}' → navigating...")

                # Queue the screen transition (main loop will handle the
                # delay so we don't block input processing)
                self.pda.pending_navigate = label
                return True
        return False

    def update(self):
        pass


class StatsScreen(Screen):
    """PC Stats sub-screen with live-updating bars.

    Uses mock data with simulated fluctuation for the PoC.
    Replace _get_stats() with real psutil calls for production.
    """

    name = "STATS"
    MACRO_INDEX = 1
    refresh_interval = 0  # uses global refresh for full re-render
    update_interval = 2   # delta-update stats every 2 seconds

    # Layout constants
    BAR_COL = 10
    BAR_WIDTH = 20

    @staticmethod
    def _net_fmt(mbps):
        """Format network speed with unit: k for KB/s, M for MB/s."""
        if mbps < 1.0:
            return f"{mbps * 1000:4.0f}k"  # e.g. " 900k"
        return f"{mbps:4.1f}M"              # e.g. "14.1M"

    def __init__(self, pda):
        super().__init__(pda)
        import random
        self._rng = random
        # Current displayed values (for delta updates)
        self._last_stats = {}

    def _get_stats(self):
        """Return current stats dict. Mock data with random fluctuation."""
        r = self._rng
        return {
            "cpu_pct": max(5, min(99, 47 + r.randint(-15, 15))),
            "cpu_temp": max(35, min(95, 52 + r.randint(-5, 5))),
            "mem_pct": max(20, min(95, 51 + r.randint(-8, 8))),
            "mem_text": "8.2/16G",
            "gpu_pct": max(5, min(99, 34 + r.randint(-12, 12))),
            "gpu_temp": max(40, min(90, 61 + r.randint(-5, 5))),
            "net_up": round(2.1 + self._rng.uniform(-1.5, 3.0), 1),
            "net_down": round(14.7 + self._rng.uniform(-8.0, 10.0), 1),
            "disk_pct": 68,  # stable
            "uptime": "3d 14h 22m",
        }

    def render(self):
        self.render_frame("SYSTEM STATUS")
        stats = self._get_stats()
        self._render_stats(stats)
        self._last_stats = stats
        self.render_status_bar()
        print("Stats screen rendered.")
        self.display.screen.dump()

    def _render_stats(self, s):
        """Render all stat rows from a stats dict."""
        d = self.display

        # Row 1: CPU
        d.write_text(1, 1, "CPU")
        d.write_text(5, 1, f"{s['cpu_pct']:3d}%")
        self._bar(self.BAR_COL, 1, self.BAR_WIDTH, s['cpu_pct'] / 100)
        d.write_text(31, 1, f"{s['cpu_temp']:2d}")
        d.write_glyph(33, 1, G_GEAR)
        d.write_text(34, 1, "C")

        # Row 2: MEM
        d.write_text(1, 2, "MEM")
        d.write_text(5, 2, f"{s['mem_pct']:3d}%")
        self._bar(self.BAR_COL, 2, self.BAR_WIDTH, s['mem_pct'] / 100)
        d.write_text(31, 2, s['mem_text'])

        # Row 3: GPU
        d.write_text(1, 3, "GPU")
        d.write_text(5, 3, f"{s['gpu_pct']:3d}%")
        self._bar(self.BAR_COL, 3, self.BAR_WIDTH, s['gpu_pct'] / 100)
        d.write_text(31, 3, f"{s['gpu_temp']:2d}")
        d.write_glyph(33, 3, G_GEAR)
        d.write_text(34, 3, "C")

        # Row 4: NET
        d.write_text(1, 4, "NET")
        d.write_glyph(5, 4, G_UP)
        d.write_text(6, 4, self._net_fmt(s['net_up']))
        d.write_glyph(11, 4, G_DOWN)
        d.write_text(12, 4, self._net_fmt(s['net_down']))

        # Row 5: DISK
        d.write_text(1, 5, "DISK")
        d.write_text(5, 5, f"{s['disk_pct']:3d}%")
        self._bar(self.BAR_COL, 5, self.BAR_WIDTH, s['disk_pct'] / 100)

        # Row 6: UPTIME (system + YIP OS session)
        d.write_text(1, 6, "UP")
        d.write_text(5, 6, s['uptime'])
        d.write_text(24, 6, "YIP")
        d.write_text(28, 6, self.pda.net_tracker.session_elapsed())

    def update(self):
        """Delta-update only changed values. Called from main loop."""
        stats = self._get_stats()
        d = self.display
        last = self._last_stats
        if not last:
            return

        # Only rewrite cells that changed
        if stats['cpu_pct'] != last.get('cpu_pct'):
            d.write_text(5, 1, f"{stats['cpu_pct']:3d}%")
            self._bar(self.BAR_COL, 1, self.BAR_WIDTH, stats['cpu_pct'] / 100)
        if stats['cpu_temp'] != last.get('cpu_temp'):
            d.write_text(31, 1, f"{stats['cpu_temp']:2d}")

        if stats['mem_pct'] != last.get('mem_pct'):
            d.write_text(5, 2, f"{stats['mem_pct']:3d}%")
            self._bar(self.BAR_COL, 2, self.BAR_WIDTH, stats['mem_pct'] / 100)

        if stats['gpu_pct'] != last.get('gpu_pct'):
            d.write_text(5, 3, f"{stats['gpu_pct']:3d}%")
            self._bar(self.BAR_COL, 3, self.BAR_WIDTH, stats['gpu_pct'] / 100)
        if stats['gpu_temp'] != last.get('gpu_temp'):
            d.write_text(31, 3, f"{stats['gpu_temp']:2d}")

        if stats['net_up'] != last.get('net_up'):
            d.write_text(6, 4, self._net_fmt(stats['net_up']))
        if stats['net_down'] != last.get('net_down'):
            d.write_text(12, 4, self._net_fmt(stats['net_down']))

        # YIP OS session time (always changes)
        d.write_text(28, 6, self.pda.net_tracker.session_elapsed())

        self._last_stats = stats

    def render_dynamic(self):
        """Only dynamic values — labels and empty bars are in the macro glyph."""
        stats = self._get_stats()
        d = self.display

        # CPU: percentage + filled bar + temp
        d.write_text(5, 1, f"{stats['cpu_pct']:3d}%")
        self._bar_filled_only(self.BAR_COL, 1, self.BAR_WIDTH, stats['cpu_pct'] / 100)
        d.write_text(31, 1, f"{stats['cpu_temp']:2d}")

        # MEM: percentage + filled bar + text
        d.write_text(5, 2, f"{stats['mem_pct']:3d}%")
        self._bar_filled_only(self.BAR_COL, 2, self.BAR_WIDTH, stats['mem_pct'] / 100)
        d.write_text(31, 2, stats['mem_text'])

        # GPU: percentage + filled bar + temp
        d.write_text(5, 3, f"{stats['gpu_pct']:3d}%")
        self._bar_filled_only(self.BAR_COL, 3, self.BAR_WIDTH, stats['gpu_pct'] / 100)
        d.write_text(31, 3, f"{stats['gpu_temp']:2d}")

        # NET: values with unit suffix
        d.write_text(6, 4, self._net_fmt(stats['net_up']))
        d.write_text(12, 4, self._net_fmt(stats['net_down']))

        # DISK: percentage + filled bar
        d.write_text(5, 5, f"{stats['disk_pct']:3d}%")
        self._bar_filled_only(self.BAR_COL, 5, self.BAR_WIDTH, stats['disk_pct'] / 100)

        # UPTIME (system + YIP OS session)
        d.write_text(5, 6, stats['uptime'])
        d.write_text(28, 6, self.pda.net_tracker.session_elapsed())

        self._last_stats = stats
        self.render_clock()
        self.render_cursor()
        print("Stats screen dynamic rendered.")
        self.display.screen.dump()

    def _bar(self, col, row, width, frac):
        """Draw a progress bar using ROM shade glyphs (full redraw)."""
        filled = int(frac * width)
        for c in range(width):
            if c < filled:
                self.display.write_glyph(col + c, row, G_SOLID)
            else:
                self.display.write_glyph(col + c, row, G_SHADE1)

    def _bar_filled_only(self, col, row, width, frac):
        """Draw only the filled portion of a bar — empty is in the macro glyph."""
        filled = int(frac * width)
        for c in range(filled):
            self.display.write_glyph(col + c, row, G_SOLID)

    def on_input(self, key):
        return False


class StayScreen(Screen):
    """StayPutVR integration screen.

    Shows connection status, lock state per body part, and drift distance.
    Touch zones toggle lock state for each body part.

    Layout:
    ┌──────────── STAYPUTVR ─────────────┐
    │ CONNECTED          DRIFT: 0.02m    │
    │                                    │
    │  ┌─ LW ─┐  ┌─ RW ─┐  ┌─HEAD─┐   │  row 3 (zone 1 area)
    │                                    │
    │  ┌─ LF ─┐  ┌─ RF ─┐  ┌─PLAY─┐   │  row 5 (zone 2 area)
    │                                    │
    └●OK────────────────────── HH:MM:SS─┘

    Body parts mapped to touch zones (top 2 rows of 5x3 grid):
      Zone (1,1)=LW  (2,1)=RW  (3,1)=HEAD
      Zone (1,2)=LF  (2,2)=RF  (3,2)=PLAYSPACE
    """

    name = "STAY"
    MACRO_INDEX = 2
    update_interval = 3  # check connection status

    BODY_PARTS = [
        ["LW", "RW", "HEAD", None, None],
        ["LF", "RF", "PLAY", None, None],
    ]

    # Column positions for the 3 active body-part tiles, aligned to contact
    # grid columns 1, 2, 4 → TILE_CENTERS[0]-3, [1]-3, [3]-3
    TILE_POSITIONS = [TILE_CENTERS[0] - 3, TILE_CENTERS[1] - 3, TILE_CENTERS[3] - 3]
    # → [1, 9, 25]

    def __init__(self, pda):
        super().__init__(pda)
        # Lock state per body part
        self.locked = {
            "LW": False, "RW": False, "HEAD": False,
            "LF": False, "RF": False, "PLAY": False,
        }
        self.connected = True  # mock
        self.drift = 0.02      # mock drift in meters

    def render(self):
        self.render_frame("STAYPUTVR")
        d = self.display

        # Row 1: connection status + drift
        if self.connected:
            d.write_glyph(1, 1, G_CHECK)
            d.write_text(2, 1, "CONNECTED")
        else:
            d.write_glyph(1, 1, G_XMARK)
            d.write_text(2, 1, "DISCONNECTED")
        d.write_text(24, 1, f"DRIFT:{self.drift:5.2f}m")

        # Row 2: separator
        d.write_glyph(0, 2, G_L_TEE)
        for c in range(1, COLS - 1):
            d.write_glyph(c, 2, G_HLINE)
        d.write_glyph(COLS - 1, 2, G_R_TEE)

        # Rows 3-4: body part tiles (row 1 of parts)
        self._render_part_row(0, 3)

        # Rows 5-6: body part tiles (row 2 of parts)
        self._render_part_row(1, 5)

        self.render_status_bar()
        print("StayPutVR screen rendered.")
        self.display.screen.dump()

    def render_dynamic(self):
        """Dynamic content: connection status, drift, locked parts, clock."""
        d = self.display
        # Connection status
        if self.connected:
            d.write_glyph(1, 1, G_CHECK)
            d.write_text(2, 1, "CONNECTED")
        else:
            d.write_glyph(1, 1, G_XMARK)
            d.write_text(2, 1, "DISCONNECTED")
        # Drift value (label "DRIFT:" is in macro glyph)
        d.write_text(30, 1, f"{self.drift:5.2f}m")
        # Overwrite any locked body parts (macro glyph shows unlocked state)
        for part_row in range(2):
            display_row = 3 if part_row == 0 else 5
            for i, pos in enumerate(self.TILE_POSITIONS):
                part = self.BODY_PARTS[part_row][i]
                if part and self.locked.get(part, False):
                    self._render_single_part(part, part_row, i)
        self.render_clock()
        self.render_cursor()
        print("StayPutVR screen dynamic rendered.")
        self.display.screen.dump()

    def _render_part_row(self, part_row, display_row):
        """Render a row of body part tiles."""
        d = self.display
        for i, pos in enumerate(self.TILE_POSITIONS):
            part = self.BODY_PARTS[part_row][i]
            if part is None:
                continue
            is_locked = self.locked.get(part, False)
            # Icon + label, inverted if locked
            if is_locked:
                d.write_glyph(pos, display_row, G_LOCK)
            else:
                d.write_glyph(pos, display_row, G_UNLOCK)
            label = f" {part:4s} "
            d.write_text(pos + 1, display_row, label, inverted=is_locked)
            # State text below
            state = "LOCKED" if is_locked else " FREE "
            d.write_text(pos, display_row + 1, state.center(8)[:8], inverted=is_locked)

    def _render_single_part(self, part, part_row, part_col):
        """Re-render a single body part tile after toggle."""
        display_row = 3 if part_row == 0 else 5
        pos = self.TILE_POSITIONS[part_col]
        is_locked = self.locked[part]
        d = self.display

        # Force-write icon + label
        if is_locked:
            d.write_glyph(pos, display_row, G_LOCK)
        else:
            d.write_glyph(pos, display_row, G_UNLOCK)
        label = f" {part:4s} "
        # Write each char explicitly to overwrite inverted state
        for i, ch in enumerate(label):
            char_idx = ord(ch)
            if is_locked:
                char_idx += INVERT_OFFSET
            d.write_char(pos + 1 + i, display_row, char_idx)

        state = "LOCKED" if is_locked else " FREE "
        state = state.center(8)[:8]
        for i, ch in enumerate(state):
            char_idx = ord(ch) if 32 <= ord(ch) <= 126 else 32
            if is_locked:
                char_idx += INVERT_OFFSET
            d.write_char(pos + i, display_row + 1, char_idx)

    # Map contact point → (body_part_row, body_part_col) in BODY_PARTS.
    # Contact grid is CRT_Wrist_{col}{row}, col 1-5, row 1-3.
    # Physical alignment (verified in VRC):
    #   12→LW, 13→LF,  22→RW, 23→RF,  42→HEAD, 43→PLAY
    CONTACT_MAP = {
        "12": (0, 0),  # LW
        "13": (1, 0),  # LF
        "22": (0, 1),  # RW
        "23": (1, 1),  # RF
        "42": (0, 2),  # HEAD
        "43": (1, 2),  # PLAY
    }

    def on_input(self, key):
        """Handle tile taps — toggle lock on body parts."""
        mapping = self.CONTACT_MAP.get(key)
        if mapping is None:
            return False

        ty, tx = mapping
        part = self.BODY_PARTS[ty][tx]
        if part is None:
            return False

        self.locked[part] = not self.locked[part]
        state = "LOCKED" if self.locked[part] else "FREE"
        print(f">> StayPutVR: {part} → {state}")
        self._render_single_part(part, ty, tx)
        self.display.screen.dump()
        return True

    def update(self):
        """Update connection status and drift (mock)."""
        import random
        self.drift = max(0.0, round(self.drift + random.uniform(-0.01, 0.01), 3))
        d = self.display
        d.write_text(29, 1, f"{self.drift:5.2f}")


# --- Network Tracker (global, persists across screen switches) ---


class NetTracker:
    """Reads /proc/net/dev and tracks speeds + cumulative totals."""

    def __init__(self):
        self.session_start = time.monotonic()
        self.total_dl = 0
        self.total_ul = 0
        self.current_dl = 0.0  # bytes/sec
        self.current_ul = 0.0
        self.iface = "?"
        self._last_rx = None
        self._last_tx = None
        self._last_time = None
        self._init()

    def _init(self):
        iface, rx, tx = self._read_net_bytes()
        if iface:
            self.iface = iface
            self._last_rx = rx
            self._last_tx = tx
            self._last_time = time.monotonic()

    @staticmethod
    def _read_net_bytes():
        """Return (iface, rx_bytes, tx_bytes) for the most active non-lo interface."""
        try:
            best = None
            with open("/proc/net/dev") as f:
                for line in f:
                    if ":" not in line:
                        continue
                    iface, data = line.split(":", 1)
                    iface = iface.strip()
                    if iface == "lo":
                        continue
                    fields = data.split()
                    rx, tx = int(fields[0]), int(fields[8])
                    if best is None or rx + tx > best[1] + best[2]:
                        best = (iface, rx, tx)
            return best if best else (None, 0, 0)
        except (OSError, ValueError):
            return (None, 0, 0)

    def sample(self):
        """Take a speed sample. Call once per second from main loop."""
        iface, rx, tx = self._read_net_bytes()
        now = time.monotonic()
        if iface and self._last_rx is not None and self._last_time is not None:
            dt = now - self._last_time
            if dt > 0:
                dl = max(0, rx - self._last_rx)
                ul = max(0, tx - self._last_tx)
                self.current_dl = dl / dt
                self.current_ul = ul / dt
                self.total_dl += dl
                self.total_ul += ul
        if iface:
            self.iface = iface
        self._last_rx = rx
        self._last_tx = tx
        self._last_time = now

    def session_elapsed(self):
        """Session duration as H:MM:SS string."""
        s = int(time.monotonic() - self.session_start)
        return f"{s // 3600}:{(s % 3600) // 60:02d}:{s % 60:02d}"

    @staticmethod
    def fmt_rate(bps):
        """Format bytes/sec with unit, 5 chars: ' 1.2M' or ' 342k'."""
        if bps < 1000:
            return f"{bps:4.0f}B"
        bps /= 1024
        if bps < 100:
            return f"{bps:4.1f}k"
        if bps < 1000:
            return f"{bps:4.0f}k"
        bps /= 1024
        if bps < 100:
            return f"{bps:4.1f}M"
        if bps < 1000:
            return f"{bps:4.0f}M"
        bps /= 1024
        return f"{bps:4.1f}G"

    @staticmethod
    def fmt_total(n):
        """Format byte count with unit, 5 chars."""
        if n < 1000:
            return f"{n:4.0f}B"
        n /= 1024
        if n < 100:
            return f"{n:4.1f}k"
        if n < 1000:
            return f"{n:4.0f}k"
        n /= 1024
        if n < 100:
            return f"{n:4.1f}M"
        if n < 1000:
            return f"{n:4.0f}M"
        n /= 1024
        return f"{n:4.1f}G"


class NetScreen(Screen):
    """Network activity monitor with oscilloscope-style time-domain graph.

    Layout:
    ┌──────────────── NETWORK ──────────────┐
    │enp5 ↓ 1.2M ↑ 0.3k D: 142M  0:23:45 │  row 1
    │██████████████████████████████████████│  rows 2-6: graph
    │██████████████████████████████████████│
    │██████████████████████████████████████│
    │██████████████████████████████████████│
    │██████████████████████████████████████│
    └|──────────────────────────  HH:MM:SS─┘
    """

    name = "NET"
    MACRO_INDEX = 3
    update_interval = 1

    # Graph area
    GRAPH_LEFT = 1
    GRAPH_RIGHT = 38
    GRAPH_TOP = 2
    GRAPH_BOTTOM = 6
    GRAPH_WIDTH = GRAPH_RIGHT - GRAPH_LEFT + 1   # 38 columns
    GRAPH_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP + 1   # 5 rows
    GRAPH_LEVELS = GRAPH_HEIGHT * 2               # 10 half-block levels

    def __init__(self, pda):
        super().__init__(pda)
        self._graph_data = [0.0] * self.GRAPH_WIDTH
        self._write_pos = 0
        self._scale = 1024.0  # min 1 KB/s

    def render(self):
        """Full character render (fallback, no macro)."""
        self.render_frame("NETWORK")
        self._render_info_line()
        self._render_graph_full()
        self.render_status_bar()
        print("Net screen rendered.")
        self.display.screen.dump()

    def render_dynamic(self):
        """Dynamic content over macro glyph."""
        self._render_info_line()
        # Graph is empty on first entry — fills via update()
        self.render_clock()
        self.render_cursor()
        print("Net screen dynamic rendered.")
        self.display.screen.dump()

    def _render_info_line(self):
        """Row 1: iface ↓dlspd ↑ulspd D:total  H:MM:SS"""
        d = self.display
        t = self.pda.net_tracker
        # Interface name (4 chars max)
        d.write_text(1, 1, f"{t.iface[:4]:4s}")
        # DL speed
        d.write_glyph(6, 1, G_DOWN)
        d.write_text(7, 1, t.fmt_rate(t.current_dl))
        # UL speed
        d.write_glyph(13, 1, G_UP)
        d.write_text(14, 1, t.fmt_rate(t.current_ul))
        # DL total
        d.write_text(20, 1, "D:")
        d.write_text(22, 1, t.fmt_total(t.total_dl))
        # Session time
        st = t.session_elapsed()
        d.write_text(COLS - 1 - len(st), 1, st)

    def _update_scale(self):
        """Auto-scale Y axis to max value in buffer."""
        peak = max(self._graph_data) if any(self._graph_data) else 0
        self._scale = max(peak, 1024.0)  # floor at 1 KB/s

    def _draw_column(self, pos):
        """Draw one graph column at buffer position."""
        val = self._graph_data[pos]
        col = self.GRAPH_LEFT + pos
        fill = round(val / self._scale * self.GRAPH_LEVELS) if self._scale > 0 else 0
        fill = min(fill, self.GRAPH_LEVELS)

        for cell in range(self.GRAPH_HEIGHT):
            row = self.GRAPH_BOTTOM - cell
            needed = (cell + 1) * 2
            if fill >= needed:
                self.display.write_glyph(col, row, G_SOLID)
            elif fill >= needed - 1:
                self.display.write_glyph(col, row, G_LOWER)
            else:
                self.display.write_char(col, row, ord(" "))

    def _clear_column(self, pos):
        """Clear a graph column to spaces."""
        col = self.GRAPH_LEFT + pos
        for cell in range(self.GRAPH_HEIGHT):
            self.display.write_char(col, self.GRAPH_BOTTOM - cell, ord(" "))

    def _render_graph_full(self):
        """Render entire graph (for full-render fallback)."""
        self._update_scale()
        for i in range(self.GRAPH_WIDTH):
            self._draw_column(i)

    def update(self):
        """Called every update_interval — add sample and redraw column."""
        t = self.pda.net_tracker
        dl = t.current_dl

        self._graph_data[self._write_pos] = dl
        self._update_scale()

        self.display.begin_buffered()
        self._draw_column(self._write_pos)
        # Clear 2 columns ahead as sweep gap
        for offset in range(1, 3):
            gap = (self._write_pos + offset) % self.GRAPH_WIDTH
            self._clear_column(gap)
        self._write_pos = (self._write_pos + 1) % self.GRAPH_WIDTH
        # Refresh info line
        self._render_info_line()

    def on_input(self, key):
        return False


class HeartScreen(Screen):
    """Heart rate monitor with BPM trend graph.

    Layout:
    ┌────────────── HEARTBEAT ──────────────┐
    │ ♥  72 BPM   HI: 98 LO: 61 AVG: 74  │
    │130│                                  │  row 2: graph top + scale
    │   │██                          ███   │
    │   │████ ██████           █████████   │
    │   │██████████████████████████████████│
    │ 60│██████████████████████████████████│  row 6: graph bottom + scale
    └|──────────────────────────  HH:MM:SS─┘
    """

    name = "HEART"
    MACRO_INDEX = 4
    update_interval = 1

    # Scale range
    BPM_MIN = 60
    BPM_MAX = 130

    # Graph area (cols 5-38 to leave room for scale labels)
    GRAPH_LEFT = 5
    GRAPH_RIGHT = 38
    GRAPH_TOP = 2
    GRAPH_BOTTOM = 6
    GRAPH_WIDTH = GRAPH_RIGHT - GRAPH_LEFT + 1   # 34
    GRAPH_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP + 1   # 5
    GRAPH_LEVELS = GRAPH_HEIGHT * 2               # 10

    def __init__(self, pda):
        super().__init__(pda)
        import random
        self._rng = random
        self.bpm = 72
        self.bpm_hi = 72
        self.bpm_lo = 72
        self.bpm_sum = 72
        self.bpm_count = 1
        self._graph_data = [0.0] * self.GRAPH_WIDTH
        self._write_pos = 0
        self._heart_on = True

    def _advance_bpm(self):
        """Simulate BPM fluctuation."""
        delta = self._rng.choice([-3, -2, -1, -1, 0, 0, 0, 1, 1, 2, 3])
        self.bpm = max(55, min(120, self.bpm + delta))
        self.bpm_hi = max(self.bpm_hi, self.bpm)
        self.bpm_lo = min(self.bpm_lo, self.bpm)
        self.bpm_sum += self.bpm
        self.bpm_count += 1

    @property
    def bpm_avg(self):
        return self.bpm_sum // self.bpm_count

    def _bpm_to_level(self, bpm):
        """Map BPM to graph level (0-10)."""
        frac = (bpm - self.BPM_MIN) / (self.BPM_MAX - self.BPM_MIN)
        return max(0, min(self.GRAPH_LEVELS, round(frac * self.GRAPH_LEVELS)))

    def render(self):
        self.render_frame("HEARTBEAT")
        self._render_info_line()
        self.render_status_bar()
        print("Heart screen rendered.")
        self.display.screen.dump()

    def render_dynamic(self):
        self._render_info_line()
        self.render_clock()
        self.render_cursor()
        print("Heart screen dynamic rendered.")
        self.display.screen.dump()

    def _render_info_line(self):
        """Row 1: ♥  72 BPM   HI: 98 LO: 61 AVG: 74"""
        d = self.display
        if self._heart_on:
            d.write_glyph(1, 1, G_HEART)
        else:
            d.write_char(1, 1, ord(" "))
        d.write_text(4, 1, f"{self.bpm:3d}")
        d.write_text(17, 1, f"{self.bpm_hi:3d}")
        d.write_text(24, 1, f"{self.bpm_lo:3d}")
        d.write_text(32, 1, f"{self.bpm_avg:3d}")

    def _draw_column(self, pos):
        """Draw one trend column at buffer position."""
        val = self._graph_data[pos]
        col = self.GRAPH_LEFT + pos
        fill = self._bpm_to_level(val) if val > 0 else 0
        for cell in range(self.GRAPH_HEIGHT):
            row = self.GRAPH_BOTTOM - cell
            needed = (cell + 1) * 2
            if fill >= needed:
                self.display.write_glyph(col, row, G_SOLID)
            elif fill >= needed - 1:
                self.display.write_glyph(col, row, G_LOWER)
            else:
                self.display.write_char(col, row, ord(" "))

    def _clear_column(self, pos):
        col = self.GRAPH_LEFT + pos
        for cell in range(self.GRAPH_HEIGHT):
            self.display.write_char(col, self.GRAPH_BOTTOM - cell, ord(" "))

    def update(self):
        """Sample BPM, advance trend sweep, update display."""
        self._advance_bpm()
        self._heart_on = not self._heart_on

        self._graph_data[self._write_pos] = self.bpm

        self.display.begin_buffered()
        self._draw_column(self._write_pos)
        for offset in range(1, 3):
            gap = (self._write_pos + offset) % self.GRAPH_WIDTH
            self._clear_column(gap)
        self._write_pos = (self._write_pos + 1) % self.GRAPH_WIDTH
        self._render_info_line()

    def on_input(self, key):
        return False


# Screen registry: maps tile label → Screen class
SCREEN_REGISTRY = {
    "STATS": StatsScreen,
    "NET": NetScreen,
    "HEART": HeartScreen,
    "SPVR": StayScreen,
}


# --- PDA Controller ---


class PDAController:
    """Main PDA controller: screen stack, input routing, display management."""

    # Delay between highlight flash and screen transition (seconds)
    NAVIGATE_DELAY = 0.3

    def __init__(self, display, refresh_interval=0):
        self.display = display
        self.input_queue = queue.Queue()
        self.last_trigger = {}  # debounce tracking
        self.debounce_ms = 300
        self.cursor_visible = 0  # spinner frame index
        self.net_tracker = NetTracker()

        # Rolling refresh: re-render current screen periodically.
        # 0 = disabled. Ensures remote viewers see consistent content.
        self.refresh_interval = refresh_interval
        self._last_refresh = 0

        # Screen stack — home is always the root
        self.home_screen = HomeScreen(self)
        self.screen_stack = [self.home_screen]

        # Pending navigation (set by HomeScreen on tile tap)
        self.pending_navigate = None
        self._navigate_time = 0
        self._last_update = 0

    @property
    def current_screen(self):
        return self.screen_stack[-1]

    def _reset_refresh(self):
        """Reset the rolling refresh timer (called after any full render)."""
        self._last_refresh = time.monotonic()

    def _start_render(self, screen):
        """Clear display and queue a buffered render of the given screen.

        If the screen has a macro glyph, stamps it first (1 write = entire
        static frame), then queues only dynamic content. Otherwise falls
        back to full character-by-character rendering.
        """
        self.display.cancel_buffered()
        self.display.clear_screen()

        if screen.MACRO_INDEX >= 0:
            # Stamp the macro glyph (1 write = full static frame)
            self.display.set_macro_mode()
            self.display.stamp_macro(screen.MACRO_INDEX)
            # Switch to text mode for dynamic content
            self.display.set_text_mode()
            self.display.begin_buffered()
            screen.render_dynamic()
        else:
            # No macro glyph — full character render
            self.display.set_text_mode()
            self.display.begin_buffered()
            screen.render()

        n = self.display.buffered_remaining()
        print(f">> Queued {n} writes for {screen.name}")
        self._reset_refresh()

    def push_screen(self, screen_cls):
        """Clear display, push a new screen, and start rendering it."""
        screen = screen_cls(self)
        self.screen_stack.append(screen)
        print(f">> Push: {screen.name}")
        self._start_render(screen)

    def pop_screen(self):
        """Pop current screen and start rendering the one underneath."""
        self.display.cancel_buffered()
        if len(self.screen_stack) <= 1:
            self._go_home()
            return
        old = self.screen_stack.pop()
        print(f">> Pop: {old.name} → {self.current_screen.name}")
        self._start_render(self.current_screen)

    def _go_home(self):
        """Clear stack, reset home, start redraw."""
        self.display.cancel_buffered()
        self.home_screen = HomeScreen(self)
        self.screen_stack = [self.home_screen]
        self._start_render(self.home_screen)

    def get_refresh_interval(self):
        """Return the active refresh interval (screen-specific or global)."""
        screen_interval = self.current_screen.refresh_interval
        if screen_interval > 0:
            return screen_interval
        return self.refresh_interval

    def maybe_refresh(self):
        """Start a buffered re-render if refresh interval has elapsed.

        If the screen has a macro glyph, re-stamps it first (resets static
        content including bar backgrounds), then queues dynamic content.
        """
        # Don't start a new refresh while one is in progress
        if self.display.is_buffered():
            return
        interval = self.get_refresh_interval()
        if interval <= 0:
            return
        now = time.monotonic()
        if now - self._last_refresh >= interval:
            screen = self.current_screen
            print(f"[refresh] Re-rendering {screen.name}")

            if screen.MACRO_INDEX >= 0:
                # Re-stamp macro glyph (resets bars, borders, etc.)
                self.display.set_macro_mode()
                self.display.stamp_macro(screen.MACRO_INDEX)
                self.display.set_text_mode()
                self.display.begin_buffered()
                screen.render_dynamic()
            else:
                self.display.begin_buffered()
                screen.render()

            n = self.display.buffered_remaining()
            print(f"[refresh] {n} writes queued")
            self._last_refresh = now

    def tick_refresh(self):
        """Drain one buffered write. Call this from the main loop.

        Returns True if a write was sent (i.e., refresh is in progress).
        Yields to input processing if anything is queued, so navigation
        inputs (TL/ML) interrupt long renders without extra latency.
        """
        if self.display.is_buffered():
            if not self.input_queue.empty():
                return False  # yield to process_input before next write
            return self.display.flush_one()
        return False

    def maybe_update(self):
        """Call current screen's update() if its update_interval has elapsed.

        Skipped during buffered renders — update() writes immediately and
        we don't want to mix immediate writes with buffered ones.
        """
        if self.display.is_buffered():
            return
        interval = self.current_screen.update_interval
        if interval <= 0:
            return
        now = time.monotonic()
        if now - self._last_update >= interval:
            self.current_screen.update()
            self._last_update = now

    def update_clock(self):
        """Delta-update just the clock portion of the status bar."""
        now = datetime.datetime.now().strftime("%H:%M:%S")
        col = COLS - 1 - len(now)
        self.display.write_text(col, 7, now)

    SPINNER = "|/-\\"

    def toggle_cursor(self):
        """Advance the status bar spinner."""
        self.cursor_visible = (self.cursor_visible + 1) % len(self.SPINNER)
        self.display.write_char(1, 7, ord(self.SPINNER[self.cursor_visible]))

    # --- Input handling ---

    def on_crt_input(self, address, *args):
        """OSC handler — called from listener thread."""
        value = args[0] if args else None
        if not value:
            return

        param = address.split("/")[-1]
        now_ms = time.monotonic() * 1000
        last = self.last_trigger.get(param, 0)
        if now_ms - last < self.debounce_ms:
            return
        self.last_trigger[param] = now_ms

        self.input_queue.put(param)
        print(f"OSC Input: {param} (value={value})")

    def queue_input(self, param_suffix):
        """Queue a simulated input event (e.g. from keyboard)."""
        self.input_queue.put(f"CRT_Wrist_{param_suffix}")

    def process_input(self):
        """Process queued input events (called from main thread)."""
        # Handle pending navigation (delayed after highlight flash)
        if self.pending_navigate:
            if time.monotonic() >= self._navigate_time:
                label = self.pending_navigate
                self.pending_navigate = None
                screen_cls = SCREEN_REGISTRY.get(label)
                if screen_cls:
                    self.push_screen(screen_cls)
                else:
                    print(f">> No screen for '{label}' yet")
                    # Un-highlight and stay on home
                    home = self.home_screen
                    for ty in range(TILE_ROWS):
                        for tx in range(TILE_COLS):
                            if TILE_LABELS[ty][tx] == label:
                                home.tile_highlighted[ty][tx] = False
                                home._write_tile(tx, ty)
                    self.display.screen.dump()
            return  # Don't process other input during navigation delay

        while not self.input_queue.empty():
            try:
                param = self.input_queue.get_nowait()
            except queue.Empty:
                break

            key = param.replace("CRT_Wrist_", "")

            # Global button handling
            if key == "TL":
                print(">> TL: Home")
                self._go_home()
                continue
            elif key == "ML":
                print(">> ML: Back")
                self.pop_screen()
                continue

            # Route to current screen
            handled = self.current_screen.on_input(key)

            # If home screen set a pending navigation, start the delay timer
            if self.pending_navigate:
                self._navigate_time = time.monotonic() + self.NAVIGATE_DELAY
                return  # Stop processing input, wait for delay

            if not handled:
                print(f">> Unhandled input: {key}")


# --- Keyboard Input ---


def keyboard_thread(pda):
    """Read keyboard input for testing. Runs on a daemon thread.

    Commands:
      11-53  Tap tile at grid position (col 1-5, row 1-3)
      TL     Top-left button (redraw home)
      ML     Middle-left button (clear screen)
      q      Quit
    """
    import tty
    import termios

    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        buf = ""
        while True:
            if select.select([sys.stdin], [], [], 0.1)[0]:
                ch = sys.stdin.read(1)
                if ch == '\x03':  # Ctrl+C
                    break
                if ch in ('\r', '\n'):
                    cmd = buf.strip().upper()
                    buf = ""
                    if not cmd:
                        continue
                    if cmd == 'Q':
                        import os
                        os._exit(0)
                    elif cmd in ('TL', 'ML', 'BL', 'TR'):
                        pda.queue_input(cmd)
                    elif len(cmd) == 2 and cmd.isdigit():
                        pda.queue_input(cmd)
                    else:
                        print(f"? Unknown: '{cmd}' (try 11-53, TL, ML, q)")
                else:
                    buf += ch
                    sys.stdout.write(ch)
                    sys.stdout.flush()
    except Exception:
        pass
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)


# --- Main ---


def main():
    global WRITE_DELAY, SETTLE_DELAY

    parser = argparse.ArgumentParser(description="PDA Controller PoC — Williams Tube")
    parser.add_argument("--ip", default="127.0.0.1", help="VRChat OSC IP (default: 127.0.0.1)")
    parser.add_argument("--send-port", type=int, default=9000, help="OSC send port (default: 9000)")
    parser.add_argument("--listen-port", type=int, default=9001, help="OSC listen port (default: 9001)")
    parser.add_argument("--y-offset", type=float, default=0.0,
                        help="Y cursor offset (default: 0.0). Shifts all rows up/down.")
    parser.add_argument("--y-scale", type=float, default=1.0,
                        help="Y cursor scale (default: 1.0). <1 compresses total span.")
    parser.add_argument("--y-curve", type=float, default=1.0,
                        help="Y cursor curvature (default: 1.0). >1 pushes middle rows down, "
                             "<1 pushes them up.")
    parser.add_argument("--calibrate", action="store_true",
                        help="Show calibration test pattern instead of home screen")
    parser.add_argument("--refresh", type=float, default=0,
                        help="Rolling refresh interval in seconds (default: 0 = off). "
                             "Re-renders the current screen periodically so remote "
                             "viewers see consistent content.")
    parser.add_argument("--write-delay", type=float, default=WRITE_DELAY,
                        help=f"Delay per character write in seconds (default: {WRITE_DELAY}). "
                             "Lower = faster rendering, but may drop writes.")
    parser.add_argument("--settle-delay", type=float, default=SETTLE_DELAY,
                        help=f"Settle delay after cursor move in seconds (default: {SETTLE_DELAY}). "
                             "Lower = faster, but cursor may not settle.")
    args = parser.parse_args()

    # Apply timing overrides
    WRITE_DELAY = args.write_delay
    SETTLE_DELAY = args.settle_delay

    print("PDA Controller PoC")
    print(f"  Send:   {args.ip}:{args.send_port}")
    print(f"  Listen: 0.0.0.0:{args.listen_port}")
    print(f"  Display: {COLS}x{ROWS} text mode")
    print(f"  Timing: write={WRITE_DELAY*1000:.0f}ms, settle={SETTLE_DELAY*1000:.0f}ms")
    print(f"  Y: offset={args.y_offset}, scale={args.y_scale}, curve={args.y_curve}")
    if args.refresh > 0:
        print(f"  Refresh: every {args.refresh}s")
    print()
    print("Keyboard commands: 11-53 = tap tile, TL = home, ML = back, q = quit")
    print()

    # OSC sender
    client = udp_client.SimpleUDPClient(args.ip, args.send_port)
    screen = ScreenBuffer()
    display = PDADisplay(client, screen,
                         y_offset=args.y_offset, y_scale=args.y_scale,
                         y_curve=args.y_curve)

    # PDA controller
    pda = PDAController(display, refresh_interval=args.refresh)

    # OSC listener
    dispatcher = Dispatcher()
    dispatcher.map("/avatar/parameters/CRT_Wrist_*", pda.on_crt_input)

    server = ThreadingOSCUDPServer(("0.0.0.0", args.listen_port), dispatcher)
    listener_thread = threading.Thread(target=server.serve_forever, daemon=True)
    listener_thread.start()
    print(f"OSC listener started on port {args.listen_port}")

    # Keyboard input thread
    kb_thread = threading.Thread(target=keyboard_thread, args=(pda,), daemon=True)
    kb_thread.start()

    # Initialize display
    display.clear_screen()
    display.set_text_mode()

    if args.calibrate:
        print("Rendering calibration pattern...")
        print("  Integer row Y positions:")
        for r in range(ROWS):
            y = display.row_to_y(r)
            print(f"    Row {r}: Y={y:.4f}")
        print("  Zone center Y positions (tile rows):")
        for z, zr in enumerate(PDAController.ZONE_ROWS):
            y = display.row_to_y(zr)
            print(f"    Zone {z+1}: row={zr:.3f}, Y={y:.4f}")

        # Write integer row markers
        for r in range(ROWS):
            label = f"R{r}"
            line = label + "-" * (COLS - len(label))
            display.write_text(0, r, line)
        # Write zone center markers (===) so you can compare vs grid lines
        for z, zr in enumerate(PDAController.ZONE_ROWS):
            label = f"Z{z+1}"
            line = label + "=" * (COLS - len(label))
            display.write_text(0, zr, line)
        display.screen.dump()
    else:
        print("Rendering home screen...")
        pda._start_render(pda.current_screen)

    # Main loop
    # Priority order each tick:
    #   1. Input (immediate response)
    #   2. Clock / cursor blink (1Hz)
    #   3. Start new background refresh if interval elapsed
    #   4. Drain one buffered write from background refresh
    #   5. Sleep only if nothing to do
    print("\nRunning (Ctrl+C to stop)...")
    last_clock = 0
    try:
        while True:
            # 1. Input — highest priority
            pda.process_input()

            # 2. Drain one buffered write (if render in progress)
            if pda.tick_refresh():
                # A write was sent (~70ms), loop back to check input
                continue

            # --- Below here only runs when buffer is idle ---

            # 3. Clock update (1Hz)
            now = time.monotonic()
            if now - last_clock >= 1.0:
                pda.update_clock()
                pda.toggle_cursor()
                pda.net_tracker.sample()
                last_clock = now

            # 4. Screen-specific delta updates (e.g., stats bars)
            pda.maybe_update()

            # 5. Maybe start a new background refresh
            pda.maybe_refresh()

            # 6. Nothing to do — sleep briefly
            time.sleep(0.05)
    except KeyboardInterrupt:
        print("\nStopped.")
        server.shutdown()


if __name__ == "__main__":
    main()
