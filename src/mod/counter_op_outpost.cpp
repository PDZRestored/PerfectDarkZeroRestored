// Counter-Operative round 13: Outpost's last fight (the gun on the hovercraft). Built only with PDZ_COUNTEROP.
// The study: research notes: OUTPOST_STUDY_Mon16Nov.md.
//
// What Rare does (Title Update 3 code and the level data):
//   - Outpost's level script: setup 82370658, update 82374378, message handler 82376848 (script r3, the message r8:
//     bytes +1 type, +2 argument, +3 flag), flag handler 8237B918, fight-start callback 8237DFF0. Script fields:
//     +0x404 its flag block, +0x55C..+0x8B4 the 107 placed guards, +0x904 Mai Hem (s32a), +0x90C her driver (s32b),
//     +0x910 her hovercraft (vehicle id 0x1A).
//   - Level message 8 / 22 (case 82378EFC; a trigger the agent walks through) starts the last fight: Rare DELETES
//     all 107 placed guards there (82378F58-823792E0: vtable +0 with 1), then the cutscene level12_mid plays, then
//     8237DFF0 wakes Mai Hem and her driver. The driver gets Rare's ride task for the hovercraft with a
//     drive-between-pads task (pads 0x981, 0xD11, 0x855, 0x833). MAI HEM HERSELF gets the ride task "man the gun of
//     this vehicle" (821E5BA0 set up by 821E6068, kind 2): the gunner of Rare's fight is the boss.
//   - The gun is a hardpoint turret (the class of the fixed guns; +0x500 state: 0 free, 3 in use; +0x5C0 / +0x5C4 the
//     gunner) that is a child of the hovercraft prop (child flag [[child +8] +4] bit 0x4000). An AI gunner mounts
//     with the turret's own MountStart 825904F0(turret, character, flag). A player mounts through the vehicle:
//     8266A8E8(controller, character, seat 1, flag) -> the hovercraft's CanEnter 826788F0 (turret free, 82590F10,
//     the seat's usable bytes +0x9C / +0x9D) -> its Enter 82674C18 -> 825904F0. (Enter answers 0 for seat 1 even when
//     the mount worked, so 8266A8E8 answers 0 too: the proof of a mount is the turret's gunner.)
//   - Rare closes both seats of her hovercraft to players during the fight (usable bytes +0x3D and +0x9D = 0,
//     8237D59C).
//   - Mai Hem's death sends level message 8 / 24 (her "Destroyed" trigger): the objective of the fight. Her driver's
//     death sends 8 / 49.
//
// What this file does (each behind its cvar; nothing runs outside a Counter-Op game on Outpost):
//   [1] pdz_counterop_outpost_fight_release: at message 8 / 22, before Rare's delete, player 2's current guard is
//       given back alive (not killed) and he leaves the world without a death: otherwise the guard he "is" is
//       deleted under him.
//   [2] pdz_counterop_outpost_turret: once Mai Hem sits at the gun, the normal takeover may pick her (and picks her
//       first). Inside the takeover: Rare's own emergency evacuation of the turret (82590E30) takes her off the gun,
//       she is hidden as every taken guard is, and Rare's mount for players (8266A8E8, seat 1) seats player 2 in the
//       same tick. The driver and the route stay Rare's. He cannot leave the seat (Rare's "does the gunner get
//       off now" 82591948 is answered no for him there); when his life ends he is taken off the gun first (the same evacuation), then the
//       usual end of a life runs: Mai Hem is restored where he sat and dies through Rare's kill, which sends 8 / 24
//       as her own death does.
#include "counter_op_rules13.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

bool PdzCounterOpRunning();     // counter_op.cpp
uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();
bool PdzCounterOpIsOf(const uint8_t *base, uint32_t object, uint32_t who); // counter_op_unheard.cpp: who, or a thing of his
// counter_op.cpp (round 13 f): player 2 gives up his body without a death; the normal pick runs again.
void PdzCounterOpReleaseBody(PPCContext &ctx, uint8_t *base, const char *why, bool at_once = false); // counter_op.cpp

// Sun 22 Nov (round 18, item 8): log 046 19:33:55-19:33:58, player 2 got Mai Hem 2.5 s after the cutscene (the normal
// wait before a pick); Mansion's boss has been his on the next tick since round 16 (pdz_counterop_mansion_instant).
REXCVAR_DEFINE_BOOL(pdz_counterop_outpost_instant, true, "Gameplay",
                    "Counter-Operative (Sun 22 Nov, default on, Outpost's last fight): once Mai Hem sits at her "
                    "hovercraft's gun, player 2 takes her place on the next frame instead of after the usual 2.5 s "
                    "wait. Off = the usual wait")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_outpost_turret, true, "Gameplay",
                    "Counter-Operative (round 13, default on, Outpost's last fight): once Mai Hem sits at the gun of her "
                    "hovercraft, player 2 takes her place at that gun (the game's own steps: she is taken off the gun "
                    "and hidden as every taken guard is, the game's mount for players seats him). The driver and the "
                    "hovercraft's route stay the game's. His shots count on the agent only. He is the boss then: when "
                    "the agent kills him at the gun Mai Hem dies through the game's own kill, which ends the fight as "
                    "her death always does. Off = he sits out the last fight")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_outpost_maihem_health, 200.0, "Gameplay",
                      "Counter-Operative (round 14), Outpost's last fight: player 2's health at the gun of Mai "
                      "Hem's hovercraft. 200 = what the level gives Mai Hem for this fight; 100 = a player's "
                      "(round 13)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_outpost_gun_damage_scale, 0.3, "Gameplay",
                      "Counter-Operative (round 14), Outpost's last fight: what the damage of player 2's gun on "
                      "the agent is multiplied by (health and armour). 1 = round 13b: a human holding his aim on "
                      "her kills her in 1.2 s; 0.3 (default, round 15: the owner's choice) = 3 health a hit; 0.1 = "
                      "about 12 s of held fire. The game's own Mai Hem "
                      "at that gun aims far more loosely still (measured Tue 17 Nov: one hit of 6 on a standing "
                      "agent in 88 s)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_outpost_fight_release, true, "Gameplay",
                    "Counter-Operative (round 13, default on, Outpost): when the last fight starts the game deletes every placed "
                    "guard of the level. The guard player 2 plays at that moment is given back alive first and he "
                    "leaves the world without a death (he then waits for a body). Off = round 12 (he goes on as a "
                    "guard the game has deleted)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_outpost_turret_stay, true, "Gameplay",
                    "Counter-Operative (round 13, Outpost): at the hovercraft's gun player 2 cannot get off (the "
                    "game's dismount request is refused for him there); he stays until his life ends. Off = he may "
                    "get off (the seat is closed to players by the level, so he could not get back on)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_outpost_log, false, "Testing",
                    "TESTING TOOL (counter-op, Outpost): once a second one line with Mai Hem, her driver, her "
                    "hovercraft's seats and the gun's state, from the first level message on")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_outpost_mount, 2, "Testing",
                     "TESTING TOOL (counter-op, Outpost): how player 2 is put at the gun. 2 (default, proven) = the "
                     "turret's own mount (825904F0); 0 = the vehicle's mount for players first (8266A8E8, seat 1: it "
                     "did not seat him in the first run), then the turret's own; 1 = the vehicle's only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_outpost_mount_flag, 1, "Testing",
                     "TESTING TOOL (counter-op, Outpost): the mount's flag: 1 = seated at once (what the game passes "
                     "in a two-player co-op game), 0 = with the mount animation")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_outpost_kill, 0, "Testing",
                     "TESTING TOOL (counter-op, Outpost): each time the value changes while player 2 sits at the "
                     "hovercraft's gun, his health is put at his floor, so his life ends as from a hit (the tool "
                     "pdz_counterop_test_kill_p2 moves him, which a seated gunner must not be)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_outpost_agent, 0, "Testing",
                     "TESTING TOOL (counter-op, Outpost): while player 2 sits at the hovercraft's gun the agent is "
                     "moved with the game's teleport (a seated gunner's own position is not a world position, so the "
                     "older tools miss him). 1 = she is kept 7 m from the hovercraft on a circle around it (20 "
                     "degrees a second), so a sweeping gun crosses her; 2 = she is put 7 m from it every 1.5 s and "
                     "her view is turned at the gunner every frame, so a script's trigger presses shoot him; 3 "
                     "(round 13b) = every tick she is put ON THE GUN'S BARREL LINE (the line through the gun's two "
                     "aim bones, the ones the game's own 825901A0 / 82590398 read the gun's yaw and pitch from), "
                     "5 m, 10 m and 20 m ahead of the muzzle in turn (and 5 m behind, in case the bones run the "
                     "other way), 3 s each, so a firing gun must hit her wherever it points; her health is logged "
                     "at every change")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_82376848); // Outpost's script: its message handler
