// Counter-Operative round 13b (helper q): player 2 at a STATIONARY turret. Built only with PDZ_COUNTEROP.
//
// The owner's rule (G2, counter_op_vehicles.cpp): vehicles and guns mounted on vehicles are closed to player 2; a
// stationary turret (a hardpoint turret, class 0x2E, vtable 820C5BE0, whose parent is not a vehicle prop) is open
// to him. His shots from one count on the agent only; he can be hurt while he is on it.
//
// What Rare does (TU3 code):
//   - Use: the turret's virtual +0x94 = 8258FA50(turret, user): turret state (+0x500) 0, the user a human (class 2),
//     turret +0x48C bit 0, the 'may he use it' test 82590F10, then the mount 825904F0(turret, user, 0). No distance
//     test in it (the prompt system's: the turret's +0x8C = 8258FAE8 offers the prompt through 825AAB48).
//   - A human gunner fires with his trigger (bot_turrets.cpp's header: the turret's update 8258CEA0); he gets off
//     with his A button (82591948) through the dismount 825910F0.
//   - Rare's emergency evacuation 82590E30(turret): the gunner is off at once, the turret is reset and free.
//
// What this file does (nothing outside a running Counter-Op game):
//   [1] pdz_counterop_turret_leave_first: when player 2 is taken out of the world while he is the gunner of a
//       stationary turret (his life ends, his body is given up), he is taken off the gun first with Rare's
//       evacuation, so the turret is free again and his position is a world position when the life's end reads it.
//       While he is the gunner the mod does not redraw his own weapon. A guard who is picked for him while that
//       guard is the gunner of a stationary turret is taken off it the same way before he is hidden.
//   [2] pdz_counterop_turret_take_gunner: after [1]'s guard step, player 2 is seated at that gun in the guard's
//       place (Rare's mount 825904F0).
//   [3] The testing tool pdz_counterop_test_p2_turret (see the cvar).
#include "counter_op_rules13.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"
#include <rex/system/kernel_state.h>

bool PdzCounterOpRunning();     // counter_op.cpp
uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();
bool PdzCounterOpIsOf(const uint8_t *base, uint32_t object, uint32_t who); // counter_op_unheard.cpp: who, or a thing of his

REXCVAR_DEFINE_BOOL(pdz_counterop_turret_leave_first, true, "Gameplay",
                    "Counter-Operative (round 13b, default on: seen in four runs): when player 2's life ends (or his body is given up) while he is "
                    "the gunner of a stationary turret, he is taken off the gun first (the game's own emergency "
                    "evacuation of a turret), so the turret is free for the next user; while he is the gunner his own "
                    "weapon is not drawn again; a guard chosen as his next body while that guard mans a stationary "
                    "turret leaves it first. Off = round 13 (he is hidden still attached to the gun)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_turret_take_gunner, false, "Gameplay",
                    "Counter-Operative (round 13b, needs pdz_counterop_turret_leave_first): when the guard chosen as "
                    "player 2's next body is the gunner of a stationary turret, player 2 is seated at that gun in "
                    "his place (the game's own mount). Off = he stands beside the gun as that guard on foot")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_turret_only_agent, false, "Gameplay",
                    "Counter-Operative (round 13b): while player 2 is the gunner of a stationary turret, a hit of "
                    "his (the turret, its gun, a shot of either, or he himself as the source) on anyone but the agent "
                    "does not count (the game's own 'does this hit count' answer): no damage, no reaction. A second "
                    "net under the friendly-fire and allies rules, which already take his shots. Off = round 13")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_p2_turret, 0, "Testing",
                     "TESTING TOOL (counter-op, round 13b): a stationary turret test, one step each time the value "
                     "changes. 1 = every turret in the world is listed. 2 = player 2 is put behind the nearest free "
                     "stationary turret, facing it (a script then presses his Use). 3 = if he is not the gunner, the "
                     "game's own Use of the turret is called for him. 4 = the agent is put in the gun's line of fire "
                     "(searched in 15 degree steps until she is hit; her health is logged and refilled). 5 = the "
                     "nearest guard is put there instead. 6 = the nearest ally of the agent. 7 = the agent is put 5 m "
                     "in front and her view is held on the gunner (a script pulls her trigger). 8 = player 2's "
                     "health is put at his floor. 9 = one line with the turret's state. 20 = the nearest guard is "
                     "made the gunner (the game's mount). 21 = one line with that guard. 22 = he is taken off again. "
                     "100 = nothing but the keeping alive. While the value is not 0 the agent and her allies are kept "
                     "alive (their health is put back every tick). 0 = the tool rests")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_p2_turret_offset_deg, 0, "Testing",
                     "TESTING TOOL (counter-op, round 13b): where steps 4-7 put the agent / the guard / the ally: this "
                     "many degrees round the gun from its rest direction, 3.5 m from it, with no search (runs 1-3: "
                     "straight ahead of River's turret #2 is a drop, they fell to their death there; 60 is solid "
                     "ground). 0 = straight ahead with the search")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_p2_turret_view, true, "Testing",
                    "TESTING TOOL (counter-op, round 13b): in steps 4-6 the gunner's view is held level on whoever "
                    "stands in the line. Off = his view is left to his pad (a script's right stick sweeps the gun)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_p2_turret_pick, -1, "Testing",
                     "TESTING TOOL (counter-op, round 13b): which stationary turret step 2 uses: its number in step "
                     "1's list; -1 = the free one nearest to player 2")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_8258FA50); // a hardpoint turret's Use (turret, user)
REX_EXTERN(sub_825904F0); // turret: MountStart(turret, character, flag) -> bool (hooked by counter_op_outpost.cpp)
REX_EXTERN(sub_82590E30); // turret: emergency evacuation (turret): the gunner is off at once
REX_EXTERN(sub_825AD0E0); // Rare's teleport (character, &position)
REX_EXTERN(sub_825AD148); // move a character (character, &position, 0)
REX_EXTERN(sub_825AD3B0); // set a character's view (character, f1 pitch (positive down), f2 yaw)
REX_EXTERN(sub_82518580); // an object's matrix (object, out 0x40: side, up +0x10, forward +0x20, position +0x30)

