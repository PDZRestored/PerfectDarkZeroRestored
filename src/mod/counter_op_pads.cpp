// counter_op_pads.cpp (Counter-Op round 3, item 1): player 2 of Counter-Op always on the second controller.
// TU3 addresses.
//
// How a co-op participant gets its pad (all TU3, CONFIRMED in the disassembly):
//   0x82E74070 = Rare's frontend block ("fe"): +0x14 player 1's pad (0xFF = none -> 0 at use), +0x15 the co-op
//   partner's pad (0xFF = none), +0x16 another pad (823A2A00). Init 823A1718 (both 0xFF).
//   The co-op partner chooser (scene init 823CB738 / tick 823CB808 / back 823CB990) writes +0x15: the init auto-picks
//   the only other connected pad, the tick takes ANY connected pad that presses START except the pad of
//   823A2A00(fe, scene owner); it never compares with +0x14. The back-out resets +0x15 = 0xFF.
//   Level start 82471EE0 (from the level loop 8222D4A8 at 8222D9E0), local co-op (game type +40 == 1, +0x20 == 0):
//     player 1: pad = fe+0x14 (0xFF -> 0); player 2: pad = fe+0x15 (no 0xFF check);
//     profile = 82508B40(pad) (first of 8 profiles at 0x82D9F1C0 with +0x26DE set and +0x26E8 == pad, else 0);
//     82472320(profile+0x26E8, XUID +0x2698, 8250A118(profile), name +0x268C) builds the participant and stores the
//     pad at participant +0x690 (824724CC; also 82274790 copies the profile's controller settings).
//   Per frame: 822748C8(controller = participant +0x690, ...) reads pad object [[0x82D6C000] + 4 * controller+0]
//   (8227490C..82274920); the pad objects are filled from XInputGetState(0..3) by 82723F40 (identity pad map
//   0x82E2B6D8, set in 82723E50; its setter 827241D0 has no caller).
//   So two participants share one controller exactly when their +0x690 are equal.
//
// The fix (three parts; part 1 and 2 are the fix, part 3 is optional):
//   1. Before Rare builds the co-op participants (hook 82471EE0, no hook on it in src\ today): in Counter-Op, put
//      player 1 on pad 0 and player 2 on pad 1 in the frontend block, when each pad has a profile (the SDK's local
//      guest for user 1 exists while the second controller is connected). Rare then builds both participants with
//      matching profile / XUID / name, as if the menus had gone right.
//   2. Every frame in a Counter-Op level (CALL-IN from counter_op.cpp's Tick, which already has p1p / p2p): if player
//      2's participant +0x690 is not 1 (or player 1's is 1), write it. The controller update reads +0x690 every
//      frame, so this takes effect at once, whatever path made the participant (a later join, a rebuilt participant).
//   3. Optional: the chooser never keeps a partner pad equal to player 1's (hook 823CB808), Rare's own init rule
//      (823CB7AC..823CB7C4) applied in the tick too.

#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(pdz_counterop_pad_fix, true, "Gameplay",
                    "Counter-Op: player 2 always reads the second controller (user index 1) and player 1 the first "
                    "(user index 0), however player 2 joined (frontend block before the level start, and each "
                    "frame on the participants). Off = Rare's assignment.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_same_pad, false, "Testing",
                    "TESTING TOOL (counter-op): before each local co-op level start, Rare's frontend block is given "
                    "the same pad for player 2 as for player 1 (the hand test's fault: one pad moves both players), to "
                    "show the fault with pdz_counterop_pad_fix off and the fix with it on")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

void PdzCounterOpLevelBoundary(uint8_t *base, const char *why); // counter_op.cpp (round 6, item 1)
void PdzCounterOpHandsTakeBack(uint8_t *base, bool touch, const char *why); // counter_op_hands.cpp (round 8)
REX_EXTERN(__imp__sub_82471EE0); // level start: builds the local participants (SP: 1, local co-op: 2)
REX_EXTERN(__imp__sub_823CB808); // co-op partner chooser tick (optional part 3)
REX_EXTERN(__imp__sub_8250A340); // the profile task (f1 step), every frame from 82140820: reads XNotifyGetNext
REX_EXTERN(sub_82AD9FF8);        // XamUserGetSigninState(user r3) thunk (hooked in render/test_harness.cpp)

