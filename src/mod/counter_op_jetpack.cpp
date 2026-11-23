// Counter-Operative round 2: a jetpack guard taken over by player 2, who flies the jetpack in Rare's own player
// jetpack mode. Draft, not built. See counter_op_jetpack.h and research notes: round2\e\FINDINGS.txt.
//
// Rare's vehicles (Title Update 3, read from the code):
//   - Vehicle prop: vtable 820CA9E0 (constructor 825D18A0), setup 825D1B20 by the template's type byte (+0x34:
//     1 motorbike, 2 / 4 hovercraft, 3 jetpack). A jetpack gets its controller (0xAB0 bytes, 825D2370, vtable
//     820CABC0) at prop +0x364 and an AI driving task (0x110 bytes, 82197480, vtable 8209106C) at prop +0xE50.
//     Prop +0x30 = position; prop vtable +148 = 825D2E90, Rare's Use (a human pressing Use on the vehicle).
//   - Controller (base class 82669280, shared by bike, hovercraft and jetpack): +0x0C the prop, +0x14 the type
//     (3 jetpack), +0x1EC / +0x1F0 vehicle state now / asked (0 empty, 1 grounded, 2 flying, 3 destroyed: the base
//     update 826699B0), seats from +0x3C, 0x60 bytes each: +0x3C / +0x3D usable bytes, +0x41 exit asked, +0x58 seat
//     state (0 empty, 1 getting in, 2 seat animation, 3 seated, 4 getting out), +0x5C an 8-byte handle whose object
//     (+0x60) is the occupant. Jetpack only: +0x380 its rider state (0 none, 2 riding, 7 getting off / dying ...),
//     +0x81C on-the-ground flag (ground probe 8268B878).
//   - Mount, for AI and humans alike: 8266A8E8(controller, character, seat, flag) -> bool. For a locally controlled
//     character (vtable +0x180 = +0x40 bit 0x2) it asks the type's CanEnter (jetpack vtable +0x14 = 82684970: in
//     single player = seat 0, seat free and usable (8266AD18), the prop not destroyed, the optional mount callback
//     at controller +0x18 (none in Trinity Escape's script), rider state +0x380 == 0); then it detaches the character
//     from any parent, files the handle, attaches him to the prop (825191A8: character +0x5C = prop, out of the
//     world list as every rider is) and takes Rare's riding action lock (8254D8F0, mask 0x00800C04_8404803E).
//     Callers: Rare's Use 825D2E90 (humans), the AI mount task 821B1490 (vtable 82091B58), level actions 824CFDE8.
//   - Riding: the jetpack update 82685F80 (controller vtable +152) reads the occupant through its vtable: +0x250 /
//     +0x254 (a human: participant +0x698 = the controller object's +8 context; the update sets 4 = jetpack, so the
//     pad mapping of mnk_layout.cpp applies), +0x290 (a human: +0x19A4) and steers the player's own view through
//     825B3AC0 (return 8268659C, mouse_look.cpp's "jetpack"). A guard answers 0 to all of them: the same update,
//     the human branch only for a human.
//   - Getting off: a human presses action 0x18 (human vtable +0x1EC = participant button bits +0x728), the base
//     update asks 8266ADE0(controller, seat), which for a local rider needs the jetpack's CanExit (826854F8:
//     +0x380 == 2 and +0x81C, i.e. landed). Mid-air there is no exit. An occupant that dies or goes: jetpack
//     vtable +140 (82684718; in the air: Rare's jetpack death, +0x380 = 7 and the exit asked).
//
// Trinity Escape (trinity2): two jetpack props, vehicle1111654452 at (-3162, 952, 3482) and vehicle1111654494 at
// (71, 962, -9034) (the level package's propsetup records, template byte 3). Their guards s21a (setup at -2757, 954,
// 3510) and s21c (332, 954, -9052) are dormant until the above-sea part: the script method 822DC250 (822E2318..
// 822E25C0) wakes its 16 guards (vtable +0x340) and gives s21a / s21c Rare's ride task (821E5BA0 + 821E60D0 with
// the vehicle and a fly-between-3-pads task 822E5A58 / 822E5B18: pads 0x124C / 0x1241 / 0x122B and 0xDA9 / 0xD96 /
// 0xD91), after 8268A550(controller, 0) (the jetpack's shell parts). So they walk 3-4 m to their jetpacks and fly
// from then on. s21b ("s21jetpackguard2", 5760, 995, 3715) has no jetpack: the level holds only two.
#include "counter_op_jetpack.h"
#include "counter_op_guards.h" // round 14: DatacoreRidersRule
#include "counter_op_rules13.h" // round 13: PdzCounterOpVehicleAllow (helper a's vehicle gate)
#include "guest_function.h"     // round 13b: pdz::GuestFunction (a vtable slot called as the game calls it)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack, true, "Gameplay",
                    "Counter-Operative: player 2 may take over a jetpack guard (Trinity Escape) while the guard is on "
                    "foot next to his parked jetpack; player 2 then flies it with the player's own jetpack controls. "
                    "A guard already flying stays the game's AI. Off = jetpack guards are never taken")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_remount, false, "Gameplay",
                    "Counter-Operative (testing, default off): when player 2 presses Use beside a free jetpack and the "
                    "game's own mount did not seat him within 0.3 s, make the same mount once (the one the game's Use "
                    "makes). The game's Use already remounts him in the logs read so far; this is a fallback only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_held_mask, true, "Gameplay",
                    "Counter-Operative (default on): buttons player 2 already holds when he takes over a guard (an A "
                    "held from the briefing, a cutscene skip, a menu) are ignored until he lets go of them once")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Round 13 (helper c): THE FLYING TAKEOVER. A guard already seated on his jetpack may be picked; his seat is handed
// to player 2 in the same tick, inside the takeover (research notes: JETPACK_AIR_STUDY_Sun15Nov.md).
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_air, true, "Gameplay",
                    "Counter-Operative (round 13, default on): player 2 may take over a jetpack guard who is already seated and "
                    "flying. The seat changes hands in one tick through the game's own steps (the rider leaves, the "
                    "jetpack is emptied, the game's mount seats player 2); the jetpack keeps its place, loses its speed "
                    "and may drop to the ground, then he flies it with the player's jetpack controls. Levels: "
                    "pdz_counterop_jetpack_air_levels. Off = a seated rider is never taken (rounds 2 to 12)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Mon 23 Nov (co20, the owner's decision): Bridge is out of the default. There the jetpack's steering found player 1
// through the jetpack's view (pdz_mouse: "user 0 found through the view") and, after player 2 died in the seat, pad 1
// drove both players with his view inside the agent; Datacore and Jungle resolve the steering to player 2. Bridge's
// two riders (s48a / s48b) are on the level's denied list, so with the flying takeover off they are never picked,
// in the seat or on foot, and nothing else seats him there: Handles() names no Bridge guard, his own Use on a
// vehicle is refused (pdz_counterop_p2_no_vehicles) unless a hand-over registered the exception, and AfterSpawn()
// makes no mount on Bridge.
REXCVAR_DEFINE_STRING(pdz_counterop_jetpack_air_levels, "datacore,trinity2,jungle", "Gameplay",
                      "Counter-Operative (round 13): the missions on which pdz_counterop_jetpack_air applies (comma "
                      "list of datacore, trinity2, jungle). Take a level out to switch its flying takeover off. "
                      "Bridge is not in it (Mon 23 Nov): its jetpack riders stay the game's own")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_jetpack_air_max_height, 8.0, "Gameplay",
                      "Counter-Operative (round 13): metres. A flying rider is only taken over when solid ground lies "
                      "within this distance straight below his jetpack (the jetpack's own ground ray); a hand-over "
                      "makes a flying jetpack drop to the ground. Higher up, or over a drop, he is left for a later pick")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 15 (Thu 19 Nov night): Datacore's end, the owner's decision of Tue 17 Nov 11:41.
REXCVAR_DEFINE_DOUBLE(pdz_counterop_datacore_rider_max_distance, 250.0, "Gameplay",
                      "Counter-Operative on Datacore's end (round 15): metres. A settled flying rider within this "
                      "distance of the agent may be handed to player 2 (elsewhere pdz_counterop_jetpack_air_max_distance)")
    .range(10.0, 2000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_rider_any_height, true, "Gameplay",
                    "Counter-Operative on Datacore's end (round 15, default on): a settled flying rider is handed over "
                    "at any height (no pdz_counterop_jetpack_air_max_height test; three riders were refused for "
                    "height in a play test and player 2 waited). Off = round 14")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_datacore_rider_fallback_s, 10.0, "Gameplay",
                      "Counter-Operative on Datacore's end (round 15): seconds without a body after which the distance "
                      "limit goes too: the nearest settled rider anywhere")
    .range(0.0, 600.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_jetpack_air_max_distance, 150.0, "Gameplay",
                      "Counter-Operative (round 13): metres. A flying rider is only taken over while he is within this "
                      "distance of the agent (Jungle's two riders fly at the far end of the level from its start: "
                      "without this player 2's first body was one of them, 414 m from her)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_jetpack_air_shell, "as_rider", "Gameplay",
                      "Counter-Operative (round 13): the jetpack's shell (with it on, RB takes off and LT climbs; with "
                      "it off the jetpack only skims) once player 2 is given a jetpack guard's jetpack. as_rider = as "
                      "that guard had it (Datacore: on, although the level switches it off for a player; Trinity "
                      "Escape: off, as its script sets it for its riders); on; off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_air_after_seat_death, false, "Gameplay",
                    "Counter-Operative (round 13): right after player 2 died in a jetpack seat his next body may again "
                    "be a flying rider. Off (default: not proven) = his next body is a guard on foot")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 13b (helper s): his death in a jetpack seat.
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_seat_death, true, "Gameplay",
                    "Counter-Operative (round 13b, default on): player 2's death in a jetpack seat is noticed in the seat itself "
                    "(his health at 0 while seated), so the rule 'his next body is a guard on foot' "
                    "(pdz_counterop_jetpack_air_after_seat_death off) really applies, and the death is followed in "
                    "the log for 14 s (the jetpack, the restored guard, his next body). Off = round 13: the death is "
                    "only noticed when he is still seated as his revive falls due, which the game's own rider death "
                    "never leaves him")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_close_after_life, true, "Gameplay",
                    "Counter-Operative (round 13b, default on): when the life that was given a jetpack is over, that jetpack is "
                    "closed to player 2 again (the vehicle rule's exception is taken back, and seat bytes a hand-over "
                    "opened are put back as the level had them). Off = round 13: it stays open to his Use until the "
                    "level ends")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_jet_kill, "", "Testing",
                      "TESTING TOOL (counter-op): a program of deaths of player 2, one digit a step, done in order "
                      "(e.g. 6263), each step waiting for its moment: 1 = on foot, 3 s after he got the body (health "
                      "put at his floor: his own death); 6 = the same again and again until he sits on a jetpack; 2 = "
                      "seated on a jetpack at hover height or under 2.5 m above ground; 3 = seated, 5 m or more above "
                      "ground (2 and 3: after 15 s seated, at whatever height); 4 = seated anywhere; 5 = seated "
                      "anywhere, his health lowered to 0.5 and the agent's view turned on him for 8 s first (a "
                      "script's trigger presses may land the hit). Seated: one killing hit through the game's "
                      "character damage routine with the agent as the attacker")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_jet_kill_riders, 0, "Testing",
                     "TESTING TOOL (counter-op): each new non-zero value kills every seated AI jetpack rider through "
                     "the game's own kill, as if the agent had shot him (player 2's jetpack is left alone)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Fri 20 Nov (item 5): the rider's pose. RESEARCH_rider_Fri20Nov.md.
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_rider_pose, true, "Gameplay",
                    "Counter-Operative (Fri 20 Nov, default on): player 2 gets on a jetpack he is given with empty "
                    "hands, as the game's own riders do: the gun the takeover has just started to draw is put back "
                    "at once through the game's weapon switch, before the game's mount; while he rides that jetpack a "
                    "gun that still turns up in his hands is put back the same way and the game's own seat step for "
                    "the hands (the one its mount makes) is made again. Off = the gun's draw runs into the mount "
                    "(arms up, hands off the handles, twitching)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_jet_take_first, false, "Testing",
                    "TESTING TOOL (counter-op): player 2's next picks take a flying jetpack rider whose seat can be "
                    "handed over (the one nearest the agent) before any other guard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Sun 22 Nov (round 17): the jetpack crashes (Rare's bug check near destroyed jetpacks): a tool to destroy them
// through Rare's own prop damage, and a watch of every jetpack's state changes. Both run in single player too.
REXCVAR_DEFINE_INT32(pdz_counterop_test_jet_destroy, 0, "Testing",
                     "TESTING TOOL (Sun 22 Nov, single player and Counter-Op): each new non-zero value destroys "
                     "jetpacks through the game's own prop damage (825EFA40 on the jetpack's damage part, the agent "
                     "as the attacker): 1 = every jetpack with an AI rider seated, 2 = player 2's jetpack, 3 = every "
                     "jetpack with nobody on it, 4 = every jetpack, 5 = the jetpack nearest the agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_ride_log, false, "Testing",
                    "TESTING (Sun 22 Nov, single player and Counter-Op): once a second, one line for player 2 on a "
                    "jetpack and one for an AI rider: seat / rider / vehicle state, height over the ground flag, hands, "
                    "hand animation bytes, the wall mask, and every animation the game played on him that second "
                    "(name x count @ caller)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_jet_watch, false, "Testing",
                    "TESTING TOOL (Sun 22 Nov, single player and Counter-Op): every jetpack's vehicle / rider / seat "
                    "state, occupant, ground flag and health are read 10 times a second and each change is logged")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_8266A8E8);        // mount: (controller, character, seat, flag) -> bool (Rare's Use and AI mount both call it)
