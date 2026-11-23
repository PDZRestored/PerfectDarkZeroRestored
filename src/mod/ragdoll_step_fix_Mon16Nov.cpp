// Ragdoll drive step fix (pdz_fix_ragdoll_120, Mon 16 Nov): a ragdoll that follows its death animation is never
// driven with a shorter time step than the one Havok is about to integrate.
//
// How a dying character's ragdoll is driven (TU3): the physics manager's update sub_824E8BC8(f1 = the game's
// measured frame dt) calls the ragdoll controllers' update sub_825EA370(f1 = that frame dt) and, straight after it,
// the Havok world step sub_829C5F30(world, f1 = frame dt, f2 = solver step). For every active controller that is not
// fully physical yet, sub_825EA370 calls Havok's drive-to-pose sub_82A5D3C0(f1 = frame dt, ...): each body gets the
// velocity that takes it to the animation's pose in f1 seconds (velocity = distance / f1). The world then moves the
// bodies with those velocities for whole solver steps.
//
// On the Xbox the frame dt is a whole number of display intervals (1/30 or 1/60 s) and the solver step is 1/60, so
// the body arrives at the pose or short of it. With the game paced at 120 Hz the solver step is 1/120 s
// (pdz_physics_frame_step), but the frame dt is the measured one: the frame after a hitch (a first-use shader, a
// speech read, both common in the moment a guard is shot) is a short catch-up frame. Its drive velocity is
// distance / (a very short dt), and Havok then carries that velocity for a full solver step: the body overshoots
// the pose by solver step / frame dt, and if the ragdoll goes fully physical around then it keeps the speed and
// flies off. (Read in the code; not yet seen in a log: pdz_ragdoll_log prints the short frames, see below.)
//
// The fix: sub_825EA370's f1 is never below the solver step sub_829C5F30 will use this frame (1 / the paced rate,
// at least Rare's 1/60: the same rule as physics_step.cpp). A longer drive dt only makes the body arrive a little
// late, as on the Xbox. At a steady 60 the frame dt already is the step: nothing changes. Without pacing nothing
// changes. Off = the measured frame dt, as before.
//
// pdz_ragdoll_log on this base (the launch-address ragdoll hooks of ragdoll_tuning.cpp are not built here): one line
// when a controller changes mode / active / physical, one line for every short frame while a ragdoll is active, and
// one line when a ragdoll body moves faster than 15 m/s. All of it once per frame; nothing when the log is off.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include <array>
#include <string>
#include <fmt/format.h>

#include "../game_cvars.h"    // pdz_ragdoll_log
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night: the controller list read only where it is mapped
#include "render/game_pace.h" // EffectiveHz

