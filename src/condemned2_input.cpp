// condemned2recomp - ReXGlue Recompiled Project
//
// Condemned 2 Native Input Driver
//
// Handles native PC mouse and keyboard input, weapon/melee input behavior, TV antenna tuning behavior, gamepad passthrough.

#include <rex/hook.h>
#include <rex/input/flags.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/mnk/mnk_input_driver.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#include <cstring>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

#include <condemned2_hooks.h>
#include <condemned2_input.h>

// Native PC Binds
REXCVAR_DEFINE_STRING(condemned2_input_move_forward, "W", "Condemned 2/Input/Keyboard", "Forward");
REXCVAR_DEFINE_STRING(condemned2_input_move_backward, "S", "Condemned 2/Input/Keyboard", "Backward");
REXCVAR_DEFINE_STRING(condemned2_input_move_left, "A", "Condemned 2/Input/Keyboard", "Left");
REXCVAR_DEFINE_STRING(condemned2_input_move_right, "D", "Condemned 2/Input/Keyboard", "Right");
REXCVAR_DEFINE_STRING(condemned2_input_sprint, "Left Shift, Right Shift", "Condemned 2/Input/Keyboard", "Sprint");
REXCVAR_DEFINE_STRING(condemned2_input_walk, "Left Alt, Right Alt", "Condemned 2/Input/Keyboard", "Walk");

REXCVAR_DEFINE_STRING(condemned2_input_primary_fire, "Mouse1", "Condemned 2/Input/Keyboard", "Swing Left/Primary Fire");
REXCVAR_DEFINE_STRING(condemned2_input_secondary_fire, "Mouse2", "Condemned 2/Input/Keyboard", "Swing Right/Secondary Fire");
REXCVAR_DEFINE_STRING(condemned2_input_block_alt, "Mouse3", "Condemned 2/Input/Keyboard", "Block (alt)");
REXCVAR_DEFINE_STRING(condemned2_input_hook_attacks, "Q", "Condemned 2/Input/Keyboard", "Hook Attacks");
REXCVAR_DEFINE_STRING(condemned2_input_kick, "Space", "Condemned 2/Input/Keyboard", "Kick");

REXCVAR_DEFINE_STRING(condemned2_input_taser, "T", "Condemned 2/Input/Keyboard", "Taser");
REXCVAR_DEFINE_STRING(condemned2_input_throw, "G", "Condemned 2/Input/Keyboard", "Throw Item/Weapon");
REXCVAR_DEFINE_STRING(condemned2_input_drop, "B", "Condemned 2/Input/Keyboard", "Drop Item/Weapon, Holster Weapon");
REXCVAR_DEFINE_STRING(condemned2_input_slowmo, "M", "Condemned 2/Input/Keyboard", "Activates Slow-mo");

REXCVAR_DEFINE_STRING(condemned2_input_forensics, "R", "Condemned 2/Input/Keyboard", "Investigate/Use Forensic Tools");
REXCVAR_DEFINE_STRING(condemned2_input_tool_uv, "1", "Condemned 2/Input/Keyboard", "UV Light");
REXCVAR_DEFINE_STRING(condemned2_input_tool_camera, "2", "Condemned 2/Input/Keyboard", "Camera");
REXCVAR_DEFINE_STRING(condemned2_input_tool_spectrometer, "3", "Condemned 2/Input/Keyboard", "Spectrometer");
REXCVAR_DEFINE_STRING(condemned2_input_tool_gps, "4", "Condemned 2/Input/Keyboard", "GPS");
REXCVAR_DEFINE_STRING(condemned2_input_tool_take_picture, "Mouse1, E", "Condemned 2/Input/Keyboard", "Camera Shutter");
REXCVAR_DEFINE_STRING(condemned2_input_zoom, "Mouse2, V", "Condemned 2/Input/Keyboard", "Camera Zoom");

REXCVAR_DEFINE_STRING(condemned2_input_flashlight, "F", "Condemned 2/Input/Keyboard", "Flashlight");
REXCVAR_DEFINE_STRING(condemned2_input_use, "E", "Condemned 2/Input/Keyboard", "Use");
REXCVAR_DEFINE_STRING(condemned2_input_check, "H", "Condemned 2/Input/Keyboard", "Check");
REXCVAR_DEFINE_STRING(condemned2_input_objectives, "Tab", "Condemned 2/Input/Keyboard", "Current Objectives");

// Mouse Settings
REXCVAR_DEFINE_STRING(condemned2_input_mouse, "Mixed","Condemned 2/Input/Mouse", "Mouse Look Mode").allowed({"Native", "Mixed", "Disable"});
REXCVAR_DEFINE_DOUBLE(condemned2_input_mouse_sensitivity, 0.075, "Condemned 2/Input/Mouse", "Mouse Sensitivity").range(0.01, 1.0);

