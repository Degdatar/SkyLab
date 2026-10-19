# SkyLab Guide

SkyLab is an in-game asset inspection and preview editor for Skyrim Special Edition and Anniversary Edition. It opens from the SkyOS-powered SkyViewer module and provides a searchable 3D preview for loaded game assets and NPCs.

## Requirements

- SkyOS Core, with `SkyOS.dll` enabled.
- Skyrim SE or AE, SKSE, and the matching Address Library setup.
- Install SkyLab and SkyOS in the same MO2 or Vortex profile.

SkyLab's SkyViewer module declares SkyOS Core as a required dependency. If the core DLL is missing, SkyLab's input and renderer hooks stay disabled.

## Open SkyLab

The default shortcut is **F7**. In **SkyViewer → SkyLab → Keyboard shortcuts**, use **Change** to record a key or chord and **Reset** to return to F7. SkyOS supports combinations such as **Alt+F1** and **Shift+B**. Press **Escape** to close the editor.

When enabled, the crosshair option opens the object currently under the game's crosshair as the editor opens. You can also open a NIF resource directly from the editor's File menu.

## Main features

- Preview NIF meshes captured from world objects and NPCs.
- Search loose assets and assets in loaded Skyrim BSA archives by asset name or linked in-game object name. Search results open the selected NIF in the preview.
- Search NPCs by name, editor ID, FormID, or FaceGen NIF ID and load their captured appearance and equipment.
- Inspect mesh and texture paths and mesh properties in the editor.
- Import an OBJ as a static Skyrim SE/AE NIF using Nifly.
- Configure crosshair loading and viewport defaults in the SkyViewer module, including collision, wireframe, vertex normals, UV overlay, and field of view.

## SkyViewer settings

SkyOS stores module settings at:

```text
%LOCALAPPDATA%\SkyOS\SkyViewer\SkyLab\settings.json
```

The module provides one native SkyOS keyboard shortcut control plus settings for crosshair auto-open, collision display, wireframe, vertex normals, UV overlay, and preview field of view. The SkyViewer page contains the SkyLab banner and description; the shortcut and other options are in SkyViewer's settings area. Changes are read by the SkyLab plugin while Skyrim is running.

## OBJ import limits

OBJ import creates static NIF geometry. It does not create Skyrim skin weights, skeleton bindings, or animation data, so imported geometry will not deform as an actor or wearable armor.

## Preview note

NPC FaceGen tint geometry may appear separately from the face in SkyLab's preview. This preview behavior does not change the NPC in the game.