namespace
{
  constexpr uint32_t kVehicleVtable = 0x820CA9E0, kTurretClass = 0x2E;
  constexpr uint32_t kClass = 0x0C, kParent = 0x5C, kPosition = 0x30, kHealth = 120;
  constexpr uint32_t kHidden = 6272;        // character: bit 0x10 = hidden by 825818E0
  constexpr uint32_t kParticipantOf = 6560; // a human's participant; its +980 = his health
  constexpr uint32_t kPromptProp = 0x1A30;  // a human: the prop whose prompt he is offered
  constexpr uint32_t kTurretState = 0x500, kTurretGunner = 0x5C4, kTurretUsable = 0x48C, kTurretGun = 0x494;
  constexpr uint32_t kTurretFiring = 1289;
  constexpr uint32_t kGunParams = 860, kGunType = 1144;
  constexpr uint32_t kWorld = 0x82D6BFD8 + 24, kWorldFirst = 43212, kWorldEnd = 43216; // nodes: +4 object, +12 next
  constexpr uint32_t kCharList = 0x82E454A0; // +0x710 first, +0x714 end; node +0 character, +8 next
  constexpr float kOwnFloor = 1.0f;          // counter_op.cpp's floor for player 2 (pdz_counterop_own_death)
  constexpr int kMaxTurrets = 48;

  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t L32(const uint8_t *b, uint32_t a)
  {
    const uint8_t *p = b + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LF(const uint8_t *b, uint32_t a)
  {
    const uint32_t bits = L32(b, a);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
  }
  void SF(uint8_t *b, uint32_t a, float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, sizeof(v));
    uint8_t *p = b + a;
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
  }
  uint32_t Call(PPCContext &ctx, uint8_t *base, PPCFunc *fn, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = r3;
    frame.ctx.r4.u64 = r4;
    frame.ctx.r5.u64 = r5;
    fn(frame.ctx, base);
    return frame.ctx.r3.u32;
  }

  bool On() { return REXCVAR_GET(pdz_counterop_turret_leave_first); }
  bool IsTurret(const uint8_t *b, uint32_t o) { return Ptr(o) && L32(b, o + kClass) == kTurretClass; }
  bool OnVehicle(const uint8_t *b, uint32_t turret)
  {
    const uint32_t parent = L32(b, turret + kParent);
    return Ptr(parent) && L32(b, parent) == kVehicleVtable;
  }
  // The stationary turret `character` is the gunner of, or 0.
  uint32_t GunOf(const uint8_t *b, uint32_t character)
  {
    if (!Ptr(character))
    {
      return 0;
    }
    const uint32_t turret = L32(b, character + kParent);
    return IsTurret(b, turret) && L32(b, turret + kTurretGunner) == character && !OnVehicle(b, turret) ? turret : 0;
  }
  int GunType(const uint8_t *b, uint32_t turret)
  {
    const uint32_t gun = L32(b, turret + kTurretGun);
    const uint32_t params = Ptr(gun) ? L32(b, gun + kGunParams) : 0;
    return Ptr(params) ? int(L32(b, params + kGunType)) : -1;
  }
  std::string TurretText(const uint8_t *b, uint32_t turret)
  {
    if (!Ptr(turret))
    {
      return "no turret";
    }
    return fmt::format("turret {:08X} at ({:.0f}, {:.0f}, {:.0f}): state {} (0 free, 3 in use), gunner {:08X}, firing "
                       "{}, usable word {:08X}, gun type {} (10 M60, 18 plasma, 23 rocket)",
                       turret, LF(b, turret + kPosition), LF(b, turret + kPosition + 4), LF(b, turret + kPosition + 8),
                       L32(b, turret + kTurretState), L32(b, turret + kTurretGunner), int(b[turret + kTurretFiring]),
                       L32(b, turret + kTurretUsable), GunType(b, turret));
  }
  float HumanHealth(const uint8_t *b, uint32_t human)
  {
    const uint32_t participant = Ptr(human) ? L32(b, human + kParticipantOf) : 0;
    return Ptr(participant) ? LF(b, participant + 980) : 0.0f;
  }