REX_EXTERN(__imp__sub_825904F0); // turret: MountStart(turret, character, flag) -> bool
REX_EXTERN(__imp__sub_82591948); // turret: does the gunner get off now (turret) -> bool
REX_EXTERN(__imp__sub_8266A8E8); // vehicle mount, past round 13's vehicle gate (counter_op_vehicles.cpp hooks it)
REX_EXTERN(sub_82590E30);        // turret: emergency evacuation (turret): the gunner is off at once
REX_EXTERN(sub_825AD0E0);        // Rare's teleport (character, &position)
REX_EXTERN(sub_825AD3B0);        // set a character's view (character, f1 pitch (positive down), f2 yaw)
REX_EXTERN(sub_82413998);        // round 13b u: a model's bone position (model, bone, &out): leaf

namespace
{
  constexpr uint32_t kVehicleVtable = 0x820CA9E0, kHovercraftCtrlVtable = 0x820CF1F0;
  constexpr uint32_t kScriptFlags = 0x404, kScriptMaiHem = 0x904, kScriptDriver = 0x90C, kScriptCraft = 0x910;
  constexpr uint32_t kPosition = 48, kHealth = 120, kParent = 0x5C, kChildFirst = 0x58, kChildNext = 0x60;
  constexpr uint32_t kHidden = 6272;        // character: bit 0x10 = hidden by 825818E0
  constexpr uint32_t kParticipantOf = 6560; // a human's participant; its +980 = his health
  constexpr uint32_t kPropController = 0x364;
  constexpr uint32_t kCtrlState = 0x1EC, kCtrlAsked = 0x1F0;
  constexpr uint32_t kSeatBytes = 0x60, kSeatUsableA = 0x3C, kSeatUsableB = 0x3D, kSeatState = 0x58, kSeatOccupant = 0x60;
  constexpr uint32_t kTurretState = 0x500, kTurretGunner = 0x5C4, kTurretFiring = 1289;
  constexpr uint32_t kCheckpointByte = 0x82E426F8; // bit 2: the level was started from its checkpoint
  constexpr float kOwnFloor = 1.0f;                // counter_op.cpp's floor for player 2 (pdz_counterop_own_death)

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
  uint32_t Call(PPCContext &ctx, uint8_t *base, PPCFunc *fn, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0,
                uint32_t r6 = 0)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = r3;
    frame.ctx.r4.u64 = r4;
    frame.ctx.r5.u64 = r5;
    frame.ctx.r6.u64 = r6;
    fn(frame.ctx, base);
    return frame.ctx.r3.u32;
  }

  struct State
  {
    uint32_t script = 0;     // Outpost's script object (from its message handler)
    bool fight = false;      // message 8 / 22 went through: the last fight is on
    bool told_ready = false; // "Mai Hem sits at the gun" said, the pick nudged
    uint32_t boss = 0;       // Mai Hem's object once player 2 has taken her
    uint32_t turret = 0, craft = 0, ctrl = 0;
    bool pending = false;    // between Unseat and AfterSpawn of one takeover
    bool on_gun = false;     // player 2 sits at the gun
    uint8_t usable_a = 0, usable_b = 0; // the gun seat's usable bytes before the mount
    bool usable_saved = false;
    uint32_t last_turret_state = ~0u;
    uint8_t last_firing = 0;
    uint32_t fire_lines = 0, stay_lines = 0, remount_lines = 0, aim_lines = 0, hit_lines = 0, hurt_lines = 0;
    std::chrono::steady_clock::time_point next_front{};
    int32_t last_kill = 0;
    std::chrono::steady_clock::time_point next_log{};
    std::chrono::steady_clock::time_point seated_at{};
    // round 13b u: the barrel-line tool (pdz_counterop_test_outpost_agent 3) and the agent's health watch
    float agent_health = -1.0f;
    // round 14: his health at the gun given once; the hits on the agent in the fight (the measurement); tool 4
    bool health_given = false, tool4_placed = false;
    uint32_t agent_hit_lines = 0;
    uint32_t health_lines = 0, line_lines = 0;
    int line_phase = -1;
    std::chrono::steady_clock::time_point line_from{};
    // round 13b u (the owner's decision: he never leaves the gun while the fight runs): the put-back net and the
    // "the hovercraft keeps driving" log
    uint32_t reseat_lines = 0, drive_lines = 0;
    std::chrono::steady_clock::time_point next_drive{}, last_drive{};
    float drive_x = 0.0f, drive_z = 0.0f;
    bool drive_have = false;
  };
  State g;

  // The gun of a hovercraft: the child of its prop with the turret flag (the walk of 826741F0).
  uint32_t TurretOf(const uint8_t *base, uint32_t craft)
  {
    if (!Ptr(craft))
    {
      return 0;
    }
    uint32_t child = L32(base, craft + kChildFirst);
    // Sun 22 Nov night (audit): each child and its info read only where readable.
    for (int n = 0; pdz::guestmem::Ok(base, child, kChildNext + 4) && n < 64; ++n, child = L32(base, child + kChildNext))
    {
      const uint32_t info = L32(base, child + 8);
      if (pdz::guestmem::Ok(base, info, 8) && (L32(base, info + 4) & 0x4000u))
      {
        return child;
      }
    }
    return 0;
  }

  // Sun 22 Nov night (audit): the hovercraft noted at the takeover is still a readable vehicle and the noted gun is
  // still on it (a deleted or reused craft / gun is never mounted or written).
  bool CraftStillOurs(const uint8_t *base)
  {
    return pdz::guestmem::Ok(base, g.craft, kPropController + 4) && L32(base, g.craft) == kVehicleVtable;
  }
  bool GunStillOurs(const uint8_t *base)
  {
    return CraftStillOurs(base) && pdz::guestmem::Ok(base, g.turret, kTurretGunner + 4) &&
           L32(base, g.turret + kParent) == g.craft; // (the link BossAtGun took the craft from)
  }

  // Mai Hem (the script's +0x904) while she sits at the gun of her hovercraft, alive, with the gun in use.
  bool BossAtGun(const uint8_t *base, uint32_t c, uint32_t *turret_out = nullptr, uint32_t *craft_out = nullptr)
  {
    if (!g.fight || !Ptr(g.script) || !Ptr(c) || c != L32(base, g.script + kScriptMaiHem))
    {
      return false;
    }
    const uint32_t turret = L32(base, c + kParent);
    if (!Ptr(turret) || L32(base, turret + kTurretGunner) != c || L32(base, turret + kTurretState) != 3)
    {
      return false;
    }
    const uint32_t craft = L32(base, turret + kParent);
    if (!Ptr(craft) || L32(base, craft) != kVehicleVtable || !(LF(base, c + kHealth) > 0.0f) ||
        (L32(base, c + kHidden) & 0x10))
    {
      return false;
    }
    if (turret_out)
    {
      *turret_out = turret;
    }
    if (craft_out)
    {
      *craft_out = craft;
    }
    return true;
  }

  std::string SeatText(const uint8_t *base, uint32_t ctrl)
  {
    if (!Ptr(ctrl))
    {
      return "no controller";
    }
    return fmt::format("controller {:08X} (vtable {:08X}) state {} / asked {}; seat 0: state {}, occupant {:08X}, "
                       "usable {} / {}; seat 1: state {}, usable {} / {}",
                       ctrl, L32(base, ctrl), L32(base, ctrl + kCtrlState), L32(base, ctrl + kCtrlAsked),
                       L32(base, ctrl + kSeatState), L32(base, ctrl + kSeatOccupant), int(base[ctrl + kSeatUsableA]),
                       int(base[ctrl + kSeatUsableB]), L32(base, ctrl + kSeatBytes + kSeatState),
                       int(base[ctrl + kSeatBytes + kSeatUsableA]), int(base[ctrl + kSeatBytes + kSeatUsableB]));
  }

  std::string TurretText(const uint8_t *base, uint32_t turret)
  {
    if (!pdz::guestmem::Ok(base, turret, kTurretFiring + 1)) // Sun 22 Nov night (audit): Ok
    {
      return "no gun found";
    }
    return fmt::format("gun {:08X} (vtable {:08X}): state {}, gunner {:08X}, firing {}", turret, L32(base, turret),
                       L32(base, turret + kTurretState), L32(base, turret + kTurretGunner),
                       int(base[turret + kTurretFiring]));
  }

  void RestoreUsable(uint8_t *base)
  {
    // Sun 22 Nov night (audit): only into the controller of the noted hovercraft, still readable.
    if (g.usable_saved && Ptr(g.ctrl) && CraftStillOurs(base) && L32(base, g.craft + kPropController) == g.ctrl &&
        pdz::guestmem::Ok(base, g.ctrl, kSeatBytes + kSeatUsableB + 1))
    {
      base[g.ctrl + kSeatBytes + kSeatUsableA] = g.usable_a;
      base[g.ctrl + kSeatBytes + kSeatUsableB] = g.usable_b;
    }
    g.usable_saved = false;
  }

  void OffGun(uint8_t *base, const char *why)
  {
    if (!g.on_gun)
    {
      return;
    }
    g.on_gun = false;
    RestoreUsable(base);
    REXLOG_INFO("pdz_counterop: outpost: player 2 is off the hovercraft's gun ({}): {}; the gun seat's usable bytes "
                "are back as the level had them ({} / {})",
                why, TurretText(base, g.turret),
                pdz::guestmem::Ok(base, g.ctrl, kSeatBytes + kSeatUsableB + 1) ? int(base[g.ctrl + kSeatBytes + kSeatUsableA]) : 0, // Sun 22 Nov night (audit)
                pdz::guestmem::Ok(base, g.ctrl, kSeatBytes + kSeatUsableB + 1) ? int(base[g.ctrl + kSeatBytes + kSeatUsableB]) : 0);
  }
} // namespace

