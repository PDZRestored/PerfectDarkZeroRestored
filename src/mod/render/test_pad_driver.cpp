// test_pad_driver.cpp (Counter-Op round 3, item 1): scripted pads that enter the SDK at the DRIVER layer, the
// layer a real SDL / XInput controller enters, so a script's second pad takes the real second-controller path:
//   InputSystem::RefreshDevices (a new non-synthetic device) -> DeviceAssignment -> GetStateForUI ->
//   UpdateConnectedUser(1, true) (XN_SYS_INPUTDEVICESCHANGED + XN_SYS_SIGNINCHANGED 1 << 1; IsLocalGuest(1) true:
//   sign-in state 1, guest XUID, name, content XUID) -> deadzone, consumed_buttons, UI input blockers ->
//   XamInputGetState / XamInputGetKeystroke(Ex) (GetKeystrokeAnyUser stamps user 1) -> the game's XInputGetState
//   thunk sub_82AD4A18 -> 82723F40's pad objects -> each participant's controller (+0x690).
// Nothing in thirdparty\rexglue-sdk changes: InputDriver, InputSystem::AddDriver / SetDeviceAssignment and
// DeviceAssignment are public SDK headers, and the app installs them from its OnPreSetup through
// RuntimeConfig::input_factory (rex_app.cpp SetupPresentation calls OnPreSetup(config_) after setting the default).
//
// From test_harness.cpp: ScriptedPadState(pad) = the script's buttons /
// triggers / sticks of pad 0 / 1 right now, and ScriptedPadPresent(pad) = whether that pad is plugged in now.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/ui/virtual_key.h>

// Read once at start-up (the factory runs once): no hot reload.
REXCVAR_DEFINE_BOOL(pdz_test_pad_driver, false, "Render",
                    "Test harness: with --pdz_input_script, the script's pads are SDK input devices (users 0 and 1, "
                    "a 'p2' line plugs pad 2 in), as two real controllers, instead of states written over the game's "
                    "XInputGetState. Real controllers go to users 2-3. Needs a restart.");

namespace pdz::test
{
  // CALL-IN H1 (test_harness.cpp, new; the body is ApplyPadScript's merge loop without the guest writes): the
  // scripted state of pad 0 / 1 at the script time now. False when no script or the pad has no events.
  struct PadNow
  {
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    int16_t sticks[4] = {};
  };
  bool ScriptedPadState(uint32_t pad, PadNow &out);
  // Pad 0: always present while a script runs. Pad 1: from the script's 'p2 connect <t>' (new command, default 0
  // when the script has any 'p2' line), so a late-plugged second controller can be tested too.
  bool ScriptedPadPresent(uint32_t pad);
} // namespace pdz::test

namespace pdz::test::pad_driver
{
  namespace
  {
    using namespace rex;
    using namespace rex::input;
    constexpr uint64_t kIdBase = 0x50445A5300ull; // "PDZS" + pad: never collides with SDL's counter ids (1, 2, ...)
    DeviceId IdFor(uint32_t pad) { return static_cast<DeviceId>(kIdBase + pad); }
    int PadOf(DeviceId id)
    {
      const uint64_t v = static_cast<uint64_t>(id);
      return v == kIdBase || v == kIdBase + 1 ? int(v - kIdBase) : -1;
    }

    // Same thresholds / repeat timing as sdl_input_driver.h (HID_SDL_*), so menus see what a real pad gives.
    constexpr int16_t kThumbThres = 0x4E00;
    constexpr uint8_t kTriggerThres = 0x1F;
    constexpr int64_t kRepeatDelayMs = 400, kRepeatRateMs = 100;

    int64_t NowMs()
    {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
          .count();
    }
  } // namespace

  class ScriptPadDriver final : public InputDriver
  {
  public:
    ScriptPadDriver() : InputDriver(nullptr, 0) {}
    X_STATUS Setup() override { return X_STATUS_SUCCESS; }