  struct Frame3
  {
    float side[3], up[3], fwd[3], pos[3];
  };
  uint32_t g_scratch = 0; // 0x80 bytes of guest memory: a matrix (+0), a position (+0x40)
  bool Scratch()
  {
    if (!g_scratch)
    {
      g_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x80);
    }
    return g_scratch != 0;
  }
  Frame3 FrameOf(PPCContext &ctx, uint8_t *base, uint32_t object)
  {
    Frame3 f{};
    if (!Scratch() || !Ptr(object))
    {
      return f;
    }
    std::memset(base + g_scratch, 0, 0x40);
    Call(ctx, base, sub_82518580, object, g_scratch);
    for (uint32_t i = 0; i < 3; ++i)
    {
      f.side[i] = LF(base, g_scratch + 4 * i);
      f.up[i] = LF(base, g_scratch + 0x10 + 4 * i);
      f.fwd[i] = LF(base, g_scratch + 0x20 + 4 * i);
      f.pos[i] = LF(base, g_scratch + 0x30 + 4 * i);
    }
    return f;
  }
  void Put(PPCContext &ctx, uint8_t *base, uint32_t character, float x, float y, float z, bool human_move)
  {
    if (!Scratch() || !Ptr(character) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
      return;
    }
    SF(base, g_scratch + 0x40, x);
    SF(base, g_scratch + 0x44, y);
    SF(base, g_scratch + 0x48, z);
    Call(ctx, base, human_move ? sub_825AD148 : sub_825AD0E0, character, g_scratch + 0x40, 0);
  }
  void Look(PPCContext &ctx, uint8_t *base, uint32_t human, float tx, float ty, float tz, float eye_up)
  {
    const float dx = tx - LF(base, human + kPosition), dz = tz - LF(base, human + kPosition + 8);
    const float dy = ty - (LF(base, human + kPosition + 4) + eye_up);
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(flat) || flat < 20.0f)
    {
      return;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = human;
    frame.ctx.f1.f64 = -std::atan2(double(dy), double(flat)); // 825AD3B0's pitch is positive down
    frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
    sub_825AD3B0(frame.ctx, base);
  }

  // The same from a given eye (a gunner's own +0x30 may not be a world position).
  void LookFrom(PPCContext &ctx, uint8_t *base, uint32_t human, const float eye[3], float tx, float ty, float tz)
  {
    const float dx = tx - eye[0], dy = ty - eye[1], dz = tz - eye[2];
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(flat) || flat < 20.0f || !std::isfinite(dy))
    {
      return;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = human;
    frame.ctx.f1.f64 = -std::atan2(double(dy), double(flat));
    frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
    sub_825AD3B0(frame.ctx, base);
  }

  using Clock = std::chrono::steady_clock;
  struct State
  {
    // [1] the watch
    uint32_t on_turret = 0;     // the stationary turret player 2 is the gunner of
    uint8_t last_firing = 0;
    uint32_t fire_lines = 0, mounts = 0;
    Clock::time_point mounted_at{};
    uint32_t pending_turret = 0, pending_guard = 0; // a picked guard was taken off this gun (Unseat .. AfterSpawn)
    // [3] the tool
    int32_t step = 0;
    uint32_t turrets[kMaxTurrets] = {};
    int count = 0;
    uint32_t target = 0;
    bool staged = false;
    Clock::time_point staged_at{};
    float stand_y = 0.0f; // player 2's own height (+0x34) while he stood beside the gun
    bool stand_ok = false;
    uint32_t prompt_seen = 0;
    int phase = 0; // 4 agent in the line, 5 guard, 6 ally, 7 the agent shoots him
    Clock::time_point phase_at{}, dwell_at{}, placed_at{};
    float theta0 = 0.0f, centre[3] = {};
    int search = 0;
    bool locked = false;
    float locked_theta = 0.0f;
    int dwell_hits = 0, hits = 0, hit_lines = 0, other_drops = 0, search_lines = 0;
    float agent_ref = 0.0f, p2_last = 0.0f;
    int p2_lines = 0;
    uint32_t victim = 0;
    float victim_health0 = 0.0f, victim_last = 0.0f;
    int victim_lines = 0;
    uint32_t test_guard = 0;
    uint32_t kept[8] = {};
    float kept_health[8] = {};
    int kept_count = 0, kept_lines = 0;
    uint32_t hit_lines2 = 0, hits_asked = 0, hits_refused = 0;
    int64_t tick = 0, fire_tick = -100000; // level ticks; the last one on which his stationary gun fired
  };
  State g;

  float ThetaOf(int search) // 0, +15, -15, +30, -30 ... degrees
  {
    const int k = (search + 1) / 2;
    return float((search & 1) ? k : -k) * 0.261799f;
  }

  void Collect(const uint8_t *base)
  {
    g.count = 0;
    const uint32_t world = L32(base, kWorld);
    const uint32_t end = Ptr(world) ? L32(base, world + kWorldEnd) : 0;
    uint32_t node = Ptr(world) ? L32(base, world + kWorldFirst) : 0;
    for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = L32(base, node + 12))
    {
      const uint32_t o = L32(base, node + 4);
      if (IsTurret(base, o) && !OnVehicle(base, o) && g.count < kMaxTurrets)
      {
        g.turrets[g.count++] = o;
      }
    }
  }
  float Dist(const uint8_t *b, uint32_t a, uint32_t o)
  {
    if (!Ptr(a) || !Ptr(o))
    {
      return 1e9f;
    }
    const float dx = LF(b, a + kPosition) - LF(b, o + kPosition), dy = LF(b, a + kPosition + 4) - LF(b, o + kPosition + 4),
                dz = LF(b, a + kPosition + 8) - LF(b, o + kPosition + 8);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  // The nearest live character to `near`: a guard of player 2's side (ally false) or an ally of the agent.
  uint32_t NearestCharacter(const uint8_t *base, uint32_t near, bool ally, float *dist_out)
  {
    uint32_t best = 0;
    float best_d = 1e9f;
    uint32_t node = L32(base, kCharList + 0x710);
    const uint32_t end = L32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t c = L32(base, node);
      node = L32(base, node + 8);
      if (!Ptr(c) || c == PdzCounterOpPlayer2() || c == PdzCounterOpAgent())
      {
        continue;
      }
      const uint32_t cls = L32(base, c + kClass);
      if ((cls != 0x21 && cls != 0x2A) || !(LF(base, c + kHealth) > 0.0f) || (L32(base, c + kHidden) & 0x10) ||
          L32(base, c + kParent) != 0 || PdzCounterOpIsAgentAlly(base, c) != ally)
      {
        continue;
      }
      const float d = Dist(base, c, near);
      if (d < best_d)
      {
        best_d = d;
        best = c;
      }
    }
    if (dist_out)
    {
      *dist_out = best_d;
    }
    return best;
  }

  void Spot(float theta, float radius, float out[3])
  {
    out[0] = g.centre[0] + radius * std::sin(theta);
    out[1] = g.stand_ok ? g.stand_y - 80.0f : g.centre[1];
    out[2] = g.centre[2] + radius * std::cos(theta);
  }

  // The gun's line: its centre and the direction it points in (theta0), from Rare's matrices.
  void LineOfFire(PPCContext &ctx, uint8_t *base, uint32_t turret, uint32_t gunner)
  {
    const uint32_t gun = L32(base, turret + kTurretGun);
    const Frame3 ft = FrameOf(ctx, base, turret);
    const Frame3 fg = Ptr(gun) ? FrameOf(ctx, base, gun) : ft;
    const Frame3 fp = FrameOf(ctx, base, gunner);
    float fx = ft.fwd[0], fz = ft.fwd[2]; // the turret's own forward: its rest direction (the gun's may turn with the aim)
    float len = std::sqrt(fx * fx + fz * fz);
    if (!(len > 0.2f))
    {
      fx = fg.fwd[0];
      fz = fg.fwd[2];
      len = std::sqrt(fx * fx + fz * fz);
    }
    if (!(len > 0.01f))
    {
      fx = 0.0f;
      fz = 1.0f;
      len = 1.0f;
    }
    fx /= len;
    fz /= len;
    const float bx = fg.pos[0] - fp.pos[0], bz = fg.pos[2] - fp.pos[2];
    const float blen = std::sqrt(bx * bx + bz * bz);
    const char *sign = "the gunner's place gives no side (kept)";
    if (blen > 20.0f && blen < 400.0f)
    {
      if (fx * bx + fz * bz < 0.0f)
      {
        fx = -fx;
        fz = -fz;
        sign = "turned round: the gunner stands on the other side";
      }
      else
      {
        sign = "the gunner stands behind it";
      }
    }
    g.theta0 = std::atan2(fx, fz);
    const Frame3 &c = (std::isfinite(fg.pos[0]) && (fg.pos[0] != 0.0f || fg.pos[2] != 0.0f)) ? fg : ft;
    g.centre[0] = c.pos[0];
    g.centre[1] = ft.pos[1];
    g.centre[2] = c.pos[2];
    REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the gun's line: turret matrix position ({:.0f}, {:.0f}, {:.0f}) "
                "forward ({:.2f}, {:.2f}, {:.2f}); its gun {:08X} matrix position ({:.0f}, {:.0f}, {:.0f}) forward "
                "({:.2f}, {:.2f}, {:.2f}); the gunner {:08X} matrix position ({:.0f}, {:.0f}, {:.0f}), his own +0x30 "
                "({:.0f}, {:.0f}, {:.0f}), his parent {:08X}; first direction {:.0f} deg ({}); the floor height used "
                "{:.0f} ({})",
                ft.pos[0], ft.pos[1], ft.pos[2], ft.fwd[0], ft.fwd[1], ft.fwd[2], gun, fg.pos[0], fg.pos[1], fg.pos[2],
                fg.fwd[0], fg.fwd[1], fg.fwd[2], gunner, fp.pos[0], fp.pos[1], fp.pos[2], LF(base, gunner + kPosition),
                LF(base, gunner + kPosition + 4), LF(base, gunner + kPosition + 8), L32(base, gunner + kParent),
                g.theta0 * 57.29578f, sign, g.stand_ok ? g.stand_y - 80.0f : g.centre[1],
                g.stand_ok ? "player 2's own height before the mount, less 0.8 m" : "the turret's height");
  }

  void EndPhase(const uint8_t *base)
  {
    if (g.phase == 4)
    {
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the agent in the line of fire: {} drop(s) of her health while "
                  "the gun fired and she stood in its line{}, {} other drop(s) (the gun silent: the level's guards); "
                  "her health was put back to {:.1f} after each",
                  g.hits, g.locked ? fmt::format(" (line found at {:.0f} deg)", g.locked_theta * 57.29578f)
                                   : std::string(" (NO line found)"),
                  g.other_drops, g.agent_ref);
    }
    else if ((g.phase == 5 || g.phase == 6) && Ptr(g.victim))
    {
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the {} {:08X} in the line of fire: health {:.1f} at the start, "
                  "{:.1f} now ({}); the refusals are the 'friendly fire' / 'allies' lines above",
                  g.phase == 5 ? "guard" : "ally", g.victim, g.victim_health0, LF(base, g.victim + kHealth),
                  LF(base, g.victim + kHealth) == g.victim_health0 ? "UNCHANGED" : "CHANGED");
    }
    else if (g.phase == 7)
    {
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the agent's fire on the gunner: player 2's health is {:.1f} now",
                  HumanHealth(base, PdzCounterOpPlayer2()));
    }
    g.phase = 0;
    g.victim = 0;
  }

  void StartPhase(PPCContext &ctx, uint8_t *base, int phase, uint32_t p1c, uint32_t p2c)
  {
    const uint32_t turret = GunOf(base, p2c);
    if (!turret)
    {
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step {} not started: player 2 is not the gunner of a stationary "
                  "turret ({})",
                  phase, TurretText(base, g.target));
      return;
    }
    const auto now = Clock::now();
    g.phase = phase;
    g.phase_at = g.dwell_at = now;
    g.placed_at = {};
    if (!(g.agent_ref > 0.0f))
    {
      g.agent_ref = HumanHealth(base, p1c);
    }
    if (phase == 4 || (!g.locked && g.hits == 0))
    {
      LineOfFire(ctx, base, turret, p2c);
    }
    if (const int off = REXCVAR_GET(pdz_counterop_test_p2_turret_offset_deg))
    {
      g.locked = true;
      g.locked_theta = g.theta0 + float(off) * 0.0174533f;
    }
    if (phase == 4)
    {
      g.search = 0;
      g.locked = false;
      g.dwell_hits = g.hits = g.hit_lines = g.other_drops = g.search_lines = 0;
      if (const int off = REXCVAR_GET(pdz_counterop_test_p2_turret_offset_deg))
      {
        g.locked = true;
        g.locked_theta = g.theta0 + float(off) * 0.0174533f;
      }
      g.agent_ref = HumanHealth(base, p1c);
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 4: the agent {:08X} (health {:.1f}) is put 3.5 m from the "
                  "gun in its line of fire; the direction is searched in 15 degree steps of 1.5 s until two drops of "
                  "her health come in one step while the gun fires",
                  p1c, g.agent_ref);
    }
    else if (phase == 5 || phase == 6)
    {
      float d = 0.0f;
      g.victim = NearestCharacter(base, turret, phase == 6, &d);
      g.victim_lines = 0;
      if (!Ptr(g.victim))
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step {}: no live {} found in the level's character list", phase,
                    phase == 5 ? "guard of his side" : "ally of the agent");
        g.phase = 0;
        return;
      }
      g.victim_health0 = g.victim_last = LF(base, g.victim + kHealth);
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step {}: the nearest live {} {:08X} (vtable {:08X}, class {}, "
                  "health {:.1f}, {:.1f} m away) is put 3.5 m from the gun in its line ({:.0f} deg, {})",
                  phase, phase == 5 ? "guard of his side" : "ally of the agent", g.victim, L32(base, g.victim),
                  L32(base, g.victim + kClass), g.victim_health0, d / 100.0f,
                  (g.locked ? g.locked_theta : g.theta0) * 57.29578f,
                  g.locked ? "the line step 4 found" : "the first direction: step 4 found no line");
    }
    else if (phase == 7)
    {
      g.p2_last = HumanHealth(base, p2c);
      g.p2_lines = 0;
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 7: the agent is put 4 m from the gun, 70 degrees to the side, "
                  "and her view is held on the gunner; player 2's health {:.1f}",
                  g.p2_last);
    }
  }

  void PhaseTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c)
  {
    const uint32_t turret = GunOf(base, p2c);
    const auto now = Clock::now();
    if (!turret)
    {
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step {} ends: player 2 is no longer the gunner", g.phase);
      EndPhase(base);
      return;
    }
    const bool firing = base[turret + kTurretFiring] != 0;
    // The gunner's view is held on whoever stands in the line (if Rare's gun follows his view it then points at
    // them; if it does not, the search below finds the line).
    if (g.phase != 7 && REXCVAR_GET(pdz_counterop_test_p2_turret_view))
    {
      const uint32_t who = g.phase == 4 ? p1c : g.victim;
      if (Ptr(who))
      {
        // Level, and the yaw from the gun's pivot (run 1: a view aimed from a wrong eye height sent the shots
        // past her after the first; Rare's gun takes the gunner's view angles).
        const float eye[3] = {g.centre[0], LF(base, who + kPosition + 4), g.centre[2]};
        LookFrom(ctx, base, p2c, eye, LF(base, who + kPosition), LF(base, who + kPosition + 4),
                 LF(base, who + kPosition + 8));
      }
    }
    // The agent's health is watched and refilled in every phase (the level's guards shoot at her too).
    const uint32_t part1 = L32(base, p1c + kParticipantOf);
    if (g.agent_ref > 0.0f && Ptr(part1))
    {
      const float h = LF(base, part1 + 980);
      if (h < g.agent_ref - 0.01f)
      {
        float spot[3];
        Spot(g.locked ? g.locked_theta : g.theta0 + ThetaOf(g.search), 350.0f, spot);
        const float sx = LF(base, p1c + kPosition) - spot[0], sz = LF(base, p1c + kPosition + 8) - spot[2];
        const bool in_line = g.phase == 4 && std::sqrt(sx * sx + sz * sz) < 150.0f;
        if (firing && in_line)
        {
          ++g.hits;
          ++g.dwell_hits;
          if (++g.hit_lines <= 10 || g.hits % 25 == 0)
          {
            REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the agent's health {:.1f} -> {:.1f} while player 2's gun "
                        "fires and she stands at {:.0f} deg (#{}); whose hit it was is in the 'hit asked' line just "
                        "before (a line with no source is a fall, not a bullet); put back to {:.1f}",
                        g.agent_ref, h, (g.locked ? g.locked_theta : g.theta0 + ThetaOf(g.search)) * 57.29578f, g.hits,
                        g.agent_ref);
          }
        }
        else
        {
          ++g.other_drops;
        }
        SF(base, part1 + 980, g.agent_ref);
      }
    }
    if (g.phase == 4)
    {
      if (!g.locked && g.dwell_hits >= 2)
      {
        g.locked = true;
        g.locked_theta = g.theta0 + ThetaOf(g.search);
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the gun's line is found at {:.0f} deg ({:+.0f} from the first "
                    "direction): the agent stays there",
                    g.locked_theta * 57.29578f, ThetaOf(g.search) * 57.29578f);
      }
      if (!g.locked && firing && now - g.dwell_at >= std::chrono::milliseconds(1500))
      {
        g.search = (g.search + 1) % 24;
        g.dwell_at = now;
        g.dwell_hits = 0;
        g.placed_at = {};
      }
      if (!firing)
      {
        g.dwell_at = now; // the search waits for the gun
      }
      float spot[3];
      Spot(g.locked ? g.locked_theta : g.theta0 + ThetaOf(g.search), 350.0f, spot);
      const float dx = LF(base, p1c + kPosition) - spot[0], dz = LF(base, p1c + kPosition + 8) - spot[2];
      if (g.placed_at == Clock::time_point{} ||
          ((std::sqrt(dx * dx + dz * dz) > 60.0f || LF(base, p1c + kPosition + 4) < spot[1] - 120.0f) &&
           now - g.placed_at >= std::chrono::milliseconds(700))) // strayed, or falling (runs 1-3: a drop ahead)
      {
        g.placed_at = now;
        Put(ctx, base, p1c, spot[0], spot[1], spot[2], false);
        if (++g.search_lines <= 30)
        {
          REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the agent is put at ({:.0f}, {:.0f}, {:.0f}), {:.0f} deg "
                      "round the gun (search step {}); she is now at ({:.0f}, {:.0f}, {:.0f}); the gun {}",
                      spot[0], spot[1], spot[2], (g.locked ? g.locked_theta : g.theta0 + ThetaOf(g.search)) * 57.29578f,
                      g.search, LF(base, p1c + kPosition), LF(base, p1c + kPosition + 4), LF(base, p1c + kPosition + 8),
                      firing ? "fires" : "is silent");
        }
      }
    }
    else if (g.phase == 5 || g.phase == 6)
    {
      if (!Ptr(g.victim))
      {
        return;
      }
      float spot[3];
      Spot(g.locked ? g.locked_theta : g.theta0, 350.0f, spot);
      const float dx = LF(base, g.victim + kPosition) - spot[0], dz = LF(base, g.victim + kPosition + 8) - spot[2];
      if (g.placed_at == Clock::time_point{} ||
          (std::sqrt(dx * dx + dz * dz) > 80.0f && now - g.placed_at >= std::chrono::milliseconds(500)))
      {
        g.placed_at = now;
        Put(ctx, base, g.victim, spot[0], spot[1], spot[2], false);
      }
      const float h = LF(base, g.victim + kHealth);
      if (h != g.victim_last && ++g.victim_lines <= 8)
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the {} {:08X} in the line: health {:.1f} -> {:.1f} (the gun "
                    "{}): HIS HEALTH CHANGED",
                    g.phase == 5 ? "guard" : "ally", g.victim, g.victim_last, h, firing ? "fires" : "is silent");
      }
      g.victim_last = h;
    }
    else if (g.phase == 7)
    {
      float spot[3];
      Spot(g.locked ? g.locked_theta : g.theta0 + 1.2217f, 350.0f, spot); // the solid spot, else 70 degrees to the side
      const float dx = LF(base, p1c + kPosition) - spot[0], dz = LF(base, p1c + kPosition + 8) - spot[2];
      if (g.placed_at == Clock::time_point{} ||
          (std::sqrt(dx * dx + dz * dz) > 100.0f && now - g.placed_at >= std::chrono::milliseconds(1500)))
      {
        g.placed_at = now;
        Put(ctx, base, p1c, spot[0], spot[1], spot[2], false);
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the agent is put at ({:.0f}, {:.0f}, {:.0f}) beside the gun; she "
                    "is now at ({:.0f}, {:.0f}, {:.0f}); the gunner's own +0x30 ({:.0f}, {:.0f}, {:.0f})",
                    spot[0], spot[1], spot[2], LF(base, p1c + kPosition), LF(base, p1c + kPosition + 4),
                    LF(base, p1c + kPosition + 8), LF(base, p2c + kPosition), LF(base, p2c + kPosition + 4),
                    LF(base, p2c + kPosition + 8));
      }
      // Run 1: a gunner's own +0x30 is a world position (his matrix position is the turret's).
      Look(ctx, base, p1c, LF(base, p2c + kPosition), LF(base, p2c + kPosition + 4) + 20.0f,
           LF(base, p2c + kPosition + 8), 70.0f);
      const float h = HumanHealth(base, p2c);
      if (h != g.p2_last && ++g.p2_lines <= 14)
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: player 2's health at the gun {:.1f} -> {:.1f} (#{}): THE "
                    "AGENT'S FIRE HURTS THE GUNNER",
                    g.p2_last, h, g.p2_lines);
      }
      g.p2_last = h;
    }
  }

  void Step(PPCContext &ctx, uint8_t *base, int32_t step, uint32_t p1c, uint32_t p2c, bool out)
  {
    if (g.phase)
    {
      EndPhase(base);
    }
    g.staged = false;
    switch (step)
    {
    case 1:
    {
      Collect(base);
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 1: {} stationary turret(s) in the world list (class 0x2E, "
                  "the parent not a vehicle prop); player 2 {:08X} at ({:.0f}, {:.0f}, {:.0f}){}, the agent at "
                  "({:.0f}, {:.0f}, {:.0f})",
                  g.count, p2c, LF(base, p2c + kPosition), LF(base, p2c + kPosition + 4), LF(base, p2c + kPosition + 8),
                  out ? " (out of the world)" : "", LF(base, p1c + kPosition), LF(base, p1c + kPosition + 4),
                  LF(base, p1c + kPosition + 8));
      for (int i = 0; i < g.count; ++i)
      {
        const uint32_t t = g.turrets[i];
        float gd = 0.0f;
        const uint32_t guard = NearestCharacter(base, t, false, &gd);
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: #{}: {}; parent {:08X}; {:.1f} m from player 2, {:.1f} m from "
                    "the agent; the nearest live guard {:08X} is {:.1f} m from it",
                    i, TurretText(base, t), L32(base, t + kParent), Dist(base, t, p2c) / 100.0f,
                    Dist(base, t, p1c) / 100.0f, guard, gd / 100.0f);
      }
      break;
    }
    case 2:
    {
      Collect(base);
      const int pick = REXCVAR_GET(pdz_counterop_test_p2_turret_pick);
      uint32_t best = 0;
      float best_d = 1e9f;
      for (int i = 0; i < g.count; ++i)
      {
        const uint32_t t = g.turrets[i];
        if (pick >= 0 ? i != pick : (L32(base, t + kTurretState) != 0 || !(L32(base, t + kTurretUsable) & 1)))
        {
          continue;
        }
        const float d = Dist(base, t, p2c);
        if (d < best_d)
        {
          best_d = d;
          best = t;
        }
      }
      if (!best || out)
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 2 not done: {} (of {} stationary)",
                    out ? "player 2 is out of the world" : "no free usable stationary turret", g.count);
        break;
      }
      g.target = best;
      const Frame3 ft = FrameOf(ctx, base, best);
      float fx = ft.fwd[0], fz = ft.fwd[2];
      const float len = std::sqrt(fx * fx + fz * fz);
      if (len > 0.01f)
      {
        fx /= len;
        fz /= len;
      }
      else
      {
        fx = 0.0f;
        fz = 1.0f;
      }
      const float tx = LF(base, best + kPosition), ty = LF(base, best + kPosition + 4), tz = LF(base, best + kPosition + 8);
      Put(ctx, base, p2c, tx - fx * 130.0f, ty + 30.0f, tz - fz * 130.0f, true);
      g.staged = true;
      g.staged_at = Clock::now();
      g.stand_ok = false;
      g.prompt_seen = 0;
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 2: player 2 is put 1.3 m behind the {} (matrix position "
                  "({:.0f}, {:.0f}, {:.0f}), forward ({:.2f}, {:.2f}, {:.2f})), {:.1f} m from where he was; he is now "
                  "at ({:.0f}, {:.0f}, {:.0f}); his view is held on it; a script presses his Use (A) next",
                  TurretText(base, best), ft.pos[0], ft.pos[1], ft.pos[2], ft.fwd[0], ft.fwd[1], ft.fwd[2],
                  best_d / 100.0f, LF(base, p2c + kPosition), LF(base, p2c + kPosition + 4),
                  LF(base, p2c + kPosition + 8));
      break;
    }
    case 3:
    {
      const uint32_t turret = GunOf(base, p2c);
      if (turret)
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 3: player 2 IS the gunner (by his own Use press): {}",
                    TurretText(base, turret));
        break;
      }
      if (!Ptr(g.target) || out)
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 3 not done: no turret chosen, or he is out of the world");
        break;
      }
      const uint32_t prompt = L32(base, p2c + kPromptProp);
      Call(ctx, base, sub_8258FA50, g.target, p2c);
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 3: his Use press did NOT mount him (the prop of his prompt "
                  "is {:08X}; the turret's prompt was {} during the 5 s); the game's Use of the turret 8258FA50(turret, "
                  "player 2) is called for him: {}; his parent {:08X} ({})",
                  prompt, g.prompt_seen ? "offered to him" : "never offered to him", TurretText(base, g.target),
                  L32(base, p2c + kParent), GunOf(base, p2c) ? "HE IS THE GUNNER" : "he is NOT the gunner");
      break;
    }
    case 4:
    case 5:
    case 6:
    case 7:
      StartPhase(ctx, base, step, p1c, p2c);
      break;
    case 14: // step 4 once more (another pdz_counterop_test_p2_turret_offset_deg)
      StartPhase(ctx, base, 4, p1c, p2c);
      break;
    case 15: // step 5 once more (the next nearest guard)
      StartPhase(ctx, base, 5, p1c, p2c);
      break;
    case 8:
    {
      const uint32_t participant = Ptr(p2c) ? L32(base, p2c + kParticipantOf) : 0;
      if (Ptr(participant) && !out)
      {
        const float before = LF(base, participant + 980);
        SF(base, participant + 980, kOwnFloor);
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 8: player 2's health {:.1f} -> {:.1f} (his floor){}: his "
                    "life ends on this tick as from a hit",
                    before, kOwnFloor, GunOf(base, p2c) ? " at the gun" : " (he is NOT at a gun)");
      }
      else
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 8: nothing to do (player 2 is out of the world)");
      }
      break;
    }
    case 9:
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 9: {}; player 2 {:08X} is {}, his parent {:08X}, health {:.1f}",
                  TurretText(base, g.target), p2c, out ? "out of the world" : "in the world",
                  Ptr(p2c) ? L32(base, p2c + kParent) : 0, HumanHealth(base, p2c));
      break;
    case 20:
    {
      if (!Ptr(g.target))
      {
        Collect(base);
        g.target = g.count ? g.turrets[0] : 0;
      }
      float d = 0.0f;
      const uint32_t guard = Ptr(g.target) ? NearestCharacter(base, g.target, false, &d) : 0;
      if (!Ptr(guard) || L32(base, g.target + kTurretState) != 0)
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 20 not done: no live guard, or the turret is not free: {}",
                    TurretText(base, g.target));
        break;
      }
      g.test_guard = guard;
      const uint32_t r = Call(ctx, base, sub_825904F0, g.target, guard, 1) & 0xFF;
      REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 20: the nearest live guard {:08X} (vtable {:08X}, {:.1f} m "
                  "away) is made the gunner: the game's mount 825904F0(turret, guard, 1) answered {}: {}; his parent "
                  "{:08X}",
                  guard, L32(base, guard), d / 100.0f, r, TurretText(base, g.target), L32(base, guard + kParent));
      break;
    }
    case 21:
    {
      const uint32_t guard = g.test_guard;
      if (Ptr(guard))
      {
        const Frame3 fgd = FrameOf(ctx, base, guard);
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 21: the guard {:08X}: parent {:08X}, health {:.1f}, his "
                    "own +0x30 ({:.0f}, {:.0f}, {:.0f}), his matrix position ({:.0f}, {:.0f}, {:.0f}); {}",
                    guard, L32(base, guard + kParent), LF(base, guard + kHealth), LF(base, guard + kPosition),
                    LF(base, guard + kPosition + 4), LF(base, guard + kPosition + 8), fgd.pos[0], fgd.pos[1],
                    fgd.pos[2], TurretText(base, g.target));
      }
      break;
    }
    case 22:
    {
      if (Ptr(g.target) && Ptr(g.test_guard) && L32(base, g.target + kTurretGunner) == g.test_guard)
      {
        Call(ctx, base, sub_82590E30, g.target);
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step 22: the guard {:08X} is taken off the gun (82590E30): {}; "
                    "his parent {:08X}",
                    g.test_guard, TurretText(base, g.target), L32(base, g.test_guard + kParent));
      }
      g.test_guard = 0;
      break;
    }
    default:
      break;
    }
  }
} // namespace

