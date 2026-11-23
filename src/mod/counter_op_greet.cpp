// Counter-Operative: guards greet player 2 with Rare's unused SEE_FRIEND_TEAM takes (round 2, design item 11).
// Draft, not built. Evidence: research notes: round2\c\FINDINGS.md.
//
// Rare's line: event 167 (the Laboratory's greeting slot, the same cue string 820947DC as 107:
// <VOICE>_SEE_FRIEND_TEAM, "Greetings!", "Hey there!", "Carry on!", "At ease."...). Every generic voice has 4-5 takes
// in its <voice>_gen_s wave bank, and Rare's voice loader 822048D8 loads <voice>.xsb + _gen_m.xwb + _gen_s.xwb for
// every voice a level uses, so every guard voice of every test level has them. 167 rather than 107: 107 is in sound
// group 8 (at most 5 lines per level), 167 in group 16 (no level limit), category 19 (no extra condition).
//
// The call: Rare's say entry sub_8262AA40(character + 0x3190, event, r5 1 = the plain take (no _MALE), r6 0, f1 0),
// as restorations.cpp and the guards' own target step (82580BF8) call it. Rare's own gates then apply:
//   - the speaker can talk at all (+0x187C bit 0x4, vt+0xE8, +0x1880 bit 0x00800000);
//   - category 19 not muted (+0x3190 +0xBC);
//   - each guard says a group-16 line once per life (+0x3190 +0xC4 bit 16, set when he speaks, cleared only when
//     he is built: 8262A220 / 8262A100), and his own minimum gap since his last line (+0x90 time, +0x64 gap);
//   - one queued line per guard (+0xD8; a busy guard keeps his);
//   - when the line starts (speech task 821AEB78 -> 82152E20): at most 10 talkers in the level, and group 16's
//     level-wide gap of 60 s since anyone's last group-16 line (table 82E44B78, built by 821524F8).
// So Rare's own rules already give "at most one greeting a minute, each guard once". On top (ours): only when
// player 2 is in the world as a guard; the speaker alive, shown, calm (counter_op.cpp's rule plus the unaware
// bits), within pdz_counterop_greet_range (4.5 m, |dy| < 2 m), facing him (Rare's own view-cone test 82153890:
// +-60 degrees from his eye), not talking and nobody else talking within 20 m; one chance roll per guard per
// 30 s (pdz_counterop_greet_chance); and our own gap after any greeting (pdz_counterop_greet_gap). We also check
// Rare's group gap before asking, so a line is never queued only to be held and played late.
//
// Call-in points for counter_op.cpp (marked COUNTER-OP GREET there):
//   - at the end of Tick(), when player 2 is in the world as a guard (g.started, !g.out, !g.in_cutscene, alive):
//       PdzCounterOpGreetTick(ctx, base, g.p2c);
//   - in Reset():  PdzCounterOpGreetReset();
// Add this file to the PDZ_COUNTEROP target_sources line in CMakeLists.txt.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DEFINE_BOOL(pdz_counterop_greet, true, "Gameplay",
                    "Counter-Operative (default on): calm guards player 2 walks past may greet him with Rare's unused "
                    "SEE_FRIEND_TEAM lines ('Greetings!', 'Hey there!', 'Carry on!'). Rare's own speech rules decide "
                    "the rest: one guard at a time, each guard at most once, a minute between greetings")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_greet_range, 600.0, "Gameplay",
                      "Counter-Operative greetings: how close a guard must be (game units, 100 = 1 m; round 2: 450)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_greet_chance, 0.75, "Gameplay",
                      "Counter-Operative greetings: the chance a guard who could greet player 2 does (rolled once per "
                      "guard every 30 s; round 2: 0.35)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_greet_gap, 20.0, "Gameplay",
                      "Counter-Operative greetings: seconds after a greeting before another guard may be asked")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_greet_own_gap, true, "Gameplay",
                    "Counter-Operative greetings (round 3, default on): our own rules (one voice at a time, "
                    "pdz_counterop_greet_gap between greetings, each guard once) replace Rare's two rules for this "
                    "line's sound group (a minute between any two of the group's lines in the level, one per guard "
                    "per life), which the fight's own lines use up in real play. Off = round 2 (Rare's rules too)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_greet_strict_calm, false, "Gameplay",
                    "Counter-Operative greetings: only guards that were never in a fight greet him (round 2's rule). "
                    "Default off: a guard greets when he is calm right now (no target, not perceiving a hostile)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_greet_facing, true, "Gameplay",
                    "Counter-Operative greetings: when Rare's view cone (82153890) says no, the guard still sees him if "
                    "his own facing (Rare's yaw 82519A90) is within 60 degrees of player 2 (on Subway the cone said no "
                    "every time, from every side)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_greet_walk, 0, "Testing",
                     "TESTING TOOL (counter-op): once, this many seconds into the level, player 2 is put 8 m in front of "
                     "the nearest calm guard, facing him, for an input script to walk him past (the greeting itself "
                     "then follows the usual rules); 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_greet, 0, "Testing",
                     "TESTING TOOL (counter-op): every this many seconds (0 = off) player 2 is moved 2.5 m from the "
                     "nearest calm guard (each time on another side), so a scripted run can show a greeting")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(sub_825AD148); // move a character (character, &position, 0)
