// Counter-Operative round 13: Trinity Infiltration's twins. Built only with PDZ_COUNTEROP.
//
// PLAYER 2 PLAYS ONE OF THE BROTHERS IN THE BOSS FIGHT (helper d; the study is TWINS_STUDY_Sun15Nov.md, whose live
// reads took Clay over ten times with a research probe; this is the clean version of that probe's path).
//
// The level (script vtable 8209EB00) opens on the fight: Brother Clay is at script +0x514, Brother Virgil at +0x51C.
// Every frame the script reads their health and sets a bit of its flag word 0 ([script +0x3EC]) when one is at 0 or
// gone: 0x4 Clay, 0x2 Virgil; with both set it sends the level message 00 02 00 00 once (handler 822C613C: the
// objective, and 22 s later the post-fight guards s01a-s01d wake). The twins are class 820BD880 (the plain NPC
// class; the guard class 820C45A0 differs from it in three vtable slots only, none of which the takeover uses), so
// counter_op.cpp's guard-class tests let them in through IsTwin() / Pickable().
//
// The owner's rules (they override the study's advice): no anti-stall rule, no extra guard, NO give-back to Rare's AI.
//   - While the fight is on, the only body player 2 can get is his twin (pdz_counterop_trinity_twin: clay / virgil /
//     nearest), once. He is dressed as the twin by the normal takeover (body, head, voice from the setup), stands on
//     the twin's own spot (the takeover's true-origin placement), gets the twin's guns (the one in hand through the
//     normal path; ExtraWeaponTypes() adds what else the twin carries, and flashbangs) and the twin's own two pools
//     as his health (SpawnHealth()). Nobody is woken with a twin.
//   - His death is the normal takeover death: the real twin is restored where he died and killed through Rare's
//     kill, so the script sets its bit. After that he gets NO body (no second twin, no guard) until the fight is over.
//   - The fight is over when both bits are set (or both twins are gone). Normal picking then resumes; it is held a
//     little longer, until one of the post-fight guards on the pick list (s01c / s01d) is awake, at most
//     pdz_counterop_trinity_twins_after_s: without that the relaxed area rule sends him to a platform worker 58 m
//     below the deck in the 22 s before Rare wakes them.
//   - If the other twin dies first he stays his twin until the agent kills him; the fight then ends by itself.
//   - A restart (new level script) starts over: he takes the twin again. The script's skip path (level-state bit 0x20
//     at the start: both twins deleted, no cutscene) is "no fight": normal picking from the start.
// Nothing here moves him: where he walks is his own business (past the deck's edge he falls into the level's kill
// zone and dies; the twin dies with him, as in any takeover).
//
// No hook of its own. Call-ins (each marked "round 13 d"): counter_op.cpp Reset(), Tick(), Spots() (the class test,
// the hold, the deny list, the id list, the originals rule), Pick(), ApplyHealth(), SpawnAt() (the dress test, the
// extra weapons, no area wake), KeepTakenHidden(), KillTakenGuardStep().
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

