// Counter-Operative round 13: River (the hovercraft on the lakes). Built only with PDZ_COUNTEROP.
//
// Player 2 takes over the DRIVER of an enemy hovercraft through the normal takeover; the craft's AI gunner stays on
// the gun and shoots the agent by his own AI, the way Jack does for her. The study with every address:
// research notes: RIVER_STUDY_Mon16Nov.md.
//
// Rare's hovercraft (Title Update 3, read from the code):
//   - Vehicle prop: vtable 820CA9E0 (setup 825D1B20; template type byte 2 or 4 = hovercraft). Its controller
//     (0x830 bytes, 82670F60, vtable 820CF1F0) is at prop +0x364. One class for the agent's craft and the enemy's.
//   - Controller (base class 82669280, as the jetpack's): +0x0C the prop, +0x1EC / +0x1F0 vehicle state now / asked,
//     seats from +0x3C, 0x60 bytes each (+0x3C / +0x3D usable bytes, +0x58 seat state: 0 empty, 1 getting in, 2 seat
//     animation, 3 seated; +0x60 the occupant). Seat 0 = the driver, seat 1 = the gun (CanEnter 826788F0).
//     +0x630 / +0x634: a driver who died in the seat (he stays slumped there until the next driver gets in).
//   - The level script gives the gunner the AI task 821E5BA0 in "man the gun" mode (821E6068) and the driver the same
//     task in "drive" mode with a patrol (821E60D0): Jack on the agent's craft, s22a / s23a / s21a / s21c / s21e on
//     the enemy's. The gunner's aiming is the gun's own AI: it does not ask who drives.
//   - Rare's mount 8266A8E8(controller, character, seat, flag): a human into seat 0 is "getting in" (seat state 1);
//     the base update seats him on the next tick when the craft's state is 2 (8266A39C -> 8266AA98), and with state
//     1 it wants Use held (8266A410). The driving itself is the one update 82675B20 for AI and human (a human is
//     steered by his pad, his view carried by 825B3AC0).
//   - Leaving: 826745E0(controller, occupant) is Rare's full removal of a driver (riding locks released, detached,
//     seat state 0, handle cleared); 82674478 (vtable +0x90) is the same plus "state 2 -> asked 1".
//
// The hand-over (the jetpack's way, JETPACK_AIR_STUDY_Sun15Nov.md): all inside counter_op.cpp's SpawnAt, one tick:
// the guard leaves the seat (826745E0), is hidden as a guard on foot, and Rare's mount seats player 2.
#include "counter_op_river.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

#include "counter_op_rules13.h"
#include "render/game_pace.h" // SpLevelName (round 13b r: the checkpoint test tool)
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

uint32_t PdzCounterOpAgent();   // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpPlayer2(); // counter_op.cpp
bool PdzCounterOpOn();          // counter_op.cpp: a Counter-Op game is chosen (before the level start too)
uint32_t PdzCounterOpRules13OwnerOf(const uint8_t *base, uint32_t object); // counter_op_allies.cpp: who fired this

REXCVAR_DEFINE_BOOL(pdz_counterop_river_hovercraft, true, "Gameplay",
                    "Counter-Operative (round 13, default on), River: out on the lakes player 2 may take over the DRIVER of an "
                    "enemy hovercraft (he is dressed as that driver and drives with the player's own hovercraft "
                    "controls); the craft's AI gunner stays on the gun and shoots the agent by himself. Off = the "
                    "hovercraft crews are never taken (round 12)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_river_hovercraft_range_m, 400.0, "Gameplay",
                      "Counter-Operative (round 13; round 16: 400 m, was 150 m: log 027's driver 303 m away was "
                      "refused and player 2 got an island guard 246 m away), River: an enemy hovercraft's driver is only taken over once "
                      "the agent has sat in a hovercraft herself (she is out on the lakes) and while the craft is "
                      "within this many metres of her (the crews patrol the lakes from the level's first second, "
                      "one of them 207 m from her start). 0 = at any distance and at any time (testing)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_river_hovercraft_need_gunner, true, "Gameplay",
                    "Counter-Operative (round 13), River: a hovercraft driver is only taken over while a live AI "
                    "gunner sits on the craft's gun (a driver alone has no weapon at all)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_river_hovercraft_seated_at_once, true, "Gameplay",
                    "Counter-Operative (round 13), River: at the hand-over player 2 is seated at once (the mount's "
                    "flag 1: no get-in animation). Off = Rare's get-in animation for a player (a few seconds "
                    "without control)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_test_river_log, 0.0, "Testing",
                      "TESTING TOOL (counter-op, River): every this many seconds (0 = off) the hovercraft table is "
                      "logged: each craft's place, state, seats, occupants, usable bytes and callbacks, each rider, "
                      "and the distance to the agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_river_kill_driver, false, "Testing",
                    "TESTING TOOL (counter-op, River): when this turns on while player 2 drives a hovercraft his "
                    "health is put at his floor, as if the agent's last shot had just landed (his death in the seat)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(pdz_counterop_test_river_agent_near, 0.0, "Testing",
                      "TESTING TOOL (counter-op, River): each change to a value above 0: the agent is moved (Rare's "
                      "move) to this many metres beside player 2's hovercraft (or, when he has none, the nearest "
                      "enemy hovercraft with a driver), 1 m above the craft's height")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_river_agent_refill, false, "Testing",
                    "TESTING TOOL (counter-op, River): while on, the agent's health is put back to 100 whenever it "
                    "is under 50 (each refill logged): hits on her still show as lost health, but she does not die")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Mon 23 Nov (release 1.0.0). In play on River player 2 was seated as the driver of an enemy hovercraft and his right
// stick did nothing: he could not turn it. Rare's hovercraft update (82675B20) has two ways to steer: when the
// controller's commanded heading (+0x62C) is not its "none" value it turns the craft to that heading and skips the
// driver's look update (the right stick, 825B3AC0) altogether; otherwise the seated driver's stick steers. The
// heading is commanded by the AI driver's vehicle task (82196870 / 82196C78) and reset only by the hovercraft's own
// set-up (82671220): Rare never puts a human into a seat an AI driver has just left, so nothing clears it at the
// hand-over, and the craft kept the last heading the guard had asked for. While player 2 is the driver of the craft
// he was handed, the commanded heading is put back to Rare's "none" value (the constant the update compares with).
// The agent's own craft never has a commanded heading and is not touched; nothing here reads or writes a pad.
REXCVAR_DEFINE_BOOL(pdz_counterop_river_driver_steer, true, "Gameplay",
                    "Counter-Operative (Mon 23 Nov, default on), River: while player 2 drives the enemy hovercraft he "
                    "was handed, the heading its AI driver last asked for is cleared, so his right stick turns the "
                    "craft as the game's player hovercraft controls do. Off = as before (the craft keeps the AI's "
                    "heading and his right stick does nothing)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Round 13b (helper r): the real order.
// Mon 23 Nov night (1.0.1). Log 062, 21:30:28: player 2's hovercraft was destroyed under him (seat state 3 -> 4) and
// Rare threw him off alive, as it does a human; an AI driver dies with his craft. Now his life ends there too: when
// Rare's own 'destroyed' test of the craft is true while he holds a seat (or within 2 s of leaving it), his health
// goes to his floor and counter_op.cpp's own death runs on its next check (the replaced guard dies where he stood).
REXCVAR_DEFINE_BOOL(pdz_counterop_river_dies_with_craft, true, "Gameplay",
                    "Counter-Operative, River (1.0.1, default on): when the hovercraft player 2 drives is destroyed "
                    "with him in the seat, his life ends, as an AI driver's does. Off = 1.0.0 (Rare throws him off "
                    "alive)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_river_destroy_craft, 0, "Testing",
                     "TESTING TOOL (counter-op, River): each change to a new non-zero value marks the hovercraft "
                     "player 2 sits in as destroyed (Rare's destroyed flag 0x4000 of the prop; no explosion is "
                     "played), to prove pdz_counterop_river_dies_with_craft")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_river_hovercraft_first, true, "Gameplay",
                    "Counter-Operative (round 13b; on by default since round 14), River: while the agent sits in a hovercraft, the driver of an enemy "
                    "hovercraft that may be taken (pdz_counterop_river_hovercraft: a gunner aboard, within the range) "
                    "is player 2's next body before any guard on foot, the nearest craft first. Off = the ordinary "
                    "pick alone, whose rules for guards on foot (never a guard moving faster than 6 m/s, the area "
                    "rule) leave a patrolling craft's driver out")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_river_hovercraft_seated_only, true, "Gameplay",
                    "Counter-Operative (round 13b, default on), River: 'the agent has sat in a hovercraft' means she holds one of "
                    "its seats. Off = round 13: her parent object is a hovercraft, which is also true while she only "
                    "stands on its deck (seen in r_river_order1: the lakes were opened to player 2 by standing on it)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Test tools only, all off.
REXCVAR_DEFINE_INT32(pdz_counterop_test_river_kill_p2, 0, "Testing",
                     "TESTING TOOL (counter-op, River): each change to a value above 0 while player 2 is in the world: "
                     "his health is put at his floor, on foot or in a seat, as if the agent's last shot had just "
                     "landed (counter_op.cpp's own death runs on its next check)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_river_agent_drive, "", "Testing",
                      "TESTING TOOL (counter-op, River, with the test pads): 'x,z' (cm): while the agent is the driver "
                      "of a hovercraft, pad 1's sticks are driven towards that point (left stick forward, right stick "
                      "by the heading error); within 20 m it stops. After 30 s without arriving the craft is placed "
                      "there through Rare's object move (82518768), 0.6 m above the lake")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_river_checkpoint, false, "Testing",
                    "TESTING TOOL (counter-op, River): the level starts from Rare's own mid-level checkpoint (Rare's "
                    "'restart from checkpoint' bit 82E426F8 0x20 is set before the level script's Setup): the script "
                    "itself puts the agent and Jack at the hovercraft and Jack's own task goes to its gun")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_river_agent_board, 0, "Testing",
                     "TESTING TOOL (counter-op, River): each change to a value above 0, while the agent is not in a "
                     "hovercraft: 1 = she is moved (Rare's move) onto her own hovercraft (script prop 7), 1.2 m above "
                     "its centre, so her own Use press can seat her; 2 = Rare's Use of the craft is called for her "
                     "(825D2E90: what her Use press calls); 3 = the driver seat's two usable bytes are set and Rare's "
                     "mount 8266A8E8 seats her as its driver")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_river_agent_craft_to, "", "Testing",
                      "TESTING TOOL (counter-op, River): 'x,y,z' (cm): each change while the agent sits in a "
                      "hovercraft: her craft is moved there through Rare's own object move (82518768), as a level "
                      "script places a prop")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_825D2E90);        // a vehicle prop's Use (prop, user)
