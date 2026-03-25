# YipOS Private DM Feature — Feasibility Analysis & Plan

## Context

Two Yip-Boi users should be able to privately DM each other by exchanging contact info in-universe. Messages poll via Cloudflare Workers/KV on the free tier. Identity: auto-generated UUID on backend, VRChat display name shown in the UI.

---

## 1. Cloudflare Free Tier Budget

CF Workers free tier is **100,000 requests/day** (not per hour). KV: **100,000 reads/day**, **1,000 writes/day**.

| Poll interval | Reqs/pair/day | 10 pairs | 20 pairs |
|--------------|---------------|----------|----------|
| 30s          | 5,760         | 57,600   | 115,200 BUST |
| **60s**      | **2,880**     | **28,800** | **57,600** |
| 120s         | 1,440         | 14,400   | 28,800   |

KV writes: each message = 1 write. 20 pairs × 50 msgs = 1,000 (at limit).

**Strategy**: 60s default poll, adaptive 15s burst for 5 min after receiving a message. Each conversation stored as a single KV value (append-only JSON). Growth path: $5/mo paid plan → Durable Objects + WebSockets (Patreon-funded).

---

## 2. Pairing — QR Code via Existing Bitmap Mode

### How It Works

1. Alice initiates pairing → YipOS generates a 6-digit pairing code + 64-bit session ID
2. Alice's CRT switches to bitmap mode and renders a QR code (the pairing code encoded as QR)
3. **The QR is also visible on a screen-space shader sphere** for Bob's VRChat Stream Camera
4. Bob's YipOS captures his desktop screen and decodes the QR with quirc
5. Bob's YipOS sends the decoded pairing code + his user ID to the CF Worker
6. Both users confirm, creating a paired DM session
7. **Fallback**: Bob can manually type the 6-digit code displayed alongside the QR

### QR Rendering — Proven via Phase 0

The QR test screen validates the stamped-template approach:
- QR Version 1 (21×21 modules) fits in the 32×32 bitmap grid with quiet zone
- Pre-stamped QR template macro at slot 37 (finders, timing, format info, quiet zone)
- 1 macro stamp + ~90 light data module writes at SLOW delay (0.07s) = **~6.3 seconds**
- VQ codebook indices: 0 = all-black, 255 = all-white
- `generate_qr_test.py` generates the template PNG, test QR PNG, and C++ data header

---

## 3. Screen Capture (Receiver Side)

For Bob's YipOS to decode Alice's QR:

- **Windows**: DXGI Desktop Duplication API — ~200 lines, no third-party deps. Already links `windowsapp`.
- **Linux**: X11 `XGetImage` — ~80 lines. Project already forces X11 (`GLFW_BUILD_WAYLAND OFF`).
- **QR decoding**: quirc — public domain, single C file (~2k lines). Drop into `thirdparty/quirc/` like sqlite3/stb.

Platform-split pattern follows `AudioCapture_linux.cpp` / `AudioCapture_windows.cpp`.

---

## 4. Audio Dial-Up Pairing (Linux Bonus)

On Linux, PulseAudio can create a virtual mic source that mixes DTMF tones with real microphone audio. Alice's tones go through VRChat voice chat; Bob's loopback capture + Goertzel decoder extract the session data.

- DTMF: 697–1633 Hz, 16 hex digits × 150ms = ~2.4 seconds
- PulseAudio playback + virtual source: ~140 lines (APIs already linked)
- Goertzel decoder: ~150 lines (pure math)
- 5× redundant transmission + CRC-8 for robustness

Linux-exclusive enhancement. Windows uses QR + manual code.

---

## 5. Security & Anonymity

- **Session ID**: 64-bit random. Brute-force probability at 100k guesses/day against 10 sessions: `5.4×10^-14`.
- **Pairing code**: 6-digit numeric, 5-minute TTL, rate-limited (5 attempts/min/IP). 10^6 combinations, infeasible to brute force.
- **Rate limiting**: Per-IP session creation (5/day), per-session message cap (100/day).
- **KV TTL**: 5 min for unpaired sessions, 30 days for inactive paired sessions.
- **E2E encryption** (v2): X25519 key exchange during pairing → XChaCha20-Poly1305 (libsodium). Protocol supports this without changes.

