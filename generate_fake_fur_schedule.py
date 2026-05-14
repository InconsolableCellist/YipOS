#!/usr/bin/env python3
"""
Generate a fake Furality schedule for testing the ULTRA program's
notifications and "*" indicator.

Writes 8 events to yip_os/assets/cache/furality.json, anchored to the
current system clock. Backs up any existing cache to a sibling .bak file
so you can restore the real schedule after testing.

Event offsets (relative to NOW):
  -16 min, -15 min, -5 min, -1 min, +1 min, +5 min, +15 min, +20 min

Expected behavior when marked (per the C++ logic in PDAController.cpp):
  Indicator "*" visible:    -15, -5, -1, +1, +5, +15  (lead in [-15m, +15m])
  Indicator "*" hidden:     -16, +20                  (out of window)
  Imminent buzz (5 pulses): +1                        (lead <= 90s)
  Pre buzz   (3 pulses):    +5, +15                   (lead in (90s, 900s])
  No buzz:                  past events (lead <= 0)
"""

import json
import os
import shutil
import sys
import time
from pathlib import Path

# Schedule offsets in seconds from now.
OFFSETS_MIN = [-16, -15, -5, -1, 1, 5, 15, 20]
EVENT_DURATION_SECONDS = 60 * 60  # 1-hour windows; the imminent/pre logic
                                  # only cares about start_unix.

# Where the C++ side loads the cache from (relative to repo root).
DEV_CACHE_PATH = Path(__file__).resolve().parent / "yip_os" / "assets" / "cache" / "furality.json"


def make_event(offset_min: int, now: int) -> dict:
    start = now + offset_min * 60
    end = start + EVENT_DURATION_SECONDS
    sign = "p" if offset_min >= 0 else "m"
    eid = f"test-{sign}{abs(offset_min)}"
    title = f"TEST {offset_min:+d}m"  # "TEST -5m", "TEST +1m", etc.
    return {
        "id": eid,
        "title": title,
        "description": (
            f"Synthetic test event {offset_min:+d} minutes from script run time. "
            f"Mark this event in the UI to exercise the ULTRA notification path."
        ),
        "host": "test-harness",
        "location": "fake-world",
        "track": "test",
        "url": "",
        "start_unix": start,
        "end_unix": end,
        "day_index": 0,
    }


def main() -> int:
    now = int(time.time())
    events = [make_event(off, now) for off in OFFSETS_MIN]

    # Festival window covers all events with some slack.
    festival_start = min(e["start_unix"] for e in events) - 3600
    festival_end = max(e["end_unix"] for e in events) + 3600

    cache = {
        "name": "Furality Ultra (TEST FIXTURE)",
        "start_unix": festival_start,
        "end_unix": festival_end,
        "day_count": 1,
        "fetched_at": now,  # silences the 30-min refetch gate
        "events": events,
    }

    target = DEV_CACHE_PATH
    target.parent.mkdir(parents=True, exist_ok=True)

    backup_msg = ""
    if target.exists():
        backup = target.with_suffix(target.suffix + ".bak")
        # Don't clobber an existing backup (e.g. running the script twice).
        if not backup.exists():
            shutil.copy2(target, backup)
            backup_msg = f"  Backed up real cache -> {backup}"
        else:
            backup_msg = f"  Backup already exists at {backup} (left untouched)"

    with open(target, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=2)

    # Pretty event table for the tester.
    rows = []
    for off, ev in zip(OFFSETS_MIN, events):
        label = f"{off:+d} min"
        local = time.strftime("%H:%M:%S", time.localtime(ev["start_unix"]))
        rows.append(f"    {label:>8}  id={ev['id']:<10}  starts at {local}")

    print("=" * 72)
    print(" Fake ULTRA schedule written")
    print("=" * 72)
    print(f"  File:   {target}")
    if backup_msg:
        print(backup_msg)
    print(f"  Anchor: now = {now} ({time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(now))})")
    print()
    print("  Events:")
    for r in rows:
        print(r)
    print()
    print("-" * 72)
    print(" Test steps")
    print("-" * 72)
    print("  1. Launch yip_os.exe (from yip_os/build_win/ for a dev build).")
    print("     If it was already running, RESTART it so LoadCache picks up the")
    print("     new file at startup.")
    print()
    print("  2. Optional: in the desktop UI, set fur.fetch_interval to a large")
    print("     value (e.g. 86400) so the app doesn't refetch and overwrite the")
    print("     cache mid-test. Default is 1800 s (30 min).")
    print()
    print("  3. On the PDA, navigate: HOME page 2 -> ULTRA tile -> ALL (or Day 1).")
    print()
    print("  4. Mark events with SEL. Recommended pre-marks for full coverage:")
    print("       test-p1   (fires Urgent / 5-buzz within ~60 s)")
    print("       test-p5   (fires Alert  / 3-buzz immediately)")
    print("       test-p15  (fires Alert  / 3-buzz immediately, edge of window)")
    print("       test-m5   (no buzz; tests indicator-only past event)")
    print()
    print("  5. Verify:")
    print("       - Status bar col-3 BULLET lit (any marked event in window)")
    print("       - ULTRA tile on HOME page 2 shows inverted '*' at right edge")
    print("       - Audio + haptic patterns fire at the right times (SteamVR on)")
    print()
    print("-" * 72)
    print(" Restore real schedule")
    print("-" * 72)
    print(f"  Delete the fake and move the backup back, e.g.:")
    bak = target.with_suffix(target.suffix + ".bak")
    print(f"     mv \"{bak}\" \"{target}\"")
    print("  Or just trigger a real fetch (restart with internet + wait 30 min,")
    print("  or set fur.fetch_interval back to 1800 and toggle network).")
    print("=" * 72)
    return 0


if __name__ == "__main__":
    sys.exit(main())
