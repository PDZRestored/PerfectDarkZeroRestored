// Combat look-hold (Fri 20 Nov 2026; pdz_fix_combat_look_hold, on). TU3 addresses.
//
// THE TWITCH (research Thu 19 Nov, research notes: RESEARCH_twitch_Thu19Nov.md; read again here):
//   A guard's facing step sub_8252E4F8 (r3 = character, r4 = out position) asks sub_82157458 (call at 8252E514,
//   return 8252E518) whether he has something to face: an aim point, a look-at object, or his target VISIBLE in
//   Rare's per-pair table (or one of Rare's own last-known fallbacks).
//     yes: yaw / pitch are SNAPPED at the target (setters vfunc +32 / +36 = sub_8254E0F0 / sub_8254E0F8, plain
//          stores to +5744 / +5748, degrees); no smoothing, no turn rate.
//     no:  pitch 0 and yaw = his body heading (sub_82519A90 x 57.3), then vfunc +804.
//   The visible bit is rewritten by Rare's perception tick each time the guard's visibility pass completes (a budget
//   of 5 ray tests a frame), so at 120 fps a guard redoes it about four times as often as at Rare's 30: a pair at the
//   edge of a clear line flips more often, and his look jumps between Jo and his body heading (the FACING SWINGS
//   lines of the stealth layer, Rare's combat with nothing of ours).
// THE HOLD: when Rare's answer turns from yes to no for the SAME target, a human figure (class 33) keeps the look it
//   had at the target for pdz_fix_combat_look_hold_s of the AI clock (0.25 s: about one visibility pass at Rare's
//   30 fps), then Rare's heading as before. Written after Rare's step, with Rare's own setters' fields: Rare's answer,
//   the visibility tables, the sight word, his target, his tasks and his shots are not touched. The one effect on
//   what Rare is fed: his sight cone follows the look, so during a hold his next sight pass looks the way he was
//   looking a moment ago (as it would have at 30 fps).
// No syscalls: two hooks, one hash look-up per facing step.
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(pdz_fix_combat_look_hold, true, "Gameplay",
                    "Fix (Fri 20 Nov): a guard in a fight keeps looking at his target for a quarter of a second when "
                    "the game's line-of-sight answer drops for a moment, instead of snapping to his body's heading and "
                    "back (the twitch, seen more at high frame rates: the game redoes sight about four times as often "
                    "at 120 fps as at its own 30). The game's fight, sight, aim and shots are unchanged")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_fix_combat_look_hold_s, 0.25, "Gameplay",
                      "pdz_fix_combat_look_hold: how long the look is held (seconds of the game's AI clock)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_fix_combat_look_hold_log, false, "Debug",
                    "Log only: every 30 s a 'pdz_look_hold:' line (facing steps seen, answers that dropped, looks held)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_82157458); // has the character something to face (r3 = character): r3 low byte
REX_EXTERN(__imp__sub_8252E4F8); // the facing step (r3 = character, r4 = out position)

namespace
{
  constexpr uint32_t kAiClock = 0x82E2F334;     // float seconds, per level
  constexpr uint32_t kFacingCallReturn = 0x8252E518;
  constexpr uint32_t kYaw = 5744, kPitch = 5748; // degrees (sub_8254E0F0 / sub_8254E0F8)
  constexpr uint32_t kYawSetter = 0x8254E0F0, kPitchSetter = 0x8254E0F8;
  constexpr uint32_t kClass = 12, kHumanFigure = 33;
  constexpr uint32_t kTargetA = 12652, kTargetB = 12648, kTargetC = 12628; // sub_82157458's target words

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }

  // The facing step's own question, noted by the hook of sub_82157458 (the game thread).
  uint32_t g_asked_for = 0;
  bool g_answer = false;

  struct Look
  {
    float at = -1e9f; // AI clock of the last "yes"
    uint32_t target = 0;
    uint32_t yaw = 0, pitch = 0; // the look at that moment (raw float bits)
    bool yes = false;            // the last answer
    // (log only) the twitch measure: the look's last yaw, and its last jump of more than 10 degrees
    float last_yaw = 0.0f, jump_at = -1e9f;
    int jump_dir = 0;
    bool have_yaw = false;
  };
  std::unordered_map<uint32_t, Look> g_looks;
  float g_last_clock = 0.0f;
  uint64_t g_steps = 0, g_drops = 0, g_held = 0, g_jumps = 0, g_snapbacks = 0;
  float g_log_at = -1e9f;

  uint32_t TargetOf(const uint8_t *base, uint32_t c)
  {
    const uint32_t a = Load32(base, c + kTargetA);
    if (Guest(a))
    {
      return a;
    }
    const uint32_t b = Load32(base, c + kTargetB);
    return Guest(b) ? b : Load32(base, c + kTargetC);
  }
} // namespace