REXCVAR_DEFINE_BOOL(pdz_fix_ragdoll_120, false, "Gameplay",
                    "A ragdoll that follows its death animation is never driven with a shorter time step than "
                    "Havok's solver step (1/120 s at 120 fps): a short catch-up frame after a hitch no longer "
                    "throws the body past its pose (a guard that flew off when shot). At a steady 60 fps nothing "
                    "changes. Off = the measured frame time, as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DECLARE(bool, pdz_physics_frame_step);
REXCVAR_DEFINE_BOOL(pdz_ragdoll_settle_log, false, "Debug",
                    "Log only (Sat 21 Nov evening, the 'dead body jumps up a few seconds after death' report): per "
                    "ragdoll, every change of its controller's weight / mode / physical flag with the bones' heights "
                    "against the floor under them and the root's speed before and after, and a SETTLE line when it ends: "
                    "how far its root rose after it had come to rest ('pdz_ragdoll_settle:' lines)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Sun 22 Nov night (the owner's report): a dead body that has come to rest jumped up 8-13 cm and fell again
// 3-5 s after death. Rare's second ("blend", mode 1) controller drives the ragdoll back toward the death animation's
// pose with the strength 1 - weight (825EA370: 1.0 - controller +64 into Havok's drive) until its weight passes Rare's
// threshold (the float at 8208C5F8, 0.70: then 825EA370 sets the physical flag +85 and the drive stops). A body that
// slid / rolled / fell away from that pose and lay still was lifted back toward it. Now, once the body's root has been
// still for 0.4 s while that LATE controller is blending (one that starts at weight 0.40; the death animation's own
// controller starts at 0 and is left alone), the weight reads 1.0 for the length of each ragdoll update (no pull) and
// Rare's value is put back after it, so Rare's ramp and timing go on as they were. (Setting the physical flag alone
// was tried first: the body still crept 10 cm.) Nothing changes while a body falls or moves.
REXCVAR_DEFINE_BOOL(pdz_fix_ragdoll_settled_blend, false, "Gameplay",
                    "A dead body that has come to rest is no longer pulled back toward its death pose by the game's "
                    "late ragdoll blend (it jumped up a little a few seconds after death and fell again): once it lies "
                    "still, it goes fully physical at once. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Mon 23 Nov (the lift that was left: 3 of 40 bodies still rose 4-10 cm). The trace showed what moves them: nothing of
// the game's ragdoll code (the blend is complete, no drive, no position set, no "unstick"), only Havok itself. The late
// controller wakes the ragdoll's rigid bodies while the body lies in its death animation's pose, and the solver then
// works on that pose for 2-3 s (the root creeps up 4-10 cm while the limbs shuffle) until Havok puts the bodies to
// sleep by itself. Now, once the root has been still for 0.4 s and the body lies on a floor, the bodies are put to
// sleep at once with Havok's own call (829C2200: the entity's island is marked inactive): the state Rare's code
// reaches by itself 2-3 s later, without the shuffle. Anything that wakes a sleeping body (an explosion, a hit) still
// wakes it. Not done when the body does not lie on a floor (a ledge, a wall), or while anything else in the same
// island (another body, a dropped gun) still moves: those stay as before.
REXCVAR_DEFINE_BOOL(pdz_fix_ragdoll_rest_sleep, false, "Gameplay",
                    "A dead body that has come to rest on the floor is put to sleep at once (the physics engine's own "
                    "sleep), so it no longer creeps up a few centimetres over the next seconds. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(sub_82259B80); // physics ray (r3 mask, r4 from, r5 to, r6 collector, r7 1): r3 = hit
REX_EXTERN(sub_829C2200); // Havok: put an entity (r3) to sleep: its island is marked inactive

REX_EXTERN(__imp__sub_825EA370); // the ragdoll controllers' per-frame update: (f1 = frame dt)
REX_EXTERN(__imp__sub_829C1020); // Havok: a rigid body's position set (r3 body, r4 -> position, metres)
REX_EXTERN(__imp__sub_829C1068); // Havok: a rigid body's rotation set (r3 body, r4 -> quaternion)

namespace pdz::restored_sounds
{
  void RagdollTick(PPCContext &ctx, uint8_t *base, double frame_dt); // restored_sounds_Tue17Nov.cpp: the body-fall sound
}

namespace
{
  constexpr uint32_t kMinHz = 20, kMaxHz = 480;
  // The controller list (sub_825EA370: r25 = 82E3C480, the first controller at +4) and a controller's fields.
  constexpr uint32_t kControllerList = 0x82E3C484;
  constexpr uint32_t kControllerNext = 44;
  constexpr uint32_t kControllerRagdoll = 48; // -> +20 constraint array, +24 count
  constexpr uint32_t kControllerMode = 60;
  constexpr uint32_t kControllerWeight = 64;
  constexpr uint32_t kControllerActive = 84;
  constexpr uint32_t kControllerPhysical = 85;
  // Constraint +16 / +20 its two bodies; body +80 motion; motion +64 position (metres).
  constexpr uint32_t kConstraintBodyA = 16, kBodyMotion = 80, kMotionPosition = 64;
  constexpr float kFastSpeed = 15.0f; // m/s

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }

  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t bits = Load32(base, a);
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
  }

  bool Plausible(uint32_t p) { return p >= 0x10000u && p < 0xF0000000u && (p & 3u) == 0; }

  struct ControllerLog
  {
    uint32_t mode = ~0u;
    uint8_t active = 0xFF, physical = 0xFF;
    bool have_pos = false;
    float pos[3] = {};
    uint32_t fast_lines = 0;
  };
  std::unordered_map<uint32_t, ControllerLog> g_logs; // the game thread only
  uint64_t g_frames = 0, g_raised = 0, g_short_lines = 0;

  // The first constraint's first body: where the ragdoll is (any body will do for "did it fly off").
  bool BodyPosition(const uint8_t *base, uint32_t controller, float pos[3])
  {
    using pdz::guestmem::Ok; // Sun 22 Nov night: every pointer of the chain mapped before it is read
    const uint32_t ragdoll = Ok(base, controller + kControllerRagdoll, 4) ? Load32(base, controller + kControllerRagdoll) : 0;
    if (!Plausible(ragdoll) || !Ok(base, ragdoll + 20, 8))
    {
      return false;
    }
    const uint32_t array = Load32(base, ragdoll + 20);
    const uint32_t count = Load32(base, ragdoll + 24);
    if (!Plausible(array) || !count || count > 64 || !Ok(base, array, 4))
    {
      return false;
    }
    const uint32_t constraint = Load32(base, array);
    const uint32_t body = Plausible(constraint) && Ok(base, constraint + kConstraintBodyA, 4) ? Load32(base, constraint + kConstraintBodyA) : 0;
    const uint32_t motion = Plausible(body) && Ok(base, body + kBodyMotion, 4) ? Load32(base, body + kBodyMotion) : 0;
    if (!Plausible(motion) || !Ok(base, motion + kMotionPosition, 12))
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      pos[i] = LoadFloat(base, motion + kMotionPosition + i * 4);
    }
    return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]);
  }

  // ---- Sat 21 Nov evening: pdz_ragdoll_settle_log ----
  // Every body of a ragdoll (each constraint's two bodies, +16 / +20; body +80 motion, motion +64 position in metres;
  // the game's centimetres = metres x 100, the same axes: checked on the first line against the character's place).
  std::vector<uint32_t> g_body_ids; // (Mon 23 Nov) the body addresses of the last Bodies() call, same order
  int Bodies(const uint8_t *base, uint32_t controller, std::vector<std::array<float, 3>> &out)
  {
    out.clear();
    g_body_ids.clear();
    // Sun 22 Nov night (audit): every hop of the chain read only where it is mapped.
    if (!pdz::guestmem::Ok(base, controller, kControllerRagdoll + 4))
    {
      return 0;
    }
    const uint32_t ragdoll = Load32(base, controller + kControllerRagdoll);
    if (!Plausible(ragdoll) || !pdz::guestmem::Ok(base, ragdoll, 28))
    {
      return 0;
    }
    const uint32_t array = Load32(base, ragdoll + 20), count = Load32(base, ragdoll + 24);
    if (!Plausible(array) || !count || count > 64 || !pdz::guestmem::Ok(base, array, 4 * count))
    {
      return 0;
    }
    std::vector<uint32_t> seen;
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t constraint = Load32(base, array + 4 * i);
      for (const uint32_t at : {kConstraintBodyA, kConstraintBodyA + 4})
      {
        const uint32_t body = Plausible(constraint) && pdz::guestmem::Ok(base, constraint, at + 4) ? Load32(base, constraint + at) : 0;
        const uint32_t motion = Plausible(body) && pdz::guestmem::Ok(base, body, kBodyMotion + 4) ? Load32(base, body + kBodyMotion) : 0;
        if (!Plausible(motion) || !pdz::guestmem::Ok(base, motion, kMotionPosition + 12) ||
            std::find(seen.begin(), seen.end(), body) != seen.end())
        {
          continue;
        }
        seen.push_back(body);
        std::array<float, 3> p{};
        for (int k = 0; k < 3; ++k)
        {
          p[size_t(k)] = LoadFloat(base, motion + kMotionPosition + k * 4) * 100.0f;
        }
        if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]))
        {
          out.push_back(p);
          g_body_ids.push_back(body);
        }
      }
    }
    return int(out.size());
  }
  void StoreBe(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
  }
  // The floor under a point (Rare's bullet ray, closest hit, 3 m down from 1 m above it); NAN = none.
  float FloorUnder(PPCContext &ctx, uint8_t *base, const std::array<float, 3> &p)
  {
    const uint32_t buf = (ctx.r1.u32 - 0x600) & ~0xFu;
    std::memset(base + buf, 0, 0x180);
    const float from[3] = {p[0], p[1] + 100.0f, p[2]}, to[3] = {p[0], p[1] - 200.0f, p[2]};
    for (uint32_t k = 0; k < 3; ++k)
    {
      StoreBe(base, buf + 4 * k, std::bit_cast<uint32_t>(from[k]));
      StoreBe(base, buf + 16 + 4 * k, std::bit_cast<uint32_t>(to[k]));
    }
    StoreBe(base, buf + 32, 0x82090738); // Rare's closest-hit collector (the hitscan builds it)
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = buf - 0x800;
    frame.ctx.r3.u64 = 1;
    frame.ctx.r4.u64 = buf;
    frame.ctx.r5.u64 = buf + 16;
    frame.ctx.r6.u64 = buf + 32;
    frame.ctx.r7.u64 = 1;
    sub_82259B80(frame.ctx, base);
    return (frame.ctx.r3.u32 & 0xFF) ? LoadFloat(base, buf + 32 + 72) : NAN;
  }
  struct Settle
  {
    double t = 0.0;             // seconds since it went active
    float weight = -1.0f;
    uint32_t mode = ~0u;
    uint8_t physical = 0xFF;
    std::array<float, 3> root{}; // the previous frame's root
    bool have = false;
    double still_s = 0.0;       // the root under 0.1 m/s this long
    bool settled = false;
    double settled_t = 0.0;
    float settled_y = 0.0f, rise = 0.0f, rise_t = 0.0f;
    std::string rise_when;      // the controller's state at the largest rise
    int after_lines = 0;        // root speed lines after a handover (the next frame and 0.5 s on)
    double after_at = 0.0;
    float max_speed = 0.0f;
    bool rise_logged = false;
    // Mon 23 Nov (the lift that is left): the last frames, printed when the root starts to rise after rest.
    struct Frame
    {
      float t, dt, weight, clock, root_y, low_y, fast;
      uint8_t mode, physical, b86, low_bone, fast_bone;
    };
    std::vector<Frame> ring;
    std::vector<std::array<float, 3>> prev;
    bool trace_started = false;
    double trace_until = 0.0;
  };
  std::unordered_map<uint32_t, Settle> g_settle;
  std::unordered_map<uint32_t, uint32_t> g_body_owner; // body -> its controller (rebuilt every frame while the log is on)
  bool g_in_update = false;                              // inside Rare's ragdoll update
  uint32_t g_set_lines = 0;
  std::string TraceText(const Settle::Frame &f)
  {
    return fmt::format("t {:.3f} dt {:.4f} | mode {} weight {:.2f} clock {:.2f} physical {} b86 {} | root y {:.1f} | lowest bone {} y {:.1f} | "
                       "fastest bone {} {:.2f} m/s",
                       f.t, f.dt, f.mode, f.weight, f.clock, f.physical, f.b86, f.root_y, f.low_bone, f.low_y, f.fast_bone, f.fast);
  }
  std::vector<std::array<float, 3>> g_bones;

  void SettleLog(PPCContext &ctx, uint8_t *base, double frame_dt)
  {
    const double dt = frame_dt > 0.0 && frame_dt < 0.25 ? frame_dt : 1.0 / 60.0;
    for (uint32_t c = Load32(base, kControllerList), guard = 0; Plausible(c) && guard < 256; c = Load32(base, c + kControllerNext), ++guard)
    {
      const bool active = base[c + kControllerActive] != 0;
      auto it = g_settle.find(c);
      if (!active)
      {
        if (it != g_settle.end())
        {
          const Settle &q = it->second;
          REXLOG_INFO("pdz_ragdoll_settle: SETTLE controller {:08X} ended after {:.1f} s: {}", c, q.t,
                      q.settled ? fmt::format("its root came to rest at {:.1f} s; afterwards it rose at most {:.1f} cm{}", q.settled_t, q.rise,
                                              q.rise > 0.5f ? fmt::format(" (at {:.1f} s, {})", q.rise_t, q.rise_when) : std::string())
                                : std::string("its root never came to rest"));
          g_settle.erase(it);
        }
        continue;
      }
      Settle &q = it == g_settle.end() ? g_settle[c] : it->second;
      q.t += dt;
      if (q.t >= 12.0 && q.t - dt < 12.0)
      {
        // (a summary 12 s after it went active, while it is still active: most bodies stay active for long)
        REXLOG_INFO("pdz_ragdoll_settle: SETTLE controller {:08X} at 12 s: {}", c,
                    q.settled ? fmt::format("its root came to rest at {:.1f} s; afterwards it rose at most {:.1f} cm{}", q.settled_t, q.rise,
                                            q.rise > 0.5f ? fmt::format(" (at {:.1f} s, {})", q.rise_t, q.rise_when) : std::string())
                              : std::string("its root has not come to rest"));
      }
      if (Bodies(base, c, g_bones) == 0)
      {
        continue;
      }
      for (const uint32_t b : g_body_ids)
      {
        g_body_owner[b] = c;
      }
      {
        Settle::Frame f{};
        f.t = float(q.t), f.dt = float(dt), f.weight = LoadFloat(base, c + kControllerWeight), f.clock = LoadFloat(base, c + 68);
        f.mode = uint8_t(Load32(base, c + kControllerMode)), f.physical = base[c + kControllerPhysical], f.b86 = base[c + 86];
        f.root_y = g_bones[0][1], f.low_y = 1e9f;
        for (size_t b = 0; b < g_bones.size(); ++b)
        {
          if (g_bones[b][1] < f.low_y)
          {
            f.low_y = g_bones[b][1], f.low_bone = uint8_t(b);
          }
          if (q.prev.size() == g_bones.size())
          {
            const float dx = g_bones[b][0] - q.prev[b][0], dy = g_bones[b][1] - q.prev[b][1], dz = g_bones[b][2] - q.prev[b][2];
            const float v = std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f / float(dt);
            if (v > f.fast)
            {
              f.fast = v, f.fast_bone = uint8_t(b);
            }
          }
        }
        q.prev = g_bones;
        if (q.trace_started && q.t < q.trace_until)
        {
          REXLOG_INFO("pdz_ragdoll_settle: TRACE {:08X} {}", c, TraceText(f));
        }
        q.ring.push_back(f);
        if (q.ring.size() > 480)
        {
          q.ring.erase(q.ring.begin(), q.ring.begin() + 120);
        }
      }
      const std::array<float, 3> root = g_bones[0];
      const float speed = q.have ? std::sqrt((root[0] - q.root[0]) * (root[0] - q.root[0]) + (root[1] - q.root[1]) * (root[1] - q.root[1]) +
                                             (root[2] - q.root[2]) * (root[2] - q.root[2])) / 100.0f / float(dt)
                                 : 0.0f;
      const float speed_before = q.have ? speed : -1.0f;
      const uint32_t mode = Load32(base, c + kControllerMode);
      const float weight = LoadFloat(base, c + kControllerWeight);
      const uint8_t physical = base[c + kControllerPhysical];
      const std::string state = fmt::format("mode {} weight {:.2f} physical {}", mode, weight, physical);
      const bool change = mode != q.mode || physical != q.physical || std::fabs(weight - q.weight) >= 0.05f;
      if (change)
      {
        // The handover: each bone's height over the floor under it (the lowest three), and the root's speed.
        std::vector<std::pair<float, int>> low;
        for (int b = 0; b < int(g_bones.size()); ++b)
        {
          low.push_back({g_bones[size_t(b)][1], b});
        }
        std::sort(low.begin(), low.end());
        std::string bones;
        for (size_t k = 0; k < low.size() && k < 3; ++k)
        {
          const float f = FloorUnder(ctx, base, g_bones[size_t(low[k].second)]);
          bones += fmt::format("{}bone {} at y {:.0f}: {}", k ? "; " : "", low[k].second, low[k].first,
                               std::isfinite(f) ? fmt::format("{:+.1f} cm over the floor ({:.0f})", low[k].first - f, f) : std::string("no floor within 2 m"));
        }
        const float rf = FloorUnder(ctx, base, root);
        REXLOG_INFO("pdz_ragdoll_settle: controller {:08X} at {:.2f} s: {} -> {} | {} bodies, root at ({:.0f} {:.0f} {:.0f}) {} | lowest: {} | "
                    "root speed {:.2f} m/s{}",
                    c, q.t, q.mode == ~0u ? std::string("(new)") : fmt::format("mode {} weight {:.2f} physical {}", q.mode, q.weight, q.physical), state,
                    g_bones.size(), root[0], root[1], root[2],
                    std::isfinite(rf) ? fmt::format("{:+.1f} cm over its floor", root[1] - rf) : std::string("(no floor within 2 m)"), bones,
                    speed_before, q.settled ? fmt::format(", at rest since {:.1f} s", q.settled_t) : std::string());
        q.mode = mode, q.weight = weight, q.physical = physical;
        q.after_lines = 2;
        q.after_at = q.t;
      }
      else if (q.after_lines > 0 && (q.after_lines == 2 ? q.t > q.after_at : q.t >= q.after_at + 0.5))
      {
        REXLOG_INFO("pdz_ragdoll_settle: controller {:08X} at {:.2f} s ({:.2f} s after the change): root speed {:.2f} m/s, root y {:.0f}", c, q.t,
                    q.t - q.after_at, speed, root[1]);
        --q.after_lines;
      }
      q.max_speed = std::max(q.max_speed, speed);
      // At rest: the root under 0.1 m/s for 0.4 s; then its rise above that height is tracked.
      if (q.have)
      {
        q.still_s = speed < 0.1f ? q.still_s + dt : 0.0;
        if (!q.settled && q.still_s >= 0.4)
        {
          q.settled = true;
          q.settled_t = q.t;
          q.settled_y = root[1];
        }
        else if (q.settled)
        {
          const float r = root[1] - q.settled_y;
          if (r >= 2.0f && !q.trace_started)
          {
            // (Mon 23 Nov) the frames that led to it (the last 2 s), then every frame for 3 s more
            q.trace_started = true;
            q.trace_until = q.t + 3.0;
            std::string all;
            for (size_t b = 0; b < g_bones.size(); ++b)
            {
              const float fl = FloorUnder(ctx, base, g_bones[b]);
              all += fmt::format(" {}:{:+.1f}", b, std::isfinite(fl) ? g_bones[b][1] - fl : 999.0f);
            }
            REXLOG_INFO("pdz_ragdoll_settle: TRACE {:08X} BEGIN: the root is {:.1f} cm over its rest (rest at {:.2f} s, y {:.1f}); every "
                        "bone's height over the floor under it (cm):{}",
                        c, r, q.settled_t, q.settled_y, all);
            for (const Settle::Frame &f : q.ring)
            {
              if (f.t >= float(q.t) - 2.0f)
              {
                REXLOG_INFO("pdz_ragdoll_settle: TRACE {:08X} (before) {}", c, TraceText(f));
              }
            }
          }
          if (r > q.rise)
          {
            q.rise = r;
            q.rise_t = float(q.t);
            q.rise_when = state;
            if (r >= 3.0f && !q.rise_logged)
            {
              REXLOG_INFO("pdz_ragdoll_settle: RISE controller {:08X} at {:.2f} s: its root rose {:.1f} cm above where it came to rest "
                          "(at {:.1f} s) | {} | root speed {:.2f} m/s",
                          c, q.t, r, q.settled_t, state, speed);
              q.rise_logged = true;
            }
          }
          if (q.still_s >= 0.4 && root[1] < q.settled_y - 2.0f)
          {
            q.settled_y = root[1]; // (it sank further and rested again: measured from the new rest)
          }
        }
      }
      q.root = root;
      q.have = true;
    }
    if (g_settle.size() > 512)
    {
      g_settle.clear();
    }
    if (g_body_owner.size() > 8192)
    {
      g_body_owner.clear();
    }
  }

  void LogControllers(const uint8_t *base, double frame_dt, double drive_dt, double step, uint32_t hz)
  {
    uint32_t active_count = 0;
    for (uint32_t c = Load32(base, kControllerList), guard = 0; Plausible(c) && guard < 256;
         c = Load32(base, c + kControllerNext), ++guard)
    {
      ControllerLog &log = g_logs[c];
      const uint32_t mode = Load32(base, c + kControllerMode);
      const uint8_t active = base[c + kControllerActive];
      const uint8_t physical = base[c + kControllerPhysical];
      active_count += active ? 1 : 0;
      float pos[3];
      const bool have = active && BodyPosition(base, c, pos);
      float speed = 0.0f;
      if (have && log.have_pos && frame_dt > 0.0)
      {
        const float dx = pos[0] - log.pos[0], dy = pos[1] - log.pos[1], dz = pos[2] - log.pos[2];
        speed = std::sqrt(dx * dx + dy * dy + dz * dz) / float(frame_dt > step ? frame_dt : step);
      }
      if (mode != log.mode || active != log.active || physical != log.physical)
      {
        REXLOG_INFO("pdz_ragdoll: controller {:08X} mode {} weight {:.2f} active {} physical {} (frame dt {:.5f} s, "
                    "drive dt {:.5f} s, solver step {:.5f} s, body speed {:.1f} m/s)",
                    c, mode, LoadFloat(base, c + kControllerWeight), active, physical, frame_dt, drive_dt, step,
                    speed);
        log.mode = mode;
        log.active = active;
        log.physical = physical;
        log.fast_lines = 0;
      }
      if (speed > kFastSpeed && log.fast_lines < 20)
      {
        ++log.fast_lines;
        REXLOG_INFO("pdz_ragdoll: FAST controller {:08X} body speed {:.1f} m/s (mode {} weight {:.2f} physical {}; "
                    "frame dt {:.5f} s, drive dt {:.5f} s, solver step {:.5f} s)",
                    c, speed, mode, LoadFloat(base, c + kControllerWeight), physical, frame_dt, drive_dt, step);
      }
      log.have_pos = have;
      if (have)
      {
        std::memcpy(log.pos, pos, sizeof(pos));
      }
    }
    // A short frame while a ragdoll is active: the case the fix is for.
    if (active_count && frame_dt > 0.0 && frame_dt < 0.75 * step && g_short_lines < 2000)
    {
      ++g_short_lines;
      REXLOG_INFO("pdz_ragdoll: short frame: frame dt {:.5f} s under the solver step {:.5f} s (paced {} Hz), {} "
                  "active ragdoll(s): drive dt {:.5f} s ({}; the drive would overshoot x{:.1f}); raised {} of {} "
                  "frames",
                  frame_dt, step, hz, active_count, drive_dt,
                  drive_dt > frame_dt ? "raised by pdz_fix_ragdoll_120" : "pdz_fix_ragdoll_120 off", step / frame_dt,
                  g_raised, g_frames);
    }
    if (g_logs.size() > 1024)
    {
      g_logs.clear();
    }
  }
  // ---- Sun 22 Nov night: pdz_fix_ragdoll_settled_blend ----
  struct Rest
  {
    float root[3] = {};
    bool have = false;
    double still_s = 0.0;
    bool done = false;
    float first_weight = -1.0f; // the weight at its first frame in mode 1 (the late blend starts at 0.40)
    bool sibling_logged = false;
    // Mon 23 Nov (pdz_fix_ragdoll_rest_sleep): 0 waiting, 1 put to sleep, 2 left alone
    int sleep = 0;
    double sleep_wait_s = 0.0, slept_s = 0.0;
    bool floor_tested = false, woke_logged = false;
    int wakes = 0; // times something woke it after our sleep (it sleeps again once it lies still again; 20 at most)
    uint32_t body0 = 0;
    const char *why = "";
    std::unordered_map<uint32_t, std::array<float, 3>> others; // the island's other entities, last frame's places
    std::vector<std::array<float, 3>> bones;                   // its own bodies, last frame's places (cm)
  };
  std::unordered_map<uint32_t, Rest> g_rest;
  uint64_t g_settled_fixes = 0;
  std::vector<std::pair<uint32_t, uint32_t>> g_held; // (controller, Rare's weight bits) for this update

  // After Rare's update: its own weight back (unless the update itself wrote another one).
  void SettledBlendAfter(uint8_t *base)
  {
    for (const auto &[c, bits] : g_held)
    {
      if (pdz::guestmem::Ok(base, c, kControllerPhysical + 4) && Load32(base, c + kControllerWeight) == std::bit_cast<uint32_t>(1.0f))
      {
        StoreBe(base, c + kControllerWeight, bits);
      }
    }
    g_held.clear();
  }

  bool ControllerOk(const uint8_t *base, uint32_t c) { return Plausible(c) && pdz::guestmem::Ok(base, c, kControllerPhysical + 4); }

  // ---- Mon 23 Nov: pdz_fix_ragdoll_rest_sleep ----
  constexpr uint32_t kEntityIsland = 84, kIslandActive = 40; // (829C2188 / 829C21A0 / 829C2200 read them)
  uint64_t g_sleeps = 0, g_sleep_lines = 0;
  bool IslandAwake(const uint8_t *base, uint32_t body)
  {
    const uint32_t island = pdz::guestmem::Ok(base, body, kEntityIsland + 4) ? Load32(base, body + kEntityIsland) : 0;
    return Plausible(island) && pdz::guestmem::Ok(base, island, kIslandActive + 1) && base[island + kIslandActive] != 0;
  }
  // The island's entity list: the (pointer, count) pair in the island whose array holds this body.
  bool IslandEntities(const uint8_t *base, uint32_t island, uint32_t body, std::vector<uint32_t> &out)
  {
    out.clear();
    if (!pdz::guestmem::Ok(base, island, 168))
    {
      return false;
    }
    for (uint32_t off = 8; off < 160; off += 4)
    {
      const uint32_t array = Load32(base, island + off), count = Load32(base, island + off + 4);
      if (!Plausible(array) || count == 0 || count > 2048 || !pdz::guestmem::Ok(base, array, 4 * count))
      {
        continue;
      }
      bool has = false;
      for (uint32_t i = 0; i < count && !has; ++i)
      {
        has = Load32(base, array + 4 * i) == body;
      }
      if (has)
      {
        for (uint32_t i = 0; i < count; ++i)
        {
          out.push_back(Load32(base, array + 4 * i));
        }
        return true;
      }
    }
    return false;
  }
  // One try per frame for a settled late-blend body that is fully physical: true when it was put to sleep.
  void RestSleep(PPCContext &ctx, uint8_t *base, uint32_t c, Rest &r, double dt)
  {
    if (Bodies(base, c, g_bones) == 0 || g_body_ids.empty())
    {
      return;
    }
    const std::vector<uint32_t> ids = g_body_ids;
    r.body0 = ids[0];
    if (!r.floor_tested)
    {
      // Once: does it lie on a floor (the root and three quarters of its bones within 35 cm of the floor under them)?
      r.floor_tested = true;
      int low = 0;
      for (const auto &b : g_bones)
      {
        const float f = FloorUnder(ctx, base, b);
        low += std::isfinite(f) && b[1] - f <= 35.0f ? 1 : 0;
      }
      const float rf = FloorUnder(ctx, base, g_bones[0]);
      if (!(std::isfinite(rf) && g_bones[0][1] - rf <= 35.0f) || low * 4 < int(g_bones.size()) * 3)
      {
        r.sleep = 2;
        r.why = "it does not lie on a floor";
        return;
      }
    }
    if (!IslandAwake(base, ids[0]))
    {
      r.sleep = 1; // (asleep already)
      return;
    }
    // No limb may still be swinging (the arm that drops when the late controller starts): every body under 1 m/s.
    {
      float fastest = 0.0f;
      const bool have = r.bones.size() == g_bones.size();
      for (size_t b = 0; have && b < g_bones.size(); ++b)
      {
        const float dx = g_bones[b][0] - r.bones[b][0], dy = g_bones[b][1] - r.bones[b][1], dz = g_bones[b][2] - r.bones[b][2];
        fastest = std::max(fastest, std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f / float(dt));
      }
      r.bones = g_bones;
      if (!have || fastest > 1.0f)
      {
        r.why = "a limb still moves";
        return;
      }
    }
    const uint32_t island = Load32(base, ids[0] + kEntityIsland);
    std::vector<uint32_t> all;
    if (!IslandEntities(base, island, ids[0], all))
    {
      r.why = "its island's entity list was not found";
      return;
    }
    // Everything else in the island must be at rest too (another body falling on it, a dropped gun still bouncing).
    bool moving = false;
    std::unordered_map<uint32_t, std::array<float, 3>> now;
    for (const uint32_t e : all)
    {
      if (std::find(ids.begin(), ids.end(), e) != ids.end())
      {
        continue;
      }
      const uint32_t motion = Plausible(e) && pdz::guestmem::Ok(base, e, kBodyMotion + 4) ? Load32(base, e + kBodyMotion) : 0;
      if (!Plausible(motion) || !pdz::guestmem::Ok(base, motion, kMotionPosition + 12))
      {
        moving = true;
        continue;
      }
      std::array<float, 3> q{};
      for (int k = 0; k < 3; ++k)
      {
        q[size_t(k)] = LoadFloat(base, motion + kMotionPosition + 4 * k);
      }
      now[e] = q;
      const auto it = r.others.find(e);
      if (it == r.others.end())
      {
        moving = true;
      }
      else
      {
        const float dx = q[0] - it->second[0], dy = q[1] - it->second[1], dz = q[2] - it->second[2];
        moving = moving || std::sqrt(dx * dx + dy * dy + dz * dz) / float(dt) > 0.1f;
      }
    }
    r.others = std::move(now);
    if (moving)
    {
      r.why = "something else in its island still moves";
      return;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x1000) & ~0xFu;
    frame.ctx.r3.u64 = ids[0];
    sub_829C2200(frame.ctx, base);
    r.sleep = 1;
    ++g_sleeps;
    if (++g_sleep_lines <= 60 || g_sleeps % 100 == 0)
    {
      REXLOG_INFO("pdz_ragdoll: controller {:08X}: the body lies still on the floor: its {} rigid bodies are put to sleep where they lie "
                  "({} other entit{} in the island, at rest; {:.2f} s after it came to rest; #{}; pdz_fix_ragdoll_rest_sleep)",
                  c, ids.size(), all.size() - std::min(all.size(), ids.size()), all.size() - std::min(all.size(), ids.size()) == 1 ? "y" : "ies",
                  r.sleep_wait_s, g_sleeps);
    }
  }

  // Before Rare's update: a blending controller whose body has been still for 0.4 s goes physical now.
  void SettledBlend(PPCContext &ctx, uint8_t *base, double frame_dt)
  {
    const double dt = frame_dt > 0.0 && frame_dt < 0.25 ? frame_dt : 1.0 / 60.0;
    for (uint32_t c = pdz::guestmem::Ok(base, kControllerList, 4) ? Load32(base, kControllerList) : 0, guard = 0;
         ControllerOk(base, c) && guard < 256; c = Load32(base, c + kControllerNext), ++guard)
    {
      // (The physical flag does not end it: with the flag set the body still crept toward the pose until Rare's weight
      // had passed about 0.75, so the weight is held for as long as the controller blends.)
      const bool active = base[c + kControllerActive] != 0;
      if (!active)
      {
        g_rest.erase(c);
        continue;
      }
      float pos[3];
      if (!BodyPosition(base, c, pos))
      {
        continue;
      }
      Rest &r = g_rest[c];
      if (r.have)
      {
        const float dx = pos[0] - r.root[0], dy = pos[1] - r.root[1], dz = pos[2] - r.root[2];
        const float speed = std::sqrt(dx * dx + dy * dy + dz * dz) / float(dt); // m/s (Havok metres)
        r.still_s = speed < 0.1f ? r.still_s + dt : 0.0;
      }
      std::memcpy(r.root, pos, sizeof(pos));
      r.have = true;
      const uint32_t mode = Load32(base, c + kControllerMode);
      const float weight = LoadFloat(base, c + kControllerWeight);
      if (mode == 1 && r.first_weight < 0.0f)
      {
        r.first_weight = weight;
      }
      // Only Rare's LATE blend (a controller that starts blending at 0.40): the death animation's own controller is
      // mode 1 too, its weight rising from 0 while the animation plays (a body kneeling still in it is not at rest).
      if (mode != 1 || r.first_weight < 0.35f)
      {
        continue;
      }
      if (!r.done)
      {
        if (r.still_s < 0.4)
        {
          continue;
        }
        r.done = true;
        if (++g_settled_fixes <= 40 || g_settled_fixes % 100 == 0)
        {
          REXLOG_INFO("pdz_ragdoll: controller {:08X}: its body lies still ({:.1f} s) while the late blend pulls it toward the "
                      "death pose (mode 1, weight {:.2f}, started at {:.2f}): from now on the pull is off (the weight reads "
                      "1.0 during the game's ragdoll update, Rare's own value is put back after it; #{}; "
                      "pdz_fix_ragdoll_settled_blend)",
                      c, r.still_s, weight, r.first_weight, g_settled_fixes);
        }
      }
      // Every frame from then on the blend is complete: Rare's blend clock (+68 elapsed, +80 its length; 825EBF78 sets
      // them up) is put at its end and the weight at 1.0, so no code that blends by them pulls the body any more.
      const float length = LoadFloat(base, c + 80);
      if (length > 0.0f && length < 600.0f && LoadFloat(base, c + 68) < length)
      {
        StoreBe(base, c + 68, std::bit_cast<uint32_t>(length));
      }
      StoreBe(base, c + kControllerWeight, std::bit_cast<uint32_t>(1.0f));
      // Mon 23 Nov: and once Rare's update has made it physical (no drive any more), its bodies go to sleep.
      if (REXCVAR_GET(pdz_fix_ragdoll_rest_sleep))
      {
        if (r.sleep == 0 && base[c + kControllerPhysical] != 0 && r.still_s >= 0.4)
        {
          r.sleep_wait_s += dt;
          RestSleep(ctx, base, c, r, dt);
          if (r.sleep == 0 && r.sleep_wait_s > 3.0)
          {
            r.sleep = 2;
          }
          if (r.sleep == 2 && ++g_sleep_lines <= 60)
          {
            REXLOG_INFO("pdz_ragdoll: controller {:08X}: not put to sleep ({}): it stays as before (pdz_fix_ragdoll_rest_sleep)", c,
                        r.why[0] ? r.why : "no reason noted");
          }
        }
        else if (r.sleep == 1 && r.wakes < 20)
        {
          // Something woke it (a hit, another body, a prop): once its root has been still for 0.4 s again, it sleeps
          // again (the floor test is made again where it lies now).
          r.slept_s += dt;
          if (r.slept_s > 0.1 && r.body0 && IslandAwake(base, r.body0))
          {
            if (!r.woke_logged && ++g_sleep_lines <= 60)
            {
              REXLOG_INFO("pdz_ragdoll: controller {:08X}: its bodies are awake again {:.2f} s after they were put to sleep: it sleeps "
                          "again once it lies still",
                          c, r.slept_s);
            }
            r.woke_logged = true;
            ++r.wakes;
            r.sleep = 0, r.sleep_wait_s = 0.0, r.slept_s = 0.0, r.floor_tested = false, r.still_s = 0.0;
            r.bones.clear(), r.others.clear();
          }
        }
      }
      // The same for every other active controller of the SAME ragdoll (the death animation's own controller goes on
      // blending the same bodies toward its pose for about 3 s: with only the late one complete a Lab body still rose
      // 11 cm under "physical 1, weight 1.00").
      const uint32_t ragdoll = Load32(base, c + kControllerRagdoll), owner = Load32(base, c + 36);
      for (uint32_t o = Load32(base, kControllerList), n = 0; ControllerOk(base, o) && n < 256; o = Load32(base, o + kControllerNext), ++n)
      {
        if (o == c || !base[o + kControllerActive] ||
            !((ragdoll && Load32(base, o + kControllerRagdoll) == ragdoll) || (owner && Load32(base, o + 36) == owner)))
        {
          continue;
        }
        const float olen = LoadFloat(base, o + 80);
        if (olen > 0.0f && olen < 600.0f && LoadFloat(base, o + 68) < olen)
        {
          StoreBe(base, o + 68, std::bit_cast<uint32_t>(olen));
        }
        if (LoadFloat(base, o + kControllerWeight) < 1.0f)
        {
          StoreBe(base, o + kControllerWeight, std::bit_cast<uint32_t>(1.0f));
          if (!r.sibling_logged)
          {
            r.sibling_logged = true;
            REXLOG_INFO("pdz_ragdoll: controller {:08X}: the same body's other controller {:08X} (mode {}) is completed too",
                        c, o, Load32(base, o + kControllerMode));
          }
        }
      }
    }
    if (g_rest.size() > 512)
    {
      g_rest.clear();
    }
  }
} // namespace