REX_EXTERN(sub_8262AA40); // say: (speech component = character + 0x3190, event, plain take, r6, f1 delay)
REX_EXTERN(sub_825AD3B0); // a human's look angles (character, f1 pitch, f2 yaw; radians)
REX_EXTERN(sub_82519A90); // a character's yaw in radians (f1; ai_game_fixes.cpp hooks it, a pass-through here)
REX_EXTERN(sub_82153890); // in view: (&point, viewer character) -> bool (+-60 degrees from the viewer's eye)

namespace
{
  constexpr uint32_t kGameWorld = 0x82D6BFD8 + 24;    // -> world (the object list)
  constexpr uint32_t kGameLevel = 0x82D6BFD8 + 0x1C;  // -> +0x2D8 level manager -> +0x70 level sound manager
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216; // world: object nodes (+4 object, +12 next)
  constexpr uint32_t kGuardVtable = 0x820C45A0;
  constexpr uint32_t kGameTime = 0x82E2F334;           // float seconds (Rare's speech timers)
  constexpr uint32_t kGroupTable = 0x82E44B78;         // per group: u32 limit, u16 count, u8 gap s, u8
  constexpr uint32_t kGreetEvent = 167, kGreetGroup = 16, kGreetCategory = 19;
  constexpr uint32_t kPosition = 48, kHealth = 120, kHidden = 6272;
  constexpr uint32_t kVoice = 0x3190;                  // speech component
  constexpr uint32_t kVoiceTask = 0x40, kVoiceQueued = 0xD8, kVoiceMuted = 0xBC, kVoiceGroupsSaid = 0xC4;
  constexpr uint32_t kVoiceLast = 0x90, kVoiceGap = 0x64; // double last line time, float own gap
  constexpr float kBusyRadius = 2000.0f, kMaxDy = 200.0f;
  constexpr double kReRollSeconds = 30.0;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24);
    base[a + 1] = uint8_t(v >> 16);
    base[a + 2] = uint8_t(v >> 8);
    base[a + 3] = uint8_t(v);
  }
  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t bits = Load32(base, a);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
  }
  void StoreFloat(uint8_t *base, uint32_t a, float f)
  {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    Store32(base, a, bits);
  }
  double LoadDouble(const uint8_t *base, uint32_t a)
  {
    const uint64_t bits = (uint64_t(Load32(base, a)) << 32) | Load32(base, a + 4);
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  struct Roll
  {
    double at = -1e9; // game time of the last roll
    bool yes = false;
  };
  struct GreetState
  {
    std::unordered_map<uint32_t, Roll> rolls; // guard -> last roll
    double next_allowed = 0.0;                // game time
    double test_next = 0.0;                   // pdz_counterop_test_greet
    uint32_t test_side = 0;
    double walk_from = -1.0;                  // pdz_counterop_test_greet_walk: game time of his first greet tick
    bool walk_done = false;
    uint32_t scratch = 0;                     // guest vec3 for the view test
    uint32_t facing_lines = 0;                // facing-fallback log lines
    uint32_t said = 0;
    std::unordered_set<uint32_t> greeted; // round 3: guards that have greeted him (once each)
    std::mt19937 rng{std::random_device{}()};
  } gs;

  // counter_op.cpp's calm rule (not perceiving a hostile this frame, never engaged or shot, no target) plus the
  // unaware bits of the stealth notes (+6252 0x2 / 0x4 / 0x10).
  bool Calm(const uint8_t *base, uint32_t c)
  {
    // Round 3: in real play nearly every guard near the players has been in the fight once (+6252 0x2 engaged, 0x80
    // shot, the unaware bits), so "never engaged" left nobody to greet. Calm now = calm right now: not perceiving a
    // hostile this frame and no current target. pdz_counterop_greet_strict_calm restores round 2's rule.
    if (REXCVAR_GET(pdz_counterop_greet_strict_calm) && (Load32(base, c + 6252) & 0x96u))
    {
      return false;
    }
    return !(Load32(base, c + 6268) & 0x400000u) && !Load32(base, c + 0x317C);
  }

  void StoreDouble(uint8_t *base, uint32_t a, double d)
  {
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    Store32(base, a, uint32_t(bits >> 32));
    Store32(base, a + 4, uint32_t(bits));
  }

  // Rare's level sound manager (the group timers), or 0.
  uint32_t SoundManager(const uint8_t *base)
  {
    const uint32_t level = Load32(base, kGameLevel);
    const uint32_t manager = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t sound = pdz::guestmem::Ok(base, manager, 0x74) ? Load32(base, manager + 0x70) : 0; // Sun 22 Nov night (audit): Ok
    return Ptr(sound) ? sound : 0;
  }

  bool Talking(const uint8_t *base, uint32_t c)
  {
    return Load32(base, c + kVoice + kVoiceTask) != 0 || Load32(base, c + kVoice + kVoiceQueued) != 0;
  }

  // Rare's level-wide gate for group 16 as 82152E20 checks it when the line would start: 0 = fine now.
  bool GroupReady(const uint8_t *base, double now)
  {
    const uint32_t level = Load32(base, kGameLevel);
    const uint32_t manager = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t sound = pdz::guestmem::Ok(base, manager, 0x74) ? Load32(base, manager + 0x70) : 0; // Sun 22 Nov night (audit): Ok
    if (!pdz::guestmem::Ok(base, sound, 0x259))
    {
      return false;
    }
    if (base[sound + 0x258] >= 10)
    {
      return false; // ten talkers already
    }
    const uint32_t entry = kGroupTable + kGreetGroup * 8;
    const int32_t limit = int32_t(Load32(base, entry));
    const uint32_t count = (uint32_t(base[entry + 4]) << 8) | base[entry + 5];
    if (limit >= 0 && count >= uint32_t(limit))
    {
      return false;
    }
    if (REXCVAR_GET(pdz_counterop_greet_own_gap))
    {
      return true; // round 3: the group's minute is replaced by our own gap (see the ask)
    }
    const double last = LoadDouble(base, sound + kGreetGroup * 8);
    return now - last >= double(base[entry + 6]);
  }

  // Rare's per-speaker gates in 8262AA40 / 8262B378 / 8262BC00 that we can read, so a roll is not spent on a guard
  // Rare would refuse anyway.
  bool SpeakerReady(const uint8_t *base, uint32_t c, double now)
  {
    const uint32_t v = c + kVoice;
    if (Load32(base, v + kVoiceMuted) & (1u << kGreetCategory))
    {
      return false;
    }
    if (!REXCVAR_GET(pdz_counterop_greet_own_gap) && (Load32(base, v + kVoiceGroupsSaid) & (1u << kGreetGroup)))
    {
      return false; // he has said a group-16 line this life (round 3: our own once-per-guard rule instead)
    }
    if (gs.greeted.count(c))
    {
      return false; // each guard greets him once
    }
    if (now - LoadDouble(base, v + kVoiceLast) < double(LoadFloat(base, v + kVoiceGap)))
    {
      return false;
    }
    return (Load32(base, c + 0x1880) & 0x00800000u) == 0;
  }
} // namespace

