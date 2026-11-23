// Counter-Operative, rounds 7 and 9: Rare's spiderbots, and player 2 driving one. Built only with PDZ_COUNTEROP.
//
// What a spiderbot is (TU3, read in the code): its own class (id 0x3A, vtable 0x820C9E98, template "Spiderbot01"),
// not a guard and not a vehicle, with the guards' AI brain (bot +0x478) and task stack. Its AI tick is 825C7A40(bot,
// f1 dt); every 3 s 825C9180 picks its target: the nearest hostile AI object (handle +0xDA8, object +0xDAC), else the
// nearest hostile human (+0xDA0 / +0xDA4). Its attack goal (id 0x58, update 821E6FD0) walks it to within 2.5 m of that
// object, turns it, then POUNCES: Rare's jump task 821B1A00, 20 damage a leap to any character its sweep finds. It
// does not shoot. Two kinds of the one class: about one in eight on Datacore is the yellow-eyed DETONATOR (byte
// +0xDD4, chosen in the constructor): its death 825C7CC8 makes an explosive (type 0x17) owned by the bot and sets it
// off. No bot explodes by nearness: only by being destroyed.
//
// How it moves: the body update 825C7028 runs its controller ([bot+0xD9C], command function 8219AE50, spiderbots
// only), which writes a LOCAL command (side, up, forward, yaw rate) into its mover ([bot+0xE4], 825DEB40). The mover
// follows the surface under the bot's own feet (floor, wall or ceiling) and animates the legs from the measured
// motion. Rare's bots change surface only by jumps at marked path links.
//
// This file: player 2's drive (pdz_counterop_spiderbot: his sticks write that command for one of Rare's own bots,
// whose AI tick is skipped meanwhile; his trigger starts Rare's pounce or Rare's death call; a step onto a wall he
// pushes against is ours), the diagnosis (pdz_counterop_spider_log) and the test tools.
#include <algorithm>
#include <array>
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
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

#include "render/game_pace.h" // SpLevelName

bool PdzCounterOpOn();          // counter_op.cpp
bool PdzCounterOpRunning();     // counter_op.cpp
uint32_t PdzCounterOpAgent();   // the agent's character while a counter-op game runs, else 0
uint32_t PdzCounterOpPlayer2(); // player 2's character while a counter-op game runs, else 0

REXCVAR_DEFINE_BOOL(pdz_counterop_spider_log, false, "Testing",
                    "Counter-Operative (testing, logging only): every 3 s the spiderbots whose AI ticks (how many, "
                    "the nearest to the agent, its two targets), and each spiderbot death with its distance to the "
                    "agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_datacore_checkpoint, false, "Testing",
                    "TESTING TOOL (counter-op, Datacore): the level starts from Rare's own mid-level checkpoint (the "
                    "'restart from checkpoint' bit is set before the level script's Setup: the script itself then "
                    "puts both players in the bio-hab atrium, plays its cutscene and runs the spiderbot section)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_level_message, "", "Testing",
                      "TESTING TOOL (counter-op): 'type,arg,flag' or 'type,arg,flag,x,y,z' (decimal): each time the "
                      "value changes that level message is sent to the level script through Rare's 8214E520 with "
                      "the agent as the source, or the world object nearest x,y,z (a hack panel, a terminal) "
                      "(Datacore: 7,2,1 = the agent enters area 2)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(pdz_counterop_test_objective_done, -1, "Testing",
                     "TESTING TOOL (counter-op, round 8, Nightclub's script layout): each time the value changes "
                     "to N >= 0, objective record N of the level script ([script +0x3F0] + 0x34 x N) is marked "
                     "done (2), as when the agent has done it: the script's own end test then runs (no script can "
                     "play the level through)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_script_state, false, "Testing",
                    "TESTING TOOL (counter-op, round 8): log the level script's state words (the blocks its "
                    "pointers +0x3E0 .. +0x3F0 lead to, 16 words each) whenever one changes, at most twice a "
                    "second: which gate bits a trigger or a test message set")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_datacore_lift_log, false, "Testing",
                    "TESTING TOOL (counter-op, round 9, Datacore): every 2 s the end-area state: the scientists still "
                    "needed at the lift (script +0x288C), the deaths still allowed (+0x2890), the six objective "
                    "states, and the two props the script watches (+0x2768, +0x276C: position, state +0x464, +0x360)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_datacore_lift_s, 10.0, "Gameplay",
                      "Counter-Operative on Datacore (round 10): when the escort objective is done and Rare's own "
                      "call of the shaft lift (the last scientist's 'destroyed' message, 8/56) has not come this "
                      "many seconds later, the lift is called the way that message calls it. Negative = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot, true, "Gameplay",
                    "Counter-Operative on Datacore (round 9; on by default since round 15): while player 2 waits for a "
                    "guard in the spiderbot section he drives one of Rare's own spiderbots, one at a time: left stick "
                    "moves it on the surface it stands on (Rare's mover: floors, walls, ceilings), right stick turns "
                    "it, the fire trigger is Rare's attack of its kind (the pounce, or the detonation of a yellow-eyed "
                    "one). Off = he waits for a guard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_unstick, true, "Gameplay",
                    "Counter-Operative (round 15, default on): when Rare refuses player 2's pounce because a jump or "
                    "fall task that nothing holds any more was left on his spiderbot's task list (a leap cut short by a "
                    "fall), that task is ended the way the game ends one and the pounce is tried once more. Every "
                    "refused pounce is logged with its reason either way")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_close_pounce, true, "Gameplay",
                    "Counter-Operative (round 16, default on): a pounce player 2 starts at the agent aims at her as "
                    "she is now (her position, raised by pdz_counterop_spiderbot_close_pounce_aim_cm) and the leap's "
                    "destination stays on her for the whole leap; when she is within "
                    "pdz_counterop_spiderbot_close_pounce_reach_m at its launch (a close start), or the leap ends that "
                    "near her without Rare's contact, the contact is made through Rare's own reach test and hit call "
                    "(821B1838 / 821B18F0). Off = the leap aims at where her feet were at the press (round 15)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_close_pounce_aim_cm, 40.0, "Gameplay",
                      "Counter-Operative (round 16): how far above her feet player 2's pounce aims (cm, 0..120; 0 = "
                      "her feet, Rare's own aim point)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_close_pounce_reach_m, 1.0, "Gameplay",
                      "Counter-Operative (round 16): the bot-to-agent distance (m, 0.3..2.5) within which a pounce at "
                      "her that Rare's leap could not finish counts as contact")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_climb, true, "Gameplay",
                    "Counter-Operative: a driven spiderbot that is pushed against a wall steps up onto it (this "
                    "project's own rule: Rare's bots change surface at a corner by their path links' jumps, which "
                    "a stick cannot use). Off = it only follows surfaces that bend under it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_released_only, true, "Gameplay",
                    "Counter-Operative (round 11): player 2 is only given a spiderbot Rare has released (not a "
                    "set-piece bot still on its wall or on the glass tank), standing on a floor, with no armed "
                    "friend of the agent within 3 m. Off = round 10's pick (the nearest live bot)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_floor_rule, true, "Gameplay",
                    "Counter-Operative (round 12): which spiderbot player 2 may be given is decided by where it "
                    "stands and by Rare's own state, not by its 'allies' mask: any bot that is in the world (its "
                    "spawner slot active, its AI ticking), not dying, not one of the glass tank's, and on a floor "
                    "(its up axis within pdz_counterop_spiderbot_floor_deg of the world's). A set-piece bot Rare has "
                    "not released yet is released for him the way Rare's script releases it (allies and enemies "
                    "cleared). Off = round 11's test (allies FFFF refused, up.y over 0.985)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_floor_deg, 35.0, "Gameplay",
                      "Counter-Operative (round 12): a spiderbot counts as standing on a floor while its up axis is "
                      "within this many degrees of the world's up")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_spawn, true, "Gameplay",
                    "Counter-Operative on Datacore (round 12): during the spiderbot battle (after the cutscene `mid`, "
                    "until the agent reaches the platform top), when player 2 waits and no free floor bot is there "
                    "for him, one is made through Rare's own spiderbot spawner (its spawn of one slot, 823060C0) at "
                    "one of that spawner's own pads, out of every player's view and not next to the agent. Off = he "
                    "waits for one of Rare's (round 11)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_spawn_min_m, 8.0, "Gameplay",
                      "Counter-Operative (round 12): a spiderbot made for player 2 is never made nearer the agent "
                      "than this many metres")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_edge_guard, true, "Gameplay",
                    "Counter-Operative: a driven spiderbot does not walk off an edge that has no surface beyond it "
                    "(it goes over a corner onto the next surface when there is one, else it stops at the edge)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_cam_roll, true, "Gameplay",
                    "Counter-Operative (round 12, default on): the driven spiderbot's camera tilts with the surface "
                    "the bot stands on (after Rare's update of player 2's camera its up vector and look point are "
                    "set from the bot's eased frame; the blend time is pdz_counterop_spiderbot_cam_tilt_s). Off = "
                    "round 11: his first-person camera keeps the world's up")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_side_flip, true, "Gameplay",
                    "Counter-Operative (round 12, default on): the driven spiderbot's sideways command is flipped, so "
                    "that left stick right moves it to its right on every surface (the mover's local +x is the "
                    "bot's LEFT). Off = round 11 (sideways reversed)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_bot_relative, true, "Gameplay",
                    "Counter-Operative (round 12, default on): the driven spiderbot's left stick is in the bot's own "
                    "frame on every surface (up = its forward, right = its right; the chase camera sits behind it "
                    "and tilts with it). Off = round 11: the stick is laid into the surface from the camera's view")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_cam_ease_s, 0.12, "Gameplay",
                      "Counter-Operative (round 12): the driven spiderbot's chase camera follows the bot through a "
                      "critically damped spring that covers most of a gap in this many seconds (by the clock, the "
                      "same at 60 and 120 fps); its heading eases over the same time. 0 = round 11 (the camera "
                      "rigidly on the bot, a per-frame ease of its axes)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_cam_tilt_s, 0.25, "Gameplay",
                      "Counter-Operative (round 12): the driven spiderbot's camera blends its up axis to a new "
                      "surface over about this many seconds (never a snap). Used while "
                      "pdz_counterop_spiderbot_cam_ease_s is above 0")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_spiderbot_cam_wall_mask, 16, "Gameplay",
                     "Counter-Operative (round 12): the driven spiderbot's chase camera is kept out of walls and "
                     "doors: Rare's physics ray (82259B80) from just over the bot to the camera's place with this "
                     "hit mask (16 = Rare's camera blockers; 1 bullets, 2 players, 32 AI sight), and the camera "
                     "stops 20 cm short of a hit. 0 = off (round 11). Used while "
                     "pdz_counterop_spiderbot_cam_ease_s is above 0")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_cam_back_cm, 130.0, "Gameplay",
                      "Counter-Operative: the driven spiderbot's chase camera sits this far behind it (0 with "
                      "pdz_counterop_spiderbot_cam_up_cm 25 = a first-person view from the bot)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_cam_up_cm, 75.0, "Gameplay",
                      "Counter-Operative: the driven spiderbot's chase camera sits this far over it, along the bot's "
                      "own up (out from the wall when it is on one)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_poll, true, "Gameplay",
                    "Counter-Operative: while player 2 drives a spiderbot his controller is read directly every tick "
                    "(the game's own XInputGetState, called for his pad), not only when the game itself polls it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_turn, 2.6, "Gameplay",
                      "Counter-Operative: a driven spiderbot's turn rate at full right stick, radians a second (right on "
                      "the stick turns him right on screen since round 11: the sign is flipped in the code)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_spider_stick, "", "Testing",
                      "TESTING TOOL (counter-op, round 9): 'x,y,turn,fire' (-1..1, fire 0 / 1) replaces player 2's "
                      "sticks and trigger for a driven spiderbot; empty = his pad")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_spider_chase, 0, "Testing",
                     "TESTING TOOL (counter-op, round 9): 1 = a driven spiderbot's sticks steer it at the agent and "
                     "its trigger is pulled within 3 m of her (for scripted attack tests); 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_spider_kind, 0, "Testing",
                     "TESTING TOOL (counter-op, round 9): 1 = player 2 is only given yellow-eyed (detonating) "
                     "spiderbots, 2 = only ordinary ones; 0 = the nearest of either kind")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_wait_s, 5.0, "Gameplay",
                      "Counter-Operative: seconds between the end of one driven spiderbot and the next")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_range_m, 60.0, "Gameplay",
                      "Counter-Operative: a spiderbot is only given to player 2 while it is within this many metres "
                      "of the agent (she has left the section otherwise)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_spider_auto, false, "Testing",
                    "TESTING TOOL (counter-op): a driven spiderbot is steered at the agent by the tool (stick "
                    "forward towards her) and set off within 6 m of her, as an input script cannot see where she is")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 13 (b): the flat-ground lock.
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_flat, true, "Gameplay",
                    "Counter-Operative (round 13, default on): the spiderbot player 2 drives is locked to flat-ground movement: it "
                    "never goes onto a wall or a ceiling (no step onto a wall he pushes against, no step over an edge "
                    "onto a wall face, and Rare's mover is not given a surface steeper than "
                    "pdz_counterop_spiderbot_flat_deg), a bot on a wall or ceiling is never given to him, one that "
                    "ends up there is put on the floor under it, and his view keeps the world's up (no roll with the "
                    "surface). Ramps and slopes still work. Rare's own bots are not touched. Off = round 12")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_flat_deg, 45.0, "Gameplay",
                      "Counter-Operative (round 13): with pdz_counterop_spiderbot_flat, a surface counts as a floor "
                      "(a ramp, a slope) while its normal is within this many degrees of the world's up; anything "
                      "steeper is a wall and the driven bot does not go onto it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_flat_drop_m, 0.0, "Gameplay",
                      "Counter-Operative (round 13): with pdz_counterop_spiderbot_flat, a driven bot held against an "
                      "edge for 0.4 s steps down to the floor under the edge when there is one within this many "
                      "metres (it cannot walk down the wall face any more). 0 = it always stops at an edge")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 13b (t): the step rule (small height changes are not edges and not walls).
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_flat_step, true, "Gameplay",
                    "Counter-Operative (round 13b): with pdz_counterop_spiderbot_flat, where Rare's surface ray finds "
                    "nothing under the point ahead of the driven bot (the 'edge' test) but a floor lies straight "
                    "under the next half metre within pdz_counterop_spiderbot_flat_step_cm of the bot's height and "
                    "nothing is in the way, the bot is put on that floor at once and is not held (the foot of a "
                    "slope, a kerb, a stair going down); and a short steep face in Rare's mover (a kerb's chamfer) "
                    "is not refused while such a floor lies just beyond it. Off = round 13 (held at such places)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_flat_lip, false, "Gameplay",
                    "Counter-Operative (round 13b, OFF: run t_spider3 09:54:36 showed it lets the bot 1 m up a 70 "
                    "degree bank before it is brought down): the lip half of pdz_counterop_spiderbot_flat_step (a "
                    "short steep face in Rare's mover is not refused while a floor lies just beyond it)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spiderbot_flat_step_cm, 40.0, "Gameplay",
                      "Counter-Operative (round 13b): the height the driven spiderbot steps down (or over a lip) "
                      "without stopping, in cm (pdz_counterop_spiderbot_flat_step). Higher drops are edges: it "
                      "stops, and steps down after 0.4 s only within pdz_counterop_spiderbot_flat_drop_m")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_flat_step_up, false, "Gameplay",
                    "Counter-Operative (round 13b): with pdz_counterop_spiderbot_flat, a driven bot pushed for 0.2 s "
                    "against something with a floor on top of it no higher than pdz_counterop_spiderbot_flat_step_cm "
                    "(a stair's riser, a kerb) and nothing in the way steps up onto that floor. Off = it stays")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_spider_place, "", "Testing",
                      "TESTING TOOL (counter-op, round 13b): 'x,y,z,dx,dz': each time the value changes the driven "
                      "spiderbot is put on the ground under that point (Rare's physics ray down from 80 cm over "
                      "it), its nose towards the level direction (dx, dz)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_spider_survey, "", "Testing",
                      "TESTING TOOL (counter-op, round 13b, logging only): 'x,y,z,half_m,step_cm': each time the "
                      "value changes, while a spiderbot is driven, the ground heights of the square of that half "
                      "size round (x, z) are logged, one line a row (cm over y; Rare's physics ray straight down "
                      "from 250 cm over y; '.' = nothing within 9 m under it)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_spider_wall, 0, "Testing",
                     "TESTING TOOL (counter-op, round 13): each time the value changes to non-zero the driven "
                     "spiderbot is put onto the surface in front of it (round 9's step onto a wall, past the flat "
                     "lock), to test that the lock brings it back down to the floor")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

bool PdzMinigameRunning(uint32_t holder); // minigame_log.cpp
namespace counter_op_pad
{
  uint32_t Polled(uint32_t user, float &lx, float &ly); // counter_op_jetpack.cpp
  void PolledRight(uint32_t user, float &rx, float &ry); // counter_op_jetpack.cpp (round 9)
  uint32_t PollCount(uint32_t user);                     // round 10
  uint32_t PollResult(uint32_t user);                    // round 10
  void BlockY(int32_t user);                             // round 11: his Y is dropped while he drives a bot
}
REX_EXTERN(sub_82AD4A18); // XInputGetState(user r3, state r4) (hooked in test_harness.cpp: it records each pad)
namespace counter_op_guards
{
  uint32_t DatacoreStageWord(const uint8_t *base); // counter_op_guards.cpp: 0 when not Datacore
}
// Round 12 (item 2): Rare's spiderbot spawner (kind B, vtable 820A1854; layout as kind A: +4 slot count, +0xC
// template count, +0x10 pad-number list, +0x14 pad count, +0x2C slots (8 bytes each, +4 the bot), +0x30 -> one
// 'active' byte a slot).
REX_EXTERN(sub_823060C0); // kind B: spawn one slot (spawner r3, template index r4, pad index r5, slot index r6): what
                          // Rare's Fill 82304828 calls for each empty slot (822771B0, the slot's handle, the pad's
                          // 'occupied' bit, the active byte, the spawner's on-spawn callback)
REX_EXTERN(sub_82153C00); // Rare's spawners' pad test (pad number r3, 'even if seen' r5, 'ignore the pad flags' r6,
                          // f1 height) -> byte 1 = blocked (pad flags 0x90, or a human sees the point over the pad)
REX_EXTERN(sub_8216A5D8); // (pad number) -> pad object (+0x10 position, +0x34 flags) or 0

REX_EXTERN(__imp__sub_825C9180); // spiderbot retarget (bot r3, r4)
REX_EXTERN(sub_821433D8);        // handle set (handle address r3, object or 0 r4)
REX_EXTERN(sub_82518DB0);        // a character's facing (object r3, out vec3 r4)
REX_EXTERN(sub_825AD148);        // Rare's move of a character (character r3, &position r4, r5 0)
REX_EXTERN(__imp__sub_825C7A40); // spiderbot AI tick (bot r3, f1 dt)
REX_EXTERN(__imp__sub_825C7CC8); // spiderbot death (bot r3, hit record r4, r5, r7): the explosion when awake
REX_EXTERN(sub_8214E520);        // level message (manager r3, source r4, activator r5, r6 0, &message r7)
// Round 9: the drive
REX_EXTERN(__imp__sub_8219AE50); // spiderbot movement controller: this frame's command (controller r3). Only
                                 // spiderbots have this controller (vtable 820910E8, made in the bot's ctor only)
REX_EXTERN(sub_825DEB40);        // mover: set the command (mover r3, r4 = 5 floats: local vx, vy, vz, yaw rate, aux)
REX_EXTERN(sub_825DD3A0);        // mover: "moving" on / off (mover r3, r4)
REX_EXTERN(sub_821925C0);        // movement controller: stop (controller r3)
REX_EXTERN(sub_82518580);        // an object's matrix (object r3, out 0x40: side, up +0x10, forward +0x20, position +0x30)
REX_EXTERN(sub_82518650);        // set an object's matrix (object r3, &matrix r4)
REX_EXTERN(sub_821784F0);        // task allocator (r3 size) -> task
REX_EXTERN(sub_821B1A00);        // spiderbot jump task ctor (task r3, &destination r4, &landing normal r5, r6 0)
REX_EXTERN(sub_82180508);        // task stack push (stack r3 = bot + 0xA8C, task r4, r5 1)
REX_EXTERN(sub_8217E990);        // round 15: a task's end step after its stop (r3 = task), as the push does it
REX_EXTERN(sub_82181430);        // round 15: remove a task from the list and destroy it (stack r3, task r4)
REX_EXTERN(sub_821816C8);        // task stack tick (stack r3, r4 [bot + 0xD90], f1 dt)
REX_EXTERN(sub_825C8388);        // spiderbot "die now" (bot r3, &position r4, r6 0): builds a hit record, calls 825C7CC8
REX_EXTERN(sub_82151210);        // round 16: Rare's position getter (object r3) -> r3 &3 floats (a character: her feet)
REX_EXTERN(sub_821B1838);        // round 16: the leap sweep's reach test (entry r3, query r4) -> byte (see ClosePounce)
REX_EXTERN(sub_821B18F0);        // round 16: the leap sweep's hit call (entry r3, query r4) -> byte (hooked below)
void PdzCounterOpHudRestoreWeapons(PPCContext &ctx, uint8_t *base, int32_t pad, const char *why); // counter_op_hud.cpp
void PdzCounterOpHudRestoreTick(PPCContext &ctx, uint8_t *base, int32_t pad);                     // (round 16, item 2)
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_only_agent, true, "Gameplay",
                    "Counter-Operative (round 11, default on): the spiderbot player 2 drives can only affect the "
                    "agent: nobody who would take it for an enemy sees or targets it (one it pounced on may, for 10 "
                    "s), its pounce and its blast hurt nobody else, and neither is heard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(sub_8251EBD0);        // Rare's "friend?" (object r3, other r4) -> byte
REX_EXTERN(__imp__sub_825BA2B8); // explosive: set the owner (explosive r3, owner r4): handle +0x42C, object +0x430
REX_EXTERN(__imp__sub_8254A6E8); // a character's blast handler, vtable +0xC4 (victim r3, explosive r4, attacker r5)
REX_EXTERN(__imp__sub_825C88E0); // a spiderbot's blast handler, vtable +0xC4 (same arguments)
REX_EXTERN(sub_8269CEF0); // Rare's surface probe (&position in / out r3, object r4, r5 [object+0xC], r6 &old position)
                          // -> r3 byte 1 = hit. Class 0x3A branch 8269D1C0: a ray along the bot's OWN up axis; the
                          // hit record is copied to mover +0x64 (normal +0x88, point +0xA0, flags +0xC0; +0xC0 = 0
                          // on a miss). Hooked in player_move.cpp (pass-through for anything but the player).
REX_EXTERN(__imp__sub_8269CEF0); // round 13 (b): Rare's own probe, for the flat lock's call-in from player_move.cpp's hook
REX_EXTERN(sub_82259B80); // physics ray (mask r3, &from r4, &to r5, &visitor r6, r7 1) -> byte: hit (hooked in
                          // cut_scenarios.cpp; visitor 82090738 = the hitscan's: the hit point at visitor +68)
REX_EXTERN(sub_825DE0E8); // mover vtable +0x18: Rare's placement (mover r3, &position r4, &three angles r5): the
                          // mover's own position (+0x40), moving on, forced velocity off, normal from the matrix