REX_HOOK_RAW(sub_82157458)
{
  const uint32_t lr = uint32_t(ctx.lr), c = ctx.r3.u32;
  __imp__sub_82157458(ctx, base);
  if (lr == kFacingCallReturn)
  {
    g_asked_for = c;
    g_answer = (ctx.r3.u32 & 0xFFu) != 0;
  }
}

REX_HOOK_RAW(sub_8252E4F8)
{
  const uint32_t c = ctx.r3.u32;
  g_asked_for = 0;
  __imp__sub_8252E4F8(ctx, base);
  const bool hold = REXCVAR_GET(pdz_fix_combat_look_hold), log = REXCVAR_GET(pdz_fix_combat_look_hold_log);
  if ((!hold && !log) || !Guest(c) || g_asked_for != c)
  {
    return;
  }
  const uint32_t vtable = Load32(base, c);
  if (!Guest(vtable) || Load32(base, c + kClass) != kHumanFigure || Load32(base, vtable + 32) != kYawSetter ||
      Load32(base, vtable + 36) != kPitchSetter)
  {
    return; // (only a human figure whose look is Rare's two plain fields)
  }
  const float now = LoadF(base, kAiClock);
  if (!(now == now) || now < g_last_clock - 1.0f || g_looks.size() > 4096)
  {
    g_looks.clear(); // (a new level or attempt: the AI clock went back)
  }
  g_last_clock = now;
  ++g_steps;
  const uint32_t target = TargetOf(base, c);
  if (g_answer && !hold)
  {
    g_looks[c].yes = true; // (log only: the hold is off)
  }
  else if (!g_answer && !hold)
  {
    const auto it = g_looks.find(c);
    if (it != g_looks.end())
    {
      g_drops += it->second.yes ? 1 : 0;
      it->second.yes = false;
    }
  }
  else if (g_answer)
  {
    Look &l = g_looks[c];
    l.at = now;
    l.target = target;
    l.yaw = Load32(base, c + kYaw);
    l.pitch = Load32(base, c + kPitch);
    l.yes = true;
  }
  else
  {
    const auto it = g_looks.find(c);
    if (it != g_looks.end() && it->second.at > -1e8f)
    {
      Look &l = it->second;
      g_drops += l.yes ? 1 : 0;
      l.yes = false;
      const float since = now - l.at;
      if (Guest(target) && target == l.target && since >= 0.0f && since <= float(REXCVAR_GET(pdz_fix_combat_look_hold_s)))
      {
        Store32(base, c + kYaw, l.yaw); // (as Rare's own setters do: plain stores)
        Store32(base, c + kPitch, l.pitch);
        ++g_held;
      }
    }
  }
  if (log)
  {
    // The twitch measure (both with the hold on and off): a jump of the look by more than 10 degrees in one step,
    // and a SNAP-BACK = a jump the other way within 0.6 s of the one before (the two-value snap of the twitch).
    Look &l = g_looks[c];
    const float yaw = LoadF(base, c + kYaw);
    if (l.have_yaw && yaw == yaw)
    {
      float turn = yaw - l.last_yaw;
      turn = turn > 180.0f ? turn - 360.0f : turn < -180.0f ? turn + 360.0f : turn;
      if (turn > 10.0f || turn < -10.0f)
      {
        ++g_jumps;
        const int dir = turn > 0.0f ? 1 : -1;
        if (l.jump_dir == -dir && now - l.jump_at <= 0.6f && now >= l.jump_at)
        {
          ++g_snapbacks;
        }
        l.jump_dir = dir;
        l.jump_at = now;
      }
    }
    l.last_yaw = yaw;
    l.have_yaw = yaw == yaw;
  }
  if (log && (now - g_log_at >= 30.0f || now < g_log_at))
  {
    g_log_at = now;
    REXLOG_INFO("pdz_look_hold: {} facing steps of human figures, {} times the game's answer was no after a yes, {} looks held "
                "on the target (pdz_fix_combat_look_hold {}, {:.2f} s); look jumps over 10 deg {}, SNAP-BACKS (a jump back within "
                "0.6 s) {}; {} figures followed",
                g_steps, g_drops, g_held, hold ? "on" : "off", REXCVAR_GET(pdz_fix_combat_look_hold_s), g_jumps, g_snapbacks,
                g_looks.size());
  }
}
