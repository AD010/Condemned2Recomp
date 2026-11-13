"""Validate authoritative upstream input integration before Windows recompilation.

This manual-CI guard checks psxrestore/Condemned2Recomp's 2026-11-13
9df903a source-level forensic-tool fix plus local 0x65 safety gate, and the 2026-11-12 mouse fixes,
plus the locally retained checkpoint prompt guard. This is NOT a game test.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
input_cpp = (root / "src/condemned2_input.cpp").read_text(encoding="utf-8")
input_h = (root / "src/condemned2_input.h").read_text(encoding="utf-8")
midhooks = (root / "config/condemned2recomp_config_midhooks.toml").read_text(encoding="utf-8")
graphics = (root / "src/condemned2_graphics.cpp").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/build-input-hotfix.yml").read_text(encoding="utf-8")

def require(ok, message):
    if not ok:
        raise AssertionError(message)

require("auto& forensicBind = activeBinds[\"condemned2_input_forensics\"]" in input_cpp,
        "Upstream forensic button state is missing")
require("std::chrono::steady_clock::now() - forensicBind.pressTime > std::chrono::seconds(2)" in input_cpp,
        "Upstream two-second tool-wheel threshold is missing")
require("NativeKeyPress({0x74});" in input_cpp and
        "ReleaseBind(GetInputManager(), 0x74);" in input_cpp,
        "Forensic R action 0x74 must have a genuine DOWN/UP pair")
require("forensicsWheelCommandSent_" in input_cpp and
        "NativeKeyPress({0x71});" in input_cpp and
        "forensicsNowHeld && forensicBind.prev" in input_cpp,
        "Timed forensic panel command 0x71 must use its own one-shot edge")
require("NativeKeyPress({0x70});" in input_cpp,
        "Upstream forensic return 0x70 is missing")
require("pressTime = std::chrono::steady_clock::now()" in input_cpp and
        "std::chrono::steady_clock::time_point pressTime" in input_h,
        "Forensic held-button timing state is missing")
require("worldUseArmed_" in input_cpp and
        "ReleaseBind(GetInputManager(), 0x57);" in input_cpp and
        "REX_IMPORT(Input_KeyUp, ReleaseBind" in input_cpp,
        "World Use 0x57 needs true guest Input_KeyUp on release")
require('NativeKeyPress({0x47, 0x79, 0x1c})' in input_cpp and
        '0x11, 0x7a, 0xbc' in input_cpp,
        "Original mouse attack commands were changed")
# Local safety patch on top of upstream 9df903a: RMB must not send
# CancelTask(0x65) while an unrelated gameplay command set is active.
# The two attack actions remain unconditional; only the cancel action is gated.
require("REX_IMPORT(Input_IsCommandEnabled, IsCommandEnabled" in input_cpp and
        "if ((IsCommandEnabled(GetInputManager(), 0x65) & 0xff) != 0)" in input_cpp,
        "RMB CancelTask 0x65 must be gated on the active forensic command set")
# Native keyboard/mouse has no XInput combination device: both real attack
# buttons must generate the 0x40 CS_Grab action, independently of normal melee.
require("grabChordActive_" in input_h and
        "NativeKeyPress({0x40});" in input_cpp and
        "ReleaseBind(GetInputManager(), 0x40);" in input_cpp,
        "LT+RT grab must have distinct DOWN/UP edges (0x40)")
require("if (btn == 0x40 && g_condemned2_input_driver->IsUsingPCControls())" in input_cpp and
        'IsNativeBindDown("condemned2_input_primary_fire")' in input_cpp and
        'IsNativeBindDown("condemned2_input_secondary_fire")' in input_cpp,
        "Guest Input_GetButton(0x40) must see both physical attack buttons")
require('quickTool("condemned2_input_tool_uv"' in input_cpp and
        'quickTool("condemned2_input_tool_camera"' in input_cpp and
        'quickTool("condemned2_input_tool_spectrometer"' in input_cpp and
        'quickTool("condemned2_input_tool_gps"' in input_cpp and
        "ReleaseBind(GetInputManager(), static_cast<uint32_t>(id));" in input_cpp and
        "[c2-quicktool]" in input_cpp,
        "Quick forensic tool 1-4 must release native aliases and close the panel")
require("[c2-forensics] panel-open 0x71" in input_cpp and
        "[c2-grab] LT+RT overlap:" in input_cpp and
        "[c2-world-use] E_pressed" in input_cpp,
        "Input transition diagnostics are missing")

require("MnkInputDriver" not in input_cpp,
        "Do not overlay the experimental 0.2.1 MNK keyboard driver on the newly-fixed upstream native input")
require("void Mouse_SensitivityCurveNative(PPCRegister&" in input_cpp and
        "address = 0x82306EE4" in midhooks and
        'registers = ["f31"]' in midhooks,
        "Upstream native mouse sensitivity hook does not match the current source")
require("REX_HOOK_RAW(HudSpectatorModeChange)" in graphics and
        "condemned2_sp_spectator_prompt_fix" in graphics,
        "The retained single-player checkpoint HUD prompt fix is missing")
require("on:\n  workflow_dispatch:" in workflow,
        "CI must support explicit manual workflow dispatch")
# A temporary one-time workflow-file-only push is used by the GitHub app
# connector (which lacks workflow_dispatch). It is created exclusively for
# a user-authorized build, then immediately removed in the next commit.
# The checked-out triggering revision therefore still contains that push
# stanza. Reject any broad push trigger or any path other than this workflow.
if "\n  push:" in workflow:
    require("      - fix/input-weapon-pickup-qte-060" in workflow and
            "      - '.github/workflows/build-input-hotfix.yml'" in workflow and
            "      - 'src/condemned2_input.cpp'" not in workflow,
            "CI push trigger is too broad for a one-time authorized build")
print("PASS: native forensic 0x71 edge, guest E KeyUp, grab 0x40, CancelTask gate, author input, HUD guard")
