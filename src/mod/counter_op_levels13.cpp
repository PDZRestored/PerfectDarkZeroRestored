// Counter-Operative round 13: per-level pick rules (Laboratory, Temple, Bridge, Jungle, Nightclub, Subway). Built only with PDZ_COUNTEROP.
//
// 1. LABORATORY: player 2's body is only ever a guard the level data placed. The reinforcement waves (clones of the
//    four `s101` templates s10spawn / s10spawnholdflashbang / s10spawnalloutattack / s10spawnalloutattackflashbang,
//    the security girl with the two woman voices, made by the script with 82142AD8 while the alarm is on) were
//    already refused twice (the denied id `s101`; a clone made after the opening is a reinforcement). This rule
//    makes it certain whatever a later change does to those two: a guard is a body only when it was in Rare's
//    character list at the level start (the placed guards), is no clone of a named template and has no `s10*` id.
// 2. THE CLOAK (Temple's cloaking guards s06b-s06h, the Bridge's eggy-cave girls s26a-s26d and its end waves).
//    Cloaked = character +0x40 bit 0x4 (what the guards' "player cloaked" lines test, 82580FC8-82581008).
//    A guard: 8218DAE0 (weapon type 0x12 = the plasma rifle, branch 8218DD18), from the character update 8252C2A0:
//    set while the character's flag +0x187C 0x00200000 is on (given by the level script, guard by guard), the rifle
//    has charge and the guard has a target. A human: the plasma rifle's own secondary function 82250508 toggles
//    human +0x1EE0 between 0 and 4 (WeaponVisualEffectType_Cloaked; +0x1EF0 state, +0x1EE8 changed), the human
//    update 825A75A0 copies that to +0x40 bit 0x4; it drains the rifle (weapon +0x554) and ends at empty or when
//    human +0x1E50 is 0 (a byte level scripts write: Temple 8234E148). So player 2 with a cloaking guard's plasma
//    rifle has Rare's player cloak; nothing of the guard's cloak is copied. What this file adds:
//    - the hidden guard's own cloak bit is cleared at the takeover (the body Rare's kill leaves later is visible);
//    - a cloak player 2 still wears from his last life is ended (our own death path never runs Rare's reset);
//    - the Bridge's end waves (template `Endbridgespawner`, id s46a: the security girl with the plasma rifle, made at
//      8229E490 and given the cloak flag at 8229E604) are spawner clones on the denied list: here they are takeable.
#include "counter_op_levels13.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"
#include <rex/system/kernel_state.h>