REX_EXTERN(__imp__sub_8266A8E8); // the same, past round 13's vehicle gate (counter_op_vehicles.cpp hooks the mount)
REX_EXTERN(sub_8266ADE0); // ask a seat's exit: (controller, seat) -> bool (the type's CanExit for a local rider)
REX_EXTERN(sub_825AD148); // move a character (character, &position, 0)
REX_EXTERN(sub_82685800); // jetpack: the rider leaves (controller, character)
REX_EXTERN(sub_82685D00); // jetpack: emptied (controller)
REX_EXTERN(sub_821433D8); // handle assign (handle, object or 0)
REX_EXTERN(sub_82680BB0); // jetpack: its idle animation / parts after a rider change (controller)
REX_EXTERN(sub_8268A550); // jetpack: shell parts on / off, byte +0x518 (controller, on)
REX_EXTERN(sub_82259B80); // physics ray (mask, &from, &to, &visitor, 1) -> byte: hit (hooked in cut_scenarios.cpp)
REX_EXTERN(sub_8252DE88); // Rare's kill (guard, killer, death type, apply damage)
REX_EXTERN(sub_825EFA40); // a prop's damage part loses health (comp, causer, f1 health, f2 armour) (hooked: counter_op_unheard.cpp)
REX_EXTERN(sub_825AD3B0); // a character's view: (character, f1 pitch (positive down), f2 yaw), radians
REX_EXTERN(sub_824F83B8); // weapon switch: (switcher, item, .., r9 at once) (hooked in cut_scenarios.cpp; item 0 passes)
REX_EXTERN(sub_8254ECB0); // a character's hand animations on / off: (character, on) (the jetpack's seat step 82684CE0)
uint32_t PdzCounterOpAgent(); // counter_op.cpp: 0 unless a Counter-Op game is running

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216; // world (game +24): nodes (+4 object, +12 next)
  constexpr uint32_t kVehicleVtable = 0x820CA9E0;            // vehicle prop (825D18A0)
  constexpr uint32_t kJetpackCtrlVtable = 0x820CABC0;        // jetpack controller (825D2370)
  constexpr uint32_t kParent = 0x5C;                         // character / object: the object it is attached to
  constexpr uint32_t kPropPosition = 0x30, kPropController = 0x364;
  constexpr uint32_t kCtrlProp = 0x0C, kCtrlType = 0x14, kCtrlState = 0x1EC;
  constexpr uint32_t kSeat0 = 0x00, kSeatBytes = 0x60; // seat i at controller + i * 0x60
  constexpr uint32_t kSeatState = 0x58, kSeatOccupant = 0x60;
  constexpr uint32_t kJetRider = 0x380, kJetGround = 0x81C;
  constexpr uint32_t kTypeJetpack = 3;
  constexpr uint32_t kPosition = 48, kHealth = 120, kHidden = 6272;
  constexpr uint32_t kParticipantOf = 6560;   // human +0x19A0 -> participant
  constexpr uint32_t kParticipantPad = 0x690; // participant: the pad index (XInputGetState user), < 0 none
  constexpr uint32_t kPropFlags = 0x40, kPropHealth = 0x358, kCtrlEnterCallback = 0x18;
  constexpr double kUseCheck = 0.3; // s after a Use press: seated or not (Rare's mount sets seat state 1 at once)
  constexpr float kNearJetpack = 600.0f; // cm: a guard's jetpack is parked 3-4 m from him in Trinity Escape
  constexpr double kHoldCap = 3.0;       // s: the longest a revive is held for a seat to clear
  // Round 13 (the flying takeover).
  constexpr uint32_t kCtrlAsked = 0x1F0, kSeatHandle = 0x5C, kJetShell = 0x518;
  constexpr uint32_t kGuardVtable = 0x820C45A0;
  constexpr uint32_t kSetup = 84, kSetupId = 52, kSetupTeam = 56, kTeamBits = 128, kAllies = 130, kEnemies = 132;
  constexpr uint32_t kGroundMask = 0x04402200; // the mask of the jetpack's own ground ray (8268B878 -> 82165F68 mode 0)
  constexpr uint32_t kHitVisitor = 0x82090738; // the hitscan's visitor: hit point at +68
  constexpr double kAirWatch = 8.0;            // s after a hand-over: the seat and the shell are watched
  // Round 13b (his death in the seat).
  constexpr uint32_t kPartHealth = 980;    // participant: a human's health
  constexpr uint32_t kHealthFloor = 7828;  // human: the least the player damage handler leaves (0 while he rides)
  constexpr uint32_t kFlags44 = 0x44;      // character: bit 8 = dead (Rare's kill sets it)
  constexpr uint32_t kDamageSlot = 820;    // character vtable: the damage routine (82546BC8), as Rare's kill calls it
  constexpr uint32_t kKillArgWord = 0x82073C48, kKillArgFloat = 0x820DFCA4; // what Rare's kill passes (8252DEF0..8252DF14)
  constexpr float kOwnFloor = 1.0f;        // counter_op.cpp: player 2's floor while he plays a guard on foot
  constexpr int kDeathWatchLines = 14;     // one line a second after a death in the seat

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
  // Sun 22 Nov (round 18): readable now (inside the guest's range and mapped; the Sat 21 Nov cached check).
  bool Rd(const uint8_t *base, uint32_t a, uint32_t n) { return pdz::guestmem::Ok(base, a, n); }

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

  constexpr uint32_t Key(const char (&s)[5])
  {
    return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) | (uint32_t(uint8_t(s[2])) << 8) |
           uint32_t(uint8_t(s[3]));
  }

  struct JetState
  {
    std::string mission;
    std::unordered_map<uint32_t, uint32_t> pending; // guard -> the free jetpack prop Takeable() found next to him
    uint32_t riding = 0;                            // the jetpack player 2 was mounted on (0 = none)
    bool hold = false;
    bool dismount_asked = false;
    std::chrono::steady_clock::time_point hold_since{}, next_diag{};
    uint32_t scratch = 0; // guest vec3 for the move
    // The remount check (OnInteract -> Tick): the vehicle player 2 last pressed Use on, and when.
    uint32_t use_prop = 0;
    std::chrono::steady_clock::time_point use_at{};
    uint32_t uses = 0, remounts = 0, refusals = 0;
    // Round 13 (the flying takeover).
    std::unordered_map<uint32_t, uint32_t> air; // guard -> his jetpack: pending entries that are seat hand-overs
    struct Sample
    {
      float pos[3] = {};
      std::chrono::steady_clock::time_point at{};
      float speed = -1.0f; // cm/s between the last two looks, < 0 = not known
    };
    std::unordered_map<uint32_t, Sample> samples;                                    // jetpack prop -> its last look
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> told_refused; // guard -> the last refusal line
    uint32_t refused_lines = 0;
    std::unordered_map<uint32_t, uint32_t> told_riders; // rider -> the jetpack he was last listed on
    std::chrono::steady_clock::time_point next_list{};
    bool seat_death = false; // player 2's last life ended in a jetpack seat
    // The watch after a mount of ours: the seat (one "seated" line) and the shell.
    uint32_t watch_ctrl = 0, watch_prop = 0, watch_guard = 0;
    std::chrono::steady_clock::time_point watch_at{};
    bool watch_air = false, watch_seated = false;
    int shell_want = -1; // 0 / 1, -1 = left alone
    uint32_t shell_fixes = 0;
    float watch_pos[3] = {};
    float low_y = 0.0f;
    // The hand-over's facts between Unseat and AfterSpawn.
    uint8_t shell_before = 0;
    int32_t last_test_kill = 0;
    // Round 13b (helper s): the jetpack this life was given (the vehicle rule's exception is his until the life ends).
    uint32_t given_ctrl = 0, given_prop = 0, given_guard = 0;
    uint8_t unseat_usable[2] = {1, 1}, given_usable[2] = {1, 1}; // the seat's usable bytes before a hand-over set them
    bool given_usable_changed = false;
    std::chrono::steady_clock::time_point mount_at{}, spawn_at{};
    bool spawn_fresh = false;   // a body the test tool has not yet killed
    bool death_latched = false; // this life's death in the seat was noticed
    // Round 13b: a death in the seat, followed for kDeathWatchLines seconds (pdz_counterop_jetpack_seat_death).
    struct Death
    {
      bool active = false;
      uint32_t prop = 0, ctrl = 0, guard = 0;
      std::chrono::steady_clock::time_point at{}, next{};
      int lines = 0;
      float pos[3] = {}, last_jet[3] = {}, last_guard[3] = {};
      float height = -1.0f; // cm above ground at the death, < 0 = none found
      bool near_ground = false;
      double new_body_s = -1.0, off_seat_s = -1.0;
      uint32_t new_body = 0;
    } death;
    // Round 13b: the test tool pdz_counterop_test_jet_kill (a program of steps, each one a mode 1..6).
    std::vector<int> tk_prog;
    size_t tk_step = 0;
    int tk_armed = 0;   // the mode of the step now armed, 0 = none
    int tk_applied = 0; // deaths on foot asked in this step
    int tk_stage = 0;   // 0 waiting, 1 health lowered (mode 5: a real hit awaited), 2 / 3 the tool's hit made, 10 on foot: asked
    std::chrono::steady_clock::time_point tk_seated_since{}, tk_until{}, tk_told{}, tk_check{};
    bool tk_height_ok = false;
    float tk_ground = -1.0f;
    // Fri 20 Nov (item 5): the rider's pose (pdz_counterop_jetpack_rider_pose): corrections made on this ride.
    uint32_t pose_fixes = 0, pose_ride = 0;
    std::chrono::steady_clock::time_point pose_next{};
  } js;
  std::string g_tk_seen; // the tool's last value (kept over a level boundary: a value is one program, run once)
  uint32_t g_ray = 0; // guest memory for the ground ray (0x140 bytes: from +0, to +0x10, the visitor +0x20)

  uint16_t Load16(const uint8_t *base, uint32_t a) { return uint16_t((uint16_t(base[a]) << 8) | base[a + 1]); }

  std::string IdOf(const uint8_t *base, uint32_t character)
  {
    // (Sat 21 Nov evening: every step tested as mapped memory)
    const uint32_t setup = pdz::guestmem::Ok(base, character, kSetup + 4) ? Load32(base, character + kSetup) : 0;
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
    return out.empty() ? std::string("-") : out;
  }

  bool AirLevel()
  {
    if (!REXCVAR_GET(pdz_counterop_jetpack_air) || !REXCVAR_GET(pdz_counterop_jetpack) || js.mission.empty())
    {
      return false;
    }
    const std::string list = "," + std::string(REXCVAR_GET(pdz_counterop_jetpack_air_levels)) + ",";
    return list.find("," + js.mission + ",") != std::string::npos;
  }

  // Solid ground straight below a jetpack, by the ray Rare's jetpack ground probe casts (82259B80, its mask): true
  // with the distance (cm) when something lies within max_cm.
  bool GroundBelow(PPCContext &ctx, uint8_t *base, uint32_t prop, float max_cm, float &distance)
  {
    if (!g_ray)
    {
      g_ray = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x140);
      if (!g_ray)
      {
        return false;
      }
    }
    std::memset(base + g_ray, 0, 0x140);
    const float x = LoadFloat(base, prop + 0x30), y = LoadFloat(base, prop + 0x34), z = LoadFloat(base, prop + 0x38);
    StoreFloat(base, g_ray, x);
    StoreFloat(base, g_ray + 4, y);
    StoreFloat(base, g_ray + 8, z);
    StoreFloat(base, g_ray + 0x10, x);
    StoreFloat(base, g_ray + 0x14, y - max_cm);
    StoreFloat(base, g_ray + 0x18, z);
    const uint32_t visitor = g_ray + 0x20;
    Store32(base, visitor, kHitVisitor);
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
    frame.ctx.r3.u64 = kGroundMask;
    frame.ctx.r4.u64 = g_ray;
    frame.ctx.r5.u64 = g_ray + 0x10;
    frame.ctx.r6.u64 = visitor;
    frame.ctx.r7.u64 = 1;
    sub_82259B80(frame.ctx, base);
    if ((frame.ctx.r3.u32 & 0xFF) == 0)
    {
      return false;
    }
    distance = y - LoadFloat(base, visitor + 68 + 4);
    return std::isfinite(distance);
  }

  // The controller of a jetpack prop, or 0.
  uint32_t JetpackController(const uint8_t *base, uint32_t prop)
  {
    if (!Rd(base, prop, kPropController + 4) || Load32(base, prop) != kVehicleVtable)
    {
      return 0;
    }
    const uint32_t ctrl = Load32(base, prop + kPropController);
    if (!Rd(base, ctrl, kJetGround + 4) || Load32(base, ctrl) != kJetpackCtrlVtable || Load32(base, ctrl + kCtrlType) != kTypeJetpack ||
        Load32(base, ctrl + kCtrlProp) != prop)
    {
      return 0;
    }
    return ctrl;
  }

  // Free for a rider, by the fields Rare's CanEnter reads (82684970 / 8266AD18): seat 0 empty and usable, no rider
  // state, not destroyed. (The prop's +0x40 destroyed bits and the optional mount callback are left to Rare's test.)
  bool Free(const uint8_t *base, uint32_t ctrl)
  {
    const uint32_t seat = ctrl + kSeat0;
    return Load32(base, seat + kSeatState) == 0 && base[seat + 0x3C] != 0 && base[seat + 0x3D] != 0 &&
           Load32(base, ctrl + kJetRider) == 0 && Load32(base, ctrl + kCtrlState) != 3;
  }

  // The nearest free jetpack prop within kNearJetpack of (x, y, z), from the world list.
  uint32_t FreeJetpackNear(const uint8_t *base, float x, float y, float z, float *distance_out)
  {
    const uint32_t world = Load32(base, kGame + 24);
    if (!Ptr(world))
    {
      return 0;
    }
    uint32_t best = 0;
    float best_d = kNearJetpack;
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      const uint32_t ctrl = JetpackController(base, o);
      if (!ctrl || !Free(base, ctrl))
      {
        continue;
      }
      const float dx = LoadFloat(base, o + kPropPosition) - x, dy = LoadFloat(base, o + kPropPosition + 4) - y,
                  dz = LoadFloat(base, o + kPropPosition + 8) - z;
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d < best_d)
      {
        best_d = d;
        best = o;
      }
    }
    if (distance_out)
    {
      *distance_out = best_d;
    }
    return best;
  }

  // ---- Fri 20 Nov (item 5): the rider's pose ----
  // A character's hands (+4076 / +4080), his weapon switcher (+5780: +76 its character, +108 the switch in progress,
  // freed by 824F7280) and his two hand-animation bytes (+4072 / +4073, written only by 8254ECB0; the jetpack's seat
  // step 82684CE0 puts them at 0 with 8254ECB0(character, 0), which also stops each held item's animation).
  constexpr uint32_t kHandA = 4076, kHandB = 4080, kSwitcher = 5780, kSwitcherOwner = 76, kSwitchTask = 108;
  constexpr uint32_t kHandAnimA = 4072, kHandAnimB = 4073;
  constexpr uint32_t kPoseFixCap = 8; // corrections a ride

  uint32_t SwitchTask(const uint8_t *base, uint32_t c)
  {
    const uint32_t sw = Load32(base, c + kSwitcher);
    return Ptr(sw) && Load32(base, sw + kSwitcherOwner) == c ? Load32(base, sw + kSwitchTask) : 0u;
  }

  // Something in his hands, or a weapon switch under way.
  bool HandsBusy(const uint8_t *base, uint32_t c)
  {
    return Ptr(Load32(base, c + kHandA)) || Ptr(Load32(base, c + kHandB)) || Ptr(SwitchTask(base, c));
  }

  std::string HandsLine(const uint8_t *base, uint32_t c)
  {
    return fmt::format("hands {:08X} / {:08X}, weapon switch under way {:08X}, hand animations {} / {}",
                       Load32(base, c + kHandA), Load32(base, c + kHandB), SwitchTask(base, c), base[c + kHandAnimA],
                       base[c + kHandAnimB]);
  }

  // Rare's weapon switch to nothing, AT ONCE (824F83B8 with r9 = 1, as Rare's own 82385298 calls the switcher's select
  // with r9 = 1): each hand's item goes straight back into his inventory (8254BB18(c, hand, 1, 1)), the switch in
  // progress is freed (824F7280) and the switcher's targets are cleared. Called past the select's action-lock test
  // (824F8358 -> 8254DAB8(c, 2)), which the riding lock would refuse. True when it was made.
  bool PutAwayAtOnce(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    const uint32_t sw = Load32(base, c + kSwitcher);
    if (!Ptr(sw) || Load32(base, sw + kSwitcherOwner) != c || !Ptr(Load32(base, sw)))
    {
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu; // (824F83B8 stores its r10 in the caller's frame)
    frame.ctx.r3.u64 = sw;
    frame.ctx.r4.u64 = 0;
    frame.ctx.r5.u64 = 0;
    frame.ctx.r6.u64 = 0;
    frame.ctx.r7.u64 = 0;
    frame.ctx.r8.u64 = 0;
    frame.ctx.r9.u64 = 1;
    frame.ctx.r10.u64 = 0;
    sub_824F83B8(frame.ctx, base);
    return true;
  }

  // The jetpack's seat step for the hands (82684CE0 at 82684D74: 8254ECB0(character, 0)): hand animations off.
  void HandAnimationsOff(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c;
    frame.ctx.r4.u64 = 0;
    frame.ctx.f1.f64 = 0.0;
    sub_8254ECB0(frame.ctx, base);
  }

  // The seat index of a character in a controller (Rare's own search, 824CFF00 / 82684718), -1 = none.
  int SeatOf(const uint8_t *base, uint32_t ctrl, uint32_t character)
  {
    for (int i = 0; i < 4; ++i)
    {
      if (Load32(base, ctrl + i * kSeatBytes + kSeatOccupant) == character)
      {
        return i;
      }
    }
    return -1;
  }
} // namespace

void PdzRideSlotsFrame(const uint8_t *base); // below (round 18, item 6)
void PdzSpiderTestTick(PPCContext &ctx, uint8_t *base, uint32_t p1); // below (Sun 22 Nov night, Part 3)

namespace counter_op_jetpack
{
  void LevelStart(const std::string &mission)
  {
    Reset();
    js.mission = mission;
    if (mission == "bridge")
    {
      REXLOG_INFO("pdz_counterop: jetpack: bridge: flying takeover {} (pdz_counterop_jetpack_air_levels '{}'): {}",
                  AirLevel() ? "ON" : "off", std::string(REXCVAR_GET(pdz_counterop_jetpack_air_levels)),
                  AirLevel() ? "a seated rider may be handed over"
                             : "the jetpack riders stay the game's own: never picked, and player 2 is seated on no "
                               "jetpack here");
    }
  }

  void Reset()
  {
    const uint32_t scratch = js.scratch;
    js = JetState{};
    js.scratch = scratch;   // guest memory is kept for the run
    counter_op_pad::Reset(); // and nothing of player 2's pad is masked (CALL-IN P2 is this)
  }

  bool Handles(uint32_t id_key)
  {
    if (!REXCVAR_GET(pdz_counterop_jetpack))
    {
      return false;
    }
    // Trinity Escape's two riders (the only jetpacks of the level). Jungle (s12a / s12b) and Bridge riders use the
    // same Rare path but are untested; add them here once Trinity Escape is proven.
    if (js.mission == "trinity2")
    {
      return id_key == Key("s21a") || id_key == Key("s21c");
    }
    // Round 13: Jungle's two jetpack guards (on foot beside their free jetpack as on Trinity Escape; in the seat
    // through the flying takeover).
    if (js.mission == "jungle" && AirLevel())
    {
      return id_key == Key("s12a") || id_key == Key("s12b");
    }
    return false;
  }

  uint32_t VehicleOf(const uint8_t *base, uint32_t character)
  {
    if (!Rd(base, character, kParent + 4))
    {
      return 0;
    }
    const uint32_t parent = Load32(base, character + kParent);
    return Rd(base, parent, kPropController + 4) && Load32(base, parent) == kVehicleVtable ? parent : 0;
  }

  bool Riding(const uint8_t *base, uint32_t p2)
  {
    const uint32_t prop = VehicleOf(base, p2);
    const uint32_t ctrl = JetpackController(base, prop);
    return ctrl && SeatOf(base, ctrl, p2) >= 0;
  }

  // Round 13: a guard in a jetpack seat on a level where the flying takeover is on (cheap: counter_op.cpp's side
  // rule and Spots() ask it for every guard).
  bool AirRider(const uint8_t *base, uint32_t guard)
  {
    // Sun 22 Nov night (audit): the guard and his parent link read only where readable (VehicleOf, as round 18).
    if (!AirLevel() || !Rd(base, guard, kParent + 4) || Load32(base, guard) != kGuardVtable)
    {
      return false;
    }
    const uint32_t parent = VehicleOf(base, guard);
    if (!parent)
    {
      return false;
    }
    const uint32_t ctrl = JetpackController(base, parent);
    return ctrl && SeatOf(base, ctrl, guard) >= 0;
  }

  bool AirPending(uint32_t guard) { return js.air.count(guard) != 0; }

  // Datacore: a flying rider is only picked when no guard on foot can be (counter_op.cpp Pick()).
  bool AirLast() { return js.mission == "datacore"; }

  // Testing tool: the pick takes a flying rider first.
  bool AirTestFirst() { return REXCVAR_GET(pdz_counterop_test_jet_take_first); }