REX_EXTERN(sub_82518768);        // an object's position (object, &position): what River's Setup uses to place a prop
REX_EXTERN(__imp__sub_8266A8E8); // Rare's mount, past round 13's vehicle gate (counter_op_vehicles.cpp hooks it)
REX_EXTERN(sub_826745E0);        // hovercraft: full removal of an occupant (controller, occupant)
REX_EXTERN(sub_82674478);        // hovercraft vtable +0x90: the same, and a craft in state 2 is asked to state 1
REX_EXTERN(sub_82519288);        // detach (parent, child)
REX_EXTERN(sub_821433D8);        // handle assign (handle, object or 0)
REX_EXTERN(sub_825AD148);        // move a character (character, &position, 0): the test tool only

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216; // world (game +24): nodes (+4 object, +12 next)
  constexpr uint32_t kCharList = 0x82E454A0;                 // +0x710 first node, +0x714 end; node: +0 character, +8 next
  constexpr uint32_t kVehicleVtable = 0x820CA9E0, kHovercraftCtrlVtable = 0x820CF1F0;
  constexpr uint32_t kGuardVtable = 0x820C45A0, kHumanVtable = 0x820C7DE0;
  constexpr uint32_t kParent = 0x5C, kPropFlags = 0x40, kPropPosition = 0x30, kPropHealth = 0x358, kPropController = 0x364;
  constexpr uint32_t kCtrlType = 0x14, kCtrlEnterCallback = 0x18, kCtrlSeatedCallback = 0x28;
  constexpr uint32_t kCtrlState = 0x1EC, kCtrlAsked = 0x1F0, kCtrlDeadDriver = 0x634;
  constexpr uint32_t kSeatBytes = 0x60, kSeatUsable0 = 0x3C, kSeatUsable1 = 0x3D, kSeatState = 0x58, kSeatHandle = 0x5C,
                     kSeatOccupant = 0x60;
  constexpr int kSeats = 4;
  constexpr uint32_t kPosition = 48, kSetup = 84, kHealth = 120, kHidden = 6272, kSetupId = 52;
  constexpr uint32_t kParticipantOf = 6560, kPartHealth = 980;
  constexpr float kOwnFloor = 1.0f; // counter_op.cpp: player 2's health floor while he plays a guard

  using Clock = std::chrono::steady_clock;

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
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

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
    bool river = false;
    std::unordered_map<uint32_t, uint32_t> pending; // a driver Spots() accepted this pick -> his craft (prop)
    uint32_t handing = 0;                           // Unseat -> AfterSpawn: the craft whose seat was just emptied
    uint8_t usable_before[2] = {1, 1};
    uint32_t riding = 0; // the craft player 2 was put on (prop), until he is out of the world again
    uint32_t rider = 0;  // player 2's character then
    uint32_t last_seat_state = 0, last_vehicle_state = 0, last_asked = 0;
    bool seated_told = false, left_told = false;
    Clock::time_point mounted_at{}, last_table{}, last_track{}, last_agent_hit{};
    float track_pos[3] = {};
    float agent_health = -1.0f;
    bool kill_was = false;
    double near_was = 0.0;
    bool agent_rode = false; // the agent has sat in a hovercraft this level: she is out on the lakes
    uint32_t refills = 0;
    uint32_t scratch = 0;
    std::unordered_set<uint64_t> told;
    // round 13b r (test log only)
    bool hit_log = false;
    int board_was = 0;
    std::string craft_to_was;
    uint32_t agent_craft = 0, agent_seat_state = 0, agent_craft_state = 0, agent_craft_asked = 0;
    int agent_seat = -1;
    Clock::time_point last_guns{};
    std::unordered_map<uint32_t, std::array<float, 3>> craft_pos;
    struct Shooter
    {
      uint32_t hits = 0, counted = 0;
      float damage = 0.0f;
    };
    std::unordered_map<uint32_t, Shooter> on_agent, by_agent_craft, on_p2_craft;
    uint32_t hit_lines[3] = {};
    int kill_p2_was = 0;
    std::string drive_was;
    bool drive = false;
    float drive_x = 0.0f, drive_z = 0.0f, drive_sign = 1.0f, drive_prev_err = 0.0f, drive_err = 0.0f;
    float drive_pos[3] = {};
    Clock::time_point drive_since{}, drive_sample{}, drive_check{}, drive_told{};
  };
  State rs;

  // A character's setup id (4 characters).
  std::string Id(const uint8_t *base, uint32_t c)
  {
    // (Sat 21 Nov evening: every step tested as mapped memory; c can be any list member)
    const uint32_t setup = pdz::guestmem::Ok(base, c, kSetup + 4) ? Load32(base, c + kSetup) : 0;
    const uint32_t p = pdz::guestmem::Ok(base, setup, kSetupId + 4) ? Load32(base, setup + kSetupId) : 0;
    std::string out;
    for (uint32_t i = 0; pdz::guestmem::Ok(base, p, 4) && i < 4; ++i)
    {
      const uint8_t ch = base[p + i];
      if (ch < 0x21 || ch > 0x7E)
      {
        break;
      }
      out.push_back(char(ch));
    }
    if (out.empty())
    {
      out = pdz::guestmem::Ok(base, c, 4) && Load32(base, c) == kHumanVtable ? "human" : "-";
    }
    return out;
  }

  // The hovercraft controller of a vehicle prop, or 0.
  uint32_t HovercraftController(const uint8_t *base, uint32_t prop)
  {
    // Sun 22 Nov night (audit): the prop and its controller read only where readable (a craft destroyed mid-ride),
    // as round 18 did for the jetpack's controller.
    if (!pdz::guestmem::Ok(base, prop, kPropController + 4) || Load32(base, prop) != kVehicleVtable)
    {
      return 0;
    }
    const uint32_t ctrl = Load32(base, prop + kPropController);
    return pdz::guestmem::Ok(base, ctrl, kCtrlDeadDriver + 4) && Load32(base, ctrl) == kHovercraftCtrlVtable ? ctrl : 0;
  }

  // The seat a character holds on a controller (any seat state but 0), or -1.
  int SeatOf(const uint8_t *base, uint32_t ctrl, uint32_t character)
  {
    for (int i = 0; i < kSeats; ++i)
    {
      const uint32_t seat = ctrl + uint32_t(i) * kSeatBytes;
      if (Load32(base, seat + kSeatOccupant) == character && Load32(base, seat + kSeatState) != 0)
      {
        return i;
      }
    }
    return -1;
  }

  // Rare's own "destroyed" test of the mount (8266AD5C-8266AD90).
  bool Destroyed(const uint8_t *base, uint32_t prop)
  {
    const uint32_t f = Load32(base, prop + kPropFlags);
    return (f & 0x4000u) || ((f & 0x8000u) && LoadFloat(base, prop + kPropHealth) < 0.0f);
  }

  float Health(const uint8_t *base, uint32_t c)
  {
    if (!Ptr(c))
    {
      return 0.0f;
    }
    if (Load32(base, c) == kHumanVtable)
    {
      const uint32_t participant = Load32(base, c + kParticipantOf);
      if (Ptr(participant))
      {
        return LoadFloat(base, participant + kPartHealth);
      }
    }
    return LoadFloat(base, c + kHealth);
  }

  float Distance(const uint8_t *base, uint32_t a_pos, uint32_t b_pos)
  {
    const float dx = LoadFloat(base, a_pos) - LoadFloat(base, b_pos),
                dy = LoadFloat(base, a_pos + 4) - LoadFloat(base, b_pos + 4),
                dz = LoadFloat(base, a_pos + 8) - LoadFloat(base, b_pos + 8);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  // The vehicle prop a character rides: his parent, or his parent's parent (a gunner is attached to the craft's
  // gun, the gun to the craft). 0 when neither is a vehicle.
  uint32_t CraftOf(const uint8_t *base, uint32_t c)
  {
    // (Sat 21 Nov evening: c is any list member: every step tested as mapped memory)
    const uint32_t parent = pdz::guestmem::Ok(base, c, kParent + 4) ? Load32(base, c + kParent) : 0;
    if (!pdz::guestmem::Ok(base, parent, kParent + 4))
    {
      return 0;
    }
    if (Load32(base, parent) == kVehicleVtable)
    {
      return parent;
    }
    const uint32_t grand = Load32(base, parent + kParent);
    return pdz::guestmem::Ok(base, grand, 4) && Load32(base, grand) == kVehicleVtable ? grand : 0;
  }

  template <typename F> void EachCharacter(const uint8_t *base, F &&f)
  {
    uint32_t node = Load32(base, kCharList + 0x710);
    const uint32_t end = Load32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t c = Load32(base, node);
      node = Load32(base, node + 8);
      if (Ptr(c))
      {
        f(c);
      }
    }
  }

  // A live AI guard on this craft who is not its driver: the gunner.
  uint32_t GunnerOf(const uint8_t *base, uint32_t prop, uint32_t driver)
  {
    uint32_t found = 0;
    EachCharacter(base, [&](uint32_t c) {
      if (!found && c != driver && Load32(base, c) == kGuardVtable && CraftOf(base, c) == prop &&
          !(Load32(base, c + kHidden) & 0x10) && LoadFloat(base, c + kHealth) > 0.0f)
      {
        found = c;
      }
    });
    return found;
  }

  std::string SeatText(const uint8_t *base, uint32_t ctrl, int i)
  {
    const uint32_t seat = ctrl + uint32_t(i) * kSeatBytes;
    const uint32_t occupant = Load32(base, seat + kSeatOccupant);
    return fmt::format("seat {}: state {}, usable {} / {}, occupant {:08X} '{}'", i, Load32(base, seat + kSeatState),
                       base[seat + kSeatUsable0], base[seat + kSeatUsable1], occupant,
                       Ptr(occupant) ? Id(base, occupant) : std::string("-"));
  }

  // TESTING TOOL: the hovercraft table.
  void LogTable(const uint8_t *base, uint32_t p1c, uint32_t p2c)
  {
    std::vector<uint32_t> props;
    const auto add = [&](uint32_t prop) {
      if (HovercraftController(base, prop) && std::find(props.begin(), props.end(), prop) == props.end())
      {
        props.push_back(prop);
      }
    };
    const uint32_t world = Load32(base, kGame + 24);
    if (Ptr(world))
    {
      const uint32_t end = Load32(base, world + kWorldEnd);
      uint32_t node = Load32(base, world + kWorldFirst);
      for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
      {
        add(Load32(base, node + 4));
      }
    }
    const size_t in_world = props.size();
    std::string riders;
    int rider_count = 0;
    EachCharacter(base, [&](uint32_t c) {
      const uint32_t craft = CraftOf(base, c);
      if (!craft)
      {
        return;
      }
      add(craft);
      ++rider_count;
      const uint32_t parent = Load32(base, c + kParent);
      riders += fmt::format(" ['{}' {:08X} on {:08X}{} health {:.0f}{}]", Id(base, c), c, craft,
                            parent == craft ? std::string() : fmt::format(" (through its gun {:08X})", parent),
                            Health(base, c), (Load32(base, c + kHidden) & 0x10) ? " hidden" : "");
    });
    if (Ptr(p1c) && CraftOf(base, p1c))
    {
      add(CraftOf(base, p1c));
    }
    if (Ptr(p2c) && CraftOf(base, p2c))
    {
      add(CraftOf(base, p2c));
    }
    REXLOG_INFO("pdz_counterop: river: TABLE: {} hovercraft known ({} in the world list), {} riders:{}; the agent "
                "{:08X} at ({:.0f}, {:.0f}, {:.0f}) on craft {:08X}; player 2 {:08X} on craft {:08X}",
                props.size(), in_world, rider_count, riders.empty() ? std::string(" none") : riders, p1c,
                Ptr(p1c) ? LoadFloat(base, p1c + kPosition) : 0.0f, Ptr(p1c) ? LoadFloat(base, p1c + kPosition + 4) : 0.0f,
                Ptr(p1c) ? LoadFloat(base, p1c + kPosition + 8) : 0.0f, Ptr(p1c) ? CraftOf(base, p1c) : 0u, p2c,
                Ptr(p2c) ? CraftOf(base, p2c) : 0u);
    for (const uint32_t prop : props)
    {
      const uint32_t ctrl = HovercraftController(base, prop);
      REXLOG_INFO("pdz_counterop: river: TABLE: craft {:08X} at ({:.0f}, {:.0f}, {:.0f}), {:.0f} m from the agent; flags "
                  "{:08X}, health {:.0f}{}, rider word +0x600 {:08X}; controller {:08X} type {}, state {} asked {}, "
                  "callbacks {:08X} / {:08X}, dead driver {:08X}; {}; {}",
                  prop, LoadFloat(base, prop + kPropPosition), LoadFloat(base, prop + kPropPosition + 4),
                  LoadFloat(base, prop + kPropPosition + 8),
                  Ptr(p1c) ? Distance(base, prop + kPropPosition, p1c + kPosition) / 100.0f : -1.0f,
                  Load32(base, prop + kPropFlags), LoadFloat(base, prop + kPropHealth),
                  Destroyed(base, prop) ? " DESTROYED" : "", Load32(base, prop + 0x600), ctrl, Load32(base, ctrl + kCtrlType),
                  Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked), Load32(base, ctrl + kCtrlEnterCallback),
                  Load32(base, ctrl + kCtrlSeatedCallback), Load32(base, ctrl + kCtrlDeadDriver), SeatText(base, ctrl, 0),
                  SeatText(base, ctrl, 1));
    }
  }

  void Told(uint32_t guard, uint32_t reason, const std::string &text)
  {
    // Round 13b r: with the test log on, the same answer is told again every 15 s (the distances at each pick).
    const uint64_t slot = rs.hit_log ? uint64_t(std::chrono::duration_cast<std::chrono::seconds>(
                                                    Clock::now().time_since_epoch()).count() / 15 % 4096) : 0;
    if (rs.told.size() < 400 && rs.told.insert((slot << 40) | (uint64_t(guard) << 8) | reason).second)
    {
      REXLOG_INFO("pdz_counterop: river: pick: {}", text);
    }
  }

  // Round 13b r: the level script's prop number n (1-based: River's Setup reads [[level +0x2D8] +8] + n*8 - 4).
  uint32_t ScriptProp(const uint8_t *base, uint32_t n)
  {
    const uint32_t level = Load32(base, kGame + 0x1C);
    const uint32_t manager = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    if (!Ptr(manager) || Load32(base, manager + 4) < n)
    {
      return 0;
    }
    const uint32_t list = Load32(base, manager + 8);
    return Ptr(list) ? Load32(base, list + n * 8 - 4) : 0;
  }

  // Round 13b r: Jack (River's script +0x558, the handle Setup fills from the id 'jack'), or 0.
  uint32_t Jack(const uint8_t *base)
  {
    const uint32_t level = Load32(base, kGame + 0x1C);
    const uint32_t manager = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, manager, 0x74 + 4) ? Load32(base, manager + 0x74) : 0;
    if (!Ptr(script) || Load32(base, script) != 0x8209B5E4u)
    {
      return 0;
    }
    const uint32_t jack = Load32(base, script + 0x558);
    return Ptr(jack) ? jack : 0;
  }

  // Round 13b r: a character for a log line.
  std::string Who(const uint8_t *base, uint32_t c)
  {
    if (!Ptr(c))
    {
      return "none";
    }
    if (c == PdzCounterOpAgent())
    {
      return "the agent";
    }
    if (c == PdzCounterOpPlayer2())
    {
      return "player 2";
    }
    return fmt::format("'{}' {:08X}", Id(base, c), c);
  }

  // Round 13b r: the agent holds a seat of a hovercraft (not: she stands on its deck).
  bool AgentSeated(const uint8_t *base, uint32_t agent)
  {
    const uint32_t ctrl = Ptr(agent) ? HovercraftController(base, Load32(base, agent + kParent)) : 0;
    return ctrl && SeatOf(base, ctrl, agent) >= 0;
  }
  // Round 13b r: what "she is in a hovercraft" means for the round 13 rule (see the cvar).
  bool AgentAboard(const uint8_t *base, uint32_t agent)
  {
    if (REXCVAR_GET(pdz_counterop_river_hovercraft_seated_only))
    {
      return AgentSeated(base, agent);
    }
    return Ptr(agent) && HovercraftController(base, Load32(base, agent + kParent)) != 0;
  }
  std::atomic<int32_t> g_drive_ly{0}, g_drive_rx{0};
  std::atomic<bool> g_drive_on{false};

  void EndRide(uint8_t *base, const char *why)
  {
    if (!rs.riding)
    {
      return;
    }
    const uint32_t ctrl = HovercraftController(base, rs.riding);
    if (ctrl)
    {
      base[ctrl + kSeatUsable0] = rs.usable_before[0];
      base[ctrl + kSeatUsable1] = rs.usable_before[1];
    }
    PdzCounterOpVehicleAllow(rs.riding, false);
    REXLOG_INFO("pdz_counterop: river: player 2's hovercraft {:08X} is no longer his ({}): the driver seat's usable "
                "bytes are back at {} / {}, the craft is closed to him again",
                rs.riding, why, rs.usable_before[0], rs.usable_before[1]);
    rs.riding = 0;
    rs.rider = 0;
  }
} // namespace