    void EnumerateDevices(std::vector<DeviceInfo> &out) override
    {
      for (uint32_t pad = 0; pad < 2; ++pad)
      {
        if (!ScriptedPadPresent(pad))
        {
          continue; // a pad that is not plugged in yet: absent, as SDL before its GAMEPAD_ADDED event
        }
        DeviceInfo info;
        info.id = IdFor(pad);
        info.name = pad ? "PDZ script pad 2" : "PDZ script pad 1";
        info.guid = pad ? "50445a53000000000000000000000002" : "50445a53000000000000000000000001";
        info.subtype = XINPUT_DEVSUBTYPE_GAMEPAD;
        info.synthetic = false; // a physical pad: it takes a guest user of its own, like a real controller
        out.push_back(info);
      }
    }

    X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE *out_state) override
    {
      const int pad = PadOf(id);
      if (pad < 0 || !out_state)
      {
        return X_ERROR_DEVICE_NOT_CONNECTED;
      }
      PadNow now;
      ScriptedPadState(uint32_t(pad), now);
      std::lock_guard lock(mu_);
      Pad &p = pads_[pad];
      const uint64_t key = (uint64_t(now.buttons) << 48) ^ (uint64_t(now.lt) << 40) ^ (uint64_t(now.rt) << 32) ^
                           (uint64_t(uint16_t(now.sticks[0])) << 16) ^ uint64_t(uint16_t(now.sticks[1])) ^
                           (uint64_t(uint16_t(now.sticks[2])) << 24) ^ (uint64_t(uint16_t(now.sticks[3])) << 8);
      if (key != p.last_key || p.packet == 0)
      {
        p.last_key = key;
        ++p.packet; // XInput: the packet number moves only when the state changes (SDL starts at 1)
      }
      p.now = now;
      out_state->packet_number = p.packet;
      out_state->gamepad.buttons = now.buttons;
      out_state->gamepad.left_trigger = now.lt;
      out_state->gamepad.right_trigger = now.rt;
      out_state->gamepad.thumb_lx = now.sticks[0];
      out_state->gamepad.thumb_ly = now.sticks[1];
      out_state->gamepad.thumb_rx = now.sticks[2];
      out_state->gamepad.thumb_ry = now.sticks[3];
      // Not mirrored from SDL: is_active() zeroing when the window is not focused (unattended runs are often
      // unfocused). A real pad on an unfocused window reads neutral.
      return X_ERROR_SUCCESS;
    }