void PdzCounterOpGreetReset()
{
  gs.rolls.clear();
  gs.greeted.clear();
  gs.next_allowed = 0.0;
  gs.said = 0;
  gs.walk_from = -1.0;
  gs.walk_done = false;
}

// Every frame while player 2 is in the world as a guard (counter_op.cpp's Tick).
namespace
{
  // Why no greeting happened, summed over 10 s and logged (the test logs need it: a greeting is a chance event).
  struct GreetDiag
  {
    uint32_t ticks = 0, time_bad = 0, gap = 0, group = 0, none = 0, talking = 0, cone = 0, roll = 0, asked = 0;
    uint32_t near_guards = 0, not_calm = 0, not_ready = 0, hidden = 0;
    float nearest = 1e9f;
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
  } gd;
  void GreetDiagLine()
  {
    if (std::chrono::steady_clock::now() - gd.since < std::chrono::seconds(10))
    {
      return;
    }
    if (gd.ticks)
    {
      REXLOG_INFO("pdz_counterop: greet (10 s): {} ticks: game time unreadable {}, our gap {}, Rare's group gap {}, no "
                  "guard in range {} (guards within {:.1f} m: {}, nearest {:.1f} m; refused: not calm {}, speaker not "
                  "ready {}, hidden {}), someone talking {}, out of the guard's view {}, chance said no {}, asked {}",
                  gd.ticks, gd.time_bad, gd.gap, gd.group, gd.none, REXCVAR_GET(pdz_counterop_greet_range) / 100.0,
                  gd.near_guards, gd.nearest < 1e8f ? gd.nearest / 100.0f : -1.0f, gd.not_calm, gd.not_ready,
                  gd.hidden, gd.talking, gd.cone, gd.roll, gd.asked);
    }
    gd = GreetDiag{};
  }
} // namespace