namespace
{
  // TESTING TOOL: while the tool's value is not 0 the agent and her allies are kept alive.
  void KeepAlive(uint8_t *base, uint32_t p1c)
  {
    const uint32_t part1 = L32(base, p1c + kParticipantOf);
    if (Ptr(part1))
    {
      const float h = LF(base, part1 + 980);
      if (!(g.agent_ref > 0.0f) && h > 1.5f)
      {
        g.agent_ref = h;
      }
      else if (g.agent_ref > 0.0f && h < g.agent_ref - 0.01f && g.phase != 4)
      {
        ++g.other_drops;
        SF(base, part1 + 980, g.agent_ref); // phase 4 counts and refills its own
      }
    }
    uint32_t node = L32(base, kCharList + 0x710);
    const uint32_t end = L32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t c = L32(base, node);
      node = L32(base, node + 8);
      if (!Ptr(c) || !PdzCounterOpIsAgentAlly(base, c))
      {
        continue;
      }
      const float h = LF(base, c + kHealth);
      int k = 0;
      while (k < g.kept_count && g.kept[k] != c)
      {
        ++k;
      }
      if (k == g.kept_count)
      {
        if (g.kept_count >= 8 || !(h > 0.0f))
        {
          continue;
        }
        g.kept[g.kept_count] = c;
        g.kept_health[g.kept_count++] = h;
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the ally {:08X} (vtable {:08X}, health {:.1f}) is kept alive "
                    "from now (his health is put back every tick)",
                    c, L32(base, c), h);
      }
      else if (h < g.kept_health[k] && h > 0.0f)
      {
        SF(base, c + kHealth, g.kept_health[k]);
      }
      else if (!(h > 0.0f) && ++g.kept_lines <= 4)
      {
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the ally {:08X} DIED all the same (health {:.1f})", c, h);
      }
    }
  }
} // namespace