namespace
{
  constexpr uint32_t kSpiderVtable = 0x820C9E98;
  constexpr uint32_t kPos = 0x30, kTeam = 128, kAllies = 130, kEnemies = 132;
  // +0xDD4 is the DETONATOR-KIND flag (round 7 took it for "awake"): 1 = the yellow-eyed kind whose death is a blast
  constexpr uint32_t kHumanTarget = 0xDA4, kAiTarget = 0xDAC, kAwake = 0xDD4, kDying = 0xDEC, kExploded = 0xDED;
  constexpr uint32_t kDetonator = 0xDD4;
  constexpr uint32_t kMover = 0xE4, kCtl = 0xD9C, kCtlBot = 0x38;
  constexpr uint32_t kSpeed = 0x3F4;    // Rare's spiderbot speed, 400 units / s (ctor 825C67EC)
  constexpr uint32_t kDetached = 0x3BC; // byte: not on a surface
  constexpr uint32_t kFallTask = 0xDD0, kJumpHandle = 0x470, kJumpTask = 0x474, kTaskStack = 0xA8C, kTaskStackArg = 0xD90;
  constexpr uint32_t kRetargetTimer = 0xDB0;
  constexpr uint32_t kMoverVel = 0x20, kMoverYaw = 0x38, kMoverForced = 0x134, kMoverMoving = 0xC;
  constexpr uint32_t kGame = 0x82D6BFD8, kCheckpointByte = 0x82E426F8;
  constexpr uint32_t kMoverNormal = 0x88, kMoverRecFlags = 0xC0, kMoverNormal2 = 0xF0;
  constexpr uint32_t kFallTimer = 0x30;         // fall task: seconds of tumble left (821A8950)
  constexpr uint32_t kDatacoreScript = 0x820A1610, kTankCount = 0x7E4, kTankArray = 0x80C; // spawner +0x7E0
  constexpr uint32_t kCharList = 0x82E454A0, kHand0 = 4076, kHand1 = 4080;
  constexpr uint32_t kGuardVtable = 0x820C45A0, kHumanVtable = 0x820C7DE0;

  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint32_t Load16(const uint8_t *base, uint32_t a) { return (uint32_t(base[a]) << 8) | base[a + 1]; }
  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t u = Load32(base, a);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  }
  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  float Dist(const uint8_t *base, uint32_t a, uint32_t b)
  {
    if (!Ptr(a) || !Ptr(b))
    {
      return -1.0f;
    }
    const float dx = LoadFloat(base, a + kPos) - LoadFloat(base, b + kPos),
                dy = LoadFloat(base, a + kPos + 4) - LoadFloat(base, b + kPos + 4),
                dz = LoadFloat(base, a + kPos + 8) - LoadFloat(base, b + kPos + 8);
    return std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f;
  }
  std::string Who(const uint8_t *base, uint32_t object)
  {
    if (!Ptr(object))
    {
      return "none";
    }
    const uint32_t agent = PdzCounterOpAgent(), p2 = PdzCounterOpPlayer2();
    return fmt::format("{:08X} ({}, masks {:04X} / {:04X} / {:04X})", object,
                       object == agent ? "the agent" : object == p2 ? "player 2" : fmt::format("vtable {:08X}", Load32(base, object)),
                       Load16(base, object + kTeam), Load16(base, object + kAllies), Load16(base, object + kEnemies));
  }

  // The drive (pdz_counterop_spiderbot). One bot at a time; every other spiderbot stays Rare's.
  struct Drive
  {
    uint32_t bot = 0;     // the driven spiderbot, 0 = none
    bool manual = false;  // his sticks own its movement command this frame (not while Rare's jump or fall runs)
    bool taken = false;   // the take-over calls are done
    bool fired = false;   // his trigger set a detonator off
    float x = 0.0f, y = 0.0f, turn = 0.0f; // left stick (right +, forward +), right stick x, each -1..1
    int64_t since = 0, ended = 0, last_line = 0, blocked_since = 0, last_climb = 0, far_since = 0, last_pounce = 0;
    int64_t fwd_since = 0, fall_since = 0, saved_at = 0, put_back_at = 0, chase_at = 0, chase_fire = 0;
    float chase_sign = 1.0f, chase_angle = 0.0f;
    uint8_t saved[0x40] = {}; // its matrix when it last stood on a surface under his command
    bool saved_ok = false;
    uint32_t driven = 0;  // bots driven this level
    uint32_t pounces = 0, climbs = 0;
    uint32_t presses = 0; // round 15: trigger presses that asked for a pounce (started or not) with this bot
    float cam[3] = {}, look[3] = {}, up[3] = {0.0f, 1.0f, 0.0f};
    float s_up[3] = {0.0f, 1.0f, 0.0f}, s_fwd[3] = {0.0f, 0.0f, 1.0f}; // the camera's eased axes
    bool view = false;
    uint32_t prev_buttons = 0;
    bool prev_test_fire = false;
    float start_agent_health = 0.0f;
    uint32_t scratch = 0; // 0xA0 bytes of guest memory (+0x80: an XINPUT_STATE for the direct poll)
    uint32_t polls_at_line = 0, own_polls = 0; // round 10: the game's polls of his pad at the last log line; ours
    float raw[4] = {};                         // round 10: his sticks as polled (lx, ly, rx, ry), before the dead zone
    bool input_seen = false;                   // round 10: a stick or a button of his has reached the drive
    bool left = false;                         // round 10: he left the bot (B)
    float cx = 0.0f, cz = 0.0f;   // round 11: the command in the bot's own frame (side, forward), -1..1, camera-true
    bool hold = false;            // round 11: an edge with nothing beyond it: no movement command this frame
    bool was_busy = false, was_fall = false, fall_cut = false;
    uint32_t holds = 0, flats = 0;
    // Round 12: the eased chase camera (pdz_counterop_spiderbot_cam_ease_s)
    float a_pos[3] = {}, a_vel[3] = {}; // the point the camera hangs on: follows the bot through a damped spring
    float cam_frac = 1.0f;              // how much of the way out to its place the camera is (1 = nothing in the way)
    double cam_t = 0.0;                 // the clock at the last camera step (seconds)
    bool cam_init = false;
    uint32_t cam_pulls = 0, cam_self = 0;
    // Round 13 (b): the flat-ground lock (pdz_counterop_spiderbot_flat)
    float bot_up[3] = {0.0f, 1.0f, 0.0f}; // the bot's up axis at the last drive tick (read by the probe call-in)
    int64_t edge_said = 0;
    uint32_t edge_lines = 0;
    int64_t wall_since = 0, edge_since = 0, edge_seen = 0, flat_said = 0, flat_status_at = 0, probe_said = 0;
    uint32_t flat_refused = 0, probe_refused = 0, probe_lines = 0, downs = 0, drops = 0, edge_refused = 0;
    int32_t test_wall = 0;
    // Round 13b (t): the step rule (pdz_counterop_spiderbot_flat_step)
    int64_t probe_refused_at = 0, lip_ok_until = 0, lip_checked_at = 0, step_said = 0, lip_said = 0;
    uint32_t steps = 0, step_ups = 0, lips = 0, lip_passed = 0, step_lines = 0, lip_lines = 0;
  };
  Drive g_drive;
  bool g_own_probe = false; // round 13 (b): our own call of Rare's probe runs (a candidate frame: never refused)
  float FlatCos()
  {
    return std::cos(float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_flat_deg), 10.0, 80.0)) * 3.14159265f / 180.0f);
  }
  // Round 11: what of his bot is kept from everyone but the agent.
  struct Pounced
  {
    uint32_t who = 0;
    int64_t at = 0;
  };
  Pounced g_pounced[8];          // who his pounce touched: they may see (and shoot) his bot for 10 s
  // Round 16 (item 3): the leap of a pounce he started at the agent, followed tick by tick (pdz_counterop_spiderbot_
  // close_pounce). Rare's jump task (821B1A00, update 821B1E38): +0x10 the bot, +0x30 the destination (read every
  // airborne update), +0x68 the phase (1 turn, 2 wind-up animation, 3 airborne with the contact sweep 821B2B70, 4
  // landing), +0x94 the leap's victims (up to 5, end pointer at +0xA8: once per leap).
  struct ClosePounce
  {
    uint32_t task = 0, bot = 0; // 0 = none followed
    uint32_t number = 0;        // his pounce number with this bot
    float start_m = 0.0f;       // bot to agent at the press
    int32_t phase = 0;          // the task's phase after the last tick
    uint32_t moves = 0;         // ticks the destination was put on her as she is now
    bool launch_checked = false;
    bool hit = false;           // Rare's hit call said yes for this leap
    std::string forced;         // what our contact call did (empty = none)
    int64_t at = 0;
  };
  ClosePounce g_cp;
  uint32_t g_cp_mem = 0; // 0x100 bytes of guest memory: the contact query (+0), its list (+0x40), its visitor (+0x50)
  uint32_t g_cp_lines = 0;
  void ClosePounceBefore(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t task);
  void ClosePounceAfter(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t task);
  uint32_t g_his_blast[4] = {};  // the explosives Rare made for his bot (825BA2B8 with his bot as the owner)
  int64_t g_his_blast_at[4] = {};
  uint32_t g_last_bot = 0;       // the yellow-eyed bot he drove last: its blast goes off after the drive is over
  int64_t g_last_bot_until = 0;
  uint32_t g_refused = 0;

  std::unordered_map<uint32_t, uint8_t> g_flags; // bot -> awake | dying << 1 | exploded << 2 at its last tick
  std::unordered_map<uint32_t, int64_t> g_bots; // bot -> the last AI tick (ms); game thread only
  int64_t g_last_log = 0;
  uint32_t g_deaths = 0;
  std::string g_last_message;
  uint32_t g_message_scratch = 0;
} // namespace

// sub_825C7A40(bot, f1 dt): the spiderbot AI tick. Rare's; the bot is noted for the census.
REX_HOOK_RAW(sub_825C7A40)
{
  const uint32_t bot = ctx.r3.u32;
  if (Ptr(bot) && PdzCounterOpOn())
  {
    g_bots[bot] = NowMs();
    if (REXCVAR_GET(pdz_counterop_spider_log))
    {
      const uint8_t flags = uint8_t((base[bot + kAwake] ? 1 : 0) | (base[bot + kDying] ? 2 : 0) | (base[bot + kExploded] ? 4 : 0));
      const auto it = g_flags.find(bot);
      static uint32_t lines = 0;
      if (it != g_flags.end() && it->second != flags && ++lines <= 80)
      {
        REXLOG_INFO("pdz_counterop: spider: spiderbot {:08X}: detonator kind / dying / exploded {}{}{} -> {}{}{}, {:.1f} m from "
                    "the agent{}",
                    bot, it->second & 1, (it->second >> 1) & 1, (it->second >> 2) & 1, flags & 1, (flags >> 1) & 1,
                    (flags >> 2) & 1, Dist(base, bot, PdzCounterOpAgent()), bot == g_drive.bot ? " (the driven one)" : "");
      }
      g_flags[bot] = flags;
    }
    // Round 9: the driven bot has no brain while player 2 drives it. Its task stack still runs while Rare's jump (the
    // pounce) is on it; while it falls or dies the whole tick is Rare's (the fall is a task only the tick moves).
    if (bot == g_drive.bot && g_drive.taken && Load32(base, bot) == kSpiderVtable)
    {
      const bool falling = base[bot + kDetached] || Load32(base, bot + kFallTask) || base[bot + kDying];
      if (!falling)
      {
        if (const uint32_t task = Load32(base, bot + kJumpTask))
        {
          const double dt = ctx.f1.f64;
          PPCContext saved = ctx;
          ClosePounceBefore(saved, base, bot, task); // round 16 (item 3): the destination on her as she is now
          {
            rex::CallFrame frame(saved);
            frame.ctx.r3.u64 = bot + kTaskStack;
            frame.ctx.r4.u64 = Load32(base, bot + kTaskStackArg);
            frame.ctx.f1.f64 = dt;
            sub_821816C8(frame.ctx, base);
          }
          ClosePounceAfter(saved, base, bot, task); // round 16 (item 3): close starts, the outcome
        }
        return;
      }
    }
  }
  __imp__sub_825C7A40(ctx, base);
}

// sub_8219AE50(controller): the spiderbot movement controller's command for this frame. Rare's for every bot but the
// driven one. For that one the command is player 2's: local +Z is the bot's forward, local +X its side, local Y its
// own up; the mover then follows the surface under the bot by itself (Rare's ray along the bot's own down).
// Rare's pounce hit (the jump task's sweep found a character): log only, for the proof that his leap hurts her.
REX_EXTERN(__imp__sub_821B18F0);
namespace
{
uint32_t g_last_agent = 0;
float AgentHealth(const uint8_t *base, uint32_t agent);
} // namespace
REX_HOOK_RAW(sub_821B18F0)
{
  const uint32_t a = ctx.r3.u32, b = ctx.r4.u32;
  const uint32_t his_task = g_drive.bot && pdz::guestmem::Ok(base, g_drive.bot, kJumpTask + 4) && // Sun 22 Nov night (audit)
                                    Load32(base, g_drive.bot) == kSpiderVtable
                                ? Load32(base, g_drive.bot + kJumpTask)
                                : 0;
  // Round 11 (item 9): his pounce touches only the agent.
  {
    const uint32_t info = Ptr(b) ? Load32(base, b + 0x30) : 0;
    const uint32_t leaper = Ptr(info) ? Load32(base, info) : 0;
    const uint32_t victim = Ptr(a) ? Load32(base, a + 0xC) : 0;
    if (leaper && leaper == g_drive.bot && REXCVAR_GET(pdz_counterop_spiderbot_only_agent) &&
        victim != PdzCounterOpAgent())
    {
      if (Ptr(victim) && victim != leaper && victim != PdzCounterOpPlayer2())
      {
        const int64_t now = NowMs();
        Pounced *use = &g_pounced[0];
        bool fresh = true;
        for (Pounced &p : g_pounced)
        {
          if (p.who == victim)
          {
            use = &p;
            fresh = now - p.at >= 10000;
            break;
          }
          use = p.at < use->at ? &p : use;
        }
        use->who = victim;
        use->at = now;
        if (fresh)
        {
          REXLOG_INFO("pdz_counterop: spider: player 2's pounce touched {:08X} (vtable {:08X}), not the agent: no "
                      "damage, no reaction; that one may see and shoot his bot for 10 s",
                      victim, Load32(base, victim));
        }
      }
      ctx.r3.u64 = 0;
      return;
    }
  }
  // Sun 22 Nov night (audit): the agent noted at a drive only while that game still runs with her (never past a
  // reset, a reload or the level).
  if (g_last_agent && g_last_agent != PdzCounterOpAgent())
  {
    g_last_agent = 0;
  }
  const float before = g_last_agent ? AgentHealth(base, g_last_agent) : -1.0f;
  __imp__sub_821B18F0(ctx, base);
  if ((ctx.r3.u32 & 0xFF) && his_task && his_task == g_cp.task && Ptr(a) && Load32(base, a + 0xC) == PdzCounterOpAgent())
  {
    g_cp.hit = true; // round 16 (item 3): the followed leap reached her (Rare's sweep or our contact call)
  }
  if ((ctx.r3.u32 & 0xFF) && (his_task || REXCVAR_GET(pdz_counterop_spider_log)))
  {
    static uint32_t lines = 0;
    if (lines++ < 200)
    {
      REXLOG_INFO("pdz_counterop: spider: a pounce HIT (Rare's 821B18F0, r3 {:08X} r4 {:08X}){}: the agent's health "
                  "{:.1f} -> {:.1f} across the call",
                  a, b, his_task ? fmt::format(", while PLAYER 2's bot {:08X} is in its leap (task {:08X})", g_drive.bot, his_task) : "",
                  before, g_last_agent ? AgentHealth(base, g_last_agent) : -1.0f);
    }
  }
}

// Round 16 (item 3): close pounces that missed (play log 027, 15:19:12-36, bot 01A6C900: of 7 pounces started at the
// agent within 2.5 m only 3 hit). Rare's jump task (update 821B1E38) turns the bot to the destination (phase 1), plays
// the wind-up to 0.82 (phase 2: together 0.5-0.8 s in the log), then flies at 1100 / s towards the destination,
// re-aimed at it every update (phase 3). Only phase 3 sweeps for contact (821B2B70): three spheres of a quarter of
// the bot's size (+60) around its own origin, swept from where they were, through the world's character tree
// (821B2E88 -> 821B30E0): per character 821B1838 (alive character, not yet a victim of this leap, the sweep within its
// reach sphere = entry +0x20 / +0x2C, the character's own +0x30 position and +0x3C), then 821B18F0 (its body test,
// victim vtable +0xA0 82544E08 -> a linear cast 82523750, UNSURE whether it reports a sweep that starts inside
// her; then Rare's damage, vtable +0xD4, 20 a leap, and the victim joins the leap's list). Phase 3 ends when the destination falls behind the bot's forward
// axis or its speed drops under 935 (blocked); then it lands (phase 4) if a floor is under it. Our pounce gave the
// task her FEET as they were at the press. Seen: she moved during the turn and wind-up, so the leap went to her old
// place (#12 landed 1.4 m from her, #9 flew past her and off the floor); from 0.9 m (#14, #15) the bot was pressed
// against her at once (inferred: it moved under 0.4 m), the flight ended without a contact and it landed there.
namespace
{
  // (the victims' end pointer is at +0x94 + 0x14 = +0xA8)
  constexpr uint32_t kJumpDest = 0x30, kJumpTurnTo = 0x5C, kJumpPhase = 0x68, kJumpVictims = 0x94;
  constexpr uint32_t kHitVisitorVtable = 0x82090738; // the hit visitor 821B2B70 gives each sweep (as Rare's hitscan's)
  void PutWord(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
  }
  void PutFloat(uint8_t *base, uint32_t a, float f)
  {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    PutWord(base, a, u);
  }
  float AimCm() { return float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_close_pounce_aim_cm), 0.0, 120.0)); }
  float ReachM() { return float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_close_pounce_reach_m), 0.3, 2.5)); }
  // Her aim point as she is now: Rare's position getter (her feet, as Rare's own pounce goal 821E7274 reads it),
  // raised by the aim height along the world's up.
  bool AgentAim(PPCContext &ctx, uint8_t *base, uint32_t agent, float aim[3])
  {
    if (!Ptr(agent))
    {
      return false;
    }
    uint32_t p = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = agent;
      sub_82151210(frame.ctx, base);
      p = frame.ctx.r3.u32;
    }
    const uint32_t at = Ptr(p) ? p : agent + kPos;
    for (uint32_t i = 0; i < 3; ++i)
    {
      aim[i] = LoadFloat(base, at + 4 * i);
    }
    aim[1] += AimCm();
    return std::isfinite(aim[0]) && std::isfinite(aim[1]) && std::isfinite(aim[2]);
  }
  bool InVictims(const uint8_t *base, uint32_t first, uint32_t who)
  {
    const uint32_t end = Load32(base, first + 0x14);
    if (end < first || end > first + 0x14)
    {
      return false;
    }
    for (uint32_t at = first; at < end; at += 4)
    {
      if (Load32(base, at) == who)
      {
        return true;
      }
    }
    return false;
  }
  // Rare's own contact pair for the leap, with a sweep that enters her: from 1.2 m out on the bot's side at the aim
  // height to her +0x30 position (the centre of her reach sphere), the sweep radius Rare's (a quarter of the bot's
  // size). 821B1838 decides (alive, not yet a victim of this leap, in reach), 821B18F0 tests her body and does Rare's
  // damage with the bot as the attacker. victims = the leap's list (task +0x94), or a fresh one when the task is gone.
  std::string ContactCall(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t victims, uint32_t agent)
  {
    const uint32_t entry = agent + 0x10; // her entry in the character tree (Rare's hits: r3 = the character + 0x10)
    if (Load32(base, entry + 0xC) != agent)
    {
      return fmt::format("no contact call: her entry +0x10 does not lead back to her ({:08X})", Load32(base, entry + 0xC));
    }
    if (InVictims(base, victims, agent))
    {
      return "no contact call: the leap already counts her";
    }
    float aim[3];
    if (!AgentAim(ctx, base, agent, aim))
    {
      return "no contact call: her position is unreadable";
    }
    float to[3], from[3];
    for (uint32_t i = 0; i < 3; ++i)
    {
      to[i] = LoadFloat(base, agent + kPos + 4 * i);
    }
    float dx = LoadFloat(base, bot + kPos) - aim[0], dz = LoadFloat(base, bot + kPos + 8) - aim[2];
    float len = std::sqrt(dx * dx + dz * dz);
    if (!(len > 1.0f))
    {
      dx = 1.0f, dz = 0.0f, len = 1.0f;
    }
    from[0] = aim[0] + dx / len * 120.0f, from[1] = aim[1], from[2] = aim[2] + dz / len * 120.0f;
    float radius = LoadFloat(base, bot + 60) * 0.25f;
    if (!(radius > 0.0f && radius < 200.0f))
    {
      radius = 10.0f;
    }
    const uint32_t q = g_cp_mem, list = g_cp_mem + 0x40, visitor = g_cp_mem + 0x50;
    std::memset(base + q, 0, 0xC0); // (+0xC0: the fresh victims list, set up by the caller, is kept)
    for (uint32_t i = 0; i < 3; ++i)
    {
      PutFloat(base, q + 4 * i, from[i]);          // +0x00 from
      PutFloat(base, q + 0x10 + 4 * i, to[i] - from[i]); // +0x10 the way
    }
    PutFloat(base, q + 0xC, radius);   // +0x0C radius
    PutWord(base, q + 0x1C, visitor);  // +0x1C the hit visitor (821B2E88 r8)
    PutWord(base, q + 0x20, bot);      // +0x20 the bot (821B2E88 r9)
    PutWord(base, q + 0x28, 0xFFFFFFFFu);
    base[q + 0x2C] = 1;
    PutWord(base, q + 0x30, list);     // +0x30 {attacker, &victims}
    PutWord(base, list, bot);
    PutWord(base, list + 4, victims);
    PutWord(base, visitor, kHitVisitorVtable);
    bool reach = false, hit = false;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = entry;
      frame.ctx.r4.u64 = q;
      sub_821B1838(frame.ctx, base);
      reach = (frame.ctx.r3.u32 & 0xFF) != 0;
    }
    if (!reach)
    {
      return fmt::format("Rare's reach test 821B1838 said no (her reach sphere radius {:.0f}, the sweep's {:.0f})",
                         LoadFloat(base, agent + 0x3C), radius);
    }
    const float before = AgentHealth(base, agent);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = entry;
      frame.ctx.r4.u64 = q;
      sub_821B18F0(frame.ctx, base);
      hit = (frame.ctx.r3.u32 & 0xFF) != 0;
    }
    g_cp.hit = g_cp.hit || hit;
    return fmt::format("Rare's reach test 821B1838 yes, Rare's hit call 821B18F0 {} (sweep from ({:.0f}, {:.0f}, {:.0f}) "
                       "radius {:.0f} to her position; her reach sphere radius {:.0f}); her health {:.1f} -> {:.1f}",
                       hit ? "HIT" : "found no contact with her body", from[0], from[1], from[2], radius,
                       LoadFloat(base, agent + 0x3C), before, AgentHealth(base, agent));
  }
  std::string ContactNow(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t task, bool task_alive, uint32_t agent)
  {
    if (!g_cp_mem)
    {
      g_cp_mem = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x100);
      if (!g_cp_mem)
      {
        return "no contact call: no guest memory";
      }
      std::memset(base + g_cp_mem, 0, 0x100);
    }
    uint32_t victims = task + kJumpVictims;
    if (!task_alive)
    {
      victims = g_cp_mem + 0xC0; // the task is gone with its list: a fresh one (the leap is over: no second hit)
      std::memset(base + victims, 0, 0x18);
      PutWord(base, victims + 0x14, victims);
    }
    return ContactCall(ctx, base, bot, victims, agent);
  }

  // Before the bot's task stack runs (the AI tick hook): the followed leap's destination on her as she is now (the
  // turn's target too while it turns).
  void ClosePounceBefore(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t task)
  {
    if (!g_cp.task || g_cp.task != task || g_cp.bot != bot || !REXCVAR_GET(pdz_counterop_spiderbot_close_pounce))
    {
      return;
    }
    const int32_t phase = int32_t(Load32(base, task + kJumpPhase));
    float aim[3];
    if (phase >= 1 && phase <= 3 && AgentAim(ctx, base, PdzCounterOpAgent(), aim))
    {
      for (uint32_t i = 0; i < 3; ++i)
      {
        PutFloat(base, task + kJumpDest + 4 * i, aim[i]);
        if (phase == 1)
        {
          PutFloat(base, task + kJumpTurnTo + 4 * i, aim[i]);
        }
      }
      ++g_cp.moves;
    }
  }

  // After it ran: a close start gets Rare's contact call at the launch; a flight that ended within reach of her
  // without Rare's contact gets it then; the outcome is logged once the flight is over.
  void ClosePounceAfter(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t task)
  {
    if (!g_cp.task || g_cp.task != task || g_cp.bot != bot)
    {
      return;
    }
    constexpr uint32_t kJumpVtable = 0x82091B7C;
    const uint32_t agent = PdzCounterOpAgent();
    const bool alive = Load32(base, bot + kJumpTask) == task && Load32(base, task) == kJumpVtable;
    const int32_t phase = alive ? int32_t(Load32(base, task + kJumpPhase)) : -1;
    const bool on = REXCVAR_GET(pdz_counterop_spiderbot_close_pounce) && Ptr(agent);
    const float reach = ReachM(), d = Dist(base, bot, agent);
    bool hit = g_cp.hit || (alive && Ptr(agent) && InVictims(base, task + kJumpVictims, agent));
    if (on && alive && phase == 3 && !g_cp.launch_checked)
    {
      g_cp.launch_checked = true; // the launch tick: Rare's first airborne update (and sweep) comes next tick
      if (!hit && g_cp.start_m <= reach && d >= 0.0f && d <= reach)
      {
        g_cp.forced = fmt::format("close start ({:.1f} m at the press, {:.1f} m at the launch): ", g_cp.start_m, d) +
                      ContactNow(ctx, base, bot, task, true, agent);
        hit = g_cp.hit;
      }
    }
    const bool over = !alive || (g_cp.phase == 3 && phase != 3);
    if (on && over && !hit && g_cp.phase == 3 && d >= 0.0f && d <= reach)
    {
      g_cp.forced += fmt::format("{}the flight ended {:.1f} m from her without Rare's contact: ",
                                 g_cp.forced.empty() ? "" : " | ", d) +
                     ContactNow(ctx, base, bot, task, alive, agent);
      hit = g_cp.hit;
    }
    if (!over)
    {
      g_cp.phase = phase;
      return;
    }
    if (g_cp_lines++ < 400)
    {
      REXLOG_INFO("pdz_counterop: spider: close pounce #{} with {:08X}: {}{}; started {:.1f} m from her, aimed {:.0f} cm "
                  "above her feet, the destination put on her {} time(s); the flight is over after {:.2f} s ({}), "
                  "{:.1f} m from her, her health {:.1f}",
                  g_cp.number, bot,
                  hit ? (g_cp.forced.empty() ? "HIT by Rare's own leap" : "HIT through our contact call")
                      : (g_cp.phase == 3 ? "MISS" : "MISS (the leap ended before it flew)"),
                  g_cp.forced.empty() ? std::string() : " | " + g_cp.forced, g_cp.start_m, AimCm(), g_cp.moves,
                  double(NowMs() - g_cp.at) / 1000.0, alive ? fmt::format("phase {}", phase) : "the task is gone", d,
                  AgentHealth(base, agent));
    }
    g_cp = ClosePounce{};
  }
} // namespace