// Fri 20 Nov night (play log 031, Trinity Escape 21:59:43: player 2's controller switched itself off and the mission
// went straight back to the menus). Cause, step by step: the SDK's local guest for user 1 exists only while its pad
// is connected (thirdparty xam_user.cpp IsLocalGuest), and on the disconnect input_system.cpp UpdateConnectedUser
// broadcasts XN_SYS_SIGNINCHANGED (0xA). Rare's profile task 8250A340 rescans the four users on that notice (r15 =
// 15): user 1 now answers "not signed in" (82AD9FF8 = 0), his profile (82508B40(1), +0x26DF set) is signed out
// (8250C0C0(profile, 0)), the player on that pad is found (82472D28), and outside multiplayer the "active player
// signed out" byte 0x82E3C2DF is set; at the end of the same call, in a level (game +8 = 21 / 26) Rare leaves for
// the menus (game +8 = 8, 8250AC44..8250ACE0), as the 360's sign-out rule asks. The pause menu that opened in the
// same instant was Rare's own answer to the lost pad. On a console the profile stays signed in when its controller
// turns off, so only the pause happens. Here: while a Counter-Op level runs, player 2's pad has a profile and user 1
// reads "not signed in", Rare's profile task is held (its notices stay queued in the SDK's listener); the game
// waits in Rare's pause. When the pad is back the SDK signs the same guest in again (same name, same XUID), the task
// runs, takes the queued notices, finds user 1 signed in with the same name (82509EB0 answers "no change") and
// nothing ends. The general fix is in the SDK (keep a local guest signed in after its pad drops): see the night's
// RESEARCH_counterop_Fri20Nov.md.
REXCVAR_DEFINE_BOOL(pdz_counterop_pad_drop_hold, true, "Gameplay",
                    "Counter-Op (Fri 20 Nov night, default on): when player 2's controller disconnects in a level, the "
                    "game's profile task is held until it is back, so the lost sign-in does not end the mission (the "
                    "game pauses and waits, as on a console). Off = the mission goes back to the menus at once")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
bool PdzCounterOpRunning(); // counter_op.cpp

bool PdzCounterOpOn(); // counter_op.cpp: pdz_counterop, or Counter-Operative chosen in the menu

namespace counter_op_pads
{
  namespace
  {
    constexpr uint32_t kGame = 0x82D6BFD8;      // -> game state (+40 game type: 1 co-op; +0x20 0 = local)
    constexpr uint32_t kFrontend = 0x82E74070;  // +0x14 player 1's pad, +0x15 partner's pad (bytes, 0xFF none)
    constexpr uint32_t kProfiles = 0x82D9F1C0;  // 8 profile pointers (82508B40)
    constexpr uint32_t kProfActive = 0x26DE, kProfPad = 0x26E8;
    constexpr uint32_t kPartPad = 0x690;        // participant: controller object +0 = pad index (s32, < 0 none)
    constexpr uint32_t kP1Pad = 0, kP2Pad = 1;  // the SDK's SlotAssignment: first physical pad = user 0, second = 1

    uint32_t Load32(const uint8_t *base, uint32_t a)
    {
      const uint8_t *p = base + a;
      return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    void Store32(uint8_t *base, uint32_t a, uint32_t v)
    {
      uint8_t *p = base + a;
      p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
    }
    bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

    // 82508B40(pad) replicated (a leaf): the profile attached to that pad, or 0.
    uint32_t ProfileForPad(const uint8_t *base, uint32_t pad)
    {
      for (uint32_t i = 0; i < 8; ++i)
      {
        const uint32_t p = Load32(base, kProfiles + 4 * i);
        if (Ptr(p) && base[p + kProfActive] != 0 && Load32(base, p + kProfPad) == pad)
        {
          return p;
        }
      }
      return 0;
    }

    bool LocalCoop(const uint8_t *base)
    {
      const uint32_t so = Load32(base, kGame);
      return Ptr(so) && Load32(base, so + 40) == 1 && Load32(base, so + 0x20) == 0;
    }

    uint32_t EffectiveP1(uint8_t b) { return b == 0xFF ? 0u : b; } // 82471F40..82471F4C

    uint32_t g_logged_level = 0; // participant address last logged by Enforce (one line per new participant)
  } // namespace