namespace counter_op_river
{
  // Round 13b r: TESTING TOOL, counter_op_triggers.cpp's Setup hook, before Rare's call.
  void PreSetup(uint8_t *base)
  {
    if (!REXCVAR_GET(pdz_counterop_test_river_checkpoint) || !PdzCounterOpOn())
    {
      return;
    }
    std::string raw;
    if (pdz::pace::SpLevelName(base, &raw) != "river")
    {
      return;
    }
    const uint8_t before = base[0x82E426F8u];
    base[0x82E426F8u] = uint8_t(before | 0x20);
    REXLOG_INFO("pdz_counterop: river: TEST TOOL: River starts from Rare's mid-level checkpoint (byte 82E426F8 {:02X} -> "
                "{:02X} before the level script's Setup)", before, base[0x82E426F8u]);
  }

  // Round 13b r: TEST LOG (pdz_counterop_test_river_log above 0): who hits whom on the lakes.
  HitScope::HitScope(const uint8_t *base_, uint32_t victim_, uint32_t attacker_)
      : base(base_), victim(victim_), attacker(attacker_), before(0.0f), on(rs.river && rs.hit_log && Ptr(victim_))
  {
    if (on)
    {
      before = Health(base, victim);
    }
  }
  HitScope::~HitScope()
  {
    if (!on)
    {
      return;
    }
    const uint32_t agent = PdzCounterOpAgent(), p2 = PdzCounterOpPlayer2();
    const uint32_t owner = !Ptr(attacker) ? 0
                           : (attacker == agent || attacker == p2) ? attacker
                                                                   : PdzCounterOpRules13OwnerOf(base, attacker);
    const uint32_t agent_craft = Ptr(agent) ? CraftOf(base, agent) : 0;
    const uint32_t owner_craft = Ptr(owner) ? CraftOf(base, owner) : 0, victim_craft = CraftOf(base, victim);
    int kind = -1;
    if (victim == agent)
    {
      kind = 0;
    }
    else if (agent_craft && owner_craft == agent_craft && owner != agent)
    {
      kind = 1;
    }
    else if (victim == p2 || (rs.riding && victim_craft == rs.riding))
    {
      kind = 2;
    }
    if (kind < 0)
    {
      return;
    }
    const float after = Health(base, victim);
    State::Shooter &s = (kind == 0 ? rs.on_agent : kind == 1 ? rs.by_agent_craft : rs.on_p2_craft)[owner];
    ++s.hits;
    if (after < before)
    {
      ++s.counted;
      s.damage += before - after;
    }
    if (++rs.hit_lines[kind] > 80 && rs.hit_lines[kind] % 50 != 0)
    {
      return;
    }
    static constexpr const char *kKind[3] = {"on the agent", "from the agent's craft", "on player 2's craft"};
    REXLOG_INFO("pdz_counterop: river: HIT ({} #{}): {}{} hit by {}{} (source {:08X}, vtable {:08X}): health {:.1f} -> "
                "{:.1f}{}; {:.0f} m apart",
                kKind[kind], rs.hit_lines[kind], Who(base, victim),
                victim_craft ? fmt::format(" on craft {:08X}", victim_craft) : std::string(), Who(base, owner),
                owner_craft ? fmt::format(" on craft {:08X}", owner_craft) : std::string(), attacker,
                Ptr(attacker) ? Load32(base, attacker) : 0u, before, after, after < before ? "" : " (no damage)",
                Ptr(owner) ? Distance(base, owner + kPosition, victim + kPosition) / 100.0f : -1.0f);
  }