#include "counter_op_twins.h"
#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop_trinity_twins, true, "Gameplay",
                    "Counter-Operative (round 13), Trinity Infiltration: player 2's first body is one of the Brothers "
                    "of the boss fight the level opens on, with the twin's guns and health. When his twin dies he "
                    "waits, with no body, until the agent has finished the fight; then the normal guard picking goes "
                    "on. Off: the picking of round 12 (a platform worker on a lower floor during the fight)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_trinity_twin, "clay", "Gameplay",
                      "Counter-Operative (round 13), Trinity Infiltration: which Brother player 2 plays: 'clay' "
                      "(default: the one further from the agent's start, 33 m against 24 m, and the one taken over in "
                      "nine of the ten research takeovers), 'virgil', or 'nearest' (the one nearer the agent when he "
                      "takes him)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_trinity_twin_health, 0.0, "Gameplay",
                      "Counter-Operative (round 13), Trinity Infiltration: player 2's health as a Brother. 0 "
                      "(default) = the twin's own two pools as Rare's level script set them for the difficulty, added "
                      "up (600 on Agent, Secret Agent and Dark Agent, 500 on Perfect Agent); above 0 = this number")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_trinity_twins_after_s, 40.0, "Gameplay",
                      "Counter-Operative (round 13), Trinity Infiltration: after the Brothers' fight player 2's next "
                      "guard is held until one of the post-fight guards (s01c / s01d, woken by Rare 22 s after the "
                      "fight) is up, at most this many seconds. 0 = no hold (the relaxed area rule may then send him "
                      "to a lower floor)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_twins_kill, 0, "Testing",
                     "TESTING TOOL (counter-op, Trinity Infiltration): each change: 1 = Rare's kill on Brother Clay, 2 "
                     "= on Brother Virgil, credited to the agent (never on the hidden twin player 2 stands in for)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8, kLevel = 0x1C, kLevelTarget = 0x2D8, kLevelTargetScript = 0x74;
  constexpr uint32_t kScriptVtable = 0x8209EB00;  // Trinity Infiltration's level script
  constexpr uint32_t kBrothersVtable = 0x82091EC0; // BROTHERS_BOSS_SCRIPT (script kind 37)
  constexpr uint32_t kTwinVtable = 0x820BD880, kGuardVtable = 0x820C45A0;
  constexpr uint32_t kTwinOffset[2] = {0x514, 0x51C}; // script: Clay, Virgil
  constexpr uint32_t kTwinBit[2] = {0x4, 0x2};         // flag word 0: Clay down, Virgil down
  constexpr const char *kTwinName[2] = {"Clay", "Virgil"};
  constexpr const char *kTwinId[2] = {"clay", "virg"};
  constexpr uint32_t kFlagBlock = 0x3EC, kScriptStage = 0x508;
  constexpr uint32_t kLevelState = 0x82E426F8; // byte: 0x20 at a start = the script's skip path; 0x40 once the fight is over
  constexpr uint32_t kCharList = 0x82E454A0;   // +0x710 first node, +0x714 end; node: +0 character, +8 next
  constexpr uint32_t kPosition = 48, kSetup = 84, kHealth = 120, kArmour = 124, kHidden = 6272, kSetupId = 52;
  constexpr uint32_t kInventory = 0x142C, kWeaponEntry = 1248, kWeaponDefType = 1144;
  constexpr uint32_t kWeaponList = 48, kWeaponListCount = 24, kWeaponEntryBytes = 2196;
  constexpr uint32_t kFlashbang = 30, kUnarmed = 15;
  constexpr double kFoughtSeconds = 5.0; // the fight was really played this long (outside cutscenes): the hold applies

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t bits = Load32(base, a);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  using Clock = std::chrono::steady_clock;
  double Since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

  // The level script when it is Trinity Infiltration's: [[[game +0x1C] +0x2D8] +0x74].
  uint32_t Script(const uint8_t *base)
  {
    const uint32_t level = Load32(base, kGame + kLevel);
    const uint32_t target = pdz::guestmem::Ok(base, level, kLevelTarget + 4) ? Load32(base, level + kLevelTarget) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, target, kLevelTargetScript + 4) ? Load32(base, target + kLevelTargetScript) : 0;
    return pdz::guestmem::Ok(base, script, 4) && Load32(base, script) == kScriptVtable ? script : 0;
  }

  // A character's setup id (4 characters).
  std::string Id(const uint8_t *base, uint32_t c)
  {
    // (Sat 21 Nov evening: every step tested as mapped memory)
    const uint32_t setup = pdz::guestmem::Ok(base, c, kSetup + 4) ? Load32(base, c + kSetup) : 0;
    const uint32_t p = pdz::guestmem::Ok(base, setup, kSetupId + 4) ? Load32(base, setup + kSetupId) : 0;
    std::string out;
    for (uint32_t i = 0; pdz::guestmem::Ok(base, p, 4) && i < 4 && base[p + i] >= 32 && base[p + i] < 127; ++i)
    {
      out += char(base[p + i]);
    }
    return out;
  }

  // The script's pointer is still that twin: the plain NPC class with the twin's own setup id (a deleted twin's
  // memory does not pass both).
  uint32_t ValidTwin(const uint8_t *base, uint32_t script, int i)
  {
    const uint32_t c = Load32(base, script + kTwinOffset[i]);
    return pdz::guestmem::Ok(base, c, 0x1A00) && Load32(base, c) == kTwinVtable && Id(base, c) == kTwinId[i] ? c : 0; // Sun 22 Nov night (audit): Ok first
  }

  // The twin's Brothers script: the current script of his stack ([c +0x18D4 +0x528] -0x84), else in the stack's list.
  uint32_t Brothers(const uint8_t *base, uint32_t c)
  {
    // Sun 22 Nov night (audit): every link of the AI stack read only where readable.
    if (!pdz::guestmem::Ok(base, c + 0x18D4 + 0x528, 4))
    {
      return 0;
    }
    const uint32_t top = Load32(base, c + 0x18D4 + 0x528);
    if (!Ptr(top) || !pdz::guestmem::Ok(base, top - 0x84, 0x84))
    {
      return 0;
    }
    const uint32_t cur = Load32(base, top - 0x84);
    if (pdz::guestmem::Ok(base, cur, 4) && Load32(base, cur) == kBrothersVtable)
    {
      return cur;
    }
    uint32_t node = Load32(base, top - 0xC);
    const uint32_t end = Load32(base, top - 8);
    for (int n = 0; n < 32 && pdz::guestmem::Ok(base, node, 12) && node != end; ++n)
    {
      const uint32_t obj = Load32(base, node);
      if (pdz::guestmem::Ok(base, obj, 4) && Load32(base, obj) == kBrothersVtable)
      {
        return obj;
      }
      node = Load32(base, node + 8);
    }
    return 0;
  }

  // The level's weapon list holds this type (Rare's give reads a missing type's entry unchecked).
  bool LevelGivesType(const uint8_t *base, uint32_t type)
  {
    const uint32_t list = Load32(base, kGame + kWeaponList);
    const uint32_t entries = Ptr(list) ? Load32(base, list) : 0;
    const uint32_t count = Ptr(list) ? base[list + kWeaponListCount] : 0;
    for (uint32_t i = 0; Ptr(entries) && i < count; ++i)
    {
      const uint32_t d = Load32(base, entries + i * kWeaponEntryBytes);
      if (Ptr(d) && Load32(base, d + kWeaponDefType) == type)
      {
        return true;
      }
    }
    return false;
  }

  // One of the post-fight guards on the pick list is awake and alive (its id, else empty).
  std::string PostFightGuardUp(const uint8_t *base)
  {
    uint32_t node = Load32(base, kCharList + 0x710);
    const uint32_t end = Load32(base, kCharList + 0x714);
    for (int i = 0; pdz::guestmem::Ok(base, node, 12) && node != end && i < 2048; ++i) // Sun 22 Nov night (audit): Ok
    {
      const uint32_t c = Load32(base, node);
      node = Load32(base, node + 8);
      if (!pdz::guestmem::Ok(base, c, 0x1884) || Load32(base, c) != kGuardVtable || (Load32(base, c + kHidden) & 0x10) ||
          !(LoadFloat(base, c + kHealth) > 0.0f))
      {
        continue;
      }
      const std::string id = Id(base, c);
      if (id == "s01c" || id == "s01d")
      {
        return id;
      }
    }
    return std::string();
  }

  // Which row of Rare's table the two pools are (822C1B04-822C1D10), for the log.
  std::string PoolsText(float health, float second)
  {
    struct Row
    {
      const char *name;
      float health, second;
    };
    constexpr Row kRows[] = {{"Agent", 200.f, 400.f}, {"Secret Agent", 250.f, 350.f}, {"Perfect Agent", 300.f, 200.f},
                             {"Dark Agent", 400.f, 200.f}};
    for (const Row &r : kRows)
    {
      if (std::fabs(health - r.health) < 0.5f && std::fabs(second - r.second) < 0.5f)
      {
        return fmt::format("Rare's single-player numbers for {} (no co-op 1.5)", r.name);
      }
      if (std::fabs(health - 1.5f * r.health) < 0.5f && std::fabs(second - 1.5f * r.second) < 0.5f)
      {
        return fmt::format("Rare's numbers for {} x the co-op 1.5", r.name);
      }
    }
    return "not a row of Rare's table (the twin was hurt already, or the setup's own numbers)";
  }

  enum Phase
  {
    kNone = 0,  // no fight seen (not this level's start, the skip path, or not decided yet): normal picking
    kFight,     // the fight is on: only his twin, once
    kAfter,     // the fight is over, the post-fight guards are not up yet: nobody
    kNormal,    // normal picking again
  };

  struct State
  {
    uint32_t script = 0;
    uint32_t twin[2] = {0, 0}; // this frame's valid twins (0 = gone)
    Phase phase = kNone;
    bool decided = false;      // the script's first frame was looked at
    uint32_t flags = 0;
    int choice = -1;           // the twin he is to take (index), while he has not had one
    int mine = -1;             // the twin he took
    bool took = false, died = false, bit_seen = false, wait_logged = false, bit_late_logged = false;
    bool unavailable_logged = false;
    Clock::time_point died_at{}, fight_end{}, last_frame{}, next_check{}, next_watch{};
    double fought_s = 0.0;     // seconds of fight outside cutscenes
    uint32_t aimed = 0;        // times the AI twin's target was player 2 (sampled)
  } g;
  int32_t g_kill_tool = 0; // outside State: a restart does not repeat it

  bool On() { return REXCVAR_GET(pdz_counterop_trinity_twins); }

  std::string TwinLine(const uint8_t *base, int i)
  {
    const uint32_t c = g.twin[i];
    if (!c)
    {
      return fmt::format("{}: gone", kTwinName[i]);
    }
    return fmt::format("{} {:08X} at ({:.0f}, {:.0f}, {:.0f}): health {:.1f} + second pool {:.1f}{}{}", kTwinName[i], c,
                       LoadFloat(base, c + kPosition), LoadFloat(base, c + kPosition + 4),
                       LoadFloat(base, c + kPosition + 8), LoadFloat(base, c + kHealth), LoadFloat(base, c + kArmour),
                       (Load32(base, c + kHidden) & 0x10) ? ", hidden" : "", (g.flags & kTwinBit[i]) ? ", bit set" : "");
  }

  // TESTING TOOL (pdz_counterop_test_twins_kill): Rare's kill on a twin, as a level script kills a guard.
  void KillTool(PPCContext &ctx, uint8_t *base, uint32_t p1c)
  {
    const int32_t kill = REXCVAR_GET(pdz_counterop_test_twins_kill);
    if (kill == g_kill_tool)
    {
      return;
    }
    g_kill_tool = kill;
    if (kill != 1 && kill != 2)
    {
      return;
    }
    const int i = kill - 1;
    const uint32_t c = g.twin[i];
    const uint32_t vt = Ptr(c) ? Load32(base, c) : 0;
    PPCFunc *fn = Ptr(vt) && Load32(base, vt + 0x2E4) == 0x8252DE88
                      ? pdz::GuestFunction(0x8252DE88, "counter-op twins: test kill")
                      : nullptr;
    if (!fn || !Ptr(p1c) || (Load32(base, c + kHidden) & 0x10) || !(LoadFloat(base, c + kHealth) > 0.0f))
    {
      REXLOG_INFO("pdz_counterop: twins: TEST TOOL kill {}: nothing done ({})", kill,
                  !Ptr(c) ? "that twin is gone"
                  : (Load32(base, c + kHidden) & 0x10) ? "that twin is hidden: player 2 stands in for him"
                  : !(LoadFloat(base, c + kHealth) > 0.0f) ? "that twin is dead already"
                                                           : "no kill function or no agent");
      return;
    }
    const float before = LoadFloat(base, c + kHealth);
    rex::CallFrame frame(ctx); // copies r1 / r13 only
    frame.ctx.r3.u64 = c;
    frame.ctx.r4.u64 = p1c;
    frame.ctx.r5.u64 = 5;
    frame.ctx.r6.u64 = 1;
    fn(frame.ctx, base);
    REXLOG_INFO("pdz_counterop: twins: TEST TOOL kill {}: Brother {} {:08X} killed through Rare's kill (8252DE88, "
                "credited to the agent): health {:.1f} -> {:.1f}, dead flag +0x44 {}",
                kill, kTwinName[i], c, before, LoadFloat(base, c + kHealth),
                (Load32(base, c + 0x44) & 8) ? "set" : "NOT set");
  }
} // namespace