    X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES *caps) override
    {
      if (PadOf(id) < 0 || !caps)
      {
        return X_ERROR_DEVICE_NOT_CONNECTED;
      }
      // As SDLInputDriver::UpdateXCapabilities for a wired standard pad.
      caps->type = XINPUT_DEVTYPE_GAMEPAD;
      caps->sub_type = XINPUT_DEVSUBTYPE_GAMEPAD;
      caps->flags = 0;
      caps->gamepad.buttons = 0xF3FF;
      caps->gamepad.left_trigger = 0xFF;
      caps->gamepad.right_trigger = 0xFF;
      caps->gamepad.thumb_lx = int16_t(-1);
      caps->gamepad.thumb_ly = int16_t(-1);
      caps->gamepad.thumb_rx = int16_t(-1);
      caps->gamepad.thumb_ry = int16_t(-1);
      caps->vibration.left_motor_speed = 0xFFFF;
      caps->vibration.right_motor_speed = 0xFFFF;
      return X_ERROR_SUCCESS;
    }

    X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION *) override
    {
      return PadOf(id) < 0 ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_SUCCESS;
    }

    // Keystrokes from state edges, SDL's algorithm (sdl_input_driver.cpp GetDeviceKeystroke): ups first, then
    // downs, one per call, repeat after 400 ms every 100 ms; user_index 0 (InputSystem stamps the guest user).
    X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE *ks) override
    {
      const int pad = PadOf(id);
      if (pad < 0 || !ks)
      {
        return pad < 0 ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_BAD_ARGUMENTS;
      }
      using rex::ui::VirtualKey;
      static constexpr std::array<VirtualKey, 34> kVk = {
          VirtualKey::kXInputPadDpadUp, VirtualKey::kXInputPadDpadDown, VirtualKey::kXInputPadDpadLeft,
          VirtualKey::kXInputPadDpadRight, VirtualKey::kXInputPadStart, VirtualKey::kXInputPadBack,
          VirtualKey::kXInputPadLThumbPress, VirtualKey::kXInputPadRThumbPress, VirtualKey::kXInputPadLShoulder,
          VirtualKey::kXInputPadRShoulder, VirtualKey::kNone, VirtualKey::kNone, VirtualKey::kXInputPadA,
          VirtualKey::kXInputPadB, VirtualKey::kXInputPadX, VirtualKey::kXInputPadY, VirtualKey::kXInputPadLTrigger,
          VirtualKey::kXInputPadRTrigger, VirtualKey::kXInputPadLThumbUp, VirtualKey::kXInputPadLThumbDown,
          VirtualKey::kXInputPadLThumbRight, VirtualKey::kXInputPadLThumbLeft, VirtualKey::kXInputPadLThumbUpLeft,
          VirtualKey::kXInputPadLThumbUpRight, VirtualKey::kXInputPadLThumbDownRight,
          VirtualKey::kXInputPadLThumbDownLeft, VirtualKey::kXInputPadRThumbUp, VirtualKey::kXInputPadRThumbDown,
          VirtualKey::kXInputPadRThumbRight, VirtualKey::kXInputPadRThumbLeft, VirtualKey::kXInputPadRThumbUpLeft,
          VirtualKey::kXInputPadRThumbUpRight, VirtualKey::kXInputPadRThumbDownRight,
          VirtualKey::kXInputPadRThumbDownLeft};
      PadNow now;
      ScriptedPadState(uint32_t(pad), now);
      std::lock_guard lock(mu_);
      Pad &p = pads_[pad];
      const uint64_t cur = uint64_t(now.buttons) | Analog(now);
      const int64_t t = NowMs();
      ks->unicode = 0;
      ks->user_index = 0;
      ks->hid_code = 0;
      // A key that is no longer down never repeats (a slow keystroke poll, e.g. while a menu loads a level preview,
      // otherwise repeated a short press several times before its key-up was read).
      if (p.repeat && !(cur & (uint64_t(1) << p.repeat_idx)))
      {
        p.repeat = 0;
      }
      if (p.repeat == 1 && p.repeat_t + kRepeatDelayMs < t)
      {
        p.repeat = 2;
      }
      if (p.repeat == 2 && p.repeat_t + kRepeatRateMs < t)
      {
        p.repeat_t = t;
        ks->virtual_key = uint16_t(kVk[p.repeat_idx]);
        ks->flags = X_INPUT_KEYSTROKE_KEYDOWN | X_INPUT_KEYSTROKE_REPEAT;
        return X_ERROR_SUCCESS;
      }
      const uint64_t changed = cur ^ p.keys;
      if (!changed)
      {
        return X_ERROR_EMPTY;
      }
      for (int pass = 0; pass < 2; ++pass)
      {
        for (uint8_t i = 0; i < kVk.size(); ++i)
        {
          const uint64_t bit = uint64_t(1) << i;
          if (!(changed & bit) || kVk[i] == VirtualKey::kNone)
          {
            continue;
          }
          const bool down = (cur & bit) != 0;
          if (pass == 0 && !down)
          {
            ks->virtual_key = uint16_t(kVk[i]);
            ks->flags = X_INPUT_KEYSTROKE_KEYUP;
            p.keys &= ~bit;
            p.repeat = 0;
            return X_ERROR_SUCCESS;
          }
          if (pass == 1 && down)
          {
            ks->virtual_key = uint16_t(kVk[i]);
            ks->flags = X_INPUT_KEYSTROKE_KEYDOWN;
            p.keys |= bit;
            p.repeat = 1;
            p.repeat_idx = i;
            p.repeat_t = t;
            return X_ERROR_SUCCESS;
          }
        }
      }
      return X_ERROR_EMPTY;
    }

  private:
    struct Pad
    {
      uint32_t packet = 0;
      uint64_t last_key = ~0ull;
      PadNow now;
      uint64_t keys = 0;
      int repeat = 0; // 0 idle, 1 waiting, 2 repeating
      uint8_t repeat_idx = 0;
      int64_t repeat_t = 0;
    };

    static uint64_t Analog(const PadNow &s)
    {
      uint64_t f = 0;
      f |= uint64_t(s.lt > kTriggerThres) << 16;
      f |= uint64_t(s.rt > kTriggerThres) << 17;
      for (int k = 0; k < 2; ++k)
      {
        const int16_t x = s.sticks[2 * k], y = s.sticks[2 * k + 1];
        const int i = 8 * k;
        uint64_t u = y > kThumbThres, d = y < ~kThumbThres, r = x > kThumbThres, l = x < ~kThumbThres;
        if (u && l) { u = l = 0; f |= uint64_t(1) << (22 + i); }
        if (u && r) { u = r = 0; f |= uint64_t(1) << (23 + i); }
        if (d && r) { d = r = 0; f |= uint64_t(1) << (24 + i); }
        if (d && l) { d = l = 0; f |= uint64_t(1) << (25 + i); }
        f |= u << (18 + i);
        f |= d << (19 + i);
        f |= r << (20 + i);
        f |= l << (21 + i);
      }
      return f;
    }

    std::mutex mu_;
    std::array<Pad, 2> pads_;
  };

  // Script pad 1 -> user 0, script pad 2 -> user 1 whatever their ordinals (a real pad already plugged in would
  // otherwise take ordinal 0 first: SDL enumerates ahead of an appended driver). Synthetic devices (keyboard /
  // mouse, NOP) stay on user 0 as in SlotAssignment; real pads fill users 2 and 3 in ordinal order.
  class ScriptFirstAssignment final : public DeviceAssignment
  {
  public:
    void OnDevicesChanged(const std::vector<DeviceInfo> &devices) override
    {
      for (auto &u : users_)
      {
        u.clear();
      }
      uint32_t next_real = 2;
      for (const DeviceInfo &d : devices) // sorted by ordinal
      {
        const int pad = PadOf(d.id);
        if (pad >= 0)
        {
          users_[pad].push_back(d.id);
        }
        else if (d.synthetic)
        {
          users_[0].push_back(d.id);
        }
        else if (next_real < kMaxGuestUsers)
        {
          users_[next_real++].push_back(d.id);
        }
      }
    }
    void DevicesForUser(uint32_t user, std::vector<DeviceId> &out) const override
    {
      out.clear();
      if (user < kMaxGuestUsers)
      {
        out = users_[user];
      }
    }

  private:
    std::array<std::vector<DeviceId>, kMaxGuestUsers> users_;
  };

  // The input factory for OnPreSetup (CALL-IN A1 below): the SDK's default system plus the script pads.
  std::unique_ptr<rex::system::IInputSystem> CreateInputSystem(bool tool_mode)
  {
    std::unique_ptr<InputSystem> input = CreateDefaultInputSystem(tool_mode);
    if (!tool_mode)
    {
      // AddDriver takes no lock: safe only here, before the guest's first poll.
      input->AddDriver(std::make_unique<ScriptPadDriver>());
      input->SetDeviceAssignment(std::make_unique<ScriptFirstAssignment>());
      REXLOG_INFO("pdz_test: pad driver: the script's pads are SDK devices (script pad 1 = user 0, pad 2 = user 1, "
                  "real controllers = users 2-3)");
    }
    return input;
  }
} // namespace pdz::test::pad_driver

// Installed from the app's OnPreSetup (perfectdarkzerorecomp_app.h) through RuntimeConfig::input_factory when
// pdz_test_pad_driver and pdz_input_script are both set; test_harness.cpp then leaves the pad state, keystrokes,
// capabilities and sign-in of users 0 and 1 to the SDK.