#include "counter_op_guards.h" // IsTemplateClone
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop_lab_placed_only, true, "Gameplay",
                    "Counter-Operative (round 13, Laboratory): player 2's body is only ever a guard the level data "
                    "placed: never a clone of a spawner, never an s10 spawn (the alarm's reinforcement waves, the "
                    "security girl with the woman voices). The first refusals are logged. Off = round 12 (the same "
                    "waves refused by the denied id and the reinforcement rule only)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_lab_prison_girls, true, "Gameplay",
                    "Counter-Operative (round 13, Laboratory; needs pdz_counterop_lab_placed_only): the six PLACED "
                    "prison guards with the security girl's body and the woman voices (s9g1, s9g3, s9g4, s9g5, s9g6, "
                    "s9g8) may be taken (they are placed guards, not a wave). Off = they are refused too")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_cloak_guards, true, "Gameplay",
                    "Counter-Operative (round 13, Temple / Bridge): the cloaking guards are ordinary bodies for "
                    "player 2: he gets the guard's plasma rifle and Rare's own player cloak with it (its secondary "
                    "function); the hidden guard's own cloak is switched off at the takeover, a cloak left from "
                    "his last life is ended, and the Bridge's cloaked end waves (the spawner 'Endbridgespawner') "
                    "may be taken. Off = round 12")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_cloak_start, false, "Gameplay",
                    "Counter-Operative (round 13; needs pdz_counterop_cloak_guards): when the guard was cloaked at "
                    "the moment of the takeover, player 2 starts cloaked (Rare's player cloak switched on as its "
                    "own toggle does once the plasma rifle is in his hand). Off = he starts visible and cloaks "
                    "with the secondary function")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_bridge_end_wave_all, false, "Gameplay",
                    "Counter-Operative (round 13, Bridge; needs pdz_counterop_cloak_guards): the end waves' other "
                    "template ('Endbridgespawner2', no plasma rifle) may be taken too. Off = only the cloaked one")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_cloak_start, false, "Testing",
                    "TESTING TOOL (counter-op): with pdz_counterop_cloak_start, every guard with a plasma rifle "
                    "counts as cloaked at the takeover")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(pdz_counterop_test_bystanders, 0, "Testing",
                     "TESTING TOOL (counter-op): every N seconds (0 = off) one log line per scientist of the level "
                     "(character types 58-61): in the world or asleep, distance to player 2 and to the agent, "
                     "health, the alert words (+6268, +6252) and the stimulus word (+0x1874)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// ---- round 13b u: test tools for the Laboratory's alarm waves and the Bridge's end waves ---------------------------
REXCVAR_DEFINE_INT32(pdz_counterop_test_lab_waves, 0, "Testing",
                     "TESTING TOOL (counter-op, Laboratory, round 13b): 1 = every 2 s one line with the state of the "
                     "alarm waves (the level alarm byte, the script's wave flags and counts, the four spawn pads: "
                     "blocked or seen by a human, the clones alive) and one line for every new clone; 2 = also: the "
                     "level alarm is switched on with the game's own 'alarm on' (8214EBD0) whenever it is off; 3 = "
                     "also: while fewer than 3 wave clones are alive the script's own spawn message (type 7, byte "
                     "10) is sent every 0.5 s (run u_lab1: never needed, the alarm switch's event 8 / 46 is enough)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_bridge_waves, 0, "Testing",
                     "TESTING TOOL (counter-op, Bridge, round 13b): 1 = every 2 s one line with the state of the end "
                     "spawner (script flags, alive / total / limit, the gap, the pad offset, the six pads: blocked "
                     "or seen)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_end_life, 0, "Testing",
                     "TESTING TOOL (counter-op, round 13b): each time the value changes while player 2 is in the "
                     "world his health is put at his floor (1.0), so his life ends on this tick as from a hit, "
                     "where he stands (no move, no shot of the agent needed)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82153C00);        // the spawners' pad test (pad number, f1 height, r5 'even if seen', r6 'ignore the pad flags') -> 1 = no
REX_EXTERN(sub_8216A5D8);        // a pad by its number
REX_EXTERN(sub_8214EBD0);        // level manager: alarm on
REX_EXTERN(sub_8214E520);        // level manager: send a level message (manager, source, activator, r6, message)

uint32_t PdzCounterOpAgent(); // counter_op.cpp: 0 unless a Counter-Op game is running

namespace
{
  constexpr uint32_t kGuardVtable = 0x820C45A0, kHumanVtable = 0x820C7DE0;
  constexpr uint32_t kCharList = 0x82E454A0; // Rare's character list: +0x710 first node, +0x714 end; node +0 / +8
  constexpr uint32_t kSetup = 84, kSetupName = 44, kSetupType = 244;
  constexpr uint32_t kFlags40 = 0x40, kCloaked = 0x4;       // character +0x40 bit 0x4
  constexpr uint32_t kMayCloak = 0x187C, kMayCloakBit = 0x00200000u;
  constexpr uint32_t kEffect = 0x1EE0, kEffectChanged = 0x1EE8, kEffectState = 0x1EF0, kCloakAllowed = 0x1E50;
  constexpr uint32_t kEffectCloaked = 4;                    // WeaponVisualEffectType_Cloaked
  constexpr uint32_t kHand0 = 4076, kHand1 = 4080;          // character + (0x3FB + hand) * 4
  constexpr uint32_t kWeaponParams = 0x35C, kParamsType = 0x478, kPlasmaRifle = 0x12;
  constexpr uint32_t kWeaponAmmo = 0x524, kWeaponCharge = 0x554;
  constexpr uint32_t kSecurityGirl = 54;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint32_t Load16(const uint8_t *base, uint32_t a) { return (uint32_t(base[a]) << 8) | base[a + 1]; }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
  }
  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t bits = Load32(base, a);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  std::string IdText(uint32_t id)
  {
    std::string s;
    for (int shift = 24; shift >= 0; shift -= 8)
    {
      const char ch = char((id >> shift) & 0xFF);
      s += ch >= 32 && ch < 127 ? ch : '?';
    }
    return s;
  }

  // The setup's +44 name (a spawn template's name; "_noname_" for a placed guard).
  std::string SetupName(const uint8_t *base, uint32_t guard)
  {
    // (Sat 21 Nov evening: every step tested as mapped memory)
    const uint32_t setup = pdz::guestmem::Ok(base, guard, kSetup + 4) ? Load32(base, guard + kSetup) : 0;
    const uint32_t name = pdz::guestmem::Ok(base, setup, kSetupName + 4) ? Load32(base, setup + kSetupName) : 0;
    std::string text;
    for (uint32_t i = 0; pdz::guestmem::Ok(base, name, 48) && i < 48 && base[name + i] >= 32 && base[name + i] < 127; ++i)
    {
      text += char(base[name + i]);
    }
    return text;
  }

  // The gun type of the weapon in a hand (0 = none), read as Rare's character update does (8252C3E0-8252C424: the
  // item's class flag [[w + 8]] bit 0x10, then [w + 0x35C] + 0x478).
  uint32_t HandGunType(const uint8_t *base, uint32_t c, uint32_t hand, uint32_t *weapon = nullptr)
  {
    const uint32_t w = Load32(base, c + hand);
    const uint32_t cls = Ptr(w) ? Load32(base, w + 8) : 0;
    if (!Ptr(cls) || !(Load32(base, cls) & 0x10u))
    {
      return 0;
    }
    const uint32_t params = Load32(base, w + kWeaponParams);
    if (!Ptr(params))
    {
      return 0;
    }
    if (weapon)
    {
      *weapon = w;
    }
    return Load32(base, params + kParamsType);
  }
  uint32_t PlasmaRifleOf(const uint8_t *base, uint32_t c)
  {
    uint32_t w = 0;
    for (const uint32_t hand : {kHand1, kHand0})
    {
      if (HandGunType(base, c, hand, &w) == kPlasmaRifle)
      {
        return w;
      }
    }
    return 0;
  }

  struct State
  {
    bool started = false, lab = false, bridge = false;
    std::string mission;
    std::unordered_set<uint32_t> placed;  // guards in Rare's character list at the level start
    std::unordered_set<uint32_t> refused; // Laboratory: wave bodies refused (logged once each)
    std::unordered_set<uint32_t> wave;    // Bridge: end-wave guards accepted
    std::unordered_set<uint32_t> clones_seen; // Bridge: clones already logged
    uint32_t clone_lines = 0;
    uint32_t refused_lines = 0, wave_lines = 0, cloak_lines = 0;
    int last_cloaked = -1;
    bool start_pending = false;
    std::chrono::steady_clock::time_point start_until{};
    // round 13b u: the wave test tools
    std::chrono::steady_clock::time_point tool_next_log{}, tool_next_send{};
    uint32_t tool_lines = 0, tool_sends = 0, tool_alarm_lines = 0, tool_clone_lines = 0;
    std::unordered_set<uint32_t> tool_clones;
  };
  State s;
  int32_t g_last_end_life = 0;
  uint32_t g_tool_scratch = 0;

  constexpr uint32_t kGame = 0x82D6BFD8, kClock = 0x82E2F334, kPadTable = 0x82D2D0FC;
  constexpr uint32_t kLabScriptVtable = 0x820A30EC;
  double LoadDouble(const uint8_t *base, uint32_t a)
  {
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
    {
      bits = (bits << 8) | base[a + i];
    }
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
  }
  void StoreFloat(uint8_t *base, uint32_t a, float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    Store32(base, a, v);
  }
  uint32_t ScriptOf(const uint8_t *base, uint32_t *manager_out)
  {
    const uint32_t owner = Load32(base, kGame + 0x1C);
    const uint32_t manager = pdz::guestmem::Ok(base, owner, 0x2D8 + 4) ? Load32(base, owner + 0x2D8) : 0;
    *manager_out = Ptr(manager) ? manager : 0;
    const uint32_t script = pdz::guestmem::Ok(base, manager, 0x74 + 4) ? Load32(base, manager + 0x74) : 0;
    return Ptr(script) ? script : 0;
  }
  // One pad for the log: "0x1163: free / BLOCKED (flags ..)".
  std::string PadText(PPCContext &ctx, uint8_t *base, uint32_t number)
  {
    if (!Load16(base, kPadTable + 0xA))
    {
      return fmt::format("{:X}: no pads", number);
    }
    uint32_t pad = 0, blocked = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = number;
      sub_8216A5D8(frame.ctx, base);
      pad = frame.ctx.r3.u32;
    }
    if (!Ptr(pad))
    {
      return fmt::format("{:X}: not found", number);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = number;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      frame.ctx.f1.f64 = 160.0;
      sub_82153C00(frame.ctx, base);
      blocked = frame.ctx.r3.u32 & 0xFF;
    }
    const uint32_t flags = Load32(base, pad + 0x34);
    return fmt::format("{:X} at ({:.0f}, {:.0f}, {:.0f}) flags {:X}: {}", number, LoadFloat(base, pad + 0x10),
                       LoadFloat(base, pad + 0x14), LoadFloat(base, pad + 0x18), flags,
                       !blocked ? "free" : (flags & 0x90) ? "BLOCKED by its flags (0x10 / 0x80)" : "SEEN by a human");
  }
  // The clones of named templates in Rare's character list: the live ones counted, each new one logged once.
  uint32_t CloneCensus(const uint8_t *base, const char *level)
  {
    uint32_t alive = 0;
    uint32_t node = Load32(base, kCharList + 0x710);
    const uint32_t end = Load32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t o = Load32(base, node);
      node = Load32(base, node + 8);
      if (!Ptr(o) || Load32(base, o) != kGuardVtable || s.placed.count(o) || !counter_op_guards::IsTemplateClone(base, o))
      {
        continue;
      }
      const bool live = LoadFloat(base, o + 120) > 0.0f && !(Load32(base, o + 6272) & 0x10);
      alive += live;
      if (s.tool_clones.size() < 400 && s.tool_clones.insert(o).second && s.tool_clone_lines < 40)
      {
        ++s.tool_clone_lines;
        const uint32_t setup = Load32(base, o + kSetup);
        const uint32_t idp = Ptr(setup) ? Load32(base, setup + 52) : 0;
        REXLOG_INFO("pdz_counterop: {}: TEST TOOL: a spawner's clone {:08X} appeared: template '{}', id '{}', type {}, gun "
                    "types {} / {}, health {:.0f}, at ({:.0f}, {:.0f}, {:.0f}), cloak flag {}, cloaked now: {} ({} clone(s) "
                    "so far)",
                    level, o, SetupName(base, o), Ptr(idp) ? IdText(Load32(base, idp)) : std::string("?"),
                    Ptr(setup) ? Load32(base, setup + kSetupType) : 0, HandGunType(base, o, kHand1),
                    HandGunType(base, o, kHand0), LoadFloat(base, o + 120), LoadFloat(base, o + 48), LoadFloat(base, o + 52),
                    LoadFloat(base, o + 56), (Load32(base, o + kMayCloak) & kMayCloakBit) ? "on" : "off",
                    (Load32(base, o + kFlags40) & kCloaked) ? "yes" : "no", s.tool_clones.size());
      }
    }
    return alive;
  }

  void LabWaveTool(PPCContext &ctx, uint8_t *base, uint32_t p2c, bool out, int32_t mode)
  {
    uint32_t manager = 0;
    const uint32_t script = ScriptOf(base, &manager);
    if (!script || Load32(base, script) != kLabScriptVtable)
    {
      return;
    }
    const uint32_t flags = Load32(base, script + 0x3EC);
    if (!Ptr(flags))
    {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const uint32_t alive = CloneCensus(base, "laboratory");
    if (mode >= 2 && !base[manager + 0x54] && s.tool_alarm_lines < 10)
    {
      ++s.tool_alarm_lines;
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = manager;
      sub_8214EBD0(frame.ctx, base);
      REXLOG_INFO("pdz_counterop: laboratory: TEST TOOL {}: the level alarm was off: switched on with the game's own "
                  "8214EBD0: alarm byte now {} (#{})",
                  mode, int(base[manager + 0x54]), s.tool_alarm_lines);
    }
    if (mode >= 3 && alive < 3 && now >= s.tool_next_send && s.tool_sends < 400)
    {
      s.tool_next_send = now + std::chrono::milliseconds(500);
      if (!g_tool_scratch)
      {
        g_tool_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
      }
      if (g_tool_scratch)
      {
        std::memset(base + g_tool_scratch, 0, 16);
        base[g_tool_scratch + 1] = 7;
        base[g_tool_scratch + 2] = 10;
        const uint32_t made_before = Load32(base, script + 0x3FC), alive_before = Load32(base, script + 0x404);
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = manager;
          frame.ctx.r4.u64 = 0;
          frame.ctx.r5.u64 = 0;
          frame.ctx.r6.u64 = 0;
          frame.ctx.r7.u64 = g_tool_scratch;
          sub_8214E520(frame.ctx, base);
        }
        ++s.tool_sends;
        const uint32_t alive_after = Load32(base, script + 0x404);
        if (s.tool_sends <= 6 || alive_after != alive_before || s.tool_sends % 20 == 0)
        {
          REXLOG_INFO("pdz_counterop: laboratory: TEST TOOL {}: the script's spawn message 7 / 10 sent (#{}{}): the "
                      "script's stage count {} -> {}, alive count {} -> {}: {}",
                      mode, s.tool_sends, "", made_before,
                      Load32(base, script + 0x3FC), alive_before, alive_after,
                      alive_after != alive_before ? "A WAVE GUARD WAS MADE" : "no guard made (pad seen / blocked, or the area rule)");
        }
      }
    }
    if (now < s.tool_next_log || s.tool_lines >= 90)
    {
      return;
    }
    s.tool_next_log = now + std::chrono::seconds(2);
    ++s.tool_lines;
    const float clock = LoadFloat(base, kClock);
    const uint32_t agent = PdzCounterOpAgent();
    std::string pads;
    for (const uint32_t n : {0xC9u, 0x812u, 0x10Au, 0x129u})
    {
      pads += PadText(ctx, base, n) + "; ";
    }
    REXLOG_INFO("pdz_counterop: laboratory: TEST TOOL {}: waves: alarm byte {}, flags W0 {:08X} ('alarm has sounded' bit "
                "0x08000000 {}), W8 {:08X} (stages done 0x80000..0x400000), stage count {}, alive count {}, size N {}, "
                "flashbang slots {} / {}, clock {:.1f}: {:.1f} s since the alarm mark, {:.1f} s since the last spawn, "
                "{:.1f} s since the stage end (wait {:.0f}); pads: {}clones alive {} ({} ever); the agent at ({:.0f}, "
                "{:.0f}, {:.0f}), player 2 {} at ({:.0f}, {:.0f}, {:.0f})",
                mode, int(base[manager + 0x54]), Load32(base, flags), (Load32(base, flags) & 0x08000000u) ? "SET" : "clear",
                Load32(base, flags + 8), Load32(base, script + 0x3FC), Load32(base, script + 0x404),
                Load32(base, script + 0x414), Load32(base, script + 0x400), Load32(base, script + 0x408), clock,
                clock - LoadDouble(base, script + 0x750), clock - LoadDouble(base, script + 0x760),
                clock - LoadDouble(base, script + 0x808), LoadFloat(base, script + 0x468), pads, alive,
                s.tool_clones.size(), Ptr(agent) ? LoadFloat(base, agent + 48) : 0.0f,
                Ptr(agent) ? LoadFloat(base, agent + 52) : 0.0f, Ptr(agent) ? LoadFloat(base, agent + 56) : 0.0f,
                out ? "out of the world" : "in the world", Ptr(p2c) ? LoadFloat(base, p2c + 48) : 0.0f,
                Ptr(p2c) ? LoadFloat(base, p2c + 52) : 0.0f, Ptr(p2c) ? LoadFloat(base, p2c + 56) : 0.0f);
  }

  void BridgeWaveTool(PPCContext &ctx, uint8_t *base, uint32_t p2c, bool out)
  {
    const auto now = std::chrono::steady_clock::now();
    if (now < s.tool_next_log || s.tool_lines >= 90)
    {
      return;
    }
    s.tool_next_log = now + std::chrono::seconds(2);
    uint32_t manager = 0;
    const uint32_t script = ScriptOf(base, &manager);
    const uint32_t flags = script ? Load32(base, script + 0xA14) : 0;
    if (!Ptr(flags))
    {
      return;
    }
    ++s.tool_lines;
    const float clock = LoadFloat(base, kClock);
    const uint32_t agent = PdzCounterOpAgent();
    std::string pads;
    for (const uint32_t n : {0x1163u, 0x1330u, 0x11ECu, 0x1447u, 0x13C0u, 0x13B9u})
    {
      pads += PadText(ctx, base, n) + "; ";
    }
    const uint32_t w4 = Load32(base, flags + 4), w10 = Load32(base, flags + 0x10);
    REXLOG_INFO("pdz_counterop: bridge: TEST TOOL: end spawner: script {:08X} (vtable {:08X}); runs (W+0x10 bit 0x04000000) "
                "{}, stopped (W+4 bit 0x40) {}, cloaked kind (W+4 bit 0x20) {}; alive {} total {} limit {}; gap {:.1f} s, "
                "{:.1f} s since the last spawn; pad offset {}; pads: {}the agent at ({:.0f}, {:.0f}, {:.0f}), player 2 {} "
                "at ({:.0f}, {:.0f}, {:.0f})",
                script, Load32(base, script), (w10 & 0x04000000u) ? "YES" : "no", (w4 & 0x40u) ? "YES" : "no",
                (w4 & 0x20u) ? "YES" : "no", int(base[script + 0x55C]), Load32(base, script + 0x560),
                Load32(base, script + 0x564), LoadFloat(base, script + 0x550), clock - LoadDouble(base, script + 0x430),
                int(base[script + 0x56F]), pads, Ptr(agent) ? LoadFloat(base, agent + 48) : 0.0f,
                Ptr(agent) ? LoadFloat(base, agent + 52) : 0.0f, Ptr(agent) ? LoadFloat(base, agent + 56) : 0.0f,
                out ? "out of the world" : "in the world", Ptr(p2c) ? LoadFloat(base, p2c + 48) : 0.0f,
                Ptr(p2c) ? LoadFloat(base, p2c + 52) : 0.0f, Ptr(p2c) ? LoadFloat(base, p2c + 56) : 0.0f);
  }

  // TESTING TOOL (pdz_counterop_test_bystanders): the level's scientists, one line each.
  void BystanderLog(const uint8_t *base, uint32_t p2c, bool out)
  {
    static std::chrono::steady_clock::time_point next{};
    const auto now = std::chrono::steady_clock::now();
    if (now < next)
    {
      return;
    }
    next = now + std::chrono::seconds(REXCVAR_GET(pdz_counterop_test_bystanders));
    const uint32_t agent = PdzCounterOpAgent();
    auto dist = [&](uint32_t a, uint32_t b) {
      if (!Ptr(a) || !Ptr(b))
      {
        return -1.0f;
      }
      const float dx = LoadFloat(base, a + 48) - LoadFloat(base, b + 48), dy = LoadFloat(base, a + 52) - LoadFloat(base, b + 52),
                  dz = LoadFloat(base, a + 56) - LoadFloat(base, b + 56);
      return std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f;
    };
    uint32_t node = Load32(base, kCharList + 0x710);
    const uint32_t end = Load32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t o = Load32(base, node);
      node = Load32(base, node + 8);
      const uint32_t setup = Ptr(o) && Load32(base, o) == kGuardVtable ? Load32(base, o + kSetup) : 0;
      const uint32_t type = Ptr(setup) ? Load32(base, setup + kSetupType) : 0;
      if (type < 58 || type > 61)
      {
        continue;
      }
      const uint32_t idp = Load32(base, setup + 52);
      REXLOG_INFO("pdz_counterop: test bystander '{}' {:08X} (type {}): {}, {:.1f} m from player 2{}, {:.1f} m from the "
                  "agent, health {:.0f}, alert words {:08X} / {:08X}, stimulus word {:08X}, target handle {:08X}",
                  Ptr(idp) ? IdText(Load32(base, idp)) : std::string("?"), o, type,
                  (Load32(base, o + 6272) & 0x10) ? "asleep (out of the world)" : "in the world", dist(o, p2c),
                  out ? " (he is out)" : "", dist(o, agent), LoadFloat(base, o + 120), Load32(base, o + 6268),
                  Load32(base, o + 6252), Load32(base, o + 0x1874), Load32(base, o + 0x316C));
    }
  }

  // Bridge: each clone of a named template is logged once when it appears (every 2 s, at most 40 lines a level): the
  // end waves are not guards of counter_op_guards' scan (their id is denied), so nothing else names them.
  void BridgeCloneWatch(const uint8_t *base)
  {
    static std::chrono::steady_clock::time_point next{};
    const auto now = std::chrono::steady_clock::now();
    if (now < next || s.clone_lines >= 40)
    {
      return;
    }
    next = now + std::chrono::seconds(2);
    uint32_t node = Load32(base, kCharList + 0x710);
    const uint32_t end = Load32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t o = Load32(base, node);
      node = Load32(base, node + 8);
      if (!Ptr(o) || Load32(base, o) != kGuardVtable || s.placed.count(o) || !counter_op_guards::IsTemplateClone(base, o) ||
          !s.clones_seen.insert(o).second || s.clone_lines >= 40)
      {
        continue;
      }
      ++s.clone_lines;
      const uint32_t setup = Load32(base, o + kSetup);
      REXLOG_INFO("pdz_counterop: bridge: a spawner's clone {:08X} appeared: template '{}', type {}, gun types {} / {}, at "
                  "({:.0f}, {:.0f}, {:.0f}), cloak flag {}, cloaked now: {} ({} clone(s) so far)",
                  o, SetupName(base, o), Ptr(setup) ? Load32(base, setup + kSetupType) : 0, HandGunType(base, o, kHand1),
                  HandGunType(base, o, kHand0), LoadFloat(base, o + 48), LoadFloat(base, o + 52), LoadFloat(base, o + 56),
                  (Load32(base, o + kMayCloak) & kMayCloakBit) ? "on" : "off",
                  (Load32(base, o + kFlags40) & kCloaked) ? "yes" : "no", s.clones_seen.size());
    }
  }

  void EndCloak(uint8_t *base, uint32_t p2c)
  {
    // What Rare's own "off" stores (82250734-8225073C, 825B1878).
    Store32(base, p2c + kEffect, 0);
    base[p2c + kEffectChanged] = 1;
    Store32(base, p2c + kEffectState, 0);
  }
} // namespace

