# SkyLab

SkyLab is an in-game asset inspection and preview ,replacement tool for Skyrim Anniversary Edition. It runs as an SKSE plugin and opens an ImGui-based editor inside Skyrim.

## Features

- Preview NIF meshes and inspect scene nodes, mesh statistics, texture references, and transforms.
- Open a NIF from a file or from the current crosshair reference.
- Search loose assets and assets in loaded Skyrim BSA archives by mesh or texture name, with results limited while searching.
- Search NPC records by name, editor ID, FormID, or FaceGen NIF ID, and preview NPC appearance parts.
- Preview texture replacements and apply supported changes to world references.
- Export source NIFs and referenced textures.
- Import an OBJ as a static Skyrim SE/AE NIF using Nifly. OBJ import does not create Skyrim skinning or animation data.
- Papyrus API and optional SkyUI MCM support.
- Replace NIF optiona
- Replace Texture option
## Requirements

- Windows 10/11, Visual Studio 2022 with the C++ desktop workload, and CMake 3.21 or newer.
- Skyrim AE 1.7.x or newer, SKSE64, and Address Library.
- vcpkg packages used by the project: `fmt`, `spdlog`, and `directxtk`. Set `VCPKG_ROOT` to your vcpkg installation before configuring CMake.

CMake fetches CommonLibSSE-NG, Dear ImGui, Nifly, and rapidcsv as build dependencies. These dependencies are not vendored in this repository.

## Build

From a Developer PowerShell for VS 2022, with `VCPKG_ROOT` configured:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The build copies `SkyLab.dll` to `contrib/SKSE/Plugins`. Copy the plugin and configuration into your Skyrim mod setup, or package the `contrib` and `mod` folders for installation.

## Install

Install the contents of `contrib` and `mod` into a mod manager, or merge them into the Skyrim `Data` directory. Launch the game through SKSE and press **F7** to open or close SkyLab. The hotkey and other settings can be changed in `Data/SKSE/Plugins/SkyLab.ini`.

The example configuration is in `mod/SKSE/Plugins/SkyLab.ini.example`. Papyrus source scripts are in `contrib/scripts/Source`.

## Controls and limitations

- **F7:** open or close the editor.
- **Escape:** close the editor.
- **Left mouse drag:** orbit the preview.
- **Right mouse drag:** pan the preview.
- **Left click:** select a mesh.

Big Thanks to 
Nifly 
ImGUI

OBJ import creates static geometry. It does not generate a rig, skin weights, collision, or animation setup. NIF preview compatibility depends on the geometry and material features supported by the current parser and renderer.
