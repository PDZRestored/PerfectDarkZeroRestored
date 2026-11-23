// Counter-Operative round 13 (G2): vehicles and vehicle-mounted guns are closed to player 2. Built only with PDZ_COUNTEROP.
//
// The owner's rule: player 2 rides no vehicle and uses no gun mounted on a vehicle (exceptions are registered by the
// level helpers through PdzCounterOpVehicleAllow, counter_op_rules13.h); a STATIONARY turret stays open to him; he
// cannot destroy a vehicle. What Rare does, and where each route is closed (TU3 code):
//   - RIDING. A human's Use on a vehicle prop (vtable 820CA9E0: motorbike, hovercraft, jetpack; +0x364 its
//     controller) is the prop's virtual +0x94 = 825D2E90(prop, user). In solo / co-op it calls Rare's mount
//     8266A8E8(controller, character, seat, flag) from three places (return addresses 825D2F64, 825D3098, 825D30CC:
//     the seat it picked, or seats 0..3 in turn). The other callers of the mount are not a player's Use: the AI mount
//     task 821B1490, the seat-1 helper 82497848, the level scripts' 'put in seat' 824CFDE8, and this mod's own
//     jetpack takeover (counter_op_jetpack.cpp). HERE: the mount is refused (answer 0, Rare's own 'cannot enter') for
//     player 2 when it is asked from one of the three Use addresses and the vehicle is not on the exception list.
//     Any other mount of his passes, and a vehicle he was seated on that way (his jetpack guard's jetpack) is put on
//     the exception list, so his own Use can remount it after he lands and gets off. Getting off is not touched
//     (8266ADE0 / the Use's own detach 82519288).
//   - GUNS. A mounted gun is Rare's hardpoint turret (class 0x2E, vtable 820C5BE0, constructor 8258BAA8; +0x5C4 the
//     gunner, +0x500 its state, +0x5C its parent). Its Use is virtual +0x94 = 8258FA50(turret, user): humans only,
//     then the 'may he use it' test 82590F10(turret, user) and the mount 825904F0(turret, user, 0). Its prompt
//     (virtual +0x8C = 8258FAE8) asks the same 82590F10 first. The gunner seat of a hovercraft IS such a turret, a
//     child of the vehicle prop (turret +0x5C = the vehicle; the hovercraft's seat test 826789A8-826789BC walks the
//     vehicle's children for it and asks 82590F10 too). HERE: 82590F10 answers 0 for player 2 when the turret's
//     parent is a vehicle prop and neither the turret, the vehicle nor its controller is on the exception list: no
//     prompt, no mount, and the hovercraft's gunner seat is not offered. A turret with no vehicle parent (the fixed
//     M60 / plasma / rocket emplacements) is left to him. NOT covered: a turret on something that is not Rare's
//     vehicle class (a dropship's gun, if a human can reach one at all): it counts as stationary; its first use by
//     him is logged with the parent's vtable so a hand test shows it.
//   - HIS TURRET SHOTS. Rare names a turret's shot by the turret (the near-miss resolve 8252CA0C knows class 0x2E:
//     the gunner +0x5C4), and counter_op_unheard.cpp's IsHis has known that since round 10, so the friendly-fire rule
//     (round 8), the allies rule (round 13 G1), the prop rule (round 11 / 12) and the vehicle rule below all take a
//     shot of a turret he mans as his. Round 13 adds the turret's own gun (a weapon whose owner is the turret) to
//     the same test, in case a path names the gun instead.
//   - DAMAGE TO VEHICLES. All four damage handlers of a vehicle prop (virtual +0xB4 = 825D3B08 bullet, +0xC0 =
//     825D42B0 blow, +0xC4 = 825D3EB0 blast, +0xDC = 825D41A0) first ask 825D54F8(vehicle, attacker) (return
//     addresses 825D3B30, 825D42D4, 825D3EEC, 825D41CC; a fifth caller, 825D377C, is not damage): Rare's
//     'does this damage count' for a vehicle (multiplayer: a team mate's fire answers 0; the bullet handler then
//     only plays the impact). HERE: it answers 0 when the attacker is player 2 or a thing of his. As a second net
//     the prop health funnel 825EFA40 (counter_op_unheard.cpp [6]) drops his damage to a vehicle prop and to a
//     turret mounted on one. An exception from the list does NOT lift this: he never damages a vehicle.
//   - PROMPTS. counter_op_hud.cpp's prompt hook (825AAB48) asks PdzCounterOpVehiclePromptDenied: a prompt offered
//     to player 2 by a closed vehicle (or a closed vehicle gun) is not shown. Not for the vehicle he is riding.
#include "counter_op_rules13.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"

uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();
bool PdzCounterOpIsHis(const uint8_t *base, uint32_t object);              // counter_op_unheard.cpp
uint32_t PdzCounterOpRules13OwnerOf(const uint8_t *base, uint32_t object); // counter_op_allies.cpp

REXCVAR_DEFINE_BOOL(pdz_counterop_p2_no_vehicles, true, "Gameplay",
                    "Counter-Operative (round 13, G2, default on): player 2's own Use mounts no vehicle (motorbike, hovercraft, a "
                    "free jetpack) and no gun mounted on a vehicle, and his shots, blows and blasts do no damage to a "
                    "vehicle. A stationary turret stays open to him. The jetpack of a jetpack guard he took over is "
                    "not affected. Off = round 12 (Rare's Use works for him on everything)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_p2_use_vehicle, 0, "Testing",
                     "TESTING TOOL (counter-op, round 13): each time this goes up by one, the vehicles and mounted "
                     "guns in the world are counted and, for the vehicle and the gun nearest to player 2, Rare's own "
                     "questions are asked for him and logged: the vehicle's 'does his damage count' (825D54F8), "
                     "Rare's Use of the vehicle (825D2E90, which asks the mount) and the gun's 'may he use it' "
                     "(82590F10). He is put 2.5 m from that vehicle first (a different side each time). With "
                     "pdz_counterop_p2_no_vehicles off the Use seats him")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_8266A8E8); // Rare's mount (controller, character, seat, flag) -> bool
REX_EXTERN(__imp__sub_82590F10); // a hardpoint turret's 'may he use it' (turret, user) -> bool
REX_EXTERN(__imp__sub_825D54F8); // a vehicle's 'does this damage count' (vehicle, attacker) -> bool
REX_EXTERN(sub_825D2E90);        // a vehicle prop's Use (prop, user)
REX_EXTERN(sub_825AD148);        // move a character (character, &position, 0)

