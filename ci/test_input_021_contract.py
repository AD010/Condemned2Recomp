"""Source-level guardrails for the 0.2.1 keyboard/controller regression.

These checks do not replace a game test on the mission_04 checkpoint. They
prevent CI from shipping a build that accidentally re-enables two competing
gameplay input sources, removes E/A, or disables the known HUD prompt fix.
No proprietary game files are read by this test.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/condemned2_input.cpp").read_text(encoding="utf-8")
header = (ROOT / "src/condemned2_input.h").read_text(encoding="utf-8")
graphics = (ROOT / "src/condemned2_graphics.cpp").read_text(encoding="utf-8")
workflow = (ROOT / ".github/workflows/build-input-hotfix.yml").read_text(encoding="utf-8")

def check(ok, detail):
    if not ok:
        raise AssertionError(detail)

def section(start, stop):
    check(start in source and stop in source, f"Source anchors missing: {start}/{stop}")
    return source.split(start, 1)[1].split(stop, 1)[0]

check("REXCVAR_DEFINE_BOOL(condemned2_input_legacy_mnk, true" in source,
      "Strict 0.2.1 mode must be the default")
check('REXLOG_INFO("[condemned2_input] MNK Input Driver Initialized");' in source,
      "The original 0.2.1 startup marker was lost")
check("std::make_unique<mnk::MnkInputDriver>" in source,
      "SDK MNK driver is missing")
check("std::make_unique<rex::input::condemned2input::Condemned2InputDriver>" in source,
      "Native camera driver is missing")

for name, value in {
    "keybind_lstick_up": "W", "keybind_lstick_down": "S",
    "keybind_lstick_left": "A", "keybind_lstick_right": "D",
    "keybind_a": "E", "keybind_b": "F", "keybind_x": "R",
    "keybind_y": "H", "keybind_left_trigger": "LMB",
    "keybind_right_trigger": "RMB", "keybind_rstick_press": "Space",
    "keybind_left_shoulder": "Shift+W", "keybind_right_shoulder": "G",
    "keybind_back": "Tab,Backspace", "keybind_start": "Esc,Return",
    "keybind_dpad_up": "Up,1", "keybind_dpad_down": "Down,2",
    "keybind_dpad_left": "Left,3", "keybind_dpad_right": "Right,4",
}.items():
    check(f'setGuestBind("{name}", "{value}");' in source,
          f"0.2.1 controller binding changed: {name}={value}")

device = section("X_RESULT Condemned2InputDriver::GetDeviceState(",
                 "void Condemned2InputDriver::UpdateNativeMouseLook()")
check("""if (REXCVAR_GET(condemned2_input_legacy_mnk)) {
        UpdateNativeMouseLook();
        move_dx = move_dy = 0;
        mouse_clicks = {};
      } else {
        PCControls();""" in device, "Direct native commands leak into strict 0.2.1 mode")
check("out_state->gamepad.buttons =" not in device,
      "Custom input device is injecting duplicate Xbox controller buttons")
check("out_state->gamepad.left_trigger =" not in device and
      "out_state->gamepad.right_trigger =" not in device,
      "Custom input device is injecting duplicate Xbox triggers")

mouse = section("void Condemned2InputDriver::UpdateNativeMouseLook()",
                "void Condemned2InputDriver::PCControls()")
check("SDL_GetRelativeMouseState(&dx, &dy);" in mouse and
      "last_dx = dx * sensitivity;" in mouse,
      "Native camera movement was changed")
check("void UpdateNativeMouseLook();" in header,
      "Native camera declaration is missing")

button = section("double GetButtonOverrideValue(uint32_t btn)",
                 "//Input Event Functions")
check("""if (REXCVAR_GET(condemned2_input_legacy_mnk))
      return 0;""" in button,
      "Native GetButton override must not compete with original controller input")
axes = section("double GetAxisOverrideValue(uint32_t btn)",
               "double GetButtonOverrideValue(uint32_t btn)")
check("btn != 0x0b && btn != 0x0c && btn != 0x16 && btn != 0x17" in axes,
      "Non-camera analog overrides may corrupt the original pad state")
check("REX_HOOK_RAW(HudSpectatorModeChange)" in graphics,
      "Checkpoint spectator-mode pickup-prompt hook missing")
check("on:\n  workflow_dispatch:" in workflow and "\n  push:" not in workflow,
      "GitHub Actions may run without the user's explicit signal")

print("PASS: 0.2.1 input contract, native camera, HUD prompt guard, manual-only CI")
