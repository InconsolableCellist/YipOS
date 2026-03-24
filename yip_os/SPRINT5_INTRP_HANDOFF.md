# Sprint 5: INTRP Feature — Windows Handoff Notes

## What was done (Linux)

### Sprint 1 — Dual Audio + INTRP Screens
- **INTRPScreen**: Split-screen transcription display (rows 1-3 = their speech, rows 5-6 = your speech)
- **INTRPConfScreen**: Interactive config (I SPEAK / THEY SPEAK labels, touchable)
- **INTRPLangScreen**: ListScreen subclass for picking from 7 languages
- **PDAController**: Added second `WhisperWorker*` + `AudioCapture*` (loopback pair) with getters/setters
- **main.cpp**: Creates both audio+whisper instances, wires to PDAController
- **WhisperWorker**: `SetLanguage()` now actually works (was hardcoded to "en")
- **Glyphs.hpp**: Added INTRP macro at page 1 row 0 col 2
- **Screen.cpp**: INTRP, INTRP_CONF, INTRP_LANG registered in factory map
- **generate_macro_atlas.py**: Added INTRP and INTRP_CONF macro layouts

### Sprint 2 — Translation Pipeline (CTranslate2 + NLLB)
- **TranslationWorker**: Async background thread with CTranslate2 + SentencePiece
  - Two channels: 0 = their speech → my language, 1 = my speech → their language
  - Request queue with 20-item cap, per-channel result queues
  - `PeekLatestTranslated(channel)` for UI preview
- **INTRPScreen**: Routes whisper output through TranslationWorker when languages differ
- **UIManager_INTRP.cpp**: Desktop UI tab with model status, download instructions, live preview
- **CMakeLists.txt**: Optional `find_package(ctranslate2)` + sentencepiece, `YIPOS_HAS_TRANSLATION` define
- All translation code guarded with `#ifdef YIPOS_HAS_TRANSLATION`

### Logger rewrite
- Rewrote from C++ iostream → FILE*/fprintf → raw POSIX open/write
- Bug persists: ggml/Vulkan init closes/redirects file descriptors
- Messages still appear on stderr (always works), just not in log file
- **Future fix**: Init logger AFTER whisper/ggml initialization, or defer log fd open

### Critical shutdown fix
- Added `pda.GoHome()` before worker shutdown in main.cpp to prevent segfault
- Without this, INTRPScreen destructor accesses dangling whisper worker pointers

## Files changed (modified)
- `generate_macro_atlas.py` — INTRP + INTRP_CONF macro layouts
- `yip_os/CMakeLists.txt` — translation sources + optional CT2/sentencepiece deps
- `yip_os/build.sh` — CT2_PREFIX for Linux builds
- `yip_os/build_win.bat` — optional VCPKG_ROOT integration
- `yip_os/build_installer.bat` — vcpkg hint
- `yip_os/installer/app_installer.nsi` — bundles ctranslate2/sentencepiece/openblas DLLs
- `yip_os/src/app/PDAController.hpp` — loopback audio/whisper + translation worker pointers
- `yip_os/src/audio/WhisperWorker.cpp` — SetLanguage actually sets language_
- `yip_os/src/audio/WhisperWorker.hpp` — GetModelName(), SetLanguage(), GetLanguage()
- `yip_os/src/core/Glyphs.hpp` — INTRP macro entry
- `yip_os/src/core/Logger.cpp` — raw POSIX I/O rewrite
- `yip_os/src/core/Logger.hpp` — int logFd_ instead of FILE*
- `yip_os/src/main.cpp` — loopback instances, translation worker, GoHome before shutdown
- `yip_os/src/screens/Screen.cpp` — INTRP/INTRP_CONF/INTRP_LANG factory entries
- `yip_os/src/ui/UIManager.cpp` — INTRP tab in tab bar
- `yip_os/src/ui/UIManager.hpp` — INTRP tab declarations

## Files added (new)
- `yip_os/src/screens/INTRPScreen.cpp / .hpp` — main interpreter split-screen
- `yip_os/src/screens/INTRPConfScreen.cpp / .hpp` — language config screen
- `yip_os/src/screens/INTRPLangScreen.cpp / .hpp` — language picker (ListScreen)
- `yip_os/src/translate/TranslationWorker.cpp / .hpp` — CTranslate2/NLLB async wrapper
- `yip_os/src/ui/UIManager_INTRP.cpp` — desktop UI tab for INTRP config

## Windows build setup

### Without translation (builds out of the box)
No extra deps needed. Translation code is compiled out when CT2/sentencepiece aren't found.

### With translation (needs vcpkg or manual install)
1. Install CTranslate2 + SentencePiece via vcpkg or build from source
2. Set `VCPKG_ROOT` env var before running `build_win.bat`
3. For CUDA support: build CTranslate2 with `-DWITH_CUDA=ON`
4. Installer bundles: ctranslate2.dll, sentencepiece.dll, openblas.dll (with /nonfatal)

### NLLB model setup (end user)
Users need 3 files in `%APPDATA%/yip_os/models/nllb/`:
1. `model.bin` — CTranslate2-format model (~623MB for distilled-600M)
2. `sentencepiece.bpe.model` — tokenizer (~4.8MB)
3. `shared_vocabulary.txt` — vocab file (~2.5MB)

Download from: `huggingface.co/JustFrederik/nllb-200-distilled-600M-ct2-int8`
(config.json is auto-generated if missing)

### NLLB language codes
| Short | NLLB code | Language |
|-------|-----------|----------|
| en | eng_Latn | English |
| es | spa_Latn | Español |
| fr | fra_Latn | Français |
| de | deu_Latn | Deutsch |
| it | ita_Latn | Italiano |
| ja | jpn_Jpan | 日本語 |
| pt | por_Latn | Português |

### NLLB token format (critical — wrong format produces garbage)
```
Source: [eng_Latn] [token1] [token2] ... [</s>]
Target prefix: [fra_Latn]
```
- Language codes are BARE (no `__` wrapping)
- `</s>` at end of source is REQUIRED
- ComputeType::AUTO (falls back from INT8 → FLOAT32 if backend doesn't support INT8)

## Remaining plan items
- **Sprint 3**: Extended character ROM + bank switching (accented Latin + kana)
- **Sprint 4**: MeCab + Japanese kanji→hiragana
- **Sprint 5**: CC translate option, polish, error states
- **Logger**: Investigate deferred init (open fd after ggml) or dup the fd to a high number

## Linux runtime dependencies (in /tmp/ct2_install/lib64/)
- libctranslate2.so.4
- libsentencepiece.so.0
- libsentencepiece_train.so.0
- libopenblaso.so.0