// counter_op.cpp's Reset (every level boundary).
void PdzCounterOpOutpostReset()
{
  g = State{};
}

// Round 14 (Tue 17 Nov). counter_op.cpp's hit handler hook, around Rare's handler: the hit is one of player 2's
// gun at the agent (the damage calculator's hook in counter_op_pvp.cpp scales it).
static bool g_gun_hit_now = false;
void PdzCounterOpOutpostGunHit(bool on)
{
  g_gun_hit_now = on && g.on_gun;
}
float PdzCounterOpOutpostGunScale()
{
  if (!g_gun_hit_now)
  {
    return 1.0f;
  }
  const float s = float(REXCVAR_GET(pdz_counterop_outpost_gun_damage_scale));
  return std::isfinite(s) ? std::fmin(std::fmax(s, 0.0f), 4.0f) : 1.0f;
}
// The last fight is on (counter_op.cpp: the hits on the agent are then logged, the first 300).
bool PdzCounterOpOutpostFight()
{
  return g.fight && Ptr(g.script);
}
void PdzCounterOpOutpostAgentHit(const uint8_t *base, uint32_t attacker, float before, float after, float armour_before,
                                 float armour_after)
{
  if (!g.fight || !Ptr(g.script) || ++g.agent_hit_lines > 300)
  {
    return;
  }
  const uint32_t boss = L32(base, g.script + kScriptMaiHem), driver = L32(base, g.script + kScriptDriver);
  REXLOG_INFO("pdz_counterop: outpost: fight hit on the agent by {} {:08X}: health {:.1f} -> {:.1f}, armour {:.1f} -> "
              "{:.1f} (#{})",
              attacker == boss ? "MAI HEM" : attacker == driver ? "her driver" : "another", attacker, before, after,
              armour_before, armour_after, g.agent_hit_lines);
}

