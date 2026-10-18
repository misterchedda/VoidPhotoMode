# Void Photo Mode for Dishonored 2

A native photo mode with freefly camera, camera controls, composition guides, post-processing adjustments and clean PNG capture. To learn what the mod can do in more detail, visit Nexus: [https://www.nexusmods.com/dishonored2/mods/227](https://www.nexusmods.com/dishonored2/mods/227)

## Build

Requires Windows x64, Visual Studio 2022 with Desktop development with C++,
a Windows SDK, and CMake 3.21+ for the included presets

```powershell
cmake --preset windows
cmake --build --preset release
ctest --preset release
```

The installable DLLs are written to `build/package/`. To stage a complete
release folder including instructions and third-party notices:

```powershell
cmake --install build --config Release --prefix dist
python tools/package_release.py dist VoidPhotoMode.zip
```

## Compatibility and tests

The runtime is pinned to the tested Steam executable, version 1.77.9.0,
with SHA-256:

`b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`

A normal checkout runs 10 tests when Python is available (9 without Python).
The color tests require a hardware D3D11 device; run them on a Windows desktop
with a suitable GPU. The capture test uses WARP. Tests do not launch the game.
An eleventh test is available with private original/patched HUD fixtures:

```powershell
cmake --preset windows -DHUD_ORIGINAL="C:/local/original.iggy" -DHUD_PATCHED="C:/local/patched.iggy"
ctest --preset release
```

## Credits and licensing

Frans Bouma's published Dishonored 2 camera research informed the camera
layout, coordinate conventions and Ansel integration. MinHook is vendored
under `third_party/MinHook`.