// counter_op_pvp.cpp's hook of Rare's "does this hit count" (82545698: victim, source), asked after the allies and
// Outpost rules. True = the hit does not count. Only while player 2 is the gunner of a stationary turret.
bool PdzCounterOpTurretHitRefused(const uint8_t *base, uint32_t victim, uint32_t source, uint32_t from)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || !Ptr(victim))
  {
    return false;
  }
  const uint32_t turret = GunOf(base, p2);
  if (!turret)
  {
    return false;
  }
  const uint32_t agent = PdzCounterOpAgent();
  const uint32_t gun = L32(base, turret + kTurretGun);
  const uint32_t owner = Ptr(source) ? L32(base, source + kParent) : 0;
  const bool his = Ptr(source) && (source == p2 || source == turret || (Ptr(gun) && source == gun) || owner == p2 ||
                                   owner == turret || (Ptr(gun) && owner == gun) || PdzCounterOpIsOf(base, source, p2));
  // Every question asked while the gun fires (and for 45 ticks after) is logged, whoever the source: runs 1-3 showed
  // deaths in front of the gun that were falls (no source, asked from 82548570), caused by the testing tool.
  const auto is_char = [&](uint32_t o) {
    if (!Ptr(o))
    {
      return false;
    }
    const uint32_t cls = L32(base, o + kClass);
    return cls == 2 || cls == 0x21 || cls == 0x2A;
  };
  const bool window = g.tick - g.fire_tick <= 45;
  const bool others = Ptr(source) && !his && (is_char(source) || is_char(owner));
  const bool unnamed = window && !his && !others;
  if (!his && victim != p2 && !window)
  {
    return false;
  }
  const bool refuse = his && victim != agent && victim != p2 && REXCVAR_GET(pdz_counterop_turret_only_agent);
  ++g.hits_asked;
  g.hits_refused += refuse;
  if (++g.hit_lines2 <= 90 || g.hit_lines2 % 200 == 0)
  {
    const uint32_t vcls = L32(base, victim + kClass);
    REXLOG_INFO("pdz_counterop: turrets: hit asked while player 2 mans the stationary gun (#{}; the gun {}): victim "
                "{:08X} ({}; vtable {:08X}, class {}, masks {:04X} / {:04X} / {:04X}), source {:08X} (vtable {:08X}, "
                "class {}, its +0x5C {:08X}; {}), asked from {:08X}; player 2's masks {:04X} / {:04X} / {:04X}: {}",
                g.hit_lines2, base[turret + kTurretFiring] ? "fires" : window ? "fired a moment ago" : "is silent",
                victim, victim == agent ? "THE AGENT" : victim == p2 ? "PLAYER 2" : "someone else", L32(base, victim),
                vcls, L32(base, victim + 128) >> 16, L32(base, victim + 130) >> 16, L32(base, victim + 132) >> 16,
                source, Ptr(source) ? L32(base, source) : 0, Ptr(source) ? L32(base, source + kClass) : 0, owner,
                his      ? "his: the gunner, the turret, its gun or a thing of theirs"
                : others ? "another character's"
                : unnamed ? "names nobody (a fall, a level hazard): not the gun's"
                          : "not his",
                from, L32(base, p2 + 128) >> 16, L32(base, p2 + 130) >> 16, L32(base, p2 + 132) >> 16,
                refuse                         ? "REFUSED (pdz_counterop_turret_only_agent): his turret's shots count on the agent only"
                : victim == agent              ? "left to the game (a hit on the agent)"
                : victim == p2                 ? "left to the game (a hit on the gunner)"
                : his                          ? "left to the other rules (pdz_counterop_turret_only_agent is off)"
                                               : "not the gun's: left to the game");
  }
  return refuse;
}