// True while player 2 sits at the hovercraft's gun (counter_op.cpp: no weapon redraw then).
bool PdzCounterOpOutpostOnGun()
{
  return g.on_gun;
}

// counter_op.cpp Spots() and Pick(): this guard is Mai Hem at the gun and may be taken now.
bool PdzCounterOpOutpostGunner(const uint8_t *base, uint32_t guard)
{
  if (!g.fight || g.boss || !REXCVAR_GET(pdz_counterop_outpost_turret))
  {
    return false;
  }
  return BossAtGun(base, guard);
}

// counter_op.cpp SpawnAt(), just before the taken guard is hidden: Mai Hem leaves the gun (Rare's emergency
// evacuation of a turret: the gunner detached, his locks released, the gun reset and free).
void PdzCounterOpOutpostUnseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
{
  uint32_t turret = 0, craft = 0;
  if (!PdzCounterOpOutpostGunner(base, guard) || !BossAtGun(base, guard, &turret, &craft))
  {
    return;
  }
  const uint32_t ctrl = L32(base, craft + kPropController);
  const std::string before = TurretText(base, turret);
  Call(ctx, base, sub_82590E30, turret);
  g.boss = guard;
  g.turret = turret;
  g.craft = craft;
  g.ctrl = Ptr(ctrl) ? ctrl : 0;
  g.pending = true;
  REXLOG_INFO("pdz_counterop: outpost: Mai Hem {:08X} is taken off her hovercraft's gun for player 2 {:08X} (Rare's "
              "emergency evacuation 82590E30): before: {}; after: {}; her parent now {:08X}; hovercraft {:08X} at "
              "({:.0f}, {:.0f}, {:.0f}): {}",
              guard, p2, before, TurretText(base, turret), L32(base, guard + kParent), craft,
              LF(base, craft + kPosition), LF(base, craft + kPosition + 4), LF(base, craft + kPosition + 8),
              SeatText(base, g.ctrl));
}

// counter_op.cpp SpawnAt(), right after the taken guard is hidden: player 2 is seated at the gun.
void PdzCounterOpOutpostAfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
{
  if (!g.pending || guard != g.boss)
  {
    return;
  }
  g.pending = false;
  const uint32_t turret = g.turret, ctrl = g.ctrl;
  if (!Ptr(turret) || !Ptr(p2))
  {
    return;
  }
  const int32_t how = REXCVAR_GET(pdz_counterop_test_outpost_mount);
  const uint32_t flag = REXCVAR_GET(pdz_counterop_test_outpost_mount_flag) ? 1u : 0u;
  // G2: this hovercraft and its gun are the exception for this fight (counter_op_vehicles.cpp's gate).
  PdzCounterOpVehicleAllow(g.craft, true);
  std::string steps;
  bool seated = L32(base, turret + kTurretGunner) == p2;
  if (!seated && how != 2 && Ptr(ctrl) && L32(base, ctrl) == kHovercraftCtrlVtable)
  {
    // The level closed the gun seat to players (usable bytes 0): opened for this one mount, put back when he leaves.
    g.usable_a = base[ctrl + kSeatBytes + kSeatUsableA];
    g.usable_b = base[ctrl + kSeatBytes + kSeatUsableB];
    g.usable_saved = true;
    base[ctrl + kSeatBytes + kSeatUsableA] = 1;
    base[ctrl + kSeatBytes + kSeatUsableB] = 1;
    const uint32_t r = Call(ctx, base, __imp__sub_8266A8E8, ctrl, p2, 1, flag) & 0xFF;
    seated = L32(base, turret + kTurretGunner) == p2;
    steps += fmt::format("Rare's mount for players 8266A8E8(controller, player 2, seat 1, flag {}) answered {} (it "
                         "answers 0 for the gun seat by design): {}; ",
                         flag, r, seated ? "he is the gunner" : "NOT the gunner");
  }
  if (!seated && how != 1)
  {
    const uint32_t r = Call(ctx, base, __imp__sub_825904F0, turret, p2, flag) & 0xFF;
    seated = L32(base, turret + kTurretGunner) == p2;
    steps += fmt::format("the turret's own mount 825904F0(gun, player 2, flag {}) answered {}: {}; ", flag, r,
                         seated ? "he is the gunner" : "NOT the gunner");
  }
  g.on_gun = seated;
  g.seated_at = std::chrono::steady_clock::now();
  g.last_turret_state = L32(base, turret + kTurretState);
  g.last_firing = base[turret + kTurretFiring];
  if (!seated)
  {
    RestoreUsable(base);
  }
  REXLOG_INFO("pdz_counterop: outpost: player 2 {:08X} {} the gun of Mai Hem's hovercraft: {}{}; his parent {:08X}; {}",
              p2, seated ? "is seated at" : "was NOT seated at", steps, TurretText(base, turret),
              L32(base, p2 + kParent), SeatText(base, ctrl));
}

// counter_op.cpp HideP2(), first thing: a gunner is taken off the gun before he is taken out of the world.
void PdzCounterOpOutpostBeforeHide(PPCContext &ctx, uint8_t *base, uint32_t p2)
{
  if (!g.on_gun)
  {
    return;
  }
  if (GunStillOurs(base) && Ptr(p2) && L32(base, g.turret + kTurretGunner) == p2) // Sun 22 Nov night (audit)
  {
    Call(ctx, base, sub_82590E30, g.turret);
    OffGun(base, "his life ends: Rare's emergency evacuation 82590E30 first");
  }
  else
  {
    OffGun(base, "he was no longer the gunner");
  }
}