// Mon 23 Nov (log only, pdz_ragdoll_settle_log): every position / rotation SET on a rigid body of a ragdoll the settle
// log follows, with who asked. Rare's ragdoll update itself sets them at 825EA854.. ("unstick": when a ray between a
// joint's two bodies hits the world, the first body is put at the second body's place and rotation).
REX_HOOK_RAW(sub_829C1020)
{
  if (REXCVAR_GET(pdz_ragdoll_settle_log) && g_set_lines < 6000)
  {
    const auto it = g_body_owner.find(ctx.r3.u32);
    const uint32_t lr = uint32_t(ctx.lr);
    const bool unstick = g_in_update && lr >= 0x825EA370 && lr < 0x825EA8E0;
    if (it != g_body_owner.end() || unstick)
    {
      ++g_set_lines;
      const uint32_t body = ctx.r3.u32, to = ctx.r4.u32;
      const uint32_t motion = pdz::guestmem::Ok(base, body, kBodyMotion + 4) ? Load32(base, body + kBodyMotion) : 0;
      float from[3] = {}, dest[3] = {};
      if (Plausible(motion) && pdz::guestmem::Ok(base, motion, kMotionPosition + 12) && pdz::guestmem::Ok(base, to, 12))
      {
        for (int k = 0; k < 3; ++k)
        {
          from[k] = LoadFloat(base, motion + kMotionPosition + 4 * k) * 100.0f, dest[k] = LoadFloat(base, to + 4 * k) * 100.0f;
        }
      }
      const uint32_t c = it != g_body_owner.end() ? it->second : 0;
      const auto qs = g_settle.find(c);
      std::string ray;
      if (unstick && pdz::guestmem::Ok(base, ctx.r1.u32 + 240, 48))
      {
        ray = fmt::format(" | the ray that hit: ({:.1f} {:.1f} {:.1f}) -> ({:.1f} {:.1f} {:.1f})", LoadFloat(base, ctx.r1.u32 + 240),
                          LoadFloat(base, ctx.r1.u32 + 244), LoadFloat(base, ctx.r1.u32 + 248), LoadFloat(base, ctx.r1.u32 + 272),
                          LoadFloat(base, ctx.r1.u32 + 276), LoadFloat(base, ctx.r1.u32 + 280));
      }
      REXLOG_INFO("pdz_ragdoll_settle: SET {} body {:08X} of controller {:08X} ({}): position ({:.1f} {:.1f} {:.1f}) -> ({:.1f} {:.1f} "
                  "{:.1f}): moved {:.1f} cm, dy {:+.1f} cm; asked from {:08X}{}",
                  unstick ? "UNSTICK" : "OTHER", body, c,
                  qs == g_settle.end() ? std::string("not followed")
                                       : fmt::format("t {:.3f}, {}", qs->second.t,
                                                     qs->second.settled ? fmt::format("at rest since {:.2f} s", qs->second.settled_t) : std::string("not at rest")),
                  from[0], from[1], from[2], dest[0], dest[1], dest[2],
                  std::sqrt((dest[0] - from[0]) * (dest[0] - from[0]) + (dest[1] - from[1]) * (dest[1] - from[1]) + (dest[2] - from[2]) * (dest[2] - from[2])),
                  dest[1] - from[1], lr, ray);
    }
  }
  __imp__sub_829C1020(ctx, base);
}