  // cm from the agent to a point; 0 when the agent is not known.
  static float AgentDistance(const uint8_t *base, const float pos[3])
  {
    const uint32_t agent = PdzCounterOpAgent();
    if (!Ptr(agent))
    {
      return 0.0f;
    }
    const float dx = LoadFloat(base, agent + kPosition) - pos[0], dy = LoadFloat(base, agent + kPosition + 4) - pos[1],
                dz = LoadFloat(base, agent + kPosition + 8) - pos[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  // Round 13: may this seated rider's seat be handed to player 2 now? (Spots(), once a second while he needs a body.)
  static bool AirTake(PPCContext &ctx, uint8_t *base, uint32_t guard, uint32_t prop, std::string &why)
  {
    const uint32_t ctrl = JetpackController(base, prop);
    if (!ctrl || !AirLevel())
    {
      return false; // why stays empty: the caller's old reason
    }
    const auto now = std::chrono::steady_clock::now();
    // The jetpack's speed between two looks (the looks come once a second).
    JetState::Sample &s = js.samples[prop];
    const float pos[3] = {LoadFloat(base, prop + kPropPosition), LoadFloat(base, prop + kPropPosition + 4),
                          LoadFloat(base, prop + kPropPosition + 8)};
    const double dt = std::chrono::duration<double>(now - s.at).count();
    if (s.at.time_since_epoch().count() && dt > 0.2 && dt < 3.0)
    {
      s.speed = float(std::sqrt(double(pos[0] - s.pos[0]) * (pos[0] - s.pos[0]) +
                                double(pos[1] - s.pos[1]) * (pos[1] - s.pos[1]) +
                                double(pos[2] - s.pos[2]) * (pos[2] - s.pos[2])) /
                      dt);
    }
    else if (dt >= 3.0)
    {
      s.speed = -1.0f;
    }
    if (dt > 0.2 || !s.at.time_since_epoch().count())
    {
      std::memcpy(s.pos, pos, sizeof(pos));
      s.at = now;
    }
    const uint32_t seat = Load32(base, ctrl + kSeatState), rider = Load32(base, ctrl + kJetRider),
                   vehicle = Load32(base, ctrl + kCtrlState);
    bool height_refusal = false;
    float ground = 0.0f;
    const float max_cm = float(std::clamp(REXCVAR_GET(pdz_counterop_jetpack_air_max_height), 0.0, 100.0) * 100.0);
    // Round 15 (Thu 19 Nov night; the owner's decision of Tue 17 Nov 11:41): on Datacore's end (the riders are his last
    // bodies) any settled rider within pdz_counterop_datacore_rider_max_distance (250 m) is handed over, at any height;
    // after pdz_counterop_datacore_rider_fallback_s (10 s) without a body the distance limit goes too (the nearest
    // settled rider anywhere: the pick takes the nearest spot). Log 006 21:4x: three riders were refused for height and
    // player 2 sat on AWAITING DEPLOYMENT.
    const bool dc_riders = counter_op_guards::DatacoreRidersRule(base);
    const double waited_s = counter_op_guards::PickWait();
    const bool dc_fallback = dc_riders && waited_s >= REXCVAR_GET(pdz_counterop_datacore_rider_fallback_s);
    const double max_m = dc_riders ? (dc_fallback ? 1.0e9 : REXCVAR_GET(pdz_counterop_datacore_rider_max_distance))
                                   : REXCVAR_GET(pdz_counterop_jetpack_air_max_distance);
    const bool any_height = dc_riders && REXCVAR_GET(pdz_counterop_datacore_rider_any_height);
    if (Load32(base, ctrl + kSeatOccupant) != guard || seat != 3 || (rider != 2 && rider != 3) || vehicle != 2)
    {
      why = fmt::format("jetpack guard in the seat of {:08X}, but not settled in flight (seat state {}, rider state {}, "
                        "vehicle state {}): not handed over now",
                        prop, seat, rider, vehicle);
    }
    else if (const float far_m = AgentDistance(base, pos) / 100.0f; far_m > float(max_m))
    {
      // Run 2 (Jungle): the first pick of the level took s12a 414 m from the agent, at the far end of the level.
      why = fmt::format("jetpack guard '{}' flying on {:08X} is {:.0f} m from the agent: further than {} ({:.0f} m), not part "
                        "of her fight yet",
                        IdOf(base, guard), prop, far_m,
                        dc_riders ? "pdz_counterop_datacore_rider_max_distance" : "pdz_counterop_jetpack_air_max_distance", max_m);
    }
    else if (js.seat_death && !REXCVAR_GET(pdz_counterop_jetpack_air_after_seat_death) &&
             !counter_op_guards::DatacoreRidersRule(base)) // (round 14: on Datacore the riders are his last bodies)
    {
      why = fmt::format("jetpack guard flying on {:08X}: player 2's last life ended in a jetpack seat, so this body is "
                        "a guard on foot (pdz_counterop_jetpack_air_after_seat_death is off)",
                        prop);
    }
    else if (!any_height && !base[ctrl + kJetGround] && !GroundBelow(ctx, base, prop, max_cm, ground))
    {
      height_refusal = true;
      why = fmt::format("jetpack guard '{}' {:08X} flying on {:08X} at ({:.0f}, {:.0f}, {:.0f}) is REFUSED for height: no "
                        "solid ground within {:.1f} m below his jetpack (pdz_counterop_jetpack_air_max_height); a "
                        "hand-over would drop it; he is left for a later pick",
                        IdOf(base, guard), guard, prop, pos[0], pos[1], pos[2], max_cm / 100.0f);
    }
    else
    {
      why = fmt::format("jetpack guard flying on {:08X} ({}, {:.0f} m from the agent{}): the seat is handed over", prop,
                        base[ctrl + kJetGround] ? std::string("at hover height")
                        : any_height            ? std::string("Datacore's end: any height")
                                                : fmt::format("ground {:.1f} m below", ground / 100.0f),
                        AgentDistance(base, pos) / 100.0f,
                        dc_fallback ? fmt::format("; {:.0f} s without a body: the nearest settled rider anywhere", waited_s) : std::string());
      static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> told_ok;
      if (told_ok.size() > 256)
      {
        told_ok.clear();
      }
      if (auto &t = told_ok[guard]; !t.time_since_epoch().count() || now - t >= std::chrono::seconds(15))
      {
        t = now;
        REXLOG_INFO("pdz_counterop: jetpack air: '{}' {:08X} may be taken: {}", IdOf(base, guard), guard, why);
      }
      return true;
    }
    // One line per refused rider every 15 s (30 a level at most).
    auto &told = js.told_refused[guard];
    if (js.refused_lines < 30 && (!told.time_since_epoch().count() || now - told >= std::chrono::seconds(15)))
    {
      told = now;
      ++js.refused_lines;
      REXLOG_INFO("pdz_counterop: jetpack air: {}{}", why, height_refusal ? "" : " (not a height refusal)");
    }
    return false;
  }

  bool Takeable(PPCContext &ctx, uint8_t *base, uint32_t guard, std::string &why)
  {
    js.pending.erase(guard);
    js.air.erase(guard);
    if (Load32(base, guard + kHidden) & 0x10)
    {
      why = "jetpack guard dormant (Rare's script wakes him later)";
      return false;
    }
    if (LoadFloat(base, guard + kHealth) <= 0.0f)
    {
      why = "jetpack guard dead";
      return false;
    }
    if (const uint32_t prop = VehicleOf(base, guard))
    {
      // Round 13: the flying takeover (pdz_counterop_jetpack_air).
      if (AirTake(ctx, base, guard, prop, why))
      {
        js.pending[guard] = prop;
        js.air[guard] = prop;
        return true;
      }
      if (why.empty())
      {
        why = fmt::format("jetpack guard riding (prop {:08X}): stays Rare's AI (no exit in the air)", prop);
      }
      return false;
    }
    float d = 0.0f;
    const uint32_t prop = FreeJetpackNear(base, LoadFloat(base, guard + kPosition),
                                          LoadFloat(base, guard + kPosition + 4),
                                          LoadFloat(base, guard + kPosition + 8), &d);
    if (!prop)
    {
      why = "jetpack guard on foot, no free jetpack within 6 m";
      return false;
    }
    js.pending[guard] = prop;
    why = fmt::format("jetpack guard on foot, his jetpack {:08X} parked and free {:.1f} m away", prop, d / 100.0f);
    return true;
  }

  // Round 12 (item 6). Datacore's riders are clones (no setup id of their own), so Handles() cannot name them: the
  // caller finds them by their spawner slot and gives the jetpack of that slot. Only the plan is made here (the
  // pending entry AfterSpawn() reads); the mount itself is Rare's 8266A8E8, as for Trinity Escape.
  bool DatacoreRiderTake(const uint8_t *base, uint32_t guard, uint32_t prop, std::string &why)
  {
    js.pending.erase(guard);
    js.air.erase(guard);
    if (!REXCVAR_GET(pdz_counterop_jetpack))
    {
      why = "pdz_counterop_jetpack is off: taken as a guard on foot";
      return false;
    }
    const uint32_t ctrl = JetpackController(base, prop);
    if (!ctrl)
    {
      why = fmt::format("his slot's object {:08X} is no jetpack (vtable {:08X}): taken as a guard on foot", prop,
                        Ptr(prop) ? Load32(base, prop) : 0);
      return false;
    }
    if (!Free(base, ctrl))
    {
      why = fmt::format("his jetpack {:08X} is not free for a rider (seat 0 state {}, usable {} / {}, rider state {}, "
                        "vehicle state {}): taken as a guard on foot",
                        prop, Load32(base, ctrl + kSeatState), base[ctrl + 0x3C], base[ctrl + 0x3D],
                        Load32(base, ctrl + kJetRider), Load32(base, ctrl + kCtrlState));
      return false;
    }
    const float dx = LoadFloat(base, prop + kPropPosition) - LoadFloat(base, guard + kPosition),
                dy = LoadFloat(base, prop + kPropPosition + 4) - LoadFloat(base, guard + kPosition + 4),
                dz = LoadFloat(base, prop + kPropPosition + 8) - LoadFloat(base, guard + kPosition + 8);
    // Round 13b (helper s; log s_datacore 09:59:51): after his death in the seat the guard MADE for him (the last
    // resort) reused the dead rider's object and slot, so this plan sent player 2 43 m to the jetpack that had
    // dropped (vehicle state 1: Rare cancels a mount on a grounded jetpack a tick later). A jetpack is only planned
    // when it is parked (state 0) within 12 m of the guard, and never for the body right after a death in the seat.
    if (REXCVAR_GET(pdz_counterop_jetpack_seat_death))
    {
      const float far_m = std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f;
      const uint32_t state = Load32(base, ctrl + kCtrlState);
      if (js.seat_death || far_m > 12.0f || state != 0)
      {
        why = fmt::format("his slot's jetpack {:08X} is not planned for him ({}; {:.1f} m from him, vehicle state {}): "
                          "taken as a guard on foot",
                          prop,
                          js.seat_death ? "player 2's last life ended in a jetpack seat"
                          : state != 0  ? "it is not parked: a dropped jetpack"
                                        : "too far from him",
                          far_m, state);
        return false;
      }
    }
    js.pending[guard] = prop;
    why = fmt::format("his jetpack {:08X} is free, {:.1f} m from him: player 2 will be seated on it through Rare's "
                      "mount (8266A8E8) and fly it with the player's jetpack controls",
                      prop, std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f);
    return true;
  }

  // Round 13: the seat hand-over, first half. counter_op.cpp SpawnAt(), take mode, after ShowP2 and just before the
  // taken guard is hidden: a guard picked in his seat (AirTake) leaves it through the steps Rare's occupant handler
  // makes for a rider removed at once (82684718 at 82684794..826847C0: 82685800, 82685D00, seat state 0, the handle
  // cleared, 82680BB0), so the hide is that of a guard on foot and AfterSpawn() finds the seat free in the same tick.
  // Rare's jetpack death never runs. Never called on its own: player 2 must be in the world and unlocked.
  void Unseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
  {
    const auto it = js.air.find(guard);
    if (it == js.air.end())
    {
      return;
    }
    const uint32_t prop = it->second;
    const uint32_t ctrl = JetpackController(base, prop);
    if (!ctrl || !Ptr(p2) || Load32(base, ctrl + kSeatOccupant) != guard || Load32(base, ctrl + kSeatState) != 3)
    {
      REXLOG_WARN("pdz_counterop: jetpack air: NO hand-over: guard {:08X} is no longer settled in the seat of {:08X} "
                  "(controller {:08X}, seat state {}, occupant {:08X}): player 2 is that guard on foot",
                  guard, prop, ctrl, ctrl ? Load32(base, ctrl + kSeatState) : 0u,
                  ctrl ? Load32(base, ctrl + kSeatOccupant) : 0u);
      js.air.erase(guard);
      js.pending.erase(guard);
      return;
    }
    const uint32_t seat0 = Load32(base, ctrl + kSeatState), rider0 = Load32(base, ctrl + kJetRider),
                   vehicle0 = Load32(base, ctrl + kCtrlState), asked0 = Load32(base, ctrl + kCtrlAsked);
    const uint8_t ground0 = base[ctrl + kJetGround], usable0 = base[ctrl + 0x3C], usable1 = base[ctrl + 0x3D];
    js.shell_before = base[ctrl + kJetShell];
    js.unseat_usable[0] = usable0; // round 13b: put back when this life is over (pdz_counterop_jetpack_close_after_life)
    js.unseat_usable[1] = usable1;
    float ground = 0.0f;
    const bool have_ground = !ground0 && GroundBelow(ctx, base, prop, 20000.0f, ground);
    const auto sample = js.samples.find(prop);
    const float speed = sample == js.samples.end() ? -1.0f : sample->second.speed;
    for (int i = 0; i < 3; ++i)
    {
      js.watch_pos[i] = LoadFloat(base, prop + kPropPosition + 4 * i);
    }
    Call(ctx, base, sub_82685800, ctrl, guard);
    Call(ctx, base, sub_82685D00, ctrl);
    Store32(base, ctrl + kSeatState, 0);
    Call(ctx, base, sub_821433D8, ctrl + kSeatHandle, 0);
    Call(ctx, base, sub_82680BB0, ctrl);
    // A seat a level script closed to players (Bridge: 822A2510 clears +0x3D): Rare's CanEnter would refuse him.
    base[ctrl + 0x3C] = 1;
    base[ctrl + 0x3D] = 1;
    REXLOG_INFO("pdz_counterop: jetpack air: HAND-OVER of the jetpack {:08X} at ({:.0f}, {:.0f}, {:.0f}) from guard '{}' "
                "{:08X}: {}, speed before {}; before: seat state {}, rider state {}, vehicle state {} -> {}, shell {}, "
                "usable {} / {}; after the rider left: seat state {}, rider state {}, vehicle state {} -> {}, shell {}, "
                "usable 1 / 1, guard parent {:08X}",
                prop, js.watch_pos[0], js.watch_pos[1], js.watch_pos[2], IdOf(base, guard), guard,
                ground0       ? std::string("at hover height (near-ground byte set)")
                : have_ground ? fmt::format("ground {:.1f} m below the jetpack", ground / 100.0f)
                              : std::string("no ground found below"),
                speed < 0.0f ? std::string("not known") : fmt::format("{:.1f} m/s", speed / 100.0f), seat0, rider0,
                vehicle0, asked0, js.shell_before, usable0, usable1, Load32(base, ctrl + kSeatState),
                Load32(base, ctrl + kJetRider), Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked),
                base[ctrl + kJetShell], Load32(base, guard + kParent));
  }

  // Round 13b (helper s): the life that was given a jetpack is over: the jetpack is closed to player 2 again (helper
  // a's exception taken back; the usable bytes a hand-over set are put back as the level had them, when the seat is
  // empty). pdz_counterop_jetpack_close_after_life.
  static void TakeBack(uint8_t *base, const char *why)
  {
    if (!js.given_ctrl || !REXCVAR_GET(pdz_counterop_jetpack_close_after_life))
    {
      return;
    }
    const uint32_t ctrl = js.given_ctrl, prop = js.given_prop;
    js.given_ctrl = 0;
    js.given_prop = 0;
    const bool valid = JetpackController(base, prop) == ctrl;
    std::string bytes = "the seat's usable bytes were not changed by the hand-over";
    if (valid && js.given_usable_changed)
    {
      if (Load32(base, ctrl + kSeatState) == 0)
      {
        base[ctrl + 0x3C] = js.given_usable[0];
        base[ctrl + 0x3D] = js.given_usable[1];
        bytes = fmt::format("the seat's usable bytes are back at {} / {}, as the level had them", js.given_usable[0],
                            js.given_usable[1]);
      }
      else
      {
        bytes = fmt::format("the seat is not empty (seat state {}): its usable bytes are left at {} / {}",
                            Load32(base, ctrl + kSeatState), base[ctrl + 0x3C], base[ctrl + 0x3D]);
      }
    }
    js.given_usable_changed = false;
    PdzCounterOpVehicleAllow(ctrl, false);
    REXLOG_INFO("pdz_counterop: jetpack: the jetpack {:08X} player 2 was given is no longer his ({}): closed to him "
                "again{}; {}; now seat state {}, rider state {}, vehicle state {}",
                prop, why, valid ? "" : " (the prop is no jetpack any more)", bytes,
                valid ? Load32(base, ctrl + kSeatState) : 0u, valid ? Load32(base, ctrl + kJetRider) : 0u,
                valid ? Load32(base, ctrl + kCtrlState) : 0u);
  }

  std::string AfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard)
  {
    // Round 13b: a new body. The last life's jetpack is closed again; a death in the seat gets its "next body" line.
    {
      const auto now = std::chrono::steady_clock::now();
      if (js.death_latched && REXCVAR_GET(pdz_counterop_jetpack_seat_death))
      {
        js.death.new_body = guard;
        js.death.new_body_s = std::chrono::duration<double>(now - js.death.at).count();
        REXLOG_INFO("pdz_counterop: jetpack seat death: player 2's NEXT BODY is guard '{}' {:08X}, {:.2f} s after the "
                    "killing hit in the seat ({}); the rule 'a guard on foot after a death in the seat' was {}",
                    IdOf(base, guard), guard, js.death.new_body_s,
                    js.air.count(guard) ? "a FLYING RIDER again" : js.pending.count(guard)
                                                                       ? "a jetpack guard on foot beside his jetpack"
                                                                       : "a guard on foot",
                    js.seat_death ? "in force" : "NOT in force");
      }
      js.death_latched = false;
      js.spawn_at = now;
      js.spawn_fresh = true;
      TakeBack(base, "he has a new body");
    }
    js.seat_death = false; // a new body
    if (js.mission == "bridge" && !AirLevel())
    {
      js.pending.clear(); // Mon 23 Nov (co20): on Bridge he is seated on no jetpack
      js.air.clear();
      return "";
    }
    const auto it = js.pending.find(guard);
    if (it == js.pending.end())
    {
      js.air.clear();
      return "";
    }
    const uint32_t prop = it->second;
    const bool air = js.air.count(guard) != 0;
    js.pending.clear();
    js.air.clear();
    const uint32_t ctrl = JetpackController(base, prop);
    if (!ctrl || !Free(base, ctrl))
    {
      return fmt::format("jetpack: the jetpack {:08X} is no longer free: player 2 stays on foot as the guard", prop);
    }
    // Next to the jetpack first (Rare's Use is made from beside it; the mount itself doesn't test the distance). A
    // hand-over in the air: at the jetpack itself.
    if (!js.scratch)
    {
      js.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
    }
    if (js.scratch)
    {
      StoreFloat(base, js.scratch, LoadFloat(base, prop + kPropPosition));
      StoreFloat(base, js.scratch + 4, LoadFloat(base, prop + kPropPosition + 4));
      StoreFloat(base, js.scratch + 8, LoadFloat(base, prop + kPropPosition + 8) + (air ? 0.0f : 60.0f));
      Call(ctx, base, sub_825AD148, p2, js.scratch, 0);
    }
    const uint8_t shell_before = air ? js.shell_before : base[ctrl + kJetShell];
    // Fri 20 Nov (item 5, pdz_counterop_jetpack_rider_pose): log 027 15:23:03, the bad pose: spawn (c) had just
    // given him the guard's gun and started its draw (the guard held it), and Rare's mount ran in the same tick; the
    // gun was in his hands until 0.8 s into the seat animation. The good rider (15:24:30) came after Rare's revive:
    // the draw never took, his hands stayed empty. Rare's own riders get on with no switch under way, so the draw is
    // undone at once here, before the mount.
    std::string pose;
    js.pose_fixes = 0;
    js.pose_ride = prop;
    js.pose_next = {};
    if (REXCVAR_GET(pdz_counterop_jetpack_rider_pose) && HandsBusy(base, p2))
    {
      const std::string before = HandsLine(base, p2);
      const bool made = PutAwayAtOnce(ctx, base, p2);
      pose = fmt::format("; rider pose: before the mount {}: {} -> {} (pdz_counterop_jetpack_rider_pose)", before,
                         made ? "Rare's weapon switch to nothing at once (824F83B8, r9 1)" : "NO weapon switcher",
                         HandsLine(base, p2));
    }
    // Sun 22 Nov (item 6, the owner's clue from log 043 11:34:51: hands emptied but the hand ANIMATIONS still on at the
    // mount): the hand animations go off before the mount too, as Rare's seat step (82684CE0 -> 8254ECB0(c, 0)) leaves
    // them for every rider, instead of from the seat step 0.3 s later.
    if (REXCVAR_GET(pdz_counterop_jetpack_rider_pose) && (base[p2 + kHandAnimA] || base[p2 + kHandAnimB]))
    {
      const uint8_t a = base[p2 + kHandAnimA], b = base[p2 + kHandAnimB];
      HandAnimationsOff(ctx, base, p2);
      pose += fmt::format("; hand animations {} / {} -> {} / {} before the mount (Rare's seat step 8254ECB0(p2, 0))", a, b,
                          base[p2 + kHandAnimA], base[p2 + kHandAnimB]);
    }
    // Round 13: the mount itself, past the vehicle gate that closes Rare's mount to player 2's own Use (G2): this
    // jetpack is the guard's he was given.
    const uint32_t ok = Call(ctx, base, __imp__sub_8266A8E8, ctrl, p2, 0, 0) & 0xFF;
    std::string extra = pose;
    if (ok)
    {
      js.riding = prop;
      // G2 (counter_op_vehicles.cpp): this jetpack is the one vehicle open to him (his own Use may get back on it
      // after he has landed and got off), as that file does for a mount that passes through its gate.
      PdzCounterOpVehicleAllow(ctrl, true);
      // Round 13b: remembered, so the exception ends with this life (TakeBack).
      js.given_ctrl = ctrl;
      js.given_prop = prop;
      js.given_guard = guard;
      js.given_usable[0] = js.unseat_usable[0];
      js.given_usable[1] = js.unseat_usable[1];
      js.given_usable_changed = air && (js.unseat_usable[0] != 1 || js.unseat_usable[1] != 1);
      js.mount_at = std::chrono::steady_clock::now();
      if (air && Load32(base, ctrl + kCtrlState) == 2 && Load32(base, ctrl + kCtrlAsked) == 1 &&
          Load32(base, ctrl + kSeatState) == 1)
      {
        // Datacore, Jungle: for a deployed jetpack the mount asks vehicle state 1 (its pose test 8268CC70), and the
        // base update would cancel the entry a tick later (a human must hold Use to get on a grounded jetpack:
        // 8266A410..8266A4A0). The value Rare's mount writes for a jetpack in the air is put back.
        Store32(base, ctrl + kCtrlAsked, 2);
        extra += "; the mount asked vehicle state 1 for the flying jetpack: asked state put back to 2";
      }
      if (REXCVAR_GET(pdz_counterop_jetpack_air))
      {
        const std::string mode = REXCVAR_GET(pdz_counterop_jetpack_air_shell);
        js.shell_want = mode == "on" ? 1 : mode == "off" ? 0 : (shell_before ? 1 : 0);
        js.watch_ctrl = ctrl;
        js.watch_prop = prop;
        js.watch_guard = guard;
        js.watch_at = std::chrono::steady_clock::now();
        js.watch_air = air;
        js.watch_seated = false;
        js.shell_fixes = 0;
        js.low_y = LoadFloat(base, prop + kPropPosition + 4);
        if (!air)
        {
          for (int i = 0; i < 3; ++i)
          {
            js.watch_pos[i] = LoadFloat(base, prop + kPropPosition + 4 * i);
          }
        }
        extra += fmt::format("; shell {} before, {} now, kept {} (pdz_counterop_jetpack_air_shell {})", shell_before,
                             base[ctrl + kJetShell], js.shell_want ? "on" : "off", mode);
      }
    }
    return fmt::format("jetpack: player 2 {} the jetpack {:08X} (Rare's mount 8266A8E8, seat 0, as the Use action{}): "
                       "seat state {}, vehicle state {} -> {}, rider state {}, attached to {:08X}{}",
                       ok ? "mounts" : "was REFUSED by", prop, air ? ", in the same tick as the guard left the seat" : "",
                       Load32(base, ctrl + kSeatState), Load32(base, ctrl + kCtrlState),
                       Load32(base, ctrl + kCtrlState + 4), Load32(base, ctrl + kJetRider), Load32(base, p2 + kParent),
                       extra);
  }