  // Part 1, run before Rare's 82471EE0 in a local co-op level start.
  void NormaliseFrontend(uint8_t *base)
  {
    uint8_t &p1 = base[kFrontend + 0x14];
    uint8_t &p2 = base[kFrontend + 0x15];
    const uint8_t old1 = p1, old2 = p2;
    const uint32_t prof0 = ProfileForPad(base, kP1Pad), prof1 = ProfileForPad(base, kP2Pad);
    if (!prof1)
    {
      // No profile on pad 1: the second controller is not connected (the SDK signs user 1 in as a local guest only
      // while its pad is connected, xam_user.cpp IsLocalGuest), or the profile task has not attached it yet.
      // Rare's 82471EE0 would read NULL +0x26E8 for a partner pad without a profile (protect_zero: a crash), so
      // here only the clearly wrong case is repaired: a partner pad equal to player 1's, or none.
      if (p2 == 0xFF || p2 == EffectiveP1(p1) || !ProfileForPad(base, p2))
      {
        for (uint32_t pad = 0; pad < 4; ++pad)
        {
          if (pad != EffectiveP1(p1) && ProfileForPad(base, pad))
          {
            p2 = uint8_t(pad);
            break;
          }
        }
      }
      REXLOG_WARN("pdz_counterop: pads: no profile on pad 1 (is the second controller connected?): frontend pads "
                  "{:02X} / {:02X} -> {:02X} / {:02X}",
                  old1, old2, p1, p2);
      return;
    }
    if (EffectiveP1(p1) == kP2Pad && prof0)
    {
      p1 = uint8_t(kP1Pad); // player 1 had joined with the second pad: he goes to the first (user 0 = keyboard too)
    }
    p2 = uint8_t(kP2Pad);
    REXLOG_INFO("pdz_counterop: pads: level start: frontend pads {:02X} / {:02X} -> {:02X} / {:02X} (player 1 on pad "
                "{}, player 2 on pad 1)",
                old1, old2, p1, p2, EffectiveP1(p1));
  }