  // Round 13b r: counter_op.cpp's Pick, before the ordinary pick: this spot is a hovercraft driver DriverTake
  // accepted in this scan, and the agent sits in a hovercraft right now.
  bool DriverFirst(const uint8_t *base, uint32_t guard)
  {
    if (!rs.river || !REXCVAR_GET(pdz_counterop_river_hovercraft_first) || !rs.pending.count(guard))
    {
      return false;
    }
    const uint32_t agent = PdzCounterOpAgent();
    if (!AgentAboard(base, agent))
    {
      return false;
    }
    const uint32_t prop = rs.pending[guard];
    REXLOG_INFO("pdz_counterop: river: pick: the agent sits in a hovercraft: the driver '{}' {:08X} of the enemy hovercraft "
                "{:08X}, {:.0f} m from her, comes before any guard on foot (pdz_counterop_river_hovercraft_first)",
                Id(base, guard), guard, prop, Distance(base, prop + kPropPosition, agent + kPosition) / 100.0f);
    return true;
  }

  void Reset()
  {
    const uint32_t scratch = rs.scratch; // a test tool's 16 guest bytes, kept
    rs = State();
    rs.scratch = scratch;
    g_drive_on = false; // Sun 22 Nov night (audit): the test drive's pad writes never outlive the level
  }

  bool Driving(const uint8_t *base, uint32_t p2)
  {
    if (!rs.river || !Ptr(p2))
    {
      return false;
    }
    const uint32_t ctrl = HovercraftController(base, Load32(base, p2 + kParent));
    return ctrl && (SeatOf(base, ctrl, p2) >= 0 || Load32(base, ctrl + kCtrlDeadDriver) == p2);
  }

  bool DriverTake(PPCContext &ctx, uint8_t *base, uint32_t guard)
  {
    (void)ctx;
    if (!rs.river || !REXCVAR_GET(pdz_counterop_river_hovercraft) || !Ptr(guard) || Load32(base, guard) != kGuardVtable)
    {
      return false;
    }
    const uint32_t prop = Load32(base, guard + kParent);
    const uint32_t ctrl = HovercraftController(base, prop);
    if (!ctrl || Load32(base, ctrl + kSeatOccupant) != guard)
    {
      return false; // on foot, a gunner, or on something else: the denied list's answer stands
    }
    rs.pending.erase(guard);
    const std::string who = fmt::format("hovercraft driver '{}' {:08X} (craft {:08X})", Id(base, guard), guard, prop);
    const uint32_t seat_state = Load32(base, ctrl + kSeatState), vehicle = Load32(base, ctrl + kCtrlState);
    if (seat_state != 3 || (Load32(base, guard + kHidden) & 0x10) || LoadFloat(base, guard + kHealth) <= 0.0f)
    {
      Told(guard, 1, fmt::format("{} is not taken: not settled in the seat (seat state {}, hidden {}, health {:.0f})", who,
                                 seat_state, (Load32(base, guard + kHidden) & 0x10) ? 1 : 0,
                                 LoadFloat(base, guard + kHealth)));
      return false;
    }
    if (Destroyed(base, prop) || vehicle == 3)
    {
      Told(guard, 2, fmt::format("{} is not taken: the craft is destroyed (state {})", who, vehicle));
      return false;
    }
    const uint32_t gunner = GunnerOf(base, prop, guard);
    if (!gunner && REXCVAR_GET(pdz_counterop_river_hovercraft_need_gunner))
    {
      Told(guard, 3, fmt::format("{} is not taken: no live AI gunner on the craft (a driver alone has no weapon)", who));
      return false;
    }
    const uint32_t agent = PdzCounterOpAgent();
    const double range = REXCVAR_GET(pdz_counterop_river_hovercraft_range_m);
    const float d = Ptr(agent) ? Distance(base, prop + kPropPosition, agent + kPosition) / 100.0f : -1.0f;
    if (range > 0.0 && !rs.agent_rode)
    {
      Told(guard, 6, fmt::format("{} is not taken yet: the agent has not sat in a hovercraft yet (she is not out on "
                                 "the lakes); the craft is {:.0f} m from her", who, d));
      return false;
    }
    if (range > 0.0 && (d < 0.0f || d > float(range)))
    {
      Told(guard, 4, fmt::format("{} is not taken yet: the craft is {:.0f} m from the agent (limit {:.0f} m, "
                                 "pdz_counterop_river_hovercraft_range_m)", who, d, range));
      return false;
    }
    Told(guard, 5, fmt::format("{} may be taken in his seat: craft state {}, gunner '{}' {:08X}, {:.0f} m from the agent",
                               who, vehicle, gunner ? Id(base, gunner) : std::string("none"), gunner, d));
    rs.pending[guard] = prop;
    return true;
  }

  void SeatSpot(const uint8_t *base, uint32_t guard, float at[3])
  {
    const auto it = rs.pending.find(guard);
    if (!rs.river || it == rs.pending.end() || !HovercraftController(base, it->second))
    {
      return;
    }
    const float was[3] = {at[0], at[1], at[2]};
    at[0] = LoadFloat(base, it->second + kPropPosition);
    at[1] = LoadFloat(base, it->second + kPropPosition + 4) + 120.0f;
    at[2] = LoadFloat(base, it->second + kPropPosition + 8);
    REXLOG_INFO("pdz_counterop: river: the spot of the seated driver '{}' is his craft {:08X}: ({:.0f}, {:.0f}, {:.0f}), "
                "1.2 m above its centre, not the guard's own origin ({:.0f}, {:.0f}, {:.0f}) (under the water line: the "
                "lake kills a character on foot)",
                Id(base, guard), it->second, at[0], at[1], at[2], was[0], was[1], was[2]);
  }