// counter_op.cpp's Reset (every level boundary).
void PdzCounterOpTurretsReset()
{
  g = State{};
}

// counter_op.cpp: true while player 2 is the gunner of a stationary turret (no weapon redraw then).
bool PdzCounterOpTurretOnGun(const uint8_t *base)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  return p2 && On() && GunOf(base, p2) != 0;
}

// counter_op.cpp OwnDeath() and HideP2(), first thing: a gunner is taken off the gun before his life's end reads
// where he is and before he is taken out of the world.
void PdzCounterOpTurretBeforeHide(PPCContext &ctx, uint8_t *base, uint32_t p2, const char *why)
{
  if (!Ptr(p2))
  {
    return;
  }
  const uint32_t turret = GunOf(base, p2);
  if (!turret)
  {
    return;
  }
  if (!On())
  {
    REXLOG_INFO("pdz_counterop: turrets: player 2 leaves the world ({}) as the gunner of {} and "
                "pdz_counterop_turret_leave_first is off: he is NOT taken off the gun first (round 13)",
                why ? why : "", TurretText(base, turret));
    return;
  }
  const std::string before = TurretText(base, turret);
  const float lx = LF(base, p2 + kPosition), ly = LF(base, p2 + kPosition + 4), lz = LF(base, p2 + kPosition + 8);
  Call(ctx, base, sub_82590E30, turret);
  REXLOG_INFO("pdz_counterop: turrets: player 2 {:08X} is taken off the stationary gun first ({}; Rare's emergency "
              "evacuation 82590E30): before: {}; after: {}; his parent now {:08X}; his position ({:.0f}, {:.0f}, "
              "{:.0f}) -> ({:.0f}, {:.0f}, {:.0f})",
              p2, why ? why : "", before, TurretText(base, turret), L32(base, p2 + kParent), lx, ly, lz,
              LF(base, p2 + kPosition), LF(base, p2 + kPosition + 4), LF(base, p2 + kPosition + 8));
}