// counter_op.cpp Tick(), every level tick of a started Counter-Op game.
void PdzCounterOpOutpostTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, bool out, bool cutscene)
{
  if (!Ptr(g.script))
  {
    return; // not Outpost, or no level message yet
  }
  const auto now = std::chrono::steady_clock::now();
  const uint32_t boss = L32(base, g.script + kScriptMaiHem), driver = L32(base, g.script + kScriptDriver);
  const uint32_t craft = L32(base, g.script + kScriptCraft);
  if (REXCVAR_GET(pdz_counterop_test_outpost_log) && now >= g.next_log)
  {
    g.next_log = now + std::chrono::seconds(1);
    const uint32_t ctrl = Ptr(craft) ? L32(base, craft + kPropController) : 0;
    const uint32_t turret = TurretOf(base, craft);
    const uint32_t flags = L32(base, g.script + kScriptFlags);
    const uint32_t participant = Ptr(p2c) ? L32(base, p2c + kParticipantOf) : 0;
    REXLOG_INFO("pdz_counterop: outpost: TEST LOG: fight {}{}; flags {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}; Mai "
                "Hem {:08X} (vtable {:08X}, health {:.1f}, hidden {}, parent {:08X}); driver {:08X} (health {:.1f}, "
                "hidden {}, parent {:08X}); hovercraft {:08X} at ({:.0f}, {:.0f}, {:.0f}); {}; {}; player 2 {} "
                "(parent {:08X}, health {:.1f})",
                g.fight ? "on" : "not yet", cutscene ? " (cutscene)" : "", Ptr(flags) ? L32(base, flags) : 0,
                Ptr(flags) ? L32(base, flags + 4) : 0, Ptr(flags) ? L32(base, flags + 8) : 0,
                Ptr(flags) ? L32(base, flags + 12) : 0, Ptr(flags) ? L32(base, flags + 16) : 0,
                Ptr(flags) ? L32(base, flags + 20) : 0, boss, Ptr(boss) ? L32(base, boss) : 0,
                Ptr(boss) ? LF(base, boss + kHealth) : 0.0f,
                Ptr(boss) ? int((L32(base, boss + kHidden) & 0x10) != 0) : 0, Ptr(boss) ? L32(base, boss + kParent) : 0,
                driver, Ptr(driver) ? LF(base, driver + kHealth) : 0.0f,
                Ptr(driver) ? int((L32(base, driver + kHidden) & 0x10) != 0) : 0,
                Ptr(driver) ? L32(base, driver + kParent) : 0, craft, Ptr(craft) ? LF(base, craft + kPosition) : 0.0f,
                Ptr(craft) ? LF(base, craft + kPosition + 4) : 0.0f, Ptr(craft) ? LF(base, craft + kPosition + 8) : 0.0f,
                SeatText(base, ctrl), TurretText(base, turret), out ? "out of the world" : "in the world",
                Ptr(p2c) ? L32(base, p2c + kParent) : 0, Ptr(participant) ? LF(base, participant + 980) : 0.0f);
  }
  if (!g.fight)
  {
    return;
  }
  // Mai Hem sits at the gun: said once, and the pick is asked to run again whatever he was doing (a guard that
  // escaped Rare's delete, "out for the rest of the mission", a body of a deleted guard).
  if (!g.told_ready && !g.boss && !cutscene && REXCVAR_GET(pdz_counterop_outpost_turret) && BossAtGun(base, boss))
  {
    g.told_ready = true;
    REXLOG_INFO("pdz_counterop: outpost: Mai Hem {:08X} sits at the gun of her hovercraft {:08X} ({}): player 2 may "
                "take her place now (pdz_counterop_outpost_turret)",
                boss, craft, TurretText(base, TurretOf(base, craft)));
    PdzCounterOpReleaseBody(ctx, base, "outpost: Mai Hem sits at her hovercraft's gun: his body is given up for the gun",
                            REXCVAR_GET(pdz_counterop_outpost_instant)); // round 18 (item 8)
  }
  // round 14: TESTING TOOL 4 (the measurement of Rare's own fight): the agent is put once 10 m from the hovercraft
  // and left standing there. Works without player 2 at the gun.
  if (REXCVAR_GET(pdz_counterop_test_outpost_agent) == 4 && !g.tool4_placed && Ptr(p1c) && Ptr(craft) && !cutscene)
  {
    static uint32_t scratch4 = 0;
    if (!scratch4)
    {
      scratch4 = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
    }
    if (scratch4)
    {
      g.tool4_placed = true;
      SF(base, scratch4, LF(base, craft + kPosition) + 1000.0f);
      SF(base, scratch4 + 4, LF(base, craft + kPosition + 4) + 30.0f);
      SF(base, scratch4 + 8, LF(base, craft + kPosition + 8));
      Call(ctx, base, sub_825AD0E0, p1c, scratch4);
      REXLOG_INFO("pdz_counterop: outpost: TEST TOOL 4: the agent is put once 10 m from the hovercraft ({:.0f}, {:.0f}, "
                  "{:.0f}) and left standing: she is now at ({:.0f}, {:.0f}, {:.0f})",
                  LF(base, craft + kPosition), LF(base, craft + kPosition + 4), LF(base, craft + kPosition + 8),
                  LF(base, p1c + kPosition), LF(base, p1c + kPosition + 4), LF(base, p1c + kPosition + 8));
    }
  }
  if (!g.on_gun)
  {
    return;
  }
  // round 14: at the gun he has Mai Hem's health for this fight, given once when he is seated.
  if (!g.health_given && Ptr(p2c) && !out && !cutscene)
  {
    const uint32_t participant = L32(base, p2c + kParticipantOf);
    const float want = float(REXCVAR_GET(pdz_counterop_outpost_maihem_health));
    if (Ptr(participant) && std::isfinite(want) && want >= 1.0f && want <= 2000.0f)
    {
      g.health_given = true;
      const float before = LF(base, participant + 980);
      if (before > kOwnFloor)
      {
        SF(base, participant + 980, want);
        REXLOG_INFO("pdz_counterop: outpost: player 2 at the hovercraft's gun has Mai Hem's health for this fight: {:.1f} "
                    "-> {:.1f} (pdz_counterop_outpost_maihem_health)",
                    before, LF(base, participant + 980));
      }
    }
  }
  const uint32_t turret = g.turret;
  const bool gun_ours = GunStillOurs(base); // Sun 22 Nov night (audit): never Rare's mount on a gone gun
  if (gun_ours && Ptr(p2c) && L32(base, turret + kTurretGunner) != p2c && !out && !cutscene &&
      REXCVAR_GET(pdz_counterop_outpost_turret_stay) && g.reseat_lines < 20)
  {
    // round 13b u: the owner's decision: Mai Hem never leaves her gun, so neither does he while the fight runs. A
    // dismount Rare's turret did by itself (the update 8258CEA0: no gun left 8258D2A4, the kick byte +0x513, the
    // gunner too far from his seat 8258D5D8, a gunner's animation state 8258D7D4) is undone on the same tick with the
    // turret's own mount, as the takeover did. Not when his life has ended (HideP2 took him off first: on_gun is
    // false by then) and not when he is out of the world.
    const uint32_t participant = L32(base, p2c + kParticipantOf);
    const float health = Ptr(participant) ? LF(base, participant + 980) : 0.0f;
    if (health > kOwnFloor)
    {
      const uint32_t state_before = L32(base, turret + kTurretState), gunner_before = L32(base, turret + kTurretGunner);
      const uint32_t r = Call(ctx, base, __imp__sub_825904F0, turret, p2c, 1) & 0xFF;
      ++g.reseat_lines;
      REXLOG_INFO("pdz_counterop: outpost: player 2 was off the hovercraft's gun without his life ending (the gun's state "
                  "{}, gunner {:08X}; his health {:.1f}): put back at once with the turret's own mount 825904F0 (answer "
                  "{}): {} (he never leaves the gun while the fight runs; pdz_counterop_outpost_turret_stay; #{})",
                  state_before, gunner_before, health, r,
                  L32(base, turret + kTurretGunner) == p2c ? "he is the gunner again" : "NOT seated", g.reseat_lines);
    }
  }
  if (!gun_ours || !Ptr(p2c) || L32(base, turret + kTurretGunner) != p2c)
  {
    OffGun(base, "the gun's gunner is no longer player 2");
    return;
  }
  // round 13b u: the hovercraft keeps driving under Rare's AI with him at the gun: one line every 3 s.
  if (now >= g.next_drive && Ptr(g.craft) && g.drive_lines < 80)
  {
    const float x = LF(base, g.craft + kPosition), y = LF(base, g.craft + kPosition + 4), z = LF(base, g.craft + kPosition + 8);
    const double dt = g.drive_have ? std::chrono::duration<double>(now - g.last_drive).count() : 0.0;
    const float moved = g.drive_have ? std::sqrt((x - g.drive_x) * (x - g.drive_x) + (z - g.drive_z) * (z - g.drive_z)) : 0.0f;
    const uint32_t ctrl = L32(base, g.craft + kPropController);
    // (Sat 21 Nov evening: every step tested as mapped memory)
    const uint32_t occupant = pdz::guestmem::Ok(base, ctrl, kSeatOccupant + 4) ? L32(base, ctrl + kSeatOccupant) : 0;
    const uint32_t setup = pdz::guestmem::Ok(base, occupant, 88) ? L32(base, occupant + 84) : 0;
    const uint32_t idp = pdz::guestmem::Ok(base, setup, 56) ? L32(base, setup + 52) : 0;
    char id[5] = {'?', 0, 0, 0, 0};
    if (pdz::guestmem::Ok(base, idp, 4))
    {
      for (int i = 0; i < 4; ++i)
      {
        const char ch = char(base[idp + i]);
        id[i] = ch >= 32 && ch < 127 ? ch : '?';
      }
    }
    ++g.drive_lines;
    REXLOG_INFO("pdz_counterop: outpost: DRIVE: the hovercraft at ({:.0f}, {:.0f}, {:.0f}){}; its driver (seat 0) {:08X} "
                "'{}' ({}, health {:.0f}, seat state {}; 3 = seated), vehicle state {}; the gunner is player 2, gun state "
                "{} ({:.0f} s after the mount; #{})",
                x, y, z,
                g.drive_have && dt > 0.1 ? fmt::format(": moved {:.1f} m in {:.1f} s = {:.1f} m/s", moved / 100.0f, dt,
                                                       moved / 100.0f / dt)
                                         : std::string(),
                occupant, id, occupant == driver ? "the script's driver s32b" : "NOT the script's driver",
                Ptr(occupant) ? LF(base, occupant + kHealth) : 0.0f, Ptr(ctrl) ? L32(base, ctrl + kSeatState) : 0,
                Ptr(ctrl) ? L32(base, ctrl + kCtrlState) : 0, L32(base, turret + kTurretState),
                std::chrono::duration<double>(now - g.seated_at).count(), g.drive_lines);
    g.drive_x = x;
    g.drive_z = z;
    g.drive_have = true;
    g.last_drive = now;
    g.next_drive = now + std::chrono::seconds(3);
  }
  const uint32_t state = L32(base, turret + kTurretState);
  if (state != g.last_turret_state)
  {
    REXLOG_INFO("pdz_counterop: outpost: the gun's state {} -> {} with player 2 at it ({:.1f} s after the mount; 3 = in "
                "use)",
                g.last_turret_state, state, std::chrono::duration<double>(now - g.seated_at).count());
    g.last_turret_state = state;
  }
  const uint8_t firing = base[turret + kTurretFiring];
  if (firing != g.last_firing)
  {
    g.last_firing = firing;
    if (++g.fire_lines <= 6)
    {
      REXLOG_INFO("pdz_counterop: outpost: player 2 {} the hovercraft's gun (the gun's firing byte; #{}{})",
                  firing ? "fires" : "stops firing", g.fire_lines, g.fire_lines == 6 ? "; further times not logged" : "");
    }
  }
  const int32_t tool = REXCVAR_GET(pdz_counterop_test_outpost_agent);
  // round 13b u: the agent's health, every change while player 2 sits at the gun (the proof of his gun's hits).
  if (Ptr(p1c))
  {
    const uint32_t participant1 = L32(base, p1c + kParticipantOf);
    const float health1 = Ptr(participant1) ? LF(base, participant1 + 980) : -1.0f;
    if (health1 != g.agent_health)
    {
      if (g.agent_health >= 0.0f && ++g.health_lines <= 60)
      {
        REXLOG_INFO("pdz_counterop: outpost: the agent's health {:.1f} -> {:.1f} with player 2 at the hovercraft's gun "
                    "(the gun's firing byte {}; {:.1f} s after the mount; #{})",
                    g.agent_health, health1, int(base[turret + kTurretFiring]),
                    std::chrono::duration<double>(now - g.seated_at).count(), g.health_lines);
      }
      g.agent_health = health1;
    }
  }
  // round 13b u: TESTING TOOL 3: the agent on the gun's barrel line, every tick.
  if (tool == 3 && Ptr(p1c) && !cutscene)
  {
    static uint32_t scratch3 = 0;
    if (!scratch3)
    {
      scratch3 = REX_KERNEL_MEMORY()->SystemHeapAlloc(48);
    }
    const uint32_t model = L32(base, turret + 0x48);
    const uint32_t bone_a = L32(base, 0x82DA431Cu + 8), bone_b = L32(base, 0x82DA431Cu + 0xC);
    const uint32_t slot = L32(base, 0x82FBE3C0u); // 82413998: the model's matrices are its word (this + 15)
    const uint32_t mats = Ptr(model) && slot < 64 ? L32(base, model + (slot + 0xF) * 4) : 0;
    const uint32_t cls = Ptr(model) ? L32(base, model) : 0;
    const uint32_t info = Ptr(cls) ? L32(base, cls + 0xC) : 0;
    const uint32_t bones = Ptr(info) ? L32(base, info + 0x34) : 0;
    // The bones are in the gun prop's own frame (run u_outpost1: 1.5 m from its origin). The prop's world matrix is the
    // model's word (slot + 10) (the turret's virtual +0x10 = 82518580 copies it; the update adds its yaw to the bones'
    // yaw at 8258E0D8-8258E0F0): world = local x row 0 + local y row 1 + local z row 2 + row 3.
    const uint32_t wm = Ptr(model) && slot < 64 ? L32(base, model + (slot + 0xA) * 4) : 0;
    if (scratch3 && Ptr(model) && L32(base, model + 0x3C) && Ptr(mats) && Ptr(bones) && Ptr(wm) && bone_a < 256 &&
        bone_b < 256)
    {
      Call(ctx, base, sub_82413998, model, bone_a, scratch3);
      Call(ctx, base, sub_82413998, model, bone_b, scratch3 + 16);
      const float lax = LF(base, scratch3), lay = LF(base, scratch3 + 4), laz = LF(base, scratch3 + 8);
      const float lbx = LF(base, scratch3 + 16), lby = LF(base, scratch3 + 20), lbz = LF(base, scratch3 + 24);
      auto world = [&](float x, float y, float z, int axis) {
        return x * LF(base, wm + axis * 4) + y * LF(base, wm + 0x10 + axis * 4) + z * LF(base, wm + 0x20 + axis * 4) +
               LF(base, wm + 0x30 + axis * 4);
      };
      const float ax = world(lax, lay, laz, 0), ay = world(lax, lay, laz, 1), az = world(lax, lay, laz, 2);
      const float bx = world(lbx, lby, lbz, 0), by = world(lbx, lby, lbz, 1), bz = world(lbx, lby, lbz, 2);
      float dx = bx - ax, dy = by - ay, dz = bz - az;
      const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (std::isfinite(len) && len > 0.01f)
      {
        dx /= len;
        dy /= len;
        dz /= len;
        if (g.line_phase < 0)
        {
          g.line_from = now;
        }
        const int phase = int(std::chrono::duration<double>(now - g.line_from).count() / 3.0) % 4;
        static const float kAhead[4] = {500.0f, 1000.0f, -500.0f, 2000.0f};
        const float ahead = kAhead[phase];
        // Ahead of bone b (positive) or behind bone a (negative). 825AD0E0 puts her position about 77 above the
        // height asked (run f_outpost4: asked +130, read +205), so the line's height less 77 is asked.
        const float ox = ahead >= 0.0f ? bx : ax, oy = ahead >= 0.0f ? by : ay, oz = ahead >= 0.0f ? bz : az;
        const float tx = ox + dx * ahead, ty = oy + dy * ahead, tz = oz + dz * ahead;
        SF(base, scratch3 + 32, tx);
        SF(base, scratch3 + 36, ty - 77.0f);
        SF(base, scratch3 + 40, tz);
        Call(ctx, base, sub_825AD0E0, p1c, scratch3 + 32);
        if (phase != g.line_phase && ++g.line_lines <= 40)
        {
          const float jx = LF(base, p1c + kPosition), jy = LF(base, p1c + kPosition + 4),
                      jz = LF(base, p1c + kPosition + 8);
          // her distance from the line through the first bone along the barrel direction
          const float rx = jx - ax, ry = jy - ay, rz = jz - az;
          const float along = rx * dx + ry * dy + rz * dz;
          const float qx = rx - along * dx, qy = ry - along * dy, qz = rz - along * dz;
          REXLOG_INFO("pdz_counterop: outpost: TEST TOOL 3: the gun's aim bones (world) {} at ({:.0f}, {:.0f}, {:.0f}) and {} at "
                      "({:.0f}, {:.0f}, {:.0f}) ({:.0f} apart; barrel direction ({:.2f}, {:.2f}, {:.2f})); hovercraft at "
                      "({:.0f}, {:.0f}, {:.0f}); the agent asked {:.0f} cm {} at ({:.0f}, {:.0f}, {:.0f}), she is now at "
                      "({:.0f}, {:.0f}, {:.0f}): {:.0f} along the line, {:.0f} off it; her health {:.1f}; firing byte {} "
                      "(#{})",
                      bone_a, ax, ay, az, bone_b, bx, by, bz, len, dx, dy, dz, LF(base, g.craft + kPosition),
                      LF(base, g.craft + kPosition + 4), LF(base, g.craft + kPosition + 8), std::fabs(ahead),
                      ahead >= 0.0f ? "ahead of the second bone" : "behind the first bone", tx, ty, tz, jx, jy, jz, along,
                      std::sqrt(qx * qx + qy * qy + qz * qz), g.agent_health, int(base[turret + kTurretFiring]),
                      g.line_lines);
        }
        g.line_phase = phase;
      }
    }
    else if (++g.line_lines <= 3)
    {
      REXLOG_INFO("pdz_counterop: outpost: TEST TOOL 3: the gun's aim bones cannot be read (model {:08X}, its matrices "
                  "{:08X}, bone table {:08X}, bones {} / {}, matrix slot {})",
                  model, mats, bones, bone_a, bone_b, slot);
    }
  }
  else if (tool != 3)
  {
    g.line_phase = -1;
  }
  if (tool > 0 && tool < 3 && Ptr(p1c) && Ptr(g.craft) && !cutscene)
  {
    static uint32_t scratch = 0;
    if (!scratch)
    {
      scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
    }
    const float cx = LF(base, g.craft + kPosition), cy = LF(base, g.craft + kPosition + 4),
                cz = LF(base, g.craft + kPosition + 8);
    if (scratch && now >= g.next_front && std::isfinite(cx) && std::isfinite(cz))
    {
      g.next_front = now + std::chrono::milliseconds(tool == 1 ? 250 : 1500);
      const float angle = tool == 1 ? float(std::chrono::duration<double>(now - g.seated_at).count() * 0.349) : 0.0f;
      SF(base, scratch, cx + 700.0f * std::sin(angle));
      SF(base, scratch + 4, cy + (tool == 1 ? 130.0f : 30.0f)); // 1: her body at the height of the gun's line
      SF(base, scratch + 8, cz + 700.0f * std::cos(angle));
      Call(ctx, base, sub_825AD0E0, p1c, scratch);
      if (++g.aim_lines <= 3 || g.aim_lines % 40 == 0)
      {
        REXLOG_INFO("pdz_counterop: outpost: TEST TOOL {}: the agent is put 7 m from the hovercraft ({:.0f}, {:.0f}, "
                    "{:.0f}), angle {:.0f} deg: she is now at ({:.0f}, {:.0f}, {:.0f}) (pdz_counterop_test_outpost_agent; "
                    "#{})",
                    tool, cx, cy, cz, angle * 57.29578f, LF(base, p1c + kPosition), LF(base, p1c + kPosition + 4),
                    LF(base, p1c + kPosition + 8), g.aim_lines);
      }
    }
    if (tool == 2)
    {
      const float dx = cx - LF(base, p1c + kPosition), dz = cz - LF(base, p1c + kPosition + 8);
      const float dy = (cy + 200.0f) - (LF(base, p1c + kPosition + 4) + 87.0f); // his chest against her eye
      const float flat = std::sqrt(dx * dx + dz * dz);
      if (std::isfinite(flat) && flat > 50.0f)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = p1c;
        frame.ctx.f1.f64 = -std::atan2(double(dy), double(flat)); // 825AD3B0's pitch is positive down
        frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
        sub_825AD3B0(frame.ctx, base);
      }
    }
  }
  // TESTING TOOL: his life ends at the gun.
  const int32_t kill = REXCVAR_GET(pdz_counterop_test_outpost_kill);
  if (kill != g.last_kill)
  {
    g.last_kill = kill;
    const uint32_t participant = L32(base, p2c + kParticipantOf);
    if (kill != 0 && Ptr(participant) && !cutscene)
    {
      const float before = LF(base, participant + 980);
      SF(base, participant + 980, kOwnFloor);
      REXLOG_INFO("pdz_counterop: outpost: TEST TOOL: player 2's health {:.1f} -> {:.1f} (his floor) at the gun: his "
                  "life ends on this tick as from a hit (pdz_counterop_test_outpost_kill {})",
                  before, kOwnFloor, kill);
    }
  }
}