void PdzCounterOpGreetTick(PPCContext &ctx, uint8_t *base, uint32_t p2c)
{
  if (!REXCVAR_GET(pdz_counterop_greet) || !Ptr(p2c))
  {
    return;
  }
  GreetDiagLine();
  ++gd.ticks;
  const double now = double(LoadFloat(base, kGameTime));
  // pdz_counterop_test_greet_walk: once, 8 m in front of the nearest calm guard, facing him.
  if (const int walk = REXCVAR_GET(pdz_counterop_test_greet_walk); walk > 0 && !gs.walk_done && std::isfinite(now))
  {
    if (gs.walk_from < 0.0)
    {
      gs.walk_from = now;
    }
    const uint32_t world = Load32(base, kGameWorld);
    if (now >= gs.walk_from + walk && Ptr(world))
    {
      const float px = LoadFloat(base, p2c + kPosition), pz = LoadFloat(base, p2c + kPosition + 8);
      uint32_t best = 0;
      float best_d2 = 1e18f;
      for (uint32_t node = Load32(base, world + kWorldFirst), end = Load32(base, world + kWorldEnd), i = 0;
           Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
      {
        const uint32_t c = Load32(base, node + 4);
        if (!Ptr(c) || c == p2c || Load32(base, c) != kGuardVtable || LoadFloat(base, c + kHealth) <= 0.0f ||
            (Load32(base, c + kHidden) & 0x10u) || !Calm(base, c))
        {
          continue;
        }
        const float dx = LoadFloat(base, c + kPosition) - px, dz = LoadFloat(base, c + kPosition + 8) - pz;
        if (dx * dx + dz * dz < best_d2)
        {
          best_d2 = dx * dx + dz * dz;
          best = c;
        }
      }
      if (!gs.scratch)
      {
        gs.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
      }
      if (best && gs.scratch)
      {
        gs.walk_done = true;
        rex::CallFrame frame(ctx);
        frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400) & ~0xFu;
        frame.ctx.r3.u64 = best;
        sub_82519A90(frame.ctx, base);
        const double yaw = frame.ctx.f1.f64;
        const float gx = LoadFloat(base, best + kPosition), gz = LoadFloat(base, best + kPosition + 8);
        StoreFloat(base, gs.scratch, gx + 800.0f * float(std::sin(yaw)));
        StoreFloat(base, gs.scratch + 4, LoadFloat(base, best + kPosition + 4));
        StoreFloat(base, gs.scratch + 8, gz + 800.0f * float(std::cos(yaw)));
        frame.ctx.r3.u64 = p2c;
        frame.ctx.r4.u64 = gs.scratch;
        frame.ctx.r5.u64 = 0;
        sub_825AD148(frame.ctx, base);
        frame.ctx.r3.u64 = p2c;
        frame.ctx.f1.f64 = 0.0;
        frame.ctx.f2.f64 = yaw + 3.14159265358979; // facing the guard
        sub_825AD3B0(frame.ctx, base);
        REXLOG_INFO("pdz_counterop: test greet walk: player 2 put 8 m in front of calm guard {:08X} (his yaw {:.1f} "
                    "deg), facing him ({:.0f} m away before)",
                    best, yaw * 57.29578, std::sqrt(best_d2) / 100.0f);
      }
    }
  }
  // pdz_counterop_test_greet: put him next to the nearest calm guard now and then (a side each time).
  if (const int every = REXCVAR_GET(pdz_counterop_test_greet); every > 0 && std::isfinite(now) &&
                                                                 now >= gs.test_next)
  {
    gs.test_next = now + every;
    const uint32_t world = Load32(base, kGameWorld);
    const float px = LoadFloat(base, p2c + kPosition), pz = LoadFloat(base, p2c + kPosition + 8);
    uint32_t best = 0;
    float best_d2 = 1e18f;
    for (uint32_t node = Ptr(world) ? Load32(base, world + kWorldFirst) : 0, end = Ptr(world) ? Load32(base, world + kWorldEnd) : 0, i = 0;
         Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t c = Load32(base, node + 4);
      if (!Ptr(c) || c == p2c || Load32(base, c) != kGuardVtable || LoadFloat(base, c + kHealth) <= 0.0f ||
          (Load32(base, c + kHidden) & 0x10u) || !Calm(base, c))
      {
        continue;
      }
      const float dx = LoadFloat(base, c + kPosition) - px, dz = LoadFloat(base, c + kPosition + 8) - pz;
      if (dx * dx + dz * dz < best_d2)
      {
        best_d2 = dx * dx + dz * dz;
        best = c;
      }
    }
    if (best)
    {
      if (!gs.scratch)
      {
        gs.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
      }
      if (gs.scratch)
      {
        static const float kSide[4][2] = {{250.0f, 0.0f}, {0.0f, 250.0f}, {-250.0f, 0.0f}, {0.0f, -250.0f}};
        const float *off = kSide[gs.test_side++ % 4];
        StoreFloat(base, gs.scratch, LoadFloat(base, best + kPosition) + off[0]);
        StoreFloat(base, gs.scratch + 4, LoadFloat(base, best + kPosition + 4));
        StoreFloat(base, gs.scratch + 8, LoadFloat(base, best + kPosition + 8) + off[1]);
        rex::CallFrame frame(ctx);
        frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400) & ~0xFu;
        frame.ctx.r3.u64 = p2c;
        frame.ctx.r4.u64 = gs.scratch;
        frame.ctx.r5.u64 = 0;
        sub_825AD148(frame.ctx, base);
        REXLOG_INFO("pdz_counterop: test greet: player 2 moved 2.5 m from calm guard {:08X} ({:.0f} m away before)",
                    best, std::sqrt(best_d2) / 100.0f);
      }
    }
  }
  if (!std::isfinite(now))
  {
    ++gd.time_bad;
    return;
  }
  if (now < gs.next_allowed)
  {
    ++gd.gap;
    return;
  }
  if (!GroupReady(base, now))
  {
    ++gd.group;
    return;
  }
  const uint32_t world = Load32(base, kGameWorld);
  if (!Ptr(world))
  {
    return;
  }
  const float px = LoadFloat(base, p2c + kPosition), py = LoadFloat(base, p2c + kPosition + 4),
              pz = LoadFloat(base, p2c + kPosition + 8);
  const float range = float(REXCVAR_GET(pdz_counterop_greet_range));
  uint32_t best = 0;
  float best_d2 = range * range;
  const uint32_t end = Load32(base, world + kWorldEnd);
  uint32_t node = Load32(base, world + kWorldFirst);
  for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
  {
    const uint32_t c = Load32(base, node + 4);
    if (!Ptr(c) || c == p2c || Load32(base, c) != kGuardVtable)
    {
      continue;
    }
    const float dx = LoadFloat(base, c + kPosition) - px, dy = LoadFloat(base, c + kPosition + 4) - py,
                dz = LoadFloat(base, c + kPosition + 8) - pz;
    const float d2 = dx * dx + dz * dz;
    if (!std::isfinite(d2))
    {
      continue;
    }
    if (d2 < kBusyRadius * kBusyRadius && Talking(base, c))
    {
      ++gd.talking;
      return; // someone near him is talking: wait
    }
    gd.nearest = std::min(gd.nearest, std::sqrt(d2));
    if (d2 >= best_d2 || std::fabs(dy) > kMaxDy || LoadFloat(base, c + kHealth) <= 0.0f)
    {
      continue;
    }
    ++gd.near_guards;
    if (Load32(base, c + kHidden) & 0x10u)
    {
      ++gd.hidden;
      continue;
    }
    if (!Calm(base, c))
    {
      ++gd.not_calm;
      continue;
    }
    if (!SpeakerReady(base, c, now))
    {
      ++gd.not_ready;
      continue;
    }
    best = c;
    best_d2 = d2;
  }
  if (!best)
  {
    ++gd.none;
    return;
  }
  // Rare's view cone from the guard's eye to player 2's chest.
  if (!gs.scratch)
  {
    gs.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
    if (!gs.scratch)
    {
      return;
    }
  }
  StoreFloat(base, gs.scratch, px);
  StoreFloat(base, gs.scratch + 4, py + 120.0f);
  StoreFloat(base, gs.scratch + 8, pz);
  bool sees;
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400) & ~0xFu;
    frame.ctx.r3.u64 = gs.scratch;
    frame.ctx.r4.u64 = best;
    sub_82153890(frame.ctx, base);
    sees = (frame.ctx.r3.u32 & 0xFF) != 0;
  }
  if (!sees && REXCVAR_GET(pdz_counterop_greet_facing))
  {
    double yaw = 0.0, own = 0.0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400) & ~0xFu;
      frame.ctx.r3.u64 = best;
      sub_82519A90(frame.ctx, base);
      yaw = frame.ctx.f1.f64;
      frame.ctx.r3.u64 = p2c;
      sub_82519A90(frame.ctx, base);
      own = frame.ctx.f1.f64;
    }
    const double to = std::atan2(double(px - LoadFloat(base, best + kPosition)),
                                 double(pz - LoadFloat(base, best + kPosition + 8)));
    double diff = std::remainder(to - yaw, 2.0 * 3.14159265358979);
    sees = std::isfinite(diff) && std::fabs(diff) < 60.0 * 3.14159265358979 / 180.0;
    // Round 11 (item 5): one line per guard per change of the answer, not one a frame.
    static std::unordered_map<uint32_t, bool> facing_logged;
    if (facing_logged.size() > 512)
    {
      facing_logged.clear();
    }
    const auto logged = facing_logged.find(best);
    if (logged == facing_logged.end() || logged->second != sees)
    {
      facing_logged[best] = sees;
      ++gs.facing_lines;
      REXLOG_INFO("pdz_counterop: greet: Rare's cone said no for guard {:08X}; his yaw {:.1f} deg, player 2 at "
                  "{:.1f} deg from him ({:.1f} off): {} (player 2's own yaw {:.1f}, view yaw +0x1670 {:.1f})",
                  best, yaw * 57.29578, to * 57.29578, diff * 57.29578, sees ? "facing him" : "not facing",
                  own * 57.29578, LoadFloat(base, p2c + 0x1670));
    }
  }
  if (!sees)
  {
    ++gd.cone;
    return;
  }
  Roll &roll = gs.rolls[best];
  if (now - roll.at < kReRollSeconds && !roll.yes)
  {
    return; // he passed on it recently
  }
  if (now - roll.at >= kReRollSeconds)
  {
    roll.at = now;
    roll.yes = std::uniform_real_distribution<double>(0.0, 1.0)(gs.rng) < REXCVAR_GET(pdz_counterop_greet_chance);
    if (!roll.yes)
    {
      ++gd.roll;
      return;
    }
  }
  if (REXCVAR_GET(pdz_counterop_greet_own_gap))
  {
    // Round 3: in real play Rare's two group-16 rules (a minute between any two group-16 lines in the level, and each
    // guard one group-16 line per life) are used up by the fight's own lines, so no greeting ever started. Ours
    // replace them for this one line: our gap (pdz_counterop_greet_gap) and once per guard; Rare's timer for the
    // group is set back by its own gap and the guard's "said it" bit cleared, just before the ask.
    if (const uint32_t sound = SoundManager(base))
    {
      StoreDouble(base, sound + kGreetGroup * 8, now - double(base[kGroupTable + kGreetGroup * 8 + 6]) - 1.0);
    }
    const uint32_t said = Load32(base, best + kVoice + kVoiceGroupsSaid);
    Store32(base, best + kVoice + kVoiceGroupsSaid, said & ~(1u << kGreetGroup));
  }
  gs.greeted.insert(best);
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400) & ~0xFu;
    frame.ctx.r3.u64 = best + kVoice;
    frame.ctx.r4.u64 = kGreetEvent;
    frame.ctx.r5.u64 = 1; // the plain take (SEE_FRIEND_TEAM has no _MALE takes)
    frame.ctx.r6.u64 = 0; // Rare's queue checks on
    frame.ctx.f1.f64 = 0.0;
    sub_8262AA40(frame.ctx, base);
  }
  roll.yes = false; // once asked, the next roll waits kReRollSeconds
  gs.next_allowed = now + REXCVAR_GET(pdz_counterop_greet_gap);
  ++gs.said;
  ++gd.asked;
  REXLOG_INFO("pdz_counterop: greet #{}: guard {:08X} asked to say SEE_FRIEND_TEAM (167) to player 2 at {:.1f} m{}",
              gs.said, best, std::sqrt(best_d2) / 100.0f,
              Load32(base, best + kVoice + kVoiceQueued) == kGreetEvent ? " (queued by Rare)"
                                                                         : " (Rare refused or started it at once)");
}