  bool HoldRevive(PPCContext &ctx, uint8_t *base, uint32_t p2)
  {
    const uint32_t prop = VehicleOf(base, p2);
    const uint32_t ctrl = JetpackController(base, prop);
    const int seat = ctrl ? SeatOf(base, ctrl, p2) : -1;
    const auto now = std::chrono::steady_clock::now();
    if (seat < 0)
    {
      if (js.hold)
      {
        REXLOG_INFO("pdz_counterop: jetpack: player 2 is out of the jetpack seat after {:.2f} s: Rare's revive runs",
                    std::chrono::duration<double>(now - js.hold_since).count());
      }
      js.hold = false;
      js.dismount_asked = false;
      js.riding = 0;
      return false;
    }
    if (!js.hold)
    {
      js.hold = true;
      js.hold_since = now;
      js.seat_death = true; // round 13: his next body is a guard on foot (pdz_counterop_jetpack_air_after_seat_death)
    }
    if (!js.dismount_asked && Load32(base, ctrl + seat * kSeatBytes + kSeatState) == 3)
    {
      js.dismount_asked = true;
      const uint32_t asked = Call(ctx, base, sub_8266ADE0, ctrl, uint32_t(seat)) & 0xFF;
      REXLOG_INFO("pdz_counterop: jetpack: player 2 died in the jetpack {:08X} (seat {}, rider state {}, on the ground "
                  "{}): Rare's exit {} (8266ADE0)",
                  prop, seat, Load32(base, ctrl + kJetRider), base[ctrl + kJetGround], asked ? "asked" : "refused");
    }
    if (std::chrono::duration<double>(now - js.hold_since).count() >= kHoldCap)
    {
      REXLOG_WARN("pdz_counterop: jetpack: player 2 still in seat {} of {:08X} after {:.0f} s (seat state {}, rider "
                  "state {}): Rare's revive runs anyway",
                  seat, prop, kHoldCap, Load32(base, ctrl + seat * kSeatBytes + kSeatState),
                  Load32(base, ctrl + kJetRider));
      js.hold = false;
      js.dismount_asked = false;
      return false;
    }
    return true;
  }