REX_HOOK_RAW(sub_829C1068)
{
  if (REXCVAR_GET(pdz_ragdoll_settle_log) && g_set_lines < 6000)
  {
    const uint32_t lr = uint32_t(ctx.lr);
    const bool unstick = g_in_update && lr >= 0x825EA370 && lr < 0x825EA8E0;
    const auto it = g_body_owner.find(ctx.r3.u32);
    if (it != g_body_owner.end() && !unstick)
    {
      ++g_set_lines;
      REXLOG_INFO("pdz_ragdoll_settle: SET OTHER body {:08X} of controller {:08X}: rotation set; asked from {:08X}", ctx.r3.u32, it->second, lr);
    }
  }
  __imp__sub_829C1068(ctx, base);
}

REX_HOOK_RAW(sub_825EA370)
{
  ++g_frames;
  const double frame_dt = ctx.f1.f64;
  const uint32_t hz = pdz::pace::EffectiveHz();
  const bool paced = hz >= kMinHz && hz <= kMaxHz;
  // The solver step sub_829C5F30 uses this frame (physics_step.cpp): 1 / the paced rate, never longer than Rare's
  // 1/60; Rare's 1/60 when the paced step is off or nothing paces the guest.
  const double step =
      paced && REXCVAR_GET(pdz_physics_frame_step) ? 1.0 / double(hz > 60u ? hz : 60u) : 1.0 / 60.0;
  double drive_dt = frame_dt;
  if (REXCVAR_GET(pdz_fix_ragdoll_120) && paced && frame_dt > 0.0 && frame_dt < step)
  {
    drive_dt = step;
    ctx.f1.f64 = double(float(step));
    ++g_raised;
  }
  if (REXCVAR_GET(pdz_fix_ragdoll_settled_blend))
  {
    SettledBlend(ctx, base, frame_dt); // (Sun 22 Nov night)
  }
  g_in_update = true;
  __imp__sub_825EA370(ctx, base);
  g_in_update = false;
  SettledBlendAfter(base);
  pdz::restored_sounds::RagdollTick(ctx, base, frame_dt); // (Tue 17 Nov: a ragdoll that lands makes the body-fall sound)
  if (REXCVAR_GET(pdz_ragdoll_log))
  {
    LogControllers(base, frame_dt, drive_dt, step, hz);
  }
  if (REXCVAR_GET(pdz_ragdoll_settle_log))
  {
    SettleLog(ctx, base, frame_dt); // (Sat 21 Nov evening, log only)
  }
}