namespace
{
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t L32(const uint8_t *b, uint32_t a)
  {
    const uint8_t *p = b + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LF(const uint8_t *b, uint32_t a)
  {
    const uint32_t v = L32(b, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  void SF(uint8_t *b, uint32_t a, float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    b[a] = uint8_t(v >> 24);
    b[a + 1] = uint8_t(v >> 16);
    b[a + 2] = uint8_t(v >> 8);
    b[a + 3] = uint8_t(v);
  }
  constexpr uint32_t kVehicleVtable = 0x820CA9E0; // vehicle prop (825D18A0)
  constexpr uint32_t kTurretClass = 0x2E;         // hardpoint turret (8258BAA8: class 0x2E, vtable 820C5BE0)
  constexpr uint32_t kClass = 0x0C, kParent = 0x5C, kPosition = 0x30;
  constexpr uint32_t kPropController = 0x364, kCtrlProp = 0x0C, kCtrlType = 0x14, kCtrlState = 0x1EC;
  constexpr uint32_t kSeatState = 0x58; // seat 0: 0 empty, 1 getting in, 3 seated
  constexpr uint32_t kTurretGunner = 0x5C4, kTurretState = 0x500;
  constexpr uint32_t kUseReturn[3] = {0x825D2F64, 0x825D3098, 0x825D30CC}; // the mount's callers in Rare's Use
  constexpr uint32_t kWorld = 0x82D6BFD8 + 24, kWorldFirst = 43212, kWorldEnd = 43216; // nodes: +4 object, +12 next

  constexpr int kAllowMax = 32;
  std::atomic<uint32_t> g_allowed[kAllowMax] = {};

  bool On() { return REXCVAR_GET(pdz_counterop_p2_no_vehicles); }
  const char *TypeName(uint32_t type)
  {
    return type == 1 ? "motorbike" : type == 2 || type == 4 ? "hovercraft" : type == 3 ? "jetpack" : "vehicle";
  }
  bool IsVehicleProp(const uint8_t *b, uint32_t o) { return Ptr(o) && L32(b, o) == kVehicleVtable; }
  // A controller (what the mount sees), by the exception list: itself or its prop.
  bool ControllerAllowed(const uint8_t *b, uint32_t ctrl)
  {
    if (PdzCounterOpVehicleAllowed(ctrl))
    {
      return true;
    }
    const uint32_t prop = Ptr(ctrl) ? L32(b, ctrl + kCtrlProp) : 0;
    return PdzCounterOpVehicleAllowed(prop);
  }
  bool PropAllowed(const uint8_t *b, uint32_t prop)
  {
    return PdzCounterOpVehicleAllowed(prop) || (Ptr(prop) && PdzCounterOpVehicleAllowed(L32(b, prop + kPropController)));
  }
  // The vehicle prop a turret is mounted on, or 0 (a stationary turret, or a parent that is not Rare's vehicle).
  uint32_t TurretVehicle(const uint8_t *b, uint32_t turret)
  {
    const uint32_t parent = L32(b, turret + kParent);
    return IsVehicleProp(b, parent) ? parent : 0;
  }
  bool IsTurret(const uint8_t *b, uint32_t o) { return Ptr(o) && L32(b, o + kClass) == kTurretClass; }
  // A turret on a vehicle that is closed to him.
  bool TurretClosed(const uint8_t *b, uint32_t turret)
  {
    const uint32_t vehicle = TurretVehicle(b, turret);
    return vehicle && !PdzCounterOpVehicleAllowed(turret) && !PropAllowed(b, vehicle);
  }
  bool His(const uint8_t *b, uint32_t o, uint32_t p2)
  {
    return o == p2 || PdzCounterOpIsHis(b, o) || PdzCounterOpRules13OwnerOf(b, o) == p2;
  }

  uint64_t g_mounts_refused = 0, g_guns_refused = 0, g_damage_refused = 0, g_prompts_refused = 0;
} // namespace

void PdzCounterOpVehicleAllow(uint32_t guest_object, bool allow)
{
  if (!guest_object)
  {
    return;
  }
  int free_slot = -1;
  for (int i = 0; i < kAllowMax; ++i)
  {
    const uint32_t v = g_allowed[i].load(std::memory_order_relaxed);
    if (v == guest_object)
    {
      if (!allow)
      {
        g_allowed[i].store(0, std::memory_order_relaxed);
        REXLOG_INFO("pdz_counterop: vehicles: the exception for {:08X} is taken back: closed to player 2 again",
                    guest_object);
      }
      return;
    }
    if (!v && free_slot < 0)
    {
      free_slot = i;
    }
  }
  if (!allow)
  {
    return;
  }
  if (free_slot < 0)
  {
    REXLOG_INFO("pdz_counterop: vehicles: no room for the exception {:08X} ({} entries): it stays closed to player 2",
                guest_object, kAllowMax);
    return;
  }
  g_allowed[free_slot].store(guest_object, std::memory_order_relaxed);
  REXLOG_INFO("pdz_counterop: vehicles: exception registered: player 2 may use {:08X} (a vehicle prop, its "
              "controller or a mounted gun; until the next level boundary)",
              guest_object);
}

bool PdzCounterOpVehicleAllowed(uint32_t guest_object)
{
  if (!guest_object)
  {
    return false;
  }
  for (int i = 0; i < kAllowMax; ++i)
  {
    if (g_allowed[i].load(std::memory_order_relaxed) == guest_object)
    {
      return true;
    }
  }
  return false;
}

void PdzCounterOpAlliesReset(); // counter_op_allies.cpp (Sun 22 Nov night, audit)
void PdzCounterOpRules13Reset()
{
  for (int i = 0; i < kAllowMax; ++i)
  {
    g_allowed[i].store(0, std::memory_order_relaxed);
  }
  PdzCounterOpAlliesReset(); // Sun 22 Nov night (audit): the allies' holders too
}

// [1] Rare's mount (controller r3, character r4, seat r5, flag r6; returns 1 when seated / getting in).
REX_HOOK_RAW(sub_8266A8E8)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || ctx.r4.u32 != p2)
  {
    __imp__sub_8266A8E8(ctx, base);
    return;
  }
  const uint32_t ctrl = ctx.r3.u32, seat = ctx.r5.u32, from = uint32_t(ctx.lr);
  const bool from_use = from == kUseReturn[0] || from == kUseReturn[1] || from == kUseReturn[2];
  const uint32_t prop = Ptr(ctrl) ? L32(base, ctrl + kCtrlProp) : 0;
  const uint32_t type = Ptr(ctrl) ? L32(base, ctrl + kCtrlType) : 0;
  if (from_use && On() && !ControllerAllowed(base, ctrl))
  {
    // Rare's Use tries seats 0..3 in turn: one line per Use (seat 0 or the picked seat), then one in 50.
    static uint32_t last_ctrl = 0;
    ++g_mounts_refused;
    if (ctrl != last_ctrl || g_mounts_refused <= 6 || g_mounts_refused % 50 == 0)
    {
      last_ctrl = ctrl;
      REXLOG_INFO("pdz_counterop: vehicles: player 2's Use of the {} {:08X} (controller {:08X}, seat {}) is refused "
                  "(#{}; Rare's mount 8266A8E8 answered 0, asked from {:08X}): vehicles are closed to him",
                  TypeName(type), prop, ctrl, seat, g_mounts_refused, from);
    }
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8266A8E8(ctx, base);
  const bool ok = (ctx.r3.u32 & 0xFF) != 0;
  if (ok && !from_use && Ptr(ctrl) && !ControllerAllowed(base, ctrl))
  {
    // Not his own Use (the mod's jetpack takeover, a level script): the vehicle is his from now on.
    REXLOG_INFO("pdz_counterop: vehicles: player 2 was seated on the {} {:08X} (controller {:08X}, seat {}) by "
                "something that is not his own Use (asked from {:08X}): this vehicle stays open to him",
                TypeName(type), prop, ctrl, seat, from);
    PdzCounterOpVehicleAllow(ctrl, true);
  }
}

// [2] A hardpoint turret's 'may he use it' (turret r3, user r4; 1 = yes). Asked by the turret's Use (8258FAA0), its
// prompt (8258FB04) and the hovercraft's gunner seat test (826789BC).
REX_HOOK_RAW(sub_82590F10)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (p2 && ctx.r4.u32 == p2 && Ptr(ctx.r3.u32))
  {
    const uint32_t turret = ctx.r3.u32;
    static uint32_t last_turret = 0; // asked every frame he stands at one: one line per turret
    const bool closed = On() && TurretClosed(base, turret);
    if (turret != last_turret)
    {
      last_turret = turret;
      const uint32_t parent = L32(base, turret + kParent);
      REXLOG_INFO("pdz_counterop: vehicles: mounted gun {:08X} (vtable {:08X}; its parent {:08X}, vtable {:08X}) and "
                  "player 2: {} (Rare's 'may he use it' 82590F10, asked from {:08X})",
                  turret, L32(base, turret), parent, Ptr(parent) ? L32(base, parent) : 0,
                  closed                           ? "a gun on a vehicle: closed to him (answer 0: no prompt, no mount)"
                  : TurretVehicle(base, turret)    ? "a gun on a vehicle that is on the exception list (or the rule is off): Rare decides"
                                                   : "a stationary gun: open to him, Rare decides (its shots count on the agent only)",
                  uint32_t(ctx.lr));
    }
    if (closed)
    {
      ++g_guns_refused;
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_82590F10(ctx, base);
}

// [3] A vehicle's 'does this damage count' (vehicle r3, attacker r4; 1 = yes).
REX_HOOK_RAW(sub_825D54F8)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  // Only for the four damage handlers (and the test tool, lr 0). The fifth caller (825D377C in 825D32C8, the vehicle's
  // 'what can he do here' score, asked every frame he stands at one: River run 3) is left to Rare.
  const uint32_t from = uint32_t(ctx.lr);
  const bool damage = from == 0x825D3B30 || from == 0x825D3EEC || from == 0x825D41CC || from == 0x825D42D4 || from == 0;
  if (p2 && damage && On() && Ptr(ctx.r4.u32) && His(base, ctx.r4.u32, p2))
  {
    if (++g_damage_refused <= 8 || g_damage_refused % 200 == 0)
    {
      const uint32_t vehicle = ctx.r3.u32;
      const uint32_t ctrl = Ptr(vehicle) ? L32(base, vehicle + kPropController) : 0;
      REXLOG_INFO("pdz_counterop: vehicles: player 2's damage to the {} {:08X} does not count (#{}; the source "
                  "{:08X}; Rare's vehicle test 825D54F8 answered 0, asked from {:08X}): he cannot destroy a vehicle",
                  TypeName(Ptr(ctrl) ? L32(base, ctrl + kCtrlType) : 0), vehicle, g_damage_refused, ctx.r4.u32,
                  uint32_t(ctx.lr));
    }
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_825D54F8(ctx, base);
}

// counter_op_unheard.cpp's prop health funnel (825EFA40), already known to be player 2's doing: a vehicle prop, or a
// turret mounted on one, takes no damage from him.
bool PdzCounterOpVehicleNoDamage(const uint8_t *base, uint32_t prop)
{
  if (!On() || !Ptr(prop))
  {
    return false;
  }
  if (!IsVehicleProp(base, prop) && !(IsTurret(base, prop) && TurretVehicle(base, prop)))
  {
    return false;
  }
  if (++g_damage_refused <= 8 || g_damage_refused % 200 == 0)
  {
    REXLOG_INFO("pdz_counterop: vehicles: player 2's damage to {:08X} (vtable {:08X}, class {}: a vehicle or a gun "
                "mounted on one) is dropped at the prop health funnel 825EFA40 (#{})",
                prop, L32(base, prop), L32(base, prop + kClass), g_damage_refused);
  }
  return true;
}

// counter_op_hud.cpp's prompt hook (825AAB48), for a prompt offered to player 2 by `prop`.
bool PdzCounterOpVehiclePromptDenied(const uint8_t *base, uint32_t prop)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || !On() || !Ptr(prop) || L32(base, p2 + kParent) == prop)
  {
    return false; // the vehicle he rides keeps its prompts (getting off)
  }
  const bool closed = IsVehicleProp(base, prop) ? !PropAllowed(base, prop)
                                                : IsTurret(base, prop) && TurretClosed(base, prop);
  g_prompts_refused += closed;
  return closed;
}

// ---- TESTING TOOL (pdz_counterop_test_p2_use_vehicle) ----------------------------------------------------------------
namespace
{
  uint32_t Call2(PPCContext &ctx, uint8_t *base, PPCFunc *fn, uint32_t r3, uint32_t r4, uint32_t lr = 0)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = r3;
    frame.ctx.r4.u64 = r4;
    frame.ctx.lr = lr;
    fn(frame.ctx, base);
    return frame.ctx.r3.u32;
  }
} // namespace

