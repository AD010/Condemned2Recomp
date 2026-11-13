"""Source-only regression checks for psxrestore's 2026-11-13 input overhaul.

A successful compiler run is NOT proof that A pickup prompts or LT+RT boss
finishers work at mission_04; those still require a real in-game check.
This test does not read private Xbox game binaries or user save files.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

def require(condition, message):
    if not condition:
        raise AssertionError(message)

cpp = source("src/condemned2_input.cpp")
header = source("src/condemned2_input.h")
tu = source("config/condemned2recomp_config_tu.toml")
midhooks = source("config/condemned2recomp_config_midhooks.toml")
graphics = source("src/condemned2_graphics.cpp")
cheats = source("src/condemned2_cheats.cpp")
widgets = source("src/ui/condemned2_widgets.h")
launcher = source("src/ui/condemned2_launcher.cpp")
workflow = source(".github/workflows/build-input-hotfix.yml")
cmake = source("CMakeLists.txt")

def contains_all(blob, *tokens):
    return all(token in blob for token in tokens)

# Post-0.6.0 upstream rewrite, ActionIDs, newer mouse pipeline and TU hook.
require(contains_all(header, "enum ActionID : uint32_t",
                     "PLAYER_USE = 0x57", "PLAYER_GRAB = 0x40",
                     "FORENSICS_OPEN = 0x71", "FORENSICS_QUICK = 0x70",
                     "void TimedBind(", "void ReleaseCapturedInputs()"),
        "Missing action identifiers or native input lifecycle")
require(contains_all(cpp, "last_accum_dx += dx", "last_accum_dy += dy",
                     "void Mouse_SensitivityCurveNative(PPCRegister&",
                     "REX_HOOK_RAW(Camera_MouseLook)",
                     "ResetAccumMouse();"),
        "Missing upstream accumulated native mouse input")
require(contains_all(tu, '0x82306DD8 = {name="Camera_MouseLook"}',
                     '0x8234BEC0 = {name="Input_KeyUp"}',
                     '0x8234C388 = {name="Input_IsCommandEnabled"}',
                     '0x824149D8 = {name="HudSpectatorModeChange"}'),
        "Incorrect TU hook addresses for current source")
require(contains_all(midhooks, "address = 0x82306EF4",
                     'name = "Mouse_SensitivityCurveNative"',
                     'registers = ["f1"]'),
        "Upstream sensitivity midhook 6411f7e was lost")

# Input_KeyUp must NOT be disguised as Input_KeyDown.
require(contains_all(cpp, "REX_IMPORT(Input_KeyUp, Input_ReleaseBind",
                     "if (release) {",
                     "Input_ReleaseBind(keyManager, static_cast<uint32_t>(k));"),
        "Game button UP was not dispatched to Input_KeyUp")

# Use is a single action even when E and Enter are both down.
require(contains_all(cpp, "const bool useDown = IsBindDown(useBind) || IsBindDown(enterMenuBind);",
                     "heldUseActions_.push_back(ActionID::PLAYER_USE)",
                     "ActivateBind(heldUseActions_);",
                     "ActivateBind(heldUseActions_, true);",
                     "[c2-use] A/Use DOWN", "[c2-use] A/Use UP"),
        "Contextual A/Use must preserve the same aliases through UP")
require("bool useHeld_ = false;" in header and
        "std::vector<int32_t> heldUseActions_;" in header,
        "The shared E/Enter action state is missing")

# Timed forensic tool selection and all four quick wheel shortcuts.
require(contains_all(cpp, "forensicsPanelOpen_ = true",
                     "forensicsPanelOpen_ = false",
                     "ActivateBind({holdKeycode}, true);",
                     "ActivateBind({onKeycode}, true);",
                     "ActivateBind({offKeycode});",
                     'quickTool("condemned2_input_tool_uv"',
                     'quickTool("condemned2_input_tool_camera"',
                     'quickTool("condemned2_input_tool_spectrometer"',
                     'quickTool("condemned2_input_tool_gps"'),
        "Tool command UP/return path was lost")

# Do not cancel investigation on every unrelated RMB attack.
require(contains_all(cpp, "REX_IMPORT(Input_IsCommandEnabled, Input_CommandEnabled",
                     "ActionID::RIGHT_TRIGGER_PRESS, ActionID::TRIGGER_HOLD",
                     "Input_CommandEnabled(GetInputManager(), ActionID::CANCEL_INVESTIGATION)",
                     "ActivateBind({ActionID::CANCEL_INVESTIGATION});"),
        "RMB CancelTask must be conditional while normal melee is retained")

# Both mouse buttons need an actual guest grab action and release.
require(contains_all(cpp, "const bool bothTriggers = isMouseLeft && isMouseRight;",
                     "ActivateBind({ActionID::PLAYER_GRAB});",
                     "ActivateBind({ActionID::PLAYER_GRAB}, true);",
                     "const MouseBind cached = g_condemned2_input_driver->MouseClick();",
                     "return cached.isMouseLeft && cached.isMouseRight;",
                     "[c2-grab] LT+RT overlap"),
        "Native LT+RT Grab 0x40 state not preserved")
require(contains_all(header, "bool grabChordActive_ = false;",
                     "bool IsInputDialogOpen() const"),
        "Grab chord must not survive F4 dialog capture")
require(contains_all(cpp, "ReleaseCapturedInputs();",
                     "void Condemned2InputDriver::ReleaseCapturedInputs()",
                     "mouse_clicks = {};"),
        "Guest action releases are missing when settings capture input")

# Repair checkpoint spectator prompt issue without replacing official HUD.
require(contains_all(graphics, "REX_HOOK_RAW(HudSpectatorModeChange)",
                     "condemned2_sp_spectator_prompt_fix"),
        "Single-player checkpoint HUD prompt protection missing")
require(contains_all(cpp, "[c2-guest-action]", "[c2-hud]",
                     "[c2-forensics]", "[c2-quicktool]"),
        "Context-action diagnostics missing; future mission_04 log would be blind")

# All author interface/QoL changes since v0.6.0 have been migrated.
require(contains_all(widgets, "ImGui::PushFont(font, fontSize)",
                     "static bool AddMenuButton("),
        "Latest upstream DPI-responsive widgets missing")
require(contains_all(launcher, "io.DisplaySize.y * 0.044",
                     "io.DisplaySize.y * 0.868f"),
        "Upstream proportional launcher layout missing")
require(contains_all(cheats, "GRENADEOVERRIDE Taser_Level03",
                     "flashlightintensity"),
        "Upstream taser/flashlight fix missing")
require("GPU_PLUGINS xenos" in cmake,
        "Windows source build must retain Xenos GPU plugin linkage")

# SDK gitlink must track author's latest 546a082a exactly.
result = subprocess.run(["git", "ls-tree", "HEAD", "thirdparty/rexglue-sdk"],
                        cwd=ROOT, capture_output=True, text=True, check=True)
require("160000 commit 546a082a4356173635f6c40510a41ff53e518b5a" in result.stdout,
        "ReXGlue-SDK gitlink is not the author's v0.10.1 pin")

# Source-only edits must never start CI. The one-time authorized run can
# temporarily have a push trigger restricted to this workflow file alone.
require("on:\n  workflow_dispatch:" in workflow,
        "Existing workflow_dispatch trigger is required")
if "\n  push:" in workflow:
    require(contains_all(workflow,
                         "      - fix/input-weapon-pickup-qte-060",
                         "      - '.github/workflows/build-input-hotfix.yml'") and
            "      - 'src/condemned2_input.cpp'" not in workflow,
            "Temporary user-authorized CI trigger may target ONLY this workflow file")
print("PASS: upstream 47b6508 input+UI, SDK 546a082a, A Use, grab QTE, forensic UP, HUD")