namespace counter_op_levels13
{
  void LevelStart(const uint8_t *base, const std::string &mission)
  {
    s = State{};
    s.started = true;
    s.mission = mission;
    s.lab = mission == "laboratory";
    s.bridge = mission == "bridge";
    uint32_t node = Load32(base, kCharList + 0x710);
    const uint32_t end = Load32(base, kCharList + 0x714);
    uint32_t clones = 0;
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t o = Load32(base, node);
      node = Load32(base, node + 8);
      if (Ptr(o) && Load32(base, o) == kGuardVtable)
      {
        if (counter_op_guards::IsTemplateClone(base, o))
        {
          ++clones;
          continue;
        }
        s.placed.insert(o);
      }
    }
    if (s.lab || s.bridge || mission == "temple")
    {
      REXLOG_INFO("pdz_counterop: levels13: {}: {} guard(s) placed by the level data in Rare's character list at the "
                  "start ({} clone(s) of a named template left out); placed-only rule {} (pdz_counterop_lab_placed_only, "
                  "Laboratory only), cloak rule {} (pdz_counterop_cloak_guards)",
                  mission, s.placed.size(), clones,
                  s.lab ? (REXCVAR_GET(pdz_counterop_lab_placed_only) ? "ON" : "off") : "not this level",
                  REXCVAR_GET(pdz_counterop_cloak_guards) ? "ON" : "off");
    }
  }

  bool RefuseBody(const uint8_t *base, uint32_t guard, uint32_t id)
  {
    if (!s.started || !s.lab || !REXCVAR_GET(pdz_counterop_lab_placed_only) || !Ptr(guard))
    {
      return false;
    }
    std::string why;
    const uint32_t setup = Load32(base, guard + kSetup);
    if (counter_op_guards::IsTemplateClone(base, guard))
    {
      why = fmt::format("a clone of the spawner template '{}'", SetupName(base, guard));
    }
    else if ((id >> 8) == 0x733130u) // "s10?"
    {
      why = "an s10 spawn id";
    }
    else if (!s.placed.count(guard))
    {
      why = "not one of the guards the level data placed (not in Rare's character list at the level start)";
    }
    else if (!REXCVAR_GET(pdz_counterop_lab_prison_girls) && Ptr(setup) && Load32(base, setup + kSetupType) == kSecurityGirl)
    {
      why = "a placed security girl of the prison (pdz_counterop_lab_prison_girls is off)";
    }
    if (why.empty())
    {
      return false;
    }
    if (s.refused.size() < 400 && s.refused.insert(guard).second && s.refused_lines < 12)
    {
      ++s.refused_lines;
      REXLOG_INFO("pdz_counterop: laboratory: wave body refused: guard '{}' {:08X} (type {}) is never player 2's body: "
                  "{} ({} refused so far; pdz_counterop_lab_placed_only)",
                  IdText(id), guard, Ptr(setup) ? Load32(base, setup + kSetupType) : 0, why, s.refused.size());
    }
    return true;
  }

  bool WaveBody(const uint8_t *base, uint32_t guard)
  {
    if (!s.started || !s.bridge || !REXCVAR_GET(pdz_counterop_cloak_guards) || !Ptr(guard))
    {
      return false;
    }
    if (s.wave.count(guard))
    {
      return true;
    }
    if (!counter_op_guards::IsTemplateClone(base, guard))
    {
      return false;
    }
    const std::string name = SetupName(base, guard);
    const bool cloaked_kind = name == "Endbridgespawner";
    if (!cloaked_kind && !(name == "Endbridgespawner2" && REXCVAR_GET(pdz_counterop_bridge_end_wave_all)))
    {
      return false;
    }
    if (s.wave.size() >= 400)
    {
      return false;
    }
    s.wave.insert(guard);
    if (s.wave_lines < 30)
    {
      ++s.wave_lines;
      REXLOG_INFO("pdz_counterop: bridge: end-wave guard {:08X} (template '{}', gun types {} / {}, cloak flag {}, "
                  "cloaked now: {}) may be taken by player 2 like a placed guard ({} so far; pdz_counterop_cloak_guards)",
                  guard, name, HandGunType(base, guard, kHand1), HandGunType(base, guard, kHand0),
                  (Load32(base, guard + kMayCloak) & kMayCloakBit) ? "on" : "off",
                  (Load32(base, guard + kFlags40) & kCloaked) ? "yes" : "no", s.wave.size());
    }
    return true;
  }

  bool WaveBodyKnown(uint32_t guard)
  {
    return s.started && s.bridge && !s.wave.empty() && REXCVAR_GET(pdz_counterop_cloak_guards) && s.wave.count(guard) != 0;
  }

  bool KeepPicking() { return s.started && s.bridge && REXCVAR_GET(pdz_counterop_cloak_guards); }

  void OnTake(PPCContext &ctx, uint8_t *base, uint32_t p2c, uint32_t guard)
  {
    (void)ctx;
    if (!s.started || !REXCVAR_GET(pdz_counterop_cloak_guards) || !Ptr(p2c) || !Ptr(guard) ||
        Load32(base, guard) != kGuardVtable || Load32(base, p2c) != kHumanVtable)
    {
      return;
    }
    s.start_pending = false;
    const uint32_t flags = Load32(base, guard + kFlags40);
    const bool guard_cloaked = (flags & kCloaked) != 0;
    const uint32_t rifle = PlasmaRifleOf(base, guard);
    const uint32_t effect = Load32(base, p2c + kEffect), state = Load32(base, p2c + kEffectState);
    std::string left;
    if (effect == kEffectCloaked && state != 2 && state != 3)
    {
      EndCloak(base, p2c);
      left = "; the cloak player 2 still wore from his last life is ended";
    }
    if (guard_cloaked)
    {
      Store32(base, guard + kFlags40, flags & ~kCloaked); // the hidden guard is not left cloaked (his later body)
    }
    if (!rifle && !guard_cloaked && left.empty())
    {
      return; // an ordinary guard: nothing to say
    }
    const bool start = REXCVAR_GET(pdz_counterop_cloak_start) && rifle &&
                       (guard_cloaked || REXCVAR_GET(pdz_counterop_test_cloak_start));
    if (start)
    {
      s.start_pending = true;
      s.start_until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    }
    REXLOG_INFO("pdz_counterop: cloak: takeover of guard {:08X}: {} (ammo {}, charge {:.2f}), the guard's cloak flag "
                "+0x187C {}, he was {} at this moment{}{}; player 2: effect {} state {}, cloak allowed byte {}{}",
                guard, rifle ? "he holds a plasma rifle" : "no plasma rifle in his hands",
                rifle ? Load16(base, rifle + kWeaponAmmo) : 0, rifle ? LoadFloat(base, rifle + kWeaponCharge) : 0.0f,
                (Load32(base, guard + kMayCloak) & kMayCloakBit) ? "on" : "off", guard_cloaked ? "CLOAKED" : "visible",
                guard_cloaked ? " (his own cloak bit cleared: the hidden guard stays visible)" : "",
                start ? "; player 2 starts cloaked once the rifle is in his hand (pdz_counterop_cloak_start)"
                      : rifle ? "; player 2 starts visible and cloaks with the rifle's secondary function" : "",
                effect, state, base[p2c + kCloakAllowed], left);
  }

  void Tick(PPCContext &ctx, uint8_t *base, uint32_t p2c, bool out)
  {
    // round 13b u: the test tools (each returns at once when its cvar is 0).
    if (s.started && s.lab && REXCVAR_GET(pdz_counterop_test_lab_waves) > 0)
    {
      LabWaveTool(ctx, base, p2c, out, REXCVAR_GET(pdz_counterop_test_lab_waves));
    }
    if (s.started && s.bridge && REXCVAR_GET(pdz_counterop_test_bridge_waves) > 0)
    {
      BridgeWaveTool(ctx, base, p2c, out);
    }
    if (const int32_t end_life = REXCVAR_GET(pdz_counterop_test_end_life); end_life != g_last_end_life)
    {
      g_last_end_life = end_life;
      const uint32_t participant = s.started && Ptr(p2c) && Load32(base, p2c) == kHumanVtable ? Load32(base, p2c + 6560) : 0;
      if (end_life != 0 && Ptr(participant) && !out)
      {
        const float before = LoadFloat(base, participant + 980);
        StoreFloat(base, participant + 980, 1.0f);
        REXLOG_INFO("pdz_counterop: TEST TOOL: player 2's health {:.1f} -> 1.0 (his floor) where he stands: his life ends "
                    "on this tick as from a hit (pdz_counterop_test_end_life {})",
                    before, end_life);
      }
      else if (end_life != 0)
      {
        REXLOG_INFO("pdz_counterop: TEST TOOL: pdz_counterop_test_end_life {}: nothing done (player 2 is {})", end_life,
                    out ? "out of the world" : "not a human in a started game");
      }
    }
    if (s.started && Ptr(p2c) && REXCVAR_GET(pdz_counterop_test_bystanders) > 0)
    {
      BystanderLog(base, p2c, out);
    }
    if (s.started && s.bridge && REXCVAR_GET(pdz_counterop_cloak_guards))
    {
      BridgeCloneWatch(base);
    }
    if (!s.started || !Ptr(p2c) || !REXCVAR_GET(pdz_counterop_cloak_guards) || Load32(base, p2c) != kHumanVtable)
    {
      return;
    }
    const uint32_t effect = Load32(base, p2c + kEffect);
    if (out)
    {
      s.start_pending = false;
      const uint32_t state = Load32(base, p2c + kEffectState);
      if (effect == kEffectCloaked && state != 2 && state != 3)
      {
        EndCloak(base, p2c);
        if (s.cloak_lines < 60)
        {
          ++s.cloak_lines;
          REXLOG_INFO("pdz_counterop: cloak: player 2 left the world cloaked: his cloak is ended (effect 4 -> 0)");
        }
      }
      s.last_cloaked = -1;
      return;
    }
    if (s.start_pending)
    {
      const uint32_t rifle = PlasmaRifleOf(base, p2c);
      if (rifle && effect != kEffectCloaked && base[p2c + kCloakAllowed] && Load16(base, rifle + kWeaponAmmo) &&
          Load32(base, p2c + kEffectState) != 3)
      {
        // What the plasma rifle's own toggle stores (82250644-8225066C).
        Store32(base, p2c + kEffect, kEffectCloaked);
        base[p2c + kEffectChanged] = 1;
        Store32(base, p2c + kEffectState, 1);
        s.start_pending = false;
        REXLOG_INFO("pdz_counterop: cloak: player 2 starts cloaked as the guard was (effect -> 4, state -> 1; rifle "
                    "{:08X}, ammo {}, charge {:.2f})",
                    rifle, Load16(base, rifle + kWeaponAmmo), LoadFloat(base, rifle + kWeaponCharge));
      }
      else if (effect == kEffectCloaked || std::chrono::steady_clock::now() >= s.start_until)
      {
        s.start_pending = false;
        if (effect != kEffectCloaked)
        {
          REXLOG_INFO("pdz_counterop: cloak: player 2 does not start cloaked: no plasma rifle in his hand within 6 s, "
                      "or no ammo, or the level does not allow his cloak (allowed byte {})",
                      base[p2c + kCloakAllowed]);
        }
      }
    }
    const int cloaked = (Load32(base, p2c + kFlags40) & kCloaked) ? 1 : 0;
    if (cloaked != s.last_cloaked)
    {
      const bool first = s.last_cloaked < 0;
      s.last_cloaked = cloaked;
      if ((cloaked || !first) && s.cloak_lines < 60)
      {
        ++s.cloak_lines;
        const uint32_t rifle = PlasmaRifleOf(base, p2c);
        REXLOG_INFO("pdz_counterop: cloak: player 2 is now {} (character +0x40 bit 4; effect {}, state {}, allowed "
                    "byte {}; {})",
                    cloaked ? "CLOAKED" : "visible", effect, Load32(base, p2c + kEffectState), base[p2c + kCloakAllowed],
                    rifle ? fmt::format("plasma rifle {:08X} ammo {} charge {:.2f}", rifle,
                                        Load16(base, rifle + kWeaponAmmo), LoadFloat(base, rifle + kWeaponCharge))
                          : std::string("no plasma rifle in his hands"));
      }
    }
  }
} // namespace counter_op_levels13