void PdzCounterOpVehiclesTestTick(PPCContext &ctx, uint8_t *base, bool playing)
{
  static int32_t done = 0;
  const int32_t want = REXCVAR_GET(pdz_counterop_test_p2_use_vehicle);
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (want <= done || !playing || !p2)
  {
    return;
  }
  ++done;
  const float px = LF(base, p2 + kPosition), py = LF(base, p2 + kPosition + 4), pz = LF(base, p2 + kPosition + 8);
  uint32_t vehicle = 0, turret = 0, turret_on_vehicle = 0;
  float vehicle_d = 1e30f, turret_d = 1e30f, tov_d = 1e30f;
  int vehicles = 0, turrets = 0, turrets_on_vehicles = 0, types[5] = {};
  const uint32_t world = L32(base, kWorld);
  const uint32_t end = Ptr(world) ? L32(base, world + kWorldEnd) : 0;
  uint32_t node = Ptr(world) ? L32(base, world + kWorldFirst) : 0;
  for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = L32(base, node + 12))
  {
    const uint32_t o = L32(base, node + 4);
    if (!Ptr(o))
    {
      continue;
    }
    const bool is_vehicle = IsVehicleProp(base, o), is_turret = !is_vehicle && IsTurret(base, o);
    if (!is_vehicle && !is_turret)
    {
      continue;
    }
    const float dx = LF(base, o + kPosition) - px, dy = LF(base, o + kPosition + 4) - py,
                dz = LF(base, o + kPosition + 8) - pz;
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (is_vehicle)
    {
      ++vehicles;
      const uint32_t ctrl = L32(base, o + kPropController);
      const uint32_t type = Ptr(ctrl) ? L32(base, ctrl + kCtrlType) : 0;
      ++types[type < 5 ? type : 0];
      // The vehicle to try: a free one (seat 0 empty) with a controller, the nearest.
      if (Ptr(ctrl) && L32(base, ctrl + kSeatState) == 0 && d < vehicle_d)
      {
        vehicle_d = d;
        vehicle = o;
      }
    }
    else
    {
      ++turrets;
      if (TurretVehicle(base, o))
      {
        ++turrets_on_vehicles;
        if (d < tov_d)
        {
          tov_d = d;
          turret_on_vehicle = o;
        }
      }
      else if (d < turret_d)
      {
        turret_d = d;
        turret = o;
      }
    }
  }
  REXLOG_INFO("pdz_counterop: TEST TOOL (vehicles) #{}: the world list holds {} vehicle prop(s) ({} motorbike, {} "
              "hovercraft, {} jetpack, {} other) and {} mounted gun(s) ({} of them on a vehicle); "
              "pdz_counterop_p2_no_vehicles is {}",
              done, vehicles, types[1], types[2] + types[4], types[3], types[0], turrets, turrets_on_vehicles,
              On() ? "on" : "off");
  if (vehicle)
  {
    const uint32_t ctrl = L32(base, vehicle + kPropController);
    const uint32_t counts = Call2(ctx, base, sub_825D54F8, vehicle, p2) & 0xFF;
    const uint32_t parent_before = L32(base, p2 + kParent);
    // Run 2 (River): from 130 m Rare's Use picks no seat and never asks the mount. He is put 2.5 m from the vehicle
    // first, on a different side each time (+x, -x, +z, -z).
    {
      static const float kSide[4][2] = {{250.0f, 0.0f}, {-250.0f, 0.0f}, {0.0f, 250.0f}, {0.0f, -250.0f}};
      const int side = (done - 1) & 3;
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u32 -= 0x100; // our own scratch between Rare's frame and the callee's
      const uint32_t pos = frame.ctx.r1.u32 + 0xC0;
      SF(base, pos, LF(base, vehicle + kPosition) + kSide[side][0]);
      SF(base, pos + 4, LF(base, vehicle + kPosition + 4) + 30.0f);
      SF(base, pos + 8, LF(base, vehicle + kPosition + 8) + kSide[side][1]);
      frame.ctx.r3.u64 = p2;
      frame.ctx.r4.u64 = pos;
      frame.ctx.r5.u64 = 0;
      sub_825AD148(frame.ctx, base);
      REXLOG_INFO("pdz_counterop: TEST TOOL (vehicles) #{}: player 2 put 2.5 m from the vehicle {:08X} at ({:.0f}, "
                  "{:.0f}, {:.0f}) (side {}); he is now at ({:.0f}, {:.0f}, {:.0f})",
                  done, vehicle, LF(base, vehicle + kPosition), LF(base, vehicle + kPosition + 4),
                  LF(base, vehicle + kPosition + 8), side, LF(base, p2 + kPosition), LF(base, p2 + kPosition + 4),
                  LF(base, p2 + kPosition + 8));
    }
    Call2(ctx, base, sub_825D2E90, vehicle, p2);
    REXLOG_INFO("pdz_counterop: TEST TOOL (vehicles) #{}: the nearest free vehicle is the {} {:08X} (controller "
                "{:08X}, vehicle state {}) {:.1f} m from player 2: 'does his damage count' (825D54F8) = {}; Rare's "
                "Use (825D2E90) for him: seat 0 state {} (0 = still empty), his parent {:08X} -> {:08X} ({})",
                done, TypeName(L32(base, ctrl + kCtrlType)), vehicle, ctrl, L32(base, ctrl + kCtrlState),
                vehicle_d / 100.0f, counts, L32(base, ctrl + kSeatState), parent_before, L32(base, p2 + kParent),
                L32(base, p2 + kParent) == vehicle ? "HE IS ON IT" : "he is not on it");
  }
  for (const uint32_t t : {turret_on_vehicle, turret})
  {
    if (!t)
    {
      continue;
    }
    const uint32_t parent = L32(base, t + kParent);
    const uint32_t may = Call2(ctx, base, sub_82590F10, t, p2) & 0xFF;
    REXLOG_INFO("pdz_counterop: TEST TOOL (vehicles) #{}: the nearest {} mounted gun {:08X} (parent {:08X}, vtable "
                "{:08X}; state {}, gunner {:08X}) {:.1f} m from player 2: 'may he use it' (82590F10) = {}; the prop "
                "health funnel would {} his damage to it",
                done, t == turret ? "stationary" : "vehicle-", t, parent, Ptr(parent) ? L32(base, parent) : 0,
                L32(base, t + kTurretState), L32(base, t + kTurretGunner), (t == turret ? turret_d : tov_d) / 100.0f,
                may, On() && TurretVehicle(base, t) ? "drop" : "leave to the prop rule");
  }
}