  // Why Rare's CanEnter (82684970 -> 8266AD18) would refuse seat 0 now: every field it reads, in its order, plus the
  // two things the Use path checks before it (player 2's parent link, the Use action's lock).
  static std::string EnterFields(const uint8_t *base, uint32_t prop, uint32_t ctrl, uint32_t p2)
  {
    // 8254DAB8(p2, 8), the Use action's lock (825AA9C0 asks it before the Use): one counter byte per action bit, at
    // +0x1699 for a locally controlled character (+0x40 bit 0x2) unless game type 3, else +0x16D9; mask 8 is taken
    // as index 3 (8254D8A8 not read: UNSURE).
    return fmt::format("seat 0 state {}, usable +0x3C {} +0x3D {}, prop +0x40 {:08X} (destroyed: bit 0x8000, or "
                       "0x10000 with health {:.2f} low), enter callback {:08X}, rider state {}, vehicle state {} -> {}, "
                       "on the ground {}; player 2 parent {:08X}, +0x40 {:08X}, Use lock counters {} / {}",
                       Load32(base, ctrl + kSeatState), base[ctrl + 0x3C], base[ctrl + 0x3D],
                       Load32(base, prop + kPropFlags), LoadFloat(base, prop + kPropHealth),
                       Load32(base, ctrl + kCtrlEnterCallback), Load32(base, ctrl + kJetRider),
                       Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlState + 4), base[ctrl + kJetGround],
                       Load32(base, p2 + kParent), Load32(base, p2 + kPropFlags), base[p2 + 0x1699 + 3],
                       base[p2 + 0x16D9 + 3]);
  }

  void OnInteract(const uint8_t *, uint32_t prop, uint32_t, uint32_t kind)
  {
    if (kind != 3 || !REXCVAR_GET(pdz_counterop_jetpack))
    {
      return;
    }
    // Runs inside Rare's Use (825D2E90 -> 825199F0 -> 825178B0): only noted here, looked at in Tick.
    js.use_prop = prop;
    js.use_at = std::chrono::steady_clock::now();
    ++js.uses;
  }

  // Round 13: after a mount of ours. One line when he is seated (or is not after kAirWatch); the shell kept as
  // pdz_counterop_jetpack_air_shell says (Datacore's mount callback 822F4BB8 switches it off for a player).
  static void AirWatch(PPCContext &ctx, uint8_t *base, uint32_t p2, std::chrono::steady_clock::time_point now)
  {
    const uint32_t ctrl = js.watch_ctrl, prop = js.watch_prop;
    const double since = std::chrono::duration<double>(now - js.watch_at).count();
    if (JetpackController(base, prop) != ctrl || Load32(base, ctrl + kSeatOccupant) != p2)
    {
      REXLOG_INFO("pdz_counterop: jetpack air: player 2 is no longer on the jetpack {:08X} {:.2f} s after the mount "
                  "(seated {}): the watch ends",
                  prop, since, js.watch_seated ? "yes" : "NO");
      js.watch_ctrl = 0;
      return;
    }
    const float y = LoadFloat(base, prop + kPropPosition + 4);
    js.low_y = std::min(js.low_y, y);
    if (js.shell_want >= 0 && base[ctrl + kJetShell] != uint8_t(js.shell_want) && js.shell_fixes < 4)
    {
      const uint8_t was = base[ctrl + kJetShell];
      Call(ctx, base, sub_8268A550, ctrl, uint32_t(js.shell_want));
      ++js.shell_fixes;
      REXLOG_INFO("pdz_counterop: jetpack air: shell of {:08X} was {} {:.2f} s after the mount (the level's rule for a "
                  "player): Rare's 8268A550(ctrl, {}) put it back as the guard had it; now {} (fix {})",
                  prop, was, since, js.shell_want, base[ctrl + kJetShell], js.shell_fixes);
    }
    if (!js.watch_seated && Load32(base, ctrl + kSeatState) == 3)
    {
      js.watch_seated = true;
      const uint32_t part = Load32(base, p2 + kParticipantOf);
      REXLOG_INFO("pdz_counterop: jetpack air: player 2 is SEATED on the jetpack {:08X} {:.2f} s after the {}: at "
                  "({:.0f}, {:.0f}, {:.0f}), {:.1f} m below where it was (lowest {:.1f} m below); seat state 3, rider "
                  "state {}, vehicle state {} -> {}, on the ground {}, shell {}, pad context {}, health {:.1f}",
                  prop, since, js.watch_air ? "hand-over" : "mount", LoadFloat(base, prop + kPropPosition), y,
                  LoadFloat(base, prop + kPropPosition + 8), (js.watch_pos[1] - y) / 100.0f,
                  (js.watch_pos[1] - js.low_y) / 100.0f, Load32(base, ctrl + kJetRider),
                  Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked), base[ctrl + kJetGround],
                  base[ctrl + kJetShell], Ptr(part) ? Load32(base, part + 0x698) : 0u, LoadFloat(base, p2 + kHealth));
    }
    if (since >= kAirWatch)
    {
      if (!js.watch_seated)
      {
        REXLOG_WARN("pdz_counterop: jetpack air: player 2 is NOT seated {:.0f} s after the mount on {:08X} (seat state "
                    "{}, rider state {}, vehicle state {} -> {})",
                    kAirWatch, prop, Load32(base, ctrl + kSeatState), Load32(base, ctrl + kJetRider),
                    Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked));
      }
      js.watch_ctrl = 0;
    }
  }

  // Round 13: every 2 s, each guard newly found in a jetpack seat is listed once with his side by Rare's masks (which
  // riders of a level are the agent's enemies: only those are ever handed over). Also the test tool's kill.
  static void AirList(PPCContext &ctx, uint8_t *base, uint32_t p2, std::chrono::steady_clock::time_point now)
  {
    const int32_t kill = REXCVAR_GET(pdz_counterop_test_jet_kill_riders);
    const bool do_kill = kill != js.last_test_kill && kill != 0;
    js.last_test_kill = kill;
    if (!do_kill && now < js.next_list)
    {
      return;
    }
    js.next_list = now + std::chrono::seconds(2);
    const uint32_t world = Load32(base, kGame + 24);
    const uint32_t agent = PdzCounterOpAgent();
    if (!Ptr(world))
    {
      return;
    }
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    uint32_t riders[32];
    uint32_t n = 0;
    // Sun 22 Nov night (audit): the nodes and the occupant (a just-killed rider may be freed) read only where readable.
    for (int i = 0; Rd(base, node, 16) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      const uint32_t ctrl = JetpackController(base, o);
      const uint32_t c = ctrl ? Load32(base, ctrl + kSeatOccupant) : 0;
      if (!Rd(base, c, 0x1A00) || Load32(base, c) != kGuardVtable || Load32(base, ctrl + kSeatState) == 0)
      {
        continue;
      }
      if (do_kill && n < 32 && c != p2 && Load32(base, ctrl + kSeatState) == 3 && LoadFloat(base, c + kHealth) > 0.0f)
      {
        riders[n++] = c;
      }
      const auto told = js.told_riders.find(c);
      if (told != js.told_riders.end() && told->second == o)
      {
        continue;
      }
      if (js.told_riders.size() >= 64)
      {
        continue;
      }
      js.told_riders[c] = o;
      const uint32_t setup = Load32(base, c + kSetup);
      const uint16_t team = Load16(base, c + kTeamBits), allies = Load16(base, c + kAllies),
                     enemies = Load16(base, c + kEnemies);
      std::string side = "the agent is not known";
      if (Ptr(agent))
      {
        const uint16_t a_team = Load16(base, agent + kTeamBits), a_allies = Load16(base, agent + kAllies),
                       a_enemies = Load16(base, agent + kEnemies);
        // Rare's friend test (8251EBD0), both ways, as counter_op.cpp's HostileToAgent.
        const bool he_likes_her = (a_team & (team | allies)) != 0 && (a_team & enemies) == 0;
        const bool she_likes_him = (team & (a_team | a_allies)) != 0 && (team & a_enemies) == 0;
        side = !he_likes_her && !she_likes_him ? "the agent's ENEMY (may be handed over)"
                                               : "on the agent's side (never taken)";
      }
      REXLOG_INFO("pdz_counterop: jetpack air: rider '{}' {:08X} (setup team byte {}, masks {:04X} / {:04X} / {:04X}) "
                  "is in the seat of the jetpack {:08X} at ({:.0f}, {:.0f}, {:.0f}): {}; seat state {}, rider state {}, "
                  "vehicle state {}, shell {}, usable {} / {}",
                  IdOf(base, c), c, Ptr(setup) ? base[setup + kSetupTeam] : 255, team, allies, enemies, o,
                  LoadFloat(base, o + kPropPosition), LoadFloat(base, o + kPropPosition + 4),
                  LoadFloat(base, o + kPropPosition + 8), side, Load32(base, ctrl + kSeatState),
                  Load32(base, ctrl + kJetRider), Load32(base, ctrl + kCtrlState), base[ctrl + kJetShell],
                  base[ctrl + 0x3C], base[ctrl + 0x3D]);
    }
    for (uint32_t i = 0; i < n; ++i)
    {
      Call(ctx, base, sub_8252DE88, riders[i], agent, 5, 1);
      REXLOG_INFO("pdz_counterop: jetpack air: TEST (pdz_counterop_test_jet_kill_riders {}): Rare's kill (8252DE88, "
                  "killer the agent) on the seated rider '{}' {:08X}: health now {:.1f}",
                  kill, IdOf(base, riders[i]), riders[i], LoadFloat(base, riders[i] + kHealth));
    }
  }

  // ---- Sun 22 Nov (round 17): the destroy tool and the jetpack watch (single player too) ----
  struct JetSeen
  {
    uint32_t state = 0xFFFFFFFF, asked = 0, rider = 0, seat = 0, occupant = 0, flags = 0;
    uint8_t ground = 0;
    float health = 0.0f;
  };
  std::unordered_map<uint32_t, JetSeen> g_jet_seen;
  int32_t g_jet_destroy_last = 0;
  std::chrono::steady_clock::time_point g_jet_watch_next{};
  uint32_t g_jet_watch_world = 0;

  static std::string OccupantName(const uint8_t *base, uint32_t c, uint32_t p1, uint32_t p2)
  {
    if (!Ptr(c))
    {
      return "nobody";
    }
    if (c == p2)
    {
      return fmt::format("player 2 {:08X}", c);
    }
    if (c == p1)
    {
      return fmt::format("the agent {:08X}", c);
    }
    return fmt::format("'{}' {:08X}", IdOf(base, c), c);
  }

  int32_t g_sp_kill_last = 0;

  // Sun 22 Nov (item 6): the ride log's animation counts (anim_log.cpp's play hook calls PdzRideAnimPlay).
  struct RideAnims
  {
    uint32_t who = 0;
    std::vector<std::pair<std::string, uint32_t>> keys; // "name@caller"
    std::vector<uint32_t> counts;
  };
  RideAnims g_ride_anim[2]; // 0 = player 2, 1 = the AI rider
  std::atomic<uint32_t> g_ride_watch[2] = {};
  std::chrono::steady_clock::time_point g_ride_next{};

  std::string RideAnimText(RideAnims &r)
  {
    std::string out;
    for (size_t i = 0; i < r.keys.size(); ++i)
    {
      out += fmt::format("{}{} x{} @{:08X}", out.empty() ? "" : ", ", r.keys[i].first, r.counts[i], r.keys[i].second);
    }
    r.keys.clear();
    r.counts.clear();
    return out.empty() ? std::string("none") : out;
  }

  std::string RideLine(const uint8_t *base, uint32_t c, uint32_t prop, uint32_t ctrl)
  {
    return fmt::format("on {:08X} at ({:.0f}, {:.0f}, {:.0f}): seat state {}, rider state {}, vehicle state {} -> {}, "
                       "on the ground {}, shell {}; hands {:08X} / {:08X}, hand animation bytes {} / {}, wall mask +0x1CC0 "
                       "{:08X}, parent {:08X}",
                       prop, LoadFloat(base, prop + kPropPosition), LoadFloat(base, prop + kPropPosition + 4),
                       LoadFloat(base, prop + kPropPosition + 8), Load32(base, ctrl + kSeatState), Load32(base, ctrl + kJetRider),
                       Load32(base, ctrl + kCtrlState), Load32(base, ctrl + kCtrlAsked), base[ctrl + kJetGround],
                       base[ctrl + kJetShell], Load32(base, c + 4076), Load32(base, c + 4080), base[c + 4072], base[c + 4073],
                       Load32(base, c + 0x1CC0), Load32(base, c + kParent));
  }

  void RideLogTick(const uint8_t *base, uint32_t p2)
  {
    if (!REXCVAR_GET(pdz_counterop_jetpack_ride_log))
    {
      g_ride_watch[0] = g_ride_watch[1] = 0;
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < g_ride_next)
    {
      return;
    }
    g_ride_next = now + std::chrono::seconds(1);
    // Player 2 on a jetpack, and the first seated AI rider of the world list.
    uint32_t who[2] = {0, 0}, props[2] = {0, 0}, ctrls[2] = {0, 0};
    if (Rd(base, p2, 0x1CC4))
    {
      const uint32_t prop = VehicleOf(base, p2);
      const uint32_t ctrl = JetpackController(base, prop);
      if (ctrl && SeatOf(base, ctrl, p2) >= 0)
      {
        who[0] = p2, props[0] = prop, ctrls[0] = ctrl;
      }
    }
    const uint32_t world = Load32(base, kGame + 24);
    const uint32_t end = Rd(base, world + kWorldFirst, 8) ? Load32(base, world + kWorldEnd) : 0;
    uint32_t node = Rd(base, world + kWorldFirst, 8) ? Load32(base, world + kWorldFirst) : 0;
    for (int i = 0; Rd(base, node, 16) && node != end && i < 16384 && !who[1]; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      const uint32_t ctrl = JetpackController(base, o);
      const uint32_t c = ctrl ? Load32(base, ctrl + kSeatOccupant) : 0;
      if (Rd(base, c, 0x1CC4) && c != p2 && Load32(base, c) == kGuardVtable && Load32(base, ctrl + kSeatState) == 3 &&
          LoadFloat(base, c + kHealth) > 0.0f)
      {
        who[1] = c, props[1] = o, ctrls[1] = ctrl;
      }
    }
    for (int k = 0; k < 2; ++k)
    {
      if (g_ride_anim[k].who != who[k])
      {
        g_ride_anim[k] = RideAnims{};
        g_ride_anim[k].who = who[k];
      }
      g_ride_watch[k].store(who[k]);
      if (who[k])
      {
        REXLOG_INFO("pdz_counterop: jetpack ride log: {} {:08X} {}; animations played this second: {}",
                    k == 0 ? "PLAYER 2" : fmt::format("AI rider '{}'", IdOf(base, who[k])), who[k],
                    RideLine(base, who[k], props[k], ctrls[k]), RideAnimText(g_ride_anim[k]));
      }
    }
  }

  void TestTools(PPCContext &ctx, uint8_t *base, uint32_t p1, uint32_t p2)
  {
    RideLogTick(base, p2); // Sun 22 Nov (item 6): pdz_counterop_jetpack_ride_log
    PdzRideSlotsFrame(base); // round 18 (item 6): the body's animation slots of the watched riders
    {
      PPCContext saved = ctx;
      PdzSpiderTestTick(saved, base, p1); // Sun 22 Nov night (Part 3): the spiderbot test kill / landing log
    }
    // pdz_counterop_test_jet_kill_riders outside a Counter-Op game (single player): Rare's kill on every seated AI
    // rider, the agent as the killer (in Counter-Op AirList does it).
    const int32_t sp_kill = REXCVAR_GET(pdz_counterop_test_jet_kill_riders);
    if (sp_kill != g_sp_kill_last && sp_kill != 0 && !PdzCounterOpAgent())
    {
      const uint32_t world = Load32(base, kGame + 24);
      const uint32_t end = Rd(base, world + kWorldFirst, 8) ? Load32(base, world + kWorldEnd) : 0;
      uint32_t node = Rd(base, world + kWorldFirst, 8) ? Load32(base, world + kWorldFirst) : 0;
      uint32_t riders[16];
      uint32_t n = 0;
      for (int i = 0; Rd(base, node, 16) && node != end && i < 16384 && n < 16; ++i, node = Load32(base, node + 12))
      {
        const uint32_t ctrl = JetpackController(base, Load32(base, node + 4));
        const uint32_t c = ctrl ? Load32(base, ctrl + kSeatOccupant) : 0;
        if (Rd(base, c, 0x1A00) && Load32(base, c) == kGuardVtable && Load32(base, ctrl + kSeatState) == 3 && LoadFloat(base, c + kHealth) > 0.0f)
        {
          riders[n++] = c;
        }
      }
      for (uint32_t i = 0; i < n; ++i)
      {
        Call(ctx, base, sub_8252DE88, riders[i], p1, 5, 1);
        REXLOG_INFO("pdz_counterop: jetpack TEST kill (pdz_counterop_test_jet_kill_riders {}, single player): Rare's kill "
                    "(8252DE88, killer the agent) on the seated rider '{}' {:08X}: health now {:.1f}",
                    sp_kill, IdOf(base, riders[i]), riders[i], LoadFloat(base, riders[i] + kHealth));
      }
      if (n == 0)
      {
        REXLOG_INFO("pdz_counterop: jetpack TEST kill (pdz_counterop_test_jet_kill_riders {}, single player): no seated rider", sp_kill);
      }
    }
    g_sp_kill_last = sp_kill;
    const int32_t want = REXCVAR_GET(pdz_counterop_test_jet_destroy);
    const bool destroy = want != g_jet_destroy_last && want != 0;
    g_jet_destroy_last = want;
    const bool watch = REXCVAR_GET(pdz_counterop_test_jet_watch);
    const auto now = std::chrono::steady_clock::now();
    if (!destroy && (!watch || now < g_jet_watch_next))
    {
      return;
    }
    g_jet_watch_next = now + std::chrono::milliseconds(100);
    const uint32_t world = Load32(base, kGame + 24);
    if (!Rd(base, world + kWorldFirst, 8))
    {
      return;
    }
    if (world != g_jet_watch_world)
    {
      g_jet_watch_world = world;
      g_jet_seen.clear();
    }
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    uint32_t hit[16];
    uint32_t n = 0, nearest = 0;
    float nearest_d = 1e30f;
    for (int i = 0; Rd(base, node, 16) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      const uint32_t ctrl = JetpackController(base, o);
      if (!ctrl)
      {
        continue;
      }
      const uint32_t occ = Load32(base, ctrl + kSeatOccupant);
      const uint32_t seat = Load32(base, ctrl + kSeatState);
      if (watch)
      {
        JetSeen now_seen;
        now_seen.state = Load32(base, ctrl + kCtrlState);
        now_seen.asked = Load32(base, ctrl + kCtrlAsked);
        now_seen.rider = Load32(base, ctrl + kJetRider);
        now_seen.seat = seat;
        now_seen.occupant = occ;
        now_seen.flags = Load32(base, o + kPropFlags);
        now_seen.ground = base[ctrl + kJetGround];
        now_seen.health = LoadFloat(base, o + kPropHealth);
        JetSeen &was = g_jet_seen[o];
        if (was.state != now_seen.state || was.asked != now_seen.asked || was.rider != now_seen.rider ||
            was.seat != now_seen.seat || was.occupant != now_seen.occupant || was.flags != now_seen.flags ||
            was.ground != now_seen.ground)
        {
          REXLOG_INFO("pdz_counterop: jetpack watch: {:08X} (controller {:08X}) at ({:.0f}, {:.0f}, {:.0f}): vehicle "
                      "state {} -> asked {}, rider state {}, seat state {}, on it {}, on the ground {}, prop flags "
                      "{:08X}, health {:.1f}{}",
                      o, ctrl, LoadFloat(base, o + kPropPosition), LoadFloat(base, o + kPropPosition + 4),
                      LoadFloat(base, o + kPropPosition + 8), now_seen.state, now_seen.asked, now_seen.rider,
                      now_seen.seat, OccupantName(base, occ, p1, p2), now_seen.ground, now_seen.flags,
                      now_seen.health, was.state == 0xFFFFFFFF ? " (first look)" : "");
          was = now_seen;
        }
      }
      if (!destroy || n >= 15)
      {
        continue;
      }
      const bool ai = Rd(base, occ, 4) && occ != p2 && Load32(base, occ) == kGuardVtable && seat == 3;
      const bool his = Ptr(p2) && occ == p2;
      const bool empty = !Ptr(occ) || seat == 0;
      if ((want == 1 && ai) || (want == 2 && his) || (want == 3 && empty) || want == 4)
      {
        hit[n++] = o;
      }
      if (want == 5 && Rd(base, p1, kPosition + 12))
      {
        const float dx = LoadFloat(base, o + kPropPosition) - LoadFloat(base, p1 + kPosition);
        const float dy = LoadFloat(base, o + kPropPosition + 4) - LoadFloat(base, p1 + kPosition + 4);
        const float dz = LoadFloat(base, o + kPropPosition + 8) - LoadFloat(base, p1 + kPosition + 8);
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < nearest_d)
        {
          nearest_d = d;
          nearest = o;
        }
      }
    }
    if (destroy && want == 5 && nearest)
    {
      hit[n++] = nearest;
    }
    if (destroy && n == 0)
    {
      REXLOG_INFO("pdz_counterop: jetpack TEST destroy (pdz_counterop_test_jet_destroy {}): no jetpack of that kind", want);
    }
    for (uint32_t i = 0; i < n; ++i)
    {
      const uint32_t o = hit[i];
      const uint32_t comp = Load32(base, o + 0x74);
      const uint32_t ctrl = JetpackController(base, o);
      if (!Rd(base, comp, 12) || Load32(base, comp) != o)
      {
        REXLOG_INFO("pdz_counterop: jetpack TEST destroy: {:08X} has no damage part of its own (+0x74 {:08X}): left", o, comp);
        continue;
      }
      const float before = LoadFloat(base, o + kPropHealth);
      const uint32_t occ = ctrl ? Load32(base, ctrl + kSeatOccupant) : 0;
      REXLOG_INFO("pdz_counterop: jetpack TEST destroy (pdz_counterop_test_jet_destroy {}): {:08X} at ({:.0f}, {:.0f}, "
                  "{:.0f}), on it {}, seat state {}, rider state {}, vehicle state {}, on the ground {}: the game's prop "
                  "damage (825EFA40, 100000, causer the agent {:08X})",
                  want, o, LoadFloat(base, o + kPropPosition), LoadFloat(base, o + kPropPosition + 4),
                  LoadFloat(base, o + kPropPosition + 8), OccupantName(base, occ, p1, p2),
                  ctrl ? Load32(base, ctrl + kSeatState) : 0u, ctrl ? Load32(base, ctrl + kJetRider) : 0u,
                  ctrl ? Load32(base, ctrl + kCtrlState) : 0u, ctrl ? base[ctrl + kJetGround] : 0, p1);
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = comp;
        frame.ctx.r4.u64 = p1;
        frame.ctx.f1.f64 = 100000.0;
        frame.ctx.f2.f64 = 0.0;
        sub_825EFA40(frame.ctx, base);
      }
      REXLOG_INFO("pdz_counterop: jetpack TEST destroy: {:08X} after: health {:.1f} -> {:.1f}, part state {}, vehicle state "
                  "{} -> asked {}, rider state {}, seat state {}",
                  o, before, LoadFloat(base, o + kPropHealth), Load32(base, comp + 8),
                  ctrl ? Load32(base, ctrl + kCtrlState) : 0u, ctrl ? Load32(base, ctrl + kCtrlAsked) : 0u,
                  ctrl ? Load32(base, ctrl + kJetRider) : 0u, ctrl ? Load32(base, ctrl + kSeatState) : 0u);
    }
  }

  // ---- Round 13b (helper s): his death in the seat ----
  static float HumanHealth(const uint8_t *base, uint32_t p2)
  {
    const uint32_t part = Rd(base, p2 + kParticipantOf, 4) ? Load32(base, p2 + kParticipantOf) : 0;
    return Rd(base, part + kPartHealth, 4) ? LoadFloat(base, part + kPartHealth) : 0.0f;
  }
  static uint32_t PadContext(const uint8_t *base, uint32_t p2)
  {
    const uint32_t part = Rd(base, p2 + kParticipantOf, 4) ? Load32(base, p2 + kParticipantOf) : 0;
    return Rd(base, part + 0x698, 4) ? Load32(base, part + 0x698) : 0u;
  }

  // One line for the log: the jetpack, player 2 and the guard he had replaced.
  static std::string DeathState(const uint8_t *base, uint32_t p2)
  {
    JetState::Death &d = js.death;
    const bool valid = JetpackController(base, d.prop) == d.ctrl;
    std::string jet = "the jetpack prop is gone";
    if (valid)
    {
      const float x = LoadFloat(base, d.prop + kPropPosition), y = LoadFloat(base, d.prop + kPropPosition + 4),
                  z = LoadFloat(base, d.prop + kPropPosition + 8);
      const float moved = std::sqrt((x - d.last_jet[0]) * (x - d.last_jet[0]) + (y - d.last_jet[1]) * (y - d.last_jet[1]) +
                                    (z - d.last_jet[2]) * (z - d.last_jet[2]));
      d.last_jet[0] = x;
      d.last_jet[1] = y;
      d.last_jet[2] = z;
      jet = fmt::format("jetpack {:08X} at ({:.0f}, {:.0f}, {:.0f}), {:.1f} m below where he was hit, moved {:.2f} m "
                        "since the last line; seat state {}, occupant {:08X}, exit asked {}, rider state {}, vehicle "
                        "state {} -> {}, near ground {}, usable {} / {}, shell {}, prop flags {:08X}, free for a rider "
                        "{}, open to player 2 {}",
                        d.prop, x, y, z, (d.pos[1] - y) / 100.0f, moved / 100.0f, Load32(base, d.ctrl + kSeatState),
                        Load32(base, d.ctrl + kSeatOccupant), base[d.ctrl + 0x41], Load32(base, d.ctrl + kJetRider),
                        Load32(base, d.ctrl + kCtrlState), Load32(base, d.ctrl + kCtrlAsked), base[d.ctrl + kJetGround],
                        base[d.ctrl + 0x3C], base[d.ctrl + 0x3D], base[d.ctrl + kJetShell],
                        Load32(base, d.prop + kPropFlags), Free(base, d.ctrl) ? "yes" : "no",
                        PdzCounterOpVehicleAllowed(d.ctrl) ? "yes" : "no");
    }
    const std::string him =
        fmt::format("player 2 at ({:.0f}, {:.0f}, {:.0f}), parent {:08X}, health {:.2f}, floor {:.2f}, pad context {}, "
                    "+0x44 {:08X}",
                    LoadFloat(base, p2 + kPosition), LoadFloat(base, p2 + kPosition + 4),
                    LoadFloat(base, p2 + kPosition + 8), Load32(base, p2 + kParent), HumanHealth(base, p2),
                    LoadFloat(base, p2 + kHealthFloor), PadContext(base, p2), Load32(base, p2 + kFlags44));
    std::string guard = "no replaced guard known";
    if (Ptr(d.guard))
    {
      // Sun 22 Nov night (audit): the replaced guard may be deleted meanwhile: read only where readable.
      if (!Rd(base, d.guard, 0x1A00) || Load32(base, d.guard) != kGuardVtable)
      {
        guard = fmt::format("the replaced guard {:08X} is no guard object any more", d.guard);
      }
      else
      {
        const float x = LoadFloat(base, d.guard + kPosition), y = LoadFloat(base, d.guard + kPosition + 4),
                    z = LoadFloat(base, d.guard + kPosition + 8);
        const float moved =
            std::sqrt((x - d.last_guard[0]) * (x - d.last_guard[0]) + (y - d.last_guard[1]) * (y - d.last_guard[1]) +
                      (z - d.last_guard[2]) * (z - d.last_guard[2]));
        d.last_guard[0] = x;
        d.last_guard[1] = y;
        d.last_guard[2] = z;
        guard = fmt::format("replaced guard '{}' {:08X} at ({:.0f}, {:.0f}, {:.0f}), moved {:.2f} m since the last "
                            "line, health {:.1f}, {}, dead flag {}, parent {:08X}",
                            IdOf(base, d.guard), d.guard, x, y, z, moved / 100.0f, LoadFloat(base, d.guard + kHealth),
                            (Load32(base, d.guard + kHidden) & 0x10) ? "hidden" : "in the world",
                            (Load32(base, d.guard + kFlags44) & 8) ? "set" : "not set", Load32(base, d.guard + kParent));
      }
    }
    return jet + "; " + him + "; " + guard;
  }

  // pdz_counterop_jetpack_seat_death: the death is noticed in the seat (the hit that took his health to 0 while he
  // sits; Rare's rider death takes him off about 0.8 s later, long before his revive is asked), then followed.
  static void SeatDeathTick(PPCContext &ctx, uint8_t *base, uint32_t p2, std::chrono::steady_clock::time_point now,
                            uint32_t riding_now, uint32_t ctrl_now)
  {
    JetState::Death &d = js.death;
    if (!js.death_latched && riding_now && ctrl_now && HumanHealth(base, p2) <= 0.01f &&
        std::chrono::duration<double>(now - js.mount_at).count() > 0.3)
    {
      js.death_latched = true;
      js.seat_death = true; // his next body is a guard on foot (pdz_counterop_jetpack_air_after_seat_death off)
      d = JetState::Death{};
      d.active = true;
      d.at = now;
      d.next = now + std::chrono::seconds(1);
      d.prop = riding_now;
      d.ctrl = ctrl_now;
      d.guard = js.given_prop == riding_now ? js.given_guard : 0;
      for (int i = 0; i < 3; ++i)
      {
        d.pos[i] = d.last_jet[i] = LoadFloat(base, riding_now + kPropPosition + 4 * i);
        d.last_guard[i] = Ptr(d.guard) ? LoadFloat(base, d.guard + kPosition + 4 * i) : 0.0f;
      }
      d.near_ground = base[ctrl_now + kJetGround] != 0;
      float ground = 0.0f;
      if (GroundBelow(ctx, base, riding_now, 30000.0f, ground))
      {
        d.height = ground;
      }
      REXLOG_INFO("pdz_counterop: jetpack seat death: PLAYER 2 IS KILLED IN THE SEAT of the jetpack {:08X} (life as "
                  "guard {:08X}), {:.2f} s after the mount: {}{}; seat state {}, rider state {}, vehicle state {} -> {}; "
                  "his next body will be a guard on foot (pdz_counterop_jetpack_air_after_seat_death {}); {}",
                  riding_now, d.guard, std::chrono::duration<double>(now - js.mount_at).count(),
                  d.height < 0.0f ? std::string("no ground found within 300 m below")
                                  : fmt::format("ground {:.1f} m below the jetpack", d.height / 100.0f),
                  d.near_ground ? " (near-ground byte set: hover height)" : " (in the air)",
                  Load32(base, ctrl_now + kSeatState), Load32(base, ctrl_now + kJetRider),
                  Load32(base, ctrl_now + kCtrlState), Load32(base, ctrl_now + kCtrlAsked),
                  REXCVAR_GET(pdz_counterop_jetpack_air_after_seat_death) ? "is ON: a rider may be picked" : "off",
                  DeathState(base, p2));
    }
    if (!d.active)
    {
      return;
    }
    const double since = std::chrono::duration<double>(now - d.at).count();
    if (d.off_seat_s < 0.0 && !riding_now)
    {
      d.off_seat_s = since;
      REXLOG_INFO("pdz_counterop: jetpack seat death: +{:.2f} s: player 2 is OFF THE SEAT: {}", since,
                  DeathState(base, p2));
    }
    if (now < d.next)
    {
      return;
    }
    d.next += std::chrono::seconds(1);
    ++d.lines;
    if (d.lines < kDeathWatchLines)
    {
      REXLOG_INFO("pdz_counterop: jetpack seat death: +{:.0f} s: {}", since, DeathState(base, p2));
      return;
    }
    d.active = false;
    const bool valid = JetpackController(base, d.prop) == d.ctrl;
    REXLOG_INFO("pdz_counterop: jetpack seat death: SUMMARY after {:.0f} s: he was off the seat {} after the hit; next "
                "body {}; the jetpack is {}; player 2 {}; {}",
                since, d.off_seat_s < 0.0 ? std::string("NEVER (still seated)") : fmt::format("{:.2f} s", d.off_seat_s),
                d.new_body_s < 0.0 ? std::string("NOT YET")
                                   : fmt::format("guard {:08X} {:.2f} s after the hit", d.new_body, d.new_body_s),
                !valid                                       ? "gone"
                : Load32(base, d.ctrl + kCtrlState) == 3     ? "WRECKED (vehicle state 3)"
                : Load32(base, d.ctrl + kSeatState) != 0     ? "NOT EMPTY (a seat state is left)"
                : Load32(base, d.ctrl + kJetRider) != 0      ? "empty but a RIDER STATE IS LEFT"
                : Free(base, d.ctrl)                         ? "whole, empty and usable by a rider"
                                                             : "whole and empty, its seat closed (usable bytes)",
                VehicleOf(base, p2) == d.prop ? "is STILL ATTACHED to it" : "is not attached to it",
                DeathState(base, p2));
  }

  // TESTING TOOL (pdz_counterop_test_jet_kill): the agent's view at the rider (his +48 is waist height; hers too).
  static void AimAgentAtRider(PPCContext &ctx, uint8_t *base, uint32_t agent, uint32_t p2)
  {
    const float dx = LoadFloat(base, p2 + kPosition) - LoadFloat(base, agent + kPosition);
    const float dy = LoadFloat(base, p2 + kPosition + 4) + 15.0f - (LoadFloat(base, agent + kPosition + 4) + 62.0f);
    const float dz = LoadFloat(base, p2 + kPosition + 8) - LoadFloat(base, agent + kPosition + 8);
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(flat) || flat < 50.0f)
    {
      return;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = agent;
    frame.ctx.f1.f64 = -std::atan2(double(dy), double(flat)); // 825AD3B0's pitch is positive down
    frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
    sub_825AD3B0(frame.ctx, base);
  }

  // TESTING TOOL: one killing hit on player 2 through the game's character damage routine, exactly as Rare's kill
  // (8252DE88 at 8252DEC0..8252DF28) calls it: vtable +820 (victim, attacker, f1 = his health as the damage, f2 = his
  // full health, f3 = Rare's constant, r6 0, r7 3, r8 0, r9 0, r10 5, stack bytes +87 / +111 = 0, word +100). The
  // attacker is the agent. Only the damage call: Rare's kill's own tail (the guard's die) is not made for a human.
  static bool SeatHit(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t agent, std::string &how)
  {
    const uint32_t part = Load32(base, p2 + kParticipantOf);
    const uint32_t vtable = Load32(base, p2);
    // Sun 22 Nov night (audit): only the human class (player 2), whose vtable is in the game image.
    const uint32_t slot = vtable == 0x820C7DE0u ? Load32(base, vtable + kDamageSlot) : 0;
    PPCFunc *fn = slot ? pdz::GuestFunction(slot, "counter-op jetpack test kill") : nullptr;
    if (!fn || !Ptr(part))
    {
      how = fmt::format("no damage routine (vtable {:08X}, slot +820 = {:08X})", vtable, slot);
      return false;
    }
    const float before = LoadFloat(base, part + kPartHealth);
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
    const uint32_t sp = frame.ctx.r1.u32;
    std::memset(base + sp + 80, 0, 48); // the stack arguments (bytes +87 and +111 are 0 in Rare's kill)
    Store32(base, sp + 100, kKillArgWord);
    frame.ctx.r3.u64 = p2;
    frame.ctx.r4.u64 = agent;
    frame.ctx.r5.u64 = slot;
    frame.ctx.r6.u64 = 0;
    frame.ctx.r7.u64 = 3;
    frame.ctx.r8.u64 = 0;
    frame.ctx.r9.u64 = 0;
    frame.ctx.r10.u64 = 5;
    frame.ctx.f1.f64 = double(before);
    frame.ctx.f2.f64 = double(LoadFloat(base, p2 + kHealth)); // a human's +120 is his full health
    frame.ctx.f3.f64 = double(LoadFloat(base, kKillArgFloat));
    fn(frame.ctx, base);
    const float after = LoadFloat(base, part + kPartHealth);
    how = fmt::format("the game's damage routine {:08X} (vtable +820), attacker the agent {:08X}, damage {:.2f} of full "
                      "{:.1f}: health {:.2f} -> {:.2f}, floor {:.2f}",
                      slot, agent, before, LoadFloat(base, p2 + kHealth), before, after,
                      LoadFloat(base, p2 + kHealthFloor));
    return after <= 0.01f;
  }

  static void TestKillTick(PPCContext &ctx, uint8_t *base, uint32_t p2, std::chrono::steady_clock::time_point now,
                           uint32_t riding_now, uint32_t ctrl_now)
  {
    const std::string want = REXCVAR_GET(pdz_counterop_test_jet_kill);
    // The next step of the program (or its end).
    const auto next_step = [&](const char *why) {
      ++js.tk_step;
      js.tk_armed = js.tk_step < js.tk_prog.size() ? js.tk_prog[js.tk_step] : 0;
      js.tk_stage = 0;
      js.tk_applied = 0;
      js.tk_told = {};
      js.tk_seated_since = {};
      js.tk_check = {};
      REXLOG_INFO("pdz_counterop: jetpack: TEST (pdz_counterop_test_jet_kill): step {} of {} is done ({}); {}",
                  js.tk_step, js.tk_prog.size(), why,
                  js.tk_armed ? fmt::format("next step: mode {}", js.tk_armed) : std::string("the program is finished"));
    };
    if (want != g_tk_seen)
    {
      g_tk_seen = want;
      js.tk_prog.clear();
      for (const char ch : want)
      {
        if (ch >= '1' && ch <= '6')
        {
          js.tk_prog.push_back(ch - '0');
        }
      }
      js.tk_step = 0;
      js.tk_armed = js.tk_prog.empty() ? 0 : js.tk_prog[0];
      js.tk_stage = 0;
      js.tk_applied = 0;
      js.tk_told = {};
      js.tk_seated_since = {};
      js.tk_check = {};
      REXLOG_INFO("pdz_counterop: jetpack: TEST (pdz_counterop_test_jet_kill '{}'): {} step(s) armed, the first is "
                  "mode {}",
                  want, js.tk_prog.size(), js.tk_armed);
    }
    if (!js.tk_armed)
    {
      return;
    }
    const int mode = js.tk_armed;
    const uint32_t part = Load32(base, p2 + kParticipantOf);
    const uint32_t agent = PdzCounterOpAgent();
    if (!Ptr(part) || !Ptr(agent))
    {
      return;
    }
    const float health = LoadFloat(base, part + kPartHealth);
    std::string wait;
    if (mode != 1 && mode != 6 && js.tk_stage >= 1 && health <= 0.01f)
    {
      next_step(js.tk_stage == 1 ? "a REAL HIT ended his life while the agent's view was on him: the 'damage "
                                   "handler' line above names the attacker"
                                 : "the tool's hit ended his life");
      return;
    }
    if (mode == 1 || mode == 6)
    {
      const double since = std::chrono::duration<double>(now - js.spawn_at).count();
      if (riding_now || VehicleOf(base, p2))
      {
        if (mode == 6 && riding_now && health > 0.01f && !js.death_latched) // (not the seat he is dying in)
        {
          next_step("he is on a jetpack");
          return;
        }
        wait = "he is on a vehicle";
      }
      else if (js.tk_stage == 10 && js.spawn_fresh)
      {
        // The death the tool asked for has happened: he has a new body.
        if (mode == 1 || js.tk_applied >= 8)
        {
          next_step(mode == 1 ? "he died on foot and has a new body"
                              : "8 deaths on foot and still no jetpack: given up");
          return;
        }
        js.tk_stage = 0; // mode 6: this body too, until he sits on a jetpack
      }
      else if (!js.spawn_fresh && js.tk_stage == 10 && now >= js.tk_until && health > kOwnFloor + 0.05f)
      {
        // No new body 40 s after the tool lowered his health, and the health is up again (he waits out of the world
        // with a full bar; a get-up that held his own death back would look the same): once more.
        js.spawn_fresh = true;
        js.tk_stage = 0;
        wait = "no new body 40 s after the tool lowered his health: once more";
      }
      else if (!js.spawn_fresh || since < 3.0)
      {
        wait = js.spawn_fresh ? "his body is under 3 s old" : "no new body yet since the tool's last death on foot";
      }
      else if (health <= kOwnFloor + 0.05f)
      {
        wait = "his health is at his floor already";
      }
      else
      {
        StoreFloat(base, part + kPartHealth, kOwnFloor);
        js.spawn_fresh = false;
        js.tk_stage = 10;
        js.tk_until = now + std::chrono::seconds(40);
        ++js.tk_applied;
        REXLOG_INFO("pdz_counterop: jetpack: TEST (pdz_counterop_test_jet_kill, mode {}): player 2 on foot, {:.1f} s "
                    "in this body: health {:.1f} -> {:.1f} (his floor: his own death follows; #{} of this step)",
                    mode, since, health, kOwnFloor, js.tk_applied);
        return;
      }
    }
    else
    {
      const bool seated = riding_now && ctrl_now && SeatOf(base, ctrl_now, p2) == 0 &&
                          Load32(base, ctrl_now + kSeatState) == 3 && health > 0.01f;
      if (!seated)
      {
        js.tk_seated_since = {};
        wait = riding_now ? "he is not settled in the seat yet" : "he is not on a jetpack";
      }
      else
      {
        if (!js.tk_seated_since.time_since_epoch().count())
        {
          js.tk_seated_since = now;
        }
        if (now >= js.tk_check)
        {
          js.tk_check = now + std::chrono::milliseconds(250);
          const bool near = base[ctrl_now + kJetGround] != 0;
          float ground = 0.0f;
          const bool have = GroundBelow(ctx, base, riding_now, 30000.0f, ground);
          js.tk_ground = have ? ground : -1.0f;
          js.tk_height_ok = mode == 2   ? (near || (have && ground <= 250.0f))
                            : mode == 3 ? (!near && (!have || ground >= 500.0f))
                                        : true;
        }
        if (std::chrono::duration<double>(now - js.tk_seated_since).count() < 1.0)
        {
          wait = "seated for under 1 s";
        }
        else if (!js.tk_height_ok && std::chrono::duration<double>(now - js.tk_seated_since).count() < 15.0)
        {
          // (after 15 s seated the hit is made at whatever height he is: the log line says which)
          wait = fmt::format("seated, but the height is not the mode's (ground {} below, near-ground byte {})",
                             js.tk_ground < 0.0f ? std::string("not found")
                                                 : fmt::format("{:.1f} m", js.tk_ground / 100.0f),
                             base[ctrl_now + kJetGround]);
        }
      }
      if (wait.empty())
      {
        if (mode == 5 && js.tk_stage == 0)
        {
          js.tk_stage = 1;
          js.tk_until = now + std::chrono::seconds(8);
          if (health > 0.5f)
          {
            StoreFloat(base, part + kPartHealth, 0.5f);
          }
          const float at[3] = {LoadFloat(base, p2 + kPosition), LoadFloat(base, p2 + kPosition + 4),
                               LoadFloat(base, p2 + kPosition + 8)};
          REXLOG_INFO("pdz_counterop: jetpack: TEST (pdz_counterop_test_jet_kill {}): player 2 seated on {:08X}: "
                      "health {:.1f} -> 0.5, floor {:.2f}; the agent ({:.1f} m away) looks at him for 8 s: a real "
                      "hit may end his life, else the tool's hit",
                      js.tk_armed, riding_now, health, LoadFloat(base, p2 + kHealthFloor),
                      AgentDistance(base, at) / 100.0f);
        }
        if (mode == 5 && js.tk_stage == 1 && now < js.tk_until)
        {
          AimAgentAtRider(ctx, base, agent, p2);
          return;
        }
        if (js.tk_stage >= 2 && now < js.tk_until)
        {
          return; // the tool's hit was made: its result is read above
        }
        if (js.tk_stage >= 3)
        {
          REXLOG_WARN("pdz_counterop: jetpack: TEST (pdz_counterop_test_jet_kill {}): two hits of the tool did not "
                      "end player 2's life (health {:.2f}): given up",
                      js.tk_armed, health);
          next_step("given up: two hits of the tool did not end his life");
          return;
        }
        if (LoadFloat(base, p2 + kHealthFloor) != 0.0f && js.tk_stage == 2)
        {
          StoreFloat(base, p2 + kHealthFloor, 0.0f); // counter_op.cpp does this for a rider every tick
        }
        std::string how;
        const bool dead = SeatHit(ctx, base, p2, agent, how);
        js.tk_stage = js.tk_stage < 2 ? 2 : 3;
        js.tk_until = now + std::chrono::seconds(3);
        REXLOG_INFO("pdz_counterop: jetpack: TEST (pdz_counterop_test_jet_kill {}): the tool's hit on player 2 seated "
                    "on {:08X} ({}, near-ground byte {}): {}: {}",
                    js.tk_armed, riding_now,
                    js.tk_ground < 0.0f ? std::string("no ground found below")
                                        : fmt::format("ground {:.1f} m below", js.tk_ground / 100.0f),
                    base[ctrl_now + kJetGround], how, dead ? "KILLED" : "NOT killed yet");
        return;
      }
      if (js.tk_stage == 1 && now < js.tk_until && riding_now)
      {
        AimAgentAtRider(ctx, base, agent, p2);
      }
    }
    if (!wait.empty() && (!js.tk_told.time_since_epoch().count() || now - js.tk_told >= std::chrono::seconds(5)))
    {
      js.tk_told = now;
      REXLOG_INFO("pdz_counterop: jetpack: TEST (pdz_counterop_test_jet_kill {}): waiting: {}", js.tk_armed, wait);
    }
  }

  void Tick(PPCContext &ctx, uint8_t *base, uint32_t p2)
  {
    if (!Ptr(p2))
    {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    // Round 13b (helper s): his death in the seat (before the ride's own bookkeeping below), and the test tool.
    {
      const uint32_t prop13b = VehicleOf(base, p2);
      const uint32_t ctrl13b = JetpackController(base, prop13b);
      const uint32_t riding13b = ctrl13b && SeatOf(base, ctrl13b, p2) >= 0 ? prop13b : 0;
      if (REXCVAR_GET(pdz_counterop_jetpack_seat_death))
      {
        SeatDeathTick(ctx, base, p2, now, riding13b, riding13b ? ctrl13b : 0u);
      }
      if (js.tk_armed || g_tk_seen != REXCVAR_GET(pdz_counterop_test_jet_kill))
      {
        TestKillTick(ctx, base, p2, now, riding13b, riding13b ? ctrl13b : 0u);
      }
    }
    if (js.watch_ctrl)
    {
      AirWatch(ctx, base, p2, now);
    }
    if (AirLevel() || REXCVAR_GET(pdz_counterop_test_jet_kill_riders) != js.last_test_kill)
    {
      AirList(ctx, base, p2, now);
    }
    // Follow the ride by his parent link: a ride he starts himself through Rare's Use counts as much as ours.
    const uint32_t prop_now = VehicleOf(base, p2);
    const uint32_t ctrl_now = JetpackController(base, prop_now);
    const uint32_t riding_now = ctrl_now && SeatOf(base, ctrl_now, p2) >= 0 ? prop_now : 0;
    if (riding_now != js.riding)
    {
      if (riding_now)
      {
        REXLOG_INFO("pdz_counterop: jetpack: player 2 gets on the jetpack {:08X} (seat state {}, vehicle state {} -> "
                    "{}){}",
                    riding_now, Load32(base, ctrl_now + kSeatState), Load32(base, ctrl_now + kCtrlState),
                    Load32(base, ctrl_now + kCtrlState + 4),
                    js.use_prop == riding_now ? ": Rare's Use (825D2E90 -> 8266A8E8)" : "");
      }
      else
      {
        REXLOG_INFO("pdz_counterop: jetpack: player 2 is off the jetpack {:08X} (parent {:08X})", js.riding,
                    Load32(base, p2 + kParent));
      }
      js.riding = riding_now;
      js.next_diag = now; // one ride line at once
    }
    // Fri 20 Nov (item 5, pdz_counterop_jetpack_rider_pose): on the jetpack he was given, during the seat animation
    // (seat state 2) and seated (3), nothing may be in his hands and no weapon switch under way (the draw that ran into
    // the mount at 15:23:03 in log 027), and once seated his hand animations stay as the jetpack's seat step left them
    // (off). Each correction: Rare's switch to nothing at once, then that seat step's 8254ECB0(p2, 0). At most
    // kPoseFixCap a ride, 0.5 s apart.
    if (riding_now && riding_now == js.given_prop && ctrl_now == js.given_ctrl &&
        REXCVAR_GET(pdz_counterop_jetpack_rider_pose))
    {
      if (js.pose_ride != riding_now)
      {
        js.pose_ride = riding_now;
        js.pose_fixes = 0;
        js.pose_next = {};
      }
      const int seat = SeatOf(base, ctrl_now, p2);
      const uint32_t state = seat >= 0 ? Load32(base, ctrl_now + seat * kSeatBytes + kSeatState) : 0u;
      const bool busy = HandsBusy(base, p2);
      const bool anim_on = state == 3 && (base[p2 + kHandAnimA] || base[p2 + kHandAnimB]);
      if ((state == 2 || state == 3) && (busy || anim_on) && js.pose_fixes < kPoseFixCap && now >= js.pose_next &&
          HumanHealth(base, p2) > 0.01f)
      {
        ++js.pose_fixes;
        js.pose_next = now + std::chrono::milliseconds(500);
        const std::string before = HandsLine(base, p2);
        const bool made = busy && PutAwayAtOnce(ctx, base, p2);
        HandAnimationsOff(ctx, base, p2);
        REXLOG_INFO("pdz_counterop: jetpack: rider pose: player 2 on {:08X} (seat state {}, {:.2f} s after the mount): "
                    "{}: {}Rare's seat step for the hands (8254ECB0(p2, 0)) -> {} (pdz_counterop_jetpack_rider_pose; "
                    "#{} of {} this ride)",
                    riding_now, state, std::chrono::duration<double>(now - js.mount_at).count(), before,
                    made ? "Rare's weapon switch to nothing at once (824F83B8, r9 1), " : "", HandsLine(base, p2),
                    js.pose_fixes, kPoseFixCap);
      }
    }
    // Round 18 (the owner, Sun 22 Nov): round 17's landed auto-exit is gone; he gets off only when he asks (never ejected).
    // The remount check: kUseCheck after a Use press on a jetpack.
    if (js.use_prop && std::chrono::duration<double>(now - js.use_at).count() >= kUseCheck)
    {
      const uint32_t prop = js.use_prop;
      js.use_prop = 0;
      const uint32_t ctrl = JetpackController(base, prop);
      if (ctrl && js.riding != prop)
      {
        ++js.refusals;
        REXLOG_WARN("pdz_counterop: jetpack: player 2 pressed Use on the jetpack {:08X} and is not seated after "
                    "{:.1f} s (Use #{}, not seated #{}): {}",
                    prop, kUseCheck, js.uses, js.refusals, EnterFields(base, prop, ctrl, p2));
        if (REXCVAR_GET(pdz_counterop_jetpack_remount) && !VehicleOf(base, p2) && Free(base, ctrl) &&
            LoadFloat(base, p2 + kHealth) > 0.0f)
        {
          // The mount Rare's Use makes on this jetpack (825D2F50..825D2F60: seat 0 first, flag 0).
          const uint32_t ok = Call(ctx, base, sub_8266A8E8, ctrl, p2, 0, 0) & 0xFF;
          ++js.remounts;
          REXLOG_INFO("pdz_counterop: jetpack: fallback mount #{} (pdz_counterop_jetpack_remount) {}: seat state {}, "
                      "parent {:08X}",
                      js.remounts, ok ? "seated him" : "was REFUSED", Load32(base, ctrl + kSeatState),
                      Load32(base, p2 + kParent));
        }
      }
    }
    if (!js.riding || now < js.next_diag)
    {
      return;
    }
    js.next_diag = now + std::chrono::milliseconds(500);
    const int seat = SeatOf(base, ctrl_now, p2);
    REXLOG_INFO("pdz_counterop: jetpack ride: seat {} state {}, vehicle state {}, rider state {}, on the ground {}, "
                "at ({:.0f}, {:.0f}, {:.0f}); player 2 parent {:08X}, controller context {}; {}",
                seat, seat >= 0 ? Load32(base, ctrl_now + seat * kSeatBytes + kSeatState) : 0u,
                Load32(base, ctrl_now + kCtrlState), Load32(base, ctrl_now + kJetRider), base[ctrl_now + kJetGround],
                LoadFloat(base, js.riding + kPropPosition), LoadFloat(base, js.riding + kPropPosition + 4),
                LoadFloat(base, js.riding + kPropPosition + 8), Load32(base, p2 + kParent),
                // participant (+6560) +0x698: the controller object's context (4 = jetpack, mnk_layout.cpp)
                Rd(base, Load32(base, p2 + kParticipantOf) + 0x698, 4) ? Load32(base, Load32(base, p2 + kParticipantOf) + 0x698) : 0u,
                HandsLine(base, p2)); // Fri 20 Nov (item 5): the rider pose diagnosis
  }
} // namespace counter_op_jetpack

