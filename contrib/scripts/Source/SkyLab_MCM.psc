ScriptName SkyLab_MCM extends SKI_ConfigBase
; MCM for SkyLab — hotkey and view options. Requires SkyUI.

int prop HotkeyKeycode = 0x76 auto ; F7
int prop ShowCollision = 1 auto
int prop ShowWireframe = 0 auto

Event OnConfigInit()
    ModName = "SkyLab"
    Pages = new string[2]
    Pages[0] = "General"
    Pages[1] = "View"
EndEvent

Event OnPageReset(string page)
    if page == "General"
        AddHeaderOption("Hotkey (default F7)")
        AddKeyMapOptionST("Hotkey", "Toggle SkyLab", HotkeyKeycode)
        AddHeaderOption("Workflow")
        AddTextOptionST("Hint", "Aim at object + press hotkey", "")
    elseif page == "View"
        AddHeaderOption("Viewport")
        AddToggleOptionST("Wireframe", "Wireframe", ShowWireframe as bool)
        AddToggleOptionST("Collision", "Show Collision", ShowCollision as bool)
    endif
EndEvent

State Hotkey
    Event OnKeyMapChangeST(int keyCode, string conflictControl, string conflictName)
        HotkeyKeycode = keyCode
        SetKeymapOptionValueST(keyCode)
        Debug.Notification("SkyLab hotkey set")
    EndEvent
    Event OnDefaultOpenST()
        SetKeymapOptionValueST(HotkeyKeycode)
    EndEvent
EndState

State Wireframe
    Event OnSelectST()
        ShowWireframe = 1 - ShowWireframe
        SetToggleOptionValueST(ShowWireframe as bool)
    EndEvent
    Event OnDefaultOpenST()
        SetToggleOptionValueST(ShowWireframe as bool)
    EndEvent
EndState

State Collision
    Event OnSelectST()
        ShowCollision = 1 - ShowCollision
        SetToggleOptionValueST(ShowCollision as bool)
    EndEvent
    Event OnDefaultOpenST()
        SetToggleOptionValueST(ShowCollision as bool)
    EndEvent
EndState

State Hint
    Event OnSelectST()
        ShowMessage("Aim crosshair at any object (weapon, armor stand, furniture, static) and press F7. SkyLab will open its NIF, show mesh/nodes/collision/textures. Right-click a mesh to replace its texture and see it live on the object.", false)
    EndEvent
EndState
