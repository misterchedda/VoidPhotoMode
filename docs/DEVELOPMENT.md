# Development notes

## Runtime ownership

The dinput8 proxy forwards to the absolute Windows system library. It starts
backend initialization from DirectInput8Create, outside its DllMain. The
backend validates the executable hash and expected layouts before installing
hooks. The engine pump owns photo-mode entry/exit and restores prior settings.
Render hooks read published control values. PNG encoding runs on a worker.
Hot unloading is not supported; close the game before replacing either DLL.

The DLL includes no probe interface, controller server or named pipe commands.
The small temporary D3D11 device/window used during startup discovers API hook
addresses; it is required runtime initialization, not a research probe.

## UI contract

Edit `ui/contract.json`, then run:

```powershell
python tools/generate_contract.py
python tools/generate_contract.py --check
```

This generates `src/generated/ui_contract.h` and marked sections in
`ui/PhotoPanel.as.inc`. C++ uses named commands and argument slots. Slider
limits are generated for both sides. The numeric protocol remains compatible
with the shipped panel. CTest detects stale generated contract files when
Python is available.

Changing ActionScript does not automatically change the embedded HUD delta.
Compile the modified HUDScene using JPEXS against a locally extracted original
HUD, repack the resulting ABC into an IGGY copy, then run:

```powershell
python tools/make_delta.py local-assets/original.iggy local-assets/patched.iggy src/generated/hud_patch_data.inc
```

The extraction/repacking research tools and original HUDScene are intentionally
outside this production repository. A standard C++ build is self-contained
and uses the checked-in delta; UI asset rebuilding still needs that external
workflow and locally owned assets. Never commit full game assets or the
contents of `local-assets/`. The decoder checks source and output fingerprints
and rejects malformed patches.

## Known constraints

- Only the pinned Steam executable is supported.
- Alt-Tab ends photo mode and restores the normal game UI. It does not preserve
  framing across the handoff.
- Captures use 8-bit SDR. The five-present settling delay remains a heuristic.
- Non-neutral color transforms allocate a fresh small LUT to preserve results
  still bound or referenced by recorded command lists. Neutral settings bypass
  the transform.
- Other dinput8 loaders are not chained. Other graphics mods are not verified.
- The custom output-folder option has not had a separate live-game test.

## Repository hygiene

Build products go under `build/`, staged releases under `dist/`, and private
fixtures under `local-assets/`; all are ignored. Generated source files are
tracked so ordinary builds do not need Python, Java or game assets. The
checked-in .clang-format applies to our C++ sources; do not reformat vendored
MinHook or hand-edit the generated contract header.