// Sun 22 Nov (item 6): anim_log.cpp's 826946A8 hook, every animation play (character r3, entry r4, the caller).
void PdzRideAnimPlay(const uint8_t *base, uint32_t character, uint32_t entry, uint32_t caller)
{
  using namespace counter_op_jetpack;
  for (int k = 0; k < 2; ++k)
  {
    if (character == 0 || g_ride_watch[k].load(std::memory_order_relaxed) != character)
    {
      continue;
    }
    std::string name = "?";
    const uint32_t text = Rd(base, entry, 4) ? Load32(base, entry) : 0;
    if (Rd(base, text, 48))
    {
      const char *t = reinterpret_cast<const char *>(base + text);
      name.assign(t, strnlen(t, 48));
    }
    RideAnims &r = g_ride_anim[k];
    bool found = false;
    for (size_t i = 0; i < r.keys.size(); ++i)
    {
      if (r.keys[i].second == caller && r.keys[i].first == name)
      {
        ++r.counts[i];
        found = true;
        break;
      }
    }
    if (!found && r.keys.size() < 24)
    {
      r.keys.emplace_back(name, caller);
      r.counts.push_back(1);
    }
  }
}


// ---- Sun 22 Nov (round 18, item 6): the body's animation slots, for the jetpack spasm (pdz_counterop_jetpack_ride_log)
// Layout (read in 821EB640 / 821EB730 / 821E9000 / 821EB1E8 / 821EB0A0; cutscene_smooth.cpp has the sampler part):
// anim object A = [character +72], slot set S = [A +4]; S +12 the node array (8 bytes a node: +2 s16 next index, +4
// the slot), S +20 the head node; S +44 the last entry started on slot 0. A slot: +0 u8 id (0 = full body: the jetpack's
// ride pose and the locomotion; 4-6 actions; 60 / 61 the hands), +16 the sampler (+164 t 0..1, +176 rate), +244 the
// entry ([entry] = its name), +260 a procedural override, +272 the replace fade, +280 blend mode, +284 fade state
// (1 in, 2 full, 3 out), +292 layer weight, +296 fade weight (effective = +292 x +296). Every slot start goes through
// 821EB0A0 (obj r3, entry r4, mode r6, speed f1, blend f3, slot id = the byte at r1 + 87): same entry on the slot ->
// nothing (0); else 821EB1E8 starts a new slot and a crossfade (1).
REX_EXTERN(__imp__sub_821EB0A0);
REX_EXTERN(__imp__sub_821EB1E8);
namespace
{
  struct SlotWatch
  {
    uint32_t who = 0, anim = 0;
    std::chrono::steady_clock::time_point detail_until{}, next_detail{};
    std::vector<std::pair<std::string, uint32_t>> keys; // "slot N name ret R" @ caller
    std::vector<uint32_t> counts;
    uint32_t restarts0 = 0, plays = 0;
  };
  SlotWatch g_sw[2];
  std::atomic<uint32_t> g_sw_anim[2] = {};