// counter_op.cpp SpawnAt(), just before the taken guard is hidden: a guard who mans a stationary turret leaves it.
void PdzCounterOpTurretUnseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
{
  g.pending_turret = g.pending_guard = 0;
  const uint32_t turret = GunOf(base, guard);
  if (!turret || !On())
  {
    if (turret)
    {
      REXLOG_INFO("pdz_counterop: turrets: the guard {:08X} taken for player 2 is the gunner of {} and "
                  "pdz_counterop_turret_leave_first is off: he is hidden at the gun (round 13)",
                  guard, TurretText(base, turret));
    }
    return;
  }
  const std::string before = TurretText(base, turret);
  Call(ctx, base, sub_82590E30, turret);
  g.pending_turret = turret;
  g.pending_guard = guard;
  REXLOG_INFO("pdz_counterop: turrets: the guard {:08X} taken for player 2 {:08X} is taken off his stationary gun "
              "first (82590E30): before: {}; after: {}; the guard's parent now {:08X}",
              guard, p2, before, TurretText(base, turret), L32(base, guard + kParent));
}

// counter_op.cpp SpawnAt(), right after the taken guard is hidden: player 2 is seated at that gun.
void PdzCounterOpTurretAfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
{
  const uint32_t turret = g.pending_turret;
  const bool mine = turret && guard == g.pending_guard;
  g.pending_turret = g.pending_guard = 0;
  if (!mine || !Ptr(p2) || !REXCVAR_GET(pdz_counterop_turret_take_gunner))
  {
    return;
  }
  const uint32_t r = Call(ctx, base, sub_825904F0, turret, p2, 1) & 0xFF;
  REXLOG_INFO("pdz_counterop: turrets: player 2 {:08X} takes the guard's place at the stationary gun "
              "(pdz_counterop_turret_take_gunner): Rare's mount 825904F0(turret, player 2, 1) answered {}: {}; his "
              "parent {:08X} ({})",
              p2, r, TurretText(base, turret), L32(base, p2 + kParent),
              GunOf(base, p2) == turret ? "he is the gunner" : "he is NOT the gunner");
}

