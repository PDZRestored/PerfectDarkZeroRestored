// Counter-Operative, Sat 21 Nov: player 2 gets a body quickly and close to the agent.
//
// Log 033 (Jungle, Agent): player 2 waited 61 s, then got 's03g' (asleep, held for his group's scene, 73.5 m away).
//   1. Jungle has no area table: the area rule refused all 66 placed guards on every try (they all sleep until the
//      script wakes them), and the relax steps used the same rule.
//   2. The twelve live guards fighting near the agent were Rare's spawned wave guards, all named 's01a', and 's01a' was
//      on Jungle's denied list (our own 8 Nov guess, not a proven need of the level script).
//   3. Relax step 2 took a sleeping, scene-held guard once round 16's time limit on the scene hold ran out.
//   4. The wait view was upside down.
// Each fix has its own switch (default on; off = the behaviour before this job):
//   pdz_counterop_deny_proven:       the denied lists reviewed against Rare's level scripts (DENY_REVIEW_Sat21Nov.md):
//                                    an id stays denied only where taking him is shown to break the script.
//   pdz_counterop_take_spawned:      spawned copies of a takeable id (wave / spawner guards sharing one name) may be
//                                    taken, not only the level's originals.
//   pdz_counterop_untabled_by_position: on a level with no area table, awake guards within reach of the agent
//                                    (pdz_counterop_untabled_reach_m), ahead of her before behind her; the relax steps
//                                    widen the reach.
//   pdz_counterop_relax_awake_only:  the relax steps never take a sleeping (dormant) or scene-held guard: if nothing
//                                    fits, player 2 keeps waiting.
//   pdz_counterop_scene_hold_no_timeout: the scene hold is never lifted by time (round 16 lifted it after 30 / 60 s).
//   pdz_counterop_wait_view_upright: while he waits, his hidden body (whose first-person view is his half of the
//                                    screen) is carried behind and above the agent, facing her look (counter_op.cpp
//                                    WaitViewTick); logged once a second.
#include <rex/cvar.h>
#include <rex/logging.h>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

REXCVAR_DEFINE_BOOL(pdz_counterop_deny_proven, true, "Gameplay",
                    "Counter-Operative (Sat 21 Nov, default on): each mission's denied guard ids are the reviewed list "
                    "(DENY_REVIEW_Sat21Nov.md): an id stays denied only where Rare's level script is shown to need that "
                    "guard (it waits on him, moves him, gives him a task, spawns something from him, or he is a named "
                    "character, boss, hostage or vehicle crew); the freed ids may be taken. Off = the 8 Nov lists")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(pdz_counterop_take_spawned, true, "Gameplay",
                    "Counter-Operative (Sat 21 Nov, default on): guards the level spawns as copies of a takeable id "
                    "(wave or spawner guards sharing one name, e.g. Jungle's 's01a') may be taken like the level's "
                    "originals. Off = such a later wave is never taken (round 2)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_untabled_by_position, true, "Gameplay",
                    "Counter-Operative (Sat 21 Nov, default on): on a level with no area table (Datacore, Mansion, "
                    "Laboratory, River, Jungle, Temple, Outpost, Bridge) player 2 is given an awake guard within "
                    "pdz_counterop_untabled_reach_m of the agent, ahead of her before behind her; after "
                    "pdz_counterop_area_relax_s the reach doubles, after twice that any distance. Sleeping guards are "
                    "never taken there. Off = any awake guard at any distance")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_untabled_reach_m, 60.0, "Gameplay",
                      "Counter-Operative, pdz_counterop_untabled_by_position: the reach (m) from the agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_relax_awake_only, true, "Gameplay",
                    "Counter-Operative (Sat 21 Nov, default on): when no guard fits the area rule for a while, the "
                    "relaxed picks still never take a sleeping (dormant) or scene-held guard: player 2 keeps waiting "
                    "for an awake one. Off = round 16 (relax step 2 took any eligible guard)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_scene_hold_no_timeout, true, "Gameplay",
                    "Counter-Operative (Sat 21 Nov, default on): a sleeping guard that Rare's level script still holds "
                    "for his group's wake is never taken before the level wakes him, however long player 2 has waited "
                    "(pdz_counterop_scene_hold_max_s is not applied). Off = round 16: the hold lifts after 30 s (60 s "
                    "for a guard in no wake group; log 033 took Jungle's sleeping 's03g' 73 m away that way)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_wait_view_upright, true, "Gameplay",
                    "Counter-Operative (Sat 21 Nov, default on): while player 2 waits for a body his view is upright, "
                    "behind and above the agent: his half of the screen is the first-person view of his own hidden "
                    "body, so that body is carried every tick to 2.5 m behind her and 0.7 m above her feet, facing "
                    "where she looks (or to her own place when a wall is behind her), and logged once a second. Off = "
                    "the body stays where the camera probe left it (3 m under her feet: under the Jungle's water the "
                    "view was upside down, log 033) or where he died")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  struct Proven
  {
    const char *mission;
    const char *denied; // kept: a proven need of the level script (DENY_REVIEW_Sat21Nov.md)
    const char *freed;  // freed: no proven need; eligible from now on
  };
  // Filled from the review (deny_review\new_denied.tsv / freed.tsv of the job folder). A mission not listed keeps its
  // 8 Nov list.