KV keys: `dm:session:{session_id}`, `dm:messages:{session_id}`. No enumeration possible without knowing the random session ID.

---

## 6. Architecture

### CF Worker Endpoints

```
POST /dm/pair/create    { user_id, session_id }         -> { code: "483291" }
POST /dm/pair/join      { code: "483291", user_id }     -> { session_id, peer_id }
                     OR { session_id, user_id }          (direct, for QR/audio)
GET  /dm/pair/status    ?session_id=...&user_id=...     -> { status, peer_id }
POST /dm/pair/confirm   { session_id, user_id }         -> { ok }
GET  /dm/messages       ?session_id=...&since=<ts>      -> [messages]
POST /dm/send           { session_id, user_id, text }   -> { ok }
```

### New C++ Components

| Class | Location | Mirrors |
|-------|----------|---------|
| `DMClient` | `src/net/DMClient.hpp/cpp` | `ChatClient` — libcurl, JSON, multi-session |
| `QRGen` | `src/img/QRGen.hpp/cpp` | Minimal QR V1 encoder (~300 lines, no deps) |
| `ScreenCapture` | `src/platform/ScreenCapture.hpp` | `AudioCapture` — platform-split abstract |
| `ScreenCapture_windows.cpp` | `src/platform/` | DXGI Desktop Duplication |
| `ScreenCapture_linux.cpp` | `src/platform/` | X11 XGetImage |
| `DMScreen` | `src/screens/DMScreen.hpp/cpp` | `ChatScreen` — ListScreen conversation list |
| `DMDetailScreen` | `src/screens/DMDetailScreen.hpp/cpp` | `ChatDetailScreen` — chat view + compose |
| `DMPairScreen` | `src/screens/DMPairScreen.hpp/cpp` | Custom — QR render + pairing state machine |

Thirdparty: `thirdparty/quirc/` (QR decoder, public domain, ~2k lines C)

### CRT Layouts

**DMScreen (conversation list):**
```
+--------------------------------------+
|DM                           12:34PM  |
|*Bob: hey whats up              <1m   |
| Alice: see you later            2h   |
| Carol: lol                      1d   |
|                                      |
|                                      |
|PAIR                    <   1/3   >   |
||                        YipOS v1.2   |
+--------------------------------------+
```

**DMPairScreen (initiator — dialing):**
```
+--------------------------------------+
|DM PAIR                               |
|                                      |
|  [QR CODE RENDERING IN BITMAP MODE]  |
|                                      |
| Code: 483291     Expires 4:32        |
|                                      |
|                                      |
||                        YipOS v1.2   |
+--------------------------------------+
```

**DMPairScreen (receiver — listening):**
```
+--------------------------------------+
|DM PAIR                               |
|                                      |
| Listening... ░░░░░░░░░░             |
|                                      |
| Play dial tone near mic             |
| or enter code: [______]             |
|                                      |
||                        YipOS v1.2   |
+--------------------------------------+
```

### Local Persistence (config.ini)

```ini
dm.user_id=<auto-generated UUID>
dm.display_name=<VRC name>
dm.sessions=session_id_1,session_id_2
dm.session.{id}.peer_name=Bob
dm.session.{id}.last_seen=1711234567
```

### Notifications

Reuse existing unseen-indicator pattern: `has_unseen_dm_` flag in PDAController, status bar dot, asterisk on DM home tile.

---

## 7. Implementation Phases

### Phase 0: QR Rendering Proof-of-Concept ✅

- `generate_qr_test.py` generates test QR PNG, template macro PNG, and C++ data header
- QR template macro added to `generate_macro_atlas.py` at slot 37
- `QRTestScreen` stamps template then writes 90 data modules at SLOW delay (~6.3s)
- QRTEST tile added to Home Page 2
- Verified working in VRChat

### Phase 1: Core DM Infrastructure