REX_HOOK_RAW(sub_8219AE50)
{
  const uint32_t ctl = ctx.r3.u32;
  const uint32_t bot = Ptr(ctl) ? Load32(base, ctl + kCtlBot) : 0;
  if (!g_drive.manual || !bot || bot != g_drive.bot || Load32(base, bot) != kSpiderVtable || !g_drive.scratch)
  {
    __imp__sub_8219AE50(ctx, base);
    return;
  }
  const uint32_t mover = Load32(base, bot + kMover);
  if (!Ptr(mover))
  {
    return;
  }
  const float speed = LoadFloat(base, bot + kSpeed);
  // Round 11: the command in the bot's own frame, as the camera shows it (PdzCounterOpSpiderDrive); none at an edge.
  float x = g_drive.hold ? 0.0f : g_drive.cx, y = g_drive.hold ? 0.0f : g_drive.cz;
  const float len = std::sqrt(x * x + y * y);
  if (len > 1.0f)
  {
    x /= len, y /= len;
  }
  // Round 11 (item 8; log 010: right stick +x gave turn +1 and yaw rate +2.6, which turned him LEFT on screen): the
  // sign is flipped here, in code; pdz_counterop_spiderbot_turn stays a positive rate (2.6).
  const float rate = -g_drive.turn * float(REXCVAR_GET(pdz_counterop_spiderbot_turn));
  const uint32_t cmd = g_drive.scratch + 0x60;
  const auto put = [&](uint32_t at, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    base[at] = uint8_t(u >> 24), base[at + 1] = uint8_t(u >> 16), base[at + 2] = uint8_t(u >> 8), base[at + 3] = uint8_t(u);
  };
  put(cmd + 0x0, x * speed); // round 12: g_drive.cx already carries the sideways flip (the mover's +x is the bot's LEFT)
  put(cmd + 0x4, LoadFloat(base, mover + kMoverVel + 4)); // Rare keeps the current y
  put(cmd + 0x8, y * speed);
  put(cmd + 0xC, rate);
  put(cmd + 0x10, rate * 0.3f); // Rare writes rate x 0.3 here
  PPCContext saved = ctx;
  rex::CallFrame frame(saved);
  frame.ctx.r3.u64 = mover;
  frame.ctx.r4.u64 = cmd;
  sub_825DEB40(frame.ctx, base);
}

// sub_825C9180(bot, r4): the spiderbot's choice of target, every 3 s. Rare's (the driven bot's AI tick is skipped, so
// it is not asked while he drives it).
REX_HOOK_RAW(sub_825C9180)
{
  __imp__sub_825C9180(ctx, base);
}

// sub_825C7CC8(bot, ...): a spiderbot's death (the explosion when it is awake). Rare's; logged.
REX_HOOK_RAW(sub_825C7CC8)
{
  const uint32_t bot = ctx.r3.u32;
  const bool log = REXCVAR_GET(pdz_counterop_spider_log) && Ptr(bot) && Load32(base, bot) == kSpiderVtable;
  const uint8_t exploded_before = log ? base[bot + kExploded] : 0;
  const uint32_t agent = PdzCounterOpAgent();
  const float agent_health = Ptr(agent) && Ptr(Load32(base, agent + 6560)) ? LoadFloat(base, Load32(base, agent + 6560) + 980) : -1.0f;
  const float d = log ? Dist(base, bot, agent) : -1.0f;
  __imp__sub_825C7CC8(ctx, base);
  if (log && ++g_deaths <= 60)
  {
    const float after = Ptr(agent) && Ptr(Load32(base, agent + 6560)) ? LoadFloat(base, Load32(base, agent + 6560) + 980) : -1.0f;
    REXLOG_INFO("pdz_counterop: spider: death #{} of spiderbot {:08X} (detonator kind {}, exploded {} -> {}), {:.1f} m from the "
                "agent; the agent's health {:.1f} -> {:.1f} across Rare's call (called from {:08X})",
                g_deaths, bot, int(base[bot + kAwake]), int(exploded_before), int(base[bot + kExploded]), d,
                agent_health, after, uint32_t(ctx.lr));
  }
}

// counter_op_triggers.cpp, the level script's Setup hook (8214DB68), before Rare's call.
void PdzCounterOpSpiderPreSetup(uint8_t *base)
{
  if (!REXCVAR_GET(pdz_counterop_test_datacore_checkpoint) || !PdzCounterOpOn())
  {
    return;
  }
  std::string raw;
  if (pdz::pace::SpLevelName(base, &raw) != "datacore")
  {
    return;
  }
  const uint8_t before = base[kCheckpointByte];
  base[kCheckpointByte] = uint8_t(before | 0x20);
  REXLOG_INFO("pdz_counterop: spider: TEST TOOL: Datacore starts from Rare's mid-level checkpoint (byte 82E426F8 {:02X} "
              "-> {:02X} before the level script's Setup)",
              before, base[kCheckpointByte]);
}

// counter_op.cpp's tick (a Counter-Op level runs).
// Round 10 (item 4): Datacore's shaft lift. Rare's chain (round 9, helper D): the lift ([script+0x2768], parked at
// the top, state +0x464 == 0) is called by the script's Update (822FD6BC..822FD7AC) once flag 0x53 is up (state
// word [script+0x3E0]+8, bit 0x00080000): it then sets word +0x20 bit 0x00400000 and a timer, and some seconds later
// toggles bit 0x00100000, whose flag handler (82302964) makes the lift's own call (vtable +0x94). Flag 0x53 is set in
// one place, the message handler's case for 8/51..8/67 (822FE5FC: a plain OR of bit arg + 0x1B), by message 8/56:
// the 'destroyed' trigger of scientist index 3. In the hand test of round 9 (log 007) the escort objective was done
// (two scientists counted at the door) with only 8/53 received; scientist 3 had passed his wait (round 9's Fix A
// answered 1) 21 s earlier and 8/56 had not come when the game was closed 5 s later. Whether Rare's own call was
// still on its way is not known. So: once the escort objective is done (record 2 == 2, nobody still needed), and
// flag 0x53 is still down pdz_counterop_datacore_lift_s later with the lift still parked at the top, the flag is set
// here exactly as the message sets it. Everything after that (the timer, the doors, the ride) is Rare's.
namespace
{
  void DatacoreLiftTick(uint8_t *base)
  {
    static uint32_t seen_script = 0;
    static int64_t done_at = 0, last_line = 0;
    static int lines = 0;
    static int stage = 0; // 0 waiting for the escort, 1 escort done: counting, 2 Rare called it, 3 we called it, 4 down
    const double wait_s = REXCVAR_GET(pdz_counterop_datacore_lift_s);
    const uint32_t level = Load32(base, kGame + 0x1C);
    const uint32_t target = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, target, 0x74 + 4) ? Load32(base, target + 0x74) : 0;
    if (!Ptr(script) || Load32(base, script) != 0x820A1610u || !PdzCounterOpRunning())
    {
      seen_script = 0;
      return;
    }
    if (script != seen_script)
    {
      seen_script = script;
      done_at = last_line = 0;
      stage = 0;
      lines = 0;
    }
    const uint32_t words = Load32(base, script + 0x3E0), records = Load32(base, script + 0x3E4);
    const uint32_t lift = Load32(base, script + 0x2768);
    if (!Ptr(words) || !Ptr(records) || !Ptr(lift) || Load32(base, lift) != 0x820C6DC0u)
    {
      return;
    }
    const int64_t now = NowMs();
    const uint32_t w8 = Load32(base, words + 8), w20 = Load32(base, words + 0x20);
    const int32_t needed = int32_t(Load32(base, script + 0x288C));
    const uint32_t escort = Load32(base, records + 0x34 * 2);
    const int32_t state = int32_t(Load32(base, lift + 0x464));
    const bool flag = (w8 & 0x00080000u) != 0;
    if (stage == 0)
    {
      if (flag)
      {
        stage = 2;
        REXLOG_INFO("pdz_counterop: datacore: LIFT: Rare's own call: the last scientist's 'destroyed' message (8/56) "
                    "has set flag 0x53 (escort objective state {}, scientists still needed {}); the script's timer "
                    "runs, then the lift comes (lift state {})",
                    escort, needed, state);
      }
      else if (escort == 2 && needed <= 0)
      {
        stage = 1;
        done_at = now;
        REXLOG_INFO("pdz_counterop: datacore: LIFT: the escort objective is done (record 2 = 2, scientists still "
                    "needed {}), flag 0x53 (the lift call) is still down, the lift is parked (state {}, position "
                    "{:.2f}): Rare's own call is waited for {:.0f} s",
                    needed, state, LoadFloat(base, lift + 0x360), wait_s);
      }
      return;
    }
    if (stage == 1)
    {
      if (flag)
      {
        stage = 2;
        REXLOG_INFO("pdz_counterop: datacore: LIFT: Rare's own call came {:.1f} s after the escort objective (message "
                    "8/56 set flag 0x53): nothing done here",
                    double(now - done_at) / 1000.0);
      }
      else if (state != 0)
      {
        stage = 2;
        REXLOG_INFO("pdz_counterop: datacore: LIFT: the lift is already under way (state {}): nothing done here", state);
      }
      else if (wait_s >= 0.0 && double(now - done_at) >= wait_s * 1000.0)
      {
        const uint32_t v = w8 | 0x00080000u;
        base[words + 8] = uint8_t(v >> 24), base[words + 9] = uint8_t(v >> 16), base[words + 10] = uint8_t(v >> 8),
                   base[words + 11] = uint8_t(v);
        stage = 3;
        REXLOG_INFO("pdz_counterop: datacore: LIFT CALLED BY COUNTER-OP: the escort objective has been done for {:.1f} "
                    "s and the last scientist's 'destroyed' message (8/56) has not come (W.8 {:08X}, W.20 {:08X}; "
                    "the lift parked at the top, state 0): flag 0x53 is set as that message sets it (W.8 -> {:08X}); "
                    "the script's own timer and lift call follow",
                    double(now - done_at) / 1000.0, w8, w20, v);
      }
      return;
    }
    if (stage == 2 || stage == 3)
    {
      if (state == 1)
      {
        REXLOG_INFO("pdz_counterop: datacore: LIFT: it is down (state 1, position {:.2f}), {}", LoadFloat(base, lift + 0x360),
                    stage == 3 ? "called by Counter-Op's flag" : "called by Rare's own message");
        stage = 4;
      }
      else if (now - last_line >= 3000 && lines < 20)
      {
        last_line = now;
        ++lines;
        REXLOG_INFO("pdz_counterop: datacore: LIFT: on its way: state {} (5 doors, 3 moving down, 6 arriving), position "
                    "{:.2f}; W.20 {:08X} (0x00400000 timer started, 0x00100000 / 0x00040000 the call made)",
                    state, LoadFloat(base, lift + 0x360), w20);
      }
    }
  }
} // namespace

// Sun 22 Nov (round 17): the level message tool on its own, so single player can use it too (counter_op.cpp's
// 8222F740 hook calls it when no Counter-Op game runs).
void PdzCounterOpTestLevelMessage(PPCContext &ctx, uint8_t *base, uint32_t agent)
{
  const std::string message = REXCVAR_GET(pdz_counterop_test_level_message);
  if (message != g_last_message)
  {
    g_last_message = message;
    int type = 0, arg = 0, flag = 0;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    uint32_t source = agent;
    std::string source_text = "the agent as source and activator";
    const uint32_t owner = Load32(base, kGame + 0x1C);
    const uint32_t manager = pdz::guestmem::Ok(base, owner, 0x2D8 + 4) ? Load32(base, owner + 0x2D8) : 0;
    if (!g_message_scratch)
    {
      g_message_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
    }
    const int fields = std::sscanf(message.c_str(), "%d,%d,%d,%f,%f,%f", &type, &arg, &flag, &sx, &sy, &sz);
    if (fields >= 3 && Ptr(manager) && g_message_scratch && Ptr(agent))
    {
      if (fields == 6)
      {
        // Round 8: the source is the world object nearest that point (a hack panel, a terminal: the script compares
        // the source with its own prop); the agent stays the activator.
        const uint32_t world = Load32(base, kGame + 24);
        const uint32_t end = Ptr(world) ? Load32(base, world + 43216) : 0;
        uint32_t node = Ptr(world) ? Load32(base, world + 43212) : 0;
        float best = 1e18f;
        for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
        {
          const uint32_t o = Load32(base, node + 4);
          if (!Ptr(o) || o == agent)
          {
            continue;
          }
          const float dx = LoadFloat(base, o + kPos) - sx, dy = LoadFloat(base, o + kPos + 4) - sy,
                      dz = LoadFloat(base, o + kPos + 8) - sz;
          const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
          if (d < best)
          {
            best = d;
            source = o;
          }
        }
        source_text = fmt::format("source = the world object nearest ({:.0f}, {:.0f}, {:.0f}): {:08X} (vtable {:08X}, "
                                  "{:.2f} m off), the agent as activator",
                                  sx, sy, sz, source, source != agent ? Load32(base, source) : 0, best / 100.0f);
      }
      std::memset(base + g_message_scratch, 0, 16);
      base[g_message_scratch + 1] = uint8_t(type);
      base[g_message_scratch + 2] = uint8_t(arg);
      base[g_message_scratch + 3] = uint8_t(flag);
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = manager;
      frame.ctx.r4.u64 = source;
      frame.ctx.r5.u64 = agent;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = g_message_scratch;
      sub_8214E520(frame.ctx, base);
      REXLOG_INFO("pdz_counterop: spider: TEST TOOL: level message type {} arg {} flag {} sent (Rare's 8214E520, {})",
                  type, arg, flag, source_text);
    }
  }
}

void PdzCounterOpSpiderTick(PPCContext &ctx, uint8_t *base, uint32_t agent)
{
  DatacoreLiftTick(base);
  // TESTING TOOL: a level message.
  PdzCounterOpTestLevelMessage(ctx, base, agent);
  {
    static int32_t last_objective = -1;
    const int32_t objective = REXCVAR_GET(pdz_counterop_test_objective_done);
    if (objective != last_objective)
    {
      last_objective = objective;
      const uint32_t level = Load32(base, kGame + 0x1C);
      const uint32_t target = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
      const uint32_t script = pdz::guestmem::Ok(base, target, 0x74 + 4) ? Load32(base, target + 0x74) : 0;
      const uint32_t records = Ptr(script) ? Load32(base, script + 0x3F0) : 0;
      if (objective >= 0 && objective % 100 < 8 && objective < 200 && Ptr(records) &&
          Load32(base, script) == 0x8209D57Cu)
      {
        // N = done (2); 100 + N = active (1), as when the level has got that far.
        const uint32_t at = records + 0x34 * uint32_t(objective % 100);
        const uint32_t was = Load32(base, at);
        const uint8_t to = objective >= 100 ? 1 : 2;
        base[at] = 0, base[at + 1] = 0, base[at + 2] = 0, base[at + 3] = to;
        REXLOG_INFO("pdz_counterop: TEST TOOL: Nightclub objective record {} state {} -> {} ({})", objective % 100, was,
                    to, to == 1 ? "active" : "done");
      }
    }
  }
  if (REXCVAR_GET(pdz_counterop_test_script_state))
  {
    static int64_t last_at = 0;
    static uint32_t last[5][16] = {};
    static uint32_t changes[5][16] = {}; // a word that keeps changing (a blinking timer bit) is left out after 6
    static uint32_t last_script = 0;
    const int64_t t = NowMs();
    const uint32_t level = Load32(base, kGame + 0x1C);
    const uint32_t target = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, target, 0x74 + 4) ? Load32(base, target + 0x74) : 0;
    if (Ptr(script) && t - last_at >= 500)
    {
      std::string text;
      for (uint32_t k = 0; k < 5; ++k)
      {
        const uint32_t block = Load32(base, script + 0x3E0 + 4 * k);
        if (!Ptr(block))
        {
          continue;
        }
        for (uint32_t i = 0; i < 16; ++i)
        {
          const uint32_t v = Load32(base, block + 4 * i);
          if (v != last[k][i] || script != last_script)
          {
            if (script != last_script)
            {
              changes[k][i] = 0;
            }
            else if (++changes[k][i] <= 6)
            {
              text += fmt::format(" [+{:X}]+{:X}: {:08X} -> {:08X};", 0x3E0 + 4 * k, 4 * i, last[k][i], v);
            }
            last[k][i] = v;
          }
        }
      }
      if (script != last_script)
      {
        last_script = script;
        text = fmt::format(" a new script {:08X} (vtable {:08X})", script, Load32(base, script));
      }
      if (!text.empty())
      {
        last_at = t;
        REXLOG_INFO("pdz_counterop: TEST TOOL: script state:{} (agent at ({:.0f}, {:.0f}, {:.0f}))", text,
                    Ptr(agent) ? LoadFloat(base, agent + kPos) : 0.0f, Ptr(agent) ? LoadFloat(base, agent + kPos + 4) : 0.0f,
                    Ptr(agent) ? LoadFloat(base, agent + kPos + 8) : 0.0f);
      }
    }
  }
  if (REXCVAR_GET(pdz_counterop_test_datacore_lift_log))
  {
    static int64_t last_at = 0;
    static std::string last_text;
    const int64_t t = NowMs();
    const uint32_t level = Load32(base, kGame + 0x1C);
    const uint32_t target = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, target, 0x74 + 4) ? Load32(base, target + 0x74) : 0;
    if (pdz::guestmem::Ok(base, script, 4) && Load32(base, script) == 0x820A1610u && t - last_at >= 2000)
    {
      last_at = t;
      const uint32_t records = Load32(base, script + 0x3E4), words = Load32(base, script + 0x3E0);
      std::string text = fmt::format("scientists still needed {} / deaths allowed {}; objectives", int32_t(Load32(base, script + 0x288C)),
                                     int32_t(Load32(base, script + 0x2890)));
      for (uint32_t n = 0; n < 6 && Ptr(records); ++n)
      {
        text += fmt::format(" {}", Load32(base, records + 0x34 * n));
      }
      if (Ptr(words))
      {
        text += fmt::format("; W.8 {:08X} W.C {:08X} W.14 {:08X} W.1C {:08X} W.20 {:08X} W.24 {:08X}", Load32(base, words + 8),
                            Load32(base, words + 0xC), Load32(base, words + 0x14), Load32(base, words + 0x1C),
                            Load32(base, words + 0x20), Load32(base, words + 0x24));
      }
      for (const uint32_t slot : {0x2768u, 0x276Cu})
      {
        const uint32_t prop = Load32(base, script + slot);
        if (Ptr(prop))
        {
          text += fmt::format("; prop +{:X} {:08X} (vtable {:08X}) at ({:.0f}, {:.0f}, {:.0f}) state {} f360 {:.1f}", slot, prop,
                              Load32(base, prop), LoadFloat(base, prop + kPos), LoadFloat(base, prop + kPos + 4),
                              LoadFloat(base, prop + kPos + 8), int32_t(Load32(base, prop + 0x464)), LoadFloat(base, prop + 0x360));
        }
        else
        {
          text += fmt::format("; prop +{:X} none", slot);
        }
      }
      if (text != last_text)
      {
        last_text = text;
        REXLOG_INFO("pdz_counterop: TEST TOOL: datacore lift: {} (agent at ({:.0f}, {:.0f}, {:.0f}))", text,
                    Ptr(agent) ? LoadFloat(base, agent + kPos) : 0.0f, Ptr(agent) ? LoadFloat(base, agent + kPos + 4) : 0.0f,
                    Ptr(agent) ? LoadFloat(base, agent + kPos + 8) : 0.0f);
      }
    }
  }
  if (!REXCVAR_GET(pdz_counterop_spider_log))
  {
    return;
  }
  const int64_t now = NowMs();
  if (now - g_last_log < 3000)
  {
    return;
  }
  g_last_log = now;
  uint32_t ticking = 0, awake = 0, nearest = 0;
  float nearest_m = 1e9f;
  for (auto it = g_bots.begin(); it != g_bots.end();)
  {
    const uint32_t bot = it->first;
    if (now - it->second > 1500 || Load32(base, bot) != kSpiderVtable)
    {
      it = g_bots.erase(it);
      continue;
    }
    ++ticking;
    awake += base[bot + kAwake] ? 1 : 0;
    const float d = Dist(base, bot, agent);
    if (d >= 0.0f && d < nearest_m)
    {
      nearest_m = d;
      nearest = bot;
    }
    ++it;
  }
  if (!ticking)
  {
    return;
  }
  REXLOG_INFO("pdz_counterop: spider: {} spiderbot(s) ticking ({} of the detonator kind); the nearest to the agent {:08X} at {:.1f} m "
              "(masks {:04X} / {:04X} / {:04X}, detonator kind {}, dying {}, at ({:.0f}, {:.0f}, {:.0f})): its AI target {}, "
              "its human target {}",
              ticking, awake, nearest, nearest_m, Load16(base, nearest + kTeam), Load16(base, nearest + kAllies),
              Load16(base, nearest + kEnemies), int(base[nearest + kAwake]), int(base[nearest + kDying]),
              LoadFloat(base, nearest + kPos), LoadFloat(base, nearest + kPos + 4), LoadFloat(base, nearest + kPos + 8),
              Who(base, Load32(base, nearest + kAiTarget)), Who(base, Load32(base, nearest + kHumanTarget)));
}

// ---- The drive (round 9) -----------------------------------------------------------------------------------------
// Player 2 drives one of Rare's own spiderbots while he waits for a guard in Datacore's spiderbot section.
//   Movement: Rare's mover. The bot's movement controller asks for a command every frame (8219AE50); for the driven
//     bot that command is his sticks (local side / forward speed at Rare's 400 units a second, a turn rate). Rare's
//     mover then does the rest: the ray along the bot's own down, the snap to the surface, the up axis eased to the
//     surface's normal, gravity along the bot's own down, the wall sweep, and the walk / turn animation blend from
//     the measured motion. A bot on a wall or a ceiling stays there and walks on it.
//   Ours (Rare has no such rule): a bot pushed against a wall steps up onto it (its frame is pitched up a quarter
//     turn; Rare's ray then finds the wall). Rare's own bots change surface at corners by their path links' jumps.
//   Attack: Rare's. An ordinary bot: Rare's jump task (the pounce: 20 damage to every character its leap sweeps).
//     A yellow-eyed one (kind flag +0xDD4): Rare's death call, which for that kind is the blast.
//   Its brain is off while he drives (the AI tick is skipped; the task stack runs only for the pounce); while it
//     falls or dies Rare's tick runs.
bool PdzCounterOpSpiderDriving() { return g_drive.bot != 0; }
// Round 15 (Thu 19 Nov night): the bot player 2 drives now, whatever the switches (0 = none): counter_op_pvp.cpp
// treats its pounce on the agent as his hit (pdz_counterop_spiderbot_kill).
uint32_t PdzCounterOpSpiderDrivenBot() { return g_drive.bot; }
// Round 11. The bot player 2 drives, 0 = none (or the rule is off).
uint32_t PdzCounterOpSpiderBot() { return REXCVAR_GET(pdz_counterop_spiderbot_only_agent) ? g_drive.bot : 0; }
// The driven bot, the yellow-eyed bot he drove until 5 s ago, or an explosive Rare made for it in the last 8 s.
// Cheap when he drives nothing (a few compares, no clock read).
bool PdzCounterOpSpiderIsHis(uint32_t object)
{
  if (!object || !REXCVAR_GET(pdz_counterop_spiderbot_only_agent))
  {
    return false;
  }
  if (object == g_drive.bot)
  {
    return true;
  }
  if (object == g_last_bot)
  {
    if (NowMs() < g_last_bot_until)
    {
      return true;
    }
    g_last_bot = 0;
  }
  for (int i = 0; i < 4; ++i)
  {
    if (g_his_blast[i] == object)
    {
      if (NowMs() - g_his_blast_at[i] < 8000)
      {
        return true;
      }
      g_his_blast[i] = 0;
    }
  }
  return false;
}
// Did his pounce touch this character in the last 10 s?
bool PdzCounterOpSpiderPouncedOn(uint32_t who)
{
  for (const Pounced &p : g_pounced)
  {
    if (p.who && p.who == who)
    {
      return NowMs() - p.at < 10000;
    }
  }
  return false;
}
// True when `viewer` must not see or find `target`: the target is the bot he drives, the viewer would take it for an
// enemy (Rare's friend test, asked the way the sight loop asks it at 8258072C: viewer, seen object) and his pounce has
// not touched that viewer in the last 10 s. The bot's own side (Rare's other bots) is not affected.
bool PdzCounterOpSpiderHiddenFrom(PPCContext &ctx, uint8_t *base, uint32_t viewer, uint32_t target)
{
  if (!g_drive.bot || target != g_drive.bot || !REXCVAR_GET(pdz_counterop_spiderbot_only_agent) || !Ptr(viewer) ||
      viewer == target || PdzCounterOpSpiderPouncedOn(viewer))
  {
    return false;
  }
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = viewer;
  frame.ctx.r4.u64 = target;
  sub_8251EBD0(frame.ctx, base);
  return (frame.ctx.r3.u32 & 0xFF) == 0;
}