  std::string EntryName(const uint8_t *base, uint32_t entry)
  {
    const uint32_t text = Rd(base, entry, 4) ? Load32(base, entry) : 0;
    if (!Rd(base, text, 48))
    {
      return "?";
    }
    const char *t = reinterpret_cast<const char *>(base + text);
    std::string n(t, strnlen(t, 48));
    const size_t cut = n.rfind("anim_");
    return cut != std::string::npos && cut + 5 < n.size() ? n.substr(cut + 5) : n;
  }

  float SlotF(const uint8_t *base, uint32_t slot, uint32_t off) { return LoadFloat(base, slot + off); }

  // The slot list of a character's body, one item a slot.
  std::string SlotList(const uint8_t *base, uint32_t c)
  {
    const uint32_t a = Rd(base, c + 72, 4) ? Load32(base, c + 72) : 0;
    const uint32_t set = Rd(base, a + 4, 4) ? Load32(base, a + 4) : 0;
    if (!Rd(base, set, 48))
    {
      return "no slot set";
    }
    const uint32_t arr = Load32(base, set + 12), head = Load32(base, set + 20);
    std::string out = fmt::format("top {} last0 '{}':", base[set + 1], EntryName(base, Load32(base, set + 44)));
    int32_t idx = head >= arr ? int32_t((head - arr) >> 3) : -1;
    for (int n = 0; idx >= 0 && idx < 256 && n < 24; ++n)
    {
      const uint32_t node = arr + uint32_t(idx) * 8;
      if (!Rd(base, node, 8))
      {
        break;
      }
      const uint32_t slot = Load32(base, node + 4);
      if (Rd(base, slot, 0x140))
      {
        const float fade = SlotF(base, slot, 296), layer = SlotF(base, slot, 292);
        out += fmt::format(" [{} '{}' w {:.2f} (fade {:.2f} x {:.2f}, state {}) t {:.3f} rate {:.2f}{}{}{}]", base[slot],
                           EntryName(base, Load32(base, slot + 244)), fade * layer, fade, layer, Load32(base, slot + 284),
                           SlotF(base, slot, 164), SlotF(base, slot, 176),
                           Load32(base, slot + 260) ? fmt::format(" override {:08X}", Load32(base, slot + 260)) : "",
                           SlotF(base, slot, 272) != 0.0f ? fmt::format(" replace {:.2f}", SlotF(base, slot, 272)) : "",
                           Load32(base, slot + 280) ? fmt::format(" blend {}", Load32(base, slot + 280)) : "");
      }
      idx = int16_t((uint16_t(base[node + 2]) << 8) | base[node + 3]);
    }
    return out;
  }

  int Watched(uint32_t obj)
  {
    for (int k = 0; k < 2; ++k)
    {
      const uint32_t a = g_sw_anim[k].load(std::memory_order_relaxed);
      if (a && (obj == a || obj == counter_op_jetpack::g_ride_watch[k].load(std::memory_order_relaxed)))
      {
        return k;
      }
    }
    return -1;
  }

  void SlotPlayed(const uint8_t *base, int k, bool inner, uint32_t slot_id, uint32_t entry, uint32_t mode, double speed,
                  double blend, uint32_t caller, uint32_t ret)
  {
    SlotWatch &w = g_sw[k];
    ++w.plays;
    if (slot_id == 0 && ret)
    {
      ++w.restarts0;
    }
    const std::string name = EntryName(base, entry);
    if (std::chrono::steady_clock::now() < w.detail_until)
    {
      REXLOG_INFO("pdz_counterop: ride slots: {} {:08X}: {} slot {} '{}' mode {} speed {:.2f} blend {:.2f} from {:08X}: {}",
                  k == 0 ? "PLAYER 2" : "AI rider", w.who, inner ? "821EB1E8 (new slot)" : "821EB0A0", slot_id, name, mode,
                  speed, blend, caller, ret ? "STARTED (a crossfade)" : "already playing: nothing");
    }
    const std::string key = fmt::format("slot {} '{}' -> {}", slot_id, name, ret ? 1 : 0);
    for (size_t i = 0; i < w.keys.size(); ++i)
    {
      if (w.keys[i].second == caller && w.keys[i].first == key)
      {
        ++w.counts[i];
        return;
      }
    }
    if (w.keys.size() < 24)
    {
      w.keys.emplace_back(key, caller);
      w.counts.push_back(1);
    }
  }
} // namespace

REX_HOOK_RAW(sub_821EB0A0)
{
  const int k = Watched(ctx.r3.u32);
  if (k < 0)
  {
    __imp__sub_821EB0A0(ctx, base);
    return;
  }
  const uint32_t slot_id = Rd(base, ctx.r1.u32 + 87, 1) ? base[ctx.r1.u32 + 87] : 0xFF;
  const uint32_t entry = ctx.r4.u32, mode = ctx.r6.u32, caller = uint32_t(ctx.lr);
  const double speed = ctx.f1.f64, blend = ctx.f3.f64;
  __imp__sub_821EB0A0(ctx, base);
  SlotPlayed(base, k, false, slot_id, entry, mode, speed, blend, caller, ctx.r3.u32 & 0xFF);
}

REX_HOOK_RAW(sub_821EB1E8)
{
  const int k = uint32_t(ctx.lr) == 0x821EB1D8u ? -1 : Watched(ctx.r3.u32); // (from 821EB0A0: counted there)
  if (k < 0)
  {
    __imp__sub_821EB1E8(ctx, base);
    return;
  }
  const uint32_t slot_id = Rd(base, ctx.r1.u32 + 87, 1) ? base[ctx.r1.u32 + 87] : 0xFF;
  const uint32_t entry = ctx.r4.u32, mode = ctx.r6.u32, caller = uint32_t(ctx.lr);
  const double speed = ctx.f1.f64, blend = ctx.f3.f64;
  __imp__sub_821EB1E8(ctx, base);
  SlotPlayed(base, k, true, slot_id, entry, mode, speed, blend, caller, 1);
}

