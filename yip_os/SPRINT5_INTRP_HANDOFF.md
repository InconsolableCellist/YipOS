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

## What was done (Windows port)

### Build system fixes
- **build_win.bat** (root + yip_os): Rewrote to avoid `if (...)` blocks entirely (vcvarsall
  sets env vars with `Program Files (x86)` which breaks batch parenthesis matching). All
  conditionals use `goto`. Switched from `VsDevCmd.bat` to `vcvarsall.bat x64`.
- **build_win.bat**: Added `CT2_PREFIX` support (defaults to `C:\ct2_install`), mirrors Linux
  `build.sh`'s `CT2_PREFIX`. Copies translation + CUDA runtime DLLs to `build_win/` after build.
- **build_win.bat**: Auto-detects vcpkg from VS 2026 (quotes path for spaces in
  `Program Files (x86)`).
- **CMakeLists.txt**: Moved translation detection BEFORE `add_executable` so `list(REMOVE_ITEM)`
  actually removes TranslationWorker.cpp when deps aren't found.
- **CMakeLists.txt**: Added Windows SentencePiece discovery — `find_package(sentencepiece CONFIG)`
  plus manual fallback via `find_library`/`find_path` for plain installs without cmake config.
- **CMakeLists.txt**: Added `CURL_USE_LIBSSH2 OFF` to avoid runtime dependency on libssh2.dll.
- **CMakeLists.txt**: SentencePiece linking handles imported targets, static targets, or manual
  lib/include paths depending on how it was installed.

### MSVC compatibility fixes
- **Logger.hpp**: Renamed `Level::ERROR` → `Level::ERR` (Windows headers `#define ERROR 0`)
- **Logger.cpp**: Updated all `Level::ERROR` references to `Level::ERR`
- **WhisperWorker.hpp**: Wrapped `std::min`/`std::max` with extra parens `(std::max)(...)` to
  prevent Windows min/max macro expansion

### CUDA acceleration for translation
- **TranslationWorker.cpp**: CUDA-first device selection — tries `Device::CUDA`, catches exception
  and falls back to `Device::CPU`. Logs which device is active.
- **TranslationWorker.cpp**: Runtime CUDA→CPU fallback — if CUDA fails during inference (PTX
  mismatch, driver too old, etc.), automatically reloads model on CPU and continues translating.
- **TranslationWorker.hpp**: Added `device_name_` member, `model_dir_` for fallback, `GetDeviceName()`
- **UIManager_INTRP.cpp**: Status line shows "(CPU)" or "(CUDA)" after "loaded and running"

### INTRPScreen translation guards
- **INTRPScreen.cpp**: Added `#ifdef YIPOS_HAS_TRANSLATION` guards around all `translator->` calls
  in `Update()`. Without translation, the screen shows raw speech-to-text without crashing.

### Installer updates
- **app_installer.nsi**: Added CUDA runtime DLLs (cublas64_12, cublasLt64_12, cudart64_12) with
  `/nonfatal`. Added cleanup in uninstall section.

### build_ct2.bat — CTranslate2 build helper
- New script: `build_ct2.bat` builds SentencePiece + CTranslate2 with CUDA from source
- Uses micromamba CUDA toolkit (`C:\micromamba\envs\ct2build`) without full conda activation
- Forward-slash CUDA paths for CMake compatibility
- `--allow-unsupported-compiler` for CUDA 12.6 + VS 2026 (MSVC 19.50)
- Targets CUDA architectures 60-89 (Pascal through Ada Lovelace)
- Installs to `C:\ct2_install` with CUDA runtime DLLs

## Files changed (modified)
- `build_win.bat` — full rewrite: vcvarsall, goto-based flow, CT2_PREFIX, DLL copy
- `generate_macro_atlas.py` — INTRP + INTRP_CONF macro layouts
- `yip_os/CMakeLists.txt` — translation detection before add_executable, Windows SentencePiece
  discovery, libssh2 disabled, SentencePiece linking for all install types
- `yip_os/build.sh` — CT2_PREFIX for Linux builds
- `yip_os/build_win.bat` — mirrors root build_win.bat changes
- `yip_os/build_installer.bat` — vcpkg hint
- `yip_os/installer/app_installer.nsi` — CUDA DLL bundling + uninstall cleanup
- `yip_os/src/app/PDAController.hpp` — loopback audio/whisper + translation worker pointers
- `yip_os/src/audio/WhisperWorker.cpp` — SetLanguage actually sets language_
- `yip_os/src/audio/WhisperWorker.hpp` — min/max macro fix, GetModelName(), SetLanguage()
- `yip_os/src/core/Glyphs.hpp` — INTRP macro entry
- `yip_os/src/core/Logger.cpp` — raw POSIX I/O rewrite, Level::ERR
- `yip_os/src/core/Logger.hpp` — Level::ERR rename, int logFd_
- `yip_os/src/main.cpp` — loopback instances, translation worker, GoHome before shutdown
- `yip_os/src/screens/INTRPScreen.cpp` — `#ifdef YIPOS_HAS_TRANSLATION` guards in Update()
- `yip_os/src/screens/Screen.cpp` — INTRP/INTRP_CONF/INTRP_LANG factory entries
- `yip_os/src/translate/TranslationWorker.cpp` — CUDA device selection + runtime fallback
- `yip_os/src/translate/TranslationWorker.hpp` — device_name_, model_dir_, GetDeviceName()
- `yip_os/src/ui/UIManager.cpp` — INTRP tab in tab bar
- `yip_os/src/ui/UIManager.hpp` — INTRP tab declarations
- `yip_os/src/ui/UIManager_INTRP.cpp` — shows CPU/CUDA in NLLB status

## Files added (new)
- `build_ct2.bat` — CTranslate2 + SentencePiece CUDA build helper
- `yip_os/src/screens/INTRPScreen.cpp / .hpp` — main interpreter split-screen
- `yip_os/src/screens/INTRPConfScreen.cpp / .hpp` — language config screen
- `yip_os/src/screens/INTRPLangScreen.cpp / .hpp` — language picker (ListScreen)
- `yip_os/src/translate/TranslationWorker.cpp / .hpp` — CTranslate2/NLLB async wrapper
- `yip_os/src/ui/UIManager_INTRP.cpp` — desktop UI tab for INTRP config

## Windows build setup

### Without translation (builds out of the box)
No extra deps needed. Translation code is compiled out when CT2/sentencepiece aren't found.

### With translation + CUDA
1. Install micromamba on Windows: download exe to `C:\micromamba\micromamba.exe`
2. Create env: `micromamba create -n ct2build cuda-toolkit=12.6.3 cmake ninja -c conda-forge`
3. Run `build_ct2.bat` — builds SentencePiece + CTranslate2 with CUDA into `C:\ct2_install`
4. Run `build_win.bat` — auto-detects `C:\ct2_install`, enables translation
5. Installer bundles: ctranslate2.dll, cublas64_12.dll, cublasLt64_12.dll, cudart64_12.dll

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
