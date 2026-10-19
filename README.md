# SkyLab

SkyLab is an in-game asset inspection and preview tool for Skyrim Special Edition and Anniversary Edition. It runs as an SKSE plugin and provides an ImGui editor inside Skyrim.

## Features

- Preview NIF meshes and inspect scene nodes, geometry statistics, transforms, and texture references.
- Open NIFs from disk, game resources, or the current crosshair reference.
- Search loose assets and loaded BSA archives for meshes and textures.
- Search NPC records by name, editor ID, FormID, or FaceGen NIF ID and preview available appearance parts.
- Preview texture replacements and apply supported changes to world references.
- Export NIFs and referenced textures.
- Import OBJ files as static Skyrim SE/AE NIFs using Nifly.

OBJ imports create static geometry. They do not generate a character rig, skin weights, collision, or animation data. NIF preview support depends on the geometry and material features handled by the current parser and renderer.

## Requirements

- Windows 10 or 11, Visual Studio 2022 with the C++ desktop workload, and CMake 3.21 or newer.
- Skyrim SE/AE, SKSE64, and Address Library.
- vcpkg packages: `fmt`, `spdlog`, and `directxtk`. Set `VCPKG_ROOT` to your vcpkg installation before configuring CMake.

CMake fetches CommonLibSSE-NG, Dear ImGui, Nifly, and rapidcsv as build dependencies.

## Build

From a Developer PowerShell for Visual Studio 2022 with `VCPKG_ROOT` configured:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The build copies `SkyLab.dll` to `contrib/SKSE/Plugins`. Install the contents of `contrib` and `mod` through a mod manager or into the Skyrim `Data` directory. Launch through SKSE and press **F7** to open or close SkyLab.

## Controls

- **F7:** open or close the editor.
- **Escape:** close the editor.
- **Left mouse drag:** orbit the preview.
- **Right mouse drag:** pan the preview.
- **Left click:** select a mesh.

Configuration is stored in `Data/SKSE/Plugins/SkyLab.ini`. The example file is `mod/SKSE/Plugins/SkyLab.ini.example`; Papyrus source scripts are in `contrib/scripts/Source`.

Thanks to the Nifly and Dear ImGui projects.