#include "counter_op_deny_Sat21Nov.inc"

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
  void StoreFloat(uint8_t *base, uint32_t a, float f)
  {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    uint8_t *p = base + a;
    p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
} // namespace

// The reviewed lists of a mission (nullptr = not reviewed: the 8 Nov list stays).
const char *PdzCounterOpProvenDenied(const std::string &mission, const char **freed)
{
  if (!REXCVAR_GET(pdz_counterop_deny_proven))
  {
    return nullptr;
  }
  for (const Proven &p : kProven)
  {
    if (mission == p.mission)
    {
      if (freed)
      {
        *freed = p.freed;
      }
      return p.denied;
    }
  }
  return nullptr;
}

bool PdzCounterOpTakeSpawned() { return REXCVAR_GET(pdz_counterop_take_spawned); }
bool PdzCounterOpRelaxAwakeOnly() { return REXCVAR_GET(pdz_counterop_relax_awake_only); }
bool PdzCounterOpSceneHoldNoTimeout() { return REXCVAR_GET(pdz_counterop_scene_hold_no_timeout); }
bool PdzCounterOpWaitViewUprightOn() { return REXCVAR_GET(pdz_counterop_wait_view_upright); }
bool PdzCounterOpUntabledByPosition() { return REXCVAR_GET(pdz_counterop_untabled_by_position); }
float PdzCounterOpUntabledReachCm(int relax)
{
  const float m = float(std::max(1.0, REXCVAR_GET(pdz_counterop_untabled_reach_m)));
  return relax >= 2 ? 1e30f : relax == 1 ? 2.0f * m * 100.0f : m * 100.0f;
}

// Called right after the spectate write (counter_op.cpp) while player 2 waits, outside a spiderbot drive: mode = his
// camera object (+4 position, +16 look point, +0x34 up, +124 the three sway angles); eye = the agent's eye.
REXCVAR_DEFINE_DOUBLE(pdz_counterop_test_first_pick_s, 0.0, "Testing",
                      "TESTING TOOL (Sat 21 Nov): the first pick of a level (and, since the spectate job, the pick after a death) "
                      "waits this many seconds instead of 3 / 2.5 (so a "
                      "test sees the waiting view for longer). 0 = 3 s")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_wait_dump, false, "Testing",
                    "TESTING TOOL (Sat 21 Nov): with the wait view lines, the first 3 seconds of each wait also dump "
                    "player 2's camera object (+0..+0x200) and his character's view fields (+0x1640..+0x16C0) as floats")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
double PdzCounterOpTestFirstPickS() { return REXCVAR_GET(pdz_counterop_test_first_pick_s); }

namespace
{
  std::string FloatDump(const uint8_t *base, uint32_t at, uint32_t bytes)
  {
    std::string s;
    for (uint32_t o = 0; o < bytes; o += 4)
    {
      const float f = LoadFloat(base, at + o);
      if (std::isfinite(f) && std::fabs(f) < 1e7f && (f == 0.0f || std::fabs(f) > 1e-6f))
      {
        s += fmt::format(" +{:X}={:.3f}", o, f);
      }
      else
      {
        s += fmt::format(" +{:X}=#{:08X}", o, Load32(base, at + o));
      }
    }
    return s;
  }
} // namespace