// counter_op.cpp Tick(), every level tick of a started Counter-Op game.
void PdzCounterOpTurretsTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, bool out, bool cutscene)
{
  if (!Ptr(p1c) || !Ptr(p2c))
  {
    return;
  }
  // [1] The watch: one line when he mounts, when he fires, when he is off.
  const uint32_t turret = out ? 0 : GunOf(base, p2c);
  ++g.tick;
  if (turret && base[turret + kTurretFiring])
  {
    g.fire_tick = g.tick;
  }
  if (turret != g.on_turret)
  {
    const auto now = Clock::now();
    if (turret)
    {
      ++g.mounts;
      g.mounted_at = now;
      g.last_firing = base[turret + kTurretFiring];
      g.fire_lines = 0;
      REXLOG_INFO("pdz_counterop: turrets: player 2 {:08X} mans a stationary gun (#{} this level): {}; its parent "
                  "{:08X}; his health {:.1f}; his shots count on the agent only, the agent's shots hurt him",
                  p2c, g.mounts, TurretText(base, turret), L32(base, turret + kParent), HumanHealth(base, p2c));
    }
    else
    {
      REXLOG_INFO("pdz_counterop: turrets: player 2 is off the stationary gun after {:.1f} s ({}): {}",
                  std::chrono::duration<double>(now - g.mounted_at).count(), out ? "he is out of the world" : "he got off",
                  TurretText(base, g.on_turret));
    }
    g.on_turret = turret;
  }
  if (turret)
  {
    const uint8_t firing = base[turret + kTurretFiring];
    if (firing != g.last_firing)
    {
      g.last_firing = firing;
      if (++g.fire_lines <= 8)
      {
        REXLOG_INFO("pdz_counterop: turrets: player 2 {} the stationary gun {:08X} (its firing byte; #{}{})",
                    firing ? "fires" : "stops firing", turret, g.fire_lines,
                    g.fire_lines == 8 ? "; further times not logged" : "");
      }
    }
  }
  // [3] The testing tool.
  const int32_t step = REXCVAR_GET(pdz_counterop_test_p2_turret);
  if (step == 0 && g.step == 0)
  {
    return;
  }
  if (cutscene)
  {
    return;
  }
  if (step != 0)
  {
    KeepAlive(base, p1c);
  }
  if (step != g.step)
  {
    g.step = step;
    Step(ctx, base, step, p1c, p2c, out);
  }
  if (g.staged && !out)
  {
    if (GunOf(base, p2c))
    {
      g.staged = false;
    }
    else if (Ptr(g.target))
    {
      if (L32(base, p2c + kParent) == 0)
      {
        g.stand_y = LF(base, p2c + kPosition + 4);
        g.stand_ok = true;
      }
      const uint32_t prompt = L32(base, p2c + kPromptProp);
      if (prompt == g.target && !g.prompt_seen)
      {
        g.prompt_seen = 1;
        REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: the turret {:08X} offers player 2 its prompt (his +0x1A30) "
                    "where he stands, {:.1f} m from it",
                    g.target, Dist(base, g.target, p2c) / 100.0f);
      }
      if (Clock::now() - g.staged_at < std::chrono::milliseconds(6000))
      {
        Look(ctx, base, p2c, LF(base, g.target + kPosition), LF(base, p2c + kPosition + 4) + 40.0f,
             LF(base, g.target + kPosition + 8), 60.0f); // nearly level: a little down
      }
    }
  }
  if (g.phase && !out)
  {
    PhaseTick(ctx, base, p1c, p2c);
  }
  else if (g.phase && out)
  {
    REXLOG_INFO("pdz_counterop: turrets: TEST TOOL: step {} ends: player 2 is out of the world", g.phase);
    EndPhase(base);
  }
}