// Outpost's script: its message handler (script r3, the message r8). Only this level's script reaches it.
REX_HOOK_RAW(sub_82376848)
{
  const uint32_t script = ctx.r3.u32, message = ctx.r8.u32;
  if (PdzCounterOpRunning() && Ptr(script) && Ptr(message))
  {
    g.script = script;
    if (base[message + 1] == 8 && base[message + 2] == 22 && !g.fight)
    {
      const uint32_t flags = L32(base, script + kScriptFlags);
      const bool done = Ptr(flags) && (L32(base, flags + 4) & 2u);
      const bool checkpoint = (base[kCheckpointByte] & 2u) != 0;
      if (!done && !checkpoint)
      {
        g.fight = true;
        const bool release = REXCVAR_GET(pdz_counterop_outpost_fight_release);
        REXLOG_INFO("pdz_counterop: outpost: level message 8 / 22: the last fight starts (Rare deletes the 107 "
                    "placed guards now, then the cutscene, then Mai Hem and her driver man her hovercraft): {}",
                    release ? "player 2's guard is given back first (pdz_counterop_outpost_fight_release)"
                            : "pdz_counterop_outpost_fight_release is off: player 2 is left as he is");
        if (release)
        {
          PdzCounterOpReleaseBody(ctx, base, "outpost: the last fight starts, Rare deletes every placed guard");
        }
      }
    }
  }
  __imp__sub_82376848(ctx, base);
}