void PdzCounterOpWaitViewUpright(uint8_t *base, uint32_t mode, const float eye[3], uint32_t p2c)
{
  static int64_t last_log = 0, last_call = 0, wait_start = 0;
  static uint32_t lines = 0, fixes = 0, wrong_up = 0, dumps = 0;
  if (!Ptr(mode))
  {
    return;
  }
  {
    const int64_t now = NowMs();
    if (now - last_call > 2000)
    {
      wait_start = now; // a new wait
    }
    last_call = now;
    static int64_t last_dump = 0;
    if (REXCVAR_GET(pdz_counterop_test_wait_dump) && now - wait_start <= 3000 && now - last_dump >= 1000 &&
        dumps < 40)
    {
      last_dump = now;
      ++dumps;
      REXLOG_INFO("pdz_counterop: wait dump {}: camera {:08X}:{}", dumps, mode, FloatDump(base, mode, 0x200));
      if (Ptr(p2c))
      {
        REXLOG_INFO("pdz_counterop: wait dump {}: player 2 {:08X} +0x1640:{}", dumps, p2c,
                    FloatDump(base, p2c + 0x1640, 0x80));
      }
    }
  }
  constexpr uint32_t kPos = 4, kLook = 16, kUp = 0x34, kSway = 124;
  float pos[3], look[3], up[3], f[3];
  for (uint32_t i = 0; i < 3; ++i)
  {
    pos[i] = LoadFloat(base, mode + kPos + i * 4);
    look[i] = LoadFloat(base, mode + kLook + i * 4);
    up[i] = LoadFloat(base, mode + kUp + i * 4);
    f[i] = look[i] - pos[i];
  }
  const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  const float ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
  const bool sane = std::isfinite(fl) && fl > 1.0f && std::isfinite(ul) && ul > 0.5f && ul < 2.0f;
  float pitch = 0.0f, roll = 0.0f;
  float want[3] = {0.0f, 1.0f, 0.0f};
  if (sane)
  {
    for (float &v : f)
    {
      v /= fl;
    }
    pitch = std::asin(std::clamp(f[1], -1.0f, 1.0f)) * 57.29578f;
    // The upright up: the world's up made square to the look direction.
    want[0] = -f[1] * f[0], want[1] = 1.0f - f[1] * f[1], want[2] = -f[1] * f[2];
    const float wl = std::sqrt(want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);
    if (wl > 1e-3f)
    {
      for (float &v : want)
      {
        v /= wl;
      }
      // Roll: the angle from the upright up to the camera's up, about the look direction (signed).
      float u[3] = {up[0] / ul, up[1] / ul, up[2] / ul};
      const float d = u[0] * f[0] + u[1] * f[1] + u[2] * f[2];
      for (uint32_t i = 0; i < 3; ++i)
      {
        u[i] -= d * f[i];
      }
      const float c = u[0] * want[0] + u[1] * want[1] + u[2] * want[2];
      const float cr[3] = {want[1] * u[2] - want[2] * u[1], want[2] * u[0] - want[0] * u[2],
                           want[0] * u[1] - want[1] * u[0]};
      const float s = cr[0] * f[0] + cr[1] * f[1] + cr[2] * f[2];
      roll = std::atan2(s, c) * 57.29578f;
    }
  }
  // Log only: this camera object's fields do not reach his picture (it is his hidden body's first-person view; the
  // body is moved by counter_op.cpp's WaitViewTick). Kept as a diagnostic of the follow write.
  const bool on = false;
  const bool wrong = sane && std::fabs(roll) > 1.0f;
  wrong_up += wrong;
  const int64_t now = NowMs();
  if (now - last_log >= 1000 && lines < 400)
  {
    last_log = now;
    ++lines;
    const float bx = pos[0] - eye[0], by = pos[1] - eye[1], bz = pos[2] - eye[2];
    REXLOG_INFO("pdz_counterop: wait camera object (diagnostic; his picture is his hidden body's eye): {:08X} at ({:.0f}, {:.0f}, {:.0f}), {:.1f} m from the agent's eye "
                "({:.1f} m above it), pitch {:.1f} deg, roll {:.1f} deg as Rare's update left it (up ({:.2f}, {:.2f}, "
                "{:.2f}), sway {:.2f} / {:.2f} / {:.2f}){}; {} upright fixes, {} updates with a rolled up so far",
                mode, pos[0], pos[1], pos[2], std::sqrt(bx * bx + by * by + bz * bz) / 100.0f, by / 100.0f, pitch, roll,
                up[0], up[1], up[2], LoadFloat(base, mode + kSway), LoadFloat(base, mode + kSway + 4),
                LoadFloat(base, mode + kSway + 8),
                !sane ? " (not a usable camera: left alone)"
                : on  ? "" : "",
                fixes, wrong_up);
  }
}