// An explosive gets its owner (825C7E14 in the bot's death: the type 0x17 explosive of a yellow-eyed bot).
REX_HOOK_RAW(sub_825BA2B8)
{
  const uint32_t explosive = ctx.r3.u32, owner = ctx.r4.u32;
  if ((g_drive.bot || g_last_bot) && Ptr(explosive) && PdzCounterOpSpiderIsHis(owner))
  {
    int slot = 0;
    for (int i = 1; i < 4; ++i)
    {
      slot = g_his_blast_at[i] < g_his_blast_at[slot] ? i : slot;
    }
    g_his_blast[slot] = explosive;
    g_his_blast_at[slot] = NowMs();
    REXLOG_INFO("pdz_counterop: spider: the explosive {:08X} of player 2's bot {:08X} is his: its blast hurts only the "
                "agent and is not heard",
                explosive, owner);
  }
  __imp__sub_825BA2B8(ctx, base);
}
// The blast handlers (vtable +0xC4; called by 825214E8 at 825216D4 and by the delayed-blast list at 82521400):
// victim r3, the explosive r4, its owner r5. A blast of his bot does nothing to anyone but the agent: no damage, no
// knock, no 'damaged' stimulus. (What they return is not read at 82521410; at 825216E0 UNSURE: 0 is given.)
namespace
{
  bool HisBlast(uint32_t victim, uint32_t explosive, uint32_t attacker)
  {
    if (!g_drive.bot && !g_last_bot && !g_his_blast[0] && !g_his_blast[1] && !g_his_blast[2] && !g_his_blast[3])
    {
      return false;
    }
    if (victim == PdzCounterOpAgent() || !(PdzCounterOpSpiderIsHis(explosive) || PdzCounterOpSpiderIsHis(attacker)))
    {
      return false;
    }
    if (++g_refused <= 20 || g_refused % 100 == 0)
    {
      REXLOG_INFO("pdz_counterop: spider: the blast of player 2's bot does nothing to {:08X} (explosive {:08X}, owner "
                  "{:08X}; #{}): only the agent can be hurt by it",
                  victim, explosive, attacker, g_refused);
    }
    return true;
  }
} // namespace
REX_HOOK_RAW(sub_8254A6E8)
{
  if (HisBlast(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32))
  {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8254A6E8(ctx, base);
}
REX_HOOK_RAW(sub_825C88E0)
{
  if (HisBlast(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32))
  {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_825C88E0(ctx, base);
}
bool PdzCounterOpSpiderView(float eye[3], float look[3])
{
  (void)eye;
  (void)look;
  return false; // round 7's two-point view: replaced by PdzCounterOpSpiderCamera
}
// counter_op.cpp's spectate write: the camera's place, the point it looks at, and its up vector (roll = false: the
// world's up stays).
bool PdzCounterOpSpiderCamera(float cam[3], float look[3], float up[3], bool &roll)
{
  if (!g_drive.bot || !g_drive.view)
  {
    return false;
  }
  std::memcpy(cam, g_drive.cam, sizeof(g_drive.cam));
  std::memcpy(look, g_drive.look, sizeof(g_drive.look));
  std::memcpy(up, g_drive.up, sizeof(g_drive.up));
  // Round 13 (b): with the flat lock his view keeps the world's up (both tilt writes in counter_op.cpp need `roll`).
  roll = REXCVAR_GET(pdz_counterop_spiderbot_cam_roll) && !REXCVAR_GET(pdz_counterop_spiderbot_flat);
  return true;
}
void PdzCounterOpSpiderReset()
{
  const uint32_t scratch = g_drive.scratch;
  g_drive = Drive{};
  g_drive.scratch = scratch;
  g_last_agent = 0; // Sun 22 Nov night (audit): the pounce-hit hook's agent never outlives the game
  g_bots.clear();
  g_flags.clear();
  g_deaths = 0;
  for (Pounced &p : g_pounced)
  {
    p = Pounced{};
  }
  std::memset(g_his_blast, 0, sizeof(g_his_blast));
  std::memset(g_his_blast_at, 0, sizeof(g_his_blast_at));
  g_last_bot = 0;
  g_cp = ClosePounce{}; // round 16 (item 3)
  g_cp_lines = 0;
  counter_op_pad::BlockY(-1);
}

// Round 13 (b), the flat lock inside Rare's own mover. player_move.cpp's hook of Rare's surface probe 8269CEF0
// (&position in / out r3, object r4, r6 &old position) calls this first; true = the probe has been run here.
// Rare's spiderbot mover (825DD768) moves the bot, then asks this probe (through 8269C580) what is under its feet:
// a ray along the bot's OWN up axis; the hit's normal (mover +0x88) is what the mover then turns the bot's up axis
// to (825DDC24..), which is how a bot follows a floor that bends up into a wall. For the bot player 2 drives, with
// the lock on: when the bot stood on a floor (the last hit's normal within pdz_counterop_spiderbot_flat_deg of the
// world's up) and this move's hit is steeper than that, the move is refused the way a wall refuses it: the position
// goes back to the old one, the mover keeps its last hit record, and the probe still answers 'on the ground'. So
// the bot stops at the foot of the wall and its up axis never starts to turn. Every other object: untouched (one
// compare). No system call; the clock is read only when a move is refused.
bool PdzCounterOpSpiderFlatProbe(PPCContext &ctx, uint8_t *base)
{
  const uint32_t bot = ctx.r4.u32;
  // (only while his sticks own the movement: not during Rare's jump (the pounce) or fall)
  if (!g_drive.bot || bot != g_drive.bot || g_own_probe || !g_drive.manual || !REXCVAR_GET(pdz_counterop_spiderbot_flat))
  {
    return false;
  }
  const uint32_t pos = ctx.r3.u32, old = ctx.r6.u32;
  const uint32_t mover = Load32(base, bot) == kSpiderVtable ? Load32(base, bot + kMover) : 0;
  if (!Ptr(pos) || !Ptr(old) || !Ptr(mover))
  {
    return false;
  }
  uint8_t rec[0x60], old_pos[12];
  std::memcpy(rec, base + mover + 0x64, sizeof(rec));
  std::memcpy(old_pos, base + old, sizeof(old_pos));
  const uint32_t was_flags = Load32(base, mover + kMoverRecFlags);
  float was_n[3] = {LoadFloat(base, mover + kMoverNormal), LoadFloat(base, mover + kMoverNormal + 4),
                    LoadFloat(base, mover + kMoverNormal + 8)};
  __imp__sub_8269CEF0(ctx, base);
  if (!(ctx.r3.u32 & 0xFF) || !Load32(base, mover + kMoverRecFlags) || !was_flags)
  {
    return true;
  }
  float n[3] = {LoadFloat(base, mover + kMoverNormal), LoadFloat(base, mover + kMoverNormal + 4),
                LoadFloat(base, mover + kMoverNormal + 8)};
  const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  const float wl = std::sqrt(was_n[0] * was_n[0] + was_n[1] * was_n[1] + was_n[2] * was_n[2]);
  if (!(nl > 1e-4f) || !(wl > 1e-4f) || !std::isfinite(nl) || !std::isfinite(wl))
  {
    return true;
  }
  const float *up = g_drive.bot_up;
  const float ny = (n[0] * up[0] + n[1] * up[1] + n[2] * up[2] < 0.0f ? -n[1] : n[1]) / nl;
  const float was_ny = (was_n[0] * up[0] + was_n[1] * up[1] + was_n[2] * up[2] < 0.0f ? -was_n[1] : was_n[1]) / wl;
  const float lim = FlatCos();
  if (ny >= lim || was_ny < lim)
  {
    return true; // a floor, a ramp; or it did not stand on a floor before (the drive tick brings it down)
  }
  const int64_t now = NowMs();
  // Round 13b (t), the step rule: a short steep face with a floor just beyond it (a kerb's chamfer, the lip at the
  // foot of a ramp) is not a wall: the drive tick has looked (GroundAhead) and found the floor going on.
  if (now < g_drive.lip_ok_until)
  {
    ++g_drive.lip_passed;
    return true;
  }
  g_drive.probe_refused_at = now;
  const float px = LoadFloat(base, pos), py = LoadFloat(base, pos + 4), pz = LoadFloat(base, pos + 8);
  std::memcpy(base + mover + 0x64, rec, sizeof(rec));
  std::memcpy(base + pos, old_pos, sizeof(old_pos));
  ctx.r3.u64 = 1;
  ++g_drive.probe_refused;
  if (now - g_drive.probe_said >= 1000 && g_drive.probe_lines < 40)
  {
    g_drive.probe_said = now;
    ++g_drive.probe_lines;
    REXLOG_INFO("pdz_counterop: spider: flat lock: Rare's mover is REFUSED a surface for {:08X}: the hit under the "
                "point ({:.0f}, {:.0f}, {:.0f}) has the normal ({:.2f}, {:.2f}, {:.2f}) (up part {:.2f}, a floor "
                "needs {:.2f}); it stays at ({:.0f}, {:.0f}, {:.0f}) on the floor (normal up part {:.2f}), its up "
                "axis ({:.2f}, {:.2f}, {:.2f}); {} move(s) refused in this drive",
                bot, px, py, pz, n[0] / nl, n[1] / nl, n[2] / nl, ny, lim, LoadFloat(base, pos), LoadFloat(base, pos + 4),
                LoadFloat(base, pos + 8), was_ny, up[0], up[1], up[2], g_drive.probe_refused);
  }
  return true;
}

namespace
{
  float AgentHealth(const uint8_t *base, uint32_t agent)
  {
    // Sun 22 Nov night (audit): both reads tested (mapped now)
    const uint32_t part = pdz::guestmem::Ok(base, agent, 6564) ? Load32(base, agent + 6560) : 0;
    return pdz::guestmem::Ok(base, part, 984) ? LoadFloat(base, part + 980) : -1.0f;
  }
  void StoreFloatBe(uint8_t *base, uint32_t a, float f)
  {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    base[a] = uint8_t(u >> 24), base[a + 1] = uint8_t(u >> 16), base[a + 2] = uint8_t(u >> 8), base[a + 3] = uint8_t(u);
  }
  struct Frame3
  {
    float side[3], up[3], fwd[3], pos[3];
  };
  // The bot's own matrix (Rare's 82518580): side, up, forward, position.
  Frame3 BotFrame(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t scratch)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = bot;
    frame.ctx.r4.u64 = scratch;
    sub_82518580(frame.ctx, base);
    Frame3 f;
    for (uint32_t i = 0; i < 3; ++i)
    {
      f.side[i] = LoadFloat(base, scratch + 4 * i);
      f.up[i] = LoadFloat(base, scratch + 0x10 + 4 * i);
      f.fwd[i] = LoadFloat(base, scratch + 0x20 + 4 * i);
      f.pos[i] = LoadFloat(base, scratch + 0x30 + 4 * i);
    }
    return f;
  }
  bool Falling(const uint8_t *base, uint32_t bot)
  {
    return base[bot + kDetached] || Load32(base, bot + kFallTask) || base[bot + kDying];
  }
  // Hand the bot back to Rare: the controller's own stop, "moving" off, its next choice of target at once.
  void Release(PPCContext &ctx, uint8_t *base, uint32_t bot)
  {
    if (!Ptr(bot) || Load32(base, bot) != kSpiderVtable || base[bot + kDying] || base[bot + kExploded])
    {
      return;
    }
    const uint32_t ctl = Load32(base, bot + kCtl), mover = Load32(base, bot + kMover);
    if (Ptr(ctl))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = ctl;
      sub_821925C0(frame.ctx, base);
    }
    if (Ptr(mover))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = mover;
      frame.ctx.r4.u64 = 0;
      sub_825DD3A0(frame.ctx, base);
    }
    StoreFloatBe(base, bot + kRetargetTimer, 0.0f);
  }
  void EndDrive(PPCContext &ctx, uint8_t *base, uint32_t agent, const char *why)
  {
    REXLOG_INFO("pdz_counterop: spider: player 2's spiderbot {:08X} is over ({}) after {:.1f} s ({} pounce(s), {} "
                "step(s) onto a wall); the agent's health {:.1f} -> {:.1f}; the next one in {:.0f} s if one is in the "
                "section",
                g_drive.bot, why, double(NowMs() - g_drive.since) / 1000.0, g_drive.pounces, g_drive.climbs,
                g_drive.start_agent_health, AgentHealth(base, agent), REXCVAR_GET(pdz_counterop_spiderbot_wait_s));
    Release(ctx, base, g_drive.bot);
    g_cp = ClosePounce{};                                  // round 16 (item 3)
    PdzCounterOpHudRestoreWeapons(ctx, base, -1, why); // round 16 (item 2): his weapon HUD back at once
    if (base[g_drive.bot + kDetonator])
    {
      g_last_bot = g_drive.bot; // its blast (set off by him or by whoever destroyed it) comes after the drive
      g_last_bot_until = NowMs() + 5000;
    }
    const uint32_t driven = g_drive.driven, scratch = g_drive.scratch;
    g_drive = Drive{};
    g_drive.driven = driven;
    g_drive.scratch = scratch;
    g_drive.ended = NowMs();
  }
  // Round 15 (Thu 19 Nov night, play log 006 21:42:24 on: every press "pounce #0 ... not started", no reason). The only
  // refusal is in Rare's push 82180508: the top task of the bot's channel-0 list answers "no" to a new jump through its
  // vtable +0xC. Rare's jump (vtable 82091B7C, kind 39) and fall / get-up (820913F0, kind 40) refuse a jump; the
  // knock-down (82091490, kind 8) refuses everything. Seen: pounce #21 was cut short when the bot came off its surface
  // and Rare pushed a fall over the leap; afterwards neither handle (+0x474 the jump, +0xDD0 the fall) pointed at a
  // task, so a task none of ours could see stayed on the list (the AI tick runs the list only while +0x474 is set).
  // The list (bot + 0xA8C): first entry Load32(bot + 0xD70), the end Load32(bot + 0xD74); an entry: +0 the task,
  // +0x14 the next. A task: +0 vtable, +4 its handle list, +0xC state (1 running, 3 done), +0x10 owner, jump phase
  // +0x68, fall timer +0x30, fall byte +0x50.
  constexpr uint32_t kTaskListFirst = 0xD70, kTaskListEnd = 0xD74;
  constexpr uint32_t kJumpTaskVtable = 0x82091B7C, kFallTaskVtable = 0x820913F0, kKnockTaskVtable = 0x82091490;
  std::string TaskListText(const uint8_t *base, uint32_t bot)
  {
    std::string s;
    uint32_t node = Load32(base, bot + kTaskListFirst);
    const uint32_t end = Load32(base, bot + kTaskListEnd);
    for (int i = 0; i < 8 && Ptr(node) && node != end; ++i, node = Load32(base, node + 0x14))
    {
      const uint32_t task = Load32(base, node);
      if (!Ptr(task))
      {
        s += fmt::format(" [entry {:08X}: task {:08X}]", node, task);
        continue;
      }
      const uint32_t vt = Load32(base, task);
      s += fmt::format(" [{:08X} {} (vtable {:08X}) state {} owner {:08X} handles {:08X} phase {} fall timer {:.2f} byte {}]", task,
                       vt == kJumpTaskVtable ? "jump" : vt == kFallTaskVtable ? "fall/get-up" : vt == kKnockTaskVtable ? "knock-down" : "other",
                       vt, Load32(base, task + 0xC), Load32(base, task + 0x10), Load32(base, task + 4), Load32(base, task + 0x68),
                       LoadFloat(base, task + 0x30), int(base[task + 0x50]));
    }
    return s.empty() ? std::string(" (empty)") : s;
  }
  // A jump or fall task left on the list that neither handle holds, while the bot stands under his command: ended the
  // way Rare's push ends an accepted top (vtable +0x18 stop if running, 8217E990, then 82181430 removes and destroys it,
  // clearing every handle to it). Never a knock-down or anything else. True if one was ended.
  bool EndStaleTask(PPCContext &ctx, uint8_t *base, uint32_t bot, std::string &what)
  {
    if (!REXCVAR_GET(pdz_counterop_spiderbot_unstick))
    {
      return false;
    }
    uint32_t node = Load32(base, bot + kTaskListFirst);
    const uint32_t end = Load32(base, bot + kTaskListEnd);
    for (int i = 0; i < 8 && Ptr(node) && node != end; ++i, node = Load32(base, node + 0x14))
    {
      const uint32_t task = Load32(base, node);
      if (!Ptr(task) || task == Load32(base, bot + kJumpTask) || task == Load32(base, bot + kFallTask))
      {
        continue;
      }
      const uint32_t vt = Load32(base, task);
      if (vt != kJumpTaskVtable && vt != kFallTaskVtable)
      {
        continue;
      }
      const uint32_t state = Load32(base, task + 0xC);
      if (state == 1)
      {
        if (PPCFunc *stop = pdz::GuestFunction(Load32(base, vt + 0x18), "counter-op spiderbot stale task stop"))
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = task;
          stop(frame.ctx, base);
        }
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = task;
        sub_8217E990(frame.ctx, base);
      }
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = bot + kTaskStack;
        frame.ctx.r4.u64 = task;
        sub_82181430(frame.ctx, base);
      }
      what = fmt::format("a {} task {:08X} (state {}) no handle held was left on the list: ended and removed as Rare's push "
                         "does (pdz_counterop_spiderbot_unstick)",
                         vt == kJumpTaskVtable ? "jump" : "fall/get-up", task, state);
      return true;
    }
    return false;
  }

  // Rare's pounce: the jump task, started exactly as the bot starts its own jumps (825C9C2C..825C9CA0).
  bool StartPounce(PPCContext &ctx, uint8_t *base, uint32_t bot, const float dest[3], const float normal[3],
                   uint32_t scratch, std::string &why)
  {
    if (Load32(base, bot + kJumpTask) != 0)
    {
      why = fmt::format("its jump handle (+0x474) still holds task {:08X}", Load32(base, bot + kJumpTask));
      return false;
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
      StoreFloatBe(base, scratch + 4 * i, dest[i]);
      StoreFloatBe(base, scratch + 0x10 + 4 * i, normal[i]);
    }
    uint32_t task = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 0xB0;
      sub_821784F0(frame.ctx, base);
      task = frame.ctx.r3.u32;
    }
    if (!Ptr(task))
    {
      why = "no memory for a task (the allocator 821784F0 gave none)";
      return false;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = task;
      frame.ctx.r4.u64 = scratch;
      frame.ctx.r5.u64 = scratch + 0x10;
      frame.ctx.r6.u64 = 0;
      sub_821B1A00(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = bot + kJumpHandle;
      frame.ctx.r4.u64 = task;
      sub_821433D8(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = bot + kTaskStack;
      frame.ctx.r4.u64 = Load32(base, bot + kJumpTask);
      frame.ctx.r5.u64 = 1;
      sub_82180508(frame.ctx, base);
    }
    if (Load32(base, bot + kJumpTask) == 0)
    {
      why = fmt::format("Rare's push 82180508 refused it (the top task of the bot's list said no) | bot +0x474 {:08X} +0xDD0 "
                        "{:08X} +0x3BC {:08X} +0xDEC {} | list:{}",
                        Load32(base, bot + kJumpTask), Load32(base, bot + kFallTask), Load32(base, bot + 0x3BC),
                        int(base[bot + 0xDEC]), TaskListText(base, bot));
      return false;
    }
    return true;
  }
  // Rare's detonation of a yellow-eyed bot: health 0 through Rare's setter, then Rare's own death call (825C8388
  // builds the hit record and calls 825C7CC8: explosive type 0x17 at the bot, owned by the bot, set off).
  bool Detonate(PPCContext &ctx, uint8_t *base, uint32_t bot)
  {
    if (PPCFunc *fn = pdz::GuestFunction(Load32(base, Load32(base, bot) + 0x158), "counter-op spiderbot health"))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = bot;
      frame.ctx.r5.u64 = 0;
      frame.ctx.f1.f64 = 0.0;
      fn(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = bot;
      frame.ctx.r4.u64 = bot + kPos;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      sub_825C8388(frame.ctx, base);
    }
    return base[bot + kExploded] != 0;
  }
  float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  void Cross3(const float a[3], const float b[3], float o[3])
  {
    o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0];
  }
  bool Norm3(float v[3])
  {
    const float l = std::sqrt(Dot3(v, v));
    if (!(l > 1e-4f) || !std::isfinite(l))
    {
      return false;
    }
    v[0] /= l, v[1] /= l, v[2] /= l;
    return true;
  }
  // Round 12: the chase camera's clock (seconds, fine enough for a frame step at 120 fps).
  double NowSeconds()
  {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  // Round 12: one of Rare's physics rays (82259B80 with the hitscan's visitor 82090738, as cut_scenarios.cpp's
  // RayHitPoint builds it): true on a hit, with its point.
  uint32_t g_cam_ray = 0; // 0x140 bytes of guest memory: from +0, to +0x10, the visitor +0x20
  bool CamRay(PPCContext &ctx, uint8_t *base, uint32_t mask, const float from[3], const float to[3], float hit[3])
  {
    if (!g_cam_ray)
    {
      g_cam_ray = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x140);
      if (!g_cam_ray)
      {
        return false;
      }
    }
    std::memset(base + g_cam_ray, 0, 0x140);
    for (uint32_t i = 0; i < 3; ++i)
    {
      StoreFloatBe(base, g_cam_ray + 4 * i, from[i]);
      StoreFloatBe(base, g_cam_ray + 0x10 + 4 * i, to[i]);
    }
    const uint32_t visitor = g_cam_ray + 0x20;
    base[visitor] = 0x82, base[visitor + 1] = 0x09, base[visitor + 2] = 0x07, base[visitor + 3] = 0x38; // 82090738
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
    frame.ctx.r3.u64 = mask;
    frame.ctx.r4.u64 = g_cam_ray;
    frame.ctx.r5.u64 = g_cam_ray + 0x10;
    frame.ctx.r6.u64 = visitor;
    frame.ctx.r7.u64 = 1;
    sub_82259B80(frame.ctx, base);
    if ((frame.ctx.r3.u32 & 0xFF) == 0)
    {
      return false;
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
      hit[i] = LoadFloat(base, visitor + 68 + 4 * i);
    }
    return std::isfinite(hit[0] + hit[1] + hit[2]);
  }
  // +1 / -1: whether the engine's side axis is up x forward or its negative (measured from the bot's own matrix,
  // so that no handedness is assumed)
  float Handed(const Frame3 &f)
  {
    float c[3];
    Cross3(f.up, f.fwd, c);
    return Dot3(c, f.side) >= 0.0f ? 1.0f : -1.0f;
  }
  // A frame that sits flat on a surface: up = the normal, forward = the hint laid into the surface.
  bool FlatFrame(const float pos[3], const float n[3], const float hint[3], const float hint2[3], float h, Frame3 &t)
  {
    for (uint32_t i = 0; i < 3; ++i)
    {
      t.up[i] = n[i], t.pos[i] = pos[i];
    }
    const float d = Dot3(hint, n);
    for (uint32_t i = 0; i < 3; ++i)
    {
      t.fwd[i] = hint[i] - n[i] * d;
    }
    if (std::sqrt(Dot3(t.fwd, t.fwd)) < 0.3f)
    {
      const float d2 = Dot3(hint2, n);
      for (uint32_t i = 0; i < 3; ++i)
      {
        t.fwd[i] = hint2[i] - n[i] * d2;
      }
    }
    if (!Norm3(t.fwd))
    {
      return false;
    }
    Cross3(t.up, t.fwd, t.side);
    for (uint32_t i = 0; i < 3; ++i)
    {
      t.side[i] *= h;
    }
    return Norm3(t.side);
  }
  void WriteFrame(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t at, const Frame3 &t)
  {
    {
      rex::CallFrame frame(ctx); // the current matrix first: its fourth column is kept
      frame.ctx.r3.u64 = bot;
      frame.ctx.r4.u64 = at;
      sub_82518580(frame.ctx, base);
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
      StoreFloatBe(base, at + 4 * i, t.side[i]);
      StoreFloatBe(base, at + 0x10 + 4 * i, t.up[i]);
      StoreFloatBe(base, at + 0x20 + 4 * i, t.fwd[i]);
      StoreFloatBe(base, at + 0x30 + 4 * i, t.pos[i]);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = bot;
    frame.ctx.r4.u64 = at;
    sub_82518650(frame.ctx, base);
  }
  // Rare's own surface ray for a frame the bot does not have yet: the bot is given the candidate frame, Rare's probe
  // (8269CEF0, the spiderbot branch: a ray along the bot's own up axis, from 2d over the point to 2d + 30 under it,
  // d = the point's distance from the mover's position) is asked, and the bot's matrix is put back. Nothing moves.
  // Out: the hit point and the surface normal, turned to face the bot's body side (never into the surface).
  bool Probe(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t mover, uint32_t scratch, const Frame3 &cand,
             float hit[3], float n[3], int *why = nullptr) // round 13b (t): why 0 = a hit, 1 = no hit, 2 = a hit whose normal is refused
  {
    if (why)
    {
      *why = 1;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = bot;
      frame.ctx.r4.u64 = scratch + 0xE0;
      sub_82518580(frame.ctx, base);
    }
    WriteFrame(ctx, base, bot, scratch + 0xA0, cand);
    for (uint32_t i = 0; i < 3; ++i)
    {
      StoreFloatBe(base, scratch + 0x120 + 4 * i, cand.pos[i]);
      StoreFloatBe(base, scratch + 0x12C + 4 * i, cand.pos[i]);
    }
    bool ok = false;
    // Round 13 (b): with the flat lock the mover's own hit record (mover +0x64 .. +0xC4: what Rare's last move
    // found under the bot) is kept across a question of ours, so that the lock's tests read Rare's surface.
    const bool keep_rec = REXCVAR_GET(pdz_counterop_spiderbot_flat);
    uint8_t rec[0x60];
    std::memcpy(rec, base + mover + 0x64, sizeof(rec));
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = scratch + 0x120;
      frame.ctx.r4.u64 = bot;
      frame.ctx.r5.u64 = Load32(base, bot + 0xC);
      frame.ctx.r6.u64 = scratch + 0x12C;
      g_own_probe = true; // round 13 (b): a question of ours, not the mover's move
      sub_8269CEF0(frame.ctx, base);
      g_own_probe = false;
      ok = (frame.ctx.r3.u32 & 0xFF) != 0 && Load32(base, mover + kMoverRecFlags) != 0;
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
      hit[i] = LoadFloat(base, scratch + 0x120 + 4 * i);
      n[i] = LoadFloat(base, mover + kMoverNormal + 4 * i);
    }
    if (keep_rec)
    {
      std::memcpy(base + mover + 0x64, rec, sizeof(rec));
    }
    {
      rex::CallFrame frame(ctx); // the bot's own matrix back
      frame.ctx.r3.u64 = bot;
      frame.ctx.r4.u64 = scratch + 0xE0;
      sub_82518650(frame.ctx, base);
    }
    if (!ok || !Norm3(n) || !std::isfinite(hit[0] + hit[1] + hit[2]))
    {
      return false;
    }
    if (Dot3(n, cand.up) < 0.0f) // a back face: the normal must point to the side the bot's body is on
    {
      n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
    }
    if (why)
    {
      *why = Dot3(n, cand.up) > 0.5f ? 0 : 2;
    }
    return Dot3(n, cand.up) > 0.5f;
  }
  // Put the bot on a surface: Rare's placement (the mover's own position, moving on, forced velocity off), then the
  // exact frame (up = the surface's normal) and the mover's two normals.
  bool Place(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t mover, uint32_t scratch, const float pos[3],
             const float n[3], const float hint[3], const float hint2[3], float h)
  {
    Frame3 t;
    if (!Ptr(mover) || !FlatFrame(pos, n, hint, hint2, h, t))
    {
      return false;
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
      StoreFloatBe(base, scratch + 0x120 + 4 * i, pos[i]);
      StoreFloatBe(base, scratch + 0x138 + 4 * i, 0.0f);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = mover;
      frame.ctx.r4.u64 = scratch + 0x120;
      frame.ctx.r5.u64 = scratch + 0x138;
      g_own_probe = true; // round 13 (b): a placement of ours is never refused by the flat lock's probe call-in
      sub_825DE0E8(frame.ctx, base);
      g_own_probe = false;
    }
    WriteFrame(ctx, base, bot, scratch + 0xA0, t);
    for (uint32_t i = 0; i < 3; ++i)
    {
      StoreFloatBe(base, mover + kMoverNormal + 4 * i, n[i]);
      StoreFloatBe(base, mover + kMoverNormal2 + 4 * i, n[i]);
    }
    return true;
  }
  // The corner ahead in direction m (a unit vector in the bot's surface). kind 0: a surface that rises in front of
  // it (a wall from a floor, a floor from a wall): its normal faces back along m. kind 1: a convex edge (the top of
  // the glass box): the next surface lies beyond the edge and faces along m. The bot is placed on what Rare's ray
  // finds (place = true); false = no such surface, nothing was changed.
  bool Corner(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t mover, uint32_t scratch, const Frame3 &f,
              const float m[3], int kind, bool place, float out_n[3])
  {
    const float h = Handed(f);
    const bool ahead = Dot3(m, f.fwd) > 0.5f;
    for (int k = 0; k < (kind == 0 ? 4 : 3); ++k)
    {
      float up[3], pos[3], hint[3], hint2[3];
      for (uint32_t i = 0; i < 3; ++i)
      {
        up[i] = kind == 0 ? -m[i] : m[i];
        pos[i] = kind == 0 ? f.pos[i] + f.up[i] * 10.0f + m[i] * (25.0f * float(k))
                           : f.pos[i] + m[i] * 20.0f - f.up[i] * (15.0f + 25.0f * float(k));
        // going straight ahead the nose follows the turn; else the heading is kept as far as the surface allows
        hint[i] = ahead ? (kind == 0 ? f.up[i] : -f.up[i]) : f.fwd[i];
        hint2[i] = kind == 0 ? f.up[i] : -f.up[i];
      }
      Frame3 t;
      if (!FlatFrame(pos, up, hint, hint2, h, t))
      {
        continue;
      }
      float hit[3], n[3];
      if (Probe(ctx, base, bot, mover, scratch, t, hit, n) && Dot3(n, f.up) < 0.94f) // another surface (> 20 deg)
      {
        std::memcpy(out_n, n, sizeof(float) * 3);
        return !place || Place(ctx, base, bot, mover, scratch, hit, n, hint, hint2, h);
      }
    }
    return false;
  }
  // ---- Round 13 (b): the flat-ground lock's helpers -----------------------------------------------------------------
  // The frame saved when the bot last stood on a surface under his command (g_drive.saved).
  bool SavedFrame(Frame3 &s)
  {
    if (!g_drive.saved_ok)
    {
      return false;
    }
    const auto at = [&](uint32_t o) {
      const uint32_t v = (uint32_t(g_drive.saved[o]) << 24) | (uint32_t(g_drive.saved[o + 1]) << 16) |
                         (uint32_t(g_drive.saved[o + 2]) << 8) | g_drive.saved[o + 3];
      float x;
      std::memcpy(&x, &v, 4);
      return x;
    };
    for (uint32_t i = 0; i < 3; ++i)
    {
      s.side[i] = at(4 * i), s.up[i] = at(0x10 + 4 * i), s.fwd[i] = at(0x20 + 4 * i), s.pos[i] = at(0x30 + 4 * i);
    }
    return Norm3(s.up) && std::isfinite(s.pos[0] + s.pos[1] + s.pos[2]);
  }
  // The floor under a point: Rare's own surface ray (Probe) straight down (a candidate frame with the world's up),
  // from points lower and lower under `origin` (Rare's ray for a candidate d from the bot runs from 2d over it to
  // 2d + 30 cm under it). A hit counts when its normal is floor-like (y >= lim), it is not above max_y and not more
  // than max_down under the origin. Nothing is moved.
  bool FloorSearch(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t mover, uint32_t scratch, const Frame3 &f,
                   const float origin[3], float max_down, float max_y, float lim, float hit[3], float n[3])
  {
    const float world_up[3] = {0.0f, 1.0f, 0.0f};
    const float h = Handed(f);
    static const float kSteps[] = {0.0f, 25.0f, 50.0f, 100.0f, 150.0f, 250.0f, 400.0f};
    float reached = 0.0f;
    for (const float s : kSteps)
    {
      if (reached >= max_down)
      {
        break;
      }
      const float pos[3] = {origin[0], origin[1] - s, origin[2]};
      Frame3 t;
      if (FlatFrame(pos, world_up, f.fwd, f.side, h, t) && Probe(ctx, base, bot, mover, scratch, t, hit, n) &&
          n[1] >= lim && hit[1] <= max_y && hit[1] >= origin[1] - max_down - 30.0f)
      {
        return true;
      }
      reached = s * 3.0f + 30.0f;
    }
    return false;
  }
  // The lock's last resort and its put-back: the bot on the floor where it last stood under his command.
  bool PlaceSaved(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t mover, uint32_t scratch, float h, float lim)
  {
    Frame3 s;
    return SavedFrame(s) && s.up[1] >= lim && Place(ctx, base, bot, mover, scratch, s.pos, s.up, s.fwd, s.side, h);
  }
  // ---- Round 13b (t): the step rule's helpers ----------------------------------------------------------------------
  // The ground ahead: a floor (normal up part >= lim, Rare's own surface ray straight down) under one of the points
  // 14 / 30 / 50 cm on in the level direction of m, no more than `step` cm under or over the bot's own height and at
  // least `min_rise` cm over it, with nothing between the bot and that place (two of Rare's physics rays, bullet and
  // camera blockers: level out 40 cm over the higher of the two, then down to the place). Nothing is moved.
  bool GroundAhead(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t mover, uint32_t scratch, const Frame3 &f,
                   const float m[3], float step, float lim, float min_rise, float hit[3], float n[3])
  {
    float mh[3] = {m[0], 0.0f, m[2]};
    if (!Norm3(mh))
    {
      return false;
    }
    const uint32_t mask = uint32_t(REXCVAR_GET(pdz_counterop_spiderbot_cam_wall_mask)) | 1u;
    static const float kOn[] = {14.0f, 30.0f, 50.0f};
    for (const float s : kOn)
    {
      const float o[3] = {f.pos[0] + mh[0] * s, f.pos[1] + 10.0f, f.pos[2] + mh[2] * s};
      if (!FloorSearch(ctx, base, bot, mover, scratch, f, o, step + 10.0f, f.pos[1] + step, lim, hit, n) ||
          hit[1] < f.pos[1] - step || hit[1] - f.pos[1] < min_rise)
      {
        continue;
      }
      const float top = std::max(f.pos[1], hit[1]) + 40.0f;
      const float a0[3] = {f.pos[0], top, f.pos[2]}, a1[3] = {hit[0], top, hit[2]}, a2[3] = {hit[0], hit[1] + 12.0f, hit[2]};
      float at[3];
      if (CamRay(ctx, base, mask, a0, a1, at) || CamRay(ctx, base, mask, a1, a2, at))
      {
        continue;
      }
      return true;
    }
    return false;
  }
  // For the log: the ground's height along the level direction of m, in cm over the bot's own (Rare's physics ray
  // straight down from 60 cm over the bot's height to 6 m under it; '.' = nothing).
  std::string GroundProfile(PPCContext &ctx, uint8_t *base, const Frame3 &f, const float m[3])
  {
    float mh[3] = {m[0], 0.0f, m[2]};
    if (!Norm3(mh))
    {
      return "no level direction";
    }
    const uint32_t mask = uint32_t(REXCVAR_GET(pdz_counterop_spiderbot_cam_wall_mask)) | 1u;
    std::string out;
    static const float kAt[] = {-20.0f, 0.0f, 12.0f, 25.0f, 40.0f, 60.0f, 90.0f, 130.0f, 200.0f};
    for (const float s : kAt)
    {
      const float a[3] = {f.pos[0] + mh[0] * s, f.pos[1] + 60.0f, f.pos[2] + mh[2] * s};
      const float b[3] = {a[0], f.pos[1] - 600.0f, a[2]};
      float at[3];
      out += CamRay(ctx, base, mask, a, b, at) ? fmt::format(" {:.0f}:{:+.0f}", s, at[1] - f.pos[1])
                                               : fmt::format(" {:.0f}:.", s);
    }
    return out;
  }
  // ONLY RELEASED BOTS. Rare's on-spawn (822F2FC8) marks every set-piece spiderbot with allies = 0xFFFF: spawner
  // +0x7E0 (the 7 on the glass tank: enemies 0x0400, an extra goal), +0x848 / +0x87C (enemies 0x0800 / 0x1000),
  // +0x8B0 (team | 0x4000). The script's release is 82301940 / 82301988: allies and enemies cleared. The battle's
  // bots (on-spawn 822F34B8) are born with both cleared. The tank's bots are never released: the script's Update
  // (822FBDD4..822FBE98) quietly kills any of them whose up.y is 0.1 or more ("fallen off the tank").
  bool Released(const uint8_t *base, uint32_t bot)
  {
    if (Load16(base, bot + kAllies) == 0xFFFFu)
    {
      return false;
    }
    const uint32_t level = Load32(base, kGame + 0x1C);
    const uint32_t target = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, target, 0x74 + 4) ? Load32(base, target + 0x74) : 0;
    if (pdz::guestmem::Ok(base, script, 4) && Load32(base, script) == kDatacoreScript)
    {
      const uint32_t count = Load32(base, script + kTankCount), array = Load32(base, script + kTankArray);
      for (uint32_t i = 0; Ptr(array) && i < count && i < 64; ++i)
      {
        if (Load32(base, array + 8 * i + 4) == bot)
        {
          return false; // one of the glass tank's
        }
      }
    }
    return true;
  }
  // On a floor by Rare's own state: attached (no tumble, no fall, no jump), the mover's last surface ray hit, and
  // the up axis within 10 degrees of the world's (Rare's mover leaves up to 8.1 degrees uncorrected).
  bool OnFloor(PPCContext &ctx, uint8_t *base, uint32_t bot, uint32_t scratch)
  {
    const uint32_t mover = Load32(base, bot + kMover);
    if (!Ptr(mover) || Falling(base, bot) || Load32(base, bot + kJumpTask) || !Load32(base, mover + kMoverRecFlags))
    {
      return false;
    }
    return BotFrame(ctx, base, bot, scratch).up[1] > 0.985f;
  }
  // An armed friend of the agent within `metres` of the bot (Rare's friend test 8251EBD0 from her side: the other's
  // team bits among her team | allies and not among her enemies; an item in either hand).
  bool NearArmedFriend(const uint8_t *base, uint32_t bot, uint32_t agent, uint32_t p2c, float metres)
  {
    const uint16_t mine = uint16_t(Load16(base, agent + kTeam) | Load16(base, agent + kAllies));
    const uint16_t foes = uint16_t(Load16(base, agent + kEnemies));
    uint32_t node = Load32(base, kCharList + 0x710);
    const uint32_t end = Load32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t o = Load32(base, node);
      node = Load32(base, node + 8);
      if (!Ptr(o) || o == agent || o == p2c || o == bot)
      {
        continue;
      }
      const uint32_t vt = Load32(base, o);
      if (vt != kGuardVtable && vt != kHumanVtable)
      {
        continue;
      }
      const uint16_t team = uint16_t(Load16(base, o + kTeam));
      if (!(team & mine) || (team & foes))
      {
        continue;
      }
      if (!Ptr(Load32(base, o + kHand0)) && !Ptr(Load32(base, o + kHand1)))
      {
        continue;
      }
      const float d = Dist(base, bot, o);
      if (d >= 0.0f && d < metres)
      {
        return true;
      }
    }
    return false;
  }
  // Round 11: nobody keeps the bot he has just been given as a target. The holders are the ones Rare's sight update
  // 82580508 fills (+0x3168 current, +0x3150 fallback; the object at +4), cleared through Rare's handle setter as
  // 82580F6C-82580F74 does it. Characters: Rare's list (0x82E454A0 +0x710 .. +0x714; node +0 character, +8 next).
  uint32_t DropTargetsOn(PPCContext &ctx, uint8_t *base, uint32_t bot)
  {
    constexpr uint32_t kCharList = 0x82E454A0, kGuardVtable = 0x820C45A0;
    uint32_t node = Load32(base, kCharList + 0x710), dropped = 0;
    const uint32_t end = Load32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t c = Load32(base, node);
      node = Load32(base, node + 8);
      if (!Ptr(c) || Load32(base, c) != kGuardVtable)
      {
        continue;
      }
      for (const uint32_t holder : {0x3168u, 0x3150u})
      {
        if (Load32(base, c + holder + 4) == bot)
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = c + holder;
          frame.ctx.r4.u64 = 0;
          sub_821433D8(frame.ctx, base);
          ++dropped;
        }
      }
    }
    return dropped;
  }

  // ---- Round 12 (item 2): the pick by floor and Rare's state, and a bot made through Rare's spawner ----------------
  constexpr uint32_t kSpawnerB = 0x820A1854; // kind B (spiderbot) spawner vtable (ctor 823046C8)
  constexpr uint32_t kSpSlotCount = 4, kSpTemplates = 0xC, kSpPads = 0x10, kSpPadCount = 0x14, kSpSlots = 0x2C,
                     kSpActive = 0x30;
  // Datacore's eleven spiderbot spawners (script ctor 822F8FD8-822F912C). On-spawn callbacks (822F934C-822F9384):
  // +0x7E0 / +0x848 / +0x87C / +0x8B0 -> 822F2FC8 (the set pieces: allies FFFF until the script's release 82301940 /
  // 82301988); +0x814 / +0x918 / +0x94C -> 822F34B8; +0x9B4 / +0x9E8 -> 822F3610; +0x8E4 / +0x980 -> 822F37C0.
  constexpr uint32_t kAllBotSpawners[] = {0x7E0, 0x814, 0x848, 0x87C, 0x8B0, 0x8E4, 0x918, 0x94C, 0x980, 0x9B4, 0x9E8};
  // The spawners a bot may be made from for player 2: never the glass tank's, never a set piece's.
  constexpr uint32_t kBattleBotSpawners[] = {0x814, 0x8E4, 0x918, 0x94C, 0x980, 0x9B4, 0x9E8};

  uint32_t DatacoreScriptOf(const uint8_t *base)
  {
    const uint32_t level = Load32(base, kGame + 0x1C);
    const uint32_t target = pdz::guestmem::Ok(base, level, 0x2D8 + 4) ? Load32(base, level + 0x2D8) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, target, 0x74 + 4) ? Load32(base, target + 0x74) : 0;
    return pdz::guestmem::Ok(base, script, 4) && Load32(base, script) == kDatacoreScript ? script : 0;
  }
  // One of the seven on the glass tank (spawner +0x7E0): Rare's script kills any of them that leaves a pane.
  bool TankBot(const uint8_t *base, uint32_t script, uint32_t bot)
  {
    if (!script)
    {
      return false;
    }
    const uint32_t count = Load32(base, script + kTankCount), array = Load32(base, script + kTankArray);
    for (uint32_t i = 0; Ptr(array) && i < count && i < 64; ++i)
    {
      if (Load32(base, array + 8 * i + 4) == bot)
      {
        return true;
      }
    }
    return false;
  }
  // Rare's own state of a bot: the spawner slot that holds it and that slot's 'active' byte (1 = in the world, 0 =
  // stashed). offset = 0 when it is in no spawner slot (a placed bot): then nothing says it is stashed.
  bool SlotActive(const uint8_t *base, uint32_t script, uint32_t bot, uint32_t *offset)
  {
    *offset = 0;
    for (const uint32_t off : kAllBotSpawners)
    {
      const uint32_t sp = script ? script + off : 0;
      if (!sp || Load32(base, sp) != kSpawnerB)
      {
        continue;
      }
      const uint32_t count = Load32(base, sp + kSpSlotCount), slots = Load32(base, sp + kSpSlots);
      const uint32_t active = Load32(base, sp + kSpActive);
      for (uint32_t i = 0; Ptr(slots) && i < count && i < 64; ++i)
      {
        if (Load32(base, slots + 8 * i + 4) == bot)
        {
          *offset = off;
          return !Ptr(active) || base[active + i] != 0;
        }
      }
    }
    return true;
  }
  // The battle: after the cutscene `mid` (the caller's stage test) and before the platform top: the script's area
  // word ([script +0x3E0] +0: bit N = the agent is in area N) has no bit of area 7 or later.
  bool BattleOn(const uint8_t *base, uint32_t script)
  {
    const uint32_t words = script ? Load32(base, script + 0x3E0) : 0;
    return Ptr(words) && (Load32(base, words) & 0x180u) == 0; // areas 7 and 8 = the platform top
  }

  struct SpawnState
  {
    uint32_t script = 0;
    uint32_t bot = 0;        // the bot last made for him
    int64_t at = 0, tried = 0;
    uint32_t made = 0;
    std::string last_why;
    std::unordered_map<uint32_t, uint8_t> refused; // bot -> the reason it was last refused for (one line a change)
    uint32_t refusal_lines = 0;
    std::vector<uint32_t> unused; // bots made for him and not driven yet
  };
  SpawnState g_spawn;

  // A bot for player 2 through Rare's own spawner: an empty slot of one of the battle's spawners, at that spawner's
  // own pad nearest the agent that is at least pdz_counterop_spiderbot_spawn_min_m from her, within the drive's
  // range, and passes Rare's own pad test (not occupied / blocked, no human sees the point 60 cm over it: 82153C00
  // with Rare's arguments from the kind B Fill 82304898-823048AC). The same preconditions as Rare's Fill: a free node
  // in Rare's character list. Returns the bot or 0; why = what happened.
  uint32_t SpawnBotFor(PPCContext &ctx, uint8_t *base, uint32_t script, uint32_t agent, std::string &why)
  {
    constexpr uint32_t kCharListA = 0x82E454A0;
    if (Load32(base, kCharListA + 0x714) == kCharListA + 0x708 || Load32(base, kCharListA + 0x718) >= 126)
    {
      why = fmt::format("Rare's character list has no room ({} of 150 nodes in use)", Load32(base, kCharListA + 0x718));
      return 0;
    }
    struct PadCand
    {
      uint32_t sp, off, pad_index, pad_number, slot;
      float d, x, y, z;
    };
    std::vector<PadCand> cands;
    const float min_m = float(REXCVAR_GET(pdz_counterop_spiderbot_spawn_min_m));
    const float max_m = float(REXCVAR_GET(pdz_counterop_spiderbot_range_m));
    uint32_t spawners = 0, full = 0, too_near = 0, too_far = 0, blocked = 0;
    for (const uint32_t off : kBattleBotSpawners)
    {
      const uint32_t sp = script + off;
      if (Load32(base, sp) != kSpawnerB)
      {
        continue;
      }
      ++spawners;
      const uint32_t count = Load32(base, sp + kSpSlotCount), slots = Load32(base, sp + kSpSlots);
      const uint32_t pads = Load32(base, sp + kSpPads), pad_count = Load32(base, sp + kSpPadCount);
      if (!Ptr(slots) || !Ptr(pads) || !Ptr(Load32(base, sp + kSpActive)) || Load32(base, sp + kSpTemplates) == 0)
      {
        continue;
      }
      int free_slot = -1;
      for (uint32_t i = 0; i < count && i < 64 && free_slot < 0; ++i)
      {
        free_slot = Load32(base, slots + 8 * i + 4) == 0 ? int(i) : -1;
      }
      if (free_slot < 0)
      {
        ++full;
        continue;
      }
      for (uint32_t p = 0; p < pad_count && p < 64; ++p)
      {
        const uint32_t number = Load32(base, pads + 4 * p);
        uint32_t pad = 0;
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = number & 0xFFFFu;
          sub_8216A5D8(frame.ctx, base);
          pad = frame.ctx.r3.u32;
        }
        if (!Ptr(pad))
        {
          continue;
        }
        PadCand c{sp, off, p, number, uint32_t(free_slot), 0.0f, LoadFloat(base, pad + 0x10), LoadFloat(base, pad + 0x14),
                  LoadFloat(base, pad + 0x18)};
        const float dx = c.x - LoadFloat(base, agent + kPos), dy = c.y - LoadFloat(base, agent + kPos + 4),
                    dz = c.z - LoadFloat(base, agent + kPos + 8);
        c.d = std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f;
        if (c.d < min_m)
        {
          ++too_near;
          continue;
        }
        if (c.d > max_m)
        {
          ++too_far;
          continue;
        }
        cands.push_back(c);
      }
    }
    std::sort(cands.begin(), cands.end(), [](const PadCand &a, const PadCand &b) { return a.d < b.d; });
    for (const PadCand &c : cands)
    {
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = c.pad_number;
        frame.ctx.r4.u64 = 0;
        frame.ctx.r5.u64 = 0; // not 'even if seen': a pad a human sees is refused
        frame.ctx.r6.u64 = 0; // the pad's own flags count
        frame.ctx.f1.f64 = 60.0; // Rare's height for a spiderbot pad (the float at 820A72C4)
        sub_82153C00(frame.ctx, base);
        if (frame.ctx.r3.u32 & 0xFF)
        {
          ++blocked;
          continue;
        }
      }
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = c.sp;
        frame.ctx.r4.u64 = 0; // the spawner's one template (Spiderbot01)
        frame.ctx.r5.u64 = c.pad_index;
        frame.ctx.r6.u64 = c.slot;
        sub_823060C0(frame.ctx, base);
      }
      const uint32_t slots = Load32(base, c.sp + kSpSlots);
      const uint32_t bot = Ptr(slots) ? Load32(base, slots + 8 * c.slot + 4) : 0;
      why = fmt::format("Rare's spawner +0x{:X} (823060C0: slot {}, its pad #{} = pad {} at ({:.0f}, {:.0f}, {:.0f}), "
                        "{:.1f} m from the agent, seen by no player; {} nearer pad(s) refused by Rare's pad test)",
                        c.off, c.slot, c.pad_index, c.pad_number, c.x, c.y, c.z, c.d, blocked);
      return Ptr(bot) && Load32(base, bot) == kSpiderVtable ? bot : 0;
    }
    why = fmt::format("no pad to make one at: {} battle spawner(s), {} with every slot filled, pads: {} nearer than "
                      "{:.0f} m to the agent, {} further than {:.0f} m, {} occupied or in a player's view",
                      spawners, full, too_near, min_m, too_far, max_m, blocked);
    return 0;
  }
} // namespace