// A turret's MountStart (turret r3, character r4, flag r5). The hidden Mai Hem keeps Rare's "man the gun" task: it
// must not put her back on the gun while player 2 has her place.
REX_HOOK_RAW(sub_825904F0)
{
  if (g.boss && ctx.r4.u32 == g.boss && PdzCounterOpRunning() && (L32(base, g.boss + kHidden) & 0x10))
  {
    if (++g.remount_lines <= 3)
    {
      REXLOG_INFO("pdz_counterop: outpost: the hidden Mai Hem {:08X} asks for the gun {:08X} again (her ride task, "
                  "asked from {:08X}): refused (#{}{})",
                  g.boss, ctx.r3.u32, uint32_t(ctx.lr), g.remount_lines,
                  g.remount_lines == 3 ? "; further times not logged" : "");
    }
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_825904F0(ctx, base);
}

// A turret's "does the gunner get off now" (turret r3; 1 = yes): its request byte +0x510 (82590DB8 sets it) or, for
// a human gunner, his A button (participant +0x720). Asked every frame by the turret's update (8258D444) while it is
// in use; 1 starts the dismount (825910F0). Answered 0 for player 2 at Mai Hem's gun.
REX_HOOK_RAW(sub_82591948)
{
  if (g.on_gun && ctx.r3.u32 == g.turret && REXCVAR_GET(pdz_counterop_outpost_turret_stay) && PdzCounterOpPlayer2() &&
      L32(base, g.turret + kTurretGunner) == PdzCounterOpPlayer2())
  {
    const uint32_t turret = ctx.r3.u32;
    __imp__sub_82591948(ctx, base);
    if (ctx.r3.u32 & 0xFF)
    {
      base[turret + 0x510] = 0; // a request is not kept for later
      if (++g.stay_lines <= 3)
      {
        REXLOG_INFO("pdz_counterop: outpost: player 2 asked to get off the hovercraft's gun (Rare's test 82591948 "
                    "said yes): refused, he stays until his life ends (pdz_counterop_outpost_turret_stay; #{}{})",
                    g.stay_lines, g.stay_lines == 3 ? "; further times not logged" : "");
      }
    }
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82591948(ctx, base);
}

// counter_op_pvp.cpp's hook of Rare's "does this hit count" (82545698: victim, source), asked first: while player 2
// sits at the hovercraft's gun his hits count on the agent only (Mai Hem's team is not the troopers' team, so the
// friendly-fire rule does not cover the waves), and only the agent's hits count on him (the hovercraft, its driver
// and the waves cannot hurt the gunner they carry). True = the hit does not count.
bool PdzCounterOpOutpostHitRefused(const uint8_t *base, uint32_t victim, uint32_t source)
{
  if (!g.on_gun)
  {
    return false;
  }
  const uint32_t p2 = PdzCounterOpPlayer2(), agent = PdzCounterOpAgent();
  if (!p2 || !Ptr(victim))
  {
    return false;
  }
  // Sun 22 Nov night (audit): the source's +92 read only where readable (it can be a short record).
  const bool his = Ptr(source) && (source == p2 || (pdz::guestmem::Ok(base, source, 96) && L32(base, source + 92) == p2) || PdzCounterOpIsOf(base, source, p2));
  if (his && victim != agent && victim != p2)
  {
    if (++g.hit_lines <= 6 || g.hit_lines % 200 == 0)
    {
      REXLOG_INFO("pdz_counterop: outpost: the gunner's hit on {:08X} (vtable {:08X}) does not count (#{}; the source "
                  "{:08X}): at the hovercraft's gun his shots count on the agent only",
                  victim, L32(base, victim), g.hit_lines, source);
    }
    return true;
  }
  if (victim == p2 && !his &&
      !(Ptr(source) && (source == agent || (pdz::guestmem::Ok(base, source, 96) && L32(base, source + 92) == agent) || PdzCounterOpIsOf(base, source, agent))))
  {
    if (++g.hurt_lines <= 6 || g.hurt_lines % 200 == 0)
    {
      REXLOG_INFO("pdz_counterop: outpost: a hit on the gunner from {:08X} (vtable {:08X}) does not count (#{}): at "
                  "the hovercraft's gun only the agent can hurt him",
                  source, Ptr(source) ? L32(base, source) : 0, g.hurt_lines);
    }
    return true;
  }
  return false;
}