  // Part 2: CALL-IN from counter_op.cpp Tick() (sub_8222F740 hook), after FindPlayers(), every frame in a
  // Counter-Op level: Enforce(base, p1p, p2p).
  void Enforce(uint8_t *base, uint32_t p1p, uint32_t p2p)
  {
    if (!REXCVAR_GET(pdz_counterop_pad_fix) || !Ptr(p2p))
    {
      return;
    }
    const int32_t pad2 = int32_t(Load32(base, p2p + kPartPad));
    const int32_t pad1 = Ptr(p1p) ? int32_t(Load32(base, p1p + kPartPad)) : -1;
    if (pad2 == int32_t(kP2Pad) && pad1 != int32_t(kP2Pad))
    {
      if (g_logged_level != p2p)
      {
        g_logged_level = p2p;
        REXLOG_INFO("pdz_counterop: pads: player 1 participant {:08X} pad {}, player 2 participant {:08X} pad 1 (ok)",
                    p1p, pad1, p2p);
      }
      return;
    }
    // Only to a pad that has a profile: Rare looks the profile up from +0x690 without a NULL check in places
    // (Datacore's script 822FAD30 / 82302078 / 823022AC: 82508B40(+0x690) then reads it).
    if (!ProfileForPad(base, kP2Pad))
    {
      static uint32_t warned = 0;
      if (warned != p2p)
      {
        warned = p2p;
        REXLOG_WARN("pdz_counterop: pads: player 2 participant {:08X} is on pad {} (player 1 on {}); pad 1 has no "
                    "profile (second controller not connected?): left as it is",
                    p2p, pad2, pad1);
      }
      return;
    }
    Store32(base, p2p + kPartPad, kP2Pad);
    if (pad1 == int32_t(kP2Pad) && ProfileForPad(base, kP1Pad))
    {
      Store32(base, p1p + kPartPad, kP1Pad);
    }
    REXLOG_WARN("pdz_counterop: pads: player 2 participant {:08X} was on pad {} (player 1 on {}): now pad 1{}", p2p,
                pad2, pad1, pad1 == int32_t(kP2Pad) ? ", player 1 now pad 0" : "");
  }
} // namespace counter_op_pads

// Part 1 hook. 82471EE0 takes no arguments (it reads the game state itself) and returns nothing.
REX_HOOK_RAW(sub_82471EE0)
{
  PdzCounterOpLevelBoundary(base, "a level starts"); // round 6 (item 1)
  PdzCounterOpHandsTakeBack(base, false, "a level starts"); // round 8 (item 2): normally none left
  if (REXCVAR_GET(pdz_counterop_test_same_pad) && PdzCounterOpOn() && counter_op_pads::LocalCoop(base))
  {
    // Testing: the fault of the hand test, made on purpose: the partner's pad byte = player 1's.
    uint8_t &p1 = base[0x82E74070 + 0x14];
    uint8_t &p2 = base[0x82E74070 + 0x15];
    REXLOG_INFO("pdz_counterop: pads: TEST (pdz_counterop_test_same_pad): frontend pads {:02X} / {:02X} -> the partner "
                "on player 1's pad {:02X}",
                p1, p2, p1 == 0xFF ? 0 : p1);
    p2 = p1 == 0xFF ? 0 : p1;
  }
  if (REXCVAR_GET(pdz_counterop_pad_fix) && PdzCounterOpOn() && counter_op_pads::LocalCoop(base))
  {
    counter_op_pads::NormaliseFrontend(base);
  }
  __imp__sub_82471EE0(ctx, base);
}

// Part 3 (optional): the chooser tick never leaves the partner on player 1's pad. UNSURE: the tick has already
// sent the "joined" message to the scene (823CB958..823CB97C) when it stores +0x15; resetting it here may leave
// the joined text up for a frame. Parts 1 + 2 alone are enough for the level.
REX_HOOK_RAW(sub_823CB808)
{
  __imp__sub_823CB808(ctx, base);
  uint8_t &p1 = base[0x82E74070 + 0x14];
  uint8_t &p2 = base[0x82E74070 + 0x15];
  if (REXCVAR_GET(pdz_counterop_pad_fix) && p2 != 0xFF && p2 == (p1 == 0xFF ? 0 : p1))
  {
    REXLOG_WARN("pdz_counterop: pads: the co-op chooser took player 1's pad {} as the partner: cleared", p2);
    p2 = 0xFF;
  }
}

// Fri 20 Nov night (pdz_counterop_pad_drop_hold, see the top of the file): Rare's profile task is held while player
// 2's controller is away in a Counter-Op level. One sign-in query a frame (a lock in the SDK, no system call), only
// while a Counter-Op level runs.
REX_HOOK_RAW(sub_8250A340)
{
  static bool held = false;
  static std::chrono::steady_clock::time_point held_since{}, last_note{};
  static uint64_t held_calls = 0;
  bool hold = false;
  uint32_t prof = 0;
  if (REXCVAR_GET(pdz_counterop_pad_drop_hold) && PdzCounterOpRunning() && counter_op_pads::LocalCoop(base))
  {
    prof = counter_op_pads::ProfileForPad(base, 1);
    if (prof && base[prof + 0x26DF] != 0) // signed in (the byte 8250A340 tests before its sign-out rule, +9951)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 1;
      sub_82AD9FF8(frame.ctx, base);
      hold = frame.ctx.r3.u32 == 0;
    }
  }
  const auto now = std::chrono::steady_clock::now();
  if (hold)
  {
    ++held_calls;
    if (!held)
    {
      held = true;
      held_since = last_note = now;
      held_calls = 1;
      REXLOG_WARN("pdz_counterop: pads: player 2's controller is gone (user 1 no longer signed in: the SDK's local "
                  "guest lasts only while its pad is connected; his profile {:08X}): Rare's profile task 8250A340 is "
                  "held so its sign-out rule does not send the mission back to the menus; the game waits in Rare's "
                  "pause until the controller is back (pdz_counterop_pad_drop_hold)",
                  prof);
    }
    else if (now - last_note >= std::chrono::seconds(30))
    {
      last_note = now;
      REXLOG_INFO("pdz_counterop: pads: still waiting for player 2's controller ({:.0f} s, {} profile task calls held)",
                  std::chrono::duration<double>(now - held_since).count(), held_calls);
    }
    return;
  }
  if (held)
  {
    held = false;
    REXLOG_INFO("pdz_counterop: pads: player 2's controller is back (or the level ended) after {:.1f} s ({} profile "
                "task calls held): Rare's profile task runs again and takes the queued sign-in notices",
                std::chrono::duration<double>(now - held_since).count(), held_calls);
  }
  __imp__sub_8250A340(ctx, base);
}
