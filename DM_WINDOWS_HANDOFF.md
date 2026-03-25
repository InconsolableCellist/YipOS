# DM Feature — Windows Build & Test Handoff

## What's Done

All DM feature code (Phases 1 + 2) is written and the QR pipeline is verified on Linux.

### New Files (all untracked, in yip_os/)
| File | Purpose |
|------|---------|
| `src/net/DMClient.hpp/cpp` | Network client — pairing, messaging, session mgmt via CF Worker |
| `src/screens/DMScreen.hpp/cpp` | Conversation list (ListScreen) |
| `src/screens/DMDetailScreen.hpp/cpp` | Message thread view |
| `src/screens/DMPairScreen.hpp/cpp` | Pairing state machine — QR render + scan |
| `src/img/QRGen.hpp/cpp` | QR V1 numeric encoder (21×21, EC-H) |
| `src/platform/ScreenCapture.hpp` | Abstract screen capture interface |
| `src/platform/ScreenCapture_windows.cpp` | DXGI Desktop Duplication capture |
| `src/platform/ScreenCapture_linux.cpp` | X11 XGetImage (won't work on Wayland — fallback to code entry) |
| `src/ui/UIManager_DM.cpp` | ImGui DM tab (pair, compose, sessions) |
| `thirdparty/quirc/` | QR decoder library (6 C files, ISC license) |

### Modified Files
| File | Change |
|------|--------|
| `CMakeLists.txt` | Added all new sources, quirc lib, X11 link (Linux) |
| `src/app/PDAController.hpp/cpp` | DMClient ownership, session persistence, unseen cache, polling |
| `src/screens/Screen.cpp` | DM/DM_DTL/DM_PAIR in screen factory, unseen indicator |
| `src/core/Glyphs.hpp` | DM tile on home page 2 |
| `src/ui/UIManager.hpp/cpp` | DM tab hook |

### Backend
- CF Worker deployed at `https://yipos-dm.dan-a7b.workers.dev/`
- Source in `dm-worker/worker.js` — all endpoints tested via curl
- KV namespace: `yipos-dm-kv`

## What's Verified
- **QR encode→decode round-trip**: QRGen produces valid QR codes that quirc decodes correctly (5/5 test codes pass)
- **Bug fixed**: Format info bits were LSB-first instead of MSB-first in `QRGen::ComputeFormatInfo()` — fixed and verified
- **CF Worker endpoints**: create, join, confirm, send, messages all tested with curl
- **Worker security**: input sanitization, rate limiting, auth checks, message caps

## What Needs Windows Testing

### 1. Build It
The CMakeLists.txt changes should work on Windows. quirc compiles as C (not C++) — CMake handles this via `add_library(quirc STATIC ...)` with `.c` files. No new Windows dependencies beyond what's already linked (d3d11, dxgi are system libs, linked via `#pragma comment`).

### 2. Test Screen Capture (Critical Path)
`ScreenCapture_windows.cpp` uses DXGI Desktop Duplication API:
- Creates D3D11 device
- Calls `IDXGIOutput1::DuplicateOutput` on output 0
- `AcquireNextFrame` with 500ms timeout
- Copies to staging texture, maps for CPU read
- Converts BGRA → grayscale

**Quick smoke test**: Run YipOS, go to DM PAIR screen, tap SCAN. Check logs for:
- `DMPair: quirc_new failed` → quirc issue
- `ScreenCapture: D3D11CreateDevice failed` → GPU access issue
- `ScreenCapture: DuplicateOutput failed` → needs to run as non-elevated process, or multi-monitor issue
- `ScreenCapture: AcquireNextFrame failed` → frame acquisition issue
- `DMPair: scanned QR code: XXXXXX` → it works

**Known DXGI gotchas**:
- DuplicateOutput fails if the app is running elevated (admin) but the desktop is not
- Only captures the primary monitor (output 0). Multi-monitor users would need the QR on monitor 0
- Some GPUs need `D3D_DRIVER_TYPE_HARDWARE` specifically
- `AcquireNextFrame` returns `DXGI_ERROR_WAIT_TIMEOUT` if no new frame — this is handled

### 3. End-to-End Pairing Test
Need two YipOS instances (could be same machine, two windows):
1. Instance A: DM PAIR → tap DIAL → creates session, renders QR on CRT
2. Instance B: DM PAIR → tap SCAN → captures screen, looks for QR
3. If B's screen can see A's CRT (e.g., side-by-side windows), quirc should decode the 6-digit code
4. B auto-joins, A sees "Peer connected", taps OK → pairing complete

**Alternatively**, test with manual code entry:
1. Instance A: DM PAIR → DIAL → note the 6-digit code shown
2. Instance B: Use ImGui DM tab → paste code in "Join Code" field → click Join

### 4. Test Messaging
After pairing:
1. Both instances should show the session in DM list
2. Open a conversation, type in ImGui compose box, send
3. Other instance should see the message after next poll (60s default, or click Refresh)

## Config Defaults
- Worker endpoint: `https://yipos-dm.dan-a7b.workers.dev/`
- User ID: auto-generated UUID on first run (stored in config.ini as `dm.user_id`)
- Display name: from `osc.username` config key
- Poll interval: 60s (configurable in ImGui DM tab)

## Notes
- The QR template macro (slot 37) needs to exist in the macro atlas for QR rendering to look right. If it's not generated yet, QR encode will still work but `StampMacro(37)` will stamp whatever's in that slot. The fallback text code display works regardless.
- Linux screen capture is non-functional on Wayland desktops — users enter the 6-digit code manually instead.
