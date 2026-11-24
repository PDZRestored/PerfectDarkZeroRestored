// Counter-Operative, Sat 21 Nov (spectate job): player 2's waiting view is RARE'S OWN spectator chase camera.
//
// Research (research notes: research_camera\SPECTATE_CAMERA_Sat21Nov.md, static reading of TU3):
//   - Each split-screen view has a camera manager ([character + 0x19A4] for a player) holding a stack of camera
//     modes. Every frame the task CameraControlUpdate (8220F478) calls 8220C0D8(manager, step): the top mode's
//     update, then 8220C3F8 builds the view from the top mode's +4 / +0x10 / +0x34. A view only changes when
//     8220C0D8 runs for its manager (writing a mode's fields alone shows nothing: the old follow write).
//   - Rare's spectator camera is camera MODE 1 (vtable 82095FF8, update 82217838): an orbiting chase camera around a
//     target (pivot = the target's position + its eye height, 180-225 cm away), with its own wall handling (a 15 cm
//     sphere cast from the pivot, pulled in on a hit; +0x15C: each frame it tests 10 directions and steers toward
//     the most open one). It reads no controller input. Campaign / co-op use it as the death camera (825B18D0).
//   - DarkOps (8241F838) switches a spectating player to it like this: pop his stack to mode 0, push mode 1
//     (8220CCD0(manager, 1)), target 82218500(mode, target, &zero), and its values (distances 180 / 225, pitch
//     0.3-1.2 rad, yaw from the target's +0xE4 object, the open-space orbit on, the spectate callback 825ADD28).
//     Nothing of that needs the multiplayer objects; only DarkOps' choice of WHOM to follow does.
// Here (pdz_counterop_rare_spectate, default on): while player 2 waits for a body (level start, after a death, after
// a checkpoint restart or reload) or is out for good, the same push is made on HIS manager with JO as the target
// (re-targeted if Jo is rebuilt), and his manager is updated once a frame right after hers when Rare did not update
// it (8220C0D8 hook). The moment he gets a body (ShowP2), or when he drives a spiderbot / rides, the mode is popped
// and his own first-person view is back. Our own stand-in (the hidden body carried behind her,
// pdz_counterop_wait_view_upright) stays as the fallback only when Rare's camera cannot be put on his manager
// (no manager yet, a stack that is not his gameplay camera's). Off = the stand-in as before.
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include <fmt/format.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DEFINE_BOOL(pdz_counterop_rare_spectate, true, "Gameplay",
                    "Counter-Operative (Sat 21 Nov, default on): while player 2 waits for a body (or is out for good) "
                    "his half of the screen is Rare's own spectator chase camera (the multiplayer / death camera, "
                    "camera mode 1) following the agent, with Rare's own wall handling; his own view is back the moment "
                    "he gets a body. Off = our stand-in (his hidden body carried behind her, "
                    "pdz_counterop_wait_view_upright)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Mon 23 Nov (co19). Log 056 (Bridge): in his first wait the chase camera sat 9.5 m behind the agent, 3.6 m up (the
// view the owner called fine); in his second wait it sat 2 m from her, 1.5 m up: inside her head when she ran, on her
// back when she took cover. Both are Rare's: the spectate callback (825ADD28) writes the mode's two distances every
// frame: 150 / 225 cm for a target on foot, and for a target on a vehicle (the tank she starts Bridge on) the size of
// what she rides over the tangent of the view angle (940 / 1129 cm there). Here the callback's result is raised to
// the tank's numbers for player 2's wait camera only, so every wait has the first wait's view; Rare's wall handling
// and open-space orbit work on as before (the camera still comes closer where a wall is in the way).
// Mon 23 Nov (co20, the owner's decision): the default is 0 again: Rare's own chase camera, the standard spectator
// view of the other levels. The raise stays as a switch only.
REXCVAR_DEFINE_DOUBLE(pdz_counterop_wait_camera_cm, 0.0, "Gameplay",
                      "Counter-Operative (Mon 23 Nov, default 0 = Rare's own spectator chase camera: 150 / 225 cm from "
                      "a target on foot, further for one on a vehicle): when above 0, the least distance in "
                      "centimetres of player 2's waiting camera from the agent; its wanted distance is 1.2 times "
                      "that, as Rare's own rule for a target on a vehicle")
    .range(0.0, 3000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Mon 23 Nov night (1.0.1). Logs 057 / 062: when player 2 died his picture was, for a frame, his hidden body's eye
// (our stand-in, 1.2 m from the agent's eye), and the frame after that a stale picture: the chase camera was only
// pushed once the replaced guard's kill had run (one or two ticks after the death; the spectate step also ran before
// the death check), and a pushed camera got its first update only on the frame after the push. Now the push is made
// in the death's own tick (counter_op.cpp asks again right after the death, and a pending guard kill no longer holds
// the camera back: the push moves no body) and the pushed camera is updated in the same frame, right after the
// agent's. The camera's distance is Rare's own from its first update (the eased distance +0x1A4 is at its goal at
// the push already: measured, nothing to change there).
REXCVAR_DEFINE_BOOL(pdz_counterop_wait_camera_at_once, true, "Gameplay",
                    "Counter-Operative (1.0.1, default on): when player 2 dies, Rare's spectator chase camera is put "
                    "on his half in the same frame and updated in it, so his first waiting picture is that camera at "
                    "Rare's own distance. Off = 1.0.0 (a frame at her eye and a stale frame first)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(sub_8220CCD0);        // camera manager: create mode id and push it (r3 manager, r4 id) -> the mode
REX_EXTERN(__imp__sub_825ADD28); // Rare's spectate callback (r3 mode, ...): writes the mode's distances +0x104 / +0x108
REX_EXTERN(sub_8220CE08);        // camera manager: pop the top (r3 manager, r4 1 = delete, r5 0)
REX_EXTERN(sub_82218500);        // chase mode: set its target (r3 mode, r4 target object, r5 -> offset vec3)
REX_EXTERN(sub_82276B58);        // an eased value: set its goal (r3 -> value, f1 goal)
REX_EXTERN(__imp__sub_8220C0D8); // camera manager: update (r3 manager, f1 step)

namespace
{
  constexpr uint32_t kManagerOf = 0x19A4;                 // player character -> his camera manager
  constexpr uint32_t kStackBase = 0x14, kStackTop = 0x444; // manager: mode stack, one past the top
  constexpr uint32_t kMode0Vtable = 0x82095EF0, kMode1Vtable = 0x82095FF8;
  constexpr uint32_t kSpectateCallback = 0x825ADD28;      // Rare's spectate pivot callback (mode +0x19C)
  constexpr uint32_t kWantedDistance = 0x82D35CE4;        // Rare's tunable: the wanted distance (225 cm)
  constexpr uint32_t kTargetPtr = 0x80;                   // mode 1: the target (weak ref pointer)

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
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  struct State
  {
    bool on = false;
    uint32_t mgr = 0, mode = 0, jo = 0, p2c = 0, jo_mgr = 0;
    uint32_t scratch = 0;
    uint32_t rare_since = 0;     // Rare's updates of his manager since her last update
    uint64_t ours = 0, rare = 0; // his manager's updates: ours / Rare's (while on)
    uint32_t retargets = 0, pushes = 0, lost = 0;
    uint32_t first_ticks = 0;    // Mon 23 Nov night: ticks since the last push (the first ones are logged)
    float eased_was = 0.0f;      // the eased distance as Rare's push left it
    int64_t retry_at = 0, log_at = 0;
    uint32_t fail_lines = 0;
    bool in_extra = false;
  } g;

  int32_t Depth(const uint8_t *base, uint32_t mgr)
  {
    const uint32_t top = Load32(base, mgr + kStackTop);
    if (top < mgr + kStackBase || top > mgr + kStackBase + 4 * 64)
    {
      return -1;
    }
    return int32_t((top - (mgr + kStackBase)) / 4);
  }
  uint32_t Top(const uint8_t *base, uint32_t mgr)
  {
    return Depth(base, mgr) > 0 ? Load32(base, Load32(base, mgr + kStackTop) - 4) : 0;
  }
  std::string StackText(const uint8_t *base, uint32_t mgr)
  {
    const int32_t n = Depth(base, mgr);
    if (n < 0)
    {
      return "unreadable";
    }
    std::string s = fmt::format("{} mode(s):", n);
    for (int32_t i = 0; i < n && i < 8; ++i)
    {
      const uint32_t m = Load32(base, mgr + kStackBase + 4 * uint32_t(i));
      const uint32_t vt = Ptr(m) ? Load32(base, m) : 0;
      s += fmt::format(" {:08X} ({})", m,
                       vt == kMode0Vtable   ? "mode 0: first person"
                       : vt == kMode1Vtable ? "mode 1: chase"
                                            : fmt::format("vtable {:08X}", vt));
    }
    return s;
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

  // Rare's spectate push (8241F838's camera part) on his manager, target Jo. Returns the mode or 0.
  uint32_t Push(PPCContext &ctx, uint8_t *base, uint32_t mgr, uint32_t jo, uint32_t p2c, std::string &why)
  {
    const int32_t depth = Depth(base, mgr);
    if (depth < 0 || depth > 16)
    {
      why = fmt::format("his camera manager {:08X} has an unreadable stack (depth {})", mgr, depth);
      return 0;
    }
    // Pop down to his gameplay camera (mode 0), bounded (Rare's own loop never ends on a stack with no mode 0).
    for (int i = 0; i < 16 && Depth(base, mgr) > 1 && Ptr(Top(base, mgr)) && Load32(base, Top(base, mgr)) != kMode0Vtable;
         ++i)
    {
      Call(ctx, base, sub_8220CE08, mgr, 1, 0);
    }
    if (Depth(base, mgr) > 0 && Load32(base, Top(base, mgr)) != kMode0Vtable)
    {
      why = fmt::format("his camera stack does not end in his gameplay camera ({})", StackText(base, mgr));
      return 0;
    }
    // The yaw to start from: Jo's (Rare reads the target's +0xE4 object, its vtable +0x10 -> a float).
    float yaw = 0.0f;
    const uint32_t eye = Load32(base, jo + 0xE4);
    // Sun 22 Nov night (audit): a readable eye object whose vtable is in the game image (else the view-yaw fallback).
    const uint32_t eye_vt = pdz::guestmem::Ok(base, eye, 4) ? Load32(base, eye) : 0;
    if (eye_vt >= 0x82000000u && eye_vt < 0x83000000u && pdz::guestmem::Ok(base, eye_vt + 0x10, 4))
    {
      if (PPCFunc *fn = pdz::GuestFunction(Load32(base, Load32(base, eye) + 0x10), "the target's eye object (+0x10)"))
      {
        const uint32_t p = Call(ctx, base, fn, eye);
        if (Ptr(p) && std::isfinite(LoadFloat(base, p)))
        {
          yaw = LoadFloat(base, p);
        }
      }
    }
    // Jungle / Datacore runs: that object gave 0.00 both times and the camera sat in front of her (her face in his
    // picture). Her own view yaw (+0x1670, degrees; forward = (sin, cos)) puts the camera behind her as in DarkOps.
    const float rare_yaw = yaw;
    const float view_deg = LoadFloat(base, jo + 0x1670);
    if (std::isfinite(view_deg) && std::fabs(view_deg) <= 720.0f)
    {
      yaw = view_deg * 3.14159265f / 180.0f;
    }
    const uint32_t m = Call(ctx, base, sub_8220CCD0, mgr, 1);
    if (!Ptr(m) || Load32(base, m) != kMode1Vtable)
    {
      why = fmt::format("Rare's push of the chase mode gave {:08X}", m);
      return 0;
    }
    if (!g.scratch)
    {
      g.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
    }
    if (!g.scratch)
    {
      why = "no scratch memory";
      return m;
    }
    StoreFloat(base, g.scratch, 0.0f), StoreFloat(base, g.scratch + 4, 0.0f), StoreFloat(base, g.scratch + 8, 0.0f);
    Call(ctx, base, sub_82218500, m, jo, g.scratch);
    // Rare's values (8241FAB4..8241FC04).
    StoreFloat(base, m + 0x10C, 0.3f);
    StoreFloat(base, m + 0x110, -1.0f);
    StoreFloat(base, m + 0x120, 2.0f);
    StoreFloat(base, m + 0x134, 2.0f);
    StoreFloat(base, m + 0x124, 6.2831853f);
    StoreFloat(base, m + 0x138, 6.2831853f);
    StoreFloat(base, m + 0x118, 0.3f);
    StoreFloat(base, m + 0x11C, 1.2f);
    StoreFloat(base, m + 0x12C, yaw);
    StoreFloat(base, m + 0x130, yaw);
    StoreFloat(base, m + 0x104, 180.0f);
    float wanted = LoadFloat(base, kWantedDistance);
    if (!std::isfinite(wanted) || wanted < 50.0f || wanted > 2000.0f)
    {
      wanted = 225.0f;
    }
    StoreFloat(base, m + 0x108, wanted);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = m + 0x1A4;
      frame.ctx.f1.f64 = wanted;
      sub_82276B58(frame.ctx, base);
    }
    g.eased_was = LoadFloat(base, m + 0x1A4); // (diagnostic: the eased distance as the push left it)
    if (base[m + 0x128])
    {
      StoreFloat(base, m + 0x144, 0.0f);
    }
    base[m + 0x128] = 0;
    base[m + 0x15C] = 1;
    StoreFloat(base, m + 0x1C4, 20.0f);
    // Its starting angles (+0x160 pitch, +0x164 yaw: the reset leaves both at 0, and co_datacore_spec1's first second
    // showed the camera placed 3-4 m under her, through the floor, before the orbit climbed): the middle of Rare's
    // pitch window (0.3-1.2 rad) and her yaw, so the first placement is already above and behind her.
    StoreFloat(base, m + 0x160, 0.75f);
    StoreFloat(base, m + 0x164, yaw);
    Store32(base, m + 0x19C, kSpectateCallback);
    Store32(base, m + 0x1A0, p2c);
    why = fmt::format("yaw {:.2f} rad (her view yaw {:.1f} deg; the eye object gave {:.2f}), distance 180 / {:.0f} cm",
                      yaw, view_deg, rare_yaw, wanted);
    return m;
  }

  void LogState(const uint8_t *base, const char *what)
  {
    const uint32_t m = g.mode;
    const float cx = LoadFloat(base, m + 4), cy = LoadFloat(base, m + 8), cz = LoadFloat(base, m + 12);
    const float jx = LoadFloat(base, g.jo + 48), jy = LoadFloat(base, g.jo + 52), jz = LoadFloat(base, g.jo + 56);
    const float vx = LoadFloat(base, g.mgr + 0x3B4), vy = LoadFloat(base, g.mgr + 0x3B8), vz = LoadFloat(base, g.mgr + 0x3BC);
    const float dx = cx - jx, dy = cy - jy, dz = cz - jz;
    REXLOG_INFO("pdz_counterop: rare spectate: {}: his chase camera {:08X} at ({:.0f}, {:.0f}, {:.0f}), looking at "
                "({:.0f}, {:.0f}, {:.0f}); {:.1f} m from the agent's origin, {:.1f} m above it; its target {:08X} ({}); "
                "the view he got (manager +0x3B4) ({:.0f}, {:.0f}, {:.0f}) {}; his manager's updates: Rare {}, ours {}; "
                "re-targets {}, pushes {}",
                what, m, cx, cy, cz, LoadFloat(base, m + 16), LoadFloat(base, m + 20), LoadFloat(base, m + 24),
                std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f, dy / 100.0f, Load32(base, m + kTargetPtr),
                Load32(base, m + kTargetPtr) == g.jo ? "the agent" : "NOT the agent", vx, vy, vz,
                std::fabs(vx - cx) + std::fabs(vy - cy) + std::fabs(vz - cz) < 1.0f ? "= the camera" : "(not the camera yet)",
                g.rare, g.ours, g.retargets, g.pushes);
    // Mon 23 Nov (co19, diagnostic): the chase mode's own values, every 5th line.
    static uint32_t n = 0;
    if (n++ % 20 == 0 && n < 400 && pdz::guestmem::Ok(base, m, 0x1E0))
    {
      std::string f;
      for (uint32_t o = 0x100; o < 0x1D8; o += 4)
      {
        const float v = LoadFloat(base, m + o);
        f += std::isfinite(v) && std::fabs(v) < 1.0e7f && (std::fabs(v) > 1.0e-6f || v == 0.0f)
                 ? fmt::format(" {:X}:{:.2f}", o, v)
                 : fmt::format(" {:X}:#{:08X}", o, Load32(base, m + o));
      }
      REXLOG_INFO("pdz_counterop: rare spectate: mode {:08X} fields:{}", m, f);
    }
  }
} // namespace

bool PdzCounterOpRareSpectateActive() { return g.on; }
bool PdzCounterOpWaitCameraAtOnce() { return REXCVAR_GET(pdz_counterop_wait_camera_at_once); }

// Off: pop Rare's chase mode (if it is still the top of his stack) and forget it. Before he gets a body (ShowP2), when
// he starts to drive a spiderbot or ride, or when the wait ends.
void PdzCounterOpRareSpectateOff(PPCContext &ctx, uint8_t *base, const char *why)
{
  if (!g.on)
  {
    return;
  }
  g.on = false;
  const bool top = Ptr(g.mgr) && Top(base, g.mgr) == g.mode;
  if (top)
  {
    Call(ctx, base, sub_8220CE08, g.mgr, 1, 0);
  }
  REXLOG_INFO("pdz_counterop: rare spectate: OFF ({}): {}; his camera stack now {}", why,
              top ? "Rare's chase mode popped" : "the chase mode was no longer the top: left to Rare",
              Ptr(g.mgr) ? StackText(base, g.mgr) : std::string("-"));
}

void PdzCounterOpRareSpectateReset()
{
  const uint32_t scratch = g.scratch;
  g = State{};
  g.scratch = scratch;
}

// Every Counter-Op tick. want = player 2 waits (or is out for good) with nothing else driving his view; cutscene =
// a cutscene is playing (nothing is switched then). Returns true while Rare's chase camera is his view.
bool PdzCounterOpRareSpectateTick(PPCContext &ctx, uint8_t *base, bool want, bool cutscene, uint32_t jo, uint32_t p2c)
{
  if (!REXCVAR_GET(pdz_counterop_rare_spectate) || !want)
  {
    PdzCounterOpRareSpectateOff(ctx, base, !want ? "he has a view of his own again" : "pdz_counterop_rare_spectate off");
    return false;
  }
  if (cutscene || !Ptr(jo) || !Ptr(p2c))
  {
    return g.on;
  }
  const int64_t now = NowMs();
  const uint32_t mgr = Load32(base, p2c + kManagerOf);
  if (g.on)
  {
    // Still ours? (a spawn, a checkpoint restart or a cutscene may have replaced his stack or the mode)
    if (mgr != g.mgr || p2c != g.p2c || Top(base, mgr) != g.mode)
    {
      ++g.lost;
      REXLOG_INFO("pdz_counterop: rare spectate: the chase mode is no longer the top of his camera stack ({}; manager "
                  "{:08X} -> {:08X}): pushed again",
                  Ptr(mgr) ? StackText(base, mgr) : std::string("no manager"), g.mgr, mgr);
      g.on = false;
    }
    else
    {
      g.jo = jo;
      g.jo_mgr = Load32(base, jo + kManagerOf);
      if (Load32(base, g.mode + kTargetPtr) != jo)
      {
        ++g.retargets;
        Call(ctx, base, sub_82218500, g.mode, jo, g.scratch);
        REXLOG_INFO("pdz_counterop: rare spectate: the chase camera's target was {:08X}: set to the agent {:08X} again "
                    "(re-target #{})",
                    Load32(base, g.mode + kTargetPtr), jo, g.retargets);
      }
      // Behind her: Rare sets the yaw window (+0x12C / +0x130) once, at the switch; ours is pushed at the level start
      // before the opening turns her (her yaw 0 then: co_jungle_spec1 / 2 showed her face). The window follows her
      // view yaw every tick, so the chase camera stays behind her as she turns (the open-space orbit still steers it
      // clear of walls).
      const float view_deg = LoadFloat(base, jo + 0x1670);
      if (std::isfinite(view_deg) && std::fabs(view_deg) <= 720.0f)
      {
        const float yaw = view_deg * 3.14159265f / 180.0f;
        StoreFloat(base, g.mode + 0x12C, yaw);
        StoreFloat(base, g.mode + 0x130, yaw);
      }
      if (now - g.log_at >= 1000)
      {
        g.log_at = now;
        LogState(base, fmt::format("following the agent (her view yaw {:.1f} deg)", view_deg).c_str());
      }
      else if (g.first_ticks < 6)
      {
        // Mon 23 Nov night: the first ticks after a push, one line each (where the camera starts).
        LogState(base, fmt::format("tick {} after the push (eased distance now {:.0f} cm, at the push {:.0f} cm; "
                                   "pdz_counterop_wait_camera_at_once {})",
                                   g.first_ticks + 1, LoadFloat(base, g.mode + 0x1A4), g.eased_was,
                                   REXCVAR_GET(pdz_counterop_wait_camera_at_once) ? "on" : "off")
                             .c_str());
      }
      ++g.first_ticks;
      return true;
    }
  }
  if (now < g.retry_at)
  {
    return false;
  }
  g.retry_at = now + 1000;
  if (!Ptr(mgr))
  {
    if (g.fail_lines++ < 6)
    {
      REXLOG_INFO("pdz_counterop: rare spectate: player 2 has no camera manager yet ([char + 0x19A4] = {:08X}): our "
                  "stand-in view meanwhile",
                  mgr);
    }
    return false;
  }
  const std::string before = StackText(base, mgr);
  std::string why;
  const uint32_t m = Push(ctx, base, mgr, jo, p2c, why);
  if (!Ptr(m) || Load32(base, m) != kMode1Vtable)
  {
    if (g.fail_lines++ < 6)
    {
      REXLOG_INFO("pdz_counterop: rare spectate: Rare's chase camera could not be put on his manager {:08X}: {}; our "
                  "stand-in view meanwhile",
                  mgr, why);
    }
    return false;
  }
  g.on = true;
  g.mgr = mgr, g.mode = m, g.jo = jo, g.p2c = p2c, g.jo_mgr = Load32(base, jo + kManagerOf);
  g.rare_since = 1; // (no extra update until Rare's update of hers has been seen once)
  if (REXCVAR_GET(pdz_counterop_wait_camera_at_once))
  {
    g.rare_since = 0; // Mon 23 Nov night: its first update this frame, right after the agent's (the camera task runs after us)
  }
  g.first_ticks = 0;
  ++g.pushes;
  g.log_at = now;
  REXLOG_INFO("pdz_counterop: rare spectate: ON: Rare's spectator chase camera (mode 1, {:08X}) pushed on player 2's "
              "camera manager {:08X} with the agent {:08X} as its target, Rare's DarkOps values ({}), the open-space "
              "orbit on, the spectate callback 825ADD28; his stack {} -> {}",
              m, mgr, jo, why, before, StackText(base, mgr));
  return true;
}

// Mon 23 Nov (co19, pdz_counterop_wait_camera_cm): Rare's spectate callback, for player 2's wait camera only: the two
// distances it has just written are raised to the far view's (never lowered: a target on something bigger keeps
// Rare's own larger numbers), and the eased distance's goal with them, as the callback itself does.
REX_HOOK_RAW(sub_825ADD28)
{
  const uint32_t mode = ctx.r3.u32;
  __imp__sub_825ADD28(ctx, base);
  const double least = REXCVAR_GET(pdz_counterop_wait_camera_cm);
  if (!g.on || mode != g.mode || least <= 0.0 || !pdz::guestmem::Ok(base, mode, 0x1B0) ||
      Load32(base, mode) != kMode1Vtable)
  {
    return;
  }
  const float least_cm = float(least), wanted_cm = least_cm * 1.2f;
  const float was_least = LoadFloat(base, mode + 0x104), was_wanted = LoadFloat(base, mode + 0x108);
  if (!(was_least >= least_cm))
  {
    StoreFloat(base, mode + 0x104, least_cm);
  }
  if (!(was_wanted >= wanted_cm))
  {
    StoreFloat(base, mode + 0x108, wanted_cm);
    PPCContext saved = ctx;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = mode + 0x1A4;
      frame.ctx.f1.f64 = wanted_cm;
      sub_82276B58(frame.ctx, base);
    }
    ctx = saved; // (the callback's own results are left as it returned them)
    static uint32_t lines = 0;
    if (lines++ < 4)
    {
      REXLOG_INFO("pdz_counterop: rare spectate: wait camera distance: Rare's callback gave {:.0f} / {:.0f} cm (a target "
                  "on foot): raised to {:.0f} / {:.0f} cm, the view of a target on a vehicle "
                  "(pdz_counterop_wait_camera_cm)",
                  was_least, was_wanted, least_cm, wanted_cm);
    }
  }
}

// The camera manager update: counts Rare's updates of his manager; right after the agent's, his is updated once when
// Rare did not do it since her last one (CameraControlUpdate may skip a manager whose player is out of the world).
namespace pdz::mountlog
{
  void OnCameraUpdate(const uint8_t *base, uint32_t manager, double step); // Tue 24 Nov (pdz_mount_log)
}
REX_HOOK_RAW(sub_8220C0D8)
{
  const uint32_t mgr = ctx.r3.u32;
  const double step = ctx.f1.f64;
  if (g.on && !g.in_extra && mgr == g.mgr)
  {
    ++g.rare_since;
    ++g.rare;
  }
  __imp__sub_8220C0D8(ctx, base);
  pdz::mountlog::OnCameraUpdate(base, mgr, step);
  if (g.on && !g.in_extra && mgr == g.jo_mgr && Ptr(g.mgr))
  {
    // Sun 22 Nov night (audit): only while the manager is still his and our chase mode is still its top (a reload can
    // put the agent's new manager at the old address while his old one is gone).
    const bool still_his = pdz::guestmem::Ok(base, g.p2c, kManagerOf + 4) && Load32(base, g.p2c + kManagerOf) == g.mgr &&
                           pdz::guestmem::Ok(base, g.mgr, kStackTop + 4) && Top(base, g.mgr) == g.mode &&
                           pdz::guestmem::Ok(base, g.mode, 4) && Load32(base, g.mode) == kMode1Vtable;
    if (g.rare_since == 0 && still_his)
    {
      g.in_extra = true;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = g.mgr;
        frame.ctx.f1.f64 = step;
        __imp__sub_8220C0D8(frame.ctx, base);
      }
      g.in_extra = false;
      ++g.ours;
    }
    g.rare_since = 0;
  }
}