// counter_op.cpp's tick. waiting =player 2 is out of the world waiting for a guard, outside cutscenes. pad = his
// XInput user.
void PdzCounterOpSpiderDrive(PPCContext &ctx, uint8_t *base, uint32_t agent, uint32_t p2c, int32_t pad, bool waiting,
                             uint32_t)
{
  struct YGuard // on every way out: his Y button is dropped while he has a bot, passed on again when he has none
  {
    int32_t pad;
    ~YGuard() { counter_op_pad::BlockY(g_drive.bot && pad >= 0 && pad < 4 ? pad : -1); }
  } y_guard{pad};
  PdzCounterOpHudRestoreTick(ctx, base, pad); // round 16 (item 2): a check of his weapon HUD, when one is armed
  const int64_t now = NowMs();
  if (!g_drive.scratch && REXCVAR_GET(pdz_counterop_spiderbot))
  {
    g_drive.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x150);
    if (g_drive.scratch)
    {
      std::memset(base + g_drive.scratch, 0, 0x150);
    }
  }
  const uint32_t scratch = g_drive.scratch;
  const bool on = REXCVAR_GET(pdz_counterop_spiderbot) && PdzCounterOpRunning() && Ptr(agent) && Ptr(p2c) &&
                  pad >= 0 && pad < 4 && scratch;
  if (g_drive.bot)
  {
    const uint32_t bot = g_drive.bot;
    if (Load32(base, bot) != kSpiderVtable)
    {
      g_drive.bot = 0; // nothing of it is left to hand back
      const uint32_t driven = g_drive.driven;
      g_drive = Drive{};
      g_drive.driven = driven;
      g_drive.scratch = scratch;
      g_drive.ended = now;
      REXLOG_INFO("pdz_counterop: spider: player 2's spiderbot {:08X} is over (the level removed it)", bot);
      g_cp = ClosePounce{};                                                      // round 16 (item 3)
      PdzCounterOpHudRestoreWeapons(ctx, base, pad, "the level removed the bot"); // round 16 (item 2)
      return;
    }
    if (base[bot + kDying] || base[bot + kExploded])
    {
      EndDrive(ctx, base, agent, g_drive.fired ? "he set it off" : "it was destroyed");
      return;
    }
    if (!on || !waiting)
    {
      EndDrive(ctx, base, agent, "handed back to Rare (he no longer waits, a cutscene, or the mode is off)");
      return;
    }
    if (PdzMinigameRunning(agent))
    {
      EndDrive(ctx, base, agent, "handed back to Rare (the agent is in a hacking minigame)");
      return;
    }
    const float agent_m = Dist(base, bot, agent);
    g_last_agent = agent;
    // the agent has left the section: back to the waiting rule
    const float range = float(REXCVAR_GET(pdz_counterop_spiderbot_range_m));
    if (agent_m > range * 1.5f)
    {
      if (!g_drive.far_since)
      {
        g_drive.far_since = now;
      }
      else if (now - g_drive.far_since > 8000)
      {
        EndDrive(ctx, base, agent, "handed back to Rare (the agent has left it behind)");
        return;
      }
    }
    else
    {
      g_drive.far_since = 0;
    }
    // his input
    // Round 10 (item 3): hand test of round 9 (log 007): 123 'driving' lines, every one with sticks (0.00, 0.00) and
    // no trigger press, with a real second controller. The values came only from the game's own polls of his pad
    // (the XInputGetState hook's record). His pad is now polled here as well, through the same guest function (so the
    // record, the held-button mask and the test scripts all see it), and the log says how often the game polled it.
    if (REXCVAR_GET(pdz_counterop_spiderbot_poll))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = uint32_t(pad);
      frame.ctx.r4.u64 = scratch + 0x80;
      std::memset(base + scratch + 0x80, 0, 0x10);
      sub_82AD4A18(frame.ctx, base);
      ++g_drive.own_polls;
    }
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    const uint32_t buttons = counter_op_pad::Polled(uint32_t(pad), lx, ly);
    counter_op_pad::PolledRight(uint32_t(pad), rx, ry);
    bool fire = (buttons & 0x20000u) && !(g_drive.prev_buttons & 0x20000u); // the right trigger's press
    const bool leave = (buttons & 0x2000u) && !(g_drive.prev_buttons & 0x2000u); // B: he leaves the bot
    g_drive.prev_buttons = buttons;
    g_drive.raw[0] = lx, g_drive.raw[1] = ly, g_drive.raw[2] = rx, g_drive.raw[3] = ry;
    if (!g_drive.input_seen && (buttons || std::fabs(lx) > 0.2f || std::fabs(ly) > 0.2f || std::fabs(rx) > 0.2f))
    {
      g_drive.input_seen = true;
      REXLOG_INFO("pdz_counterop: spider: player 2's REAL controller reaches the drive: pad {} (XInput user), left "
                  "stick ({:.2f}, {:.2f}), right stick ({:.2f}, {:.2f}), buttons {:05X}, {:.1f} s into the drive",
                  pad, lx, ly, rx, ry, buttons, double(now - g_drive.since) / 1000.0);
    }
    if (leave && now - g_drive.since > 500)
    {
      g_drive.left = true;
      EndDrive(ctx, base, agent, "he left it (B): handed back to Rare");
      return;
    }
    {
      const std::string test = REXCVAR_GET(pdz_counterop_test_spider_stick);
      float tx = 0.0f, ty = 0.0f, tt = 0.0f;
      int tf = 0;
      if (!test.empty() && std::sscanf(test.c_str(), "%f,%f,%f,%d", &tx, &ty, &tt, &tf) >= 3)
      {
        lx = tx, ly = ty, rx = tt;
        fire = tf != 0 && !g_drive.prev_test_fire;
        g_drive.prev_test_fire = tf != 0;
      }
    }
    if (REXCVAR_GET(pdz_counterop_test_spider_chase) == 1)
    {
      // TEST TOOL: steer at the agent in the bot's own frame; the turn sign is learnt (flipped when the angle grows)
      const Frame3 c = BotFrame(ctx, base, bot, scratch);
      float side = 0.0f, ahead = 0.0f;
      for (uint32_t i = 0; i < 3; ++i)
      {
        const float to = LoadFloat(base, agent + kPos + 4 * i) - c.pos[i];
        side += to * c.side[i], ahead += to * c.fwd[i];
      }
      const float angle = std::atan2(side, ahead);
      if (now - g_drive.chase_at > 600)
      {
        if (g_drive.chase_at && std::fabs(angle) > std::fabs(g_drive.chase_angle) + 0.05f && std::fabs(angle) > 0.3f)
        {
          g_drive.chase_sign = -g_drive.chase_sign;
          REXLOG_INFO("pdz_counterop: spider: TEST TOOL chase: the angle to the agent grew ({:.2f} -> {:.2f} rad): turn "
                      "sign now {:+.0f}",
                      g_drive.chase_angle, angle, g_drive.chase_sign);
        }
        g_drive.chase_at = now, g_drive.chase_angle = angle;
      }
      rx = std::clamp(angle * 2.0f, -1.0f, 1.0f) * g_drive.chase_sign;
      lx = 0.0f;
      ly = std::fabs(angle) < 0.6f && agent_m > 1.6f ? 1.0f : 0.0f;
      fire = agent_m >= 0.0f && agent_m < 3.0f && std::fabs(angle) < 0.35f && now - g_drive.chase_fire > 2500;
      if (fire)
      {
        g_drive.chase_fire = now;
      }
    }
    const auto dead = [](float v) { return std::fabs(v) < 0.2f ? 0.0f : v; };
    g_drive.x = dead(lx), g_drive.y = dead(ly), g_drive.turn = dead(rx);
    Frame3 f = BotFrame(ctx, base, bot, scratch);
    // Round 13 (b): the flat-ground lock (pdz_counterop_spiderbot_flat) and what counts as a floor.
    const bool flat = REXCVAR_GET(pdz_counterop_spiderbot_flat);
    const float flat_cos = FlatCos();
    std::memcpy(g_drive.bot_up, f.up, sizeof(g_drive.bot_up)); // for the probe call-in (PdzCounterOpSpiderFlatProbe)
    // Steering as the chase camera shows it, on any surface. Stick up = the camera's view direction laid into the
    // surface the bot stands on (when the camera looks nearly straight at that surface: the world's up laid into
    // it); stick right = the surface direction at right angles to that, on the bot's own right-hand side (the same
    // side as on a floor).
    // Round 12 (item 1): the mover's local +x is the bot's LEFT (hand test, log 013: stick right gave local vx > 0
    // and the bot went to its left on floors and walls), so the sideways part is flipped
    // (pdz_counterop_spiderbot_side_flip). Round 12 (item 4, pdz_counterop_spiderbot_bot_relative): the stick is in
    // the bot's own frame on every surface: up = its forward, right = its right. The chase camera sits behind the
    // bot and tilts with it, so that is also what the screen shows, and forward stays forward across a step from a
    // floor onto a wall (the step turns the nose up the wall).
    const float side_sign = REXCVAR_GET(pdz_counterop_spiderbot_side_flip) ? -1.0f : 1.0f;
    float m[3] = {f.fwd[0], f.fwd[1], f.fwd[2]}; // the world direction he asks for (unit)
    if (REXCVAR_GET(pdz_counterop_spiderbot_bot_relative))
    {
      g_drive.cx = side_sign * g_drive.x;
      g_drive.cz = g_drive.y;
      float w[3];
      for (uint32_t i = 0; i < 3; ++i)
      {
        w[i] = f.side[i] * g_drive.cx + f.fwd[i] * g_drive.cz; // where that command takes it in the world
      }
      if (Norm3(w))
      {
        m[0] = w[0], m[1] = w[1], m[2] = w[2];
      }
    }
    else
    {
      float c[3], F[3], R[3], w[3];
      for (uint32_t i = 0; i < 3; ++i)
      {
        c[i] = g_drive.view ? g_drive.look[i] - g_drive.cam[i] : f.fwd[i];
      }
      if (!Norm3(c))
      {
        c[0] = f.fwd[0], c[1] = f.fwd[1], c[2] = f.fwd[2];
      }
      const float cn = Dot3(c, f.up);
      for (uint32_t i = 0; i < 3; ++i)
      {
        F[i] = c[i] - f.up[i] * cn;
      }
      if (std::sqrt(Dot3(F, F)) < 0.35f) // the camera looks (nearly) straight at the surface
      {
        const float world_up[3] = {0.0f, 1.0f, 0.0f};
        const float un = Dot3(world_up, f.up);
        for (uint32_t i = 0; i < 3; ++i)
        {
          F[i] = world_up[i] - f.up[i] * un;
        }
      }
      if (!Norm3(F))
      {
        F[0] = f.fwd[0], F[1] = f.fwd[1], F[2] = f.fwd[2];
      }
      Cross3(f.up, F, R);
      const float h = Handed(f);
      for (uint32_t i = 0; i < 3; ++i)
      {
        R[i] *= h * side_sign;
        w[i] = R[i] * g_drive.x + F[i] * g_drive.y;
      }
      g_drive.cx = Dot3(w, f.side);
      g_drive.cz = Dot3(w, f.fwd);
      if (Norm3(w))
      {
        m[0] = w[0], m[1] = w[1], m[2] = w[2];
      }
    }
    const float stick = std::min(1.0f, std::sqrt(g_drive.x * g_drive.x + g_drive.y * g_drive.y));
    const uint32_t mover = Load32(base, bot + kMover);
    const bool jumping = Load32(base, bot + kJumpTask) != 0;
    const bool falling = Falling(base, bot);
    if (!g_drive.taken && Ptr(mover))
    {
      // take-over: Rare's "moving" flag on (the walk blend needs it), any forced world move vector off
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = mover;
      frame.ctx.r4.u64 = 1;
      sub_825DD3A0(frame.ctx, base);
      if (!jumping)
      {
        std::memset(base + mover + kMoverForced, 0, 12);
      }
      g_drive.taken = true;
    }
    // While Rare's jump (the pounce) or fall runs, the command is Rare's.
    g_drive.manual = g_drive.taken && !jumping && !falling;
    if (g_drive.manual && Ptr(mover) && !base[mover + kMoverMoving])
    {
      base[mover + kMoverMoving] = 1; // a finished task of Rare's switched the walk blend off
    }
    const float vz = Ptr(mover) ? LoadFloat(base, mover + kMoverVel + 8) : 0.0f;
    const float speed = LoadFloat(base, bot + kSpeed);
    const float along = Ptr(mover) && stick > 0.05f
                            ? (LoadFloat(base, mover + kMoverVel) * g_drive.cx + vz * g_drive.cz) / stick
                            : 0.0f; // the measured speed in the direction he asks for
    const bool attached = g_drive.manual && Ptr(mover) && Load32(base, mover + kMoverRecFlags) != 0;
    // Round 13 (b): the normal of the surface Rare's last move found under the bot (turned to the bot's body side),
    // read before any question of ours.
    float rare_n[3] = {0.0f, 1.0f, 0.0f};
    bool rare_n_ok = false;
    if (attached)
    {
      for (uint32_t i = 0; i < 3; ++i)
      {
        rare_n[i] = LoadFloat(base, mover + kMoverNormal + 4 * i);
      }
      rare_n_ok = Norm3(rare_n);
      if (rare_n_ok && Dot3(rare_n, f.up) < 0.0f)
      {
        rare_n[0] = -rare_n[0], rare_n[1] = -rare_n[1], rare_n[2] = -rare_n[2];
      }
    }
    g_drive.hold = false;
    if (stick > 0.5f)
    {
      g_drive.fwd_since = g_drive.fwd_since ? g_drive.fwd_since : now;
    }
    else
    {
      g_drive.fwd_since = 0;
    }
    // Where it last stood on a surface under his command.
    if (attached && now - g_drive.saved_at > 200 && now - g_drive.last_climb > 400 &&
        (!flat || (f.up[1] >= flat_cos && (!rare_n_ok || rare_n[1] >= flat_cos)))) // round 13 (b): a floor only
    {
      std::memcpy(g_drive.saved, base + scratch, 0x40); // BotFrame's matrix (read at the top of this tick)
      g_drive.saved_ok = true;
      g_drive.saved_at = now;
    }
    // A fall (Rare's tumble: the fall task 825CA878, physics on, byte +0x3BC) never ends the drive. Rare's own task
    // ends the tumble when its timer (task +0x30, 30 s) runs out: the timer is cut short, the task then ends the
    // physics and plays its get-up by itself (821A8950), and the bot is put back where it last stood.
    const bool fall = falling && !jumping && !base[bot + kDying];
    if (fall)
    {
      g_drive.fall_since = g_drive.fall_since ? g_drive.fall_since : now;
      const uint32_t ft = Load32(base, bot + kFallTask);
      if (!g_drive.fall_cut && base[bot + kDetached] && Ptr(ft) && LoadFloat(base, ft + kFallTimer) > 0.05f)
      {
        StoreFloatBe(base, ft + kFallTimer, 0.05f);
        g_drive.fall_cut = true;
        REXLOG_INFO("pdz_counterop: spider: {:08X} came off its surface at ({:.0f}, {:.0f}, {:.0f}): Rare's tumble is "
                    "cut short (fall task {:08X}, timer +0x30 -> 0.05 s); the drive goes on, the bot goes back where "
                    "it last stood",
                    bot, f.pos[0], f.pos[1], f.pos[2], ft);
      }
    }
    else
    {
      g_drive.fall_since = 0;
      g_drive.fall_cut = false;
    }
    // Landing: Rare's fall or jump has just ended. After a fall: back where it last stood. After a jump (the
    // pounce): onto the surface under it, flat (up = that surface's normal); none under it: where it last stood.
    const bool busy = jumping || falling;
    if (g_drive.was_busy && !busy && Ptr(mover) && !base[bot + kDying])
    {
      const float h = Handed(f);
      bool placed = false;
      const char *what = "nothing to stand on was found";
      if (flat)
      {
        // Round 13 (b): with the flat lock it only ever lands on a floor. After a jump (the pounce, also one at a
        // wall): the floor under where the leap ended (Rare's ray straight down, up to 8 m); none: the floor where
        // it last stood. After a fall: where it last stood, else the floor under it.
        float hit[3], n[3];
        const bool level = f.up[1] >= flat_cos;
        const float o[3] = {f.pos[0] + (level ? 0.0f : f.up[0] * 30.0f), f.pos[1] + (level ? 10.0f : f.up[1] * 30.0f),
                            f.pos[2] + (level ? 0.0f : f.up[2] * 30.0f)};
        if (g_drive.was_fall && PlaceSaved(ctx, base, bot, mover, scratch, h, flat_cos))
        {
          placed = true;
          what = "flat lock: back on the floor where it last stood";
          g_drive.put_back_at = now;
        }
        else if (FloorSearch(ctx, base, bot, mover, scratch, f, o, 800.0f, o[1] + 15.0f, flat_cos, hit, n) &&
                 Place(ctx, base, bot, mover, scratch, hit, n, f.fwd, f.side, h))
        {
          placed = true;
          what = "flat lock: on the floor under it, up = the floor's normal";
        }
        else if (PlaceSaved(ctx, base, bot, mover, scratch, h, flat_cos))
        {
          placed = true;
          what = "flat lock: no floor under it: back on the floor where it last stood";
          g_drive.put_back_at = now;
        }
      }
      else if (!g_drive.was_fall)
      {
        float hit[3], n[3];
        Frame3 c = f;
        for (uint32_t i = 0; i < 3; ++i)
        {
          c.pos[i] = f.pos[i] + f.up[i] * 10.0f;
        }
        if (Probe(ctx, base, bot, mover, scratch, c, hit, n))
        {
          placed = Place(ctx, base, bot, mover, scratch, hit, n, f.fwd, f.side, h);
          what = "on the surface under it, up = its normal";
        }
        else
        {
          const float world_up[3] = {0.0f, 1.0f, 0.0f};
          const float p[3] = {f.pos[0], f.pos[1] + 10.0f, f.pos[2]};
          Frame3 d;
          if (FlatFrame(p, world_up, f.fwd, f.side, h, d) && Probe(ctx, base, bot, mover, scratch, d, hit, n))
          {
            placed = Place(ctx, base, bot, mover, scratch, hit, n, f.fwd, f.side, h);
            what = "on the floor under it, up = the floor's normal";
          }
        }
      }
      if (!placed && g_drive.saved_ok && !flat)
      {
        const auto at = [&](uint32_t o) {
          const uint32_t v = (uint32_t(g_drive.saved[o]) << 24) | (uint32_t(g_drive.saved[o + 1]) << 16) |
                             (uint32_t(g_drive.saved[o + 2]) << 8) | g_drive.saved[o + 3];
          float x;
          std::memcpy(&x, &v, 4);
          return x;
        };
        float p[3], u[3], w[3], s[3];
        for (uint32_t i = 0; i < 3; ++i)
        {
          s[i] = at(4 * i), u[i] = at(0x10 + 4 * i), w[i] = at(0x20 + 4 * i), p[i] = at(0x30 + 4 * i);
        }
        placed = Norm3(u) && Place(ctx, base, bot, mover, scratch, p, u, w, s, h);
        what = "back where it last stood, with the up it had there";
        g_drive.put_back_at = now;
      }
      g_drive.last_climb = now; // no step for a moment
      const bool after_fall = g_drive.was_fall;
      f = BotFrame(ctx, base, bot, scratch);
      REXLOG_INFO("pdz_counterop: spider: {:08X} {}: {} ({}); now at ({:.0f}, {:.0f}, {:.0f}), up ({:.2f}, {:.2f}, "
                  "{:.2f}); the drive goes on",
                  bot, after_fall ? "is up again after its fall" : "has landed from its jump",
                  placed ? "placed" : "NOT placed", what, f.pos[0], f.pos[1], f.pos[2], f.up[0], f.up[1], f.up[2]);
    }
    g_drive.was_fall = busy ? (g_drive.was_fall || (falling && !jumping)) : false;
    g_drive.was_busy = busy;
    // Round 13 (b), the flat lock: the bot he drives stands on a wall or a ceiling anyway (the lock was switched on
    // while he was on one, a placement of Rare's, the test tool): after a quarter of a second it is put on the floor
    // under it (Rare's ray straight down from 30 cm off its surface, up to 10 m), else on the floor where it last
    // stood; neither: the bot goes back to Rare (who walks walls) and he gets another. The up axis may lag the
    // surface by Rare's 8.1 degrees, so it is tested 10 degrees wider than the surface's normal.
    if (flat && g_drive.manual && Ptr(mover))
    {
      const float up_lim =
          std::cos(std::min(89.0f, float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_flat_deg), 10.0, 80.0)) + 10.0f) *
                   3.14159265f / 180.0f);
      const bool off_floor = f.up[1] < up_lim || (rare_n_ok && rare_n[1] < flat_cos - 0.02f);
      if (!off_floor)
      {
        g_drive.wall_since = 0;
      }
      else if (!g_drive.wall_since)
      {
        g_drive.wall_since = now;
      }
      else if (now - g_drive.wall_since > 250 && now - g_drive.last_climb > 250)
      {
        const Frame3 was = f;
        const float h = Handed(f);
        const float o[3] = {f.pos[0] + f.up[0] * 30.0f, f.pos[1] + f.up[1] * 30.0f, f.pos[2] + f.up[2] * 30.0f};
        float hit[3], n[3];
        int how = 0;
        if (FloorSearch(ctx, base, bot, mover, scratch, f, o, 1000.0f, o[1] + 5.0f, flat_cos, hit, n) &&
            Place(ctx, base, bot, mover, scratch, hit, n, f.fwd, f.side, h))
        {
          how = 1;
        }
        else if (PlaceSaved(ctx, base, bot, mover, scratch, h, flat_cos))
        {
          how = 2;
          g_drive.put_back_at = now;
        }
        g_drive.last_climb = now;
        g_drive.wall_since = 0;
        ++g_drive.downs;
        f = BotFrame(ctx, base, bot, scratch);
        std::memcpy(g_drive.bot_up, f.up, sizeof(g_drive.bot_up));
        REXLOG_INFO("pdz_counterop: spider: flat lock: {:08X} stood on a wall or ceiling (up ({:.2f}, {:.2f}, {:.2f}), "
                    "surface normal ({:.2f}, {:.2f}, {:.2f}), at ({:.0f}, {:.0f}, {:.0f})): {} (#{} in this drive); now "
                    "at ({:.0f}, {:.0f}, {:.0f}), up ({:.2f}, {:.2f}, {:.2f})",
                    bot, was.up[0], was.up[1], was.up[2], rare_n[0], rare_n[1], rare_n[2], was.pos[0], was.pos[1],
                    was.pos[2],
                    how == 1   ? "brought DOWN to the floor under it (Rare's ray, Rare's placement)"
                    : how == 2 ? "no floor found under it: put back on the floor where it last stood"
                               : "no floor found under it and none remembered: it goes back to Rare",
                    g_drive.downs, f.pos[0], f.pos[1], f.pos[2], f.up[0], f.up[1], f.up[2]);
        if (!how)
        {
          EndDrive(ctx, base, agent, "flat lock: it stood on a wall or ceiling with no floor found under it: handed "
                                     "back to Rare");
          return;
        }
      }
    }
    else
    {
      g_drive.wall_since = 0;
    }
    // Flat on its surface: Rare's mover turns the up axis to the surface's normal only while they are more than 8.1
    // degrees apart (825DDC4C: dot < 0.99 at 820EAEE8); the rest is taken out here, from the normal of Rare's last
    // surface ray (mover +0x88).
    if (attached)
    {
      float n[3] = {LoadFloat(base, mover + kMoverNormal), LoadFloat(base, mover + kMoverNormal + 4),
                    LoadFloat(base, mover + kMoverNormal + 8)};
      if (Norm3(n))
      {
        float d = Dot3(n, f.up);
        if (d < 0.0f)
        {
          n[0] = -n[0], n[1] = -n[1], n[2] = -n[2], d = -d;
        }
        Frame3 t;
        if (d >= 0.99f && d < 0.9995f && FlatFrame(f.pos, n, f.fwd, f.side, Handed(f), t))
        {
          WriteFrame(ctx, base, bot, scratch + 0xA0, t); // the mover keeps the bot's rotation (825DD7FC)
          f = BotFrame(ctx, base, bot, scratch);
          if (g_drive.flats++ < 5)
          {
            REXLOG_INFO("pdz_counterop: spider: {:08X} set flat on its surface: up -> ({:.2f}, {:.2f}, {:.2f}) (it "
                        "was {:.1f} degrees off Rare's surface normal)",
                        bot, n[0], n[1], n[2], std::acos(std::min(1.0f, d)) * 57.29578f);
          }
        }
      }
    }
    // Round 13b (t), the step rule's lip test. Rare's mover was refused a steep face a moment ago (the probe call-in,
    // PdzCounterOpSpiderFlatProbe): round 13's run 3 had the bot stuck 12 s at the 9 cm lip at the foot of a 34 degree
    // slope (717 refusals: the hit on the lip's chamfer is 57 degrees steep). When the floor goes on just beyond it
    // (GroundAhead: a floor within the step height, a clear way), the call-in lets Rare's mover have the lip for the
    // next 150 ms. A real wall has no such floor behind its face: it stays refused.
    const bool step_rule = flat && REXCVAR_GET(pdz_counterop_spiderbot_flat_step);
    const float step_h = float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_flat_step_cm), 5.0, 80.0));
    if (step_rule && REXCVAR_GET(pdz_counterop_spiderbot_flat_lip) && attached && stick > 0.3f &&
        now - g_drive.probe_refused_at < 200 && now >= g_drive.lip_ok_until &&
        now - g_drive.lip_checked_at >= 50)
    {
      g_drive.lip_checked_at = now;
      float gh[3] = {}, gn[3] = {};
      if (GroundAhead(ctx, base, bot, mover, scratch, f, m, step_h, flat_cos, -1000.0f, gh, gn))
      {
        g_drive.lip_ok_until = now + 150;
        ++g_drive.lips;
        if (now - g_drive.lip_said >= 1000 && g_drive.lip_lines < 30)
        {
          g_drive.lip_said = now;
          ++g_drive.lip_lines;
          REXLOG_INFO("pdz_counterop: spider: flat lock: step rule: the steep face Rare's mover met is a LIP for {:08X} at "
                      "({:.0f}, {:.0f}, {:.0f}): a floor goes on at ({:.0f}, {:.0f}, {:.0f}) ({:+.0f} cm, normal ({:.2f}, "
                      "{:.2f}, {:.2f})) with a clear way: not refused for 150 ms (#{} in this drive; {} move(s) refused, {} "
                      "let through so far); direction ({:.2f}, {:.2f}, {:.2f}); the ground along its way:{}",
                      bot, f.pos[0], f.pos[1], f.pos[2], gh[0], gh[1], gh[2], gh[1] - f.pos[1], gn[0], gn[1], gn[2],
                      g_drive.lips, g_drive.probe_refused, g_drive.lip_passed, m[0], m[1], m[2],
                      GroundProfile(ctx, base, f, m));
        }
      }
    }
    // An edge ahead: Rare's ray at the point 12 cm on finds no surface under the bot. A surface beyond the edge (a
    // convex corner: the top of the glass, the far side of a ledge): it goes over onto it, its up = that surface's
    // normal. A surface rising in front: nothing here (the step below). Neither: it stops at the edge.
    if (attached && stick > 0.3f && REXCVAR_GET(pdz_counterop_spiderbot_edge_guard) && now - g_drive.last_climb > 250)
    {
      Frame3 c = f;
      for (uint32_t i = 0; i < 3; ++i)
      {
        c.pos[i] = f.pos[i] + m[i] * 12.0f;
      }
      float hit[3], n[3];
      int ahead_why = 0;
      const bool ahead_ok = Probe(ctx, base, bot, mover, scratch, c, hit, n, &ahead_why);
      const float ahead_n[3] = {n[0], n[1], n[2]};
      if (!ahead_ok && flat)
      {
        // Round 13b (t), the step rule (pdz_counterop_spiderbot_flat_step). Round 13's run 2 held the bot 13 s at a
        // place that was no edge: Rare's ray along the bot's own (tilted) up axis found nothing it accepted under the
        // point 12 cm on, though the ground went on a little lower. Now the floor straight under the next half metre
        // is looked for (GroundAhead): within the step height and with a clear way the bot is put on it at once.
        const bool step_on = step_rule;
        const float step_cm = step_h;
        float gh[3] = {}, gn[3] = {};
        // Round 13 (b), the flat lock at an edge. The surface beyond the edge is only stepped onto when it is a
        // floor (a slope going down); a wall face is not. A surface rising in front: nothing (it is a wall: Rare's
        // sweep stops the bot). Otherwise it stops at the edge, and, held against it for 0.4 s, steps down to the
        // floor under the edge when Rare's ray finds one within pdz_counterop_spiderbot_flat_drop_m (as a character
        // drops off a ledge; it can no longer walk down the wall face).
        float n1[3] = {};
        if (Corner(ctx, base, bot, mover, scratch, f, m, 1, false, n1) && n1[1] >= flat_cos &&
            Corner(ctx, base, bot, mover, scratch, f, m, 1, true, n))
        {
          g_drive.last_climb = now;
          f = BotFrame(ctx, base, bot, scratch);
          REXLOG_INFO("pdz_counterop: spider: flat lock: over an edge onto a slope (its normal ({:.2f}, {:.2f}, {:.2f}) "
                      "is a floor's); now at ({:.0f}, {:.0f}, {:.0f})",
                      n[0], n[1], n[2], f.pos[0], f.pos[1], f.pos[2]);
        }
        else if (step_on && GroundAhead(ctx, base, bot, mover, scratch, f, m, step_cm, flat_cos, -1000.0f, gh, gn) &&
                 Place(ctx, base, bot, mover, scratch, gh, gn, f.fwd, f.side, Handed(f)))
        {
          const Frame3 was = f;
          g_drive.last_climb = now - 130; // the edge test again in 120 ms
          g_drive.edge_seen = 0;
          ++g_drive.steps;
          f = BotFrame(ctx, base, bot, scratch);
          std::memcpy(g_drive.bot_up, f.up, sizeof(g_drive.bot_up));
          if (now - g_drive.step_said >= 500 && g_drive.step_lines < 40)
          {
            g_drive.step_said = now;
            ++g_drive.step_lines;
            REXLOG_INFO("pdz_counterop: spider: flat lock: step rule: NOT an edge for {:08X}: Rare's ray under the point "
                        "ahead {} but the floor goes on within {:.0f} cm of its height: put on it (#{} in this drive): "
                        "({:.0f}, {:.0f}, {:.0f}), up ({:.2f}, {:.2f}, {:.2f}) -> ({:.0f}, {:.0f}, {:.0f}), {:+.0f} cm, the "
                        "floor's normal ({:.2f}, {:.2f}, {:.2f}); direction ({:.2f}, {:.2f}, {:.2f})",
                        bot,
                        ahead_why == 2 ? fmt::format("hit a surface it does not take (normal ({:.2f}, {:.2f}, {:.2f}))",
                                                     ahead_n[0], ahead_n[1], ahead_n[2])
                                       : std::string("found nothing"),
                        step_cm, g_drive.steps, was.pos[0], was.pos[1], was.pos[2], was.up[0], was.up[1], was.up[2],
                        f.pos[0], f.pos[1], f.pos[2], f.pos[1] - was.pos[1], gn[0], gn[1], gn[2], m[0], m[1], m[2]);
          }
        }
        else if (!Corner(ctx, base, bot, mover, scratch, f, m, 0, false, n))
        {
          g_drive.hold = true;
          if (now - g_drive.edge_seen > 300) // not held against this edge a moment ago: the 0.4 s start now
          {
            g_drive.edge_since = now;
          }
          g_drive.edge_seen = now;
          const float drop = float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_flat_drop_m), 0.0, 10.0)) * 100.0f;
          ++g_drive.edge_refused;
          bool dropped = false;
          const char *why = "";
          if (drop > 0.0f && now - g_drive.edge_since >= 400)
          {
            const float o[3] = {f.pos[0] + m[0] * 45.0f, f.pos[1] + 10.0f, f.pos[2] + m[2] * 45.0f};
            const Frame3 was = f;
            // Nothing may stand between the bot and the place it steps down to (a pane, a door, a wall beyond a
            // gap): two of Rare's physics rays (bullet and camera blockers), level out over the edge, then down.
            const auto clear = [&](const float a[3], const float b[3]) {
              float at[3];
              return !CamRay(ctx, base, uint32_t(REXCVAR_GET(pdz_counterop_spiderbot_cam_wall_mask)) | 1u, a, b, at);
            };
            const float a0[3] = {f.pos[0], f.pos[1] + 40.0f, f.pos[2]}, a1[3] = {o[0], f.pos[1] + 40.0f, o[2]};
            // (round 13b t: the limit is exact: FloorSearch alone let a floor up to 20 cm under the limit pass)
            const bool found = FloorSearch(ctx, base, bot, mover, scratch, f, o, drop, f.pos[1] + 25.0f, flat_cos, hit, n) &&
                               hit[1] >= f.pos[1] - drop - 2.0f;
            const float a2[3] = {hit[0], hit[1] + 20.0f, hit[2]};
            const bool way1 = found && clear(a0, a1), way2 = way1 && clear(a1, a2);
            if (way2 && Place(ctx, base, bot, mover, scratch, hit, n, f.fwd, f.side, Handed(f)))
            {
              dropped = true;
              g_drive.hold = false;
              g_drive.last_climb = now;
              g_drive.edge_seen = 0;
              ++g_drive.drops;
              f = BotFrame(ctx, base, bot, scratch);
              REXLOG_INFO("pdz_counterop: spider: flat lock: {:08X} steps DOWN off the edge to the floor under it (#{} "
                          "in this drive): ({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, {:.0f}, {:.0f}), {:.0f} cm down, the "
                          "floor's normal ({:.2f}, {:.2f}, {:.2f})",
                          bot, g_drive.drops, was.pos[0], was.pos[1], was.pos[2], f.pos[0], f.pos[1], f.pos[2],
                          was.pos[1] - f.pos[1], n[0], n[1], n[2]);
            }
            else
            {
              g_drive.edge_since = now; // the next look in 0.4 s
              why = !found  ? "; no floor found under the edge within the step-down limit"
                    : !way1 ? "; a floor is under the edge but something is in the way over the edge (Rare's physics ray)"
                    : !way2 ? "; a floor is under the edge but something is in the way down to it (Rare's physics ray)"
                            : "; a floor is under the edge but Rare's placement failed";
            }
          }
          if (!dropped && now - g_drive.edge_said >= 1000 && g_drive.edge_lines < 20)
          {
            g_drive.edge_said = now;
            ++g_drive.edge_lines;
            REXLOG_INFO("pdz_counterop: spider: flat lock: {:08X} held at an edge at ({:.0f}, {:.0f}, {:.0f}) (direction "
                        "({:.2f}, {:.2f}, {:.2f})): no floor under the point ahead; it does not go down the wall face{}{}; "
                        "its up ({:.2f}, {:.2f}, {:.2f}); Rare's ray under the point ahead {}; the ground along its way "
                        "(cm on: cm over its height):{}; step rule {} ({:.0f} cm), step-down limit {:.1f} m",
                        bot, f.pos[0], f.pos[1], f.pos[2], m[0], m[1], m[2],
                        drop > 0.0f ? "; held on, it steps down when a floor is found under the edge" : "", why, f.up[0],
                        f.up[1], f.up[2],
                        ahead_why == 2 ? fmt::format("hit a surface it does not take (normal ({:.2f}, {:.2f}, {:.2f}))",
                                                     ahead_n[0], ahead_n[1], ahead_n[2])
                                       : std::string("found nothing"),
                        GroundProfile(ctx, base, f, m), step_on ? "on" : "off", step_cm, drop / 100.0f);
          }
        }
      }
      else if (!ahead_ok)
      {
        if (Corner(ctx, base, bot, mover, scratch, f, m, 1, true, n))
        {
          g_drive.last_climb = now;
          ++g_drive.climbs;
          const Frame3 was = f;
          f = BotFrame(ctx, base, bot, scratch);
          REXLOG_INFO("pdz_counterop: spider: step #{} over an edge: up ({:.2f}, {:.2f}, {:.2f}) -> ({:.2f}, {:.2f}, "
                      "{:.2f}) (the next surface's normal from Rare's ray), ({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, "
                      "{:.0f}, {:.0f})",
                      g_drive.climbs, was.up[0], was.up[1], was.up[2], f.up[0], f.up[1], f.up[2], was.pos[0],
                      was.pos[1], was.pos[2], f.pos[0], f.pos[1], f.pos[2]);
        }
        else if (!Corner(ctx, base, bot, mover, scratch, f, m, 0, false, n))
        {
          g_drive.hold = true;
          if (g_drive.holds++ < 10)
          {
            REXLOG_INFO("pdz_counterop: spider: {:08X} held at an edge at ({:.0f}, {:.0f}, {:.0f}): no surface "
                        "under the point ahead and none beyond the edge (direction ({:.2f}, {:.2f}, {:.2f}))",
                        bot, f.pos[0], f.pos[1], f.pos[2], m[0], m[1], m[2]);
          }
        }
      }
    }
    float agent_ahead = 0.0f;
    for (uint32_t i = 0; i < 3; ++i)
    {
      agent_ahead += (LoadFloat(base, agent + kPos + 4 * i) - f.pos[i]) * f.fwd[i];
    }
    const bool agent_blocks = agent_m >= 0.0f && agent_m < 2.5f && agent_ahead > 0.0f;
    // Ours: pushed against a surface that rises in front of it (a wall from the floor, the floor from a wall), it
    // steps onto it; nothing in front and an edge instead: over the edge. Rare's own ray finds the surface and gives
    // its normal; no surface: nothing is changed (the bot stays where it stands, the drive goes on).
    if (attached && (REXCVAR_GET(pdz_counterop_spiderbot_climb) || flat) && stick > 0.5f && along < speed * 0.15f &&
        !agent_blocks && g_drive.fwd_since && now - g_drive.fwd_since > 600 && now - g_drive.since > 800)
    {
      if (!g_drive.blocked_since)
      {
        g_drive.blocked_since = now;
      }
      else if (flat)
      {
        // Round 13b (t), pdz_counterop_spiderbot_flat_step_up: pushed 0.2 s against something low with a floor on top
        // (a stair's riser, a kerb: at least 4 cm and at most the step height over the bot, a clear way): up onto it.
        float uh[3] = {}, un[3] = {};
        if (step_rule && REXCVAR_GET(pdz_counterop_spiderbot_flat_step_up) && now - g_drive.blocked_since > 200 &&
            now - g_drive.last_climb > 300 &&
            GroundAhead(ctx, base, bot, mover, scratch, f, m, step_h, flat_cos, 4.0f, uh, un) &&
            Place(ctx, base, bot, mover, scratch, uh, un, f.fwd, f.side, Handed(f)))
        {
          const Frame3 was = f;
          g_drive.last_climb = now - 130;
          g_drive.blocked_since = 0;
          ++g_drive.step_ups;
          f = BotFrame(ctx, base, bot, scratch);
          std::memcpy(g_drive.bot_up, f.up, sizeof(g_drive.bot_up));
          if (g_drive.step_ups <= 40)
          {
            REXLOG_INFO("pdz_counterop: spider: flat lock: step rule: {:08X} steps UP onto the floor in front (#{} in this "
                        "drive): ({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, {:.0f}, {:.0f}), {:+.0f} cm, the floor's normal "
                        "({:.2f}, {:.2f}, {:.2f}); direction ({:.2f}, {:.2f}, {:.2f})",
                        bot, g_drive.step_ups, was.pos[0], was.pos[1], was.pos[2], f.pos[0], f.pos[1], f.pos[2],
                        f.pos[1] - was.pos[1], un[0], un[1], un[2], m[0], m[1], m[2]);
          }
        }
        // Round 13 (b), the flat lock: no step onto the surface in front, no step over an edge. It stays on the
        // floor, like a character pushed against a wall. One line a second while he keeps pushing (what Rare's ray
        // finds in front, the bot's up axis, the floor's normal).
        else if (now - g_drive.blocked_since > 300 && now - g_drive.flat_said >= 1000)
        {
          g_drive.flat_said = now;
          float wn[3] = {};
          const bool wall = Corner(ctx, base, bot, mover, scratch, f, m, 0, false, wn);
          if (g_drive.flat_refused++ < 30)
          {
            REXLOG_INFO("pdz_counterop: spider: flat lock: {:08X} is pushed against {} (direction ({:.2f}, {:.2f}, "
                        "{:.2f}), measured speed that way {:.0f} of {:.0f}): NO step onto it (#{} in this drive); it "
                        "stays on the floor at ({:.0f}, {:.0f}, {:.0f}), up ({:.2f}, {:.2f}, {:.2f}), the floor's "
                        "normal ({:.2f}, {:.2f}, {:.2f}); the ground along its way (cm on: cm over its height):{}",
                        bot,
                        wall ? fmt::format("a surface in front with the normal ({:.2f}, {:.2f}, {:.2f}){}", wn[0], wn[1],
                                           wn[2], wn[1] >= flat_cos ? " (a slope)" : " (a wall)")
                             : std::string("something Rare's surface ray does not find (a prop, a door, a character)"),
                        m[0], m[1], m[2], along, speed, g_drive.flat_refused, f.pos[0], f.pos[1], f.pos[2], f.up[0],
                        f.up[1], f.up[2], rare_n[0], rare_n[1], rare_n[2], GroundProfile(ctx, base, f, m));
          }
        }
      }
      else if (now - g_drive.blocked_since > 300 && now - g_drive.last_climb > 900)
      {
        const Frame3 was = f;
        float n[3] = {};
        const bool wall = Corner(ctx, base, bot, mover, scratch, f, m, 0, true, n);
        const bool edge = !wall && Corner(ctx, base, bot, mover, scratch, f, m, 1, true, n);
        g_drive.last_climb = now;
        g_drive.blocked_since = 0;
        if (wall || edge)
        {
          ++g_drive.climbs;
          f = BotFrame(ctx, base, bot, scratch);
        }
        REXLOG_INFO("pdz_counterop: spider: step #{} {}: up ({:.2f}, {:.2f}, {:.2f}) -> ({:.2f}, {:.2f}, {:.2f}), "
                    "({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, {:.0f}, {:.0f}); direction ({:.2f}, {:.2f}, {:.2f})",
                    g_drive.climbs,
                    wall ? "onto the surface in front (its normal from Rare's ray)"
                         : edge ? "over the edge (the next surface's normal from Rare's ray)"
                                : "NOT made: Rare's ray found no surface in front or beyond; it stays where it stands",
                    was.up[0], was.up[1], was.up[2], f.up[0], f.up[1], f.up[2], was.pos[0], was.pos[1], was.pos[2],
                    f.pos[0], f.pos[1], f.pos[2], m[0], m[1], m[2]);
      }
    }
    else
    {
      g_drive.blocked_since = 0;
    }
    // Round 13 (b): TESTING TOOL: the bot is put onto the surface in front of it past the lock (round 9's step).
    {
      static int32_t last_wall = 0;
      const int32_t tw = REXCVAR_GET(pdz_counterop_test_spider_wall);
      if (tw != last_wall)
      {
        last_wall = tw;
        if (tw && attached)
        {
          const Frame3 was = f;
          float n[3] = {};
          const bool wall = Corner(ctx, base, bot, mover, scratch, f, f.fwd, 0, true, n);
          if (wall)
          {
            g_drive.last_climb = now;
            f = BotFrame(ctx, base, bot, scratch);
            std::memcpy(g_drive.bot_up, f.up, sizeof(g_drive.bot_up));
          }
          REXLOG_INFO("pdz_counterop: spider: TEST TOOL: {:08X} {}: up ({:.2f}, {:.2f}, {:.2f}) -> ({:.2f}, {:.2f}, "
                      "{:.2f}), ({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, {:.0f}, {:.0f}); the flat lock is {}",
                      bot, wall ? "put ONTO the surface in front of it (past the lock)" : "NOT put onto a wall: Rare's ray "
                                                                                          "found no surface in front",
                      was.up[0], was.up[1], was.up[2], f.up[0], f.up[1], f.up[2], was.pos[0], was.pos[1], was.pos[2],
                      f.pos[0], f.pos[1], f.pos[2], flat ? "on: it must come down" : "off");
        }
      }
    }
    // Round 13b (t): TESTING TOOLS: the driven bot put on the ground at a point; the ground heights of a square.
    {
      static std::string last_place, last_survey;
      const std::string place = REXCVAR_GET(pdz_counterop_test_spider_place);
      if (place != last_place)
      {
        last_place = place;
        float px = 0.0f, py = 0.0f, pz = 0.0f, dx = 0.0f, dz = 1.0f;
        if (!place.empty() && Ptr(mover) && std::sscanf(place.c_str(), "%f,%f,%f,%f,%f", &px, &py, &pz, &dx, &dz) >= 3)
        {
          const uint32_t mask = uint32_t(REXCVAR_GET(pdz_counterop_spiderbot_cam_wall_mask)) | 1u;
          const float a[3] = {px, py + 80.0f, pz}, b[3] = {px, py - 600.0f, pz};
          const float world_up[3] = {0.0f, 1.0f, 0.0f}, hint[3] = {dx, 0.0f, dz};
          float at[3] = {};
          const Frame3 was = f;
          const bool found = CamRay(ctx, base, mask, a, b, at);
          const bool placed = found && Place(ctx, base, bot, mover, scratch, at, world_up, hint, f.fwd, Handed(f));
          if (placed)
          {
            g_drive.last_climb = now;
            g_drive.saved_ok = false;
            g_drive.edge_seen = 0;
            f = BotFrame(ctx, base, bot, scratch);
            std::memcpy(g_drive.bot_up, f.up, sizeof(g_drive.bot_up));
          }
          REXLOG_INFO("pdz_counterop: spider: TEST TOOL: place '{}': {:08X} {}: ({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, {:.0f}, "
                      "{:.0f}), forward ({:.2f}, {:.2f}, {:.2f}); the ground along its way:{}",
                      place, bot, placed ? "PUT on the ground under the point" : found ? "NOT placed (Rare's placement failed)" : "NOT placed (no ground under the point)",
                      was.pos[0], was.pos[1], was.pos[2], f.pos[0], f.pos[1], f.pos[2], f.fwd[0], f.fwd[1], f.fwd[2],
                      GroundProfile(ctx, base, f, f.fwd));
        }
      }
      const std::string survey = REXCVAR_GET(pdz_counterop_test_spider_survey);
      if (survey != last_survey)
      {
        last_survey = survey;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f, half_m = 10.0f, cell = 50.0f;
        if (!survey.empty() && std::sscanf(survey.c_str(), "%f,%f,%f,%f,%f", &sx, &sy, &sz, &half_m, &cell) >= 3)
        {
          cell = std::clamp(cell, 5.0f, 200.0f);
          const int half = std::clamp(int(half_m * 100.0f / cell), 1, 80);
          const uint32_t mask = uint32_t(REXCVAR_GET(pdz_counterop_spiderbot_cam_wall_mask)) | 1u;
          REXLOG_INFO("pdz_counterop: spider: TEST TOOL: survey '{}': ground heights in cm over y {:.0f}, {} x {} points "
                      "{:.0f} cm apart; each row: z, then the points from x {:.0f} to x {:.0f}",
                      survey, sy, 2 * half + 1, 2 * half + 1, cell, sx - float(half) * cell, sx + float(half) * cell);
          for (int iz = -half; iz <= half; ++iz)
          {
            std::string row;
            for (int ix = -half; ix <= half; ++ix)
            {
              const float a[3] = {sx + float(ix) * cell, sy + 250.0f, sz + float(iz) * cell};
              const float b[3] = {a[0], sy - 900.0f, a[2]};
              float at[3] = {};
              row += CamRay(ctx, base, mask, a, b, at) ? fmt::format(" {:.0f}", at[1] - sy) : std::string(" .");
            }
            REXLOG_INFO("pdz_counterop: spider: survey z {:.0f}:{}", sz + float(iz) * cell, row);
          }
        }
      }
    }
    // Round 13 (b): once a second while his stick is held (pdz_counterop_spider_log): what the bot stands on.
    if (flat && stick > 0.5f && now - g_drive.flat_status_at >= 1000 && REXCVAR_GET(pdz_counterop_spider_log))
    {
      g_drive.flat_status_at = now;
      REXLOG_INFO("pdz_counterop: spider: flat lock: stick held ({:.2f}, {:.2f}): {:08X} at ({:.0f}, {:.0f}, {:.0f}), up "
                  "({:.2f}, {:.2f}, {:.2f}), the surface under it {} normal ({:.2f}, {:.2f}, {:.2f}) (a floor needs up "
                  "part {:.2f}); speed the way he asks {:.0f} of {:.0f}; so far: {} move(s) onto a steep surface "
                  "refused in Rare's mover, {} push(es) against something without a step, {} edge hold(s), {} step(s) "
                  "down, {} time(s) brought down",
                  g_drive.x, g_drive.y, bot, f.pos[0], f.pos[1], f.pos[2], f.up[0], f.up[1], f.up[2],
                  attached ? (rare_n_ok ? "has the" : "has no readable") : "is not there (no hit):", rare_n[0], rare_n[1],
                  rare_n[2], flat_cos, along, speed, g_drive.probe_refused, g_drive.flat_refused, g_drive.edge_refused,
                  g_drive.drops, g_drive.downs);
    }
    // his view: a low follow camera behind and a little over the bot, in the bot's own frame, eased
    const double cam_ease_s = std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_cam_ease_s), 0.0, 1.0);
    if (cam_ease_s > 0.0)
    {
      // Round 12 (item 3). The camera hangs on a point that follows the bot through a critically damped spring
      // (exact step for the measured frame time: the same motion at 60 and 120 fps), so a bot that shakes (Rare's
      // jump held against a closed door moved it to and fro every frame, log 013 22:56:24-28) no longer shakes the
      // picture; its axes ease by the clock too (the heading over the same time, the up axis over
      // pdz_counterop_spiderbot_cam_tilt_s). Then one of Rare's physics rays from just over that point to the
      // camera's place: the camera stops 20 cm short of what the ray hits (at once), and goes back out eased.
      const double t = NowSeconds();
      const float dt = std::clamp(g_drive.cam_t > 0.0 ? float(t - g_drive.cam_t) : 0.016f, 0.001f, 0.1f);
      g_drive.cam_t = t;
      float gap2 = 0.0f;
      for (uint32_t i = 0; i < 3; ++i)
      {
        gap2 += (f.pos[i] - g_drive.a_pos[i]) * (f.pos[i] - g_drive.a_pos[i]);
      }
      if (!g_drive.cam_init || !g_drive.view || !(gap2 < 400.0f * 400.0f))
      {
        // the first frame of a drive, or the bot was put somewhere else (back where it last stood): no glide
        const bool first = !g_drive.cam_init;
        for (uint32_t i = 0; i < 3; ++i)
        {
          g_drive.a_pos[i] = f.pos[i], g_drive.a_vel[i] = 0.0f;
          g_drive.s_up[i] = f.up[i], g_drive.s_fwd[i] = f.fwd[i];
        }
        g_drive.cam_frac = 1.0f;
        g_drive.cam_init = true;
        if (first)
        {
          REXLOG_INFO("pdz_counterop: spider: camera: eased chase camera for {:08X}: follow {:.2f} s, tilt {:.2f} s, "
                      "wall ray mask {} (0 = off), tilt with the surface {}; left stick {}, sideways {}",
                      bot, cam_ease_s, REXCVAR_GET(pdz_counterop_spiderbot_cam_tilt_s),
                      int32_t(REXCVAR_GET(pdz_counterop_spiderbot_cam_wall_mask)),
                      REXCVAR_GET(pdz_counterop_spiderbot_cam_roll) ? "on" : "off",
                      REXCVAR_GET(pdz_counterop_spiderbot_bot_relative) ? "in the bot's own frame"
                                                                        : "laid in from the camera (round 11)",
                      REXCVAR_GET(pdz_counterop_spiderbot_side_flip) ? "flipped (right = its right)" : "as round 11");
        }
      }
      else
      {
        // slower while Rare's jump or fall has the bot (the leap is fast and may end against a door)
        const float w = float(4.0 / cam_ease_s) * (jumping || falling ? 0.5f : 1.0f);
        const float e = std::exp(-w * dt);
        const float tilt_s = float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_cam_tilt_s), 0.01, 2.0));
        const float kf = 1.0f - std::exp(-dt * 3.0f / float(cam_ease_s)), ku = 1.0f - std::exp(-dt * 3.0f / tilt_s);
        for (uint32_t i = 0; i < 3; ++i)
        {
          const float j0 = g_drive.a_pos[i] - f.pos[i], j1 = g_drive.a_vel[i] + w * j0;
          g_drive.a_pos[i] = f.pos[i] + (j0 + j1 * dt) * e;
          g_drive.a_vel[i] = (g_drive.a_vel[i] - j1 * w * dt) * e;
          g_drive.s_up[i] += (f.up[i] - g_drive.s_up[i]) * ku;
          g_drive.s_fwd[i] += (f.fwd[i] - g_drive.s_fwd[i]) * kf;
        }
      }
      float u[3] = {g_drive.s_up[0], g_drive.s_up[1], g_drive.s_up[2]};
      if (!Norm3(u)) // the old and the new up were opposite: take the new one
      {
        for (uint32_t i = 0; i < 3; ++i)
        {
          u[i] = g_drive.s_up[i] = f.up[i];
        }
      }
      float v[3];
      const float vu = Dot3(g_drive.s_fwd, u);
      for (uint32_t i = 0; i < 3; ++i)
      {
        v[i] = g_drive.s_fwd[i] - u[i] * vu; // the eased heading, at right angles to the eased up
      }
      if (!Norm3(v))
      {
        const float fu = Dot3(f.fwd, u);
        for (uint32_t i = 0; i < 3; ++i)
        {
          v[i] = f.fwd[i] - u[i] * fu;
          g_drive.s_fwd[i] = f.fwd[i];
        }
        if (!Norm3(v))
        {
          v[0] = f.fwd[0], v[1] = f.fwd[1], v[2] = f.fwd[2];
        }
      }
      const float back = float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_cam_back_cm), 0.0, 400.0));
      const float over = float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_cam_up_cm), 0.0, 300.0));
      const float near_up = std::min(over, 30.0f);
      float from[3], place[3], dir[3];
      for (uint32_t i = 0; i < 3; ++i)
      {
        from[i] = g_drive.a_pos[i] + u[i] * near_up;             // just over the bot: where the ray starts
        place[i] = g_drive.a_pos[i] + u[i] * over - v[i] * back; // where the camera wants to be
        dir[i] = place[i] - from[i];
      }
      const float reach = std::sqrt(Dot3(dir, dir));
      float want = 1.0f;
      const uint32_t mask = uint32_t(REXCVAR_GET(pdz_counterop_spiderbot_cam_wall_mask));
      if (mask && reach > 5.0f && std::isfinite(reach))
      {
        constexpr float kMargin = 20.0f; // the near plane stays out of the wall
        float to[3], hit[3];
        for (uint32_t i = 0; i < 3; ++i)
        {
          to[i] = place[i] + dir[i] / reach * kMargin;
        }
        if (CamRay(ctx, base, mask, from, to, hit))
        {
          const float hx = hit[0] - from[0], hy = hit[1] - from[1], hz = hit[2] - from[2];
          const float hd = std::sqrt(hx * hx + hy * hy + hz * hz);
          if (hd < 6.0f)
          {
            // a hit at the ray's own start is the bot itself (or the ray starts inside something): not a wall
            if (g_drive.cam_self++ == 0)
            {
              REXLOG_INFO("pdz_counterop: spider: camera: the wall ray (mask {}) hits {:.1f} cm from its start over the "
                          "bot: taken for the bot itself, ignored (if the camera goes through walls, this is why)",
                          mask, hd);
            }
          }
          else
          {
            want = std::clamp((hd - kMargin) / reach, 0.0f, 1.0f);
            if (want < 0.98f && g_drive.cam_frac >= 0.98f && g_drive.cam_pulls++ < 6)
            {
              REXLOG_INFO("pdz_counterop: spider: camera: something is between the bot and the camera's place ({:.0f} "
                          "cm along a {:.0f} cm reach, ray mask {}): the camera is pulled in to {:.0f} % of the way",
                          hd, reach, mask, want * 100.0f);
            }
          }
        }
      }
      // in at once (never inside a wall), out again eased over about 0.3 s
      g_drive.cam_frac = want < g_drive.cam_frac
                             ? want
                             : g_drive.cam_frac + (want - g_drive.cam_frac) * (1.0f - std::exp(-dt / 0.1f));
      for (uint32_t i = 0; i < 3; ++i)
      {
        g_drive.cam[i] = from[i] + dir[i] * g_drive.cam_frac;
        g_drive.look[i] = g_drive.a_pos[i] + u[i] * 20.0f + v[i] * 200.0f;
        g_drive.up[i] = u[i];
      }
      g_drive.view = true;
    }
    else // round 11's camera (pdz_counterop_spiderbot_cam_ease_s 0)
    {
      const float k = 0.18f;
      float su = 0.0f, sf = 0.0f;
      for (uint32_t i = 0; i < 3; ++i)
      {
        g_drive.s_up[i] += (f.up[i] - g_drive.s_up[i]) * (g_drive.view ? k : 1.0f);
        g_drive.s_fwd[i] += (f.fwd[i] - g_drive.s_fwd[i]) * (g_drive.view ? k : 1.0f);
        su += g_drive.s_up[i] * g_drive.s_up[i];
        sf += g_drive.s_fwd[i] * g_drive.s_fwd[i];
      }
      su = std::sqrt(su), sf = std::sqrt(sf);
      if (su > 0.05f && sf > 0.05f && std::isfinite(su) && std::isfinite(sf))
      {
        const float back = float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_cam_back_cm), 0.0, 400.0));
        const float over = float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_cam_up_cm), 0.0, 300.0));
        for (uint32_t i = 0; i < 3; ++i)
        {
          const float u = g_drive.s_up[i] / su, w = g_drive.s_fwd[i] / sf;
          g_drive.cam[i] = f.pos[i] + u * over - w * back;
          g_drive.look[i] = f.pos[i] + u * 20.0f + w * 200.0f;
          g_drive.up[i] = u;
        }
        g_drive.view = true;
      }
    }
    if (fire && !jumping && !falling && !g_drive.fired)
    {
      if (base[bot + kDetonator])
      {
        const float before = AgentHealth(base, agent);
        const bool gone = Detonate(ctx, base, bot);
        g_drive.fired = true;
        REXLOG_INFO("pdz_counterop: spider: player 2 set his yellow-eyed spiderbot {:08X} off {:.1f} m from the agent "
                    "(Rare's death call 825C8388 -> 825C7CC8): exploded flag {}; the agent's health {:.1f} -> {:.1f} "
                    "across the call",
                    bot, agent_m, int(gone), before, AgentHealth(base, agent));
      }
      else if (now - g_drive.last_pounce > 700)
      {
        // Rare's own pounce target is the target's position and up axis within 2.5 m; his is the agent when she is
        // that near and ahead, else a point 2.5 m ahead on his own surface.
        float dest[3], normal[3];
        float to[3], ahead = 0.0f;
        for (uint32_t i = 0; i < 3; ++i)
        {
          to[i] = LoadFloat(base, agent + kPos + 4 * i) - f.pos[i];
          ahead += to[i] * f.fwd[i];
        }
        const bool at_agent = agent_m >= 0.0f && agent_m < 3.5f && ahead > 0.0f;
        for (uint32_t i = 0; i < 3; ++i)
        {
          dest[i] = at_agent ? LoadFloat(base, agent + kPos + 4 * i) : f.pos[i] + f.fwd[i] * 250.0f;
          normal[i] = f.up[i];
        }
        // Round 16 (item 3): at the agent, the leap aims at her body as she is now and follows her (ClosePounce).
        const bool close_rule = at_agent && REXCVAR_GET(pdz_counterop_spiderbot_close_pounce);
        float feet[3] = {dest[0], dest[1], dest[2]};
        if (close_rule)
        {
          float aim[3];
          if (AgentAim(ctx, base, agent, aim))
          {
            std::memcpy(dest, aim, sizeof(dest));
          }
        }
        std::string why, fixed;
        bool ok = StartPounce(ctx, base, bot, dest, normal, scratch + 0x40, why);
        if (!ok && EndStaleTask(ctx, base, bot, fixed))
        {
          // Round 15: a stale jump / fall task was ended: one more try.
          std::string why2;
          ok = StartPounce(ctx, base, bot, dest, normal, scratch + 0x40, why2);
          why += " | then " + fixed + (ok ? ": the retry started" : ": the retry was refused too: " + why2);
        }
        g_drive.last_pounce = now;
        g_drive.pounces += ok ? 1 : 0;
        ++g_drive.presses;
        REXLOG_INFO("pdz_counterop: spider: player 2's pounce #{} (press {} with this bot) with spiderbot {:08X} (Rare's jump "
                    "task 821B1A00): {}; {} ({:.1f} m from the agent, her health {:.1f}){}",
                    g_drive.pounces, g_drive.presses, bot, ok ? "started" : "not started", at_agent ? "at the agent" : "2.5 m ahead",
                    agent_m, AgentHealth(base, agent), why.empty() ? std::string() : " | why: " + why);
        if (ok && close_rule)
        {
          g_cp = ClosePounce{};
          g_cp.task = Load32(base, bot + kJumpTask);
          g_cp.bot = bot;
          g_cp.number = g_drive.pounces;
          g_cp.start_m = agent_m;
          g_cp.phase = Ptr(g_cp.task) ? int32_t(Load32(base, g_cp.task + kJumpPhase)) : 0;
          g_cp.at = now;
          if (g_cp_lines++ < 400)
          {
            REXLOG_INFO("pdz_counterop: spider: close pounce #{} with {:08X}: aimed at the agent as she is now ({:.0f}, "
                        "{:.0f}, {:.0f}) = her feet ({:.0f}, {:.0f}, {:.0f}) + {:.0f} cm (Rare's own aim: her feet at "
                        "the press), {:.1f} m away; the destination follows her for the whole leap; contact reach "
                        "{:.1f} m (her reach sphere radius {:.0f}, the bot's size {:.0f}) "
                        "(pdz_counterop_spiderbot_close_pounce)",
                        g_cp.number, bot, dest[0], dest[1], dest[2], feet[0], feet[1], feet[2], AimCm(), agent_m,
                        ReachM(), LoadFloat(base, agent + 0x3C), LoadFloat(base, bot + 60));
          }
        }
      }
    }
    if (now - g_drive.last_line >= 1000)
    {
      g_drive.last_line = now;
      const uint32_t polls = counter_op_pad::PollCount(uint32_t(pad));
      const uint32_t game_polls = polls - g_drive.polls_at_line - g_drive.own_polls;
      REXLOG_INFO("pdz_counterop: spider: pad: player 2's pad {} this second: polled {} time(s) by the game and {} by "
                  "the drive (last result {:08X}: {}); REAL sticks left ({:.2f}, {:.2f}) right ({:.2f}, {:.2f}), "
                  "buttons {:05X} (0x20000 = right trigger, 0x2000 = B)",
                  pad, int32_t(game_polls), g_drive.own_polls, counter_op_pad::PollResult(uint32_t(pad)),
                  counter_op_pad::PollResult(uint32_t(pad)) == 0 ? "connected" : "NOT CONNECTED", g_drive.raw[0],
                  g_drive.raw[1], g_drive.raw[2], g_drive.raw[3], g_drive.prev_buttons);
      g_drive.polls_at_line = polls;
      g_drive.own_polls = 0;
      REXLOG_INFO("pdz_counterop: spider: driving {:08X} ({}) at ({:.0f}, {:.0f}, {:.0f}), up ({:.2f}, {:.2f}, {:.2f}), "
                  "forward ({:.2f}, {:.2f}, {:.2f}); sticks ({:.2f}, {:.2f}) turn {:.2f}; measured local velocity "
                  "({:.0f}, {:.0f}, {:.0f}) yaw rate {:.2f}; {}{}{}; {:.1f} m from the agent, her health {:.1f}",
                  bot, base[bot + kDetonator] ? "yellow-eyed" : "ordinary", f.pos[0], f.pos[1], f.pos[2], f.up[0], f.up[1],
                  f.up[2], f.fwd[0], f.fwd[1], f.fwd[2], g_drive.x, g_drive.y, g_drive.turn,
                  Ptr(mover) ? LoadFloat(base, mover + kMoverVel) : 0.0f, Ptr(mover) ? LoadFloat(base, mover + kMoverVel + 4) : 0.0f,
                  vz, Ptr(mover) ? LoadFloat(base, mover + kMoverYaw) : 0.0f, g_drive.manual ? "his command" : "Rare's command",
                  jumping ? ", Rare's jump runs" : "", falling ? ", falling / off a surface" : "", agent_m,
                  AgentHealth(base, agent));
      if (falling)
      {
        const uint32_t ft = Load32(base, bot + kFallTask);
        REXLOG_INFO("pdz_counterop: spider: fall state of {:08X}: byte +3BC {}, byte +DC8 {}, byte +DEC {}, flags +40 "
                    "{:08X}, fall task {:08X} (vtable {:08X}, words +4 {:08X} +8 {:08X} +C {:08X} +10 {:08X}, byte +50 {})",
                    bot, base[bot + kDetached], base[bot + 0xDC8], base[bot + 0xDEC], Load32(base, bot + 0x40), ft,
                    Ptr(ft) ? Load32(base, ft) : 0, Ptr(ft) ? Load32(base, ft + 4) : 0, Ptr(ft) ? Load32(base, ft + 8) : 0,
                    Ptr(ft) ? Load32(base, ft + 0xC) : 0, Ptr(ft) ? Load32(base, ft + 0x10) : 0,
                    Ptr(ft) ? int(base[ft + 0x50]) : -1);
      }
    }
    return;
  }
  if (!on || !waiting || PdzMinigameRunning(agent))
  {
    return;
  }
  // The spiderbot section: Datacore after its cutscene `mid` (stage word 0x100), and a bot of Rare's near the agent.
  if (!(counter_op_guards::DatacoreStageWord(base) & 0x100u) ||
      now - g_drive.ended < int64_t(REXCVAR_GET(pdz_counterop_spiderbot_wait_s) * 1000.0))
  {
    return;
  }
  uint32_t best = 0;
  float best_m = float(REXCVAR_GET(pdz_counterop_spiderbot_range_m));
  // Round 12 (item 2): the floor rule's facts, and the state of the bots made for him (a new level starts it afresh).
  const bool floor_rule = REXCVAR_GET(pdz_counterop_spiderbot_floor_rule);
  const uint32_t script12 = DatacoreScriptOf(base);
  if (script12 != g_spawn.script)
  {
    g_spawn = SpawnState{};
    g_spawn.script = script12;
  }
  const float floor_cos =
      std::cos(float(std::clamp(REXCVAR_GET(pdz_counterop_spiderbot_floor_deg), 0.0, 89.0)) * 3.14159265f / 180.0f);
  uint32_t looked = 0, refused_now = 0;
  for (const auto &entry : g_bots)
  {
    const uint32_t bot = entry.first;
    if (now - entry.second > 500 || Load32(base, bot) != kSpiderVtable || base[bot + kDying] || base[bot + kExploded] ||
        Falling(base, bot) || Load32(base, bot + kJumpTask) || !Ptr(Load32(base, bot + kMover)) ||
        !Ptr(Load32(base, bot + kCtl)))
    {
      continue;
    }
    const int kind = REXCVAR_GET(pdz_counterop_test_spider_kind);
    if ((kind == 1 && !base[bot + kDetonator]) || (kind == 2 && base[bot + kDetonator]))
    {
      continue;
    }
    ++looked;
    // Round 13 (b), the flat lock: whatever pick rule is on, a bot is only given to him while Rare's own surface ray
    // has a hit under it, that surface is a floor (its normal within pdz_counterop_spiderbot_flat_deg of the world's
    // up) and the bot's up axis agrees. (Round 12's floor rule below tests the bot's up axis only.)
    if (REXCVAR_GET(pdz_counterop_spiderbot_flat))
    {
      const uint32_t mv = Load32(base, bot + kMover);
      const Frame3 bf = BotFrame(ctx, base, bot, scratch);
      float sn[3] = {LoadFloat(base, mv + kMoverNormal), LoadFloat(base, mv + kMoverNormal + 4),
                     LoadFloat(base, mv + kMoverNormal + 8)};
      const bool hit = Load32(base, mv + kMoverRecFlags) != 0 && Norm3(sn);
      const float sny = hit ? (Dot3(sn, bf.up) < 0.0f ? -sn[1] : sn[1]) : -1.0f;
      const float lim = FlatCos();
      if (!hit || sny < lim || bf.up[1] < lim)
      {
        ++refused_now;
        uint8_t &told = g_spawn.refused[bot];
        if (told != 5 && g_spawn.refusal_lines < 400)
        {
          told = 5;
          ++g_spawn.refusal_lines;
          REXLOG_INFO("pdz_counterop: spider: pick: flat lock: bot {:08X} is NOT given to player 2: it is on a wall or a "
                      "ceiling, or in the air (Rare's surface ray {}, the surface's normal up part {:.3f}, the bot's "
                      "up.y {:.3f}, a floor needs {:.3f}; {:.1f} m from the agent)",
                      bot, hit ? "has a hit" : "has NO hit", sny, bf.up[1], lim, Dist(base, bot, agent));
        }
        continue;
      }
    }
    // Round 12 (item 2): by where it stands and by Rare's own state. Log 013: bot 01447A00 stood on a floor (up.y
    // 0.991) with allies FFFF and was refused for 10 s by round 11's rule; 0163B680 / 014D6000 stood on a slope
    // (up.y 0.951 / 0.947) and were refused by its 0.985.
    if (floor_rule)
    {
      uint32_t slot_of = 0;
      const bool tank = TankBot(base, script12, bot);
      const bool active = !tank && SlotActive(base, script12, bot, &slot_of);
      const uint32_t mv = Load32(base, bot + kMover);
      const float up_y = BotFrame(ctx, base, bot, scratch).up[1];
      const bool floor = active && Load32(base, mv + kMoverRecFlags) != 0 && up_y >= floor_cos;
      const bool friend_near = floor && NearArmedFriend(base, bot, agent, p2c, 3.0f);
      const uint8_t reason = tank ? 1 : !active ? 2 : !floor ? 3 : friend_near ? 4 : 0;
      if (reason)
      {
        ++refused_now;
        uint8_t &told = g_spawn.refused[bot];
        if (told != reason && g_spawn.refusal_lines < 400)
        {
          told = reason;
          ++g_spawn.refusal_lines;
          REXLOG_INFO("pdz_counterop: spider: pick: bot {:08X} is NOT given to player 2: {} (spawner +0x{:X}, allies mask "
                      "{:04X}, the mover's last surface ray word {:08X}, up.y {:.3f} against {:.3f}, {:.1f} m from the "
                      "agent)",
                      bot,
                      reason == 1   ? "one of the glass tank's (Rare's script kills one that leaves its pane)"
                      : reason == 2 ? "Rare has it stashed (its spawner slot is not active)"
                      : reason == 3 ? "it does not stand on a floor"
                                    : "an armed friend of the agent is within 3 m of it",
                      slot_of, Load16(base, bot + kAllies), Load32(base, mv + kMoverRecFlags), up_y, floor_cos,
                      Dist(base, bot, agent));
        }
        continue;
      }
      g_spawn.refused.erase(bot);
      if (bot == g_spawn.bot)
      {
        best = bot; // the one made for him comes first
        best_m = Dist(base, bot, agent);
        break;
      }
    }
    // Round 11: only a bot Rare has released, standing on a floor, not within 3 m of an armed friend of the agent.
    else if (REXCVAR_GET(pdz_counterop_spiderbot_released_only))
    {
      const bool released = Released(base, bot);
      const bool floor = released && OnFloor(ctx, base, bot, scratch);
      const bool friend_near = floor && NearArmedFriend(base, bot, agent, p2c, 3.0f);
      if (!released || !floor || friend_near)
      {
        // (Logged once in 5 s: why no bot is given, with the values the tests read.)
        static int64_t said = 0;
        if (now - said > 5000)
        {
          said = now;
          const uint32_t mv = Load32(base, bot + kMover);
          REXLOG_INFO("pdz_counterop: spider: bot {:08X} is NOT given to player 2: {} (allies mask {:04X}, the mover's last "
                      "surface ray word {:08X}, up.y {:.3f}, detached {}, fall task {:08X}, jump task {:08X}); he waits",
                      bot,
                      !released ? "Rare has not released it (a set-piece bot: allies FFFF, or one of the glass tank's)"
                      : !floor  ? "it does not stand on a floor"
                                : "an armed friend of the agent is within 3 m of it",
                      Load16(base, bot + kAllies), Ptr(mv) ? Load32(base, mv + kMoverRecFlags) : 0,
                      BotFrame(ctx, base, bot, scratch).up[1], int(base[bot + kDetached]), Load32(base, bot + kFallTask),
                      Load32(base, bot + kJumpTask));
        }
        continue;
      }
    }
    const float d = Dist(base, bot, agent);
    if (d >= 0.0f && d < best_m)
    {
      best_m = d;
      best = bot;
    }
  }
  if (!best)
  {
    // Round 12 (item 2): no free floor bot. During Rare's battle one is made for him through Rare's own spawner; it
    // is given to him by the loop above once it stands on a floor (a bot of the drop-in spawner lands first).
    if (floor_rule && REXCVAR_GET(pdz_counterop_spiderbot_spawn) && script12)
    {
      const bool pending = g_spawn.bot && Load32(base, g_spawn.bot) == kSpiderVtable && !base[g_spawn.bot + kDying] &&
                           !base[g_spawn.bot + kExploded] && now - g_spawn.at < 12000;
      if (!pending && now - g_spawn.tried >= 2000)
      {
        g_spawn.tried = now;
        std::string why;
        uint32_t made = 0;
        // At most two bots made for him wait unused at a time (one that came to stand on a wall is never given):
        // the agent gets no swarm of ours.
        for (size_t i = g_spawn.unused.size(); i-- > 0;)
        {
          const uint32_t u = g_spawn.unused[i];
          const auto it = g_bots.find(u);
          if (it == g_bots.end() || now - it->second > 1500 || Load32(base, u) != kSpiderVtable || base[u + kDying] ||
              base[u + kExploded])
          {
            g_spawn.unused.erase(g_spawn.unused.begin() + static_cast<std::vector<uint32_t>::difference_type>(i));
          }
        }
        if (!BattleOn(base, script12))
        {
          why = "Rare's battle is over (the agent is at the platform top): no bot is made, the normal guard picking "
                "goes on";
        }
        else if (g_spawn.unused.size() >= 2)
        {
          why = "two bots made for him are still there and not usable (not on a floor, or by an armed friend)";
        }
        else
        {
          made = SpawnBotFor(ctx, base, script12, agent, why);
        }
        if (made)
        {
          g_spawn.bot = made;
          g_spawn.at = now;
          g_spawn.unused.push_back(made);
          g_bots[made] = now; // known at once (its first AI tick comes next frame)
          ++g_spawn.made;
          g_spawn.last_why.clear();
          REXLOG_INFO("pdz_counterop: spider: SPAWN #{}: no free floor bot for player 2 ({} bot(s) looked at, {} "
                      "refused): bot {:08X} made through {}; masks {:04X} / {:04X} / {:04X}, {}; he is given it when "
                      "it stands on a floor",
                      g_spawn.made, looked, refused_now, made, why, Load16(base, made + kTeam),
                      Load16(base, made + kAllies), Load16(base, made + kEnemies),
                      base[made + kDetonator] ? "yellow-eyed" : "ordinary");
        }
        else if (why != g_spawn.last_why)
        {
          g_spawn.last_why = why;
          REXLOG_INFO("pdz_counterop: spider: SPAWN: no spiderbot made for player 2 ({} bot(s) looked at, {} refused): "
                      "{}; he waits",
                      looked, refused_now, why);
        }
      }
    }
    return;
  }
  // Round 12: a set-piece bot Rare has not released yet (allies FFFF: it counts everyone but its script target as a
  // friend) is released for him exactly as Rare's script releases it (82301940 / 82301988: allies and enemies 0).
  if (floor_rule && Load16(base, best + kAllies) == 0xFFFFu)
  {
    const uint32_t was = Load16(base, best + kEnemies);
    base[best + kAllies] = 0, base[best + kAllies + 1] = 0;
    base[best + kEnemies] = 0, base[best + kEnemies + 1] = 0;
    REXLOG_INFO("pdz_counterop: spider: pick: bot {:08X} was a set-piece bot Rare had not released (allies FFFF, enemies "
                "{:04X}): released for player 2 as Rare's script does it (allies and enemies cleared)",
                best, was);
  }
  if (best == g_spawn.bot)
  {
    g_spawn.bot = 0;
  }
  g_spawn.unused.erase(std::remove(g_spawn.unused.begin(), g_spawn.unused.end(), best), g_spawn.unused.end());
  const uint32_t driven = g_drive.driven;
  g_drive = Drive{};
  g_drive.scratch = scratch;
  g_drive.driven = driven + 1;
  g_drive.bot = best;
  g_drive.since = now;
  g_drive.start_agent_health = AgentHealth(base, agent);
  float lx = 0.0f, ly = 0.0f;
  g_drive.prev_buttons = counter_op_pad::Polled(uint32_t(pad), lx, ly); // a trigger already held is not a press
  g_drive.polls_at_line = counter_op_pad::PollCount(uint32_t(pad));
  if (REXCVAR_GET(pdz_counterop_spiderbot_only_agent))
  {
    const uint32_t dropped = DropTargetsOn(ctx, base, best);
    REXLOG_INFO("pdz_counterop: spider: {} target holder(s) that held his new bot {:08X} cleared; from now on nobody "
                "who would take it for an enemy sees it",
                dropped, best);
  }
  REXLOG_INFO("pdz_counterop: spider: player 2 drives Rare's spiderbot {:08X} (#{} this level, {}), {:.1f} m from the "
              "agent (masks {:04X} / {:04X} / {:04X}): his sticks move it through Rare's mover, his "
              "trigger is Rare's {}; every other spiderbot stays Rare's",
              best, g_drive.driven, base[best + kDetonator] ? "yellow-eyed: a detonator" : "ordinary: it pounces", best_m,
              Load16(base, best + kTeam), Load16(base, best + kAllies), Load16(base, best + kEnemies),
              base[best + kDetonator] ? "blast" : "pounce");
}