- `DMClient` (mirrors ChatClient pattern — libcurl, hand-rolled JSON parser)
- CF Worker with all DM endpoints (deploy to separate worker from chat)
- `DMScreen` — ListScreen subclass showing conversation list with unseen indicators
- `DMDetailScreen` — single conversation view with message history
- 6-digit manual code pairing as initial pairing method
- `dm.user_id` auto-generated UUID on first use, stored in config
- Notification indicators (status bar dot, home tile asterisk)
- ImGui desktop UI: text input for composing messages, DM session management

### Phase 2: QR Visual Pairing

- `QRGen` — minimal QR V1 encoder in C++ (generates 21×21 module matrix from arbitrary numeric payload)
- `DMPairScreen` — pairing ceremony UI:
  1. Initiator: generates session, stamps QR template macro, writes payload-specific data modules
  2. Receiver: captures screen + decodes QR (or enters 6-digit code manually)
  3. Both confirm, session is paired
- `ScreenCapture` (DXGI Desktop Duplication on Windows, X11 XGetImage on Linux)
- `thirdparty/quirc/` integration for QR decoding
- Screen-space shader sphere on avatar for VRChat Stream Camera capture

### Phase 3: Audio Dial-Up Pairing (Linux)

- `ToneCodec` — DTMF tone generator (sine wave synthesis) + Goertzel algorithm decoder
- `AudioPlayback` — PulseAudio playback to null sink + `module-combine-source` virtual mic mixing
- Audio pairing mode in DMPairScreen: initiator dials, receiver listens on loopback capture
- 5× redundant transmission + CRC-8 for robustness through VRChat Opus codec
- Graceful fallback to 6-digit code if decode fails

### Phase 4 (v2): Hardening

- E2E encryption: X25519 key exchange during pairing → XChaCha20-Poly1305 via libsodium
- Local SQLite message cache for offline history
- Durable Objects upgrade for real-time WebSocket messaging (Patreon-funded)

---

## 8. Key Files

### Existing (reference patterns)

| Purpose | File |
|---------|------|
| Bitmap rendering | `src/screens/IMGScreen.cpp` — `WriteBitmap()`, `EnterDisplayMode()` |
| VQ encoder | `src/img/VQEncoder.hpp/cpp` — codebook, 32×32 grid, 8×8 blocks |
| Display driver | `src/app/PDADisplay.cpp` — bitmap mode, macro stamps, buffered writes |
| Chat client | `src/net/ChatClient.hpp/cpp` — libcurl polling, JSON parse |
| Chat screen | `src/screens/ChatScreen.cpp` — ListScreen + consent + unseen indicators |
| Controller | `src/app/PDAController.hpp/cpp` — client ownership, unseen cache, screen stack |
| Screen registry | `src/screens/Screen.cpp` — factory map for `CreateScreen()` |
| Config state | `src/core/Config.hpp/cpp` — INI persistence |
| Audio capture | `src/audio/AudioCapture.hpp` + platform impls — loopback capture for receiver |

### Phase 0 (created)

| Purpose | File |
|---------|------|
| QR asset generator | `generate_qr_test.py` |
| QR template macro | `qr_template.png` (slot 37 in macro atlas) |
| Test QR image | `qr_test.png` + `assets/images/qr_test.png` |
| Hardcoded test data | `src/screens/QRTestData.hpp` |
| QR test screen | `src/screens/QRTestScreen.hpp/cpp` |
| Macro atlas (updated) | `generate_macro_atlas.py` — QR template slot, glyph render fix, QRTEST tile |
| Home tile labels | `src/core/Glyphs.hpp` — QRTEST on page 2 |

---

## 9. Verification Plan

1. **Phase 0** ✅: QR renders on CRT, phone scanner can decode it
2. **Phase 1**: curl all CF Worker endpoints manually; two YipOS instances exchange messages
3. **Phase 2**: End-to-end QR pairing between two YipOS instances (one initiates, other captures + decodes)
4. **Phase 3**: DTMF tones through Opus codec → verify decode accuracy at various VRChat distances
5. **All phases**: Manual VRChat testing (CRT rendering, Stream Camera capture, voice chat audio)
