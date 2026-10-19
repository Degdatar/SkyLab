ScriptName SkyLab Native Hidden
; SkyLab — Papyrus bridge for the Blender-inside-Skyrim editor
; All functions are native and handled by SkyLab.dll

; Show the SkyLab overlay (3D editor)
Function Show() global native
; Hide the overlay
Function Hide() global native
; Toggle visibility (bound to F7 by default)
Function Toggle() global native
; Is the overlay currently visible?
bool Function IsVisible() global native
; Open a NIF file. Path is relative to Data/, e.g. "meshes/armor/iron/armor.nif"
Function OpenNif(string path) global native
; Live-replace a texture on a ref (or crosshair ref if akRef == None).
; orig and repl are Data-relative texture paths, e.g. "textures/armor/iron.dds"
Function ReplaceTexture(ObjectReference akRef, string orig, string repl) global native