  void Unseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
  {
    rs.handing = 0;
    const auto it = rs.pending.find(guard);
    if (it == rs.pending.end())
    {
      rs.pending.clear();
      return;
    }
    const uint32_t prop = it->second;
    rs.pending.clear();
    const uint32_t ctrl = HovercraftController(base, prop);
    if (!rs.river || !ctrl || !Ptr(p2) || Load32(base, ctrl + kSeatOccupant) != guard || Load32(base, ctrl + kSeatState) != 3)
    {
      REXLOG_WARN("pdz_counterop: river: NO hand-over: guard {:08X} is no longer settled in the driver's seat of {:08X} "
                  "(controller {:08X}): player 2 is that guard on foot",
                  guard, prop, ctrl);
      return;
    }
    EndRide(base, "a new hand-over");
    const uint32_t state0 = Load32(base, ctrl + kCtrlState), asked0 = Load32(base, ctrl + kCtrlAsked);
    rs.usable_before[0] = base[ctrl + kSeatUsable0];
    rs.usable_before[1] = base[ctrl + kSeatUsable1];
    Call(ctx, base, sub_826745E0, ctrl, guard); // Rare's full removal of a driver
    if (Load32(base, ctrl + kSeatState) != 0 || Load32(base, ctrl + kSeatOccupant) == guard)
    {
      // not seen; what Rare's own leave path stores (82674580-8267458C)
      Store32(base, ctrl + kSeatState, 0);
      Call(ctx, base, sub_821433D8, ctrl + kSeatHandle, 0);
    }
    if (Load32(base, guard + kParent) == prop)
    {
      Call(ctx, base, sub_82519288, prop, guard);
    }
    // Rare's CanEnter wants both usable bytes (8266AD44-8266AD58); they go back when player 2's ride ends.
    base[ctrl + kSeatUsable0] = 1;
    base[ctrl + kSeatUsable1] = 1;
    rs.handing = prop;
    REXLOG_INFO("pdz_counterop: river: HAND-OVER of the hovercraft {:08X} at ({:.0f}, {:.0f}, {:.0f}) from driver '{}' "
                "{:08X}: before: craft state {} -> {}, usable {} / {}; after the driver left (Rare's 826745E0): seat "
                "state {}, occupant {:08X}, craft state {} -> {}, the guard's parent {:08X}; gunner '{}'",
                prop, LoadFloat(base, prop + kPropPosition), LoadFloat(base, prop + kPropPosition + 4),
                LoadFloat(base, prop + kPropPosition + 8), Id(base, guard), guard, state0, asked0, rs.usable_before[0],
                rs.usable_before[1], Load32(base, ctrl + kSeatState), Load32(base, ctrl + kSeatOccupant),
                Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked), Load32(base, guard + kParent),
                Id(base, GunnerOf(base, prop, guard)));
  }

  void AfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
  {
    const uint32_t prop = rs.handing;
    rs.handing = 0;
    if (!prop)
    {
      return;
    }
    const uint32_t ctrl = HovercraftController(base, prop);
    if (!ctrl || !Ptr(p2))
    {
      return;
    }
    const uint32_t state0 = Load32(base, ctrl + kCtrlState);
    const uint32_t flag = REXCVAR_GET(pdz_counterop_river_hovercraft_seated_at_once) ? 1u : 0u;
    PdzCounterOpVehicleAllow(prop, true); // G2's exception: this craft is the guard's he was given
    const uint32_t ok = Call(ctx, base, __imp__sub_8266A8E8, ctrl, p2, 0, flag) & 0xFF;
    if (!ok)
    {
      base[ctrl + kSeatUsable0] = rs.usable_before[0];
      base[ctrl + kSeatUsable1] = rs.usable_before[1];
      PdzCounterOpVehicleAllow(prop, false);
      REXLOG_WARN("pdz_counterop: river: Rare's mount REFUSED player 2 {:08X} on the hovercraft {:08X} (controller {:08X}, "
                  "craft state {}, enter callback {:08X}, {}): he stands on the craft as guard '{}' on foot",
                  p2, prop, ctrl, state0, Load32(base, ctrl + kCtrlEnterCallback), SeatText(base, ctrl, 0), Id(base, guard));
      return;
    }
    // A craft under way (state 2) seats a human on the next tick; one asked down to state 1 would want Use held
    // (8266A410-8266A4A0) and cancel the entry. The asked state goes back to what the craft had (the Datacore step
    // of the jetpack study).
    const uint32_t asked1 = Load32(base, ctrl + kCtrlAsked);
    if (state0 == 2 && asked1 != 2)
    {
      Store32(base, ctrl + kCtrlAsked, 2);
    }
    rs.riding = prop;
    rs.rider = p2;
    rs.last_seat_state = Load32(base, ctrl + kSeatState);
    rs.last_vehicle_state = Load32(base, ctrl + kCtrlState);
    rs.last_asked = Load32(base, ctrl + kCtrlAsked);
    rs.seated_told = rs.left_told = false;
    rs.mounted_at = Clock::now();
    rs.last_track = {};
    rs.agent_health = -1.0f;
    REXLOG_INFO("pdz_counterop: river: player 2 {:08X} mounts the hovercraft {:08X} as its driver in place of '{}' (Rare's "
                "mount, flag {}): accepted; {}; craft state {} -> {} (the mount asked {}), his parent {:08X}",
                p2, prop, Id(base, guard), flag, SeatText(base, ctrl, 0), Load32(base, ctrl + kCtrlState),
                Load32(base, ctrl + kCtrlAsked), asked1, Load32(base, p2 + kParent));
  }

  void LeaveSeat(PPCContext &ctx, uint8_t *base, uint32_t p2, const char *why)
  {
    if (!rs.river || !Ptr(p2))
    {
      return;
    }
    const uint32_t prop = Load32(base, p2 + kParent);
    const uint32_t ctrl = HovercraftController(base, prop);
    if (!ctrl)
    {
      if (rs.riding && rs.rider == p2)
      {
        EndRide(base, why ? why : "he is out of the world");
      }
      return;
    }
    const int seat = SeatOf(base, ctrl, p2);
    const uint32_t seat_at = ctrl + uint32_t(seat >= 0 ? seat : 0) * kSeatBytes;
    const bool dead_driver = Load32(base, ctrl + kCtrlDeadDriver) == p2;
    const uint32_t state0 = Load32(base, ctrl + kCtrlState), seat_state0 = seat >= 0 ? Load32(base, seat_at + kSeatState) : 0;
    if (seat >= 0 && !dead_driver)
    {
      Call(ctx, base, sub_82674478, ctrl, p2); // Rare's leave: removal, and a craft under way is asked to stop
    }
    else
    {
      Call(ctx, base, sub_826745E0, ctrl, p2);
    }
    if (seat >= 0 && Load32(base, seat_at + kSeatOccupant) == p2 && Load32(base, seat_at + kSeatState) != 0)
    {
      Store32(base, seat_at + kSeatState, 0);
      Call(ctx, base, sub_821433D8, seat_at + kSeatHandle, 0);
    }
    if (Load32(base, p2 + kParent) == prop)
    {
      Call(ctx, base, sub_82519288, prop, p2);
    }
    REXLOG_INFO("pdz_counterop: river: player 2 {:08X} leaves the hovercraft {:08X} before he goes out of the world ({}): "
                "he was in seat {} (state {}{}), craft state {}; now {}, craft state {} -> {}, his parent {:08X}",
                p2, prop, why ? why : "-", seat, seat_state0, dead_driver ? ", the craft's dead driver" : "", state0,
                SeatText(base, ctrl, seat >= 0 ? seat : 0), Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked),
                Load32(base, p2 + kParent));
    if (rs.riding)
    {
      EndRide(base, why ? why : "he is out of the world");
    }
  }

  void Tick(PPCContext &ctx, uint8_t *base, const std::string &mission, uint32_t p1c, uint32_t p2c, bool out)
  {
    rs.river = mission == "river";
    if (!rs.river)
    {
      return;
    }
    const auto now = Clock::now();
    if (!rs.agent_rode && AgentAboard(base, p1c))
    {
      rs.agent_rode = true;
      REXLOG_INFO("pdz_counterop: river: the agent sits in a hovercraft ({:08X}): she is out on the lakes; from now on "
                  "an enemy hovercraft's driver within {:.0f} m of her may be taken over (pdz_counterop_river_hovercraft "
                  "{})", Load32(base, p1c + kParent), REXCVAR_GET(pdz_counterop_river_hovercraft_range_m),
                  REXCVAR_GET(pdz_counterop_river_hovercraft) ? "on" : "off");
    }
    if (REXCVAR_GET(pdz_counterop_test_river_agent_refill) && Ptr(p1c))
    {
      const uint32_t participant = Load32(base, p1c + kParticipantOf);
      if (Ptr(participant) && LoadFloat(base, participant + kPartHealth) < 50.0f && LoadFloat(base, participant + kPartHealth) > 0.0f)
      {
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: the agent's health {:.1f} -> 100 (refill #{})",
                    LoadFloat(base, participant + kPartHealth), ++rs.refills);
        StoreFloat(base, participant + kPartHealth, 100.0f);
      }
    }
    const double every = REXCVAR_GET(pdz_counterop_test_river_log);
    rs.hit_log = every > 0.0;
    // Round 13b r: the agent's own seat (one line per change).
    {
      const uint32_t craft = Ptr(p1c) ? Load32(base, p1c + kParent) : 0;
      const uint32_t ctrl = HovercraftController(base, craft);
      const int seat = ctrl ? SeatOf(base, ctrl, p1c) : -1;
      const uint32_t seat_state = seat >= 0 ? Load32(base, ctrl + uint32_t(seat) * kSeatBytes + kSeatState) : 0;
      const uint32_t state = ctrl ? Load32(base, ctrl + kCtrlState) : 0, asked = ctrl ? Load32(base, ctrl + kCtrlAsked) : 0;
      const uint32_t on = ctrl ? craft : 0;
      if (on != rs.agent_craft || seat != rs.agent_seat || seat_state != rs.agent_seat_state || state != rs.agent_craft_state ||
          asked != rs.agent_craft_asked)
      {
        const uint32_t jack = Jack(base);
        REXLOG_INFO("pdz_counterop: river: the agent's seat: craft {:08X} -> {:08X}{}, seat {} -> {}, seat state {} -> {}, craft "
                    "state {} -> {} (asked {} -> {}); {}; Jack {:08X} '{}' {:.0f} m from her, on craft {:08X}",
                    rs.agent_craft, on, on && on == ScriptProp(base, 7) ? " (script prop 7, her own)" : "", rs.agent_seat, seat,
                    rs.agent_seat_state, seat_state, rs.agent_craft_state, state, rs.agent_craft_asked, asked,
                    ctrl ? SeatText(base, ctrl, 0) + "; " + SeatText(base, ctrl, 1) : std::string("on foot"), jack,
                    jack ? Id(base, jack) : std::string("-"),
                    jack && Ptr(p1c) ? Distance(base, jack + kPosition, p1c + kPosition) / 100.0f : -1.0f,
                    jack ? CraftOf(base, jack) : 0u);
        rs.agent_craft = on;
        rs.agent_seat = seat;
        rs.agent_seat_state = seat_state;
        rs.agent_craft_state = state;
        rs.agent_craft_asked = asked;
      }
    }
    // Round 13b r: TESTING TOOL: the agent boards her own hovercraft.
    const int board = REXCVAR_GET(pdz_counterop_test_river_agent_board);
    if (board != rs.board_was && board > 0 && Ptr(p1c))
    {
      const uint32_t prop = ScriptProp(base, 7);
      const uint32_t ctrl = HovercraftController(base, prop);
      const uint32_t jack = Jack(base);
      if (!ctrl)
      {
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_agent_board {}: script prop 7 ({:08X}) is "
                    "not a hovercraft: nothing done", board, prop);
      }
      else if (AgentSeated(base, p1c))
      {
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_agent_board {}: the agent is already in a "
                    "hovercraft ({:08X}; her own is {:08X}): nothing done", board, Load32(base, p1c + kParent), prop);
      }
      else if (board == 1)
      {
        if (!rs.scratch)
        {
          rs.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
        }
        const float was[3] = {LoadFloat(base, p1c + kPosition), LoadFloat(base, p1c + kPosition + 4),
                              LoadFloat(base, p1c + kPosition + 8)};
        if (rs.scratch)
        {
          StoreFloat(base, rs.scratch, LoadFloat(base, prop + kPropPosition));
          StoreFloat(base, rs.scratch + 4, LoadFloat(base, prop + kPropPosition + 4) + 120.0f);
          StoreFloat(base, rs.scratch + 8, LoadFloat(base, prop + kPropPosition + 8));
          Call(ctx, base, sub_825AD148, p1c, rs.scratch, 0);
        }
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: the agent moved from ({:.0f}, {:.0f}, {:.0f}) onto her own hovercraft "
                    "{:08X} (script prop 7) at ({:.0f}, {:.0f}, {:.0f}): she is now at ({:.0f}, {:.0f}, {:.0f}); craft state {} "
                    "asked {}, enter callback {:08X}; {}; {}; Jack {:08X} {:.0f} m from her, on craft {:08X}",
                    was[0], was[1], was[2], prop, LoadFloat(base, prop + kPropPosition),
                    LoadFloat(base, prop + kPropPosition + 4), LoadFloat(base, prop + kPropPosition + 8),
                    LoadFloat(base, p1c + kPosition), LoadFloat(base, p1c + kPosition + 4),
                    LoadFloat(base, p1c + kPosition + 8), Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked),
                    Load32(base, ctrl + kCtrlEnterCallback), SeatText(base, ctrl, 0), SeatText(base, ctrl, 1), jack,
                    jack ? Distance(base, jack + kPosition, p1c + kPosition) / 100.0f : -1.0f, jack ? CraftOf(base, jack) : 0u);
      }
      else
      {
        const std::string before = SeatText(base, ctrl, 0);
        if (board >= 3)
        {
          base[ctrl + kSeatUsable0] = 1;
          base[ctrl + kSeatUsable1] = 1;
        }
        const uint32_t state0 = Load32(base, ctrl + kCtrlState), asked0 = Load32(base, ctrl + kCtrlAsked);
        // 2 = Rare's Use of the vehicle prop (825D2E90(prop, user): what her Use press at the craft calls; it picks
        // the seat and asks the mount). 3 = Rare's mount itself, seat 0.
        uint32_t ok = 0;
        if (board == 2)
        {
          Call(ctx, base, sub_825D2E90, prop, p1c);
          ok = Load32(base, p1c + kParent) == prop ? 1u : 0u;
        }
        else
        {
          ok = Call(ctx, base, __imp__sub_8266A8E8, ctrl, p1c, 0, 0) & 0xFF;
        }
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: {} for the agent (controller {:08X}) on "
                    "her own hovercraft {:08X}{}: {}; before: {}, craft state {} asked {}; after: {}, craft state {} asked {}, "
                    "her parent {:08X}",
                    board == 2 ? "Rare's Use of the craft 825D2E90(prop, the agent)" : "Rare's mount 8266A8E8(controller, the agent, seat 0, flag 0)",
                    ctrl, prop, board >= 3 ? " (the driver seat's usable bytes set first)" : "", ok ? "ACCEPTED" : "REFUSED",
                    before, state0, asked0, SeatText(base, ctrl, 0), Load32(base, ctrl + kCtrlState),
                    Load32(base, ctrl + kCtrlAsked), Load32(base, p1c + kParent));
      }
    }
    rs.board_was = board;
    // Round 13b r: TESTING TOOL: player 2's death, wherever he is.
    const int kill_p2 = REXCVAR_GET(pdz_counterop_test_river_kill_p2);
    if (kill_p2 != rs.kill_p2_was && kill_p2 > 0)
    {
      const uint32_t participant = Ptr(p2c) && !out ? Load32(base, p2c + kParticipantOf) : 0;
      if (Ptr(participant))
      {
        const float before = LoadFloat(base, participant + kPartHealth);
        StoreFloat(base, participant + kPartHealth, kOwnFloor);
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_kill_p2 {}: player 2 ({}): health {:.1f} -> "
                    "{:.1f} (his floor: counter_op.cpp's own death runs on its next check)", kill_p2,
                    Driving(base, p2c) ? "in a hovercraft seat" : "on foot", before, kOwnFloor);
      }
      else
      {
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_kill_p2 {}: player 2 is not in the world: "
                    "nothing done", kill_p2);
      }
    }
    rs.kill_p2_was = kill_p2;
    // Round 13b r: TESTING TOOL: the agent drives her craft to a point (pad 1's sticks; see PdzCounterOpRiverTestPad).
    {
      const std::string to = REXCVAR_GET(pdz_counterop_test_river_agent_drive);
      if (to != rs.drive_was)
      {
        rs.drive_was = to;
        rs.drive = std::sscanf(to.c_str(), "%f,%f", &rs.drive_x, &rs.drive_z) == 2;
        rs.drive_since = rs.drive_sample = rs.drive_check = now;
        rs.drive_told = {};
        rs.drive_sign = 1.0f;
        rs.drive_err = rs.drive_prev_err = 0.0f;
        const uint32_t craft0 = Ptr(p1c) ? Load32(base, p1c + kParent) : 0;
        for (int i = 0; i < 3; ++i)
        {
          rs.drive_pos[i] = HovercraftController(base, craft0) ? LoadFloat(base, craft0 + kPropPosition + 4 * uint32_t(i)) : 0.0f;
        }
      }
      const uint32_t craft = Ptr(p1c) ? Load32(base, p1c + kParent) : 0;
      const uint32_t ctrl = HovercraftController(base, craft);
      if (rs.drive && ctrl && SeatOf(base, ctrl, p1c) == 0)
      {
        const float x = LoadFloat(base, craft + kPropPosition), y = LoadFloat(base, craft + kPropPosition + 4),
                    z = LoadFloat(base, craft + kPropPosition + 8);
        const float dx = rs.drive_x - x, dz = rs.drive_z - z, d = std::sqrt(dx * dx + dz * dz);
        const double since = std::chrono::duration<double>(now - rs.drive_since).count();
        const double dt = std::chrono::duration<double>(now - rs.drive_sample).count();
        static float speed = 0.0f;
        if (dt >= 0.25)
        {
          const float vx = x - rs.drive_pos[0], vz = z - rs.drive_pos[2];
          speed = std::sqrt(vx * vx + vz * vz) / 100.0f / float(dt);
          if (speed > 2.0f)
          {
            float err = std::atan2(dx, dz) - std::atan2(vx, vz);
            while (err > 3.14159265f) { err -= 6.2831853f; }
            while (err < -3.14159265f) { err += 6.2831853f; }
            rs.drive_err = err;
          }
          else
          {
            rs.drive_err = 0.0f;
          }
          rs.drive_pos[0] = x;
          rs.drive_pos[1] = y;
          rs.drive_pos[2] = z;
          rs.drive_sample = now;
        }
        if (std::chrono::duration<double>(now - rs.drive_check).count() >= 4.0)
        {
          if (std::fabs(rs.drive_err) > 0.7f && std::fabs(rs.drive_err) >= std::fabs(rs.drive_prev_err) - 0.09f)
          {
            rs.drive_sign = -rs.drive_sign; // the turn made it worse: the other way
          }
          rs.drive_prev_err = rs.drive_err;
          rs.drive_check = now;
        }
        const bool arrived = d < 2000.0f, late = since >= 30.0;
        if (arrived || late)
        {
          if (!arrived)
          {
            if (!rs.scratch)
            {
              rs.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
            }
            if (rs.scratch)
            {
              StoreFloat(base, rs.scratch, rs.drive_x);
              StoreFloat(base, rs.scratch + 4, 100.0f);
              StoreFloat(base, rs.scratch + 8, rs.drive_z);
              Call(ctx, base, sub_82518768, craft, rs.scratch);
            }
          }
          REXLOG_INFO("pdz_counterop: river: TEST TOOL: drive: {} after {:.1f} s: the agent's craft {:08X} was at ({:.0f}, {:.0f}, "
                      "{:.0f}), {:.0f} m from the point ({:.0f}, {:.0f}); it is now at ({:.0f}, {:.0f}, {:.0f}); the sticks are "
                      "let go",
                      arrived ? "ARRIVED by driving" : "NOT arrived, the craft is placed there (Rare's 82518768)", since, craft,
                      x, y, z, d / 100.0f, rs.drive_x, rs.drive_z, LoadFloat(base, craft + kPropPosition),
                      LoadFloat(base, craft + kPropPosition + 4), LoadFloat(base, craft + kPropPosition + 8));
          rs.drive = false;
        }
        else
        {
          g_drive_ly = 26000;
          g_drive_rx = int32_t(std::clamp(rs.drive_sign * rs.drive_err * 30000.0f, -26000.0f, 26000.0f));
          if (std::chrono::duration<double>(now - rs.drive_told).count() >= 2.0)
          {
            rs.drive_told = now;
            REXLOG_INFO("pdz_counterop: river: TEST TOOL: drive: {:.1f} s: the agent's craft at ({:.0f}, {:.0f}, {:.0f}), {:.0f} m "
                        "from the point, {:.1f} m/s, heading error {:.0f} deg, right stick {} (sign {:+.0f}), craft state {}",
                        since, x, y, z, d / 100.0f, speed, rs.drive_err * 57.29578f, g_drive_rx.load(), rs.drive_sign,
                        Load32(base, ctrl + kCtrlState));
          }
        }
      }
      g_drive_on = rs.drive && ctrl && SeatOf(base, ctrl, p1c) == 0;
    }
    // Round 13b r: TESTING TOOL: the agent's craft is placed somewhere on the lakes.
    const std::string craft_to = REXCVAR_GET(pdz_counterop_test_river_agent_craft_to);
    if (craft_to != rs.craft_to_was)
    {
      rs.craft_to_was = craft_to;
      float x = 0.0f, y = 0.0f, z = 0.0f;
      const uint32_t craft = Ptr(p1c) ? Load32(base, p1c + kParent) : 0;
      if (std::sscanf(craft_to.c_str(), "%f,%f,%f", &x, &y, &z) == 3 && HovercraftController(base, craft))
      {
        if (!rs.scratch)
        {
          rs.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
        }
        const float was[3] = {LoadFloat(base, craft + kPropPosition), LoadFloat(base, craft + kPropPosition + 4),
                              LoadFloat(base, craft + kPropPosition + 8)};
        if (rs.scratch)
        {
          StoreFloat(base, rs.scratch, x);
          StoreFloat(base, rs.scratch + 4, y);
          StoreFloat(base, rs.scratch + 8, z);
          Call(ctx, base, sub_82518768, craft, rs.scratch);
        }
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: the agent's hovercraft {:08X} moved (Rare's 82518768) from ({:.0f}, "
                    "{:.0f}, {:.0f}) to ({:.0f}, {:.0f}, {:.0f}); it is now at ({:.0f}, {:.0f}, {:.0f})",
                    craft, was[0], was[1], was[2], x, y, z, LoadFloat(base, craft + kPropPosition),
                    LoadFloat(base, craft + kPropPosition + 4), LoadFloat(base, craft + kPropPosition + 8));
      }
      else if (!craft_to.empty())
      {
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_agent_craft_to '{}': the agent is not in a "
                    "hovercraft (or the value is not x,y,z): nothing done", craft_to);
      }
    }
    // Round 13b r: TEST LOG: every gunner on a hovercraft's gun (Jack and the enemy's), every 2 s once the agent rode.
    if (every > 0.0 && rs.agent_rode && std::chrono::duration<double>(now - rs.last_guns).count() >= 2.0)
    {
      const double dt = rs.last_guns == Clock::time_point{} ? 0.0 : std::chrono::duration<double>(now - rs.last_guns).count();
      rs.last_guns = now;
      std::string text;
      std::unordered_map<uint32_t, std::array<float, 3>> seen;
      const auto speed = [&](uint32_t craft) {
        const std::array<float, 3> pos = {LoadFloat(base, craft + kPropPosition), LoadFloat(base, craft + kPropPosition + 4),
                                          LoadFloat(base, craft + kPropPosition + 8)};
        seen[craft] = pos;
        const auto it = rs.craft_pos.find(craft);
        if (it == rs.craft_pos.end() || dt <= 0.0 || dt > 10.0)
        {
          return -1.0f;
        }
        const float dx = pos[0] - it->second[0], dy = pos[1] - it->second[1], dz = pos[2] - it->second[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f / float(dt);
      };
      const auto tally = [&](const std::unordered_map<uint32_t, State::Shooter> &m, uint32_t c) {
        const auto it = m.find(c);
        return it == m.end() ? std::string("0")
                             : fmt::format("{} ({} counted, {:.0f} health)", it->second.hits, it->second.counted,
                                           it->second.damage);
      };
      EachCharacter(base, [&](uint32_t c) {
        const uint32_t craft = CraftOf(base, c);
        const uint32_t ctrl = HovercraftController(base, craft);
        if (!ctrl || Load32(base, c + kParent) == craft || c == p1c || c == p2c)
        {
          return; // on foot, or a driver: only the characters attached through a gun
        }
        const uint32_t driver = Load32(base, ctrl + kSeatOccupant);
        text += fmt::format(" [gunner {} health {:.0f} on craft {:08X} (driver {}, {:.1f} m/s, state {}), {:.0f} m from the "
                            "agent, sight words {:08X} / {:08X}; his hits so far: on the agent {}, from the agent's craft {}]",
                            Who(base, c), Health(base, c), craft,
                            Load32(base, ctrl + kSeatState) ? Who(base, driver) : std::string("none"), speed(craft),
                            Load32(base, ctrl + kCtrlState),
                            Ptr(p1c) ? Distance(base, c + kPosition, p1c + kPosition) / 100.0f : -1.0f, Load32(base, c + 6268),
                            Load32(base, c + 6252), tally(rs.on_agent, c), tally(rs.by_agent_craft, c));
      });
      rs.craft_pos = seen;
      const uint32_t jack = Jack(base);
      REXLOG_INFO("pdz_counterop: river: GUNS:{}; Jack {:08X} health {:.0f}, {:.0f} m from the agent, on craft {:08X}; the "
                  "agent's health {:.1f}, her craft {:08X}",
                  text.empty() ? std::string(" no gunner on any hovercraft") : text, jack, jack ? Health(base, jack) : 0.0f,
                  jack && Ptr(p1c) ? Distance(base, jack + kPosition, p1c + kPosition) / 100.0f : -1.0f,
                  jack ? CraftOf(base, jack) : 0u, Health(base, p1c), Ptr(p1c) ? CraftOf(base, p1c) : 0u);
    }
    if (every > 0.0 && std::chrono::duration<double>(now - rs.last_table).count() >= every)
    {
      rs.last_table = now;
      LogTable(base, p1c, p2c);
    }
    // TESTING TOOL: his death in the seat.
    const bool kill = REXCVAR_GET(pdz_counterop_test_river_kill_driver);
    if (kill && !rs.kill_was)
    {
      const uint32_t participant = Ptr(p2c) ? Load32(base, p2c + kParticipantOf) : 0;
      if (Driving(base, p2c) && Ptr(participant))
      {
        const float before = LoadFloat(base, participant + kPartHealth);
        StoreFloat(base, participant + kPartHealth, kOwnFloor);
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: player 2 in his hovercraft seat: health {:.1f} -> {:.1f} (his "
                    "floor: counter_op.cpp's own death runs on its next check)", before, kOwnFloor);
      }
      else
      {
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_kill_driver: player 2 is not in a "
                    "hovercraft seat: nothing done");
      }
    }
    rs.kill_was = kill;
    // TESTING TOOL: the agent beside a crewed enemy craft.
    const double near_m = REXCVAR_GET(pdz_counterop_test_river_agent_near);
    if (near_m > 0.0 && near_m != rs.near_was && Ptr(p1c))
    {
      uint32_t craft = rs.riding;
      float best = 1e30f;
      if (!craft)
      {
        EachCharacter(base, [&](uint32_t c) {
          const uint32_t prop = Load32(base, c + kParent);
          const uint32_t ctrl = HovercraftController(base, prop);
          if (ctrl && Load32(base, c) == kGuardVtable && Load32(base, ctrl + kSeatOccupant) == c)
          {
            const float d = Distance(base, prop + kPropPosition, p1c + kPosition);
            if (d < best)
            {
              best = d;
              craft = prop;
            }
          }
        });
      }
      if (!rs.scratch)
      {
        rs.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
      }
      if (craft && rs.scratch)
      {
        StoreFloat(base, rs.scratch, LoadFloat(base, craft + kPropPosition) + float(near_m) * 100.0f);
        StoreFloat(base, rs.scratch + 4, LoadFloat(base, craft + kPropPosition + 4) + 100.0f);
        StoreFloat(base, rs.scratch + 8, LoadFloat(base, craft + kPropPosition + 8));
        Call(ctx, base, sub_825AD148, p1c, rs.scratch, 0);
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: the agent moved {:.0f} m beside the hovercraft {:08X}: she is now "
                    "at ({:.0f}, {:.0f}, {:.0f}), the craft at ({:.0f}, {:.0f}, {:.0f})",
                    near_m, craft, LoadFloat(base, p1c + kPosition), LoadFloat(base, p1c + kPosition + 4),
                    LoadFloat(base, p1c + kPosition + 8), LoadFloat(base, craft + kPropPosition),
                    LoadFloat(base, craft + kPropPosition + 4), LoadFloat(base, craft + kPropPosition + 8));
      }
      else
      {
        REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_agent_near: no crewed hovercraft found");
      }
    }
    rs.near_was = near_m;
    if (!rs.riding)
    {
      return;
    }
    const uint32_t ctrl = HovercraftController(base, rs.riding);
    if (!ctrl || !Ptr(p2c) || p2c != rs.rider)
    {
      EndRide(base, "the craft or his character is gone");
      return;
    }
    const int seat = SeatOf(base, ctrl, p2c);
    const uint32_t seat_state = seat >= 0 ? Load32(base, ctrl + uint32_t(seat) * kSeatBytes + kSeatState) : 0;
    const uint32_t vehicle = Load32(base, ctrl + kCtrlState), asked = Load32(base, ctrl + kCtrlAsked);
    const double since = std::chrono::duration<double>(now - rs.mounted_at).count();
    if (seat_state != rs.last_seat_state || vehicle != rs.last_vehicle_state || asked != rs.last_asked)
    {
      REXLOG_INFO("pdz_counterop: river: player 2's hovercraft {:08X}, {:.1f} s after the mount: his seat {} state {} -> "
                  "{}, craft state {} -> {} (asked {} -> {}), his parent {:08X}, gunner '{}'; craft flags {:08X}, craft "
                  "health {:.0f}, Rare's destroyed test {}",
                  rs.riding, since, seat, rs.last_seat_state, seat_state, rs.last_vehicle_state, vehicle, rs.last_asked,
                  asked, Load32(base, p2c + kParent), Id(base, GunnerOf(base, rs.riding, p2c)),
                  Load32(base, rs.riding + kPropFlags), LoadFloat(base, rs.riding + kPropHealth),
                  Destroyed(base, rs.riding) ? "yes" : "no");
      rs.last_seat_state = seat_state;
      rs.last_vehicle_state = vehicle;
      rs.last_asked = asked;
    }
    // Mon 23 Nov night (1.0.1, pdz_counterop_river_dies_with_craft): his craft destroyed under him ends his life.
    {
      static int32_t test_was = 0;
      if (const int32_t test = REXCVAR_GET(pdz_counterop_test_river_destroy_craft); test != test_was)
      {
        test_was = test;
        if (test != 0 && seat >= 0 && pdz::guestmem::Ok(base, rs.riding + kPropFlags, 4))
        {
          Store32(base, rs.riding + kPropFlags, Load32(base, rs.riding + kPropFlags) | 0x4000u);
          REXLOG_INFO("pdz_counterop: river: TEST TOOL: pdz_counterop_test_river_destroy_craft {}: player 2's "
                      "hovercraft {:08X} marked destroyed (prop flag 0x4000; no explosion)",
                      test, rs.riding);
        }
      }
      static uint32_t left_craft = 0, killed_for = 0;
      static Clock::time_point left_at{};
      if (seat >= 0)
      {
        left_craft = rs.riding;
        left_at = now;
      }
      const bool recent = left_craft == rs.riding && std::chrono::duration<double>(now - left_at).count() <= 2.0;
      if (REXCVAR_GET(pdz_counterop_river_dies_with_craft) && !out && recent && killed_for != rs.riding &&
          pdz::guestmem::Ok(base, rs.riding + kPropHealth, 4) && Destroyed(base, rs.riding) &&
          pdz::guestmem::Ok(base, p2c + kParticipantOf, 4))
      {
        const uint32_t participant = Load32(base, p2c + kParticipantOf);
        if (pdz::guestmem::Ok(base, participant + kPartHealth, 4))
        {
          killed_for = rs.riding;
          const float had = LoadFloat(base, participant + kPartHealth);
          StoreFloat(base, participant + kPartHealth, 1.0f);
          REXLOG_INFO("pdz_counterop: river: player 2's hovercraft {:08X} is DESTROYED ({}; prop flags {:08X}, craft "
                      "health {:.0f}, seat {} state {}): his life ends with it, as an AI driver's does: health {:.1f} "
                      "-> 1.0 (his floor: counter_op.cpp's own death runs on its next check; "
                      "pdz_counterop_river_dies_with_craft)",
                      rs.riding, seat >= 0 ? "he is in the seat" : "he left the seat less than 2 s ago",
                      Load32(base, rs.riding + kPropFlags), LoadFloat(base, rs.riding + kPropHealth), seat, seat_state,
                      had);
        }
      }
      if (!rs.riding)
      {
        killed_for = 0;
      }
    }
    if (seat == 0 && seat_state == 3 && !rs.seated_told)
    {
      rs.seated_told = true;
      rs.left_told = false;
      REXLOG_INFO("pdz_counterop: river: player 2 is SEATED as the driver of the hovercraft {:08X}, {:.1f} s after the "
                  "mount (Rare's player hovercraft controls and camera from here)", rs.riding, since);
    }
    // Mon 23 Nov (pdz_counterop_river_driver_steer): the AI driver's commanded heading is cleared while he drives.
    if (seat == 0 && REXCVAR_GET(pdz_counterop_river_driver_steer))
    {
      constexpr uint32_t kCtrlHeadingAsked = 0x62C;  // (82675B20 reads it; 82196870 sets it; 82671220 resets it)
      constexpr uint32_t kHeadingNone = 0x8208B1F4;  // the float 82675B20 compares it with: no heading commanded
      if (pdz::guestmem::Ok(base, ctrl + kCtrlHeadingAsked, 4) && pdz::guestmem::Ok(base, kHeadingNone, 4))
      {
        const uint32_t none = Load32(base, kHeadingNone), have = Load32(base, ctrl + kCtrlHeadingAsked);
        if (have != none)
        {
          static uint32_t told_craft = 0, clears = 0;
          const float asked_heading = LoadFloat(base, ctrl + kCtrlHeadingAsked);
          Store32(base, ctrl + kCtrlHeadingAsked, none);
          ++clears;
          if (told_craft != rs.riding || clears % 600 == 0)
          {
            told_craft = rs.riding;
            REXLOG_INFO("pdz_counterop: river: player 2's hovercraft {:08X}: the heading its AI driver had asked for "
                        "({:.3f}) is cleared (Rare's 'none' value {:.3f}): his right stick steers the craft now "
                        "(cleared {} time(s) this session; pdz_counterop_river_driver_steer)",
                        rs.riding, asked_heading, LoadFloat(base, kHeadingNone), clears);
          }
        }
      }
    }
    if (seat < 0 && !rs.left_told && !out)
    {
      rs.left_told = true;
      rs.seated_told = false;
      REXLOG_INFO("pdz_counterop: river: player 2 is no longer in a seat of the hovercraft {:08X} ({:.1f} s after the "
                  "mount; craft state {} -> {}): he got out by himself or the craft threw him off; it stays open to his "
                  "Use (G2's exception) until this life ends",
                  rs.riding, since, vehicle, asked);
    }
    // Out of the seat with no live AI gunner left on the gun: the craft is closed to him again (G2: he drives, he
    // never mans a vehicle's gun; with a gunner on it the gun's seat is taken and only the driver's seat is open).
    if (seat < 0 && !out && !GunnerOf(base, rs.riding, p2c))
    {
      EndRide(base, "he is out of the seat and no live AI gunner is on the gun");
      return;
    }
    // Where the craft goes under him, and what the agent loses meanwhile (one line each, at most every 2 s / 1 s).
    if (every > 0.0 && seat == 0 && std::chrono::duration<double>(now - rs.last_track).count() >= 2.0)
    {
      const bool first = rs.last_track == Clock::time_point{};
      const double dt = first ? 0.0 : std::chrono::duration<double>(now - rs.last_track).count();
      float pos[3];
      for (int i = 0; i < 3; ++i)
      {
        pos[i] = LoadFloat(base, rs.riding + kPropPosition + 4 * uint32_t(i));
      }
      const float moved = std::sqrt((pos[0] - rs.track_pos[0]) * (pos[0] - rs.track_pos[0]) +
                                    (pos[1] - rs.track_pos[1]) * (pos[1] - rs.track_pos[1]) +
                                    (pos[2] - rs.track_pos[2]) * (pos[2] - rs.track_pos[2]));
      rs.last_track = now;
      const uint32_t gunner = GunnerOf(base, rs.riding, p2c);
      REXLOG_INFO("pdz_counterop: river: TRACK: player 2's hovercraft {:08X} at ({:.0f}, {:.0f}, {:.0f}){}; seat state {}, "
                  "craft state {}, craft health {:.0f}, his health {:.1f}, {:.0f} m from the agent, gunner '{}' (alert words "
                  "{:08X} / {:08X}, target word {:08X}), the agent's health {:.1f}; his body's centre ({:.0f}, {:.0f}, {:.0f}), death state word {}",
                  rs.riding, pos[0], pos[1], pos[2],
                  first || dt > 10.0 ? std::string()
                                     : fmt::format(", {:.1f} m/s over the last {:.1f} s", moved / 100.0f / float(dt), dt),
                  seat_state, vehicle, LoadFloat(base, rs.riding + kPropHealth), Health(base, p2c),
                  Ptr(p1c) ? Distance(base, rs.riding + kPropPosition, p1c + kPosition) / 100.0f : -1.0f,
                  Id(base, gunner), gunner ? Load32(base, gunner + 6268) : 0u, gunner ? Load32(base, gunner + 6252) : 0u,
                  gunner ? Load32(base, gunner + 0x317C) : 0u, Health(base, p1c), LoadFloat(base, p2c + kPosition),
                  LoadFloat(base, p2c + kPosition + 4), LoadFloat(base, p2c + kPosition + 8),
                  Ptr(Load32(base, p2c + kParticipantOf)) ? Load32(base, Load32(base, p2c + kParticipantOf) + 2584) : 0u);
      std::memcpy(rs.track_pos, pos, sizeof(pos));
    }
    if (Ptr(p1c))
    {
      const float hp = Health(base, p1c);
      if (rs.agent_health >= 0.0f && hp < rs.agent_health - 0.05f &&
          std::chrono::duration<double>(now - rs.last_agent_hit).count() >= 1.0)
      {
        rs.last_agent_hit = now;
        REXLOG_INFO("pdz_counterop: river: the agent lost health while player 2 drives: {:.1f} -> {:.1f} (his craft {:.0f} "
                    "m away, gunner '{}')",
                    rs.agent_health, hp, Distance(base, rs.riding + kPropPosition, p1c + kPosition) / 100.0f,
                    Id(base, GunnerOf(base, rs.riding, p2c)));
      }
      rs.agent_health = hp;
    }
  }
} // namespace counter_op_river

// Round 13b r: counter_op_triggers.cpp's Setup hook (a testing tool: nothing unless pdz_counterop_test_river_checkpoint).
void PdzCounterOpRiverPreSetup(uint8_t *base) { counter_op_river::PreSetup(base); }

// Round 13b r: test_harness.cpp's pad hook (a testing tool: nothing unless pdz_counterop_test_river_agent_drive is
// steering): pad 1's sticks.
void PdzCounterOpRiverTestPad(uint8_t *base, uint32_t user, uint32_t state)
{
  if (user != 0 || !g_drive_on.load(std::memory_order_relaxed) || state < 0x10000u)
  {
    return;
  }
  const uint16_t ly = uint16_t(int16_t(g_drive_ly.load())), rx = uint16_t(int16_t(g_drive_rx.load()));
  base[state + 8] = 0; // left stick x
  base[state + 9] = 0;
  base[state + 10] = uint8_t(ly >> 8); // left stick y (forward)
  base[state + 11] = uint8_t(ly);
  base[state + 12] = uint8_t(rx >> 8); // right stick x
  base[state + 13] = uint8_t(rx);
  base[state + 14] = 0; // right stick y
  base[state + 15] = 0;
}