// Every frame (counter_op_jetpack::TestTools): the watched riders' slot lists, each frame in a detail window (3 s
// from a new rider, then 1 s every 10 s), once a second otherwise with the plays of that second.
void PdzRideSlotsFrame(const uint8_t *base)
{
  using namespace counter_op_jetpack;
  const auto now = std::chrono::steady_clock::now();
  static std::chrono::steady_clock::time_point next_second{};
  const bool second = now >= next_second;
  if (second)
  {
    next_second = now + std::chrono::seconds(1);
  }
  for (int k = 0; k < 2; ++k)
  {
    const uint32_t who = REXCVAR_GET(pdz_counterop_jetpack_ride_log) ? g_ride_watch[k].load() : 0;
    SlotWatch &w = g_sw[k];
    if (who != w.who)
    {
      w = SlotWatch{};
      w.who = who;
      w.detail_until = now + std::chrono::seconds(3);
      w.next_detail = now + std::chrono::seconds(10);
      w.anim = who && Rd(base, who + 72, 4) ? Load32(base, who + 72) : 0;
      g_sw_anim[k].store(w.anim);
    }
    if (!who)
    {
      continue;
    }
    if (now >= w.next_detail)
    {
      w.detail_until = now + std::chrono::seconds(1);
      w.next_detail = now + std::chrono::seconds(10);
    }
    const bool detail = now < w.detail_until;
    if (detail || second)
    {
      std::string plays;
      if (second)
      {
        for (size_t i = 0; i < w.keys.size(); ++i)
        {
          plays += fmt::format("{}{} x{} @{:08X}", plays.empty() ? "" : ", ", w.keys[i].first, w.counts[i], w.keys[i].second);
        }
        w.keys.clear();
        w.counts.clear();
      }
      REXLOG_INFO("pdz_counterop: ride slots: {} {:08X}{}: {}{}", k == 0 ? "PLAYER 2" : "AI rider", who,
                  detail ? " (frame)" : "", SlotList(base, who),
                  second ? fmt::format(" | this second: {} slot starts ({} slot-0 restarts): {}", w.plays, w.restarts0,
                                       plays.empty() ? "none" : plays)
                         : "");
      if (second)
      {
        w.plays = w.restarts0 = 0;
      }
    }
  }
}


// ---- Sun 22 Nov (round 18, item 6): the jetpack spasm's cause and fix (pdz_counterop_jetpack_no_flinch) ----
// The ride log's slot reader (above), Jungle, player 2 on the jetpack: besides the jetpack's own slots (0 'jetpac_*',
// 5 'flightidle', 6 strafe) his body carries a STACK of slot-35 'testanims_testflinch' slots, an additive blend (mode 1),
// a new one started EVERY frame (61 a second, 821EB1E8 from 821E8D28) and each crossfading over the last: the flinch's
// first tenth of a second replayed sixty times a second over the seated pose. The AI rider beside him has no slot 35.
// The player starts it: the human tick's 825E9B78 plays the flinch on slot 35 at every call unless the character's class
// (+12) is 58, its virtual +232 answers yes, or the signed halfword at +976 is negative; player 2's guard body leaves it
// at 0 or above (the AI's tick never runs 825E9B78). While he sits on a jetpack the halfword reads -1 for that one call
// (put back at once), so the flinch is not started; the rest of 825E9B78 runs as Rare's.
REXCVAR_DEFINE_BOOL(pdz_counterop_jetpack_no_flinch, true, "Gameplay",
                    "Counter-Operative (Sun 22 Nov, default on): while player 2 sits on a jetpack, the player's flinch "
                    "animation is not restarted on his body every frame (it stacked up over the seated pose: the "
                    "jetpack twitch). Off = as before");
REX_EXTERN(__imp__sub_825E9B78);
uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
namespace
{
  uint64_t g_flinch_skips = 0;
}
REX_HOOK_RAW(sub_825E9B78)
{
  const uint32_t c = ctx.r3.u32;
  if (!REXCVAR_GET(pdz_counterop_jetpack_no_flinch) || c == 0 || c != PdzCounterOpPlayer2() ||
      !Rd(base, c + 976, 2) || int16_t((uint16_t(base[c + 976]) << 8) | base[c + 977]) < 0 ||
      !counter_op_jetpack::Riding(base, c))
  {
    __imp__sub_825E9B78(ctx, base);
    return;
  }
  const uint8_t was[2] = {base[c + 976], base[c + 977]};
  base[c + 976] = 0xFF;
  base[c + 977] = 0xFF;
  __imp__sub_825E9B78(ctx, base);
  base[c + 976] = was[0];
  base[c + 977] = was[1];
  if (++g_flinch_skips == 1 || g_flinch_skips % 3600 == 0)
  {
    REXLOG_INFO("pdz_counterop: jetpack: player 2's flinch (slot 35 'testflinch', 825E9B78) not restarted while he rides "
                "(his +976 = {}; class +12 = {}; #{}; pdz_counterop_jetpack_no_flinch)",
                int16_t((uint16_t(was[0]) << 8) | was[1]), Load32(base, c + 12), g_flinch_skips);
  }
}


// ---- Sun 22 Nov night (Part 3): does a dead spiderbot's landing carry a sound? (test tools, log only) ----
REXCVAR_DEFINE_INT32(pdz_test_kill_spiders, 0, "Testing",
                     "TESTING TOOL (Sun 22 Nov night): each change kills every live spiderbot in the level with the game's "
                     "own kill (the agent as the killer) and logs their animation slots and height five times a second "
                     "for 8 s")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_spider_event_log, false, "Debug",
                    "Log only (Sun 22 Nov night): every animation event a spiderbot gets (the dispatcher 821EDD40: "
                    "footsteps, body falls and the other sound events of its animations)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(__imp__sub_821EDD40);
REX_EXTERN(sub_825C8388); // Rare's spiderbot death call (bot, &position, 0, 0)
namespace
{
  constexpr uint32_t kSpiderbotVtable = 0x820C9E98; // DP_Spiderbot (class 58)
  struct SpiderWatch
  {
    uint32_t bot = 0;
    std::chrono::steady_clock::time_point until{}, next{};
  };
  std::vector<SpiderWatch> g_spider_watch;
  int32_t g_kill_spiders_last = 0;
  uint32_t g_spider_event_lines = 0, g_spider_step_lines = 0;
}

REX_HOOK_RAW(sub_821EDD40)
{
  if (REXCVAR_GET(pdz_snd_spider_event_log) && g_spider_event_lines < 400 && Rd(base, ctx.r3.u32, 0x40) &&
      Load32(base, ctx.r3.u32) == kSpiderbotVtable && Rd(base, ctx.r4.u32, 8) &&
      (((uint32_t(base[ctx.r4.u32 + 4]) << 8) | base[ctx.r4.u32 + 5]) != 10 || g_spider_step_lines++ < 6))
  {
    ++g_spider_event_lines;
    REXLOG_INFO("pdz_spider_event: spiderbot {:08X}: animation event {:08X} type {} (bytes +6 {} +7 {}), from {:08X}; "
                "slots: {}",
                ctx.r3.u32, ctx.r4.u32, (uint32_t(base[ctx.r4.u32 + 4]) << 8) | base[ctx.r4.u32 + 5],
                base[ctx.r4.u32 + 6], base[ctx.r4.u32 + 7], uint32_t(ctx.lr), SlotList(base, ctx.r3.u32));
  }
  __imp__sub_821EDD40(ctx, base);
}

void PdzSpiderTestTick(PPCContext &ctx, uint8_t *base, uint32_t p1)
{
  using namespace counter_op_jetpack;
  const int32_t want = REXCVAR_GET(pdz_test_kill_spiders);
  const auto now = std::chrono::steady_clock::now();
  if (want != g_kill_spiders_last && want != 0)
  {
    const uint32_t world = Load32(base, kGame + 24);
    const uint32_t end = Rd(base, world + kWorldFirst, 8) ? Load32(base, world + kWorldEnd) : 0;
    uint32_t node = Rd(base, world + kWorldFirst, 8) ? Load32(base, world + kWorldFirst) : 0;
    std::vector<uint32_t> bots;
    for (int i = 0; Rd(base, node, 16) && node != end && i < 16384 && bots.size() < 32; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      if (Rd(base, o, 0x200) && Load32(base, o) == kSpiderbotVtable && LoadFloat(base, o + kHealth) > 0.0f &&
          (Load32(base, o + kHidden) & 0x10) == 0)
      {
        bots.push_back(o);
      }
    }
    for (const uint32_t o : bots)
    {
      const float y0 = LoadFloat(base, o + kPosition + 4);
      // The spiderbot's own death, as counter_op_spider.cpp's Detonate: health 0 through its setter (vtable +0x158),
      // then Rare's death call 825C8388. (The guards' kill 8252DE88 is not for this class: it faulted in the first run.)
      const uint32_t vt = Load32(base, o);
      const uint32_t setter = vt >= 0x82000000u && vt < 0x83000000u && Rd(base, vt + 0x158, 4) ? Load32(base, vt + 0x158) : 0;
      if (PPCFunc *fn = setter ? pdz::GuestFunction(setter, "spiderbot health (test kill)") : nullptr)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = o;
        frame.ctx.r5.u64 = 0;
        frame.ctx.f1.f64 = 0.0;
        fn(frame.ctx, base);
        Call(ctx, base, sub_825C8388, o, o + kPosition, 0, 0);
      }
      REXLOG_INFO("pdz_spider_event: TEST kill (pdz_test_kill_spiders {}): spiderbot {:08X} at ({:.0f}, {:.0f}, {:.0f}): the "
                  "bot's own death (health setter, then 825C8388): health now {:.1f}; slots: {}",
                  want, o, LoadFloat(base, o + kPosition), y0, LoadFloat(base, o + kPosition + 8), LoadFloat(base, o + kHealth),
                  SlotList(base, o));
      g_spider_watch.push_back({o, now + std::chrono::seconds(8), now});
    }
    if (bots.empty())
    {
      REXLOG_INFO("pdz_spider_event: TEST kill (pdz_test_kill_spiders {}): no live spiderbot in the level", want);
    }
  }
  g_kill_spiders_last = want;
  for (auto it = g_spider_watch.begin(); it != g_spider_watch.end();)
  {
    if (now >= it->until || !Rd(base, it->bot, 0x200) || Load32(base, it->bot) != kSpiderbotVtable)
    {
      it = g_spider_watch.erase(it);
      continue;
    }
    if (now >= it->next)
    {
      it->next = now + std::chrono::milliseconds(200);
      REXLOG_INFO("pdz_spider_event: dead spiderbot {:08X}: y {:.1f}, health {:.1f}, hidden {:X}; slots: {}", it->bot,
                  LoadFloat(base, it->bot + kPosition + 4), LoadFloat(base, it->bot + kHealth),
                  Load32(base, it->bot + kHidden), SlotList(base, it->bot));
    }
    ++it;
  }
}

namespace
{
  // Player 2's held-over buttons. XInputGetState runs on whichever thread polls the pad, so all of it is atomic.
  constexpr uint32_t kLeftTriggerBit = 0x10000, kRightTriggerBit = 0x20000; // the triggers as two more "buttons"
  constexpr uint8_t kTriggerDown = 30;                                      // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
  std::atomic<int32_t> g_pad_user{-1};     // the XInputGetState user that is player 2's pad, -1 = nothing masked
  std::atomic<uint32_t> g_pad_mask{0};     // masked: wButtons bits, plus the two trigger bits
  std::atomic<bool> g_pad_arm_next{false}; // also mask what the next poll holds (the last poll may be a frame old)
  std::atomic<uint32_t> g_pad_last[4] = {};
  std::atomic<int32_t> g_pad_lx[4] = {}, g_pad_ly[4] = {}; // round 7: each pad's left stick (-32768..32767)
  std::atomic<int32_t> g_pad_rx[4] = {}, g_pad_ry[4] = {}; // round 9: each pad's right stick
  std::atomic<uint32_t> g_pad_polls[4] = {};               // round 10: XInputGetState calls seen for each user
  std::atomic<uint32_t> g_pad_result[4] = {};              // round 10: the last call's result (0 = connected)
  std::atomic<bool> g_pad_block_dpad{false}; // round 4: D-pad Up / Down dropped from player 2's pad
  std::atomic<uint32_t> g_pad_dpad_dropped{0};
  std::atomic<int64_t> g_pad_up_ms{-1}; // round 4: when player 2's pad last had D-pad Up down (steady clock, ms)
  std::atomic<int32_t> g_pad_block_y{-1}; // round 11: the user whose Y is dropped (player 2 while he drives a bot)
  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // XINPUT_STATE (big-endian): +0 packet, +4 wButtons, +6 LT, +7 RT, +8 sticks.
  uint32_t RawButtons(const uint8_t *base, uint32_t state)
  {
    const uint32_t buttons = (uint32_t(base[state + 4]) << 8) | base[state + 5];
    return buttons | (base[state + 6] > kTriggerDown ? kLeftTriggerBit : 0u) |
           (base[state + 7] > kTriggerDown ? kRightTriggerBit : 0u);
  }
} // namespace

namespace counter_op_pad
{
  void Arm(const uint8_t *base, uint32_t p2, const char *why)
  {
    if (!REXCVAR_GET(pdz_counterop_held_mask) || !Ptr(p2) || !Ptr(Load32(base, p2 + kParticipantOf)))
    {
      return;
    }
    const int32_t user = int32_t(Load32(base, Load32(base, p2 + kParticipantOf) + kParticipantPad));
    if (user < 0 || user > 3)
    {
      REXLOG_INFO("pdz_counterop: pad: player 2 has no pad (participant +0x690 = {}): nothing masked ({})", user, why);
      return;
    }
    const uint32_t held = g_pad_last[user].load();
    g_pad_mask = held;
    g_pad_arm_next = true;
    g_pad_user = user;
    REXLOG_INFO("pdz_counterop: pad: player 2's pad {} ({}): held now {:05X}, ignored until released", user, why,
                held);
  }

  int64_t MsSinceUp()
  {
    const int64_t at = g_pad_up_ms.load();
    return at < 0 ? -1 : NowMs() - at;
  }

  void BlockDpad(bool on) { g_pad_block_dpad.store(on, std::memory_order_relaxed); }
  void BlockY(int32_t user) { g_pad_block_y.store(user, std::memory_order_relaxed); }

  // Round 7: a pad's last polled state: buttons (wButtons | 0x10000 left trigger | 0x20000 right trigger) and the
  // left stick as -1..1.
  uint32_t Polled(uint32_t user, float &lx, float &ly)
  {
    if (user > 3)
    {
      lx = ly = 0.0f;
      return 0;
    }
    lx = float(g_pad_lx[user].load()) / 32767.0f;
    ly = float(g_pad_ly[user].load()) / 32767.0f;
    return g_pad_last[user].load();
  }

  // Round 9: a pad's last polled right stick as -1..1.
  void PolledRight(uint32_t user, float &rx, float &ry)
  {
    rx = user > 3 ? 0.0f : float(g_pad_rx[user].load()) / 32767.0f;
    ry = user > 3 ? 0.0f : float(g_pad_ry[user].load()) / 32767.0f;
  }

  // Round 10: how often a user's pad has been polled, and the last poll's result.
  uint32_t PollCount(uint32_t user) { return user > 3 ? 0u : g_pad_polls[user].load(); }
  uint32_t PollResult(uint32_t user) { return user > 3 ? 0xFFFFFFFFu : g_pad_result[user].load(); }

  void Reset()
  {
    g_pad_block_dpad = false;
    g_pad_block_y = -1;
    g_pad_dpad_dropped = 0;
    g_pad_up_ms = -1;
    g_pad_user = -1;
    g_pad_mask = 0;
    g_pad_arm_next = false;
  }

  void OnPadState(uint8_t *base, uint32_t user, uint32_t state, uint32_t result)
  {
    if (user > 3 || !Ptr(state))
    {
      return;
    }
    const uint32_t raw = result == 0 ? RawButtons(base, state) : 0u; // not connected = nothing held
    g_pad_polls[user].fetch_add(1, std::memory_order_relaxed);
    g_pad_result[user] = result;
    g_pad_last[user] = raw;
    g_pad_lx[user] = result == 0 ? int32_t(int16_t((uint16_t(base[state + 8]) << 8) | base[state + 9])) : 0;
    g_pad_ly[user] = result == 0 ? int32_t(int16_t((uint16_t(base[state + 10]) << 8) | base[state + 11])) : 0;
    g_pad_rx[user] = result == 0 ? int32_t(int16_t((uint16_t(base[state + 12]) << 8) | base[state + 13])) : 0;
    g_pad_ry[user] = result == 0 ? int32_t(int16_t((uint16_t(base[state + 14]) << 8) | base[state + 15])) : 0;
    if (result == 0 && (raw & 0x8000u) && int32_t(user) == g_pad_block_y.load(std::memory_order_relaxed))
    {
      base[state + 4] = uint8_t(base[state + 4] & ~0x80u); // round 11: XINPUT_GAMEPAD_Y 0x8000 (wButtons big-endian at +4)
    }
    if (int32_t(user) != g_pad_user.load())
    {
      return;
    }
    if (raw & 0x0001u)
    {
      g_pad_up_ms = NowMs();
    }
    if (result == 0 && (raw & 0x0003u) && g_pad_block_dpad.load(std::memory_order_relaxed))
    {
      base[state + 5] = uint8_t(base[state + 5] & ~0x03u); // XINPUT_GAMEPAD_DPAD_UP 0x0001, _DOWN 0x0002
      if (g_pad_dpad_dropped.fetch_add(1) == 0)
      {
        REXLOG_INFO("pdz_counterop: pad: player 2's D-pad Up / Down (Rare's put-away and drop-the-weapon buttons) is "
                    "not passed on while he plays a guard; logged once a level");
      }
    }
    uint32_t mask = g_pad_mask.load();
    if (g_pad_arm_next.exchange(false))
    {
      mask |= raw;
    }
    const uint32_t kept = mask & raw; // a masked button that is up now leaves the mask for good
    g_pad_mask = kept;
    if (mask && !kept)
    {
      REXLOG_INFO("pdz_counterop: pad: player 2's held-over buttons are all released ({:05X})", mask);
    }
    if (!kept || result != 0 || !REXCVAR_GET(pdz_counterop_held_mask))
    {
      return;
    }
    const uint32_t buttons = ((uint32_t(base[state + 4]) << 8) | base[state + 5]) & ~kept;
    base[state + 4] = uint8_t(buttons >> 8);
    base[state + 5] = uint8_t(buttons);
    if (kept & kLeftTriggerBit)
    {
      base[state + 6] = 0;
    }
    if (kept & kRightTriggerBit)
    {
      base[state + 7] = 0;
    }
  }
} // namespace counter_op_pad