REXCVAR_DEFINE_BOOL(condemned2_input_debug, false, "Condemned 2/Debug", "Toggle logging for all input.");
REXCVAR_DEFINE_BOOL(condemned2_input_frame_debug, false, "Condemned 2/Debug", "Toggle logging for input per frame.");
// Log only player interaction transitions; this distinguishes injected input
// from gameplay command dispatch and avoids huge per-frame trace files.
REXCVAR_DEFINE_BOOL(condemned2_input_trace_use_grab, false, "Condemned 2/Debug", "Log use/grab guest input dispatch for missing pickup prompt investigation.");
// 0.2.1 had the standard MnkInputDriver alongside the native mouse hooks.
// Since 0.3.x, the game is missing pickup/grab prompts with keyboard/mouse,
// but real controller paths still work. Restore the SDK's actual keyboard
// device, not the one-off raw XInput A bridge that broke health kits.
REXCVAR_DEFINE_BOOL(condemned2_input_legacy_mnk, true, "Condemned 2/Input/Keyboard", "Restore the 0.2.1 keyboard gamepad input device for world pickup and scripted finishers. Native mouse camera stays enabled.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
// Native action commands handle keyboard/mouse interaction directly.
// Do not inject synthetic gamepad A/LT/RT: the regression in log 030 showed
// that this removed working health-pack pickup without restoring grabs.

//Native functions for inputs
REX_EXTERN(__imp__Input_KeyDown);
REX_EXTERN(__imp__Input_KeyHold);
REX_EXTERN(__imp__Input_KeyUp);
REX_EXTERN(__imp__Input_GetButton);
REX_EXTERN(__imp__Input_GetAxis);
REX_EXTERN(__imp__Input_PlayerInput);
REX_EXTERN(__imp__Input_OnAttackLanded);
REX_EXTERN(__imp__Input_InHud); // Returns true if in any menu.
REX_EXTERN(__imp__TvAntennaAdjust); // Function that processes Radio/TV antenna adjusting
REX_EXTERN(__imp__IsMeleeWeapon); // Checks if player's current weapon is a melee weapon

//Imported functions used for PC controls!
REX_IMPORT(Input_KeyDown, PressBind, uint32_t(uint32_t, uint32_t));
REX_IMPORT(Input_KeyHold, HoldBind, uint32_t(uint32_t, uint32_t));
REX_IMPORT(Input_KeyUp, ReleaseBind, uint32_t(uint32_t, uint32_t));
REX_IMPORT(Input_IsCommandEnabled, IsCommandEnabled, uint32_t(uint32_t, uint32_t));

namespace rex::input::condemned2input {
  namespace {constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x4E4F5000); constexpr uint32_t kInputManager = 0x4005ae70; constexpr uint32_t kInputManagerCRT = 0x200afcc0;}
  Condemned2InputDriver* g_condemned2_input_driver = nullptr;

  Condemned2InputDriver::Condemned2InputDriver(rex::ui::Window* window, size_t window_z_order) : InputDriver(window, window_z_order) {
    REXLOG_INFO("[condemned2_input] Starting...");
    g_condemned2_input_driver = this;

    //Compatibility with CRT since it offsets the input manager
    isUsingCRT = (rex::cvar::GetFlagByName("rexcrt_heap_enable") == "true" ? true : false);

    //Initialize PC binds
    for (std::string bindName : pcBinds) {
      rex::cvar::RegisterChangeCallback(bindName, [this](std::string_view name, std::string_view new_value) {
        activeBinds[std::string(name)] = ParseBind(std::string(new_value));
      });
      std::string val = rex::cvar::GetFlagByName(bindName);
      if(!val.empty()){
        activeBinds[bindName] = ParseBind(val);
      }
    }
    //Internal binds
    activeBinds["pause_menu"] = {{SDL_SCANCODE_ESCAPE}};
    activeBinds["menu_nav_up"] = {{SDL_SCANCODE_UP}};
    activeBinds["menu_nav_down"] = {{SDL_SCANCODE_DOWN}};
    activeBinds["menu_nav_left"] = {{SDL_SCANCODE_LEFT}};
    activeBinds["menu_nav_right"] = {{SDL_SCANCODE_RIGHT}};
    activeBinds["menu_enter"] = {{SDL_SCANCODE_RETURN}}; 
    activeBinds["menu_go_back"] = {{SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE}}; 

    //Initialize mouse look
    rex::cvar::RegisterChangeCallback("condemned2_input_mouse", [&](std::string_view name, std::string_view new_value) {
      SetMouseLookTypeFromString(std::string(new_value));
    });
    std::string val = rex::cvar::GetFlagByName("condemned2_input_mouse");
    if(!val.empty()){
      SetMouseLookTypeFromString(val);
    }

    //Initialize debug bool
    rex::cvar::RegisterChangeCallback("condemned2_input_debug", [&](std::string_view name, std::string_view new_value) {
      isDebugging = ( new_value == "true" ? true : false );
    });
    rex::cvar::RegisterChangeCallback("condemned2_input_frame_debug", [&](std::string_view name, std::string_view new_value) {
      isDebuggingFrames = ( new_value == "true" ? true : false );
    });

    //Register once ( Only even used for mouse wheel )
    SDL_AddEventWatch(&Condemned2InputDriver::WatchEventCallback, this);
  }

  Condemned2InputDriver::~Condemned2InputDriver(){
    SDL_RemoveEventWatch(&Condemned2InputDriver::WatchEventCallback, this);
  }

  X_STATUS Condemned2InputDriver::Setup() {
    return X_STATUS_SUCCESS;
  }

  void Condemned2InputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
    DeviceInfo info;
    info.id = kDevice;
    info.name = "Condemned 2 Input Driver";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT Condemned2InputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (out_caps) {
      std::memset(out_caps, 0, sizeof(*out_caps));
      out_caps->type = 0x01;
      out_caps->sub_type = 0x01;
      out_caps->flags = 0;
      out_caps->gamepad.buttons = 0xFFFF;
      out_caps->gamepad.left_trigger = 0xFF;
      out_caps->gamepad.right_trigger = 0xFF;
      out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
      out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
      out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
      out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
      out_caps->vibration.left_motor_speed = 0xFFFF;
      out_caps->vibration.right_motor_speed = 0xFFFF;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT Condemned2InputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT Condemned2InputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    return X_ERROR_EMPTY;
  }

  void Condemned2InputDriver::OnWindowAvailable(rex::ui::Window* window) {
    if (!window) return;

    if (attached_sdl_window_) {
      SDL_DestroyWindow(attached_sdl_window_);
      attached_sdl_window_ = nullptr;
    }

    attached_window_ = window;
    void* native = attached_window_->GetNativeWindowHandle();
    if (native) {
      SDL_PropertiesID props = SDL_CreateProperties();
    #if REX_PLATFORM_WIN32
      SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, native);
    #elif REX_PLATFORM_LINUX
      SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER, (Sint64)reinterpret_cast<uintptr_t>(native));
    #endif
      attached_sdl_window_ = SDL_CreateWindowWithProperties(props);
      SDL_DestroyProperties(props);
    }

    if (!attached_sdl_window_) {
      REXLOG_INFO("[condemned2_input] Failed to create SDL Window!");
    }else if (attached_sdl_window_) {
      SDL_RaiseWindow(attached_sdl_window_);
    }
  }

  std::unique_ptr<InputSystem> CreateInputSystem(bool tool_mode, Condemned2::Condemned2Hook *hook) {
    auto input = std::make_unique<InputSystem>(nullptr);
    //Used for gamepad!
    auto sdl_driver = std::make_unique<sdl::SDLInputDriver>(nullptr, 0);
    if (sdl_driver->Setup() == X_STATUS_SUCCESS) {
      input->AddDriver(std::move(sdl_driver));
      REXLOG_INFO("[condemned2_input] SDL Input Driver Initialized");
    }

    // Restore the exact SDK MnkInputDriver class used by v0.2.1, while
    // preserving the native v0.6 mouse camera and forensic input driver.
    // Avoid all other default Mnk keybinds, which conflict with E, Space,
    // R, WASD, the forensic number keys and arrow-key evidence navigation.
    // The SDK merges synthetic devices into player 0 via SlotAssignment.
    if (REXCVAR_GET(condemned2_input_legacy_mnk)) {
      auto setGuestBind = [](const char* name, const char* value) {
        if (!rex::cvar::SetFlagByName(name, value)) {
          REXLOG_ERROR("[c2-legacy-mnk] unable to set SDK bind {}", name);
        }
      };
      setGuestBind("mnk_mode", "true");
      setGuestBind("mnk_mouse", "false");
      const char* unusedPadBinds[] = {
        "keybind_b", "keybind_x", "keybind_y",
        "keybind_left_shoulder", "keybind_right_shoulder",
        "keybind_lstick_up", "keybind_lstick_down",
        "keybind_lstick_left", "keybind_lstick_right",
        "keybind_lstick_press", "keybind_rstick_up",
        "keybind_rstick_down", "keybind_rstick_left",
        "keybind_rstick_right", "keybind_rstick_press",
        "keybind_dpad_up", "keybind_dpad_down",
        "keybind_dpad_left", "keybind_dpad_right",
        "keybind_back", "keybind_start", "keybind_guide"
      };
      for (const char* key : unusedPadBinds) setGuestBind(key, "");
      setGuestBind("keybind_a", "E");
      // X is the controller's supported forensic-tools command. The 0.6
      // custom driver directly injected 0x74 and 0x71 together, with extra
      // tool commands from 1..4, leaving the guest's tool/interaction state
      // out of sync after changing GPS/camera (upstream issues #66/#71).
      setGuestBind("keybind_x", "R");
      setGuestBind("keybind_left_trigger", "LMB");
      setGuestBind("keybind_right_trigger", "RMB");

      auto mnkDriver = std::make_unique<mnk::MnkInputDriver>(nullptr, 0);
      if (mnkDriver->Setup() == X_STATUS_SUCCESS) {
        input->AddDriver(std::move(mnkDriver));
        REXLOG_INFO("[c2-legacy-mnk] v0.2.1 controller input: E=A, R=X (forensics), LMB=LT, RMB=RT; legacy guest trigger routing active");
      } else {
        REXLOG_ERROR("[c2-legacy-mnk] SDK keyboard driver setup failed");
      }
    }

    //Condemned 2 Custom Input Driver
    auto cndmned2_driver = std::make_unique<rex::input::condemned2input::Condemned2InputDriver>(nullptr, 0); 
    if (cndmned2_driver->Setup() == X_STATUS_SUCCESS) {
      cndmned2_driver->SetHook(hook);
      input->AddDriver(std::move(cndmned2_driver));
      REXLOG_INFO("[condemned2_input] Condemned 2 Input Driver Initialized");
    }
    input->SetDeviceAssignment(std::make_unique<SlotAssignment>());
    return input;
  }
  
  bool Condemned2InputDriver::IsAnyDialogOpen(){
    if(activeWindows.size() < windowNames.size()){
      for (const std::string& windowName : windowNames){
        auto it = activeWindows.find(windowName);
        if (it != activeWindows.end() && it->second != nullptr)
          continue;

        ImGuiWindow* newWindow = ImGui::FindWindowByName(windowName.c_str());
        if(newWindow != nullptr){
          activeWindows[windowName] = newWindow;
        }
      }
    }
    for (const auto& [windowName, activeWindow] : activeWindows) {
      if ( activeWindow != nullptr && activeWindow->WasActive ){
        return true;
      }
    }
    return false;
  }

  X_RESULT Condemned2InputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }

    // The host input system merges real and synthetic controllers; never
    // return an uninitialized X_INPUT_STATE (including the first frame).
    if (out_state) {
      std::memset(out_state, 0, sizeof(*out_state));
    }
    //Wait one frame for native input system to initialize
    if(!blockFirstFrame){
      blockFirstFrame = true;
      return X_ERROR_SUCCESS;
    }

    //Toggles mouse/cursor control when settings is active.
    if(attached_window_ && attached_sdl_window_){
      bool isDialogCurrentlyOpen = IsAnyDialogOpen();
      if( isDialogOpen != isDialogCurrentlyOpen){
        isDialogOpen = isDialogCurrentlyOpen;
        attached_window_->SetCursorVisibility( isDialogOpen ? rex::ui::Window::CursorVisibility::kVisible : rex::ui::Window::CursorVisibility::kHidden);
        SDL_SetWindowRelativeMouseMode(attached_sdl_window_, !isDialogOpen);
        SDL_CaptureMouse(!isDialogOpen);
      }
    }

    //PC Controls aren't active with any dialog is open.
    if(!isDialogOpen){
      PCControls();
    }else{
      //Reset all movement when a dialog is active
      move_dx = 0;
      move_dy = 0;
      last_dx = 0;
      last_dy = 0;
      last_add_dx = 0;
      last_add_dy = 0;
      // Drop stale input edges when PC overlays take focus.
      for (auto& [name, bind] : activeBinds) bind.prev = false;
    }

    // Dispatch once per device update so hold/release transitions cannot latch.
    FlushNativeKeys();
    if (!isDialogOpen) {
      // Only log transitions: do not flood a gameplay session with per-frame
      // hook traces. This distinguishes input-state loss from missing UI art.
      const uint32_t state =
          (IsInMenu() ? 1u : 0u) |
          (IsNativeCommandEnabled(0x57) ? 2u : 0u) |
          (IsNativeCommandEnabled(0x40) ? 4u : 0u) |
          (IsNativeCommandEnabled(0x70) ? 8u : 0u) |
          (IsNativeBindDown("condemned2_input_use") ? 16u : 0u) |
          (IsNativeBindDown("condemned2_input_forensics") ? 32u : 0u) |
          ((mouse_clicks.isMouseLeft && mouse_clicks.isMouseRight) ? 64u : 0u);
      if (!actionStateSnapshotInitialized_ || state != actionStateSnapshot_) {
        REXLOG_INFO("[c2-input-state] hud={} use57={} grab40={} forensicReturn70={} keyUse={} keyTools={} dualTrigger={}",
                    (state & 1u)!=0, (state & 2u)!=0, (state & 4u)!=0,
                    (state & 8u)!=0, (state & 16u)!=0, (state & 32u)!=0,
                    (state & 64u)!=0);
        actionStateSnapshot_ = state;
        actionStateSnapshotInitialized_ = true;
      }
    }

    // Device-state fallback for the checkpoint interaction regression:
    // The UI-event-driven SDK MnkInputDriver can miss physical key events
    // when SDL captures the pointer. XInput state from this custom device is
    // merged (OR/max) with Mnk's state by InputSystem, so duplicating these
    // exact buttons does not produce duplicate guest commands. In contrast,
    // NativeKeyPress(Use/Grab) delivered independent, conflicting commands.
    // This preserves the working health-kit path and needs no real gamepad.
    if (out_state && !isDialogOpen && REXCVAR_GET(condemned2_input_legacy_mnk)) {
      // Recreate the same complete controller button state even if the SDK
      // UI listener misses input while SDL owns the mouse capture.
      uint16_t buttons = 0;
      if (IsNativeBindDown("condemned2_input_use"))
        buttons |= X_INPUT_GAMEPAD_A;
      if (IsNativeBindDown("condemned2_input_forensics"))
        buttons |= X_INPUT_GAMEPAD_X;
      out_state->gamepad.buttons = buttons;
      if (IsNativeBindDown("condemned2_input_primary_fire")) {
        out_state->gamepad.left_trigger = 0xFF;
      }
      if (IsNativeBindDown("condemned2_input_secondary_fire")) {
        out_state->gamepad.right_trigger = 0xFF;
      }
    }

    //Update controller
    packet_number_++; // Increment input packet number
    if (out_state) {
      out_state->packet_number = packet_number_;
    }
    return X_ERROR_SUCCESS;
  }

  void Condemned2InputDriver::PCControls(){
    //Left Stick/Movement
    double lx = 0;
    double ly = 0;
    double moveScale = 1;
    if(IsDown(activeBinds["condemned2_input_walk"])){ // Walk
      moveScale = 0.5;
    }else{ //buttons |= X_INPUT_GAMEPAD_LEFT_SHOULDER
      Held({activeBinds["condemned2_input_sprint"]}, {0x10} );
    }
    if(IsDown(activeBinds["condemned2_input_move_forward"])) ly = moveScale;
    if(IsDown(activeBinds["condemned2_input_move_backward"])) ly = -moveScale;
    if(IsDown(activeBinds["condemned2_input_move_right"])) lx = moveScale;
    if(IsDown(activeBinds["condemned2_input_move_left"])) lx = -moveScale;
    move_dx = lx;
    move_dy = ly;
    
    // Mouse buttons
    bool hasMeleeWeapon = IsUsingMeleeWeapon();
    bool isMouseLeftPressed = Pressed(activeBinds["condemned2_input_primary_fire"]);
    bool isMouseRightPressed = Pressed(activeBinds["condemned2_input_secondary_fire"]);
    bool isMouseLeft = IsDown(activeBinds["condemned2_input_primary_fire"]);
    bool isMouseRight = IsDown(activeBinds["condemned2_input_secondary_fire"]);
    mouse_clicks = { 
      !hasMeleeWeapon ? isMouseRight : isMouseLeft, 
      !hasMeleeWeapon ? isMouseLeft : isMouseRight, 
      IsDown(activeBinds["condemned2_input_block_alt"]), 
      !hasMeleeWeapon ? isMouseRightPressed : isMouseLeftPressed, 
      !hasMeleeWeapon ? isMouseLeftPressed : isMouseRightPressed 
    };
    // The gamepad sends trigger DOWN, HOLD, and UP. Sending only a one-frame
    // mouse click prevents its dual-trigger (LT+RT) grab/finisher path from
    // seeing both triggers held together.
    if(mouse_clicks.isMouseLeft){
      NativeKeyPress({0x47, 0x79, 0x1c});
    }
    if(mouse_clicks.isMouseRight){
      NativeKeyPress({0x11, 0x7a});
    }
    if(mouse_clicks.isMouseRightPressed){
      NativeKeyPress({0xbc}); // NoteInputTime is an edge, not a held trigger.
      // Only the active forensic task may accept CancelTask.
      if (IsNativeCommandEnabled(0x65))
        NativeKeyPress({0x65});
    }
    // Real Xbox finisher: LT+RT together. Expose it for keyboard/mouse as
    // simultaneous LMB+RMB; preserve E/Q as optional contextual aliases.
    // Dual-trigger grab is a deliberate LMB+RMB chord; send the guest
    // event unconditionally, even if IsCommandEnabled reports false.
    // This does not fire for ordinary one-button combat.
    // The SDK MnK controller already reports LT+RT to the game via
    // XInputGetState, matching the working 0.2.1 path. Do not also dispatch
    // an unrelated native Grab command: double delivery changes the game's
    // interaction state and can cancel an otherwise valid finisher.
    if (!REXCVAR_GET(condemned2_input_legacy_mnk) &&
        isMouseLeft && isMouseRight)
      NativeKeyPress({0x40});
    //Mouse movement
    float dx = 0.0f, dy = 0.0f;
    SDL_GetRelativeMouseState(&dx, &dy);
    double sensitivity = REXCVAR_GET(condemned2_input_mouse_sensitivity);
    // Mouse look
    last_dx = dx * sensitivity;
    last_dy = dy * sensitivity;
    // Additive mouse delta
    last_add_dx += last_dx * 0.01f;
    last_add_dy += -last_dy * 0.01f;
    last_add_dx = std::clamp(last_add_dx, -1.0, 1.0);
    last_add_dy = std::clamp(last_add_dy, -1.0, 1.0);

    // Only arrow keys and mouse wheel control navigation / forensic choices.
    // WASD movement MUST NOT inject menu commands (0x82..0x8e) in gameplay,
    // otherwise changing direction can alter the active forensic command state.
    // Navigation
    Held({activeBinds["menu_nav_up"]}, {0x82}, wheelY > 0.0f);
    Held({activeBinds["menu_nav_down"]}, {0x83}, wheelY < 0.0f);
    Held({activeBinds["menu_nav_right"]}, {0x85, 0x8e} );
    Held({activeBinds["menu_nav_left"]}, {0x84, 0x8d} );

    // Context-sensitive A actions: the native PC path originally delivered
    // several A aliases on E/Enter. In #21-25 these were restricted to HUD
    // menus, which also broke health kits showing an A prompt. They are
    // different command sets, not duplicate invocations of the same action:
    // 0x57 = world Use, 0x87 = menu A, 0xa7 = in-game A confirm,
    // 0xb5/0xb7 = dialogue A, 0x5d = decision A, 0xa3 = terminal A.
    // IDs come from the reviewed XEX PlayerInput/CPlayerUI action mapping.
    // Crucially, do NOT inject 0xa6 / 0x5b (Y), or 0xba (B) on E: those
    // cross-button aliases can activate an unrelated command set.
    const bool usePressed = Pressed(activeBinds["condemned2_input_use"]);
    const bool enterPressed = Pressed(activeBinds["menu_enter"]);
    if (usePressed || enterPressed) {
      NativeKeyPress({0x87, 0xa7, 0xb5, 0xb7, 0x5d, 0xa3});
      REXLOG_INFO("[c2-use-aliases] E/Enter -> world A context aliases; hud={}", IsInMenu());
    }
    // Use is a hold action: scanning and scripted moves require genuine
    // DOWN/HOLD/UP transitions. Keep the normal XInput A state in parallel.
    Held({activeBinds["condemned2_input_use"], activeBinds["menu_enter"]}, {0x57});

    //Menu back
    if(Pressed(activeBinds["menu_go_back"])){ //buttons |= X_INPUT_GAMEPAD_B;
      NativeKeyPress({0x88, 0xba}); // menu back; dismiss tutorial
      if (IsNativeCommandEnabled(0x65)) // cancel only a currently enabled task
        NativeKeyPress({0x65});
    }

    //Actions
    if(Pressed(activeBinds["pause_menu"])){ //buttons |= X_INPUT_GAMEPAD_START
      NativeKeyPress({0xd});
    }
    if(Pressed(activeBinds["condemned2_input_flashlight"])){ //buttons |= X_INPUT_GAMEPAD_B;
      NativeKeyPress({0x72,0x8c});
    }
    if(Pressed(activeBinds["condemned2_input_check"])){
      NativeKeyPress({0x3d});
    }
    if(Pressed(activeBinds["condemned2_input_kick"])){ //buttons |= X_INPUT_GAMEPAD_RIGHT_THUMB;
      NativeKeyPress({0x5a});
    }
    if(Pressed(activeBinds["condemned2_input_throw"])){ //buttons |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
      NativeKeyPress({0x3f});
    }
    if(Pressed(activeBinds["condemned2_input_drop"])){ //buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;
      NativeKeyPress({0x66});
    }
    if(Pressed(activeBinds["condemned2_input_taser"])){ //buttons |= X_INPUT_GAMEPAD_DPAD_UP
      NativeKeyPress({0x3e});
    }
    if(IsDown(activeBinds["condemned2_input_hook_attacks"])){
      NativeKeyPress({0x42, 0xa8});
      // CS_Grab / CS_DoubleGrab: only in contexts where the guest enables it.
      // Do not gate a physical Q action on IsCommandEnabled: the guest
      // reported false at the exact time the clown finisher was attempted.
      if (!REXCVAR_GET(condemned2_input_legacy_mnk))
        NativeKeyPress({0x40});
    }
    if(!IsDown(activeBinds["condemned2_input_slowmo"]) && activeBinds["condemned2_input_slowmo"].prev){
      _hooks->SendCommandCmd({_hooks->AllocateHeapForString("msg Player (slowmo off)"), 0, 0, 0});
    }
    if(Pressed(activeBinds["condemned2_input_slowmo"]) ){
      _hooks->SendCommandCmd({_hooks->AllocateHeapForString("msg Player (slowmo on default)"), 0, 0, 0});
    } 

    // Contextual grab/finisher can also be triggered with the standard E key;
    // the game chooses the enabled action, while a single mapped key remains
    // usable for pickup and scripted quick-time interactions.
    // E remains ordinary Use. Do not synthesize Grab from E; those are
    // separate actions and E worked for medkits before the A bridge.

    Held({activeBinds["condemned2_input_objectives"]}, {0x4f, 0x4e} );
    // Native forensic commands were a regression introduced after 0.2.1.
    // On keyboard, X and the tool-wheel's internal 0x71/0x70 transition were
    // fired simultaneously, sometimes leaving the game in the wrong command
    // set and disabling weapon pickup and scripted QTE prompts.
    //
    // The legacy path now uses R -> X through the merged XInput state (same
    // command lifecycle as a real controller). Do not also send tool commands
    // directly. The older native-only path remains available when legacy MnK
    // is disabled.
    if (!REXCVAR_GET(condemned2_input_legacy_mnk)) {
      if (pendingForensicsReturn_ && !IsNativeBindDown("condemned2_input_forensics")) {
        NativeKeyPress({0x70});
        pendingForensicsReturn_ = false;
      }
      const bool forensicWasDown = activeBinds["condemned2_input_forensics"].prev;
      Held({activeBinds["condemned2_input_forensics"]}, {0x74, 0x71});
      if (forensicWasDown && !IsNativeBindDown("condemned2_input_forensics"))
        pendingForensicsReturn_ = true;
    } else {
      pendingForensicsReturn_ = false;
      // On a real controller, releasing X closes the forensic wheel; the
      // guest issues ForensicsReturn (0x70) as part of its context transition.
      // The native keyboard variant used by the validated input experiments
      // explicitly sent that transition on key release. Preserve it here
      // while keeping the SDK's normal X-down/X-up state for R.
      const bool toolsDown = IsNativeBindDown("condemned2_input_forensics");
      if (forensicWheelWasDown_ && !toolsDown) {
        NativeKeyPress({0x70});
        REXLOG_INFO("[c2-forensic-transition] R released; ForensicsReturn(0x70)");
      }
      forensicWheelWasDown_ = toolsDown;
    }
    // The camera shutter defaults to Mouse1,E, and zoom to Mouse2,V. Without
    // command-set checks they also fire during melee, weapon pickup and QTE,
    // even when forensic-camera mode is inactive. These overlapping controls
    // must not inject camera actions into ordinary gameplay.
    const bool cameraShutterPressed = Pressed(activeBinds["condemned2_input_tool_take_picture"]);
    if (cameraShutterPressed && IsNativeCommandEnabled(0xb9)) {
      NativeKeyPress({0xb9});
    }
    if (IsNativeCommandEnabled(0x8b) &&
        IsDown(activeBinds["condemned2_input_zoom"])) {
      NativeKeyPress({0x8b});
    }

    // Fast-select 1..4 used to bypass the actual wheel-open/close controller
    // sequence. That is precisely the input-state loss reported in issues
    // #66/#71. Disable only these unsafe shortcuts while the stable 0.2.1
    // X-button path is selected; the forensic tool wheel remains usable via R
    // and the existing arrow-key navigation.
    if (!REXCVAR_GET(condemned2_input_legacy_mnk)) {
      if (Pressed(activeBinds["condemned2_input_tool_uv"])) {
        NativeKeyPress({0x71,0x90,0x96,0xa0});
        pendingForensicsReturn_ = true;
      }
      if (Pressed(activeBinds["condemned2_input_tool_camera"])) {
        NativeKeyPress({0x71,0x98,0x9e,0xa0});
        pendingForensicsReturn_ = true;
      }
      if (Pressed(activeBinds["condemned2_input_tool_spectrometer"])) {
        NativeKeyPress({0x74,0x71,0x97,0xa1});
        pendingForensicsReturn_ = true;
      }
      if (Pressed(activeBinds["condemned2_input_tool_gps"])) {
        NativeKeyPress({0x74,0x71,0x99,0x9f});
        pendingForensicsReturn_ = true;
      }
    }
    g_condemned2_input_driver->SetMouseWheelY(0); //Reset wheel after a frame.
  }
  
  bool SDLCALL Condemned2InputDriver::WatchEventCallback(void* userdata, SDL_Event* event){
    switch (event->type) {
      case SDL_EVENT_MOUSE_WHEEL:
        g_condemned2_input_driver->SetMouseWheelY(event->wheel.y, true);
      break;
    }
    return true;
  }

  //Custom bind system
  Bind Condemned2InputDriver::ParseBind(const std::string& s) {
    int curBind = 0;
    Bind newBind;
    std::stringstream ss(s);
    std::string token;
    while (getline(ss, token, ',')) {
      if (token.empty()) continue;
      if (curBind >= Bind::kMaxKeys) break;
      token.erase(0, token.find_first_not_of(' '));
      token.erase(token.find_last_not_of(' ') + 1);
      if (token == "Mouse1") newBind.mouseMask |= SDL_BUTTON_LMASK;
      else if (token == "Mouse2") newBind.mouseMask |= SDL_BUTTON_RMASK;
      else if (token == "Mouse3") newBind.mouseMask |= SDL_BUTTON_MMASK;
      else if (token == "Mouse4") newBind.mouseMask |= SDL_BUTTON_X1MASK;
      else if (token == "Mouse5") newBind.mouseMask |= SDL_BUTTON_X2MASK;
      newBind.keys[curBind++] = SDL_GetScancodeFromName(token.c_str());
    }
    return newBind;
  }

  bool Condemned2InputDriver::IsDown(const Bind &b) {
      if(isDialogOpen){
        return false;
      }
      if (b.mouseMask && SDL_GetMouseState(nullptr, nullptr) & b.mouseMask){
        return true;
      }
      int keyCount = 0;
      const bool* state = SDL_GetKeyboardState(&keyCount);
      if(!state  || keyCount <= 0 ){
        return false;
      }
      for (SDL_Scancode k : b.keys) {
        if(k != SDL_SCANCODE_UNKNOWN && state[k]) return true;
      }
      return false;
  }

  bool Condemned2InputDriver::Pressed(Bind& b) {
    bool held = !isDialogOpen && IsDown(b);
    bool edge = held && !b.prev;
    b.prev = held;
    return edge;
  }

  // The guest must receive a KeyDown once, KeyHold while held, and KeyUp on release.
  // Don't consume Pressed() edges here: other actions can already have examined E.
  void Condemned2InputDriver::Held(std::initializer_list<std::reference_wrapper<Bind>> binds,
                                  std::vector<int32_t> keycodes, bool pressed) {
    bool active = pressed;
    for (Bind& bind : binds) {
      const bool isDown = IsDown(bind);
      bind.prev = isDown;
      active = active || isDown;
    }
    if (active) NativeKeyPress(std::move(keycodes));
  }

  // Preserve the original order of the native input codes for forensic quick-swap/QTE.
  void Condemned2InputDriver::NativeKeyPress(std::vector<int32_t> keycodes) {
    for (int32_t key : keycodes) {
      if (std::find(requestedNativeKeys.begin(), requestedNativeKeys.end(), key) == requestedNativeKeys.end())
        requestedNativeKeys.push_back(key);
    }
  }

  void Condemned2InputDriver::FlushNativeKeys() {
    const uint32_t keyManager = GetInputManager();
    // Release keys absent from this update, including forensic tools and one-shot taps.
    for (int32_t key : activeNativeKeys) {
      if (std::find(requestedNativeKeys.begin(), requestedNativeKeys.end(), key) == requestedNativeKeys.end()) {
        if (key == 0x57 || (REXCVAR_GET(condemned2_input_trace_use_grab) && key == 0x40))
          REXLOG_INFO("[c2-native-action] UP id={:#x}", key);
        ReleaseBind(keyManager, key);
      }
    }
    for (int32_t key : requestedNativeKeys) {
      if (std::find(activeNativeKeys.begin(), activeNativeKeys.end(), key) == activeNativeKeys.end()) {
        if (key == 0x57 || (REXCVAR_GET(condemned2_input_trace_use_grab) && key == 0x40))
          REXLOG_INFO("[c2-native-action] DOWN id={:#x} enabled_hint={}", key, IsNativeCommandEnabled(key));
        PressBind(keyManager, key);
      }
      HoldBind(keyManager, key);
    }
    activeNativeKeys.swap(requestedNativeKeys);
    requestedNativeKeys.clear();
  }
  
  bool Condemned2InputDriver::IsNativeCommandEnabled(uint32_t action) {
    return (IsCommandEnabled(GetInputManager(), action) & 0xff) != 0;
  }

  // Query the original native keyboard state without copying the entire bind map.
  bool Condemned2InputDriver::IsNativeBindDown(const char* bindName) {
    const auto it = activeBinds.find(bindName);
    return it != activeBinds.end() && IsDown(it->second);
  }

  uint32_t Condemned2InputDriver::GetInputManager(){
    // Prefer the input manager pointer observed in the guest's own GetButton
    // calls. The old cvar-only guess is wrong if the RexCRT layout and cvar
    // initialization order diverge; injected Use/Grab then reach a different
    // manager than the one the game polls. Only accept the two already
    // established Condemned 2 manager addresses (never arbitrary pointers).
    const uint32_t live = observedInputManager_.load(std::memory_order_relaxed);
    if (live == kInputManager || live == kInputManagerCRT)
      return live;
    return isUsingCRT ? kInputManagerCRT : kInputManager;
  }

  void Condemned2InputDriver::ObserveInputManager(uint32_t candidate, uint32_t action) {
    if (action != 0x57 && action != 0x40)
      return;
    if (candidate != kInputManager && candidate != kInputManagerCRT)
      return;
    const uint32_t old = observedInputManager_.exchange(candidate, std::memory_order_relaxed);
    if (old != candidate) {
      REXLOG_INFO("[c2-input-manager] game GetButton({:#x}) uses {:#x}; legacy guessed {:#x}",
                  action, candidate, isUsingCRT ? kInputManagerCRT : kInputManager);
    }
  }

  bool Condemned2InputDriver::IsInMenu() { 
    if(!isInMenu){
      rex::memory::Memory *hookMemory = _hooks->GetMemory(); 
      if(hookMemory){
        uint8_t* gameState = hookMemory->TranslateVirtual<uint8_t*>(0x82936328);
        if(gameState){
          int newGameState = _hooks->get_int(gameState);
          if(newGameState != 2){
            return true;
          }
        }
      }
    }
    return isInMenu; 
  }

  //Resolves rex string cvars to mouse look enum!
  void Condemned2InputDriver::SetMouseLookTypeFromString( std::string mouseMode ){
    // Mouse look
    if (mouseMode == "Disable"){
      mouseLookType = MouseLookType::DISABLED;
    }else if (mouseMode == "Native"){
      mouseLookType = MouseLookType::NATIVE;
    }else if (mouseMode == "Mixed"){
      mouseLookType = MouseLookType::MIXED;
    }
  }
  
  double GetAxisOverrideValue(uint32_t btn){
    //Left Stick Handling / Movement Velocity
    std::pair<double,double> moveVel = g_condemned2_input_driver->MoveVel();
    if (btn == 0x2 && moveVel.second != 0){
      return moveVel.second;
    }else if (btn == 0x5 && moveVel.first != 0){
      return moveVel.first;
    }

    // Native Mouse Input
    if (g_condemned2_input_driver->GetMouseLookType() == MouseLookType::DISABLED
    || !g_condemned2_input_driver->IsUsingPCControls() ){
      return 0;
    }

    // Preserve pre-#23 native mouse analog overrides used for working punches.
    MouseBind mouseClick = g_condemned2_input_driver->MouseClick();
    if(mouseClick.isMouseMiddle && g_condemned2_input_driver->IsUsingMeleeWeapon()){ //Alt block
      if (btn == 0x79 || btn == 0x7a){
        return 1.0;
      }
    }
    else if (btn == 0x79 || btn == 0x47){ // Left click
      return mouseClick.isMouseLeft;
    }else if (btn == 0x7a){ // Right click / combo button
      return mouseClick.isMouseRight;
    }else if (btn == 0x65){ // CancelTask only if the guest explicitly enables it
      return g_condemned2_input_driver->IsNativeCommandEnabled(0x65) && mouseClick.isMouseRight;
    }

    // Radio/TV Antenna Tuning
    // This uses a soft additive mouse input so users can feel slight weight in adjusting antennas.
    if( g_condemned2_input_driver->IsUsingTuningControls() ){
      std::pair<double,double> accumMouse = g_condemned2_input_driver->AccumMouse();
      if (btn == 0x7B) {
        return accumMouse.first;
      }
      if (btn == 0x7C) {
        return accumMouse.second;
      }
    }
    if ( btn == 0x7B || btn == 0x7C ){
      return 0;
    }

    // Mouse Look
    // Overriding right stick input with raw mouse input
    std::pair<double,double> rawMouse = g_condemned2_input_driver->RawMouse();
    if (g_condemned2_input_driver->GetMouseLookType() == MouseLookType::NATIVE){ // Restored dev/unused mouse look function in Condemned 2 ( which introduces its own smoothing and sensitivity )
      if ( btn == 0x17 || btn == 0x16 ){
        return 0;
      }
      if (btn == 0xC ) {
        return rawMouse.first;
      }
      if (btn == 0xB ) {
        return rawMouse.second;
      }
    }else if (g_condemned2_input_driver->GetMouseLookType() == MouseLookType::MIXED){ // Overrides gamepad input to use raw mouse input, retaining more subtle behaviors from the gamepad input.
      if ( btn == 0xC || btn == 0xB ){
        return 0;
      }
      if (btn == 0x17 ) {
        return rawMouse.first;
      }
      if (btn == 0x16 ) {
        return rawMouse.second;
      }
    }
    return 0;
  }

  double GetButtonOverrideValue(uint32_t btn){
    // World interaction is a keyboard action, not a mouse-look action.
    // After calling the guest's Input_GetButton, supply 0x57 when E is held
    // irrespective of legacy_mnk or mouse-look mode. Original gamepad state
    // remains untouched when the host keyboard key is not down.
    if (btn == 0x57 && g_condemned2_input_driver->IsUsingPCControls() &&
        g_condemned2_input_driver->IsNativeBindDown("condemned2_input_use"))
      return 1.0;
    if (g_condemned2_input_driver->GetMouseLookType() == MouseLookType::DISABLED
    || !g_condemned2_input_driver->IsUsingPCControls() )
      return 0;

    // v0.2.1 received both keyboard edge events and button polling from the
    // SDK's MnkInputDriver. The v0.6 native input driver only injected events,
    // so the original Input_GetButton path never saw E / scripted grabs.
    // Return digital button levels for the same guest commands as key events.
    // Unlike a permission/availability hint, an explicit player button
    // press must be delivered to the engine even if Input_IsCommandEnabled
    // currently says false (as seen throughout mission_04 in log 030).
    // With the v0.2.1 keyboard controller restored, use the guest's original
    // button state instead of overlaying a separate native command state.
    if (!REXCVAR_GET(condemned2_input_legacy_mnk) && btn == 0x40) {
      const MouseBind triggers = g_condemned2_input_driver->MouseClick();
      return (triggers.isMouseLeft && triggers.isMouseRight) ||
             g_condemned2_input_driver->IsNativeBindDown("condemned2_input_hook_attacks");
    }

    //Sprint
    if( btn == 0x10 && g_condemned2_input_driver->GetActiveBinds()["condemned2_input_sprint"].prev){
      return 1;
    }

    //Is selecting forensic tool
    if( btn == 0x74 && g_condemned2_input_driver->GetActiveBinds()["condemned2_input_forensics"].prev){
      return 1;
    }

    //Attacks ( Left and Right Buttons )
    MouseBind mouseClick = g_condemned2_input_driver->MouseClick();
    bool isMiddleClicked = false;
    if(g_condemned2_input_driver->IsUsingMeleeWeapon()){ //Alt block
      isMiddleClicked = mouseClick.isMouseMiddle;
    }
    if (btn == 0x65) {
      return g_condemned2_input_driver->IsNativeCommandEnabled(0x65)
        && (mouseClick.isMouseRight || mouseClick.isMouseRightPressed);
    }
    if (btn == 0x11 || btn == 0x7a ) {
      return mouseClick.isMouseRight || mouseClick.isMouseRightPressed || isMiddleClicked;
    }else if(btn == 0x1c || btn == 0x47 || btn == 0x79){ 
      return mouseClick.isMouseLeft || mouseClick.isMouseLeftPressed || isMiddleClicked;
    }
    return 0;
  }

  //Input Event Functions
  REX_HOOK_RAW(Input_GetAxis) {
    uint32_t newID = ctx.r4.u32;
    double newValue = GetAxisOverrideValue(newID);
    __imp__Input_GetAxis(ctx, base);
    if( newValue != 0 ){
      ctx.f1.f64 = newValue;
    }
    //if ( ctx.f1.f64 != 0 ){
    //  REXLOG_INFO("[condemned2_input][Input_GetAxis] {:#x} {:#x} {}", newID, ctx.r4.u32, ctx.f1.f64);
    //}
  }
  REX_HOOK_RAW(Input_KeyDown){
    if(g_condemned2_input_driver->IsDebugging()){
      REXLOG_INFO("[condemned2_input][Input_KeyDown] {:#x} {:#x}", ctx.r3.u32, ctx.r4.u64);
    }
    __imp__Input_KeyDown(ctx, base);
  }
  REX_HOOK_RAW(Input_KeyHold){
    if(g_condemned2_input_driver->IsDebugging()){
      REXLOG_INFO("[condemned2_input][Input_KeyHold] {:#x} {:#x}", ctx.r3.u32, ctx.r4.u64);
    }
    __imp__Input_KeyHold(ctx, base);
  }
  REX_HOOK_RAW(Input_KeyUp){
    if(g_condemned2_input_driver->IsDebugging()){
      REXLOG_INFO("[condemned2_input][Input_KeyUp] {:#x} {:#x}", ctx.r3.u32, ctx.r4.u64);
    }
    __imp__Input_KeyUp(ctx, base);
  }
  REX_HOOK_RAW(Input_GetButton){
    uint32_t temp = ctx.r3.u32;
    uint32_t tempB = ctx.r4.u32;
    if (g_condemned2_input_driver)
      g_condemned2_input_driver->ObserveInputManager(temp, tempB);
    __imp__Input_GetButton(ctx, base);
    const uint32_t originalValue = ctx.r3.u32;
    double newValue = GetButtonOverrideValue(tempB);
    if(newValue != 0){
      ctx.r3.u64 = static_cast<uint64_t>(newValue);
      if (REXCVAR_GET(condemned2_input_trace_use_grab) &&
          (tempB == 0x57 || tempB == 0x40))
        REXLOG_INFO("[c2-poll-action] id={:#x} original={} override={}", tempB, originalValue, ctx.r3.u32);
    }
    if(g_condemned2_input_driver->IsDebuggingFrames()){
      REXLOG_INFO("[condemned2_input][Input_GetButton] {:#x} {:#x} {:#x} {}", temp, tempB, ctx.lr, ctx.r3.u64);
    }
  }
  REX_HOOK_RAW(Input_PlayerInput){
    const uint32_t id = ctx.r4.u32;
    if(g_condemned2_input_driver->IsDebugging()){
      REXLOG_INFO("[condemned2_input][Input_PlayerInput] {:#x} {:#x}", ctx.r3.u64, id);
    }
    __imp__Input_PlayerInput(ctx, base);
    if (REXCVAR_GET(condemned2_input_trace_use_grab) &&
        (id == 0x57 || id == 0x40))
      REXLOG_INFO("[c2-player-action] id={:#x} result={:#x}", id, ctx.r3.u32);
  }
  // Radio & TV Antenna Adjustment Function
  REX_HOOK_RAW(TvAntennaAdjust) {
    g_condemned2_input_driver->SetTuningControls( true );
    __imp__TvAntennaAdjust(ctx, base);
    g_condemned2_input_driver->SetTuningControls( false );
  }
  // Checks if player has a melee weapon! Useful for swapping mouse input.
  REX_HOOK_RAW(IsMeleeWeapon){
    __imp__IsMeleeWeapon(ctx, base);
    //REXLOG_INFO("[condemned2_input][IsMeleeWeapon] {:#x}", ctx.r3.u64 );
    g_condemned2_input_driver->SetMeleeControls( ctx.r3.u64 != 0 );
  }
  // Checks if we're in a menu or not! This does not count in-game menus for investigations or fight club!
  REX_HOOK_RAW(Input_InHud){
    __imp__Input_InHud(ctx,base);
    g_condemned2_input_driver->SetMenuMode(ctx.r3.u32);
  }
  // Handles on attack landed events for player and NPCs.
  REX_HOOK_RAW(Input_OnAttackLanded){
    uint32_t temp = ctx.r3.u32;
    uint32_t tempB = ctx.r5.u32;
    __imp__Input_OnAttackLanded(ctx, base);
    if(g_condemned2_input_driver->IsDebugging()){
      REXLOG_INFO("[condemned2_input][Input_OnAttackLanded] {:#x} {:#x} {:#x} {:#x} {:#x}", temp, tempB, ctx.r5.u32, ctx.r6.u32, ctx.r3.u64);
    }
  }
}  // namespace rex::input::condemned2input

// Apply a linear curve for raw mouse input
bool Mouse_SensitivityCurve(PPCRegister& param_1, PPCRegister& param_2) {
    if (rex::input::condemned2input::g_condemned2_input_driver->GetMouseLookType() == rex::input::condemned2input::MouseLookType::DISABLED
    || !rex::input::condemned2input::g_condemned2_input_driver->IsUsingPCControls())
      return false;

    param_1.f64 = param_2.f64;
    return true;     
}