namespace counter_op_twins
{
  void Reset()
  {
    if (g.script && g.phase != kNone)
    {
      REXLOG_INFO("pdz_counterop: twins: level boundary: the fight's state is forgotten (it was: {}{})",
                  g.phase == kFight ? "fight on" : g.phase == kAfter ? "fight over, waiting for the post-fight guards"
                                                                     : "normal picking",
                  g.took ? fmt::format("; player 2 had been {}", kTwinName[g.mine]) : std::string());
    }
    g = State{};
  }

  bool IsTwin(uint32_t character) { return character && (character == g.twin[0] || character == g.twin[1]); }

  bool FightHolds() { return g.phase == kFight || g.phase == kAfter; }

  bool Pickable(uint32_t character)
  {
    return g.phase == kFight && !g.took && g.choice >= 0 && character && character == g.twin[g.choice];
  }

  void Tick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, uint32_t current_guard, bool out, bool cutscene)
  {
    (void)out;
    const uint32_t script = Script(base);
    if (!script)
    {
      if (g.script)
      {
        g = State{};
      }
      return; // not Trinity Infiltration
    }
    const auto now = Clock::now();
    if (script != g.script)
    {
      g = State{};
      g.script = script;
      g.last_frame = now;
    }
    // The twins of this frame: IsTwin() is true for them whatever the cvar says, so that a twin already taken still
    // gets his keep-hidden pass and his kill if the cvar is switched off in mid-fight.
    for (int i = 0; i < 2; ++i)
    {
      g.twin[i] = ValidTwin(base, script, i);
    }
    const uint32_t block = Load32(base, script + kFlagBlock);
    const uint32_t flags = Ptr(block) ? Load32(base, block) : 0;
    const uint32_t old_flags = g.flags;
    g.flags = flags;
    KillTool(ctx, base, p1c);
    const double frame_s = std::chrono::duration<double>(now - g.last_frame).count();
    g.last_frame = now;
    if (!On())
    {
      if (g.phase != kNone && g.phase != kNormal)
      {
        REXLOG_INFO("pdz_counterop: twins: pdz_counterop_trinity_twins is off: normal picking{}",
                    g.took && !g.died ? " (he keeps the twin he has until he dies)" : "");
      }
      g.phase = kNone;
      g.decided = false;
      g.choice = -1;
      return;
    }
    const bool both_down = (flags & 6u) == 6u;
    const bool any_twin = g.twin[0] || g.twin[1];
    if (!g.decided)
    {
      g.decided = true;
      const uint8_t level_state = base[kLevelState];
      if (level_state & 0x20)
      {
        g.phase = kNormal;
        REXLOG_INFO("pdz_counterop: twins: Trinity Infiltration's level script {:08X}: level-state byte {:02X} has bit "
                    "0x20 (the script's skip path: the twins are deleted, no fight): normal picking from the start",
                    script, level_state);
      }
      else if (any_twin && !both_down)
      {
        g.phase = kFight;
        REXLOG_INFO("pdz_counterop: twins: Trinity Infiltration's level script {:08X}: THE FIGHT IS ON (flag word 0 "
                    "{:08X}, level-state byte {:02X}); {}; {}; player 2's body is Brother {} "
                    "(pdz_counterop_trinity_twin '{}'), nobody else until the fight is over",
                    script, flags, level_state, TwinLine(base, 0), TwinLine(base, 1),
                    REXCVAR_GET(pdz_counterop_trinity_twin) == "virgil" || REXCVAR_GET(pdz_counterop_trinity_twin) == "virg"
                        ? "Virgil"
                        : REXCVAR_GET(pdz_counterop_trinity_twin) == "nearest" ? "Clay or Virgil, the nearer to the agent"
                                                                               : "Clay",
                    REXCVAR_GET(pdz_counterop_trinity_twin));
      }
      else
      {
        g.phase = kNormal;
        REXLOG_INFO("pdz_counterop: twins: Trinity Infiltration's level script {:08X}: no fight to play (flag word 0 "
                    "{:08X}; {}; {}): normal picking",
                    script, flags, TwinLine(base, 0), TwinLine(base, 1));
      }
    }
    if (g.phase == kFight)
    {
      if (!cutscene)
      {
        g.fought_s += frame_s > 0.0 && frame_s < 0.5 ? frame_s : 0.0;
      }
      if (flags != old_flags)
      {
        REXLOG_INFO("pdz_counterop: twins: the level script's flag word 0: {:08X} -> {:08X} (Clay down 0x4 {}, Virgil "
                    "down 0x2 {}); {}; {}",
                    old_flags, flags, (flags & 4) ? "SET" : "clear", (flags & 2) ? "SET" : "clear", TwinLine(base, 0),
                    TwinLine(base, 1));
      }
      // Which twin he is to take.
      if (!g.took)
      {
        const std::string want = REXCVAR_GET(pdz_counterop_trinity_twin);
        int choice = want == "virgil" || want == "virg" ? 1 : 0;
        if (want == "nearest" && Ptr(p1c))
        {
          float best = 1e30f;
          for (int i = 0; i < 2; ++i)
          {
            const uint32_t c = g.twin[i];
            if (!c || (flags & kTwinBit[i]) || !(LoadFloat(base, c + kHealth) > 0.0f))
            {
              continue;
            }
            const float dx = LoadFloat(base, c + kPosition) - LoadFloat(base, p1c + kPosition),
                        dz = LoadFloat(base, c + kPosition + 8) - LoadFloat(base, p1c + kPosition + 8);
            const float d = dx * dx + dz * dz;
            if (d < best)
            {
              best = d;
              choice = i;
            }
          }
        }
        const uint32_t c = g.twin[choice];
        const bool usable = c && !(flags & kTwinBit[choice]) && LoadFloat(base, c + kHealth) > 0.0f;
        g.choice = usable ? choice : -1;
        if (!usable && !g.unavailable_logged && !cutscene)
        {
          g.unavailable_logged = true;
          REXLOG_INFO("pdz_counterop: twins: Brother {} is not available to player 2 ({}): he waits, with no body, for "
                      "the fight's end",
                      kTwinName[choice], !c ? "gone" : "dead already");
        }
        // (the take itself is seen by SpawnHealth(); this is the safety net)
        if (current_guard && g.choice >= 0 && current_guard == g.twin[g.choice])
        {
          g.took = true;
          g.mine = g.choice;
        }
      }
      else if (!g.died)
      {
        if (current_guard != g.twin[g.mine] || !g.twin[g.mine])
        {
          g.died = true;
          g.died_at = now;
          g.choice = -1;
          REXLOG_INFO("pdz_counterop: twins: player 2's life as Brother {} is over (his death: the real {} is restored "
                      "where he died and killed through Rare's kill); he gets no other body while the fight is on",
                      kTwinName[g.mine], kTwinName[g.mine]);
        }
        else if (now >= g.next_watch)
        {
          // Item 6: the AI twin must treat him as its brother. Its Brothers script's target (handle object +0xD0),
          // sampled four times a second.
          g.next_watch = now + std::chrono::milliseconds(250);
          const int other = 1 - g.mine;
          const uint32_t b = g.twin[other] ? Brothers(base, g.twin[other]) : 0;
          if (Ptr(b) && Ptr(p2c) && Load32(base, b + 0xD0) == p2c)
          {
            if (++g.aimed == 1 || g.aimed == 20 || g.aimed == 200)
            {
              REXLOG_INFO("pdz_counterop: twins: WARNING: the AI Brother {}'s target is PLAYER 2 ({:08X}; sample #{}, "
                          "four samples a second): he is not treated as its brother",
                          kTwinName[other], p2c, g.aimed);
            }
          }
        }
      }
      if (g.died && !g.bit_seen)
      {
        if (flags & kTwinBit[g.mine])
        {
          g.bit_seen = true;
          REXLOG_INFO("pdz_counterop: twins: the level script set its bit {:#x} for Brother {} {:.2f} s after player "
                      "2's death (flag word 0 {:08X})",
                      kTwinBit[g.mine], kTwinName[g.mine], Since(g.died_at), flags);
        }
        else if (!g.bit_late_logged && Since(g.died_at) > 5.0)
        {
          g.bit_late_logged = true;
          REXLOG_INFO("pdz_counterop: twins: WARNING: 5 s after player 2's death the level script has not set bit "
                      "{:#x} for Brother {} ({})",
                      kTwinBit[g.mine], kTwinName[g.mine], TwinLine(base, g.mine));
        }
      }
      if (g.died && g.bit_seen && !both_down && !g.wait_logged)
      {
        g.wait_logged = true;
        REXLOG_INFO("pdz_counterop: twins: player 2 is WAITING FOR THE FIGHT'S END (no body: no second twin, no "
                    "guard); {}",
                    TwinLine(base, 1 - g.mine));
      }
      if (both_down || !any_twin)
      {
        const double hold = REXCVAR_GET(pdz_counterop_trinity_twins_after_s);
        const bool fought = g.fought_s >= kFoughtSeconds;
        g.fight_end = now;
        g.next_check = now;
        g.choice = -1;
        REXLOG_INFO("pdz_counterop: twins: THE FIGHT'S END SEEN: flag word 0 {:08X} ({}), level-state byte {:02X}, "
                    "script +0x508 {}, after {:.1f} s of fight; player 2 {}; {}",
                    flags, both_down ? "both bits set: the script sends its level message 00 02 00 00" : "both twins gone",
                    base[kLevelState], Load32(base, script + kScriptStage), g.fought_s,
                    !g.took ? "never had a twin" : g.died ? fmt::format("had been {}", kTwinName[g.mine])
                                                          : fmt::format("is still {} (!)", kTwinName[g.mine]),
                    fought && hold > 0.0
                        ? fmt::format("his next guard is held until a post-fight guard (s01c / s01d) is up, at most "
                                      "{:.0f} s",
                                      hold)
                        : std::string("no hold"));
        if (fought && hold > 0.0)
        {
          g.phase = kAfter;
        }
        else
        {
          g.phase = kNormal;
          REXLOG_INFO("pdz_counterop: twins: NORMAL PICKING RESUMED (at the fight's end)");
        }
      }
    }
    else if (g.phase == kAfter && now >= g.next_check)
    {
      g.next_check = now + std::chrono::milliseconds(500);
      const std::string up = PostFightGuardUp(base);
      const double waited = Since(g.fight_end);
      if (!up.empty() || waited >= REXCVAR_GET(pdz_counterop_trinity_twins_after_s))
      {
        g.phase = kNormal;
        REXLOG_INFO("pdz_counterop: twins: NORMAL PICKING RESUMED {:.1f} s after the fight's end ({})", waited,
                    up.empty() ? "the hold's cap: no post-fight guard of the pick list came up"
                               : fmt::format("post-fight guard '{}' is up", up));
      }
    }
  }

  float SpawnHealth(const uint8_t *base, uint32_t character, float hp)
  {
    if (!On() || !IsTwin(character))
    {
      return hp;
    }
    const int i = character == g.twin[0] ? 0 : 1;
    const float health = LoadFloat(base, character + kHealth), second = LoadFloat(base, character + kArmour);
    const double fixed = REXCVAR_GET(pdz_counterop_trinity_twin_health);
    float total = health + (std::isfinite(second) && second > 0.0f ? second : 0.0f);
    if (fixed > 0.0)
    {
      total = float(fixed);
    }
    if (!std::isfinite(total) || total < 1.0f)
    {
      return hp;
    }
    g.took = true;
    g.mine = i;
    REXLOG_INFO("pdz_counterop: twins: PLAYER 2 TAKES BROTHER {} ({:08X}) at ({:.0f}, {:.0f}, {:.0f}): the twin's pools "
                "are health {:.1f} + second pool {:.1f}: {}; player 2's health and max {:.1f}{} (the guard rule's {:.1f} "
                "is not used), no armour",
                kTwinName[i], character, LoadFloat(base, character + kPosition),
                LoadFloat(base, character + kPosition + 4), LoadFloat(base, character + kPosition + 8), health, second,
                PoolsText(health, second), total,
                fixed > 0.0 ? " (pdz_counterop_trinity_twin_health)" : " (the two pools added up)", hp);
    return total;
  }

  std::vector<uint32_t> ExtraWeaponTypes(PPCContext &ctx, uint8_t *base, uint32_t character, uint32_t gun,
                                         const std::vector<uint32_t> &given)
  {
    std::vector<uint32_t> out;
    if (!On() || !IsTwin(character))
    {
      return out;
    }
    const auto wanted = [&](uint32_t type)
    {
      if (!type || type == kUnarmed || type == gun || !LevelGivesType(base, type))
      {
        return false;
      }
      for (const uint32_t t : given)
      {
        if (t == type)
        {
          return false;
        }
      }
      for (const uint32_t t : out)
      {
        if (t == type)
        {
          return false;
        }
      }
      return true;
    };
    // The twin's inventory: the first entry of each item category (Rare's walk 824F5C70(inventory, 0, category) ->
    // entry {+0 item}); the item's weapon entry (+1248) -> definition -> type (+1144).
    std::string carried;
    PPCFunc *first = pdz::GuestFunction(0x824F5C70, "counter-op twins: inventory walk");
    for (uint32_t category = 0; first && category < 6; ++category)
    {
      rex::CallFrame frame(ctx); // copies r1 / r13 only
      frame.ctx.r3.u64 = character + kInventory;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = category;
      first(frame.ctx, base);
      const uint32_t entry = frame.ctx.r3.u32;
      const uint32_t item = Ptr(entry) ? Load32(base, entry) : 0;
      const uint32_t wentry = Ptr(item) ? Load32(base, item + kWeaponEntry) : 0;
      const uint32_t def = Ptr(wentry) ? Load32(base, wentry) : 0;
      const uint32_t type = Ptr(def) ? Load32(base, def + kWeaponDefType) : 0;
      if (!type)
      {
        continue;
      }
      carried += fmt::format("{}category {}: type {}", carried.empty() ? "" : ", ", category, type);
      if (wanted(type))
      {
        out.push_back(type);
      }
    }
    // Flashbangs: Rare's Brothers script hands its twin one whenever it wants to throw and he has none
    // (821BB8E0-821BB980), so a twin always "has" them.
    bool flash_added = false;
    if (wanted(kFlashbang))
    {
      out.push_back(kFlashbang);
      flash_added = true;
    }
    std::string list;
    for (const uint32_t t : out)
    {
      list += fmt::format("{}{}", list.empty() ? "" : ", ", t);
    }
    REXLOG_INFO("pdz_counterop: twins: Brother {}'s weapons: in hand type {} (given by the takeover); his inventory "
                "holds {}; given to player 2 as well: {}{}",
                character == g.twin[0] ? kTwinName[0] : kTwinName[1], gun, carried.empty() ? "nothing readable" : carried,
                list.empty() ? "nothing more" : "type(s) " + list,
                flash_added ? " (the flashbang as Rare's Brothers script hands it to a twin who has none)" : "");
    return out;
  }
} // namespace counter_op_twins
