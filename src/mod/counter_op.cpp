// Counter-Operative (pdz_counterop, default off): docs/COUNTEROP_STUDY.md stages 0-2 and the minimum of stage 3.
//
// A normal split-screen co-op mission (game type 1 kept, so every co-op path of Rare's keeps working). Player 1 is
// the agent. Player 2 is the counter-operative: Rare's co-op human, SPAWNED at a guard's spot (log 312 redesign: he
// no longer takes a live guard's body), with that guard's gun and on the guards' side through Rare's own team masks
// (study section 5, option b): player 2's team bits (+128) = 0x0001 (ALPHA, which every SP guard counts among its
// allies, 0xFFFF) and his enemy mask (+132) = 0x0002 (BETA, the humans' bit); his allies (+130) = 0xFFFF, so all
// three masks are a team-0 guard's (the NPC constructor 8252BD50..8252BD88 via 8252FC18). Rare's friend test
// (8251EBD0: (other +128) & (my +128 | +130) and not (other +128) & my +132; vtable +460 swaps the two while +5640 >
// 0) then makes every character treat him as it treats a team-0 guard: the guards' sense (8257F620) and target step
// (82580508) ignore him, the hit handler (82549040, co-op: friend(victim, attacker)) cancels hits between him and the
// guards, and he and the agent hurt each other (the agent's auto-aim filter 824EF8B8 takes him as a target). By
// default no guard is touched (pdz_counterop_take_guard off): a spot is a free setup position. When player 2 dies,
// Rare's co-op revive (824573A8: no prompt, it is the un-die: pad move, restore, get-up animation with an
// invulnerability timer) is held 2.5 s, then run with player 2 out of the world until Rare's get-up is over (death
// state +2584 = 0, +5928 clear, participant +928 <= 0; 8 s cap), then the next spot, facing the agent (825AD3B0).
// He can't pick up items (Rare's can-pick-up test 82586238 answers no for him). The agent down = Rare's co-op game
// over (82E426F8 |= 2, 8222FF58), as Rare does when every human is down; the agent is never revived. Stage 1: Rare's
// setup filter answers with the counter-op kind bit 0x40 instead of co-op's 0x20
// (sub_82279928's hook in cut_scenarios.cpp calls PdzCounterOpFilter), so Rare's own counter-op spawn set is used.
//
// Guard spots (logs 310-312): a level's guards are all built at load; the ones a level script shows later are
// hidden through Rare's script despawn (825818E0: out of the world list and physics, +6272 bit 0x10), so the world
// list holds only the live ones (the Jungle: 5-6 of 75 at the start). Rare's own script lookup by id (821794A8 ->
// 82178FF8) walks the object registry at [[game +28] +20] (+676 / +680: groups, each with a list at +16 / +20 of
// nodes {object, _, next}), which holds hidden objects too; the spots are enumerated from it (and the world list,
// as a cross-check). A spot is an SP guard (vtable 820C45A0), alive, not taken, eligible by its setup (character
// +84): the mission's id list (section 4), or the no-id rule (team 0 or a template clone, a combat type 48 and up
// that is not a non-combatant, a gun in hand, and the id not on the mission's denylist of named / scripted / boss /
// hostage / vehicle-crew characters: the study's excluded.md). Dormant (hidden) spots come first, then live guards;
// at least 15 m from the agent and off his screen when any qualify, relaxed step by step (logged); one of the three
// nearest qualifying at random. Player 2 is moved there (Rare's move 825AD148; a dormant guard's setup position +4,
// a live guard's +48), given the guard's gun type (Rare's give 8254C378, then full reserves through Rare's ammo
// adders 82583F80 / 82584118, which clamp to each type's maximum), and the guard is hidden (825818E0) and kept
// hidden if a script shows it later. "Guards left" = eligible spots not taken (dormant + live); after a death,
// 20 s with none = out of guards (player 2 spectates, the mission goes on). That is pdz_counterop_take_guard (off by
// default): a hidden guard is not a death (825818E0 only sets +6272 0x10 and takes it out of the world, physics and
// team lists), so a script waiting for its group would wait, and a re-hidden wave guard never arrives. Default: no
// guard is hidden or re-hidden; a spot is the setup position (+4) of an eligible guard, live (its area is active)
// first, then dead (a cleared area), then dormant, free when no character is within 1.2 m and not the last life's
// spot; the gun is that guard's (else the last life's), the health a fresh guard's (setup +256 ratio 1).
//
// Look (log 312: an SP guard body on the live human crashed Rare's weapon / animation update; log 311 before it was
// the give's ammo arguments): player 2 is BUILT as one of the mission's guards (body and head) the way Rare builds a
// player: the SP player factory 825B3D58 builds the co-op partner from his setup (+52 body, +56 head) and, in co-op,
// only while player 1 has no next human in Rare's list (+6792, 825B3E00); the hook on it (pdz_counterop_guard_look)
// writes the chosen body and head ids into the setup for the duration of Rare's call and restores them, and Rare's own
// human constructor (825A6B20) wires everything from them: body entry 82278788 -> +5264, 82414070(entry, 1) unless
// +68 is set, instance 82410A60 -> +72, 82546340; head entry 822787E0 -> +5268, 82414618 unless +12 is set, instance
// 82411048 on the body -> +4104, the hair bones -> +4852 / +4856 (-1 is normal for a helmeted head); the animator
// from the body entry's +64 (825464D0 -> 821E82A0); the face set from the setup's head id (8262C060 -> 8262C890).
// Body / head ids are the level's list numbering (= a guard setup's +244 / +248; log 313). The candidates are a
// per-mission table of the placed guards' (body, head) pairs, most common first (decoded from each level's setups;
// Datacore and Arena have none: the co-op look). Each is checked with Rare's own lookup (82413FD8, null instead of
// a crash) before anything is requested: the body in the list, its animation set (+64, from its +12 name at level
// load, 822788D8 -> 821EE240) one Rare builds humans with (the male "henchman" set or the female "jo" set), its
// model (+4) and ragdoll (+32, Jack's fallback) and their +8 (82414070 reads both unchecked), and the head in the
// head list with its model and +8 (82411048 reads it unchecked). Player hands: an SP guard body has none (+16 =
// "_noname_", and the loader's fallback, the trooper hands, ships only in Trinity Escape), so 82414070 stores 0 at
// +20 and the first-person hands (825B43E0, run when his participant attaches, 825A99D8) are never made: log 312's
// null hands. The chosen body's +20 then gets the co-op body's own player hands (the ones that level ships for
// him), before Rare's call when the body is already loaded and after it otherwise (the constructor's loader writes
// +20); nothing but 825B43E0 reads +20 (guards never do). Any check failing = the next pair; a head failing = the
// co-op head; nothing usable = the co-op look with no requests at all. Every step is a 'pdz_counterop: look:' line.
//
// Health (the N64 rule, player_spawn_anti: health = (host max damage - host damage) * 0.125, capped at a full bar,
// where a basic guard has 4 damage points and the bar is 1.0: a full guard = half a bar): player 2's health and max
// (participant +980, what the human's health getter 825A9408 returns as the max) = (the guard's health / its spawn
// health) x his co-op max x pdz_counterop_health_scale (0.5), floor 25. An NPC has no max-health field (its getter
// 8252D658 returns +120 for SP guards), so the spawn health is the setup's +256 (the value the guard constructor
// copies to +120; 100 for every placed Jungle guard but the one-hit opener 'sta1' = 1.0, log 312). Armour (+124)
// stays 0.
//
// Kept: player 2 out of the world (hidden, locked) and spectating while waiting (his camera follows the agent),
// cutscene handling (no picks during one), agent down = mission failed, team bits re-applied when reset.
// Proof lines: 'hit:' (82549040 probe: who hit whom, health before / after), 'pickup blocked', 'get-up', 'spawn (f)'
// (facing), 'the agent is down'. Not done (known gaps): level triggers / objectives / key items that are not
// weapon-type pickups still accept him, props he shoots still take damage, no HUD.
// Menu (counter_op_menu.cpp, pdz_counterop_menu): MISSION MODE's COUNTER OPERATIVE entry arms the mode for the
// local co-op games that follow (On() = pdz_counterop || PdzCounterOpArmed()); Datacore / Arena are refused there;
// the debriefing header names the result.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
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
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "counter_op_guards.h" // round 2: originals vs waves, forced wake, the view test, the pick
#include "counter_op_jetpack.h" // round 2: Trinity Escape's jetpack guards flown through Rare's player mount
#include "counter_op_twins.h" // round 13 d: Trinity Infiltration's twins (player 2 plays one in the boss fight)
#include "counter_op_mansion.h" // round 13 g: Mansion's last fight (player 2 plays Mai Hem)
#include "counter_op_river.h" // round 13 h: River's lakes (player 2 drives an enemy hovercraft)
#include "counter_op_levels13.h" // round 13 i: Laboratory placed-only, the plasma rifle's cloak, Bridge's end waves

#include "guest_function.h"
#include "lab_stealth.h" // pdz_lab_guard_stealth watches the same hit handler
#include "objective_bots.h"
#include "render/game_pace.h" // SpLevelName
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop, false, "Gameplay",
                    "Counter-Operative (docs/COUNTEROP_STUDY.md; off by default): in a split-screen co-op mission, "
                    "player 2 plays a guard against the agent (player 1). He spawns at a free guard spot (a live "
                    "guard's setup position first, away from the agent and off his screen when possible; no guard "
                    "is removed), with its gun, facing the agent, and the guards treat him as one of theirs; when "
                    "he dies he spawns at another spot once Rare's get-up is over, out of sight (no revive at the "
                    "pad); he can't pick up items. The agent down = mission failed (never revived). Datacore and "
                    "Arena are not supported (no placed guards). Set it before starting the mission (Rare's "
                    "counter-op spawn set is chosen at level load). One 'pdz_counterop:' line per event")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_guard_look, true, "Gameplay",
                    "Counter-Operative: build player 2 with one of the mission's guard bodies and heads (chosen when "
                    "the level loads, through Rare's own player factory; first-person hands are the co-op partner's); "
                    "off = the co-op partner's look")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_health_scale, 0.5, "Gameplay",
                      "Counter-Operative: player 2's health and max health on each spawn = (the guard's health / its "
                      "spawn health) x the player's max x this, floor 25. Default 0.5 = N64 Perfect Dark's rule "
                      "(0.125 per damage point of a 4-point guard: a full guard gives half a bar)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_take_guard, true, "Gameplay",
                    "Counter-Operative, N64's rule (default on): each spawn REPLACES one of the level's guards (one of "
                    "the nearest to the agent, off-screen and calm first): that guard is taken out of the level "
                    "(Rare's script despawn), player 2 appears where it stood with its gun and its share of health, "
                    "and when player 2 dies the guard counts as killed (Rare's death path), so the level's counts and "
                    "scripts move on as in co-op. Player 2 is never an extra guard; with no eligible guard left he "
                    "spectates. Off: he spawns at a free guard setup position and no guard is touched")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_clones, true, "Gameplay",
                    "Counter-Operative (default on): missions whose guards are all runtime clones of spawn templates "
                    "(Datacore, Arena: no placed guards, so no eligible id list) are played too: player 2 replaces the "
                    "clones the no-id rule accepts (a combat type with a gun in hand), and since their waves have no "
                    "fixed count he is never out for good there: with no clone in play he spectates until the next one "
                    "spawns. Off = those missions are refused on the menu and play as plain co-op (0.9.3 behaviour)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_block_pickups, true, "Gameplay",
                    "Counter-Operative: player 2 can't pick up weapons, ammo or other weapon-type items (Rare's "
                    "can-pick-up test answers no for him); his own guns come from his spots")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_agent_aim, false, "Testing",
                    "TESTING TOOL (counter-op): the agent's view is turned at player 2 every frame while he is in "
                    "the world (so an input script's trigger presses shoot him wherever he is, e.g. on a jetpack)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_hide_revive_message, true, "Gameplay",
                    "Counter-Operative: the agent's HUD doesn't show Rare's co-op message \"co-op player is in need of "
                    "medical attention! check your radar\" (text 0x165) while player 2 is down or choosing a guard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_fix_view, true, "Gameplay",
                    "Counter-Operative: for 4 s after each spawn, a leftover offset on player 2's gameplay camera "
                    "(Rare's death / get-up pull-back, third person) is cleared from 0.5 s on, so he is back in first "
                    "person")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_p2_warp, "", "Testing",
                      "TESTING TOOL (counter-op): \"x,y,z\" (cm): player 2 is moved there (Rare's move, as his spawn) "
                      "once each time the value changes, while he is in the world")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_fix_view_mode, 0, "Testing",
                     "Counter-Operative (experiment): after a respawn's get-up rescue, put back player 2's player object "
                     "(+0x19A4) words saved 5 s after his first spawn (first person): 1 = +0x44 (2 in first person, 4 "
                     "after a respawn), 2 = also +0x368 (the camera mode in use); 3 = instead release Rare's action "
                     "locks (8254D968). 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_view_diff, false, "Testing",
                    "TESTING TOOL (counter-op): log the words of player 2's character / participant / player object "
                    "that differ between 5 s after his first spawn and 5 s after each later one")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_take, "", "Testing",
                      "TESTING TOOL: player 2's picks take this guard id (e.g. s21a) whenever it is a usable spot")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_fix_out_freeze, true, "Gameplay",
                    "Counter-Operative (default on): while player 2 is out of the world his view follows the agent "
                    "through Rare's camera update, run once more for his camera. That extra call gets Rare's own "
                    "frame step (and his camera's three sway angles are put back in range if they ever left it). Off = "
                    "the old call, which passed on whatever the agent's camera update left in the step register: an "
                    "infinite or huge step made the angles infinite and Rare's angle wrap loop (82214E20) never end, "
                    "so the game froze (Nightclub start, the Jungle after Trinity Escape)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_own_death, true, "Gameplay",
                    "Counter-Operative (round 3, default on): player 2 never goes through Rare's co-op death (down, "
                    "revive prompt, get-up, death camera). He keeps Rare's own health floor (the one Datacore gives its "
                    "humans); the hit that takes him to it ends that life at once: he leaves the world, the guard he "
                    "replaced dies where he stood (Rare's kill), and he takes the next guard the way he took the first "
                    "(first person, in control). Off = round 2's path through Rare's revive")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_guard_weapons, true, "Gameplay",
                    "Counter-Operative (round 3, default on): on every spawn player 2's inventory is cleared (Rare's own "
                    "clear) and he gets only the replaced guard's weapon(s) with that weapon's default ammo carry; "
                    "anything else that turns up in his hands later (Rare's co-op loadout) is removed. Off = round 2: "
                    "the guard's gun added to the co-op loadout, full reserves")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_block_dpad, true, "Gameplay",
                    "Counter-Operative (default on, round 4): player 2's D-pad Up (Rare's put-the-weapon-away) and "
                    "D-pad Down (drop the weapon) do nothing while he plays a guard: he always holds his guard's "
                    "weapon. Off (testing): they work, and empty hands are refilled by the tick")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_ammo_scale, 2.0, "Gameplay",
                      "Counter-Operative: player 2's reserve ammo on each spawn, as a multiple of Rare's default carry "
                      "for the guard's weapon (1 = exactly one carry; he can't pick up ammo)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_hostile_only, true, "Gameplay",
                    "Counter-Operative (round 3, default on): player 2 only takes guards that are the agent's enemies by "
                    "Rare's own team masks, both ways (Rare's friend test: the guard doesn't count the agent as a friend "
                    "and the agent doesn't count the guard as one). Datacore's first-half guards are the agent's "
                    "friendlies and are never taken")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_team_check, true, "Gameplay",
                    "Counter-Operative (Fri 20 Nov night, default on): at each takeover player 2 gets the taken guard's "
                    "own three masks (team / allies / enemies, Rare's per-level values, as before), and they are then "
                    "checked against the agent's with Rare's friend rule: should either still count the other a "
                    "friend, his masks get one bit more the way Rare turns a guard (a bit the agent counts as an "
                    "enemy, or one outside her side, that the guards still count as theirs) and her team bits among "
                    "his enemies. Logged at every takeover ('team: takeover'), and every 2 s Rare's own friend test "
                    "and the agent's auto-aim filter are asked about him (a line only when one says friend / refuses "
                    "him). Off = the guard's masks unchecked, no lines")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_solo_setups, true, "Gameplay",
                    "Counter-Operative (round 5, default on): the level is built with its single-player setups (Rare's "
                    "'solo only' records: Jack on Rooftops, the CI squad of Outpost, Laboratory's ladder and lift "
                    "switch, Temple's door) as well as Rare's counter-op ones; 'co-op only' records stay out. Off = "
                    "round 4 (those records missing)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_pos_log, false, "Testing",
                    "TESTING TOOL (counter-op): every 2 s, log both players' pads and positions (which pad moves whom)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_seek, false, "Testing",
                    "TESTING TOOL (counter-op, with pdz_test_pad_driver): the two script pads' sticks and the agent's "
                    "trigger are driven so the players meet: player 2's pad turns him towards the agent (right stick) "
                    "and walks him forward (left stick); the agent's pad walks her towards him and pulls the trigger "
                    "only while she has a clear line to him within 11 m. Nothing is written to either character: the "
                    "values go out through the pads, as a player's thumbs would")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_prompt_tour, false, "Testing",
                    "Counter-Operative (testing): once per level, 6 s after player 2's first spawn, a test tool moves "
                    "him in front of the nearest locked door, the nearest hackable prop and the nearest unlocked door "
                    "(four sides each, 1.5 s a side), logs every prompt Rare asks for him there and whether it was "
                    "denied, then moves him back (item 6: a script can't walk him to them)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_p2_attack, 0, "Testing",
                     "Counter-Operative (testing, with pdz_counterop_test_seek): 1 = player 2's view is turned at the "
                     "agent and his script pad fires at her with a clear line inside 14 m; 2 = the same and the agent "
                     "holds her fire (so he can kill her). The shots, hits and damage are the game's")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_p2_aim_friend, false, "Testing",
                    "TESTING TOOL (counter-op, round 8): while on, player 2's view is turned at the nearest live "
                    "guard within 25 m every frame (an input script's 'p2' trigger presses then shoot a guard of "
                    "his own side: the friendly fire test)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_max_lives, 0, "Testing",
                     "TESTING TOOL (counter-op): cap player 2's lives (replaced guards) at this many, so a test reaches "
                     "'no eligible guard left' quickly; 0 = off (the mission's own count)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_kill_p2, 0, "Testing",
                     "TESTING TOOL (counter-op): each time this goes up by one (when player 2 is alive and in the "
                     "world), player 2 is put 3 m in front of the agent, facing him, with 0.5 health, so the agent's "
                     "next hit (an input script's trigger presses) kills him through Rare's own death path")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_kill_companion, false, "Testing",
                    "TESTING TOOL (counter-op): when this turns on, the agent's single-player companions in the "
                    "level (Jack on Rooftops) are killed through Rare's kill, credited to player 2")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_kill_agent, false, "Testing",
                    "TESTING TOOL (counter-op): when this turns on, the agent (player 1) gets 0.5 health once, so the "
                    "next hit kills him through Rare's own death path (the end of a counter-op game; run without "
                    "pdz_test_god_mode)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_hold_in_minigame, true, "Gameplay",
                    "Counter-Operative (default on, round 6): player 2 is never given a guard while the agent is in a "
                    "hacking minigame (Data Thief, demo kit, Locktopus); his next guard waits until she leaves it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_minigame, false, "Testing",
                    "TESTING TOOL (counter-op): while on, the agent counts as being in a hacking minigame (no script "
                    "can walk her to a hack): player 2's next guard is held")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_datacore_late_elites, false, "Testing",
                    "TESTING TOOL (counter-op, Datacore): the two server-room elites can still be taken after the "
                    "hack (so a test that starts from the mid-level checkpoint can give player 2 one life, and with "
                    "it a camera, before the spiderbot section)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_no_restart_on_exit, true, "Gameplay",
                    "Counter-Operative (default on, round 8): once a level is left for the menus (ended, quit from "
                    "the pause menu, mission select) nothing of Counter-Op runs on it any more until the next level "
                    "starts. Off = rounds 6-7: the tick could start again on the level still loaded behind the menu")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_restart_keeps, true, "Gameplay",
                    "Counter-Operative (Sun 22 Nov, default on): the pause menu's Restart mission keeps Counter-Op. The "
                    "game passes through its menus for a restart, which read as 'the level was left' and stopped "
                    "Counter-Op for good (the level came back as plain co-op). Now, when the game sets up a level again "
                    "after that, Counter-Op starts again on it (teams, player 2 waiting, his picks). Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Sun 22 Nov (round 17, item 9; the owner's decision of 11:10 / 11:13: AUTOMATIC only, no give-up button).
// Sun 22 Nov (round 18, item 4): log 046 18:58:42 / 19:01:07, the picks after a door the agent locked took guards on the
// locked-off side (s03e at once, s03c two minutes later at relax step 5): the area rule only knew her area, which spans
// both sides of that door.
REXCVAR_DEFINE_BOOL(pdz_counterop_pick_locked_doors, true, "Gameplay",
                    "Counter-Operative (Sun 22 Nov, default on): no pick (first spot, after a death, after a cut-off "
                    "move, every relax step) takes a guard behind a door the agent has locked (the far side of the "
                    "door from her, within pdz_counterop_locked_door_reach_m), nor a guard a cut-off move has just "
                    "handed back (pdz_counterop_handed_back_s); he waits longer instead. After a cut-off move the "
                    "guards of the area she is heading into come first. Off = round 17")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_locked_door_reach_m, 60.0, "Gameplay",
                      "Counter-Operative (pdz_counterop_pick_locked_doors): how far behind a locked door a guard counts "
                      "as locked off")
    .range(10.0, 1000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_handed_back_s, 300.0, "Gameplay",
                      "Counter-Operative (pdz_counterop_pick_locked_doors): seconds a guard handed back by a cut-off "
                      "move is not picked again")
    .range(0.0, 3600.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Sun 22 Nov (round 18, item 7): the owner, Temple: a cutscene moves the agent on (CamSpy's), player 2 is left behind and
// waits for rule (b)'s 8 s or rule (c)'s 90 s.
REXCVAR_DEFINE_INT32(pdz_counterop_test_after_cutscene, 0, "Testing",
                     "TESTING TOOL (counter-op, Sun 22 Nov): each change runs pdz_counterop_cutoff_after_cutscene's check "
                     "1 s later, as at the end of a cutscene")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_safe_handback, true, "Gameplay",
                    "Counter-Operative (Mon 23 Nov, default on): the guard player 2 replaced is given back to the "
                    "game's AI (after a cut-off move, a release, his death) only while it is still a live guard "
                    "object in the game's character list. A guard the level has torn down meanwhile is left out of "
                    "the world instead (no hand-back, no corpse). Off = as before (a close to the desktop)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spawn_clear_locks, true, "Gameplay",
                    "Counter-Operative (Mon 23 Nov, default on): when player 2 comes back into the world with a new "
                    "body, any of Rare's action lock counters still held from his last life (the jetpack's riding "
                    "lock after a death in the seat: no weapon, no Use) is put to 0. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_hide_at_once, true, "Gameplay",
                    "Counter-Operative (Mon 23 Nov, default on): player 2 is hidden and locked on the first frame of "
                    "the level, before the opening cutscene test lets the start be made, so Rare's co-op player 2 is "
                    "never seen at the co-op start position. Off = hidden at the start (up to 3 s later)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_cutoff_after_cutscene, true, "Gameplay",
                    "Counter-Operative (Sun 22 Nov, default on, pdz_counterop_cutoff): 1 s after a cutscene ends, if "
                    "player 2 is in another area than the agent's (nor her next) and more than "
                    "pdz_counterop_cutoff_section_min_m from her, he moves to a guard ahead at once (the picks' rules), "
                    "instead of waiting for the timers")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_cutoff, true, "Gameplay",
                    "Counter-Operative (Sun 22 Nov, default on): when the agent has clearly left player 2's part of the "
                    "level, he moves to a guard ahead by the normal pick rules, and the guard he was is handed back to "
                    "the game's AI alive where he stood (no guard is used up, no death counted). When: (a) a door "
                    "near her becomes locked and closed with him on the other side; (b) she enters a new area of "
                    "the level's table while he is in another one, not her next, and far from her, and he does not "
                    "reach her area in time; (c) he stays far from her without getting closer for a long while. "
                    "Never on small gaps of a normal chase. Off = he stays where he is")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_cutoff_door_s, 3.0, "Gameplay",
                      "Counter-Operative (pdz_counterop_cutoff (a)): seconds after a door near the agent locked with "
                      "player 2 on its other side before he moves (the door must still be locked and closed)")
    .range(0.0, 60.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_cutoff_section_s, 8.0, "Gameplay",
                      "Counter-Operative (pdz_counterop_cutoff (b)): seconds after the agent entered a new area, with "
                      "player 2 still in another one (not her next) and far from her, before he moves")
    .range(0.0, 120.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_cutoff_section_min_m, 25.0, "Gameplay",
                      "Counter-Operative (pdz_counterop_cutoff (b)): metres. Closer to the agent than this, player 2 is "
                      "following her and is never moved by (b)")
    .range(0.0, 500.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_cutoff_far_m, 80.0, "Gameplay",
                      "Counter-Operative (pdz_counterop_cutoff (c)): metres. Farther than this from the agent ...")
    .range(10.0, 2000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_cutoff_far_s, 90.0, "Gameplay",
                      "Counter-Operative (pdz_counterop_cutoff (c)): ... for this many seconds without getting 15 m "
                      "closer, player 2 moves to a guard ahead")
    .range(10.0, 3600.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_cutoff_door, 0, "Testing",
                     "TESTING TOOL (Counter-Op, Sun 22 Nov): each new non-zero value locks the closed door nearest the "
                     "agent (within 30 m; both lock bytes +0x4A0 / +0x4A1 = 1, as a door the level locks) and puts "
                     "player 2 4 m behind it, on the side away from her; the lock bytes are put back 20 s later")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_level_boundary, true, "Gameplay",
                    "Counter-Operative (default on, round 6): everything a Counter-Op game keeps is cleared when its "
                    "level is left for the menus and again before the next level's players are built. Off = rounds "
                    "1-5: cleared only by the next level's first tick (a second game in one session could crash)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_true_origin, true, "Gameplay",
                    "Counter-Operative (default on, round 6): player 2 is put at the guard's ORIGIN (Rare's position "
                    "getter: the feet) facing the guard's way, and the guard he was is put back at player 2's origin "
                    "and facing when he dies. Off = rounds 1-5: the bounding sphere's centre (+48, waist height) was "
                    "used as the position, so he dropped about 0.9 m at every takeover and the body popped up at "
                    "every death, and he faced the agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_kill_same_tick, true, "Gameplay",
                    "Counter-Operative (default on, round 6): when player 2 dies the guard he was is restored, moved "
                    "and killed in the same tick, so it is never drawn standing. Off = Rare's kill one tick later")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_kill_hit_type, true, "Gameplay",
                    "Counter-Operative (default on, round 6): the guard's death animation is chosen by Rare's own "
                    "hit-type chooser from where the agent stands (front / back / side). Off = always a head shot "
                    "from the front (rounds 1-5)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_cutscene_tail, true, "Gameplay",
                    "Counter-Operative (default on): a cutscene also counts while Rare's cutscene player steps and for "
                    "0.75 s after it ends, and player 2's waiting lines show only once the opening is settled: none of "
                    "them over a loading screen or a cutscene. Off = round 5's test (the player state alone)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(bool, pdz_counterop_datacore_first_half); // counter_op_guards.cpp (round 5, item 4)
REXCVAR_DECLARE(bool, pdz_counterop_datacore_riders);     // counter_op_guards.cpp (round 12, item 6)
bool PdzCounterOpArmed(); // counter_op_menu.cpp: chosen in MISSION MODE
void PdzCounterOpCompanionLog(const uint8_t *base); // counter_op_triggers.cpp (round 5)
void PdzCounterOpHandsLate(PPCContext &ctx, uint8_t *base, uint32_t token); // counter_op_hands.cpp (round 5)
bool PdzCounterOpScriptTypeFlip(); // counter_op_triggers.cpp (round 5): the game type reads solo inside a script call
extern bool g_level_over; // round 8 (item 2): the level is being left for the menus (defined with the level boundary)
namespace counter_op_pads
{
  void Enforce(uint8_t *base, uint32_t p1p, uint32_t p2p); // counter_op_pads.cpp (round 3, item 1)
}
// counter_op_view.cpp (round 2, how player 2 is presented):
bool PdzCounterOpRareDark(PPCContext &ctx, uint8_t *base, int view, int32_t pad); // round 4 (item 7), counter_op_view.cpp
void PdzCounterOpSpiderHud(PPCContext &ctx, uint8_t *base, bool driving, int32_t pad); // round 11, counter_op_view.cpp
void PdzCounterOpWinNow(PPCContext &ctx, uint8_t *base, int32_t pad, int result); // round 6 (item 4), counter_op_view.cpp
void PdzCounterOpViewReset();                    // round 6 (item 1), counter_op_view.cpp
void PdzCounterOpWinRoundOver();                 // round 10 (item 2), counter_op_view.cpp
void PdzCounterOpHandsLend(PPCContext &ctx, uint8_t *base, uint32_t entry, uint32_t hands); // counter_op_hands.cpp
void PdzCounterOpSpiderTick(PPCContext &ctx, uint8_t *base, uint32_t agent); // counter_op_spider.cpp (round 7)
void PdzCounterOpTestLevelMessage(PPCContext &ctx, uint8_t *base, uint32_t agent); // counter_op_spider.cpp (Sun 22 Nov)
void PdzCounterOpSpiderDrive(PPCContext &ctx, uint8_t *base, uint32_t agent, uint32_t p2c, int32_t pad, bool waiting,
                             uint32_t scratch);
bool PdzCounterOpSpiderDriving();
bool PdzCounterOpSpiderView(float eye[3], float look[3]);
bool PdzCounterOpSpiderCamera(float cam[3], float look[3], float up[3], bool &roll); // round 9
void PdzCounterOpSpiderReset();
bool PdzMinigameRunning(uint32_t holder);        // minigame_log.cpp: a hacking minigame's update ran in the last 0.5 s
int PdzCounterOpPvpLastPart();                   // round 6 (item 6), counter_op_pvp.cpp
void PdzCounterOpRules13Reset();                 // round 13 a, counter_op_vehicles.cpp (counter_op_rules13.h)
void PdzCounterOpRules13Tick(PPCContext &ctx, uint8_t *base, bool playing); // round 13 a, counter_op_allies.cpp
void PdzCounterOpMansionTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, uint32_t current_guard, bool out, bool cutscene); // round 13 g, counter_op_mansion.cpp
// round 13 f, counter_op_outpost.cpp (Outpost's last fight: the gun of Mai Hem's hovercraft); all return at once elsewhere
void PdzCounterOpOutpostTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, bool out, bool cutscene);
void PdzCounterOpOutpostReset();
bool PdzCounterOpOutpostGunner(const uint8_t *base, uint32_t guard);
bool PdzCounterOpOutpostOnGun();
void PdzCounterOpOutpostGunHit(bool on); // round 14: counter_op_outpost.cpp
bool PdzCounterOpOutpostFight();
void PdzCounterOpOutpostAgentHit(const uint8_t *base, uint32_t attacker, float before, float after, float armour_before,
                                 float armour_after);
void PdzCounterOpOutpostUnseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);
void PdzCounterOpOutpostAfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);
void PdzCounterOpOutpostBeforeHide(PPCContext &ctx, uint8_t *base, uint32_t p2);
// round 13b q, counter_op_turrets.cpp (player 2 at a stationary turret); all return at once when he is at none
void PdzCounterOpTurretsTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, bool out, bool cutscene);
void PdzCounterOpTurretsReset();
bool PdzCounterOpTurretOnGun(const uint8_t *base);
void PdzCounterOpTurretBeforeHide(PPCContext &ctx, uint8_t *base, uint32_t p2, const char *why);
void PdzCounterOpTurretUnseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);
void PdzCounterOpTurretAfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);
void PdzCounterOpRooftopsTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t current_guard, bool out, bool cutscene); // round 13 e, counter_op_rooftops.cpp
void PdzCounterOpRooftopsReset();                // round 13 e
uint32_t PdzCounterOpRooftopsDropsLeft();        // round 13 e: 1 while Rooftops' last fight runs (Killian's ship can still drop him a body)
void PdzCounterOpObjectiveReset();               // round 6 (item 7), counter_op_objectives.cpp
void PdzCounterOpObjectiveLife(PPCContext &ctx, uint8_t *base, uint32_t p2c);
bool PdzCounterOpTriggersReset(uint8_t *base);   // round 6 (item 1), counter_op_triggers.cpp
void PdzCounterOpViewSet(int view); // every level tick: 0 his view, 1 dark 'Choosing a guard...', 2 dark 'No guards left'
bool PdzCounterOpViewDark();        // pdz_counterop_dark_view
bool PdzCounterOpWinScreens(PPCContext &ctx, uint8_t *base, bool on, int32_t pad); // counter_op_view.cpp (round 5)
void PdzCounterOpHudLevelEnd(); // counter_op_hud.cpp (round 4, item 5)
std::string PdzCounterOpAnimSummary(); // counter_op_anim.cpp (round 5, item 0)
void PdzCounterOpAnimWatch(const uint8_t *base, uint32_t p2c);
void PdzCounterOpAnimProbe(PPCContext &ctx, uint8_t *base, uint32_t p2c);
bool PdzCounterOpPvpOn();       // counter_op_pvp.cpp (round 4, item 1)
void PdzCounterOpHudWatch(uint32_t prop);          // counter_op_hud.cpp (testing: the prompt tour)
void PdzCounterOpHudWatchRead(uint32_t out[5]);
void PdzCounterOpViewRestorePerception(PPCContext &ctx, uint8_t *base, uint32_t character); // ShowP2's missing step (perception / noise slot)
// Round 12 (item 5), counter_op_view.cpp: the sky / fog / weather / lighting of player 2's view.
void PdzCounterOpEnvRefresh(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, const char *why); // a spawn / takeover
void PdzCounterOpEnvTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, bool out, bool cutscene); // every level tick
REXCVAR_DEFINE_BOOL(pdz_counterop_park_with_agent, false, "Gameplay",
                    "Counter-Operative (round 12, off by default: the look is fixed without it): while player 2 waits out of the world (and drives no "
                    "spiderbot) his hidden body is kept where the agent stands (moved there with Rare's move when it "
                    "is more than 6 m from her, at most every 2 s), so it is never left behind under water or in a "
                    "part of the level she has left. Off = round 11: it stays where he died")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
void PdzCounterOpHudOnNewBody(uint8_t *base); // counter_op_hud.cpp (round 16, item 2)
// counter_op_quick_Sat21Nov.cpp (Sat 21 Nov): the reviewed denied lists and the upright wait view.
const char *PdzCounterOpProvenDenied(const std::string &mission, const char **freed);
void PdzCounterOpWaitViewUpright(uint8_t *base, uint32_t mode, const float eye[3], uint32_t p2c);
double PdzCounterOpTestFirstPickS();
// counter_op_spectate_Sat21Nov.cpp (Sat 21 Nov, spectate job): Rare's spectator chase camera while player 2 waits.
bool PdzCounterOpRareSpectateTick(PPCContext &ctx, uint8_t *base, bool want, bool cutscene, uint32_t jo, uint32_t p2c);
void PdzCounterOpRareSpectateOff(PPCContext &ctx, uint8_t *base, const char *why);
bool PdzCounterOpRareSpectateActive();
void PdzCounterOpRareSpectateReset();
bool PdzCounterOpWaitViewUprightOn();
REXCVAR_DEFINE_BOOL(pdz_counterop_guard_armour, true, "Gameplay",
                    "Counter-Operative (round 16, default on): at every takeover player 2 gets the taken guard's own "
                    "armour, carried over like his health (the guard's armour against his health pool, on a bar of "
                    "100), and his bar is empty while he waits, so it never drops at a takeover. Off = round 15 "
                    "(armour 0 at every takeover, the co-op start's 100 visibly lost at the first)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_wait_follow, true, "Gameplay",
                    "Counter-Operative (round 16, default on): every wait for a body (AWAITING DEPLOYMENT) follows the "
                    "agent from behind, also after a checkpoint restart or a reload: when Rare has made no camera for "
                    "player 2 yet, his hidden body is put in the world for a moment under the agent's feet so that it "
                    "is made. Off = round 15 (after a reload his half showed a parked camera until his first body)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
uint32_t PdzCounterOpViewHands(PPCContext &ctx, uint8_t *base, uint32_t body_entry, std::string &why); // guard arms
// counter_op_hands.cpp (round 2): the hands probe (pdz_counterop_hands_probe) and the hands loader.
void PdzCounterOpHandsProbe(PPCContext &ctx, uint8_t *base, const char *where);
uint32_t PdzCounterOpLoadHands(PPCContext &ctx, uint8_t *base, uint32_t body_entry, std::string &why);
void PdzCounterOpReleaseBody(PPCContext &ctx, uint8_t *base, const char *why, bool at_once = false); // below (Sun 22 Nov: the cut-off rule too)
// Round 4 (items 3a, 3c): the per-body hands table and the re-dress at every spawn.
void PdzCounterOpHandsPreload(PPCContext &ctx, uint8_t *base);                                          // counter_op_hands.cpp
uint32_t PdzCounterOpHandsFor(PPCContext &ctx, uint8_t *base, uint32_t body_id, std::string &why);      // counter_op_hands.cpp
bool PdzCounterOpDress(PPCContext &ctx, uint8_t *base, uint32_t human, uint32_t body_id, uint32_t head_id,
                       uint32_t hands_asset, int32_t voice, std::string &why);                         // counter_op_dress.cpp
// COUNTER-OP GREET (counter_op_greet.cpp): guards greet player 2 with SEE_FRIEND_TEAM.
void PdzCounterOpGreetTick(PPCContext &ctx, uint8_t *base, uint32_t p2c);
void PdzCounterOpGreetReset();
// COUNTER-OP VOICE (counter_op_voice.cpp, round 4 item 3b): player 2 takes the replaced guard's voice at each spawn.
void PdzCounterOpVoiceSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2c, uint32_t guard, uint32_t setup);
void PdzCounterOpVoiceDeath(PPCContext &ctx, uint8_t *base, uint32_t p2c);
void PdzCounterOpVoiceReset();

REXCVAR_DEFINE_BOOL(pdz_counterop_no_closed_entrance, true, "Gameplay",
                    "Counter-Operative (default on, round 8): a guard who still stands at his setup position behind "
                    "a locked, closed door (woken early by the level script, waiting for his entrance) is not given "
                    "to player 2")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_quiet_takeover, true, "Gameplay",
                    "Counter-Operative (default on, round 8): at a takeover Rare's health display of player 2 is put "
                    "to rest with his new health, so the change of health and armour is not shown as a hit")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(sub_82268748); // Rare's health display: stop the low-health heartbeat sound (display r3)
REX_EXTERN(__imp__sub_8222F740); // co-op per-participant death states (every frame in a level, from 8222D4A8)
// Sun 22 Nov (item 7, pdz_counterop_wait_hud_hidden): the two crosshair draws (crosshair r3; the vtables 8209A17C /
// 820F4D58 +8 and the second class's own draw). Player 2's gun's crosshair is not drawn while he waits.
REX_EXTERN(__imp__sub_82263F08);
REX_EXTERN(__imp__sub_82264670);
REXCVAR_DEFINE_BOOL(pdz_counterop_wait_hud_hidden, true, "Gameplay",
                    "Counter-Operative (Sun 22 Nov, default on): while player 2 waits for his next body (following the "
                    "agent) his weapon HUD (weapon row, pick-up box, ammo bars), his gun's crosshair and his red damage "
                    "direction markers (and the health bar flash) are hidden; they come back with his next body. Off = "
                    "as before (his hidden body's crosshair and markers showed while he waited)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(__imp__sub_8222FD98); // co-op: game over? (every human down)
REX_EXTERN(__imp__sub_824573A8); // co-op revive (participant)
REX_EXTERN(__imp__sub_825B3D58); // the SP player factory: (setup) -> human (player 2's call once player 1 exists)
REX_EXTERN(sub_8222FF58);        // game over
REX_EXTERN(sub_825AD148);        // move a character (character, &position, 0)
REX_EXTERN(sub_8252DE88);        // guard vtable +0x2E4: Kill(guard, killer, death type, apply damage) - level scripts
                                 // call it on live guards (8231AD18, 821DBA2C, 822FFA78)
REX_EXTERN(sub_825AD0E0);        // Rare's script teleport (character, &position) -> 825AD148(c, pos, 1)
REX_EXTERN(sub_82550B08);        // guard vtable +0x2E8: die (stops weapon, animation and AI parts)
REX_EXTERN(sub_8252D6A8);        // guard vtable +0x158: set health (guard, f1, r5 0)
REX_EXTERN(sub_82151210);        // Rare's position getter (character) -> pointer to 3 floats: the origin at the FEET
REX_EXTERN(sub_82519A90);        // Rare's yaw getter (character) -> f1 radians
REX_EXTERN(sub_82519A38);        // Rare's yaw setter (character, f1 radians)
REX_EXTERN(sub_8225A5D8);        // Rare's hit-type chooser (out, body part, victim, &from position, &attacker forward)
REX_EXTERN(sub_82518E08);        // a character's forward vector (character, out 3 floats)
REX_EXTERN(sub_825800F8);        // guard vtable +4: Update(guard, f1 step)
REX_EXTERN(sub_825AD3B0);        // a human's look angles (character, f1 pitch, f2 yaw; radians, forward = (sin, cos) in
                                 // x / z): what Rare's spawn 825ADF80 calls after its move (825AE2A4 / 825AE2B4)
REX_EXTERN(__imp__sub_82586238); // can this character pick this item up? (item, character, four out bytes)
REX_EXTERN(__imp__sub_82549040); // a character's hit handler (victim, hit, attacker, ...): friend test, damage
REX_EXTERN(sub_8254C378);        // give an item (character, type, 0, 0, 0, 0)
REX_EXTERN(sub_8254BDF0);        // clear a character's whole inventory (character, 0 destroy / 1 drop into the world)
REX_EXTERN(sub_824F5C70);        // inventory: the first entry of a category (inventory, 0, category) ->
                                 // entry {+0 item, +4 category} or 0
REX_EXTERN(sub_8254C238);        // remove one item (character, item): unlink, out of either hand, destroy
REX_EXTERN(sub_82583F80);        // weapon: add primary reserve ammo (item, amount, 1 add), clamped to the type's max
REX_EXTERN(sub_82584118);        // weapon: add secondary reserve ammo (needs the entry's +4 definition)
REX_EXTERN(sub_825818E0);        // hide / restore a character (character, 1 hide): Rare's script despawn, guard vtable +832
// The generic steps of sub_825818E0's hide / restore paths (no class-specific calls), used on player 2 (a human
// has no hide of its own: his vtable +832 is an unrelated getter).
REX_EXTERN(sub_8251EBD0); // Rare's "friend?" (me r3, other r4) -> byte: owner links, then the masks (Fri 20 Nov night)
REX_EXTERN(sub_824EF8B8); // the auto-aim's target filter (viewer r3, target r4) -> byte, 1 = may be a target
// The filter's own steps (824EF8B8, after the target's dead test +232, itself, its +5784 byte and the viewer's
// layer bit of the target's +36): targetable flags (82519DA0, +64 bits), Rare's friend test, the same root object
// through +92 (825195D8: riding the same thing), in water (82506C18), then the target's vtable +644.
REX_EXTERN(sub_82519DA0); // (character) -> byte, 1 = targetable by its +64 flags
REX_EXTERN(sub_825195D8); // (a, b) -> byte, 1 = the same top object through +92
REX_EXTERN(sub_82506C18); // (character) -> byte, 1 = in water
REX_EXTERN(sub_82189B08); // hide: (character)
REX_EXTERN(sub_82517600); // physics out: (object)
REX_EXTERN(sub_82187E98); // team lists out: (character)
REX_EXTERN(sub_82527D78); // world out: (world, character)
REX_EXTERN(sub_82156E20); // (character, 1)
REX_EXTERN(sub_82187D18); // team lists in: (character)
REX_EXTERN(sub_82517530); // physics in: (object, 0)
REX_EXTERN(sub_82527C90); // world in: (world, character)
REX_EXTERN(sub_82156EF0); // (character, 1)
REX_EXTERN(sub_8254D8F0); // a player's action lock: (character, 64-bit mask), counters at +5785
REX_EXTERN(sub_8254D968); // its unlock
REX_EXTERN(__imp__sub_823B15F8); // a HUD message for one player (pad, kind 0x17, &text, f1 seconds, r7): co-op revive
REX_EXTERN(__imp__sub_82214C48); // the gameplay camera's update (camera mode, target character, f1)
// The look (what the player factory and the human constructor use).
REX_EXTERN(sub_82413FD8); // an asset by name: (name, fallback name or 0) -> asset or 0 (82414070's own lookup;
                          // "_noname_" -> 0; a missing name -> the fallback's asset, if any)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;            // -> +0 game state (+8 lists, +28 registry, +40 game type), +24 world, +68 cutscene
  constexpr uint32_t kStateInLevel = 21;
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216; // world: object nodes (+4 object, +12 next)
  // Rare's object registry (82178FF8): [game +28] +20 -> groups from +676 to +680 (next at +36), each with a node
  // list at +4 +16 .. +4 +20 (node: +0 object, +8 next). Hidden objects are still in it.
  constexpr uint32_t kRegistry = 28, kRegistryBase = 20, kGroupFirst = 676, kGroupEnd = 680, kGroupNext = 36;
  constexpr uint32_t kGroupListFirst = 4 + 16, kGroupListEnd = 4 + 20, kNodeObject = 0, kNodeNext = 8;
  constexpr uint32_t kParticipantList = 0x82E34224; // -> participant (+4 next)
  constexpr uint32_t kPlayerOne = 0x82E3C3E4;       // player 1's character
  constexpr uint32_t kLevelState = 0x82E426F8;      // byte: 0x1 completed, 0x2 failed, 0x40 checkpoint
  constexpr uint32_t kGuardVtable = 0x820C45A0, kHumanVtable = 0x820C7DE0;
  constexpr uint32_t kFilterTable = 0x8208ABC8;     // setup filter masks (+49 index, 8 bytes each, mask at +4)
  // participant
  constexpr uint32_t kPartCharacter = 964, kPartDead = 992, kPartDeathState = 2584, kPartUser = 1680;
  constexpr uint32_t kPartMaxHealth = 980; // the human's max health (825A9408 with a nonzero flag)
  // character
  constexpr uint32_t kPosition = 48, kSetup = 84, kHealth = 120, kArmour = 124, kTeamBits = 128, kAllies = 130,
                     kEnemies = 132;
  constexpr uint32_t kHand0 = 4076, kHand1 = 4080, kItemType = 12, kBetrayal = 6276, kViewYaw = 0x1670;
  constexpr uint32_t kHealthFloor = 7828; // a human: the least the player damage handler 825AAF50 leaves (Datacore 1.0)
  constexpr float kOwnFloor = 1.0f;       // round 3: player 2's floor while he plays a guard (pdz_counterop_own_death)
  constexpr uint32_t kHidden = 6272;   // bit 0x10: hidden by sub_825818E0 (a level script's dormant guards too)
  constexpr uint32_t kBodyId = 5280, kHeadId = 5284, kParticipantOf = 6560; // both classes: the ids; the human's participant
  // setup record (268 bytes for a character; the co-op partner's player setup is a 68-byte record with the same
  // +52 body / +56 head)
  // +52 is a pointer to the 4-character id string (the file holds an offset into its string tail; log 306).
  constexpr uint32_t kSetupPosition = 4, kSetupFilter = 49, kSetupId = 52, kSetupTeam = 56, kSetupType = 244,
                     kSetupHealth = 256;
  constexpr uint32_t kDescBody = 52, kDescHead = 56; // the player setup's ids (825B3D58 / 825A6B20)
  constexpr uint8_t kTeamTemplate = 8; // a spawn template: the guard is a runtime clone of it
  // Body / head list entries (game +8 -> +0 body list, +12 head list; list: +0 entries, +8 count; entry +0 = id).
  constexpr uint32_t kListsBody = 0, kListsHead = 12, kListEntries = 0, kListCount = 8;
  constexpr uint32_t kBodyModelRef = 4, kBodyHandsRef = 16, kBodyThirdRef = 32, kBodyHands = 20, kBodyAnimSet = 64,
                     kBodyAssets = 68;
  // 82414070's FALLBACK names for its lookups (82413FD8's second argument, used when the first name is missing):
  // the model (+4 -> +68, fallback the placeholder body), the hands (+16 -> +20, only with flag 1 = a human; flag 0
  // stores 0) and the ragdoll (+32 -> +36, fallback Jack's ragdoll), whose +8 it reads without a check, as it reads
  // the model's +8 (log 313's crash). 82414070 returns at once when +68 is set (the body already loaded).
  // (the model's fallback, 0x820B601C, is not used: the probe looks the model up strictly)
  constexpr uint32_t kHandsFallback = 0x820B604C, kRagdollFallback = 0x820B6078;
  constexpr uint32_t kPlaceholderBody = 86; // Rare's fallback body (82278788): never a candidate
  constexpr uint32_t kBodyAnimName = 12;     // the animation set's name (822788D8 resolves it into +64)
  constexpr uint32_t kHeadModelRef = 4, kHeadAssets = 12; // head entry: model name, model (82414618)
  // Rare's human list: head [82E3C3E4], +6792 next, +6796 previous (unlinked in 825A6F88). In co-op 825B3D58 builds
  // a human only while player 1's next is 0, i.e. player 2's call.
  constexpr uint32_t kP1Partner = 6792;
  constexpr uint32_t kBodyEntryOf = 5264, kHeadEntryOf = 5268; // a human's list entries (825A6B20)
  constexpr uint32_t kFirstPersonHands = 7796; // the two first-person hands instances (+7796 / +7800, 825B43E0)
  // The two animation sets Rare builds humans with: the male set (Jack, Jonathan, the CI partner, multiplayer men;
  // every male guard body) and the female set (Jo, Chandra; the female guard bodies).
  constexpr const char *kHumanAnimSets[] = {"aid_xml_test_henchmananims", "aid_xml_test_joanims"};
  // The gameplay camera mode (third_person.cpp, same in TU3): after its update, the camera position, the look point
  // and the eye.
  constexpr uint32_t kCamPosition = 4, kCamLook = 16, kCamEye = 188;
  constexpr float kSpectateBack = 120.0f, kSpectateUp = 30.0f; // cm: behind and above the agent's eye
  constexpr uint16_t kP2Team = 0x0001, kP2Enemies = 0x0002; // study section 5 (b)
  constexpr uint16_t kP2Allies = 0xFFFF; // a team-0 guard's allies (8252BD74): he answers the friend test as one
  // Rare's get-up after a co-op revive (824573A8 -> participant method 82474338 -> 824578D8 starts it, the character
  // update 8254CCC8..8254CDBC counts it down and ends it with 82457B40): the character's flag +5928 and timer +5924,
  // the participant's invulnerability +928 (the hit handler reads it at 8254989C; the update counts it down).
  constexpr uint32_t kGetupFlag = 5928, kGetupTimer = 5924, kPartInvulnerable = 928;
  constexpr double kGetupCapSeconds = 8.0; // the get-up wait's cap (Rare's update may not run while he is out)
  constexpr float kFreeRadius = 120.0f;    // cm: a spot with any character this close is taken
  constexpr double kHideWaitSeconds = 3.0; // the first hide waits this long at most for player 2's camera object
                                           // (Jungle: noted after 1.0 s; Temple: not within 1 s)
  constexpr double kCalmOnlySeconds = 10.0; // take: a search takes only calm guards for this long, then any
  constexpr uint32_t kHitLogFirst = 24, kHitLogEvery = 50; // the hit probe's rate limit
  constexpr double kNextGuardSeconds = 2.5;
  constexpr double kFirstAreaSeconds = 6.0;   // round 3: the first spawn tries only the agent's area this long
  constexpr double kRetrySeconds = 1.0;       // a failed pick is tried again this often
  constexpr double kOutOfGuardsSeconds = 20.0; // after a death: no spot for this long = out of guards
  constexpr double kFirstPickSeconds = 3.0;   // the first pick waits this long (the opening cutscene starts in it)
  constexpr uint64_t kLockAll = ~0ull;        // every action bit of the player's lock
  constexpr uint32_t kChildFirst = 88, kChildNext = 96; // a character's attached objects
  constexpr float kMinSpawnDistance = 1500.0f; // cm: a spot this far from the agent is preferred
  constexpr float kMinHealth = 25.0f;          // the floor of the health rule
  constexpr uint32_t kFullAmmo = 9999;         // added to each reserve by Rare's adders, clamped to the type's max
  constexpr uint32_t kWeaponList = 48, kWeaponListCount = 24, kWeaponEntryBytes = 2196, kWeaponDefType = 1144,
                     kWeaponEntry = 1248, kWeaponEntryDef = 0, kWeaponEntrySecondaryDef = 4;

  // Eligible guard ids per mission (the study's eligible table, scratchpad counterop_C\eligible.tsv; names as
  // pdz_sp_60_levels has them).
  struct MissionIds
  {
    const char *mission;
    const char *ids;
  };
// Eligible and denied ids per mission: the scope study's lists, with every id the mission's level script uses one by one (looked up by id and then handled singly, at level start or later) moved to denied (the F_ids study, 8 Nov 2026: static analysis of the TU3 level scripts). Ids the scripts reach only through group lists stay eligible: a replaced guard dies through Rare's kill when player 2 dies, so the groups' all-dead checks still resolve.
constexpr MissionIds kEligible[] = {
    {"nightclub", "s20a,s19b,s19a,s20b,s20c,s01a,s20d,s21d,s21b,s21c,s05d,s21a,s22d,s22a,s22c,s22b,s11b,s11c,s10c,s03b,s11d,s11e,s08c,s08b,s08a,s09a,s09d,s09c,s09b,s05a,s05b,s05c,s03a,s12b,s12c,s12d,s16a,s16b"}, // 38 ids (removed: s01b,s02c,s06a)
    {"subway", "lop2,saf5,arm4,arp2,arp3,saf1,armp,arp4,saf3,saf4,war1,saf2,arm3,lorp,zsb4,fac1,bik3,bik4,bik2,bik5,bik1,fac2,arm2,arm1,zsb5,zsb7,zsb6,zsb3,fac3,zsb2,zsb1,zsb8,s18b,18-1,out3,out4,out1,out2,inn2,inn3,inn4,inn1,inn6,sin2,sin3,inn5,zsba,sin4,zsb9"}, // 49 ids (removed: lor1,s16a,s16b,16-3)
    {"rooftops", "s141,s142,s133,s143,top2,s131,s123,gf32,gf33,top1,s112,s111,top3,s122,s121,s113,gf31,mff2,tff1,tff2,s132,bfb4,mff1,gft1,gft2,gft3,ggo1,ggo2,ggo3,s194,s193,bff2,bff1,17-3,17-2,s17a,s192,s191,bff4,bfm3,bfb3,bfm2,bfm1"}, // 43 ids (removed: s20a,s15b,s15a,bfb1,bfb2)
    {"mansion", "tow2,tow4,tow3,pat1,pat2,fre4,fre5,pat3,fre2,fre1,fre3,rsp1,lsp1,cpt1,s06c,wpt1,mgp1,ocg1,wpt2"}, // 19 ids (removed: mgg1,tow1,end1,ocp1)
    {"laboratory", "s3g7,s3g4,s3g5,s3g8,s1m3,s1m2,s1m4,s1m5,s3g3,s3gb,s3g2,s3gc,s11b,s11c,s11a,s11d,s9g2,s9g5,s9g6,s9g3,s9g4,s9g7,s9g8,s6s3,s6s2,s6s1,s9p2,s9p1,s9g1,s9p3,s7s4,s7s2,s7s1,s7s3"}, // 34 ids (removed: s3g1,s1m1,s6s5,s6s4)
    {"river", "s3g5,s3g4,s5g1,s3g1,s2g5,s3g3,s3g2,s4g5,s4g1,s4g3,s4g4,s4g2,s2g4,s1g1,s20b,s20a,s19b,s19a,s19d,s19c,s20c,s2g1,s1g5,s2g3,s2g2,s1g2,s20d,s1g4,s1g3,s16g,s16h,s16e,s16f,s16i,s17b,s17c,s16j,s17a,s16d,s15g,s15h,s15e,s15f,s15i,s16b,s16c,s15j,s16a,s17f,s17g,s17d,s17e,s17h,s17i,s17j,s8g3,s8g2,s9g1,s10a,s10b,s9g2,s9g3,s6g3,s6g4,s6g1,s6g2,s6g5,s6g6,s6g7,s10c,s13a,s13b,s12f,s12g,s15c,s15d,s15a,s15b,s12e,s11c,s11d,s11b,s12c,s12d,s12a,s12b"}, // 86 ids (removed: s8g4; round 13 a: s6kc, the keycard guard)
    {"trinity1", "s03e,s07l,s03d,s14c,s14d,s14a,s14b,s14e,s14h,s14i,s14f,s14g,s14j,s01d,s01c,s03b,s03c,s03a,s06e,s06d,s14k,s14l,s06c,s06b,s18a,s18b,s18c,s08a,s07j,s08b,s17c,s15b,s16c,s17a,s17b,s16a,s16b,s07i,s06a,s07k,s07a,s07h,s07n,s07m,s07f,s07g,s07c,s07b,s07e,s07d"}, // 50 ids (removed: s01b,s15a,s07z)
    {"trinity2", "s07g,s07h,s08a,s01c,s01d,s01e,s07f,s07b,s07a,s06h,s07e,s07d,s07c,s08e,s08f,s09a,s08b,s08c,s08d,s09b,s01a,s01b,s09c,s03f,s04a,s03d,s03e,s04d,s04e,s04b,s04c,s01f,s03b,s03c,s03a,s04f,s06b,s06c,s05h,s06a,s06f,s06g,s06d,s06e,s05b,s05c,s05a,s05f,s05g,s05d,s05e,s09d,s12d,s12c,s12b,s12e,s17g,s17h,s12a,s18a,s18b,s17j,s11f,s11e,s17e,s14b,s14c,s12h,s12i,s14a,s13a,s12j,s14d,s17b,s17c,s17d,s17a,s12g,s12f,s10f,s10e,s10d,s11a,s18d,s18c,s11b,s10c,s09g,s09f,s09e,s09h,s10b,s10a,s09i,s18e,s11c,s11d"}, // 97 ids (removed: s17f,s17i)
    {"jungle", "s44c,s10e,s10f,s10c,s09d,s10d,s11g,s11f,s11h,s11j,s11i,s11b,s11a,s11c,s11e,s11d,s09c,s6e1,s5l6,s6e2,s6e4,s6e3,s5l2,s5l1,s5l3,s5l5,s5l4,s7c1,s7c8,s7c7,s09b,s09a,s7c3,s7c2,s7c4,s7c6,s7c5,s02a,s02i,s02g,s03d,s03e,s03c,s03a,s03b,s02b,s02c,s44b,s11p,s44a,s43b,s43c,s11l,s11k,s11m,s11o,s11n,s41a,s41b,s41c,s03f,s03g,s03h,s42c,s43a,s42a,s42b"}, // 67 ids (removed: s10b,s10a,s02j,s02h,sta1,s02e,s02f,s02d)
    {"temple", "s2b1,s1a1,s1a2,s2b4,s2b3,s2b2,s08c,s08e,s08d,s06g,s06f,s06h,s08f,s10a,s09f,s10c,s10b,s09e,s09b,s09a,s09d,s09c,s4p3,s4p4,s4p2,s3b1,s4p1,s5c1,s06c,s06d,s06e,s06b,s5c2,s5c3"}, // 34 ids (removed: s4b2,s4b1)
    {"outpost", "s14e,s14f,s14g,s14b,s14c,s14d,s16a,s16b,s17a,s14h,s15a,s15b,s14a,s11a,s11b,s12a,s10a,s10b,s10c,s13c,s13d,s13e,s12b,s13a,s13b,s03a,s03b,s02b,s02c,s03c,s03d,s01c,s01d,s01a,s01b,s01e,s01h,s02a,s01f,s01g,s08f,s08g,s08d,s08e,s08h,s09c,s09d,s09a,s09b,s07a,s07b,s08b,s08c,s07d,s08a,s17b,s27k,s34d,s34e,s34f,s27a,s27b,s27c,s26a,s34c,s34b,s27h,s27i,s27g,s27d,s27e,s27f,s19d,s19c,s19b,s20a,s21c,s21b,s21a,s17e,s17d,s17c,s18a,s19a,s18c,s18b,s24b,s24a,s23e,s34a,s24c,s22c,s22b,s22a,s23a,s23d,s23c,s23b"}, // 98 ids (removed: s27l)
    {"bridge", "s22b,s22c,s20b,s21a,s22a,s40b,s40a,s40d,s40c,s20a,s14a,s12b,s15b,s15a,s10a,s12a,s11a,s16a,s18c,s18b,s19a,s18d,s17a,s16b,s18a,s17b,s26d,s26c,s26b,s27a,s27c,s27b,s26a,s23d,s23c,s22d,s23a,s23b,s30a,s09b,s44j,s44i,s45c,s45b,s45a,s44h,s44c,s44b,s44a,s44d,s44g,s44f,s44e,s45f,s45e,s45d,s45g,s45j,s45i,s45h,s04b,s05a,s05b,s04a,s03b,s03c,s03d,s06a,s08a,s08b,s09a,s07b,s06b,s06c,s07a,s02c,s02d,s03a,s02a,s02b"}, // 80 ids (removed: s25a,s44k,s44l)
};

constexpr MissionIds kDenied[] = {
    {"datacore", "L02S,Al01,jack"}, // round 2: Grd1 (every spawner guard's template id) is eligible
    {"nightclub", "rin1,s18c,s25a,s25b,sc31,s18b,sc38,sc33,sc36,s02d,sc32,sc30,s11a,s10d,rin2,s18a,s10a,s10b,s04a,r2bu,rin3,sc10,sc11,sc14,sc09,sc06,sc07,sc08,sc21,sc22,sc23,sc20,sc15,sc17,sc19,s12a,s02b,s02a,s02e,sc03,sc04,sc05,sc02,sc01,s01b,s02c,s06a"}, // +3 from the scripts: s01b,s02c,s06a
    {"subway", "phon,sub1,sub4,sub5,sub2,sub3,was1,sub6,zieg,jack,s19b,s20a,sak2,s20c,s20b,s17c,s17b,s19a,s17a,sak1,dspa,16-3,lor1,s16a,s16b"}, // +4 from the scripts: 16-3,lor1,s16a,s16b
    {"rooftops", "s16a,zip1,krad,sni2,sni1,sni3,bfb1,bfb2,jack,s15a,s15b,s20a"}, // +6 from the scripts: bfb1,bfb2,jack,s15a,s15b,s20a
    {"mansion", "ter1,ter2,ter3,civ2,civ3,mai1,ter4,ter8,chan,ter5,ter6,ter7,s4s2,s5s2,rdo1,s5s1,bik1,s4s1,civ1,end2,rdo2,end1,mgg1,ocp1,tow1"}, // +4 from the scripts: end1,mgg1,ocp1,tow1
    {"laboratory", "s2s2,s2s1,s4l4,s4l5,s4l3,s4l1,s4l2,s101,jack,s12a,s12b,s5l1,s5l3,s5l2,s1m1,s3g1,s6s4,s6s5"}, // +4 from the scripts: s1m1,s3g1,s6s4,s6s5
    {"river", "s22a,s21f,s23a,s22b,s21e,s21c,s21d,s21a,s21b,s23b,s24b,s24a,s8g1,s7g1,s14a,s11a,jack,s8g4,s6kc"}, // +2 from the scripts: jack,s8g4; round 13 a: s6kc (the keycard guard: the script spawns the keycard from him)
    {"trinity1", "s02a,s11b,s11c,s11a,s10h,s11d,s11f,s11g,s10g,s11e,s02b,s02c,s01a,clay,drca,virg,radd,s10a,s09h,radr,s09b,s09a,s10c,s10d,s10e,s10b,s10f,s09g,s09f,s09c,s09d,s09e,rada,s01b,s07z,s15a"}, // +3 from the scripts: s01b,s07z,s15a
    {"trinity2", "s30b,s30a,s02a,s02b,s16a,s21a,s15a,s21c,s31c,s31b,s21b,s31a,s17f,s17i,s30p"}, // +3 from the scripts: s17f,s17i,s30p
    {"jungle", "s31p,s08a,s01a,s12a,s12b,s02d,s02e,s02f,s02h,s02j,s10a,s10b,s30p,sta1"}, // +9 from the scripts: s02d,s02e,s02f,s02h,s02j,s10a,s10b,s30p,sta1
    {"temple", "s08b,s08a,s07a,s06a,s4b1,s4b2"}, // +2 from the scripts: s4b1,s4b2
    {"outpost", "s04b,s04c,s04a,s04d,s04e,s07c,s31c,s32a,s32b,s31b,s31a,s30p,bff1,s30b,s25a,s27j,s28a,s28b,s27l,s30a"}, // +2 from the scripts: s27l,s30a
    {"bridge", "s30m,s35i,s30c,s10b,s28a,s24a,s29a,s35d,s35c,s35b,s35e,s35h,s35g,s35f,s35a,s31a,s29b,s32a,s33b,s33a,s32b,s47b,s47a,s46a,s47c,s48b,s48a,s47d,s01d,s01e,s01f,s01c,s01j,s01a,s01b,s01g,s25a,s44k,s44l"}, // +3 from the scripts: s25a,s44k,s44l
    {"arena", "Grd1,zlsp"},
};

  // The placed guards' looks per mission, most common first: (body = setup +244, head = setup +248), from each
  // level's setups (team 0, combat type, under the counter-op filter, not denied; the head is that body's most
  // common one; counts in the comments). No row (Arena) = the co-op look.
  struct LookPair
  {
    uint32_t body, head;
  };
  struct MissionLook
  {
    const char *mission;
    LookPair pairs[5];
  };
  constexpr MissionLook kLook[] = {
      {"nightclub", {{48, 3}, {62, 53}}},                           // gangster 30, warehouse worker 9
      // Round 4: the opening enemies' look first (Subway's opening is bikers; the suited henchmen come later). The
      // look is re-made at every spawn (pdz_counterop_dress); this one is only what he is built with.
      {"subway", {{75, 93}, {50, 9}, {62, 54}, {51, 8}, {48, 95}}}, // biker 7 (the opening), henchman 15, worker 13
      {"rooftops", {{50, 10}, {51, 9}}},                            // henchman 34, henchman 2 14
      {"mansion", {{52, 14}, {53, 14}}},                            // Private Force A 17, B 6
      {"laboratory", {{52, 14}, {53, 14}, {54, 21}}},               // Private Force A 21, B 12, security girl 6
      {"river", {{52, 14}, {53, 17}, {54, 22}}},                    // Private Force A 56, B 31, security girl 6
      {"trinity1", {{84, 73}, {69, 77}, {70, 79}, {67, 72}}},       // trooper 19, platform workers 15 / 11, guard 8
      {"trinity2", {{84, 71}, {69, 71}, {70, 76}, {67, 72}}},       // trooper 46, platform workers 36 / 9, guard 8
      {"jungle", {{56, 29}, {52, 14}}},                             // jungle trooper 62 (heads 29-34), Private Force 13
      {"temple", {{56, 29}, {84, 33}, {52, 14}}},                   // jungle trooper 20, trooper 9, Private Force 4
      {"outpost", {{57, 35}, {55, 35}}},                            // desert trooper 66, urban trooper 33
      {"bridge", {{57, 35}, {55, 35}, {84, 35}}},                   // desert trooper 39, urban trooper 35, trooper 5
      {"datacore", {{67, 71}}}, // round 2: the IndoorGuard01 template's guard (every spawner guard is a Grd1 clone)
  };

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint16_t Load16(const uint8_t *base, uint32_t a) { return uint16_t((base[a] << 8) | base[a + 1]); }
  void Store16(uint8_t *base, uint32_t a, uint16_t v)
  {
    base[a] = uint8_t(v >> 8);
    base[a + 1] = uint8_t(v);
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
  // Sun 22 Nov (round 18): [a, a + n) may be read: inside the guest's range AND mapped now (the cached check of the
  // Sat 21 Nov crash fix). For every pointer read from guest memory that is not proven current (log 046's crash: a
  // stale player 2 from the level before, a hand slot holding 1.0).
  bool Rd(const uint8_t *base, uint32_t a, uint32_t n) { return pdz::guestmem::Ok(base, a, n); }

  uint32_t Call(PPCContext &ctx, uint8_t *base, PPCFunc *fn, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0,
                uint32_t r6 = 0, uint32_t r7 = 0, uint32_t r8 = 0)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = r3;
    frame.ctx.r4.u64 = r4;
    frame.ctx.r5.u64 = r5;
    frame.ctx.r6.u64 = r6;
    frame.ctx.r7.u64 = r7;
    frame.ctx.r8.u64 = r8;
    fn(frame.ctx, base);
    return frame.ctx.r3.u32;
  }

  float Dist3(float ax, float ay, float az, float bx, float by, float bz)
  {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  // A guest C string (printable, at most max characters); "" when unreadable.
  std::string GuestString(const uint8_t *base, uint32_t p, size_t max = 63)
  {
    std::string out;
    for (size_t i = 0; Ptr(p) && i < max; ++i)
    {
      const char ch = char(base[p + i]);
      if (!ch)
      {
        break;
      }
      if (ch < 32 || ch > 126)
      {
        return "";
      }
      out += ch;
    }
    return out;
  }

  // The setup id: setup +52 points at its string (log 306). Rare's 82142B70 packs up to 4 characters; here the first
  // goes in the top byte, as the lists above are packed. 0 = unreadable (no pointer, empty or not printable).
  uint32_t SetupIdKey(const uint8_t *base, uint32_t setup, uint32_t *ptr_out = nullptr)
  {
    const uint32_t p = pdz::guestmem::Ok(base, setup, kSetupId + 4) ? Load32(base, setup + kSetupId) : 0;
    if (ptr_out)
    {
      *ptr_out = p;
    }
    if (!pdz::guestmem::Ok(base, p, 4)) // (Sat 21 Nov evening: mapped memory, not only the guest's range)
    {
      return 0;
    }
    uint32_t key = 0;
    for (uint32_t i = 0; i < 4; ++i)
    {
      const uint8_t ch = base[p + i];
      if (!ch)
      {
        break;
      }
      if (ch < 0x21 || ch > 0x7E)
      {
        return 0;
      }
      key |= uint32_t(ch) << (24 - 8 * i);
    }
    return key;
  }

  std::string IdChars(uint32_t key)
  {
    std::string chars;
    for (int i = 24; i >= 0 && ((key >> i) & 0xFF); i -= 8)
    {
      chars += char((key >> i) & 0xFF);
    }
    return chars;
  }

  // An id for the log: the number (hex and decimal), then the characters read from its string.
  std::string IdLog(uint32_t key, uint32_t ptr)
  {
    return fmt::format("0x{:08X} / {} ('{}' from the string at {:08X})", key, key, IdChars(key), ptr);
  }

  void ParseIds(const char *list, std::unordered_set<uint32_t> &out)
  {
    const std::string ids = list;
    for (size_t i = 0; i + 4 <= ids.size(); i += 5)
    {
      out.insert((uint32_t(uint8_t(ids[i])) << 24) | (uint32_t(uint8_t(ids[i + 1])) << 16) |
                 (uint32_t(uint8_t(ids[i + 2])) << 8) | uint8_t(ids[i + 3]));
    }
  }

  // Non-combatants by character type (study section 4): scientists 58-61, party guests 71-74, club guests / staff
  // 79-83. Named / main characters are below 48.
  bool CombatType(uint32_t type)
  {
    return type >= 48 && type < 256 && !(type >= 58 && type <= 61) && !(type >= 71 && type <= 74) &&
           !(type >= 79 && type <= 83);
  }

  // Player 2's look, chosen by the factory hook at level load (before Start): kept apart from the per-level state.
  struct Look
  {
    bool decided = false, chosen = false;
    std::string mission, why;
    uint32_t coop_body = 0, coop_head = 0, body = 0, head = 0;
    // D15 (log 312's cause): the chosen body was loaded by Rare for a guard (82414070 with flag 0: no player hands,
    // entry +20 = 0) and the human constructor (825A6B20) skips the loader when +68 is set, so player 2 would get
    // null hands and Rare's first-person weapon / animation update crashes on the next frame. These hands (Rare's
    // own lookup, as the loader's flag-1 step) are written to the entry's +20 just before the factory call.
    // A guard body has no player hands at all (+16 "_noname_", the trooper fallback not shipped): fill_hands is then
    // the co-op body's, written before the call (body loaded) or after it (the constructor's loader writes +20).
    uint32_t fill_hands = 0, fill_entry = 0;
    std::string hands_why;
    std::string arms_why; // round 2: why no guard arms were found (the co-op fallback logs it)
  } g_look;

  struct Spot
  {
    uint32_t character = 0;
    uint32_t setup = 0;
    uint32_t id = 0;      // the setup id as Rare's key packs it (4 characters, the first in the top byte); 0 = none
    uint32_t id_ptr = 0;  // where its string is
    uint32_t type = 0;    // character type / body id
    bool by_list = false; // eligible by the mission's id list (else by the no-id rule)
    const char *why = ""; // the no-id rule's reason
    bool dormant = false; // hidden (a level script shows it later)
    bool dead = false;    // the guard is dead (its setup position is a cleared area; no-take mode only)
    float x = 0.0f, y = 0.0f, z = 0.0f; // the spot: a dormant guard's setup position, a live guard's own (take
                                        // mode); every guard's setup position otherwise
    float distance = 0.0f;
    bool on_screen = false;
    // Calm: not perceiving a hostile this frame (+6268 bit 0x400000, set by the target step 82580508), never engaged
    // (+6252 bit 0x2) or shot (+6252 bit 0x80), no current target (+0x317C). The take prefers calm guards.
    bool calm = true;
    bool air = false; // round 13 c: a guard in his jetpack's seat (the spot is a seat hand-over)
  };

  struct State
  {
    bool started = false;
    bool unsupported = false;
    bool spectating = false;
    bool game_over_logged = false;
    bool in_cutscene = false;
    bool allow_revive = false;
    std::string mission;
    std::unordered_set<uint32_t> ids;    // this mission's eligible ids
    uint32_t rule_live = 0;              // the last scan's live, untaken candidates by the no-id rule (clone missions)
    std::unordered_set<uint32_t> denied; // this mission's denied ids (the no-id rule never takes them)
    uint32_t state_object = 0;
    uint32_t p1p = 0, p2p = 0, p1c = 0, p2c = 0;
    uint32_t revive_participant = 0;
    std::chrono::steady_clock::time_point revive_at{};
    int lives = 0;
    uint32_t team_fixes = 0;
    uint32_t last_weapon = ~0u;
    // Round 4 (item 4): the item given at this life's spawn, when his hands went empty, and the redraws.
    uint32_t life_item = 0, redraws = 0, redraws_in_a_row = 0;
    std::chrono::steady_clock::time_point empty_since{};
    uint32_t scratch = 0; // guest vec3 for the move
    std::unordered_set<uint32_t> taken;
    std::unordered_map<uint32_t, uint32_t> rehidden; // a taken guard a script showed again: times re-hidden
    // Player 2 out of the world (hidden, locked) until he takes a spot, and the pick's retries.
    bool out = false;
    bool need_guard = false;
    bool after_death = false;
    std::chrono::steady_clock::time_point need_since{}, next_try{}, last_diag{};
    uint32_t diag_lines = 0, tries = 0;
    // Spectate camera while out (log 306): the agent's last camera (eye, look point), player 2's camera updates.
    bool agent_cam = false;
    float agent_eye[3] = {}, agent_look[3] = {};
    std::chrono::steady_clock::time_point out_since{};
    uint32_t p2_cam_updates = 0, p1_cam_updates = 0, spectate_writes = 0;
    bool spectate_reported = false;
    // Player 2's camera object (noted while he is in the world), driven from the agent's camera update while he is out
    // (Rare doesn't update it then); and the first hide waits for it (up to kHideWaitSeconds).
    uint32_t p2_mode = 0;
    bool in_spectate_call = false;
    // pdz_counterop_fix_out_freeze: the spectate calls made, the leftover step values that would have been passed
    // before the fix (not a plausible step: not finite, negative or over 0.25 s) and the largest, the angle resets.
    uint32_t spectate_calls = 0, bad_leftover = 0, angle_resets = 0;
    double max_leftover = 0.0;
    // N64 take: the guard this life replaced (killed through Rare's death path when player 2 dies), the listed ids
    // replaced or seen dead (the eligible-left count), and the kill waiting for the next tick.
    bool hide_if_no_spot = false; // after a revive: hide only if the first pick finds no spot
    bool cam_field_tried = false;  // the agent's camera object looked up in his character once
    bool p2_mode_logged = false;
    uint32_t health_clamps = 0;
    uint32_t cam_field = 0;
    uint32_t current_guard = 0, kill_guard = 0, kill_stage = 0;
    bool kill_ready = false; // the revive has moved player 2 away: the replaced guard's kill can start
    float kill_at[3] = {};
    float kill_yaw = 0.0f;    // round 6 (item 6): his facing when he died (radians), given to the restored guard
    bool kill_yaw_ok = false, kill_origin = false;
    bool held_for_hack = false, hack_hold_logged = false; // round 6: the agent is in a hacking minigame
    // Round 6 (items 5, 6): proof lines: the height of player 2 on the frames after a takeover, and of the restored
    // guard on the frames after his death (a fall or a pop shows as a changing y).
    int place_log = 0, body_log = 0;
    uint32_t body_log_guard = 0;
    std::string place_series, body_series;
    float place_given_y = 0.0f;
    std::unordered_set<uint32_t> taken_ids, dead_ids;
    bool hide_pending = false;
    std::chrono::steady_clock::time_point hide_pending_since{};
    // Health: the co-op max health, restored on a stop; what the last spawn set, checked once 5 s later.
    float coop_max_health = 0.0f;
    bool max_health_set = false;
    float set_health = 0.0f;
    std::chrono::steady_clock::time_point health_check_at{};
    bool health_check = false;
    bool registry_warned = false;
    // No-take mode: the last life's spot (not used twice in a row) and gun (when a spot's guard holds none).
    uint32_t last_setup = 0, last_gun = 0;
    // After Rare's revive: player 2 waits out of the world until Rare's get-up is over (no revive seen anywhere).
    bool wait_getup = false;
    std::chrono::steady_clock::time_point getup_rescue_at{}; // round 2: the get-up rescue after a spawn
    int getup_rescue_step = 0;
    std::chrono::steady_clock::time_point getup_since{};
    // Any revive asked for the dead agent (blocked).
    uint32_t agent_revives_blocked = 0;
    // Probes: hits between player 2 and anyone (82549040), pick-ups refused.
    uint32_t hit_lines = 0, pickups_blocked = 0;
    uint32_t agent_hit_lines = 0; // round 3: the agent's hits on player 2 logged
    uint32_t own_deaths = 0; // round 3: lives ended by our own death path (pdz_counterop_own_death)
    std::vector<uint32_t> life_types; // round 3: the weapon types this life was given (the tick allows only these)
    uint32_t foreign_removed = 0;
    std::chrono::steady_clock::time_point first_pick_at{}; // round 3: the first pick try (the first spawn's patience)
    uint32_t pick_fail_lines = 0;
  } g;
  // Sun 22 Nov night (audit): the tick found no players while a game was set (a reload in progress), and the guest
  // base the exported player views test the game state with.
  bool g_players_missing = false;
  const uint8_t *g_view_base = nullptr;
} // namespace
extern std::atomic<uint32_t> g_wait_hud_p2; // Sun 22 Nov (item 7): the waiting player 2 the crosshair hooks look at
namespace
{
  // Sun 22 Nov (item 9): the cut-off rule's state (cleared with the rest at each Reset: CutOffReset).
  struct CutOff
  {
    struct DoorSeen
    {
      bool locked = false, armed = false;           // locked at the last look; a fresh lock still to be judged
      std::chrono::steady_clock::time_point locked_at{};
    };
    std::unordered_map<uint32_t, DoorSeen> doors; // door -> what the last look saw
    bool doors_known = false;                 // the first look of a level only records
    std::chrono::steady_clock::time_point door_next{}, door_at{}, section_since{}, far_since{}, test_until{};
    uint32_t door = 0, world = 0, test_door = 0;
    uint8_t test_bytes[2] = {};
    int last_area = -2;
    float far_start = 0.0f, far_best = 0.0f;
    uint32_t moves = 0;
    int32_t test_last = 0;
    std::chrono::steady_clock::time_point test_lock_at{}; // the tool: unlocked first, locked at this time
    // Round 18 (item 4): doors the agent has locked this level (a fresh lock of a closed door with her within 20 m): the
    // door, where it is and the side she was on (a horizontal unit vector from the door towards her at the lock).
    struct Locked
    {
      uint32_t door = 0;
      float x = 0, y = 0, z = 0, nx = 0, nz = 0;
    };
    std::vector<Locked> locked;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> handed_back; // guard -> when (cut-off moves)
    std::chrono::steady_clock::time_point prefer_next_until{}; // a cut-off move's pick: her next area first
    std::chrono::steady_clock::time_point after_scene_at{};    // round 18 (item 7): the check after a cutscene
    uint64_t picks_locked_out = 0;
  } g_cut;

  // Setup filter counters (PdzCounterOpFilter, at level load; logged at the start).
  uint32_t g_filter_added = 0, g_filter_removed = 0, g_filter_seen = 0;
  uint32_t g_filter_solo = 0; // round 5 (item 5): solo-only setups let through (the solo companions)
  std::mt19937 g_rng{0xC0DE0Bu};
  bool g_played = false; // the last level started as a supported counter-op game (the debriefing reads it)
  bool g_giving = false; // our give of player 2's gun is running (the pick-up block lets it through)

  // pdz_counterop forces the mode for every co-op game (tests); otherwise it runs when chosen in the menu.
  bool On() { return REXCVAR_GET(pdz_counterop) || PdzCounterOpArmed(); }

  // The level's game state object while in a co-op level, else 0.
  uint32_t CoopLevel(const uint8_t *base)
  {
    const uint32_t so = Load32(base, kGame);
    if (!Ptr(so) || Load32(base, so + 8) != kStateInLevel ||
        (Load32(base, so + 40) != 1 && !PdzCounterOpScriptTypeFlip()))
    {
      return 0;
    }
    return so;
  }

  // Round 6 (item 8): also true while Rare's cutscene player is stepping (its per-frame step, game_pace.cpp's tap),
  // and for kCutsceneTailSeconds after either sign ends, so nothing of ours shows on the frames a cutscene fades in
  // or out on.
  constexpr double kCutsceneTailSeconds = 0.75;
  bool InCutscene(const uint8_t *base)
  {
    static uint64_t last_calls = 0;
    static std::chrono::steady_clock::time_point last_seen{};
    const auto now = std::chrono::steady_clock::now();
    const uint32_t player = Load32(base, kGame + 68);
    bool playing = Ptr(player) && Load32(base, player + 12) == 3;
    uint64_t calls = 0;
    uint32_t picture_ms = 0, clock_ms = 0;
    double step_s = 0.0, sound_ms = 0.0;
    if (pdz::pace::LastCutsceneStep(calls, picture_ms, clock_ms, step_s, sound_ms) && calls != last_calls)
    {
      last_calls = calls;
      playing = true;
    }
    if (playing)
    {
      last_seen = now;
      return true;
    }
    return REXCVAR_GET(pdz_counterop_cutscene_tail) &&
           now - last_seen < std::chrono::milliseconds(int(kCutsceneTailSeconds * 1000));
  }

  // Player 1 (the agent) and player 2 (the counter-op): the participants whose characters are humans; player 1's is
  // Rare's player one.
  void FindPlayers(const uint8_t *base, uint32_t &p1p, uint32_t &p1c, uint32_t &p2p, uint32_t &p2c)
  {
    p1p = p1c = p2p = p2c = 0;
    const uint32_t one = Load32(base, kPlayerOne);
    for (uint32_t p = Load32(base, kParticipantList), n = 0; Ptr(p) && n < 16; p = Load32(base, p + 4), ++n)
    {
      const uint32_t c = Load32(base, p + kPartCharacter);
      if (!Ptr(c) || Load32(base, c) != kHumanVtable)
      {
        continue;
      }
      if (c == one && !p1p)
      {
        p1p = p;
        p1c = c;
      }
      else if (!p2p)
      {
        p2p = p;
        p2c = c;
      }
    }
  }

  bool Alive(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    // Sun 22 Nov night (audit): CharacterDown calls the object's vtable +232: only a readable object whose vtable is
    // in the game image and whose slot is not the runtime's pure-virtual handler (a torn-down object).
    if (!pdz::guestmem::Ok(base, c, kHealth + 4))
    {
      return false;
    }
    const uint32_t vt = Load32(base, c);
    if (vt < 0x82000000u || vt >= 0x83000000u || !pdz::guestmem::Ok(base, vt + 232, 4) ||
        Load32(base, vt + 232) == 0x82AA2BC8u)
    {
      return false;
    }
    return LoadFloat(base, c + kHealth) > 0.0f && !objective_bots::CharacterDown(ctx, base, c);
  }

  // Sun 22 Nov night (audit): his weapon switcher (human +5780) before its slot 0 is called: readable, a vtable in
  // the game image (GuestFunction refuses the pure-virtual handler).
  bool SwitcherOk(const uint8_t *base, uint32_t s)
  {
    if (!pdz::guestmem::Ok(base, s, 4))
    {
      return false;
    }
    const uint32_t vt = Load32(base, s);
    return vt >= 0x82000000u && vt < 0x83000000u && pdz::guestmem::Ok(base, vt, 4);
  }

  // On the agent's screen (approximate): within 60 m and 50 degrees of his view yaw (+0x1670, degrees; forward =
  // (sin yaw, cos yaw) in x / z, the convention of Rare's setup yaws).
  bool OnAgentScreen(const uint8_t *base, uint32_t agent, float x, float z, float distance)
  {
    if (distance > 6000.0f)
    {
      return false;
    }
    const float yaw = LoadFloat(base, agent + kViewYaw) * 3.14159265f / 180.0f;
    const float dx = x - LoadFloat(base, agent + kPosition);
    const float dz = z - LoadFloat(base, agent + kPosition + 8);
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1.0f)
    {
      return true;
    }
    const float dot = (dx * std::sin(yaw) + dz * std::cos(yaw)) / len;
    return dot > std::cos(50.0f * 3.14159265f / 180.0f);
  }

  uint32_t HandWeaponType(const uint8_t *base, uint32_t c)
  {
    for (const uint32_t hand : {kHand1, kHand0})
    {
      const uint32_t w = Load32(base, c + hand);
      if (Ptr(w))
      {
        return Load32(base, w + kItemType);
      }
    }
    return 0;
  }

  // The weapon type Rare's give (8254C378) takes (log 308: the hand item's +12 above is not it; give -> 824F4D28 ->
  // 82587338 -> 82582750 -> 825843D0 looks the type up in the level's weapon list with 824F2C10 and reads the result
  // without a check, so a type the list doesn't hold is a null read). The list: game +48 -> entries at +0 (2196 bytes
  // each; +0 the definition, whose +1144 is the type), count byte +24. A held weapon keeps its entry at +1248
  // (825843D0). Returns the type of the item in a hand when that entry is one of the list's, else 0 and why.
  uint32_t GiveableWeaponType(const uint8_t *base, uint32_t c, std::string &why)
  {
    const uint32_t list = Load32(base, kGame + kWeaponList);
    const uint32_t entries = Ptr(list) ? Load32(base, list) : 0;
    const uint32_t count = Ptr(list) ? base[list + kWeaponListCount] : 0;
    if (!Ptr(entries) || !count)
    {
      why = "the level's weapon list is empty or missing";
      return 0;
    }
    why = "no item in either hand";
    for (const uint32_t hand : {kHand1, kHand0})
    {
      const uint32_t w = Load32(base, c + hand);
      if (!Ptr(w))
      {
        continue;
      }
      const uint32_t entry = Load32(base, w + kWeaponEntry);
      if (!Ptr(entry) || entry < entries || entry >= entries + count * kWeaponEntryBytes ||
          (entry - entries) % kWeaponEntryBytes != 0)
      {
        why = fmt::format("the item in hand ({:08X}) is not one of the level's {} weapons (+1248 = {:08X})", w, count,
                          entry);
        continue;
      }
      const uint32_t def = Load32(base, entry);
      const uint32_t type = Ptr(def) ? Load32(base, def + kWeaponDefType) : 0;
      if (!type)
      {
        why = fmt::format("the weapon list entry {:08X} has no definition / type", entry);
        continue;
      }
      // The same search as 824F2C10: the type must be found in the list, or give reads null.
      for (uint32_t i = 0; i < count; ++i)
      {
        const uint32_t d = Load32(base, entries + i * kWeaponEntryBytes);
        if (Ptr(d) && Load32(base, d + kWeaponDefType) == type)
        {
          return type;
        }
      }
      why = fmt::format("weapon type {} is not in the level's weapon list", type);
    }
    return 0;
  }

  // The level's weapon list holds this type (Rare's give reads a missing type's entry unchecked: 824F2C10).
  bool LevelGivesType(const uint8_t *base, uint32_t type)
  {
    const uint32_t list = Load32(base, kGame + kWeaponList);
    const uint32_t entries = Ptr(list) ? Load32(base, list) : 0;
    const uint32_t count = Ptr(list) ? base[list + kWeaponListCount] : 0;
    for (uint32_t i = 0; Ptr(entries) && i < count; ++i)
    {
      const uint32_t d = Load32(base, entries + i * kWeaponEntryBytes);
      if (Ptr(d) && Load32(base, d + kWeaponDefType) == type)
      {
        return true;
      }
    }
    return false;
  }

  // Rare's default primary reserve for an item, the value 8254C378 sets (82583F80 mode 0) before adding its r7
  // (8254C418-8254C440): the per-difficulty halfword table at item +0x44C indexed by the definition's byte +0xC7
  // (+0x1A), or the halfword at item +0x526 when there is no table.
  uint32_t DefaultReserve(const uint8_t *base, uint32_t item)
  {
    const uint32_t table = Load32(base, item + 0x44C);
    if (Ptr(table))
    {
      const uint32_t defs = Load32(base, item + 0x4E0);
      const uint32_t def = Ptr(defs) ? Load32(base, defs) : 0;
      if (Ptr(def))
      {
        const uint32_t index = (uint32_t(base[def + 0xC7]) + 0x1A) * 2;
        return uint32_t(base[table + index] << 8 | base[table + index + 1]);
      }
    }
    return uint32_t(base[item + 0x526] << 8 | base[item + 0x527]);
  }

  bool TakeGuard() { return REXCVAR_GET(pdz_counterop_take_guard); }

  // A character's current health. A human's is his participant's +980 ([+0x19A0]+0x3D4: the human vtable's getter
  // 825A9408 / setter 825A9468 with flag 0; the damage handler 825AAF50 lowers it); +120 is a human's max (flag 1).
  // A guard's is +120. (co_jungle_3 logged +120 for player 2 and so never saw his health fall.)
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
        return LoadFloat(base, participant + kPartMaxHealth);
      }
    }
    return LoadFloat(base, c + kHealth);
  }

  int32_t TestMaxLives() { return REXCVAR_GET(pdz_counterop_test_max_lives); }

  // N64 take: the mission's listed eligible ids not yet replaced and not seen dead (guards the level hasn't built yet
  // count: it builds them later). 0 = player 2 is out for good. pdz_counterop_test_max_lives caps it for tests.
  float g_eye[2][3] = {};   // the two players' gameplay camera eyes (0 the agent, 1 player 2), every camera update
  // Round 10 (item 3): player 2's view on a driven spiderbot. Round 9 wrote the bot's follow camera into his camera
  // object's position / look fields after Rare's update (the 'spectate' write); round 9's own screenshot of a drive
  // (S2, 126 s) shows that this never reaches the picture: his half kept showing the first-person view from where
  // his hidden body was parked. His picture is Rare's first-person camera of his own character. So his hidden,
  // locked, out-of-the-world body is carried with the bot instead (Rare's move 825AD148 and look angles 825AD3B0,
  // the calls every spawn uses), placed so that his eye is the chase camera and looking at the bot's look point:
  // Rare's camera, HUD and split-screen then do the rest by themselves. When the drive ends while he still waits,
  // the body goes back to where it was parked.
  struct SpiderRide
  {
    bool on = false;
    float home[3] = {};       // his origin when the ride began
    float eye_h = 150.0f;     // his eye over his origin (measured from his camera when it is updated)
    float last_eye[3] = {};   // his camera's eye at its last update (any caller)
    uint32_t eye_n = 0, eye_used = 0;
    int64_t since = 0, last_log = 0;
    uint32_t moves = 0;
  };
  SpiderRide g_ride;
  double g_ride_yaw = 0.0; // the last yaw given (kept when the camera looks straight up or down)
  bool g_eye_ok[2] = {};
  std::chrono::steady_clock::time_point g_view_from{}; // round 2: the spawn the view watch counts from
  uint32_t g_view_lines = 0, g_view_cleared = 0;
  bool g_jet_woken = false;          // round 2: the jetpack guards woken once the opening settled
  uint32_t g_originals_left = ~0u; // round 2: counter_op_guards::OriginalsLeft at the last tick (~0 = not known yet)

  uint32_t EligibleLeft()
  {
    uint32_t left = 0;
    if (!g.ids.empty() && g_originals_left != ~0u)
    {
      // Round 2 (item 6): the originals alive and not taken (live + dormant; reinforcement waves never count).
      left = g_originals_left;
      left += PdzCounterOpRooftopsDropsLeft(); // round 13 e (0 unless Rooftops' last fight runs and its cvar is on)
      if (TestMaxLives() > 0)
      {
        left = std::min<uint32_t>(left, uint32_t(std::max(0, TestMaxLives() - g.lives)));
      }
      return left;
    }
    if (g.ids.empty())
    {
      // A clone mission (pdz_counterop_clones; Datacore, Arena): the waves have no fixed count, so player 2 is never
      // out for good; the live clones of the last scan are reported, never below 1.
      left = std::max<uint32_t>(g.rule_live, 1u);
    }
    for (const uint32_t id : g.ids)
    {
      left += !g.taken_ids.count(id) && !g.dead_ids.count(id);
    }
    if (TestMaxLives() > 0)
    {
      left = std::min<uint32_t>(left, uint32_t(std::max(0, TestMaxLives() - g.lives)));
    }
    return left;
  }

  // A team-0 guard's three masks (8252BD50..8252BD88). Called before ShowP2 on a spawn, so Rare's team lists
  // (82187D18 files a character under each bit of +128) file him under the guards' bit, as Rare files every guard.
  // Round 2: the masks are the replaced guard's own (TakeGuardTeam), so he is on that guard's side wherever a level
  // uses other bits: Datacore's spawner guards are team 0004 with enemies 0001, so the usual 0001 made every one of
  // them shoot at him (no damage: the friend test; 150-400 hits a run). Elsewhere a guard's masks are 0001/FFFF/0002.
  uint16_t g_team[3] = {kP2Team, kP2Allies, kP2Enemies};

  bool HostileToAgent(const uint8_t *base, uint32_t guard); // below

  // Round 5 (item 4): on Datacore, before the mid-level cutscene, a first-half guard (the agent's friendly in Rare's
  // data) may be taken.
  bool DatacoreFirstHalfTake(const uint8_t *base, uint32_t guard)
  {
    return REXCVAR_GET(pdz_counterop_datacore_first_half) &&
           counter_op_guards::Phase(base) == counter_op_guards::DatacorePhase::kFirstHalf &&
           counter_op_guards::IsFirstHalfGuard(base, guard) &&
           counter_op_guards::DatacoreAreaAllows(base, guard, nullptr);
  }

  void TakeGuardTeam(const uint8_t *base, uint32_t guard)
  {
    const uint16_t team = Ptr(guard) ? Load16(base, guard + kTeamBits) : 0;
    if (team == 0)
    {
      g_team[0] = kP2Team, g_team[1] = kP2Allies, g_team[2] = kP2Enemies;
      return;
    }
    // Round 5 (item 4): a first-half guard of Datacore is the agent's friend (0004 / FFFF / 0001). Player 2 alone
    // gets the masks Rare itself gives such a guard when it turns on the agent (823037A8-823037DC: team 0008, enemies
    // |= the agent's own bit 0x0080): the other first-half guards count him a friend (0008 is in their allies, not
    // in their enemies) and he counts them friends, the agent and he are enemies both ways. No guard's masks change.
    if (DatacoreFirstHalfTake(base, guard) && !HostileToAgent(base, guard))
    {
      constexpr uint16_t kIndoorTeam = 0x0004, kTurnedTeam = 0x0008, kAgentBit = 0x0080; // 822F2634, 823037CC, 822F160C
      g_team[0] = uint16_t((team & ~kIndoorTeam) | kTurnedTeam);
      g_team[1] = Load16(base, guard + kAllies);
      g_team[2] = uint16_t(Load16(base, guard + kEnemies) | kAgentBit);
      std::string area_note;
      counter_op_guards::DatacoreAreaAllows(base, guard, &area_note);
      REXLOG_INFO("pdz_counterop: pick: {}", area_note);
      REXLOG_INFO("pdz_counterop: datacore: first half: guard {:08X} is the agent's friendly ({:04X} / {:04X} / "
                  "{:04X}); player 2 alone gets Rare's turned-guard masks {:04X} / {:04X} / {:04X} (the agent is "
                  "{:04X} / {:04X} / {:04X}); no guard's masks are changed",
                  guard, team, Load16(base, guard + kAllies), Load16(base, guard + kEnemies), g_team[0], g_team[1],
                  g_team[2], Load16(base, g.p1c + kTeamBits), Load16(base, g.p1c + kAllies),
                  Load16(base, g.p1c + kEnemies));
      return;
    }
    // Round 3: exactly the replaced guard's masks, nothing invented. Round 2 changed them where the agent's side
    // counted the guard as a friend (Datacore's IndoorGuard01s, team 0004 inside the humans' allies): that made a
    // friendly guard hostile. Such guards are no longer taken at all (HostileToAgent, pdz_counterop_hostile_only), and
    // a guard that is the agent's enemy both ways needs no change.
    g_team[0] = team;
    g_team[1] = Load16(base, guard + kAllies);
    g_team[2] = Load16(base, guard + kEnemies);
  }

  // Fri 20 Nov night (pdz_counterop_team_check; play log 031, Trinity Escape). TakeGuardTeam above already gives
  // player 2 the taken guard's own masks (Rare's per-level values: the 'guards' side (team bits 0001, enemies 0002)'
  // line printed those, read from 's01b' / 's01c', not a constant); the fixed 0001 / FFFF / 0002 is used only for a
  // guard without a team bit. The friend rule is Rare's 8251EBD0: other +128 & (my +128 | my +130) and not other
  // +128 & my +132 (after its owner links, and the +5640 swap of allies / enemies while a Psychosis dart works).
  // The guarantee below works on the masks alone, before ShowP2 files him in the team lists: should the agent still
  // count him a friend, his team bits get one bit more that the agent counts as an enemy (Rare's own turned-guard
  // way, 823037A8: one bit more), or else are replaced by one outside her team and allies, in both cases a bit the
  // taken guard still counts among his allies and not his enemies (so the guards keep ignoring him); should he
  // count her a friend, her team bits go into his enemies.
  uint16_t g_taken_masks[3] = {0, 0, 0}; // the taken guard's masks at the takeover (for the 'team: takeover' line)
  bool g_taken_fallback = false;         // the guard had no team bit: Rare's team-0 guard masks were used
  std::string g_team_note;               // what the guarantee changed ("" = nothing)

  bool MaskFriend(uint16_t my_team, uint16_t my_allies, uint16_t my_enemies, uint16_t other_team)
  {
    return (other_team & (my_team | my_allies)) != 0 && (other_team & my_enemies) == 0;
  }

  void TeamGuarantee(const uint8_t *base, uint32_t guard)
  {
    g_team_note.clear();
    g_taken_fallback = !Ptr(guard) || Load16(base, guard + kTeamBits) == 0;
    for (int k = 0; k < 3; ++k)
    {
      g_taken_masks[k] = Ptr(guard) ? Load16(base, guard + kTeamBits + 2 * k) : 0;
    }
    if (!REXCVAR_GET(pdz_counterop_team_check) || !Ptr(g.p1c))
    {
      return;
    }
    const uint16_t jt = Load16(base, g.p1c + kTeamBits), ja = Load16(base, g.p1c + kAllies),
                   je = Load16(base, g.p1c + kEnemies);
    uint16_t t = g_team[0], a = g_team[1], e = g_team[2];
    const bool she_friend = MaskFriend(jt, ja, je, t), he_friend = MaskFriend(t, a, e, jt);
    if (!she_friend && !he_friend)
    {
      return;
    }
    if (he_friend)
    {
      e = uint16_t(e | jt);
    }
    if (she_friend)
    {
      const uint16_t ga = Ptr(guard) ? Load16(base, guard + kAllies) : a;
      const uint16_t ge = Ptr(guard) ? Load16(base, guard + kEnemies) : e;
      uint16_t in_her_enemies = 0, outside_her_side = 0;
      for (int b = 0; b < 16; ++b)
      {
        const uint16_t bit = uint16_t(1u << b);
        if (!(ga & bit) || (ge & bit))
        {
          continue; // the guards must keep counting him as one of theirs
        }
        if ((je & bit) && !in_her_enemies)
        {
          in_her_enemies = bit;
        }
        if (!((jt | ja | je) & bit) && !outside_her_side)
        {
          outside_her_side = bit;
        }
      }
      if (in_her_enemies)
      {
        t = uint16_t(t | in_her_enemies);
      }
      else if (outside_her_side)
      {
        t = outside_her_side;
      }
    }
    const bool fixed = !MaskFriend(jt, ja, je, t) && !MaskFriend(t, a, e, jt);
    g_team_note = fmt::format("ADJUSTED (by masks the agent {}counted him a friend and he {}counted her one): "
                              "{:04X} / {:04X} / {:04X} -> {:04X} / {:04X} / {:04X}{}",
                              she_friend ? "" : "never ", he_friend ? "" : "never ", g_team[0], g_team[1], g_team[2], t,
                              a, e, fixed ? "" : " (STILL friends by masks: no bit fits both sides)");
    g_team[0] = t, g_team[1] = a, g_team[2] = e;
  }

  // Rare's friend test (8251EBD0), from `me` about `other`: other's team bits are among my team or allies, and not
  // among my enemies.
  bool FriendOf(const uint8_t *base, uint32_t me, uint32_t other)
  {
    const uint16_t team = Load16(base, other + kTeamBits);
    return (team & (Load16(base, me + kTeamBits) | Load16(base, me + kAllies))) != 0 &&
           (team & Load16(base, me + kEnemies)) == 0;
  }

  // Round 3 (item 3): an enemy of the agent by Rare's own masks, both ways: the guard doesn't count the agent as a
  // friend (it attacks her) and the agent doesn't count the guard as one (her hits count: the hit handler cancels
  // hits between friends). An ordinary guard (0001 / FFFF / 0002) against the humans' 0002: hostile.
  bool HostileToAgent(const uint8_t *base, uint32_t guard)
  {
    if (!REXCVAR_GET(pdz_counterop_hostile_only) || !Ptr(g.p1c) || !Ptr(guard))
    {
      return true;
    }
    return !FriendOf(base, guard, g.p1c) && !FriendOf(base, g.p1c, guard);
  }

  // Round 5 (item 4): what player 2 may take: the agent's enemies, and Datacore's first-half guards in its first half.
  bool TakeableSide(const uint8_t *base, uint32_t guard)
  {
    // Round 6 (change of Thu 12 Nov): on Datacore, before the mid-level cutscene, Rare gives the agent exactly two
    // enemies (the placed server-room elites tut0 / tut1); after them comes the hack, then the CamSpy, spiderbot and
    // hangar sections, where every human of Rare's is her friendly trooper (the IndoorGuard01 spawners) or one of
    // those troopers turned by her own fire. None of these is taken: he comes back with the platform's squads.
    if (counter_op_guards::Phase(base) == counter_op_guards::DatacorePhase::kFirstHalf &&
        !counter_op_guards::IsDatacoreOpeningElite(base, guard) && !counter_op_guards::IsDatacoreExtraElite(guard) &&
        !REXCVAR_GET(pdz_counterop_datacore_first_half))
    {
      return false;
    }
    // Round 7: the two server-room elites belong to the virtual tutorial at the level's start. Once the hack's door
    // is passed (stage word 0x1000), the CamSpy section has begun (0x80) or the cutscene `mid` is over (0x100) they
    // are behind a door the agent never comes back through: not taken any more, even if she left them alive.
    if (counter_op_guards::IsDatacoreOpeningElite(base, guard) && !REXCVAR_GET(pdz_counterop_test_datacore_late_elites) &&
        (counter_op_guards::DatacoreStageWord(base) & (0x80u | 0x100u | 0x1000u)))
    {
      return false;
    }
    // The platform's jetpack users are not taken on Datacore (no takeover of a rider there): a guard on a jetpack,
    // and (round 7) every guard of the riders' spawner +0x7AC, seated yet or not.
    // Round 12 (item 6): with pdz_counterop_datacore_riders a rider still on foot is an enemy of the agent like any
    // other (masks 0008 / FFFF / 0020 against her 00B0) and may be taken; one already seated stays Rare's (Rare has
    // no hand-over of a seat in the air: its exit 826854F8 needs the jetpack landed, and an occupant that goes in
    // the air is Rare's jetpack death 82684718).
    if (counter_op_guards::Phase(base) != counter_op_guards::DatacorePhase::kNone &&
        ((counter_op_jetpack::Riding(base, guard) && !counter_op_jetpack::AirRider(base, guard)) || // round 13 c: the flying takeover
         (counter_op_guards::IsDatacoreJetpackRider(base, guard) && !REXCVAR_GET(pdz_counterop_datacore_riders))))
    {
      return false;
    }
    // Round 7: the agent's friendly troopers (the IndoorGuard01 spawners of the spiderbot section, which starts
    // after the cutscene `mid`, with the stage word's 0x100) are never taken, turned against her by her own fire or
    // not.
    if (counter_op_guards::Phase(base) != counter_op_guards::DatacorePhase::kNone &&
        counter_op_guards::IsFirstHalfGuard(base, guard) && !REXCVAR_GET(pdz_counterop_datacore_first_half))
    {
      return false;
    }
    return HostileToAgent(base, guard) || DatacoreFirstHalfTake(base, guard);
  }

  void ApplyTeam(uint8_t *base, uint32_t c)
  {
    Store16(base, c + kTeamBits, g_team[0]);
    Store16(base, c + kAllies, g_team[1]);
    Store16(base, c + kEnemies, g_team[2]);
  }

  bool TeamApplied(const uint8_t *base, uint32_t c)
  {
    return Load16(base, c + kTeamBits) == g_team[0] && Load16(base, c + kAllies) == g_team[1] &&
           Load16(base, c + kEnemies) == g_team[2];
  }

  // Player 2 faces the agent (Rare's look-angle setter, as Rare's spawn calls it after its move: yaw =
  // atan2(dx, dz) in radians, pitch 0). Logged with the view yaw (+0x1670, degrees) it left.
  // Round 6 (items 5, 6). A character's origin (the feet): Rare's getter 82151210. Character +48 is the centre of
  // the bounding sphere (waist height, about 85 cm up), not a position to move anyone to.
  bool OriginOf(PPCContext &ctx, uint8_t *base, uint32_t c, float out[3])
  {
    const uint32_t p = Ptr(c) ? Call(ctx, base, sub_82151210, c) : 0;
    if (!Ptr(p))
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      out[i] = LoadFloat(base, p + uint32_t(i) * 4);
    }
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
  }
  // A character's facing (radians, Rare's getter 82519A90).
  bool YawOf(PPCContext &ctx, uint8_t *base, uint32_t c, double &yaw)
  {
    if (!Ptr(c))
    {
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c;
    sub_82519A90(frame.ctx, base);
    yaw = frame.ctx.f1.f64;
    return std::isfinite(yaw);
  }

  void FaceAgent(PPCContext &ctx, uint8_t *base)
  {
    if (!Ptr(g.p1c) || !Ptr(g.p2c))
    {
      return;
    }
    const float dx = LoadFloat(base, g.p1c + kPosition) - LoadFloat(base, g.p2c + kPosition);
    const float dz = LoadFloat(base, g.p1c + kPosition + 8) - LoadFloat(base, g.p2c + kPosition + 8);
    if (!std::isfinite(dx) || !std::isfinite(dz) || dx * dx + dz * dz < 1.0f)
    {
      REXLOG_INFO("pdz_counterop: spawn (f): facing not set (the agent's position is unusable or on top of him)");
      return;
    }
    const double yaw = std::atan2(double(dx), double(dz));
    const float before = LoadFloat(base, g.p2c + kViewYaw);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g.p2c;
      frame.ctx.f1.f64 = 0.0;
      frame.ctx.f2.f64 = yaw;
      sub_825AD3B0(frame.ctx, base);
    }
    REXLOG_INFO("pdz_counterop: spawn (f): facing the agent: yaw {:.1f} deg given (Rare's 825AD3B0, radians {:.3f}); "
                "view yaw +0x1670 {:.1f} -> {:.1f}",
                yaw * 57.29578, yaw, before, LoadFloat(base, g.p2c + kViewYaw));
  }

  // Testing (pdz_counterop_test_view_diff): his character, participant and player object 5 s after the first spawn
  // (first person) against the same 5 s after each later spawn (third person after a respawn): the words that differ.
  std::vector<uint8_t> g_vd_first[3];
  uint32_t g_fp_mode44 = 0, g_fp_mode368 = 0; // the player object's words in first person (pdz_counterop_fix_view_mode)
  void ViewDiff(const uint8_t *base, uint32_t p2c)
  {
    const uint32_t part = Load32(base, p2c + kParticipantOf), player = Load32(base, p2c + 0x19A4);
    const uint32_t at[3] = {p2c, part, player};
    const uint32_t size[3] = {0x2400, 0x1000, 0x1000};
    const char *name[3] = {"character", "participant", "player"};
    for (int k = 0; k < 3; ++k)
    {
      if (!Ptr(at[k]))
      {
        continue;
      }
      std::vector<uint8_t> now(base + at[k], base + at[k] + size[k]);
      if (g_vd_first[k].empty())
      {
        g_vd_first[k] = now;
        REXLOG_INFO("pdz_counterop: view diff: {} {:08X} saved (first person)", name[k], at[k]);
        continue;
      }
      std::string line;
      uint32_t n = 0;
      for (uint32_t off = 0; off + 4 <= size[k]; off += 4)
      {
        const uint32_t a = Load32(g_vd_first[k].data(), off), b = Load32(now.data(), off);
        if (a == b)
        {
          continue;
        }
        ++n;
        line += fmt::format(" +{:X}:{:08X}>{:08X}", off, a, b);
        if (line.size() > 900)
        {
          REXLOG_INFO("pdz_counterop: view diff: {}{}", name[k], line);
          line.clear();
        }
      }
      REXLOG_INFO("pdz_counterop: view diff: {}{} ({} words differ)", name[k], line, n);
    }
  }

  // Testing (pdz_counterop_test_agent_aim): the agent's view at player 2 (yaw as FaceAgent; pitch up positive, 1.2 m
  // above his feet), through Rare's 825AD3B0.
  void AimAgentAtP2(PPCContext &ctx, uint8_t *base, bool reverse = false)
  {
    if (!Ptr(g.p1c) || !Ptr(g.p2c))
    {
      return;
    }
    // From her camera eye to a point 35 cm under his (his chest), when both cameras are known; else the positions.
    // Round 4 (pdz_counterop_test_p2_attack): reverse = player 2's view at the agent, the same way.
    const bool eyes = g_eye_ok[0] && g_eye_ok[1];
    const int a = reverse ? 1 : 0, b = reverse ? 0 : 1; // a aims at b
    const uint32_t ac = reverse ? g.p2c : g.p1c, bc = reverse ? g.p1c : g.p2c;
    const float dx = eyes ? g_eye[b][0] - g_eye[a][0] : LoadFloat(base, bc + kPosition) - LoadFloat(base, ac + kPosition);
    const float dy = eyes ? g_eye[b][1] - 35.0f - g_eye[a][1]
                          : LoadFloat(base, bc + kPosition + 4) + 120.0f - (LoadFloat(base, ac + kPosition + 4) + 160.0f);
    const float dz = eyes ? g_eye[b][2] - g_eye[a][2] : LoadFloat(base, bc + kPosition + 8) - LoadFloat(base, ac + kPosition + 8);
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(flat) || flat < 50.0f)
    {
      return;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = ac;
    // Measured (l_shots: her shots went +1.9 deg when -1.8 was wanted, the yaw exact): 825AD3B0's pitch is positive
    // DOWN. With the eyes known the sign is flipped; the old position-based aim is left as it was.
    frame.ctx.f1.f64 = eyes ? -std::atan2(double(dy), double(flat)) : std::atan2(double(dy), double(flat));
    frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
    sub_825AD3B0(frame.ctx, base);
  }

  // Round 10 (item 3): see SpiderRide. Every tick, after the drive.
  void SpiderRideTick(PPCContext &ctx, uint8_t *base)
  {
    float cam[3], look[3], up[3];
    bool roll = false;
    const bool want = g.started && g.out && Ptr(g.p2c) && g.scratch && PdzCounterOpSpiderCamera(cam, look, up, roll) &&
                      std::isfinite(cam[0]) && std::isfinite(cam[1]) && std::isfinite(cam[2]);
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (!want)
    {
      if (g_ride.on)
      {
        g_ride.on = false;
        const bool back = g.started && g.out && Ptr(g.p2c) && g.scratch;
        if (back)
        {
          for (int i = 0; i < 3; ++i)
          {
            StoreFloat(base, g.scratch + 4 * i, g_ride.home[i]);
          }
          Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
        }
        REXLOG_INFO("pdz_counterop: spider: CAMERA HANDOVER BACK: the drive is over after {:.1f} s ({} moves of his "
                    "body): {}",
                    double(now - g_ride.since) / 1000.0, g_ride.moves,
                    back ? fmt::format("he still waits: his hidden body is parked again at ({:.0f}, {:.0f}, {:.0f}); "
                                       "his view and controller go to his next guard when he gets one",
                                       g_ride.home[0], g_ride.home[1], g_ride.home[2])
                         : std::string("he is back in the world (a guard) or the game is over: his view and "
                                       "controller are his guard's"));
      }
      return;
    }
    float origin[3] = {};
    if (!OriginOf(ctx, base, g.p2c, origin))
    {
      for (int i = 0; i < 3; ++i)
      {
        origin[i] = LoadFloat(base, g.p2c + kPosition + 4 * i);
      }
    }
    const bool fresh = g_ride.eye_n != g_ride.eye_used;
    if (!g_ride.on)
    {
      const float keep_h = g_ride.eye_h;
      const uint32_t keep_n = g_ride.eye_n, keep_used = g_ride.eye_used;
      float keep_eye[3];
      std::memcpy(keep_eye, g_ride.last_eye, sizeof(keep_eye));
      g_ride = SpiderRide{};
      g_ride.on = true;
      g_ride.eye_h = keep_h;
      g_ride.eye_n = keep_n;
      g_ride.eye_used = keep_used;
      std::memcpy(g_ride.last_eye, keep_eye, sizeof(keep_eye));
      std::memcpy(g_ride.home, origin, sizeof(origin));
      g_ride.since = now;
      REXLOG_INFO("pdz_counterop: spider: CAMERA HANDOVER: player 2's view goes to his spiderbot: his hidden body "
                  "{:08X} (origin ({:.0f}, {:.0f}, {:.0f})) is carried with it from now on, so that his eye is the "
                  "chase camera ({:.0f}, {:.0f}, {:.0f}) looking at ({:.0f}, {:.0f}, {:.0f}); his camera object "
                  "{:08X} ({}); Rare's first-person camera of his own character draws the view",
                  g.p2c, origin[0], origin[1], origin[2], cam[0], cam[1], cam[2], look[0], look[1], look[2], g.p2_mode,
                  Ptr(g.p2_mode) ? "updated by Rare's update, called for him every frame while he is out"
                                 : "NOT KNOWN YET: Rare has made him no camera in this level (no guard taken so far), "
                                   "his half may stay black until his first guard");
    }
    // His eye over his origin, measured whenever his camera was updated since our last move.
    if (fresh)
    {
      const float h = g_ride.last_eye[1] - origin[1];
      const float ex = g_ride.last_eye[0] - origin[0], ez = g_ride.last_eye[2] - origin[2];
      if (std::isfinite(h) && h > 40.0f && h < 230.0f && ex * ex + ez * ez < 100.0f * 100.0f)
      {
        g_ride.eye_h = h;
      }
      g_ride.eye_used = g_ride.eye_n;
    }
    const float to[3] = {cam[0], cam[1] - g_ride.eye_h, cam[2]};
    for (int i = 0; i < 3; ++i)
    {
      StoreFloat(base, g.scratch + 4 * i, to[i]);
    }
    Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
    ++g_ride.moves;
    const float dx = look[0] - cam[0], dy = look[1] - cam[1], dz = look[2] - cam[2];
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (std::isfinite(flat) && std::isfinite(dy) && (flat > 1.0f || std::fabs(dy) > 1.0f))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g.p2c;
      // 825AD3B0's pitch is positive DOWN (measured in round 3); straight up / down is held just short of the pole
      frame.ctx.f1.f64 = std::clamp(-std::atan2(double(dy), double(std::max(flat, 0.001f))), -1.45, 1.45);
      frame.ctx.f2.f64 = flat > 1.0f ? std::atan2(double(dx), double(dz)) : g_ride_yaw;
      g_ride_yaw = frame.ctx.f2.f64;
      sub_825AD3B0(frame.ctx, base);
    }
    if (now - g_ride.last_log >= (g_ride.moves < 200 ? 1000 : 5000))
    {
      g_ride.last_log = now;
      const float ex = g_ride.last_eye[0] - cam[0], ey = g_ride.last_eye[1] - cam[1], ez = g_ride.last_eye[2] - cam[2];
      REXLOG_INFO("pdz_counterop: spider: view: chase camera ({:.0f}, {:.0f}, {:.0f}) -> look ({:.0f}, {:.0f}, {:.0f}); "
                  "his body's origin put at ({:.0f}, {:.0f}, {:.0f}) (eye height {:.0f}); his camera's eye at its "
                  "last update ({:.0f}, {:.0f}, {:.0f}), {:.0f} cm from the chase camera (camera updates so far {}; "
                  "a few cm = the view follows)",
                  cam[0], cam[1], cam[2], look[0], look[1], look[2], to[0], to[1], to[2], g_ride.eye_h,
                  g_ride.last_eye[0], g_ride.last_eye[1], g_ride.last_eye[2], std::sqrt(ex * ex + ey * ey + ez * ez),
                  g_ride.eye_n);
    }
  }

  // The guard goes through Rare's own script despawn (guard vtable +832, sub_825818E0(character, 1): physics and
  // world removal, its hidden flag at +6272 bit 0x10; level scripts hide whole groups with it, 8232C2E0). Not a
  // death: no corpse, no dropped gun, no kill event (a script waiting for "all dead" doesn't count it).
  std::string RemoveGuard(PPCContext &ctx, uint8_t *base, uint32_t guard)
  {
    if (Load32(base, guard + kHidden) & 0x10)
    {
      return std::string("already hidden (dormant): left so, kept hidden from now on");
    }
    Call(ctx, base, sub_825818E0, guard, 1);
    return (Load32(base, guard + kHidden) & 0x10)
               ? std::string("hidden through Rare's script despawn (vtable +832: out of the world, no corpse, not a death)")
               : std::string("hide call made, but its flag (+6272 0x10) is not set");
  }

  void CallEachChild(PPCContext &ctx, uint8_t *base, uint32_t c, PPCFunc *fn, bool with_zero)
  {
    uint32_t child = Load32(base, c + kChildFirst);
    for (int n = 0; Ptr(child) && n < 64; ++n, child = Load32(base, child + kChildNext))
    {
      if (with_zero)
      {
        Call(ctx, base, fn, child, 0);
      }
      else
      {
        Call(ctx, base, fn, child);
      }
    }
  }

  // Player 2 out of the world (log 304: he stayed Joanna's co-op partner): the generic steps of Rare's hide
  // (sub_825818E0's hide path: 82189B08, physics out for his attached objects and himself, team lists out, world out,
  // 82156E20), so he isn't drawn, collided with, seen or targeted; and Rare's player action lock on every bit
  // (8254D8F0), so he can't move, fire, use or pick anything up. He stays alive for Rare's co-op rules (no "partner
  // down", no revive); his view follows the agent.
  void HideP2(PPCContext &ctx, uint8_t *base, const char *why)
  {
    counter_op_river::LeaveSeat(ctx, base, g.p2c, why); // round 13 h: River, a hovercraft driver leaves his seat first
    PdzCounterOpOutpostBeforeHide(ctx, base, g.p2c); // round 13 f: Outpost, the gunner of the hovercraft leaves the gun first
    PdzCounterOpTurretBeforeHide(ctx, base, g.p2c, why); // round 13b q: the gunner of a stationary turret leaves it first
    if (Ptr(g.p2c) && counter_op_jetpack::Riding(base, g.p2c))
    {
      REXLOG_INFO("pdz_counterop: player 2 rides a jetpack: not hidden ({})", why);
      return;
    }
    if (g.out || !Ptr(g.p2c))
    {
      return;
    }
    const uint32_t c = g.p2c, world = Load32(base, kGame + 24);
    // Round 16 (item 9): his armour bar is emptied while he waits (his HUD is hidden then), so the next takeover's
    // armour (the guard's) never shows as a drop from the co-op start's 100.
    if (REXCVAR_GET(pdz_counterop_guard_armour))
    {
      if (const uint32_t part = Load32(base, c + kParticipantOf); Ptr(part))
      {
        StoreFloat(base, part + 988, 0.0f);
      }
    }
    Call(ctx, base, sub_82189B08, c);
    CallEachChild(ctx, base, c, sub_82517600, false);
    Call(ctx, base, sub_82187E98, c);
    Call(ctx, base, sub_82517600, c);
    if (Ptr(world))
    {
      Call(ctx, base, sub_82527D78, world, c);
    }
    Call(ctx, base, sub_82156E20, c, 1);
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c;
    frame.ctx.r4.u64 = kLockAll;
    sub_8254D8F0(frame.ctx, base);
    g.out = true;
    g.out_since = std::chrono::steady_clock::now();
    g.p2_cam_updates = g.p1_cam_updates = g.spectate_writes = 0;
    g.spectate_reported = false;
    REXLOG_INFO("pdz_counterop: player 2 out of the world ({}): hidden (Rare's hide steps: physics, team lists, world) "
                "and locked (Rare's action lock, every bit); he counts as alive for the co-op rules (no partner game "
                "over, no revive); his body stays at ({:.0f}, {:.0f}, {:.0f}), his view follows the agent (spectate "
                "camera)",
                why, LoadFloat(base, c + kPosition), LoadFloat(base, c + kPosition + 4), LoadFloat(base, c + kPosition + 8));
  }

  // Mon 23 Nov (co19, pdz_counterop_spawn_clear_locks). Log 056 (Bridge, 18:33:36): he was killed in a jetpack seat, and
  // his next body never got a weapon ("hands were empty" every 3.5 s to the end; the same in every earlier test log
  // after a death in the seat). Rare's action lock is one COUNTER per action (8254D8F0 adds one to each bit of its
  // mask, 8254D968 takes one off; 8254DAB8 refuses an action while its counter is not 0). The jetpack's mount takes the
  // riding lock (mask 0x00800C04_8404803E: the weapon switch, Use, ...) and only its dismount gives it back; a rider
  // who dies in the seat never dismounts, so those counters stay at 1 for good. Our hide adds one to every counter and
  // our show takes one off every counter, so they pass through it unchanged. When he comes back into the world with a
  // new body nothing of his last life may still be locked: every counter that is left is logged and put to 0.
  constexpr uint32_t kLockCounters = 5785, kLockCount = 64;
  void ClearLeftLocks(uint8_t *base, uint32_t c)
  {
    if (!pdz::guestmem::Ok(base, c + kLockCounters, kLockCount))
    {
      return;
    }
    std::string left;
    uint32_t n = 0;
    for (uint32_t i = 0; i < kLockCount; ++i)
    {
      if (base[c + kLockCounters + i])
      {
        left += fmt::format("{}{}:{}", left.empty() ? "" : " ", i, base[c + kLockCounters + i]);
        ++n;
      }
    }
    if (!n)
    {
      return;
    }
    const bool clear = REXCVAR_GET(pdz_counterop_spawn_clear_locks);
    if (clear)
    {
      std::memset(base + c + kLockCounters, 0, kLockCount);
    }
    static uint32_t lines = 0;
    if (lines++ < 40)
    {
      REXLOG_INFO("pdz_counterop: locks: player 2 comes back into the world with {} of Rare's action lock counter(s) "
                  "still held from his last life (bit:count {}): {} (pdz_counterop_spawn_clear_locks)",
                  n, left, clear ? "put to 0: nothing of his last life locks his new body" : "LEFT as they are (switch off)");
    }
  }

  // Back in the world (the reverse: sub_825818E0's restore path without its NPC-only repositioning), unlocked.
  void ShowP2(PPCContext &ctx, uint8_t *base)
  {
    PdzCounterOpRareSpectateOff(ctx, base, "he gets a body: his own view back"); // Sat 21 Nov (spectate job)
    if (Ptr(g.p2c) && counter_op_jetpack::Riding(base, g.p2c))
    {
      return; // a rider is out of the world list by Rare's design (attached to the jetpack)
    }
    if (!g.out || !Ptr(g.p2c))
    {
      return;
    }
    const uint32_t c = g.p2c, world = Load32(base, kGame + 24);
    CallEachChild(ctx, base, c, sub_82517530, true);
    Call(ctx, base, sub_82187D18, c);
    Call(ctx, base, sub_82517530, c, 0);
    if (Ptr(world))
    {
      Call(ctx, base, sub_82527C90, world, c);
    }
    Call(ctx, base, sub_82156EF0, c, 1);
    PdzCounterOpViewRestorePerception(ctx, base, c); // round 2: HideP2's 82189B08 freed it (Rare's restore retakes it)
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c;
    frame.ctx.r4.u64 = kLockAll;
    sub_8254D968(frame.ctx, base);
    g.out = false;
    ClearLeftLocks(base, c); // Mon 23 Nov (co19)
  }

  // Round 12 (item 5): where the hidden body waits. Rare keeps the sky / fog state per view and changes it only at
  // the level's boundary triggers (counter_op_view.cpp's round 12 section), so the place itself decides nothing; but
  // a body left where he died stays in a part of the level the agent may have left (Datacore: under water while she
  // is on the rig). The one place that is known to be dry when she is, loaded and inside the level is where the
  // agent stands: the hidden, locked, out-of-the-world body is kept at her origin (Rare's move 825AD148, the call
  // every spawn and the spiderbot's camera use). Not in a cutscene, not during a spiderbot drive (the body is the
  // camera then), not while the replaced guard's kill is pending, not while she is down.
  int64_t g_park_last_ms = 0;
  uint32_t g_park_moves = 0;
  void ParkTick(PPCContext &ctx, uint8_t *base, bool cutscene)
  {
    if (!REXCVAR_GET(pdz_counterop_park_with_agent) || !g.started || !g.out || cutscene || g_ride.on || g.kill_guard ||
        g.revive_participant || g.wait_getup || PdzCounterOpSpiderDriving() || !Ptr(g.p1c) || !Ptr(g.p2c))
    {
      return;
    }
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (now - g_park_last_ms < 2000)
    {
      return;
    }
    g_park_last_ms = now;
    float hers[3] = {}, his[3] = {};
    if (!Alive(ctx, base, g.p1c) || !OriginOf(ctx, base, g.p1c, hers) || !OriginOf(ctx, base, g.p2c, his) ||
        !std::isfinite(hers[0]) || !std::isfinite(hers[1]) || !std::isfinite(hers[2]) ||
        Dist3(hers[0], hers[1], hers[2], his[0], his[1], his[2]) < 600.0f)
    {
      return;
    }
    if (!g.scratch)
    {
      g.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x40);
    }
    if (!g.scratch)
    {
      return;
    }
    for (int i = 0; i < 3; ++i)
    {
      StoreFloat(base, g.scratch + 4 * i, hers[i]);
    }
    Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
    const uint32_t n = ++g_park_moves;
    if (n <= 5 || n % 30 == 0)
    {
      REXLOG_INFO("pdz_counterop: park #{}: player 2's hidden body moved from ({:.0f}, {:.0f}, {:.0f}) to where the "
                  "agent stands ({:.0f}, {:.0f}, {:.0f}) (Rare's move 825AD148; it stays out of the world, locked); "
                  "lines: the first 5, then every 30th",
                  n, his[0], his[1], his[2], hers[0], hers[1], hers[2]);
    }
  }

  // Round 16 (item 1, pdz_counterop_wait_follow): every wait follows the agent. Rare makes a player's gameplay camera
  // object only while he is in the world (log 027: after Datacore's RESTART FROM CHECKPOINT player 2 was re-created
  // and hidden in the same frame, Rare never made his camera, so the follow below had nothing to drive and his half
  // showed a parked camera for 7 minutes; after his first guard it worked again). While he waits with no camera
  // object known, outside cutscenes, his hidden body is put 3 m under the agent's feet, back in the world for one
  // tick (Rare's restore steps, ShowP2), and hidden again as soon as Rare's update has noted his camera (or after
  // 0.25 s). From then on the spectate write follows her as at every other wait. At most 6 tries a level, 3 s apart.
  int64_t g_probe_at_ms = 0;
  bool g_probe_open = false;
  uint32_t g_probe_tries = 0;

  void CameraProbeTick(PPCContext &ctx, uint8_t *base, bool cutscene)
  {
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (!g_probe_open && PdzCounterOpRareSpectateActive())
    {
      return; // Sat 21 Nov (spectate job): Rare's chase camera is his view; no camera object of his own is needed
    }
    if (g_probe_open)
    {
      if (g.out || !g.need_guard)
      {
        g_probe_open = false; // a spot was taken meanwhile (or something hid him): nothing to undo
        return;
      }
      if (Ptr(g.p2_mode) || now - g_probe_at_ms >= 250 || cutscene)
      {
        g_probe_open = false;
        HideP2(ctx, base, Ptr(g.p2_mode) ? "camera probe: his camera is made" : "camera probe: no camera yet");
        REXLOG_INFO("pdz_counterop: wait follow: probe #{}: {} after {} ms in the world (under the agent's feet)",
                    g_probe_tries,
                    Ptr(g.p2_mode) ? fmt::format("Rare made player 2's camera {:08X}: his view follows the agent "
                                                 "while he waits",
                                                 g.p2_mode)
                                   : std::string("Rare made no camera for him yet"),
                    now - g_probe_at_ms);
      }
      return;
    }
    if (!REXCVAR_GET(pdz_counterop_wait_follow) || !g.started || !g.out || !g.need_guard || cutscene ||
        Ptr(g.p2_mode) || g_ride.on || g.kill_guard || g.revive_participant || g.wait_getup || g.hide_pending ||
        PdzCounterOpSpiderDriving() || !Ptr(g.p1c) || !Ptr(g.p2c) || g_probe_tries >= 6 ||
        now - g_probe_at_ms < 3000 || counter_op_jetpack::Riding(base, g.p2c) ||
        std::chrono::steady_clock::now() - g.out_since < std::chrono::seconds(2)) // (not as a level / its opening starts)
    {
      return;
    }
    float hers[3] = {};
    if (!Alive(ctx, base, g.p1c) || !OriginOf(ctx, base, g.p1c, hers) || !std::isfinite(hers[0]) ||
        !std::isfinite(hers[1]) || !std::isfinite(hers[2]) ||
        std::fabs(hers[0]) + std::fabs(hers[1]) + std::fabs(hers[2]) < 1.0f) // (t14 Bridge: (0, 0, 0) in the load)
    {
      return;
    }
    if (!g.scratch)
    {
      g.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x40);
    }
    if (!g.scratch)
    {
      return;
    }
    g_probe_at_ms = now;
    ++g_probe_tries;
    StoreFloat(base, g.scratch + 0, hers[0]);
    StoreFloat(base, g.scratch + 4, hers[1] - 300.0f);
    StoreFloat(base, g.scratch + 8, hers[2]);
    Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
    ShowP2(ctx, base);
    g_probe_open = !g.out;
    REXLOG_INFO("pdz_counterop: wait follow: probe #{}: player 2 waits with no camera object (a reload or a level "
                "start): his hidden body is in the world for a moment 3 m under the agent ({:.0f}, {:.0f}, {:.0f}) so "
                "that Rare makes his camera",
                g_probe_tries, hers[0], hers[1] - 300.0f, hers[2]);
  }
  void CameraProbeReset()
  {
    g_probe_at_ms = 0;
    g_probe_open = false;
    g_probe_tries = 0;
  }

  // Sat 21 Nov (pdz_counterop_wait_view_upright). Log 033 / co_jungle_waitview (captures): player 2's half of the
  // screen while he waits is Rare's first-person view from HIS OWN hidden body (round 10 found the same for the
  // spiderbot; the follow write into his camera object does not reach the picture). The camera probe left that body
  // 3 m under the agent's feet: in the Jungle under the water, so his half showed the world from under the water,
  // upside down (sky at the bottom). After a death it stays where he died, looking wherever that was. Now, while he
  // waits (out of the world, no spiderbot drive, no cutscene, no probe open), his hidden body is carried every tick to
  // 2.5 m behind the agent and 0.7 m above her feet, turned the way she looks and tilted 8 degrees down (Rare's move
  // 825AD148 and look angles 825AD3B0, as the spiderbot ride does): an upright view behind and above her. When she
  // can't see that spot (a wall behind her; tested twice a second) the body goes to where she stands instead.
  uint32_t g_wait_moves = 0;
  int64_t g_wait_log_ms = 0, g_wait_check_ms = 0;
  bool g_wait_behind_ok = true;
  void WaitViewTick(PPCContext &ctx, uint8_t *base, bool cutscene)
  {
    if (PdzCounterOpRareSpectateActive())
    {
      return; // Sat 21 Nov (spectate job): Rare's chase camera is his view; this stand-in is the fallback only
    }
    if (!PdzCounterOpWaitViewUprightOn() || !g.started || !g.out || !g.need_guard || cutscene || g_ride.on ||
        g.kill_guard || g.revive_participant || g.wait_getup || g.hide_pending || g_probe_open ||
        PdzCounterOpSpiderDriving() || !Ptr(g.p1c) || !Ptr(g.p2c) || counter_op_jetpack::Riding(base, g.p2c))
    {
      return;
    }
    float hers[3] = {};
    if (!Alive(ctx, base, g.p1c) || !OriginOf(ctx, base, g.p1c, hers) || !std::isfinite(hers[0]) ||
        !std::isfinite(hers[1]) || !std::isfinite(hers[2]) ||
        std::fabs(hers[0]) + std::fabs(hers[1]) + std::fabs(hers[2]) < 1.0f)
    {
      return;
    }
    if (!g.scratch)
    {
      g.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x40);
    }
    if (!g.scratch)
    {
      return;
    }
    constexpr float kBack = 250.0f, kUp = 70.0f, kPitchDownDeg = 8.0f;
    const float yaw_deg = LoadFloat(base, g.p1c + kViewYaw);
    const float yaw = std::isfinite(yaw_deg) ? yaw_deg * 3.14159265f / 180.0f : 0.0f;
    float spot[3] = {hers[0] - std::sin(yaw) * kBack, hers[1] + kUp, hers[2] - std::cos(yaw) * kBack};
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (now - g_wait_check_ms >= 500)
    {
      g_wait_check_ms = now;
      // Her line of sight to the spot (at about his eye height): a wall behind her keeps the body at her own place.
      g_wait_behind_ok = counter_op_guards::LineClear(ctx, base, g.p1c, spot[0], spot[1] + 150.0f, spot[2]);
    }
    if (!g_wait_behind_ok)
    {
      spot[0] = hers[0], spot[1] = hers[1], spot[2] = hers[2];
    }
    for (int i = 0; i < 3; ++i)
    {
      StoreFloat(base, g.scratch + 4 * i, spot[i]);
    }
    Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g.p2c;
      frame.ctx.f1.f64 = double(kPitchDownDeg) * 3.14159265 / 180.0; // 825AD3B0's pitch is positive DOWN
      frame.ctx.f2.f64 = double(yaw);
      sub_825AD3B0(frame.ctx, base);
    }
    ++g_wait_moves;
    if (now - g_wait_log_ms >= 1000)
    {
      g_wait_log_ms = now;
      float his[3] = {};
      OriginOf(ctx, base, g.p2c, his);
      REXLOG_INFO("pdz_counterop: wait view (pdz_counterop_wait_view_upright): player 2's hidden body (his view is its "
                  "first-person eye) at ({:.0f}, {:.0f}, {:.0f}), {}: {:.1f} m from the agent's origin, {:.1f} m above "
                  "it, facing her look (yaw {:.1f} deg), pitch {:.0f} deg down, roll 0 (Rare's look angles carry none); "
                  "{} moves so far",
                  his[0], his[1], his[2],
                  g_wait_behind_ok ? "behind and above the agent" : "at the agent's own place (a wall behind her)",
                  Dist3(his[0], his[1], his[2], hers[0], hers[1], hers[2]) / 100.0f, (his[1] - hers[1]) / 100.0f,
                  yaw_deg, kPitchDownDeg, g_wait_moves);
    }
  }

  // ---- The spots ---------------------------------------------------------------------------------------------

  // Why a pick found what it found: the characters through each test, by source and by rule.
  struct PickDiag
  {
    uint32_t registry_objects = 0, world_objects = 0, characters = 0, guards = 0; // SP guards (any state)
    uint32_t by_list = 0, by_rule = 0, denied = 0, unlisted_failed = 0;           // eligibility by setup
    uint32_t eligible = 0, live = 0, dormant = 0, dead = 0, taken = 0, unspawned = 0;
    uint32_t clones = 0, no_id = 0;                                               // the no-id rule's sources
    uint32_t occupied = 0, repeat = 0, no_setup = 0; // no-take mode: a character within 1.2 m; the last life's spot
    uint32_t friendly = 0; // round 3: guards that are not the agent's enemies by Rare's masks (never taken)
    uint32_t closed = 0;   // round 8: still at his setup position behind a locked, closed door
    std::vector<std::pair<uint32_t, uint32_t>> unlisted; // the first 10 setup ids not in the list (key, string)
    bool registry_ok = false;
  };

  std::string CountsText(const PickDiag &d)
  {
    std::string ids;
    for (const auto &[id, ptr] : d.unlisted)
    {
      ids += (ids.empty() ? "" : ",") + IdChars(id);
    }
    if (!TakeGuard())
    {
      return fmt::format("free spots {} ({} live guards' + {} dead guards' + {} dormant guards' setup positions; no "
                         "guard touched; by the id list {}, by the no-id rule {}; {} occupied (a character within "
                         "{:.1f} m), {} the last life's, {} without a setup or clones; {} denied, {} unlisted "
                         "and failing the rule{}; {} of the {} listed ids with no guard built)",
                         d.eligible, d.live, d.dead, d.dormant, d.by_list, d.by_rule, d.occupied, kFreeRadius / 100.0f,
                         d.repeat, d.no_setup, d.denied, d.unlisted_failed,
                         ids.empty() ? std::string() : " (" + ids + ")", d.unspawned, g.ids.size());
    }
    return fmt::format("spots left {} ({} dormant + {} live; by the id list {}, by the no-id rule {}; {} taken, {} "
                       "dead; {} denied, {} the agent's friendlies, {} unlisted and failing the rule{}; {} of the {} "
                       "listed ids with no guard built)",
                       d.eligible, d.dormant, d.live, d.by_list, d.by_rule, d.taken, d.dead, d.denied, d.friendly,
                       d.unlisted_failed, ids.empty() ? std::string() : " (" + ids + ")", d.unspawned, g.ids.size());
  }

  // Every object Rare's registry holds (hidden ones included), then the world list; each once.
  void CollectObjects(const uint8_t *base, std::vector<uint32_t> &out, PickDiag &d)
  {
    std::unordered_set<uint32_t> seen;
    const uint32_t so = Load32(base, kGame);
    const uint32_t registry = Ptr(so) ? Load32(base, so + kRegistry) : 0;
    if (Ptr(registry))
    {
      const uint32_t reg = registry + kRegistryBase;
      const uint32_t group_end = Load32(base, reg + kGroupEnd);
      uint32_t group = Load32(base, reg + kGroupFirst);
      for (int gi = 0; Ptr(group) && group != group_end && gi < 4096; ++gi, group = Load32(base, group + kGroupNext))
      {
        const uint32_t end = Load32(base, group + kGroupListEnd);
        uint32_t node = Load32(base, group + kGroupListFirst);
        for (int ni = 0; Ptr(node) && node != end && ni < 16384; ++ni, node = Load32(base, node + kNodeNext))
        {
          const uint32_t o = Load32(base, node + kNodeObject);
          if (Ptr(o) && seen.insert(o).second)
          {
            out.push_back(o);
            ++d.registry_objects;
          }
        }
      }
    }
    // Rare's character list (0x82E454A0 +0x710 .. +0x714, node +0 character, +8 next): every character the level
    // built, dormant guards included (round 2: the registry above gives nothing in TU3, so dormant guards were never
    // found and forced / counted).
    {
      constexpr uint32_t kCharList = 0x82E454A0;
      uint32_t node = Load32(base, kCharList + 0x710);
      const uint32_t end = Load32(base, kCharList + 0x714);
      for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
      {
        const uint32_t o = Load32(base, node);
        node = Load32(base, node + 8);
        if (Ptr(o) && seen.insert(o).second)
        {
          out.push_back(o);
          ++d.registry_objects;
        }
      }
    }
    d.registry_ok = d.registry_objects > 0;
    const uint32_t world = Load32(base, kGame + 24);
    if (Ptr(world))
    {
      const uint32_t end = Load32(base, world + kWorldEnd);
      uint32_t node = Load32(base, world + kWorldFirst);
      for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
      {
        const uint32_t o = Load32(base, node + 4);
        ++d.world_objects;
        if (Ptr(o) && seen.insert(o).second)
        {
          out.push_back(o);
        }
      }
    }
  }

  // The positions of the characters in the world list (guards alive or dead, humans, bots), player 2 left out: a
  // no-take spot is free when none is within kFreeRadius.
  void WorldCharacterPositions(const uint8_t *base, std::vector<std::array<float, 3>> &out)
  {
    const uint32_t world = Load32(base, kGame + 24);
    if (!Ptr(world))
    {
      return;
    }
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      if (!Ptr(o) || o == g.p2c)
      {
        continue;
      }
      const uint32_t vt = Load32(base, o);
      if (vt == kGuardVtable || vt == kHumanVtable || vt == 0x820BF3B8u)
      {
        out.push_back({LoadFloat(base, o + kPosition), LoadFloat(base, o + kPosition + 4),
                       LoadFloat(base, o + kPosition + 8)});
      }
    }
  }

  // Round 8 (item 3): a guard the level script woke early who still stands at his setup position behind a locked,
  // closed door (Rooftops' 's17a' in the bridge room: woken by message 7/3 with the roof guards, he waits there until
  // the script opens his door) is not taken: player 2 would be shut in. Rare's pad graph links run through locked
  // doors, so the test is the door itself: he has not left his setup position (3 m, 1.5 m in height) and a locked
  // (byte +0x4A0 or +0x4A1) and closed (state +0x49C == 1) door is within 6 m (2.5 m in height) of him.
  bool ClosedEntrance(const uint8_t *base, uint32_t guard, uint32_t setup, std::string &why)
  {
    if (!REXCVAR_GET(pdz_counterop_no_closed_entrance) || !Ptr(setup))
    {
      return false;
    }
    const float gx = LoadFloat(base, guard + kPosition), gy = LoadFloat(base, guard + kPosition + 4),
                gz = LoadFloat(base, guard + kPosition + 8);
    const float sx = LoadFloat(base, setup + kSetupPosition), sy = LoadFloat(base, setup + kSetupPosition + 4),
                sz = LoadFloat(base, setup + kSetupPosition + 8);
    const float moved = std::sqrt((gx - sx) * (gx - sx) + (gz - sz) * (gz - sz));
    if (!(moved < 300.0f) || !(std::fabs(gy - sy) < 150.0f))
    {
      return false;
    }
    const uint32_t world = Load32(base, kGame + 24);
    const uint32_t end = Ptr(world) ? Load32(base, world + kWorldEnd) : 0;
    uint32_t node = Ptr(world) ? Load32(base, world + kWorldFirst) : 0;
    for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      if (!Ptr(o) || Load32(base, o) != 0x820C27C8u) // a door
      {
        continue;
      }
      if (!(base[o + 0x4A0] || base[o + 0x4A1]) || Load32(base, o + 0x49C) != 1)
      {
        continue;
      }
      const float dx = LoadFloat(base, o + kPosition) - gx, dy = LoadFloat(base, o + kPosition + 4) - gy,
                  dz = LoadFloat(base, o + kPosition + 8) - gz;
      const float flat = std::sqrt(dx * dx + dz * dz);
      if (flat < 600.0f && std::fabs(dy) < 250.0f)
      {
        why = fmt::format("{:.1f} m from his setup position, a locked and closed door {:08X} {:.1f} m away", moved / 100.0f,
                          o, flat / 100.0f);
        return true;
      }
    }
    return false;
  }

  // Round 8 (item 5): what the first takeover of a level looked like ("he was hit"): player 2 comes into the level as
  // a co-op player with 100 armour and Rare's health display [human +0x19B4] still has its top bar up (it stays 6 s
  // after it was last switched on) with its last frame's health; our plain stores (health to the guard's share,
  // armour 100 -> 0) are then drawn by that display as a loss. Nothing was hit: the display is put to rest with the
  // new values (bar off, its "last health" = the new health, no low-health heartbeat, no damage direction markers).
  void QuietHealthFeedback(PPCContext &ctx, uint8_t *base, uint32_t human, float hp)
  {
    if (!REXCVAR_GET(pdz_counterop_quiet_takeover) || !Ptr(human))
    {
      return;
    }
    const uint32_t fx = Load32(base, human + 0x19B4);
    std::string was = "no health display";
    if (Ptr(fx))
    {
      was = fmt::format("bar {} ({:.1f} s), last health {:.0f}, low {}, heartbeat sound {:08X}", base[fx] ? "up" : "down",
                        LoadFloat(base, fx + 4), LoadFloat(base, fx + 8), Load32(base, fx + 0x28),
                        Load32(base, fx + 0x24));
      if (Load32(base, fx + 0x24))
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = fx;
        sub_82268748(frame.ctx, base); // Rare's stop of the heartbeat sound
      }
      base[fx] = 0;
      StoreFloat(base, fx + 4, 0.0f);
      StoreFloat(base, fx + 8, std::floor(hp));
      Store32(base, fx + 0x18, 0);
      Store32(base, fx + 0x1C, 0);
      Store32(base, fx + 0x20, 0);
      Store32(base, fx + 0x28, 0);
    }
    const uint32_t marks = Load32(base, human + 0x19BC);
    uint32_t cleared = 0;
    if (Ptr(marks))
    {
      for (uint32_t i = 0; i < 4; ++i)
      {
        cleared += Load32(base, marks + 0xA38 + 4 * i) != 0;
        Store32(base, marks + 0xA38 + 4 * i, 0);
      }
    }
    const uint32_t participant = Load32(base, human + kParticipantOf);
    if (Ptr(participant))
    {
      StoreFloat(base, participant + 0xA0C, 5.0f); // Rare's "seconds since hurt": past the point that shows the bar
    }
    REXLOG_INFO("pdz_counterop: spawn (d): Rare's health display put to rest for the takeover (it was: {}; {} damage "
                "direction marker(s) cleared)",
                was, cleared);
  }

  // The eligible spots, nearest the agent first, and the counts. Take mode (pdz_counterop_take_guard): live or
  // dormant guards not taken. Otherwise every eligible guard's setup position that is free, whatever the guard's
  // state (it is never touched).
  std::vector<Spot> Spots(PPCContext &ctx, uint8_t *base, PickDiag &d)
  {
    std::vector<Spot> out;
    if (!Ptr(g.p1c))
    {
      return out;
    }
    const bool take = TakeGuard();
    std::vector<uint32_t> objects;
    CollectObjects(base, objects, d);
    std::vector<std::array<float, 3>> others;
    if (!take)
    {
      WorldCharacterPositions(base, others);
    }
    std::unordered_set<uint32_t> seen_ids;
    const float ax = LoadFloat(base, g.p1c + kPosition), ay = LoadFloat(base, g.p1c + kPosition + 4),
                az = LoadFloat(base, g.p1c + kPosition + 8);
    for (const uint32_t c : objects)
    {
      const uint32_t vt = Load32(base, c);
      d.characters += vt == kGuardVtable || vt == kHumanVtable || vt == 0x820BF3B8u; // guard, human, MP bot
      if (vt != kGuardVtable && !counter_op_twins::Pickable(c) && !counter_op_mansion::Pickable(c)) // round 13 d: the twins' class 820BD880; round 13 g: Mai Hem's
      {
        continue;
      }
      ++d.guards;
      if (counter_op_twins::FightHolds() && !counter_op_twins::Pickable(c)) { continue; } // round 13 d: nobody else while the twins' fight is on
      if (counter_op_mansion::FightHolds() && !counter_op_mansion::Pickable(c)) { continue; } // round 13 g: nobody else while Mansion's last fight is on
      if (c == g.kill_guard)
      {
        continue; // a replaced guard restored for Rare's kill (Trinity Escape: counted as a live spot for a tick)
      }
      const uint32_t setup = Load32(base, c + kSetup);
      const uint8_t team = Ptr(setup) ? base[setup + kSetupTeam] : 0;
      uint32_t id_ptr = 0;
      const uint32_t id = SetupIdKey(base, setup, &id_ptr);
      if (id)
      {
        seen_ids.insert(id);
      }
      if (take && g.taken.count(c))
      {
        ++d.taken;
        continue;
      }
      if (Ptr(setup) && team != 0 && team != kTeamTemplate)
      {
        continue; // an ally or a third party
      }
      if (!TakeableSide(base, c))
      {
        ++d.friendly; // round 3: the agent's friendlies by Rare's masks (Datacore's first half) are never taken
        // Round 12 (item 6): a Datacore jetpack guard already in his seat is counted here too; said once for each.
        if (take && counter_op_jetpack::Riding(base, c) && counter_op_guards::IsDatacoreJetpackRider(base, c))
        {
          static std::unordered_set<uint32_t> told_flying;
          if (told_flying.size() < 200 && told_flying.insert(c).second)
          {
            REXLOG_INFO("pdz_counterop: pick: datacore jetpack guard {:08X} is not taken: he is in his jetpack's seat "
                        "(vehicle {:08X}); Rare has no hand-over of a seat in the air, so he stays Rare's",
                        c, counter_op_jetpack::VehicleOf(base, c));
          }
        }
        continue;
      }
      if (take && counter_op_levels13::RefuseBody(base, c, id)) { ++d.denied; continue; } // round 13 i: Laboratory, placed guards only
      Spot e;
      e.type = Ptr(setup) ? Load32(base, setup + kSetupType) : Load32(base, c + kBodyId);
      // Round 12 (item 6): a Datacore jetpack guard on foot is taken like any other original; when the jetpack of
      // his spawner slot is still free the take ends with Rare's mount (counter_op_jetpack::AfterSpawn).
      const bool air_rider = take && counter_op_jetpack::AirRider(base, c); // round 13 c: in a jetpack seat (flying takeover)
      if (take && !air_rider && REXCVAR_GET(pdz_counterop_datacore_riders) && counter_op_guards::IsDatacoreJetpackRider(base, c))
      {
        std::string rider_why;
        const uint32_t rider_prop = counter_op_guards::DatacoreRiderJetpack(base, c);
        const bool mount = counter_op_jetpack::DatacoreRiderTake(base, c, rider_prop, rider_why);
        static std::unordered_set<uint64_t> told_rider;
        if (told_rider.size() < 400 && told_rider.insert((uint64_t(c) << 1) | (mount ? 1u : 0u)).second)
        {
          REXLOG_INFO("pdz_counterop: pick: datacore jetpack guard {:08X} (on foot, masks {:04X} / {:04X} / {:04X}) may "
                      "be taken: {}",
                      c, Load16(base, c + kTeamBits), Load16(base, c + kAllies), Load16(base, c + kEnemies), rider_why);
        }
      }
      bool jet = false;
      if (take && (air_rider || (id && counter_op_jetpack::Handles(id)))) // round 13 c: or any guard in a jetpack seat
      {
        // Round 2: a jetpack guard on foot beside his free jetpack (counter_op_jetpack.cpp), else never.
        // Round 13: or seated and flying, when his seat can be handed over now (pdz_counterop_jetpack_air).
        std::string jet_why;
        if (!counter_op_jetpack::Takeable(ctx, base, c, jet_why))
        {
          ++d.denied;
          continue;
        }
        jet = true;
        e.by_list = true;
        e.why = "jetpack guard";
        e.air = counter_op_jetpack::AirPending(c); // round 13 c
        ++d.by_list;
      }
      else if (id && g.denied.count(id) && !counter_op_twins::Pickable(c) && !counter_op_mansion::Pickable(c) && // round 13 d, round 13 g
               !(jet = e.by_list = counter_op_river::DriverTake(ctx, base, c)) && // round 13 h: River, a seated hovercraft driver
               !(jet = e.by_list = (take && PdzCounterOpOutpostGunner(base, c))) && // round 13 f: Outpost, Mai Hem at her hovercraft's gun
               !counter_op_levels13::WaveBody(base, c)) // round 13 i: Bridge's cloaked end waves
      {
        ++d.denied;
        continue;
      }
      if (jet)
      {
      }
      else if (id && (g.ids.count(id) || counter_op_twins::Pickable(c) || counter_op_mansion::Pickable(c))) // round 13 d, round 13 g
      {
        e.by_list = true;
        ++d.by_list;
      }
      else
      {
        // The no-id rule: a runtime clone of a spawn template, a guard whose setup id can't be read, or an id the
        // list doesn't know: a combat type with a gun in hand.
        e.why = !Ptr(setup)             ? "no setup"
                : team == kTeamTemplate ? "runtime clone of a spawn template"
                : !id                   ? "id unreadable"
                                        : "id not in the list";
        ++(team == kTeamTemplate && Ptr(setup) ? d.clones : d.no_id);
        if (!CombatType(e.type) || !HandWeaponType(base, c))
        {
          if (id)
          {
            ++d.unlisted_failed;
            if (d.unlisted.size() < 10 &&
                std::find_if(d.unlisted.begin(), d.unlisted.end(),
                             [id](const std::pair<uint32_t, uint32_t> &u) { return u.first == id; }) ==
                    d.unlisted.end())
            {
              d.unlisted.emplace_back(id, id_ptr);
            }
          }
          continue;
        }
        ++d.by_rule;
      }
      const bool alive = Alive(ctx, base, c);
      if (!alive && id && g.ids.count(id))
      {
        g.dead_ids.insert(id); // the eligible-left count (EligibleLeft)
      }
      if (!alive && take)
      {
        ++d.dead;
        continue;
      }
      e.dormant = alive && (Load32(base, c + kHidden) & 0x10) != 0;
      if (take && !jet && !counter_op_guards::Takeable(c) && !counter_op_twins::Pickable(c) && !counter_op_mansion::Pickable(c)) // round 13 d, round 13 g
      {
        // Round 2 (item 1): live originals only (a dormant guard's setup spot put him far from the action), never a
        // reinforcement wave.
        ++(e.dormant ? d.dormant : d.denied);
        continue;
      }
      if (take && !jet && alive)
      {
        std::string closed;
        if (ClosedEntrance(base, c, setup, closed))
        {
          ++d.closed;
          static std::unordered_set<uint32_t> told;
          if (told.size() < 200 && told.insert(c).second)
          {
            REXLOG_INFO("pdz_counterop: pick: guard '{}' ({:08X}) is not taken: still in his scripted entrance ({})",
                        IdChars(id), c, closed);
          }
          continue;
        }
      }
      e.dead = !alive;
      e.calm = !(Load32(base, c + 6268) & 0x400000u) && !(Load32(base, c + 6252) & 0x82u) && !Load32(base, c + 0x317C);
      e.character = c;
      e.setup = setup;
      e.id = id;
      e.id_ptr = id_ptr;
      if (!take)
      {
        // No-take mode: the guard's setup position (where Rare placed it: a floor spot of an area the level uses),
        // free of characters, and not the spot of the life just lost.
        if (!Ptr(setup) || team == kTeamTemplate)
        {
          ++d.no_setup; // no setup, or a clone whose setup is its spawner template's (that spot may be off the map)
          continue;
        }
        if (setup == g.last_setup)
        {
          ++d.repeat;
          continue;
        }
        e.x = LoadFloat(base, setup + kSetupPosition);
        e.y = LoadFloat(base, setup + kSetupPosition + 4);
        e.z = LoadFloat(base, setup + kSetupPosition + 8);
        if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z))
        {
          continue;
        }
        bool free = true;
        for (const auto &p : others)
        {
          if (Dist3(e.x, e.y, e.z, p[0], p[1], p[2]) < kFreeRadius)
          {
            free = false;
            break;
          }
        }
        if (!free)
        {
          ++d.occupied;
          continue;
        }
        e.distance = Dist3(e.x, e.y, e.z, ax, ay, az);
        e.on_screen = OnAgentScreen(base, g.p1c, e.x, e.z, e.distance);
        ++(e.dead ? d.dead : e.dormant ? d.dormant : d.live);
        out.push_back(e);
        continue;
      }
      if (e.dormant && Ptr(setup))
      {
        e.x = LoadFloat(base, setup + kSetupPosition);
        e.y = LoadFloat(base, setup + kSetupPosition + 4);
        e.z = LoadFloat(base, setup + kSetupPosition + 8);
      }
      else
      {
        e.x = LoadFloat(base, c + kPosition);
        e.y = LoadFloat(base, c + kPosition + 4);
        e.z = LoadFloat(base, c + kPosition + 8);
      }
      if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z))
      {
        continue;
      }
      e.distance = Dist3(e.x, e.y, e.z, ax, ay, az);
      e.on_screen = OnAgentScreen(base, g.p1c, e.x, e.z, e.distance);
      ++(e.dormant ? d.dormant : d.live);
      out.push_back(e);
    }
    for (const uint32_t id : g.ids)
    {
      d.unspawned += !seen_ids.count(id);
    }
    d.eligible = uint32_t(out.size());
    if (take)
    {
      uint32_t live = 0;
      for (const Spot &e : out)
      {
        live += !e.by_list && !e.dead; // clone missions (pdz_counterop_clones): what EligibleLeft counts
      }
      g.rule_live = live;
    }
    std::sort(out.begin(), out.end(), [](const Spot &a, const Spot &b) { return a.distance < b.distance; });
    return out;
  }

  // The pick: dormant spots first, at least kMinSpawnDistance from the agent and off his screen; relaxed step by
  // step (dormant far; live far off-screen; live far; dormant any; live any); one of the three nearest qualifying at
  // random. Returns the step's name.
  // No-take mode: a live guard's setup position (an area the level is using, reachable: the guards got there) first,
  // then a dead guard's (a cleared area), then a dormant guard's (a later wave's spot: possibly behind a door the
  // script opens later, so last), each far and off-screen first.
  const char *Pick(PPCContext &ctx, uint8_t *base, const std::vector<Spot> &spots, Spot &out)
  {
    if (TakeGuard())
    {
      // Round 2 (items 5, 6): counter_op_guards' pick: the first spawn in or near the opening action out of the
      // agent's view (not in his face), a respawn out of his view first (Rare's cone + line-of-sight test).
      std::vector<counter_op_guards::Candidate> cands;
      cands.reserve(spots.size());
      for (const Spot &e : spots)
      {
        counter_op_guards::Candidate c;
        c.character = e.character;
        c.x = e.x;
        c.y = e.y;
        c.z = e.z;
        c.distance = e.distance;
        c.calm = e.calm;
        c.rider = e.air; // round 13 c: in a jetpack seat (the pick's speed rule does not apply to him)
        cands.push_back(c);
      }
      static std::string why;
      if (counter_op_twins::FightHolds()) { if (spots.empty()) { return nullptr; } out = spots[0]; return "his twin of the boss fight (pdz_counterop_trinity_twins)"; } // round 13 d
      if (counter_op_mansion::FightHolds()) { if (spots.empty()) { return nullptr; } out = spots[0]; return "his body of Mansion's last fight: Mai Hem, or a woken terracotta warrior while she is gone (pdz_counterop_mansion_maihem)"; } // round 13 g
      for (const Spot &e : spots) { if (PdzCounterOpOutpostGunner(base, e.character)) { out = e; return "Mai Hem at the gun of her hovercraft (pdz_counterop_outpost_turret)"; } } // round 13 f: Outpost's last fight
      for (const Spot &e : spots) { if (counter_op_river::DriverFirst(base, e.character)) { out = e; return "the driver of an enemy hovercraft near the agent's own (pdz_counterop_river_hovercraft_first)"; } } // round 13b r: River, the agent on the lakes
      if (counter_op_jetpack::AirTestFirst()) { for (const Spot &e : spots) { if (e.air) { out = e; return "the test's flying jetpack rider (pdz_counterop_test_jet_take_first)"; } } } // round 13 c: testing tool (spots are sorted nearest first)
      const std::string want = REXCVAR_GET(pdz_counterop_test_take);
      if (!want.empty())
      {
        for (size_t k = 0; k < spots.size(); ++k)
        {
          if (spots[k].id && IdChars(spots[k].id) == want)
          {
            out = spots[k];
            why = "the test's guard (pdz_counterop_test_take)";
            return why.c_str();
          }
        }
        if (g.lives == 0)
        {
          return nullptr; // the first spot waits for the test's guard; later ones take the usual pick
        }
      }
      // Round 3 (item 5): the first spawn waits up to kFirstAreaSeconds for an opening enemy of the agent's area
      // (out of her view or at the edge of it) before it takes one further into the level.
      const auto now = std::chrono::steady_clock::now();
      if (g.first_pick_at == std::chrono::steady_clock::time_point{})
      {
        g.first_pick_at = now;
      }
      const bool patient = g.lives == 0 && now - g.first_pick_at < std::chrono::milliseconds(int(kFirstAreaSeconds * 1000));
      // Round 12 (item 6): how long he has been without a body (the area rule is relaxed step by step with it).
      counter_op_guards::SetPickWait(std::chrono::duration<double>(now - g.need_since).count());
      // Round 13 c: on Datacore a flying jetpack rider is picked only when no guard on foot can be taken (it replaces
      // "he waits" and comes before the made last-resort guard): the pick runs without the riders first.
      const bool riders_last = counter_op_jetpack::AirLast() &&
                               std::any_of(spots.begin(), spots.end(), [](const Spot &e) { return e.air; });
      const auto pick_part = [&](bool riders) {
        std::vector<counter_op_guards::Candidate> part;
        std::vector<size_t> index;
        for (size_t k = 0; k < spots.size(); ++k)
        {
          if (spots[k].air == riders)
          {
            part.push_back(cands[k]);
            index.push_back(k);
          }
        }
        const int p = counter_op_guards::Pick(ctx, base, g.p1c, part, g.lives == 0, patient, why);
        return p < 0 ? -1 : int(index[size_t(p)]);
      };
      int i = riders_last ? pick_part(false) : counter_op_guards::Pick(ctx, base, g.p1c, cands, g.lives == 0, patient, why);
      if (i < 0 && riders_last)
      {
        i = pick_part(true);
        if (i >= 0)
        {
          why += "; a flying jetpack rider, because no guard on foot can be taken (pdz_counterop_jetpack_air)";
        }
      }
      if (i < 0)
      {
        if (g.pick_fail_lines < 12)
        {
          ++g.pick_fail_lines;
          REXLOG_INFO("pdz_counterop: pick: {}", why);
        }
        return nullptr;
      }
      out = spots[size_t(i)];
      return why.c_str();
    }
    // kind: 0 live, 1 dead, 2 dormant, 3 any
    struct Step
    {
      const char *name;
      int kind;
      bool far, off_screen;
    };
    // N64 take: a guard the level has in play (live or dormant), calm first, off-screen first, far first.
    static constexpr Step kTakeSteps[] = {
        {"a calm guard, far and off-screen", 4, true, true},
        {"a calm guard, off-screen", 4, false, true},
        {"a guard, far and off-screen", 3, true, true},
        {"a calm guard, far", 4, true, false},
        {"a guard, off-screen", 3, false, true},
        {"a guard (none off-screen)", 3, false, false},
    };
    static constexpr Step kFreeSteps[] = {
        {"a live guard's setup position, far and off-screen", 0, true, true},
        {"a dead guard's setup position, far and off-screen", 1, true, true},
        {"a live guard's setup position, far", 0, true, false},
        {"a dead guard's setup position, far", 1, true, false},
        {"a dormant guard's setup position, far and off-screen", 2, true, true},
        {"any free setup position, off-screen (none far)", 3, false, true},
        {"any free setup position (none far, none off-screen)", 3, false, false},
    };
    const bool take = TakeGuard();
    const Step *steps = take ? kTakeSteps : kFreeSteps;
    const size_t count = take ? std::size(kTakeSteps) : std::size(kFreeSteps);
    // Take: only calm guards for the first kCalmOnlySeconds of a search (a first spot or after a death); then any.
    const bool calm_only =
        take && std::chrono::steady_clock::now() - g.need_since < std::chrono::milliseconds(int(kCalmOnlySeconds * 1000));
    for (size_t si = 0; si < count; ++si)
    {
      const Step &s = steps[si];
      if (calm_only && s.kind != 4)
      {
        continue;
      }
      std::vector<const Spot *> list;
      for (const Spot &e : spots)
      {
        const int kind = e.dead ? 1 : e.dormant ? 2 : 0;
        // kind 3 = any; kind 4 = any calm guard (take mode)
        if ((s.kind == 3 || kind == s.kind || (s.kind == 4 && e.calm)) && (!s.far || e.distance >= kMinSpawnDistance) &&
            (!s.off_screen || !e.on_screen))
        {
          list.push_back(&e); // spots are sorted nearest first
        }
      }
      if (!list.empty())
      {
        // N64 (take): one of the 9 nearest the agent; no-take: one of the 3 nearest.
        const size_t n = std::min<size_t>(list.size(), take ? 9 : 3);
        out = *list[std::uniform_int_distribution<size_t>(0, n - 1)(g_rng)];
        return s.name;
      }
    }
    return nullptr;
  }

  // ---- The spawn ----------------------------------------------------------------------------------------------

  // Player 2's participant, when it is still in Rare's list (a character may be gone after a level change).
  bool ParticipantListed(const uint8_t *base, uint32_t participant)
  {
    for (uint32_t p = Load32(base, kParticipantList), n = 0; Ptr(p) && n < 16; p = Load32(base, p + 4), ++n)
    {
      if (p == participant)
      {
        return true;
      }
    }
    return false;
  }

  // The N64 rule (see the header): health and max = (the guard's health / its spawn health) x the co-op max x the
  // scale, capped at the co-op max, floor kMinHealth. Armour stays 0.
  void ApplyHealth(uint8_t *base, const Spot &e)
  {
    const float spawn_health = Ptr(e.setup) ? LoadFloat(base, e.setup + kSetupHealth) : 0.0f;
    // No-take mode: nothing is taken from the guard, so player 2 is a fresh guard of that setup (ratio 1).
    const float guard_health = TakeGuard() || !(spawn_health > 0.0f) ? LoadFloat(base, e.character + kHealth)
                                                                     : spawn_health;
    // Round 4 (item 1): with the players on multiplayer (or scaled) damage he gets a full player's health; with
    // pdz_counterop_pvp_damage "off" round 3's share (pdz_counterop_health_scale).
    const double scale = PdzCounterOpPvpOn() ? 1.0 : REXCVAR_GET(pdz_counterop_health_scale);
    const float cap = g.coop_max_health > 0.0f ? g.coop_max_health : 100.0f;
    float ratio = spawn_health > 0.0f && std::isfinite(spawn_health) ? guard_health / spawn_health : 1.0f;
    if (!std::isfinite(ratio) || ratio < 0.0f)
    {
      ratio = 0.0f;
    }
    if (ratio > 1.0f)
    {
      ratio = 1.0f;
    }
    float hp = float(double(ratio) * double(cap) * scale);
    const char *note = "";
    if (!std::isfinite(hp) || hp > cap)
    {
      hp = cap;
      note = " (capped)";
    }
    if (hp < kMinHealth)
    {
      hp = kMinHealth;
      note = " (floor)";
    }
    hp = counter_op_twins::SpawnHealth(base, e.character, hp); // round 13 d: a twin's own pools
    hp = counter_op_mansion::SpawnHealth(base, e.character, hp); // round 13 g: Mai Hem's share
    const uint32_t participant = Load32(base, g.p2c + kParticipantOf);
    const float before = LoadFloat(base, g.p2c + kHealth);
    const float before_max = Ptr(participant) ? LoadFloat(base, participant + kPartMaxHealth) : -1.0f;
    StoreFloat(base, g.p2c + kHealth, hp);
    float armour_before = -1.0f;
    if (Ptr(participant))
    {
      StoreFloat(base, participant + kPartMaxHealth, hp);
      g.max_health_set = true;
      // Round 4: a human's armour is his participant's +988 (vtable +0x154 with flag 0; human +124 is the other
      // value), so he kept a co-op player's 100 armour until now (log 053: the agent's first hits did 1 health and 6
      // armour each). No armour; and the second health value the player damage handler lowers (+984) follows the
      // health, so nothing is left over from the last life.
      armour_before = LoadFloat(base, participant + 988);
      StoreFloat(base, participant + 984, hp);
      StoreFloat(base, participant + 988, 0.0f);
    }
    StoreFloat(base, g.p2c + kArmour, 0.0f);
    // Round 16 (item 9, pdz_counterop_guard_armour): the taken guard's own armour. Rare's armour (a guard's +124, a
    // human's participant +988) is a pool in the damage table's units: while it is above 0 a covered hit (82236730:
    // a guard's armour class 1 = head, 2 = body; a human 3 = all of him) takes the row's 'through armour' health
    // damage and removes the row's armour damage from the pool. A guard's pool is counted against his own health
    // pool (setup +256), so it is carried over the same way as his health: guard armour / spawn health x the co-op
    // max, capped at 100 (the co-op player's full armour bar). Only participant +988 is set (+124 stays 0: Rare
    // would dress a human with +124 above 0 in armour pieces). HideP2 empties it while he waits, so the HUD never
    // shows his bar dropping at a takeover.
    float armour_given = 0.0f;
    if (REXCVAR_GET(pdz_counterop_guard_armour) && Ptr(participant))
    {
      const float guard_armour = LoadFloat(base, e.character + kArmour);
      if (std::isfinite(guard_armour) && guard_armour > 0.0f)
      {
        armour_given = spawn_health > 0.0f && std::isfinite(spawn_health) ? guard_armour / spawn_health * cap
                                                                          : guard_armour;
        if (!std::isfinite(armour_given) || armour_given < 0.0f)
        {
          armour_given = 0.0f;
        }
        armour_given = std::fmin(armour_given, 100.0f);
        StoreFloat(base, participant + 988, armour_given);
      }
      REXLOG_INFO("pdz_counterop: spawn (d): armour: the guard's {:.1f} of his health pool {:.1f} = {:.1f} on player "
                  "2's bar of 100 (pdz_counterop_guard_armour)",
                  guard_armour, spawn_health, armour_given);
    }
    g.set_health = hp;
    g.health_check = true;
    g.getup_rescue_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    g.getup_rescue_step = 0;
    g.health_check_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    REXLOG_INFO("pdz_counterop: spawn (d): health: the guard's {:.2f} of its spawn health {:.2f} (setup +256; its "
                "getter has no max) = {:.2f} x the co-op max {:.0f} x {:.3f} = {:.1f}{}: player 2's health {:.1f} -> "
                "{:.1f}, max health {} -> {:.1f}{}; armour (his own was {:.1f}; the guard's {:.1f}: see the armour line)",
                guard_health, spawn_health, ratio, cap, scale, hp, note, before, hp,
                before_max >= 0.0f ? fmt::format("{:.1f}", before_max) : std::string("?"), hp,
                Ptr(participant) ? "" : " (no participant at +6560: max not set)", armour_before,
                LoadFloat(base, e.character + kArmour));
  }

  // Player 2 spawns at this spot: (a) back in the world (unhidden, unlocked; the spectate camera stops), (b) moved
  // to the spot, (c) the guards' side and the guard's gun with full reserves (only a type Rare's give can look up;
  // otherwise skipped: he keeps his gun), (d) the health rule, (e) the guard hidden (a dormant one left hidden) and
  // kept so. One line per step.
  void SpawnAt(PPCContext &ctx, uint8_t *base, const Spot &e, const PickDiag &d, const char *step, const char *why)
  {
    const uint32_t guard = e.character;
    const bool take = TakeGuard();
    std::string no_gun;
    uint32_t gun = GiveableWeaponType(base, guard, no_gun);
    if (!gun && !take && g.last_gun)
    {
      gun = g.last_gun; // a dead guard's gun is usually dropped: the last life's (from this level's list)
      REXLOG_INFO("pdz_counterop: spawn: the spot's guard has no giveable gun ({}): the last life's type {} instead",
                  no_gun, gun);
    }
    if (take)
    {
      g.taken.insert(guard);
      g.current_guard = guard;
      if (e.id)
      {
        g.taken_ids.insert(e.id);
      }
    }
    else
    {
      g.last_setup = e.setup;
    }
    ++g.lives;
    if (take)
    {
      REXLOG_INFO("pdz_counterop: replaces guard {:08X} '{}' (life {}): eligible guards left {} (of {} listed: {} "
                  "replaced, {} seen dead){}",
                  guard, IdChars(e.id), g.lives, EligibleLeft(), g.ids.size(), g.taken_ids.size(), g.dead_ids.size(),
                  TestMaxLives() > 0 ? fmt::format("; test cap {} lives", TestMaxLives()) : std::string());
    }
    REXLOG_INFO("pdz_counterop: {}: player 2 (life {}) spawns at {}: guard {:08X} (setup {:08X}, id {}; {}; body {}, "
                "head {}) at ({:.0f}, {:.0f}, {:.0f}), {:.1f} m from the agent, {} the agent's screen; {}",
                why, g.lives, step, guard, e.setup, e.id ? IdLog(e.id, e.id_ptr) : std::string("none"),
                e.by_list ? fmt::format("rule: the {} id list", g.mission)
                          : fmt::format("rule: no id ({}): type 48+, not a non-combatant, armed, not denied", e.why),
                Load32(base, guard + kBodyId), Load32(base, guard + kHeadId), e.x, e.y, e.z, e.distance / 100.0f,
                e.on_screen ? "on" : "off", CountsText(d));
    // Round 4 (item 3a): he looks like the guard he takes. While he is still out of the world his body, head and
    // first-person hands are rebuilt as this guard's (Rare's multiplayer re-skin steps); his inventory is cleared
    // first (nothing is held across the swap).
    if (take && g.out && REXCVAR_GET(pdz_counterop_guard_look) && (Load32(base, guard) == kGuardVtable || counter_op_twins::IsTwin(guard) || counter_op_mansion::IsBoss(guard))) // round 13 d, round 13 g
    {
      if (REXCVAR_GET(pdz_counterop_guard_weapons))
      {
        Call(ctx, base, sub_8254BDF0, g.p2c, 0);
      }
      const uint32_t body = Load32(base, guard + kBodyId), head = Load32(base, guard + kHeadId);
      std::string hands_why, dress;
      const uint32_t hands = PdzCounterOpHandsFor(ctx, base, body, hands_why);
      const bool dressed = PdzCounterOpDress(ctx, base, g.p2c, body, head, hands, -1, dress);
      REXLOG_INFO("pdz_counterop: look: spawn (life {}): guard '{}' wears body {} / head {}: {}{}; hands: {}", g.lives,
                  IdChars(e.id), body, head, dressed ? "" : "NOT re-dressed: ", dress, hands_why);
    }
    PdzCounterOpVoiceSpawn(ctx, base, g.p2c, guard, e.setup); // COUNTER-OP VOICE
    // (a) the guards' masks first, so ShowP2's team-list filing (82187D18, by the bits of +128) matches them
    TakeGuardTeam(base, guard);
    TeamGuarantee(base, guard); // Fri 20 Nov night (pdz_counterop_team_check): enemies of the agent both ways
    ApplyTeam(base, g.p2c);
    const bool was_out = g.out;
    g_view_from = std::chrono::steady_clock::now();
    g_view_lines = 0;
    g_view_cleared = 0;
    ShowP2(ctx, base);
    REXLOG_INFO("pdz_counterop: spawn (a): player 2 {}", was_out ? fmt::format("back in the world (unhidden: world, "
                                                                               "team lists, physics; unlocked) after {} "
                                                                               "pick tries; spectate camera off",
                                                                               g.tries)
                                                                 : std::string("was already in the world"));
    // (b)
    if (!g.scratch)
    {
      g.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x40);
    }
    if (g.scratch)
    {
      // Round 6 (item 5): the guard's ORIGIN (his feet, Rare's getter) and his facing. e.x/y/z of a live guard is
      // his +48, the bounding sphere's centre: moved there, player 2 stood about 0.9 m in the air and fell. A dormant
      // guard's e.x/y/z is his setup position, which is an origin already.
      float at[3] = {e.x, e.y, e.z};
      const bool true_origin = REXCVAR_GET(pdz_counterop_true_origin);
      const bool by_getter = true_origin && take && !e.dormant && OriginOf(ctx, base, guard, at);
      double guard_yaw = 0.0;
      const bool has_yaw = true_origin && take && YawOf(ctx, base, guard, guard_yaw);
      counter_op_river::SeatSpot(base, guard, at); // round 13 h: River, a seated hovercraft driver: on the craft, not at his feet under the water
      StoreFloat(base, g.scratch, at[0]);
      StoreFloat(base, g.scratch + 4, at[1]);
      StoreFloat(base, g.scratch + 8, at[2]);
      Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0); // Rare's move: also clears the speed and the fall state
      float now_at[3] = {};
      OriginOf(ctx, base, g.p2c, now_at);
      REXLOG_INFO("pdz_counterop: spawn (b): player 2 moved to ({:.0f}, {:.0f}, {:.0f}) ({}{}); his origin is now "
                  "({:.0f}, {:.0f}, {:.0f}), his sphere centre +48 ({:.0f}, {:.0f}, {:.0f})",
                  at[0], at[1], at[2],
                  !take        ? (e.dead      ? "a dead guard's setup position"
                                  : e.dormant ? "a dormant guard's setup position"
                                              : "a live guard's setup position (the guard is elsewhere)")
                  : e.dormant ? "the dormant guard's setup position"
                              : "the live guard's position",
                  by_getter ? ": his origin at the feet, Rare's getter 82151210"
                            : true_origin && take && !e.dormant ? ": his +48 (the getter gave nothing)" : "",
                  now_at[0], now_at[1], now_at[2], LoadFloat(base, g.p2c + kPosition),
                  LoadFloat(base, g.p2c + kPosition + 4), LoadFloat(base, g.p2c + kPosition + 8));
      g.place_log = 40;
      g.place_series.clear();
      g.place_given_y = at[1];
      if (has_yaw)
      {
        const float before = LoadFloat(base, g.p2c + kViewYaw);
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = g.p2c;
        frame.ctx.f1.f64 = 0.0;
        frame.ctx.f2.f64 = guard_yaw;
        sub_825AD3B0(frame.ctx, base);
        REXLOG_INFO("pdz_counterop: spawn (f): facing as the guard did: yaw {:.1f} deg (Rare's getter 82519A90 on the "
                    "guard, given through 825AD3B0); view yaw +0x1670 {:.1f} -> {:.1f}",
                    guard_yaw * 57.29578, before, LoadFloat(base, g.p2c + kViewYaw));
      }
      else
      {
        FaceAgent(ctx, base); // (f)
      }
    }
    else
    {
      REXLOG_WARN("pdz_counterop: spawn (b): no guest scratch memory for the move: player 2 not moved");
    }
    // Round 12 (item 5): his view's sky / fog / weather / lighting for where he now stands (pdz_counterop_env_refresh).
    PdzCounterOpEnvRefresh(ctx, base, g.p1c, g.p2c, "spawn / takeover");
    PdzCounterOpObjectiveLife(ctx, base, g.p2c); // round 6 (item 7): his own objective line, if one is set
    // (c) the side and the gun, as in log 310 (no ammo amounts in the give: log 311 crashed in its secondary-ammo
    // block, which reads the entry's +4 definition, null for a one-ammo gun); then full reserves through Rare's adders.
    ApplyTeam(base, g.p2c); // again: a no-op unless something between (a) and here reset them
    if (REXCVAR_GET(pdz_counterop_team_check) && Ptr(g.p1c))
    {
      // Fri 20 Nov night: the masks at the takeover, and Rare's own answers about him (8251EBD0 also reads the owner
      // links and the Psychosis swap; 824EF8B8 is the agent's auto-aim target filter, which refuses friends).
      const bool she = (Call(ctx, base, sub_8251EBD0, g.p1c, g.p2c) & 0xFF) != 0;
      const bool he = (Call(ctx, base, sub_8251EBD0, g.p2c, g.p1c) & 0xFF) != 0;
      const bool her_guard = Ptr(guard) && (Call(ctx, base, sub_8251EBD0, g.p1c, guard) & 0xFF) != 0;
      const bool aim = (Call(ctx, base, sub_824EF8B8, g.p1c, g.p2c) & 0xFF) != 0;
      REXLOG_INFO("pdz_counterop: team: takeover (life {}): guard '{}' {:08X} masks {:04X} / {:04X} / {:04X} (Rare's "
                  "own, read now){} -> player 2 {:04X} / {:04X} / {:04X}{}; the agent {:04X} / {:04X} / {:04X}; "
                  "Rare's friend test 8251EBD0: the agent counts him a friend: {}, he counts her one: {}, the agent "
                  "counted the guard one: {}; the agent's auto-aim filter 824EF8B8 takes him as a target: {}",
                  g.lives, IdChars(e.id), guard, g_taken_masks[0], g_taken_masks[1], g_taken_masks[2],
                  g_taken_fallback ? " (no team bit: Rare's team-0 guard masks used instead)" : "",
                  Load16(base, g.p2c + kTeamBits), Load16(base, g.p2c + kAllies), Load16(base, g.p2c + kEnemies),
                  g_team_note.empty() ? std::string() : "; " + g_team_note, Load16(base, g.p1c + kTeamBits),
                  Load16(base, g.p1c + kAllies), Load16(base, g.p1c + kEnemies), she ? "YES" : "no",
                  he ? "YES" : "no", her_guard ? "YES" : "no", aim ? "yes" : "NO");
      if (she || he)
      {
        REXLOG_WARN("pdz_counterop: team: takeover (life {}): Rare's friend test still calls player 2 and the agent "
                    "friends (owner link: his +104 bit 0x20 {}, hers {}; Psychosis +5640: his {:.2f}, hers {:.2f}): "
                    "not by the masks above",
                    g.lives, (Load32(base, g.p2c + 104) & 0x20) ? "set" : "clear", // 8251EC00: rlwinm 27,31,31 = 0x20
                    (Load32(base, g.p1c + 104) & 0x20) ? "set" : "clear", LoadFloat(base, g.p2c + 5640),
                    LoadFloat(base, g.p1c + 5640));
      }
    }
    // Round 3: a guard with nothing in his hands (Nightclub's calm guards keep their guns put away) still carries
    // his weapon: the first weapon in his inventory the level can give (Rare's inventory walk 824F5C70 over the item
    // categories). Failing that, the gun of the nearest armed guard with the same body; then the last life's.
    std::string gun_from;
    if (!gun && REXCVAR_GET(pdz_counterop_guard_weapons))
    {
      for (uint32_t category = 0; category < 6 && !gun; ++category)
      {
        {
          const uint32_t entry = Call(ctx, base, sub_824F5C70, guard + 0x142C, 0, category);
          const uint32_t item = Ptr(entry) ? Load32(base, entry) : 0;
          if (!Ptr(item))
          {
            continue;
          }
          const uint32_t wentry = Load32(base, item + kWeaponEntry);
          const uint32_t def = Ptr(wentry) ? Load32(base, wentry) : 0;
          const uint32_t type = Ptr(def) ? Load32(base, def + kWeaponDefType) : 0;
          if (type && type != 15 && LevelGivesType(base, type))
          {
            gun = type;
            gun_from = fmt::format("the guard's put-away weapon (inventory category {}, item {:08X})", category, item);
          }
        }
      }
      if (!gun)
      {
        float best = 1e18f;
        PickDiag other;
        for (const Spot &s : Spots(ctx, base, other))
        {
          std::string why_not;
          const uint32_t type = Load32(base, s.character + kBodyId) == Load32(base, guard + kBodyId)
                                    ? GiveableWeaponType(base, s.character, why_not)
                                    : 0;
          const float dist = Dist3(s.x, s.y, s.z, e.x, e.y, e.z);
          if (type && dist < best)
          {
            best = dist;
            gun = type;
            gun_from = fmt::format("the gun of the nearest armed guard with his body ({:08X}, {:.0f} m away)", s.character,
                                   dist / 100.0f);
          }
        }
      }
      if (!gun && g.last_gun)
      {
        gun = g.last_gun;
        gun_from = "the last life's gun";
      }
      if (gun)
      {
        REXLOG_INFO("pdz_counterop: spawn: the guard holds nothing ({}): {} (type {})", no_gun, gun_from, gun);
      }
      else
      {
        REXLOG_WARN("pdz_counterop: spawn: the guard holds nothing ({}) and no weapon of his was found: player 2 keeps "
                    "what he has",
                    no_gun);
      }
    }
    // Round 3 (item 4): he carried the agent's co-op loadout (Rare gives both humans the same one) with the guard's
    // gun added to it. Now his whole inventory is cleared first with Rare's own clear (8254BDF0(character, 0): both
    // hands emptied, every carried item unlinked and destroyed, nothing dropped; what Rare's "strip" 8254BF38 runs
    // before its give), then the guard's weapon(s) are given: the gun in his hand and, when the other hand holds a
    // different type the level can give, that one too. The tick removes anything else that turns up in his hands.
    gun = counter_op_mansion::GunFor(guard, gun); // round 13 g: a terracotta warrior is unarmed (Rare's unarmed item, type 15)
    std::string cleared;
    g.life_types.clear();
    if (gun && REXCVAR_GET(pdz_counterop_guard_weapons))
    {
      Call(ctx, base, sub_8254BDF0, g.p2c, 0);
      cleared = "inventory cleared first (Rare's 8254BDF0); ";
      g.life_types.push_back(gun);
      for (const uint32_t hand : {kHand1, kHand0})
      {
        const uint32_t w = Load32(base, guard + hand);
        const uint32_t entry = Ptr(w) ? Load32(base, w + kWeaponEntry) : 0;
        const uint32_t def = Ptr(entry) ? Load32(base, entry) : 0;
        const uint32_t type = Ptr(def) ? Load32(base, def + kWeaponDefType) : 0;
        if (type && type != gun && LevelGivesType(base, type))
        {
          g_giving = true;
          const uint32_t second = Call(ctx, base, sub_8254C378, g.p2c, type, 0, 0, 0, 0);
          g_giving = false;
          g.life_types.push_back(type);
          cleared += fmt::format("the guard's other weapon type {} given too ({}); ", type,
                                 Ptr(second) ? "ok" : "give returned none");
          break;
        }
      }
      for (const uint32_t t : counter_op_twins::ExtraWeaponTypes(ctx, base, guard, gun, g.life_types)) { g_giving = true; Call(ctx, base, sub_8254C378, g.p2c, t, 0, 0, 0, 0); g_giving = false; g.life_types.push_back(t); } // round 13 d: a twin's other gun and flashbangs
    }
    if (gun)
    {
      g.last_gun = gun;
      g.life_item = 0;
      g_giving = true; // the pick-up block (82586238's hook) lets our own give through
      uint32_t item = Call(ctx, base, sub_8254C378, g.p2c, gun, 0, 0, 0, 0);
      g_giving = false;
      std::string ammo = "no item: no ammo added";
      if (!Ptr(item))
      {
        // co_outpost life 2: he still held that gun after Rare's revive, so the give made no new item: the gun in
        // his hand of that type gets the reserves instead.
        for (const uint32_t hand : {kHand0, kHand1})
        {
          const uint32_t held = Load32(base, g.p2c + hand);
          if (Ptr(held) && Load32(base, held + kItemType) == gun)
          {
            item = held;
            ammo = "already held";
            break;
          }
        }
      }
      if (Ptr(item))
      {
        const uint32_t entry = Load32(base, item + kWeaponEntry);
        const uint32_t def = Ptr(entry) ? Load32(base, entry + kWeaponEntryDef) : 0;
        const uint32_t def2 = Ptr(entry) ? Load32(base, entry + kWeaponEntrySecondaryDef) : 0;
        if (Ptr(def) && REXCVAR_GET(pdz_counterop_guard_weapons))
        {
          // Round 3: the guard's own ammo, not 9999. Rare's give has already set the reserve to the weapon's default
          // carry (8254C418-8254C450: 82583F80(item, D, 0)); pdz_counterop_ammo_scale adds (scale - 1) x D on top
          // (he can't pick anything up, so a little more than one carry by default).
          const uint32_t carry = DefaultReserve(base, item);
          const double scale = REXCVAR_GET(pdz_counterop_ammo_scale);
          const uint32_t extra = carry > 0 && carry <= 2000 && scale > 1.0 ? uint32_t(double(carry) * (scale - 1.0)) : 0;
          if (extra)
          {
            Call(ctx, base, sub_82583F80, item, extra, 1);
          }
          ammo = fmt::format("reserve: Rare's default carry for this weapon ({}) + {} (pdz_counterop_ammo_scale {:.1f})",
                             carry, extra, scale);
        }
        else if (Ptr(def))
        {
          Call(ctx, base, sub_82583F80, item, kFullAmmo, 1);
          ammo = "primary reserve filled (82583F80)";
          if (Ptr(def2))
          {
            Call(ctx, base, sub_82584118, item, kFullAmmo, 1);
            ammo += ", secondary reserve filled (82584118)";
          }
          else
          {
            ammo += ", no secondary definition (entry +4 null)";
          }
        }
        else
        {
          ammo = fmt::format("the item's weapon entry ({:08X}) has no definition: no ammo added", entry);
        }
      }
      // Drawn (co_outpost: the partner started unarmed and the give only put it in his inventory; his first-person
      // view had empty hands): Rare's weapon switcher (character +5780), its select (vtable slot 0: switcher, item,
      // rest 0), as the bots' draw in cut_scenarios.cpp and Rare's own draws do.
      g.life_item = Ptr(item) ? item : 0;
      std::string drawn = "already in his hand";
      if (Ptr(item) && Load32(base, g.p2c + kHand0) != item && Load32(base, g.p2c + kHand1) != item)
      {
        const uint32_t switcher = Load32(base, g.p2c + 5780);
        PPCFunc *select = SwitcherOk(base, switcher) ? pdz::GuestFunction(Load32(base, Load32(base, switcher)), "counter-op draw")
                                        : nullptr;
        if (select)
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
          frame.ctx.r3.u64 = switcher;
          frame.ctx.r4.u64 = item;
          frame.ctx.r5.u64 = 0;
          frame.ctx.r6.u64 = 0;
          frame.ctx.r7.u64 = 0;
          frame.ctx.r8.u64 = 0;
          frame.ctx.r9.u64 = 0;
          frame.ctx.r10.u64 = 0;
          select(frame.ctx, base);
          drawn = fmt::format("drawn through his weapon switcher {:08X}", switcher);
        }
        else
        {
          drawn = "NOT drawn (no weapon switcher)";
        }
      }
      REXLOG_INFO("pdz_counterop: spawn (c): guards' side (team bits {:04X}, enemies {:04X}); {}gun type {} given ({}); "
                  "{}; {}",
                  Load16(base, g.p2c + kTeamBits), Load16(base, g.p2c + kEnemies), cleared, gun,
                  item ? fmt::format("item {:08X}", item) : std::string("give returned none"), ammo, drawn);
    }
    else
    {
      REXLOG_INFO("pdz_counterop: spawn (c): guards' side (team bits {:04X}, enemies {:04X}); gun skipped ({}): player "
                  "2 keeps his gun",
                  Load16(base, g.p2c + kTeamBits), Load16(base, g.p2c + kEnemies), no_gun);
    }
    // (d)
    ApplyHealth(base, e);
    QuietHealthFeedback(ctx, base, g.p2c, g.set_health); // round 8 (item 5)
    // (e)
    if (take)
    {
      counter_op_levels13::OnTake(ctx, base, g.p2c, guard); // round 13 i: the cloak hand-over (before the guard is hidden)
      // Round 5 (item 1): awake or asleep, the sleeping guards of his whole area wake with him (round 4 woke only a
      // sleeping guard's own group: the awake 'fac3' of the sleeping warehouse woke nobody).
      if (!counter_op_twins::IsTwin(guard) && !counter_op_mansion::IsBoss(guard)) { counter_op_wake::WakeAreaOf(base, guard); } // round 13 d: a twin wakes nobody; round 13 g: nor Mai Hem
      counter_op_jetpack::Unseat(ctx, base, g.p2c, guard); // round 13 c: a guard picked in his jetpack's seat leaves it first
      PdzCounterOpOutpostUnseat(ctx, base, g.p2c, guard); // round 13 f: Mai Hem at her hovercraft's gun leaves it first
      PdzCounterOpTurretUnseat(ctx, base, g.p2c, guard); // round 13b q: a guard at a stationary turret leaves it first
      counter_op_river::Unseat(ctx, base, g.p2c, guard); // round 13 h: River, a driver picked in his hovercraft seat leaves it first
      const std::string removed = RemoveGuard(ctx, base, guard);
      REXLOG_INFO("pdz_counterop: spawn (e): guard {:08X} {}", guard, removed);
      PdzCounterOpOutpostAfterSpawn(ctx, base, g.p2c, guard); // round 13 f: Outpost, Rare's mount onto the hovercraft's gun
      PdzCounterOpTurretAfterSpawn(ctx, base, g.p2c, guard); // round 13b q: the stationary gun of the guard he took (cvar, off)
      counter_op_river::AfterSpawn(ctx, base, g.p2c, guard); // round 13 h: River, Rare's mount into the driver's seat
      const std::string jet = counter_op_jetpack::AfterSpawn(ctx, base, g.p2c, guard);
      if (!jet.empty())
      {
        REXLOG_INFO("pdz_counterop: spawn (j): {}", jet);
      }
    }
    else
    {
      REXLOG_INFO("pdz_counterop: spawn (e): guard {:08X} untouched ({}; +6272 {:08X}): no guard hidden, the level's "
                  "count and scripts unchanged; player 2's masks {:04X} / {:04X} / {:04X}",
                  guard, e.dead ? "dead" : e.dormant ? "dormant" : "live", Load32(base, guard + kHidden),
                  Load16(base, g.p2c + kTeamBits), Load16(base, g.p2c + kAllies), Load16(base, g.p2c + kEnemies));
    }
    // Round 2: buttons he was holding (a menu's or a cutscene's skip press) count only once released.
    counter_op_pad::Arm(base, g.p2c, take ? "took a guard" : "spawned as a guard");
    g.tries = 0;
    PdzCounterOpHudOnNewBody(base); // round 16 (item 2): his weapon HUD elements are checked at every new body
  }

  // Mon 23 Nov (co21, pdz_counterop_safe_handback). Log 058 (Bridge): cut-off move (c) handed guard 's11a' back
  // 3 min 17 s after player 2 took him, 161 m behind the agent, and Rare's restore (825818E0(g, 0)) called the
  // guard's virtual +868 (LR 82581928): the slot held 0x6D. The object was no guard any more: the level had torn it
  // down while it was hidden (its function table was a base class's, shorter than a guard's), and StillExists()
  // only knows spawner guards ("Rare never deletes those under us" was wrong for Bridge's placed guards). The nine
  // moves before (logs 046, 057) handed back guards that were still alive in Rare's lists.
  // Before any hand-back or kill of a replaced guard: readable, a guard's function table (or a twin's / the boss's,
  // in the game image), the slots Rare's hide / restore calls (+832, +868, +872) recompiled functions and not the
  // pure-virtual handler, and the object still in Rare's character list. Else nothing of it is called.
  bool HandBackSafe(const uint8_t *base, uint32_t guard, std::string &why)
  {
    if (!REXCVAR_GET(pdz_counterop_safe_handback))
    {
      return true;
    }
    if (!Ptr(guard) || !pdz::guestmem::Ok(base, guard, kHidden + 4))
    {
      why = "its memory is not readable";
      return false;
    }
    const uint32_t vt = Load32(base, guard);
    if (vt != kGuardVtable && !counter_op_twins::IsTwin(guard) && !counter_op_mansion::IsBoss(guard))
    {
      why = fmt::format("its function table is {:08X}, not a guard's: a torn-down object", vt);
      return false;
    }
    if (vt < 0x82000000u || vt >= 0x83000000u || !pdz::guestmem::Ok(base, vt, 880))
    {
      why = fmt::format("its function table {:08X} is not in the game image", vt);
      return false;
    }
    for (const uint32_t slot : {832u, 868u, 872u})
    {
      const uint32_t fn = Load32(base, vt + slot);
      if (fn < 0x82000000u || fn >= 0x83000000u || !pdz::GuestFunction(fn, "counter-op: a replaced guard's hide / restore slot"))
      {
        why = fmt::format("its virtual +{} is {:08X}: no function of the game (a torn-down object)", slot, fn);
        return false;
      }
    }
    constexpr uint32_t kCharList = 0x82E454A0; // (CollectObjects: every character the level built, hidden ones too)
    bool listed = false;
    if (pdz::guestmem::Ok(base, kCharList + 0x710, 8))
    {
      uint32_t node = Load32(base, kCharList + 0x710);
      const uint32_t end = Load32(base, kCharList + 0x714);
      for (int i = 0; Ptr(node) && node != end && i < 4096 && !listed && pdz::guestmem::Ok(base, node, 12); ++i)
      {
        listed = Load32(base, node) == guard;
        node = Load32(base, node + 8);
      }
    }
    if (!listed)
    {
      why = "it is no longer in the game's character list (deleted by the level)";
      return false;
    }
    return true;
  }

  // A taken guard a level script shows again goes back out (kept hidden; logged the first 3 times per guard).
  void KeepTakenHidden(PPCContext &ctx, uint8_t *base)
  {
    for (const uint32_t guard : g.taken)
    {
      if (!counter_op_guards::StillExists(base, guard))
      {
        continue; // Rare deleted it (Datacore's stage-3 clean-up of its IndoorGuard01s)
      }
      if (!pdz::guestmem::Ok(base, guard, kHidden + 4) || // Sun 22 Nov night (audit): a guard Rare freed under us
          (Load32(base, guard) != kGuardVtable && !counter_op_twins::IsTwin(guard) && !counter_op_mansion::IsBoss(guard)) || (Load32(base, guard + kHidden) & 0x10)) // round 13 d, round 13 g
      {
        continue;
      }
      Call(ctx, base, sub_825818E0, guard, 1);
      const uint32_t n = ++g.rehidden[guard];
      if (n <= 3)
      {
        REXLOG_INFO("pdz_counterop: a level script showed the taken guard {:08X} (id '{}'): hidden again ({}{})", guard,
                    IdChars(SetupIdKey(base, Load32(base, guard + kSetup))),
                    (Load32(base, guard + kHidden) & 0x10) ? "ok" : "flag still clear",
                    n == 3 ? "; further times not logged" : "");
      }
    }
  }

  // pdz_counterop_fix_out_freeze (logs 022 and co_trinity_escape_2, hang dump 12:59:38): the spectate call of Rare's
  // camera update for player 2's camera (the agent's camera hook below) used to copy the registers as Rare's update
  // for the agent left them, so its f1 (the frame step; sub_82214C48 keeps it in f24) was whatever that update
  // computed last. The update adds step x rate to the camera's three sway angles (+124 / +128 / +132, 82214DCC) and
  // wraps each with "while (angle > pi) angle -= 2 pi" (82214E20 / E3C / E58): a step of infinity (or one large
  // enough that 2 pi is below the float's precision) never leaves that loop. The dump: the game thread in that loop
  // on player 2's camera (r30 = 021230E0, the noted camera), the angle +inf against pi; the agent's camera fine.
  // Now the call gets Rare's own step (sanitised: finite, 0..0.25 s, else 1/60 s), and the three angles are reset to
  // 0 if they are ever non-finite or over 1000 (before the call; Rare's update for him never runs while he is out).
  constexpr uint32_t kCamSway = 124;   // the three sway angles (radians, wrapped to -pi..pi by Rare's update)
  constexpr double kMaxStep = 0.25;    // a plausible frame step (s)
  void SpectateStepFix(uint8_t *base, PPCContext &frame, double step, bool on)
  {
    const double leftover = frame.f1.f64;
    const bool step_ok = std::isfinite(step) && step >= 0.0 && step <= kMaxStep;
    const double use = step_ok ? step : 1.0 / 60.0;
    if (on)
    {
      frame.f1.f64 = use;
    }
    ++g.spectate_calls;
    const bool bad = !std::isfinite(leftover) || leftover < 0.0 || leftover > kMaxStep;
    if (bad)
    {
      ++g.bad_leftover;
    }
    if (std::isfinite(leftover) && std::fabs(leftover) > g.max_leftover)
    {
      g.max_leftover = std::fabs(leftover);
    }
    if (g.spectate_calls <= 2 || (bad && g.bad_leftover <= 5))
    {
      REXLOG_INFO("pdz_counterop_fix_out_freeze ({}): spectate camera update {}: {}; the step register as the agent's "
                  "update left it was {}{}",
                  on ? "on" : "OFF", g.spectate_calls,
                  on ? fmt::format("Rare's step {:.4f} s given{}", use,
                                   step_ok ? "" : fmt::format(" (Rare's own was {}: not usable)", step))
                     : std::string("that leftover is passed on as the step (the old call)"),
                  leftover,
                  bad ? fmt::format(" (NOT a step: {} so far; passed on, it freezes the game)", g.bad_leftover)
                      : std::string(" (a plausible step this time)"));
    }
    if (!on)
    {
      return;
    }
    std::string reset;
    for (uint32_t i = 0; i < 3; ++i)
    {
      const float a = LoadFloat(base, g.p2_mode + kCamSway + i * 4);
      if (!std::isfinite(a) || std::fabs(a) > 1000.0f)
      {
        StoreFloat(base, g.p2_mode + kCamSway + i * 4, 0.0f);
        reset += fmt::format("{}+{} {} -> 0", reset.empty() ? "" : ", ", kCamSway + i * 4, a);
      }
    }
    if (!reset.empty() && ++g.angle_resets <= 5)
    {
      REXLOG_WARN("pdz_counterop_fix_out_freeze: player 2's camera {:08X} sway angles out of range, reset before Rare's "
                  "wrap loop: {} ({} time(s))",
                  g.p2_mode, reset, g.angle_resets);
    }
  }

  // Stage 0 probes at the start: the level's body / head lists, player 2's masks, his look and health.
  void LogProbes(const uint8_t *base)
  {
    const uint32_t lists = Load32(base, kGame + 8);
    for (const uint32_t at : {kListsBody, kListsHead})
    {
      const uint32_t list = Ptr(lists) ? Load32(base, lists + at) : 0;
      const int32_t count = Ptr(list) ? int32_t(Load32(base, list + kListCount)) : 0;
      const uint32_t entries = Ptr(list) ? Load32(base, list + kListEntries) : 0;
      std::string ids;
      for (int32_t i = 0; Ptr(entries) && i < count && i < 160; ++i)
      {
        const uint32_t e = Load32(base, entries + uint32_t(i) * 4);
        ids += fmt::format("{}{}", ids.empty() ? "" : ",", Ptr(e) ? int32_t(Load32(base, e)) : -1);
      }
      REXLOG_INFO("pdz_counterop: probe: the level's {} list: {} entries: {}", at ? "head" : "body", count,
                  ids.empty() ? std::string("none") : ids);
    }
    REXLOG_INFO("pdz_counterop: probe: player 2 {:08X}: team bits {:04X}, allies {:04X}, enemies {:04X}, +6276 {:08X} "
                "(0x400 without 0x800 = Rare's betrayal switch: his first hit on a friend clears his bits); player 1 "
                "{:08X}: team bits {:04X}",
                g.p2c, Load16(base, g.p2c + kTeamBits), Load16(base, g.p2c + kAllies), Load16(base, g.p2c + kEnemies),
                Load32(base, g.p2c + kBetrayal), g.p1c, Load16(base, g.p1c + kTeamBits));
    const uint32_t participant = Load32(base, g.p2c + kParticipantOf);
    REXLOG_INFO("pdz_counterop: probe: player 2 is built as body {} / head {} ({}); health {:.1f}, max health {:.1f} "
                "(participant {:08X}{}), armour {:.1f}; health scale {:.3f}, floor {:.0f}",
                Load32(base, g.p2c + kBodyId), Load32(base, g.p2c + kHeadId),
                g_look.decided ? (g_look.chosen ? fmt::format("the look chosen at load: {}", g_look.why)
                                                : fmt::format("the co-op look: {}", g_look.why))
                               : std::string("the co-op look: the factory hook made no decision"),
                LoadFloat(base, g.p2c + kHealth), g.coop_max_health, participant,
                participant == g.p2p ? "" : " (not his participant from the list!)", LoadFloat(base, g.p2c + kArmour),
                REXCVAR_GET(pdz_counterop_health_scale), kMinHealth);
    // The look as built, after his participant attached (825A99D8 -> 825B43E0 made the first-person hands).
    const uint32_t body_entry = Load32(base, g.p2c + kBodyEntryOf), head_entry = Load32(base, g.p2c + kHeadEntryOf);
    const uint32_t hand0 = Load32(base, g.p2c + kFirstPersonHands), hand1 = Load32(base, g.p2c + kFirstPersonHands + 4);
    REXLOG_INFO("pdz_counterop: look: at the start player 2 wears body {} ('{}', animation set +64 {:08X}) / head {} "
                "('{}'); body instance {:08X}, head instance {:08X}; player hands +20 {:08X}, first-person hands "
                "{:08X} / {:08X}{}",
                Load32(base, g.p2c + kBodyId),
                Ptr(body_entry) ? GuestString(base, Load32(base, body_entry + kBodyModelRef)) : std::string("?"),
                Ptr(body_entry) ? Load32(base, body_entry + kBodyAnimSet) : 0, Load32(base, g.p2c + kHeadId),
                Ptr(head_entry) ? GuestString(base, Load32(base, head_entry + kHeadModelRef)) : std::string("?"),
                Load32(base, g.p2c + 72), Load32(base, g.p2c + 4104),
                Ptr(body_entry) ? Load32(base, body_entry + kBodyHands) : 0, hand0, hand1,
                Ptr(hand0) && Ptr(hand1) ? "" : " (NO first-person hands: log 312's crash case)");
  }

  void Reset(uint8_t *base, const char *why)
  {
    PdzCounterOpWinRoundOver(); // round 10 (item 2): our win / lose line never outlives the round
    if (g.started)
    {
      std::string restored;
      if (g.max_health_set && Ptr(g.p2p) && ParticipantListed(base, g.p2p))
      {
        StoreFloat(base, g.p2p + kPartMaxHealth, g.coop_max_health);
        restored = fmt::format("; his max health back to {:.0f}", g.coop_max_health);
      }
      REXLOG_INFO("pdz_counterop: stopped ({}): player 2 lives {}, team bits re-applied {} time(s){}; spectate camera "
                  "calls {} ({} with a leftover step register that was not a step, largest finite {:.3g}; angle "
                  "resets {}; pdz_counterop_fix_out_freeze {})",
                  why, g.lives, g.team_fixes, restored, g.spectate_calls, g.bad_leftover, g.max_leftover,
                  g.angle_resets, REXCVAR_GET(pdz_counterop_fix_out_freeze) ? "on" : "off");
    }
    const uint32_t scratch = g.scratch;
    PdzCounterOpGreetReset(); // COUNTER-OP GREET
    PdzCounterOpObjectiveReset(); // round 6 (item 7)
    PdzCounterOpSpiderReset(); // round 7 (item 3)
    PdzCounterOpVoiceReset(); // COUNTER-OP VOICE
    PdzCounterOpHudLevelEnd(); // round 4: the ring / radar / prompt counts of the level
    REXLOG_INFO("pdz_counterop: animations: this level: {}", PdzCounterOpAnimSummary()); // round 5 (item 0)
    counter_op_guards::Reset(); // round 2
    counter_op_wake::Reset();   // round 4
    counter_op_jetpack::Reset();
    PdzCounterOpRules13Reset(); // round 13 a
    PdzCounterOpOutpostReset(); // round 13 f
    PdzCounterOpTurretsReset(); // round 13b q
    PdzCounterOpRooftopsReset(); // round 13 e
    counter_op_twins::Reset(); // round 13 d
    counter_op_mansion::Reset(); // round 13 g
    counter_op_river::Reset(); // round 13 h
    g_jet_woken = false;
    CameraProbeReset(); // round 16 (item 1)
    PdzCounterOpRareSpectateReset(); // Sat 21 Nov (spectate job)
    g_team[0] = kP2Team, g_team[1] = kP2Allies, g_team[2] = kP2Enemies;
    g_originals_left = ~0u;
    g = State{};
    g.scratch = scratch;
    g_cut = CutOff{}; // Sun 22 Nov (item 9)
    // Round 18: never past a reset (log 046's crash: kept from Outpost's end into Bridge's load).
    if (const uint32_t was = g_wait_hud_p2.exchange(0, std::memory_order_relaxed))
    {
      REXLOG_INFO("pdz_counterop: HUD: the waiting player 2 {:08X} is forgotten at this reset ({}): the crosshair check "
                  "no longer looks at him",
                  was, why);
    }
  }

  void Start(PPCContext &ctx, uint8_t *base)
  {
    g.started = true;
    g_played = false;
    std::string raw;
    g.mission = pdz::pace::SpLevelName(base, &raw);
    for (const MissionIds &m : kEligible)
    {
      if (g.mission == m.mission)
      {
        ParseIds(m.ids, g.ids);
      }
    }
    // Sat 21 Nov (pdz_counterop_deny_proven): the reviewed list replaces the 8 Nov one; its freed ids are eligible.
    const char *freed = nullptr;
    if (const char *proven = PdzCounterOpProvenDenied(g.mission, &freed))
    {
      ParseIds(proven, g.denied);
      std::unordered_set<uint32_t> freed_ids;
      if (freed)
      {
        ParseIds(freed, freed_ids);
      }
      for (const uint32_t id : freed_ids)
      {
        g.ids.insert(id);
      }
      REXLOG_INFO("pdz_counterop: start: {}: the reviewed denied list (pdz_counterop_deny_proven): {} kept ({}), {} "
                  "freed and eligible ({})",
                  g.mission, g.denied.size(), *proven ? proven : "-", freed_ids.size(), freed && *freed ? freed : "-");
    }
    else
    {
      for (const MissionIds &m : kDenied)
      {
        if (g.mission == m.mission)
        {
          ParseIds(m.ids, g.denied);
        }
      }
    }
    // Round 2 (items 1, 3): the guard side's first snapshot, with Spots()'s eligibility test (minus alive / taken).
    counter_op_guards::LevelStart(ctx, base, g.mission,
                                  [base](uint32_t c)
                                  {
                                    const uint32_t setup = Load32(base, c + kSetup);
                                    const uint8_t team = Ptr(setup) ? base[setup + kSetupTeam] : 0;
                                    if (Ptr(setup) && team != 0 && team != kTeamTemplate)
                                    {
                                      return false;
                                    }
                                    const uint32_t id = SetupIdKey(base, setup);
                                    if (id && g.denied.count(id))
                                    {
                                      return false;
                                    }
                                    if (id && g.ids.count(id))
                                    {
                                      return true;
                                    }
                                    const uint32_t type =
                                        Ptr(setup) ? Load32(base, setup + kSetupType) : Load32(base, c + kBodyId);
                                    return CombatType(type);
                                  });
    counter_op_guards::SetTakenTest([](uint32_t c) { return g.taken.count(c) != 0; });
    counter_op_jetpack::LevelStart(g.mission);
    counter_op_levels13::LevelStart(base, g.mission); // round 13 i: the snapshot of the placed guards
    const uint32_t participant = Load32(base, g.p2c + kParticipantOf);
    g.coop_max_health = Ptr(participant) ? LoadFloat(base, participant + kPartMaxHealth) : 100.0f;
    REXLOG_INFO("pdz_counterop: start: mission {} (Rare's '{}'), agent {:08X} (participant {:08X}, user {}), counter-op "
                "{:08X} (participant {:08X}, user {}); setup filter at load: {} setups checked, {} spawned for "
                "counter-op that co-op would not ({} of them single-player-only setups), {} co-op setups left out; {} "
                "eligible ids, {} denied ids",
                g.mission.empty() ? "?" : g.mission, raw, g.p1c, g.p1p, int32_t(Load32(base, g.p1p + kPartUser)), g.p2c,
                g.p2p, int32_t(Load32(base, g.p2p + kPartUser)), g_filter_seen, g_filter_added, g_filter_solo,
                g_filter_removed, g.ids.size(), g.denied.size());
    LogProbes(base);
    if (g.ids.empty() && !REXCVAR_GET(pdz_counterop_clones))
    {
      g.unsupported = true;
      REXLOG_INFO("pdz_counterop: {}: no eligible guards (Datacore and Arena have only runtime clones; "
                  "pdz_counterop_clones is off): plain co-op",
                  g.mission.empty() ? "this level" : g.mission);
      return;
    }
    if (g.ids.empty())
    {
      REXLOG_INFO("pdz_counterop: {}: no listed guards (Datacore / Arena: runtime clones only): pdz_counterop_clones: "
                  "player 2 replaces the clones the no-id rule accepts (a combat type with a gun in hand) and is never "
                  "out for good; the eligible count is the live clones of the last scan",
                  g.mission.empty() ? "this level" : g.mission);
    }
    // Player 2 is never Joanna's partner: out of the world until his first spot. Picks are tried once a second,
    // from kFirstPickSeconds on and never during a cutscene (log 304: the one pick ran before the opening cutscene).
    g_played = true;
    // The hide waits until Rare has updated player 2's camera once (its object is then known, so the spectate view
    // can drive it while he is out), at most kHideWaitSeconds.
    // Round 2 (item 4): with the dark view (pdz_counterop_dark_view) he goes out at once; the wait for his camera
    // object (the spectate view) only mattered while his view followed the agent.
    if (Ptr(g.p2_mode) || PdzCounterOpViewDark())
    {
      HideP2(ctx, base, "until his first spot");
    }
    else
    {
      g.hide_pending = true;
      g.hide_pending_since = std::chrono::steady_clock::now();
    }
    g.need_guard = true;
    g.after_death = false;
    g.need_since = std::chrono::steady_clock::now();
    const double first_pick_s = PdzCounterOpTestFirstPickS() > 0.0 ? PdzCounterOpTestFirstPickS() : kFirstPickSeconds; // Sat 21 Nov test aid
    g.next_try = g.need_since + std::chrono::milliseconds(int(first_pick_s * 1000));
    REXLOG_INFO("pdz_counterop: start: picks tried once a second from {:.0f} s on, outside cutscenes", first_pick_s);
  }

  // One pick try (the retry loop): true when player 2 spawned.
  bool TryPick(PPCContext &ctx, uint8_t *base, const char *why)
  {
    if (TakeGuard() && !counter_op_guards::OpeningSettled())
    {
      return false; // round 2: the first pick waits for the opening (the first cutscene over, the dormant split final)
    }
    PickDiag diag;
    const std::vector<Spot> spots = Spots(ctx, base, diag);
    if (!diag.registry_ok && !g.registry_warned)
    {
      g.registry_warned = true;
      REXLOG_WARN("pdz_counterop: Rare's object registry ([game +28] +20, +676..+680) gave no objects: dormant guards "
                  "can't be found; only the world list ({} objects) is used",
                  diag.world_objects);
    }
    Spot e;
    ++g.tries;
    const char *step = Pick(ctx, base, spots, e);
    if (step)
    {
      if (TakeGuard())
      {
        --diag.eligible;
        ++diag.taken;
        --(e.dormant ? diag.dormant : diag.live);
      }
      SpawnAt(ctx, base, e, diag, step, why);
      return true;
    }
    // Round 6 (item 3): Datacore's first half with both placed elites used up: one more of their kind (the next
    // try finds him).
    counter_op_guards::DatacoreExtraElite(ctx, base, g.p1c);
    // Round 12 (item 6): the platform top with only seated jetpack riders left: one guard made for him (the next
    // try finds him). TryPick is not called in a cutscene.
    counter_op_guards::DatacoreLastResort(
        ctx, base, g.p1c, std::chrono::duration<double>(std::chrono::steady_clock::now() - g.need_since).count());
    counter_op_guards::DatacoreOutUpdate(ctx, base); // round 14: or he is out once the riders are gone
    // One line a second for the first 10 failures, then one every 10 s.
    const auto now = std::chrono::steady_clock::now();
    if (g.diag_lines < 10 || now - g.last_diag >= std::chrono::seconds(10))
    {
      ++g.diag_lines;
      g.last_diag = now;
      REXLOG_INFO("pdz_counterop: pick {} found no spot (try {}): {}; registry objects {}, world objects {}, characters "
                  "{}, SP guards {}; no-id sources: {} clones, {} unreadable or unlisted ids",
                  why, g.tries, CountsText(diag), diag.registry_objects, diag.world_objects, diag.characters,
                  diag.guards, diag.clones, diag.no_id);
    }
    return false;
  }

  // Every frame in a co-op level (after Rare's death states).
  // Testing: makes the next real hit on a player fatal, so Rare's own hit and death path runs (co_jungle: a call of
  // the player handler 825AAF50 left the health alone: that handler is the hit feedback, rumble and screen effects;
  // the shared damage routine 82546BC8 takes stack arguments). The health is set to 0.5; player 2 is also moved 3 m in
  // front of the agent (forward = (sin yaw, cos yaw) of his view yaw, as OnAgentScreen) and turned to face him, so an
  // input script's trigger presses (auto-aim on with a controller) hit him.
  void TestKill(PPCContext &ctx, uint8_t *base, uint32_t victim, uint32_t agent)
  {
    // Round 2: a rider can't be moved in front of the agent (he is attached to the jetpack): only his health is
    // lowered; pdz_counterop_test_agent_aim turns the agent's view at him so the script's shots find him.
    if (!(victim == g.p2c && counter_op_jetpack::Riding(base, victim)) && victim == g.p2c && Ptr(agent) && g.scratch)
    {
      const float yaw = LoadFloat(base, agent + kViewYaw) * 3.14159265f / 180.0f;
      StoreFloat(base, g.scratch, LoadFloat(base, agent + kPosition) + 300.0f * std::sin(yaw));
      StoreFloat(base, g.scratch + 4, LoadFloat(base, agent + kPosition + 4));
      StoreFloat(base, g.scratch + 8, LoadFloat(base, agent + kPosition + 8) + 300.0f * std::cos(yaw));
      Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
      FaceAgent(ctx, base);
    }
    if (Load32(base, victim) == kHumanVtable && Ptr(Load32(base, victim + kParticipantOf)))
    {
      // a human's health (Health). Round 3: player 2 keeps a health floor (his own death), so he is left just above
      // it: the agent's next real hit ends the life.
      const bool floor = victim == g.p2c && REXCVAR_GET(pdz_counterop_own_death);
      StoreFloat(base, Load32(base, victim + kParticipantOf) + kPartMaxHealth, floor ? kOwnFloor + 0.5f : 0.5f);
    }
    else
    {
      StoreFloat(base, victim + kHealth, 0.5f);
    }
  }

  // N64 take: player 2 died, so the guard he replaced dies too, through Rare's own kill (the call level scripts make on
  // live guards): stage 1 = Rare's restore of the hidden guard (825818E0(g, 0)) and Rare's script teleport to where
  // player 2 died; stage 2 (the next tick) = Kill(guard, the agent, 5, 1), which applies all its health as damage
  // and fires the setup's on-death event links (82518128: how level scripts hear of a death) and the dead flag
  // (+0x44 bit 8), so group counts, objectives, doors and waves move on as if the agent had killed it. A guard a script
  // made invulnerable: Rare's network kill's tail (die 82550B08, health 0).
  uint32_t g_datacore_calm_victim = 0; // the replaced first-half guard being killed for player 2's death
  counter_op_guards::DatacorePhase g_datacore_phase = counter_op_guards::DatacorePhase::kNone;

  void KillTakenGuardStep(PPCContext &ctx, uint8_t *base)
  {
    const uint32_t c = g.kill_guard;
    if (!counter_op_guards::StillExists(base, c))
    {
      REXLOG_INFO("pdz_counterop: replaced guard {:08X}: no longer in Rare's lists (deleted by the level): no kill", c);
      g.taken.erase(c);
      g.kill_guard = 0;
      g.kill_stage = 0;
      return;
    }
    if (std::string unsafe; !HandBackSafe(base, c, unsafe)) // Mon 23 Nov (co21)
    {
      REXLOG_INFO("pdz_counterop: replaced guard {:08X}: not restored and not killed ({}): it stays out of the world "
                  "(pdz_counterop_safe_handback)",
                  c, unsafe);
      g.taken.erase(c);
      g.kill_guard = 0;
      g.kill_stage = 0;
      return;
    }
    const bool usable = Ptr(c) && (Load32(base, c) == kGuardVtable || counter_op_twins::IsTwin(c) || counter_op_mansion::IsBoss(c)) && // round 13 d, round 13 g
                        Load32(base, Load32(base, c) + 0x2E4) == 0x8252DE88 && g.scratch;
    if (!usable || LoadFloat(base, c + kHealth) <= 0.0f || (Load32(base, c + 0x44) & 8))
    {
      REXLOG_INFO("pdz_counterop: replaced guard {:08X}: {} (no kill needed or possible)", c,
                  !usable ? "not a guard with Rare's kill" : "already dead");
      g.taken.erase(c);
      g.kill_guard = 0;
      g.kill_stage = 0;
      return;
    }
    if (g.kill_stage == 0)
    {
      // Round 5 (item 4): a first-half guard of Datacore dying for player 2's death must not run Rare's rule that
      // turns the guards around on the agent (the 82303588 hook asks PdzCounterOpDatacoreCalm).
      g_datacore_calm_victim = counter_op_guards::IsFirstHalfGuard(base, c) ? c : 0;
      const bool hidden = (Load32(base, c + kHidden) & 0x10) != 0;
      if (hidden)
      {
        Call(ctx, base, sub_825818E0, c, 0); // Rare's restore (world, physics, team lists)
      }
      StoreFloat(base, g.scratch, g.kill_at[0]);
      StoreFloat(base, g.scratch + 4, g.kill_at[1]);
      StoreFloat(base, g.scratch + 8, g.kill_at[2]);
      const bool true_origin = REXCVAR_GET(pdz_counterop_true_origin) && g.kill_origin;
      if (true_origin)
      {
        // Round 6 (item 6): the move alone (825AD0E0 = a history clear + this + the guard's Update with a zero step),
        // to player 2's origin; then his facing (the move leaves yaw 0) and the zero-step Update, so the body and
        // its pose are where and how he stood.
        Call(ctx, base, sub_825AD148, c, g.scratch, 0);
        if (g.kill_yaw_ok)
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = c;
          frame.ctx.f1.f64 = double(g.kill_yaw);
          sub_82519A38(frame.ctx, base);
        }
        if (Load32(base, Load32(base, c) + 4) == 0x825800F8)
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = c;
          frame.ctx.f1.f64 = 0.0;
          sub_825800F8(frame.ctx, base);
        }
      }
      else
      {
        Call(ctx, base, sub_825AD0E0, c, g.scratch);
      }
      g.taken.erase(c); // no longer kept hidden
      g.kill_stage = 1;
      g.body_log = 40;
      g.body_log_guard = c;
      g.body_series.clear();
      float guard_at[3] = {};
      OriginOf(ctx, base, c, guard_at);
      const bool same_tick = true_origin && REXCVAR_GET(pdz_counterop_kill_same_tick);
      REXLOG_INFO("pdz_counterop: replaced guard {:08X} '{}': {} and moved to where player 2 died ({:.0f}, {:.0f}, "
                  "{:.0f}) ({}); the guard's origin is now ({:.0f}, {:.0f}, {:.0f}), facing {}; Rare's kill {}",
                  c, IdChars(SetupIdKey(base, Load32(base, c + kSetup))),
                  hidden ? "restored (Rare's 825818E0(g, 0))" : "in the world", g.kill_at[0], g.kill_at[1],
                  g.kill_at[2], true_origin ? "his origin at the feet" : "his +48, waist height: rounds 1-5",
                  guard_at[0], guard_at[1], guard_at[2],
                  true_origin && g.kill_yaw_ok ? fmt::format("as he did ({:.1f} deg)", g.kill_yaw * 57.29578)
                                               : std::string("as the move leaves it"),
                  same_tick ? "in this same tick (never drawn standing)" : "next tick");
      if (!same_tick)
      {
        return;
      }
    }
    const float before = LoadFloat(base, c + kHealth);
    // Round 6 (item 6): the death animation by Rare's own hit-type chooser (the one its damage routine calls at
    // 825472C4): body part 0x12, from where the agent stands and faces -> a Hits_* animation type (front, back, left,
    // right). 5 (Hits_Head: a head shot from the front, every time) was rounds 1-5 and stays the fallback.
    uint32_t hit_type = 5;
    if (REXCVAR_GET(pdz_counterop_kill_hit_type) && REXCVAR_GET(pdz_counterop_true_origin) && Ptr(g.p1c))
    {
      const uint32_t from = Call(ctx, base, sub_82151210, g.p1c);
      if (Ptr(from))
      {
        std::memset(base + g.scratch + 0x10, 0, 0x30);
        Call(ctx, base, sub_82518E08, g.p1c, g.scratch + 0x20);
        // The chooser's body-part numbers are not the damage classes of the hit (0 head, 1 body, 2 limb): every
        // part 0..31 is asked, and the first one whose answer is a head / torso / limb animation is used for the
        // class of the hit that killed him (the answer already carries front, back or side from where she stands).
        const int cls = PdzCounterOpPvpLastPart();
        uint32_t types[32] = {};
        std::string table;
        for (uint32_t part = 0; part < 32; ++part)
        {
          std::memset(base + g.scratch + 0x10, 0, 0x10);
          types[part] = Call(ctx, base, sub_8225A5D8, g.scratch + 0x10, part, c, from, g.scratch + 0x20);
          table += fmt::format("{}{}", part ? "," : "", types[part]);
        }
        uint32_t used_part = 32;
        for (uint32_t part = 0; part < 32 && used_part == 32; ++part)
        {
          const uint32_t t = types[part];
          const bool head = t >= 5 && t <= 8, torso = t >= 1 && t <= 4, limb = t >= 9 && t <= 0x24;
          if ((cls == 0 && head) || (cls == 2 && limb) || (cls != 0 && cls != 2 && torso))
          {
            used_part = part;
            hit_type = t;
          }
        }
        static bool table_logged = false;
        REXLOG_INFO("pdz_counterop: replaced guard {:08X}: the hit that killed player 2 was class {} ({}); Rare's "
                    "hit-type chooser (8225A5D8, from where the agent stands) -> death animation type {} (body part "
                    "{}){}",
                    c, cls, cls == 0 ? "head" : cls == 2 ? "limb" : cls == 1 ? "body" : "not known: body",
                    hit_type, used_part == 32 ? std::string("none found: 5") : std::to_string(used_part),
                    table_logged ? std::string() : "; the chooser's answers for parts 0-31 here: " + table);
        table_logged = true;
      }
    }
    Call(ctx, base, sub_8252DE88, c, g.p1c, hit_type, 1);
    bool tail = false;
    if (LoadFloat(base, c + kHealth) > 0.0f)
    {
      tail = true;
      Call(ctx, base, sub_82550B08, c);
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c;
      frame.ctx.f1.f64 = 0.0;
      frame.ctx.r5.u64 = 0;
      sub_8252D6A8(frame.ctx, base);
    }
    const uint32_t id = SetupIdKey(base, Load32(base, c + kSetup));
    if (id && g.ids.count(id))
    {
      g.dead_ids.insert(id);
    }
    REXLOG_INFO("pdz_counterop: replaced guard {:08X} '{}' killed through Rare's kill (8252DE88, credited to the agent): "
                "health {:.1f} -> {:.1f}, dead flag +0x44 {}{}; eligible guards left {}",
                c, IdChars(id), before, LoadFloat(base, c + kHealth), (Load32(base, c + 0x44) & 8) ? "set" : "NOT set",
                tail ? " (an invulnerable guard: die + health 0 as Rare's network kill)" : "", EligibleLeft());
    g.kill_guard = 0;
    g.kill_stage = 0;
  }

  void SeekTick(PPCContext &ctx, uint8_t *base, bool cutscene); // pdz_counterop_test_seek (below)

  // Round 5 (item 4b): Datacore's mid-level cutscene. Rare deletes the first-half guards when it ends; if player 2 is
  // one of them he leaves the world first (no death, no life lost) and the replaced guard is given back to the level
  // script alive, in its spawner slot, so Rare's delete takes it with the others. Called from the tick as soon as the
  // cutscene is flagged, and from the 82304188 hook (Rare is about to delete that spawner's guards).
  void DatacoreGiveBack(PPCContext &ctx, uint8_t *base, const char *why)
  {
    const uint32_t guard = g.current_guard;
    if (!Ptr(guard) || (!counter_op_guards::IsFirstHalfGuard(base, guard) && !counter_op_mansion::GiveBackNow())) // round 13 g: or Mansion's last fight asks
    {
      return;
    }
    std::string unsafe; // Mon 23 Nov (co21)
    const bool safe = HandBackSafe(base, guard, unsafe);
    if (!safe)
    {
      REXLOG_INFO("pdz_counterop: give-back: replaced guard {:08X} is not handed back ({}): it stays out of the world "
                  "(pdz_counterop_safe_handback)",
                  guard, unsafe);
    }
    const bool hidden = safe && (Load32(base, guard + kHidden) & 0x10) != 0;
    if (!g.out)
    {
      HideP2(ctx, base, "datacore: the mid-level cutscene");
    }
    g.taken.erase(guard); // before the restore: the keep-hidden pass must not hide it again
    g.current_guard = 0;
    if (hidden)
    {
      Call(ctx, base, sub_825818E0, guard, 0); // Rare's restore (world, physics, team lists): alive, in its slot
    }
    const auto now = std::chrono::steady_clock::now();
    g.set_health = 0.0f;
    g.health_check = false;
    g.getup_rescue_at = {};
    g.need_guard = true;
    g.after_death = false;
    g.need_since = now;
    g.next_try = now + std::chrono::milliseconds(int(std::max(kNextGuardSeconds, PdzCounterOpTestFirstPickS()) * 1000)); // (Sat 21 Nov: the test wait too)
    if (counter_op_mansion::GiveBackForBoss())
    {
      g.next_try = now; // round 16 (item 8a): he becomes Mai Hem on the next tick
    }
    g.hide_if_no_spot = false;
    g.tries = 0;
    REXLOG_INFO("pdz_counterop: datacore: {}: player 2 taken out of the world (no death, life {} not counted as a "
                "death); replaced guard {:08X} {} and given back to the level script, not killed: Rare's delete takes "
                "it with the others; he waits for an end-area guard",
                why, g.lives, guard, hidden ? "restored (Rare's 825818E0(g, 0))" : "was already in the world");
  }

  // Round 3: the end of one of player 2's lives without Rare's co-op death (see the tick).
  void OwnDeath(PPCContext &ctx, uint8_t *base)
  {
    const uint32_t p2c = g.p2c, p2p = g.p2p;
    PdzCounterOpTurretBeforeHide(ctx, base, p2c, "his life ends"); // round 13b q: off a stationary gun before his position is read
    const float at[3] = {LoadFloat(base, p2c + kPosition), LoadFloat(base, p2c + kPosition + 4),
                         LoadFloat(base, p2c + kPosition + 8)};
    const auto now = std::chrono::steady_clock::now();
    ++g.own_deaths;
    REXLOG_INFO("pdz_counterop: player 2 is killed (life {}, own death #{}): health {:.2f} at his floor {:.1f}, at "
                "({:.0f}, {:.0f}, {:.0f}); no co-op death (Rare's death state stays {}): out of the world now, the "
                "replaced guard {:08X} dies here through Rare's kill, the next guard in {:.1f} s",
                g.lives, g.own_deaths, Health(base, p2c), kOwnFloor, at[0], at[1], at[2],
                int32_t(Load32(base, p2p + kPartDeathState)), g.current_guard, kNextGuardSeconds);
    if (TakeGuard() && Ptr(g.current_guard))
    {
      g.kill_guard = g.current_guard;
      g.current_guard = 0;
      std::memcpy(g.kill_at, at, sizeof(at));
      // Round 6 (item 6): where his FEET were and which way he faced (his +48 is waist height).
      float origin[3];
      double yaw = 0.0;
      g.kill_origin = REXCVAR_GET(pdz_counterop_true_origin) && OriginOf(ctx, base, p2c, origin);
      if (g.kill_origin)
      {
        std::memcpy(g.kill_at, origin, sizeof(origin));
      }
      g.kill_yaw_ok = REXCVAR_GET(pdz_counterop_true_origin) && YawOf(ctx, base, p2c, yaw);
      g.kill_yaw = float(yaw);
      g.kill_ready = true;
    }
    PdzCounterOpVoiceDeath(ctx, base, p2c); // COUNTER-OP VOICE (off by default)
    HideP2(ctx, base, "his guard was killed");
    // A full bar while he waits (no low-health effects in his dark half; the next spawn sets the guard's share).
    StoreFloat(base, p2p + kPartMaxHealth, g.coop_max_health > 0.0f ? g.coop_max_health : 100.0f);
    g.set_health = 0.0f;
    g.health_check = false;
    g.getup_rescue_at = {};
    g.need_guard = true;
    g.after_death = true;
    g.need_since = now;
    g.next_try = now + std::chrono::milliseconds(int(std::max(kNextGuardSeconds, PdzCounterOpTestFirstPickS()) * 1000)); // (Sat 21 Nov: the test wait too)
    g.hide_if_no_spot = false;
    g.tries = 0;
    g.diag_lines = 0;
  }

  // Testing (pdz_counterop_test_prompt_tour): see the cvar. The props come from the world list: doors (vtable
  // 820C27C8: +0x49C state, +0x4A0 / +0x4A1 locked from either side, +0x4A3 a Locktopus can work it) and hackable
  // props (vtable 820C5180). Player 2 is moved with Rare's teleport (the move the spawn uses) 1.1 m to each of the
  // four sides of the prop in turn and turned to face it.
  struct PromptTour
  {
    int stage = -1; // -1 not started, 0..2 the three props, 3 done
    int side = 0;
    uint32_t prop[3] = {0, 0, 0};
    float back[3] = {0, 0, 0};
    uint32_t door_state = 0;
    uint32_t sums[3] = {0, 0, 0};
    std::chrono::steady_clock::time_point next{};
    uint32_t level = 0;
  };
  PromptTour g_tour;
  void PromptTourTick(PPCContext &ctx, uint8_t *base, bool cutscene)
  {
    if (!REXCVAR_GET(pdz_counterop_test_prompt_tour) || cutscene || g.out || g.spectating || !Ptr(g.p2c) || !g.scratch ||
        g.lives == 0)
    {
      return;
    }
    constexpr uint32_t kDoorVt = 0x820C27C8, kHackVt = 0x820C5180;
    static constexpr const char *kName[3] = {"the nearest locked door", "the nearest hackable prop",
                                             "the nearest unlocked door"};
    const auto now = std::chrono::steady_clock::now();
    PromptTour &t = g_tour;
    if (t.level != g.state_object)
    {
      t = PromptTour{};
      t.level = g.state_object;
      t.next = now + std::chrono::seconds(6);
      return;
    }
    if (t.stage == 2 && t.side > 0 && Ptr(t.prop[2]) && Load32(base, t.prop[2]) == kDoorVt)
    {
      static uint32_t seen_prop = 0, seen_state = 0;
      const uint32_t st = Load32(base, t.prop[2] + 0x49C);
      if (seen_prop != t.prop[2])
      {
        seen_prop = t.prop[2];
        seen_state = st;
      }
      if (st != seen_state)
      {
        uint32_t r[5];
        PdzCounterOpHudWatchRead(r);
        REXLOG_INFO("pdz_counterop: test prompts: the unlocked door {:08X}: state +0x49C {} -> {} while player 2 stands "
                    "at side {} (prompts offered to him there so far: {})",
                    t.prop[2], seen_state, st, t.side, r[2]);
        seen_state = st;
      }
    }
    if (t.stage == 3 || now < t.next)
    {
      return;
    }
    const float px = LoadFloat(base, g.p2c + kPosition), py = LoadFloat(base, g.p2c + kPosition + 4),
                pz = LoadFloat(base, g.p2c + kPosition + 8);
    if (t.stage < 0)
    {
      float best[3] = {1e18f, 1e18f, 1e18f};
      uint32_t doors = 0, locked = 0, hackables = 0;
      const uint32_t world = Load32(base, kGame + 24);
      const uint32_t end = Ptr(world) ? Load32(base, world + kWorldEnd) : 0;
      uint32_t node = Ptr(world) ? Load32(base, world + kWorldFirst) : 0;
      for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
      {
        const uint32_t o = Load32(base, node + 4);
        const uint32_t vt = Ptr(o) ? Load32(base, o) : 0;
        if (vt != kDoorVt && vt != kHackVt)
        {
          continue;
        }
        const float d = Dist3(LoadFloat(base, o + kPosition), LoadFloat(base, o + kPosition + 4),
                              LoadFloat(base, o + kPosition + 8), px, py, pz);
        int k = 1;
        if (vt == kDoorVt)
        {
          ++doors;
          const bool is_locked = base[o + 0x4A0] || base[o + 0x4A1];
          locked += is_locked;
          k = is_locked ? 0 : 2;
        }
        else
        {
          ++hackables;
        }
        if (d < best[k])
        {
          best[k] = d;
          t.prop[k] = o;
        }
      }
      t.back[0] = px, t.back[1] = py, t.back[2] = pz;
      REXLOG_INFO("pdz_counterop: test prompts: the world list holds {} doors ({} locked) and {} hackable props; nearest "
                  "to player 2: locked door {:08X} ({:.0f} m), hackable {:08X} ({:.0f} m), unlocked door {:08X} ({:.0f} m)",
                  doors, locked, hackables, t.prop[0], best[0] / 100.0f, t.prop[1], best[1] / 100.0f, t.prop[2],
                  best[2] / 100.0f);
      t.stage = 0;
      t.side = 0;
    }
    // The last side's result, then the next side / prop.
    if (t.side > 0 && Ptr(t.prop[t.stage]))
    {
      uint32_t r[5];
      PdzCounterOpHudWatchRead(r);
      for (int k = 0; k < 3; ++k)
      {
        t.sums[k] += r[k];
      }
      const uint32_t prop = t.prop[t.stage];
      REXLOG_INFO("pdz_counterop: test prompts: {} {:08X}, side {} of 4: Rare asked {} prompt(s) for player 2 here; {} "
                  "denied, {} offered (last kind {}, text {:X}){}",
                  kName[t.stage], prop, t.side, r[0], r[1], r[2], r[3], r[4],
                  Load32(base, prop) == kDoorVt
                      ? fmt::format("; door state +0x49C {} (was {}), locked bytes {} / {}, lockable {}",
                                    Load32(base, prop + 0x49C), t.door_state, int(base[prop + 0x4A0]),
                                    int(base[prop + 0x4A1]), int(base[prop + 0x4A3]))
                      : std::string());
    }
    if (t.side >= 4 || !Ptr(t.prop[t.stage]))
    {
      if (Ptr(t.prop[t.stage]))
      {
        REXLOG_INFO("pdz_counterop: test prompts: {} {:08X}: in all {} asked, {} denied, {} offered", kName[t.stage],
                    t.prop[t.stage], t.sums[0], t.sums[1], t.sums[2]);
      }
      else
      {
        REXLOG_INFO("pdz_counterop: test prompts: {}: none in this level's world list", kName[t.stage]);
      }
      t.sums[0] = t.sums[1] = t.sums[2] = 0;
      t.side = 0;
      if (++t.stage == 3)
      {
        PdzCounterOpHudWatch(0);
        StoreFloat(base, g.scratch, t.back[0]);
        StoreFloat(base, g.scratch + 4, t.back[1]);
        StoreFloat(base, g.scratch + 8, t.back[2]);
        Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
        REXLOG_INFO("pdz_counterop: test prompts: done; player 2 moved back to ({:.0f}, {:.0f}, {:.0f})", t.back[0],
                    t.back[1], t.back[2]);
        return;
      }
      if (!Ptr(t.prop[t.stage]))
      {
        return; // reported on the next tick
      }
    }
    const uint32_t prop = t.prop[t.stage];
    static constexpr float kSide[4][2] = {{110.0f, 0.0f}, {-110.0f, 0.0f}, {0.0f, 110.0f}, {0.0f, -110.0f}};
    const float ox = LoadFloat(base, prop + kPosition), oy = LoadFloat(base, prop + kPosition + 4),
                oz = LoadFloat(base, prop + kPosition + 8);
    if (t.side == 0 && Load32(base, prop) == kDoorVt)
    {
      t.door_state = Load32(base, prop + 0x49C);
    }
    StoreFloat(base, g.scratch, ox + kSide[t.side][0]);
    StoreFloat(base, g.scratch + 4, oy);
    StoreFloat(base, g.scratch + 8, oz + kSide[t.side][1]);
    Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g.p2c;
      frame.ctx.f1.f64 = 0.0;
      frame.ctx.f2.f64 = std::atan2(double(-kSide[t.side][0]), double(-kSide[t.side][1]));
      sub_825AD3B0(frame.ctx, base);
    }
    PdzCounterOpHudWatch(prop);
    ++t.side;
    // Round 5 (item 3): 4 s a side at the unlocked door, so his script pad's A presses land while its prompt is up.
    t.next = now + std::chrono::milliseconds(t.stage == 2 ? 4000 : 1500);
    if (t.side == 1)
    {
      REXLOG_INFO("pdz_counterop: test prompts: player 2 moved to {} {:08X} at ({:.0f}, {:.0f}, {:.0f}) (a test tool's "
                  "move, four sides in turn)",
                  kName[t.stage], prop, ox, oy, oz);
    }
  }

  // ---- Sun 22 Nov (item 9, pdz_counterop_cutoff): player 2 cut off from the agent -> a guard ahead ----
  float CutDistance(const uint8_t *base, uint32_t a, uint32_t b)
  {
    const float dx = LoadFloat(base, a + kPosition) - LoadFloat(base, b + kPosition);
    const float dy = LoadFloat(base, a + kPosition + 4) - LoadFloat(base, b + kPosition + 4);
    const float dz = LoadFloat(base, a + kPosition + 8) - LoadFloat(base, b + kPosition + 8);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  // He leaves this body without a death: PdzCounterOpReleaseBody (out of the world, the guard restored alive), then
  // the guard is put where player 2 stood, facing as he did, so the game's AI carries on from there.
  void CutOffMove(PPCContext &ctx, uint8_t *base, const std::string &why)
  {
    const uint32_t guard = g.current_guard, p2c = g.p2c;
    float at[3] = {LoadFloat(base, p2c + kPosition), LoadFloat(base, p2c + kPosition + 4),
                   LoadFloat(base, p2c + kPosition + 8)};
    const bool feet = OriginOf(ctx, base, p2c, at);
    double yaw = 0.0;
    const bool yaw_ok = YawOf(ctx, base, p2c, yaw);
    const std::string id =
        Rd(base, guard, kSetup + 4) ? IdChars(SetupIdKey(base, Load32(base, guard + kSetup))) : std::string("none");
    ++g_cut.moves;
    REXLOG_INFO("pdz_counterop: cut off (#{}): {}: player 2 (life {}) moves to a guard ahead by the normal pick rules; "
                "his guard '{}' {:08X} goes back to the game's AI alive at ({:.0f}, {:.0f}, {:.0f}) (pdz_counterop_cutoff)",
                g_cut.moves, why, g.lives, id, guard, at[0], at[1], at[2]);
    PdzCounterOpReleaseBody(ctx, base, "cut off from the agent (pdz_counterop_cutoff)");
    if (Ptr(guard))
    {
      g_cut.handed_back[guard] = std::chrono::steady_clock::now(); // round 18 (item 4): not picked again for a while
    }
    g_cut.prefer_next_until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    if (Rd(base, guard, 0x200) && counter_op_guards::StillExists(base, guard) && (Load32(base, guard + kHidden) & 0x10) == 0 &&
        LoadFloat(base, guard + kHealth) > 0.0f && g.scratch)
    {
      StoreFloat(base, g.scratch, at[0]);
      StoreFloat(base, g.scratch + 4, at[1]);
      StoreFloat(base, g.scratch + 8, at[2]);
      Call(ctx, base, sub_825AD148, guard, g.scratch, 0);
      if (yaw_ok)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = guard;
        frame.ctx.f1.f64 = yaw;
        sub_82519A38(frame.ctx, base);
      }
      if (Rd(base, Load32(base, guard) + 4, 4) && Load32(base, Load32(base, guard) + 4) == 0x825800F8)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = guard;
        frame.ctx.f1.f64 = 0.0;
        sub_825800F8(frame.ctx, base);
      }
      REXLOG_INFO("pdz_counterop: cut off: guard '{}' {:08X} is the game's again, alive (health {:.0f}), now at ({:.0f}, "
                  "{:.0f}, {:.0f}) ({}), facing {}",
                  id, guard, LoadFloat(base, guard + kHealth), LoadFloat(base, guard + kPosition),
                  LoadFloat(base, guard + kPosition + 4), LoadFloat(base, guard + kPosition + 8),
                  feet ? "his feet where player 2's were" : "player 2's +48", yaw_ok ? "as player 2 did" : "as the move left it");
    }
    else if (Ptr(guard))
    {
      REXLOG_INFO("pdz_counterop: cut off: guard {:08X} not moved (gone from the level, still hidden or not alive)", guard);
    }
    g_cut.section_since = {};
    g_cut.far_since = {};
    g_cut.door = 0;
  }

  // Round 18 (item 4): the doors looked at twice a second (round 17's rule (a) look, now also while he waits): a fresh
  // lock of a closed door near the agent is remembered for the picks; with his body, rule (a) is armed as before.
  void DoorLook(uint8_t *base, std::chrono::steady_clock::time_point now, bool his_body)
  {
    if (now < g_cut.door_next)
    {
      return;
    }
    {
      g_cut.door_next = now + std::chrono::milliseconds(500);
      const uint32_t world = Load32(base, kGame + 24);
      if (world != g_cut.world)
      {
        g_cut.world = world;
        g_cut.doors.clear();
        g_cut.doors_known = false;
      }
      const bool world_ok = Rd(base, world, 0x400);
      const uint32_t end = world_ok ? Load32(base, world + kWorldEnd) : 0;
      uint32_t node = world_ok ? Load32(base, world + kWorldFirst) : 0;
      for (int i = 0; Rd(base, node, 16) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
      {
        const uint32_t o = Load32(base, node + 4);
        if (!Rd(base, o, 0x4A4) || Load32(base, o) != 0x820C27C8u)
        {
          continue;
        }
        // Only a LOCK (a lock byte going from 0 to set) of a closed door counts: Trinity's round doors are locked for
        // good and open / close by themselves, and such a closing is no cut-off.
        // A fresh lock is judged once the door is closed, within 5 s of the lock (a door may still be swinging).
        const bool locked = base[o + 0x4A0] || base[o + 0x4A1];
        const bool shut = locked && Load32(base, o + 0x49C) == 1;
        const bool seen = g_cut.doors.count(o) != 0;
        CutOff::DoorSeen &ds = g_cut.doors[o];
        if (seen && locked && !ds.locked)
        {
          ds.armed = true;
          ds.locked_at = now;
        }
        ds.locked = locked;
        if (!locked || (ds.armed && now - ds.locked_at > std::chrono::seconds(5)))
        {
          ds.armed = false;
        }
        if (!ds.armed || !shut)
        {
          continue;
        }
        ds.armed = false;
        // Round 18 (item 4): a lock near her is remembered with the side she is on (every pick keeps off its far side).
        const float dj = CutDistance(base, o, g.p1c);
        if (dj < 2000.0f)
        {
          float nx = LoadFloat(base, g.p1c + kPosition) - LoadFloat(base, o + kPosition);
          float nz = LoadFloat(base, g.p1c + kPosition + 8) - LoadFloat(base, o + kPosition + 8);
          const float len = std::sqrt(nx * nx + nz * nz);
          if (len > 1.0f)
          {
            CutOff::Locked rec;
            rec.door = o, rec.x = LoadFloat(base, o + kPosition), rec.y = LoadFloat(base, o + kPosition + 4);
            rec.z = LoadFloat(base, o + kPosition + 8), rec.nx = nx / len, rec.nz = nz / len;
            std::erase_if(g_cut.locked, [o](const CutOff::Locked &l) { return l.door == o; });
            g_cut.locked.push_back(rec);
            REXLOG_INFO("pdz_counterop: pick: door {:08X} at ({:.0f}, {:.0f}, {:.0f}) locked by the agent ({:.1f} m from it): "
                        "no pick takes a guard on its far side from now on (pdz_counterop_pick_locked_doors)",
                        o, rec.x, rec.y, rec.z, dj / 100.0f);
          }
        }
        if (!his_body)
        {
          continue;
        }
        // It has just been locked, closed: near her, and the two of them on its two sides?
        const float dp = CutDistance(base, o, g.p2c);
        const float jx = LoadFloat(base, g.p1c + kPosition) - LoadFloat(base, o + kPosition);
        const float jz = LoadFloat(base, g.p1c + kPosition + 8) - LoadFloat(base, o + kPosition + 8);
        const float px = LoadFloat(base, g.p2c + kPosition) - LoadFloat(base, o + kPosition);
        const float pz = LoadFloat(base, g.p2c + kPosition + 8) - LoadFloat(base, o + kPosition + 8);
        const bool across = jx * px + jz * pz < 0.0f;
        REXLOG_INFO("pdz_counterop: cut off: door {:08X} at ({:.0f}, {:.0f}, {:.0f}) was just LOCKED, closed: the agent "
                    "{:.1f} m from it, player 2 {:.1f} m{}",
                    o, LoadFloat(base, o + kPosition), LoadFloat(base, o + kPosition + 4), LoadFloat(base, o + kPosition + 8),
                    dj / 100.0f, dp / 100.0f,
                    dj < 2000.0f && dp > 300.0f && across ? ": he is on its other side (rule (a) armed)" : ": not between them");
        if (dj < 2000.0f && dp > 300.0f && across)
        {
          g_cut.door = o;
          g_cut.door_at = now;
        }
      }
      g_cut.doors_known = true;
    }
    // A remembered door unlocked again (or gone) is forgotten.
    std::erase_if(g_cut.locked, [base](const CutOff::Locked &l) {
      const bool gone = !Rd(base, l.door, 0x4A4) || Load32(base, l.door) != 0x820C27C8u;
      if (!gone && (base[l.door + 0x4A0] || base[l.door + 0x4A1]))
      {
        return false;
      }
      REXLOG_INFO("pdz_counterop: pick: door {:08X} {}: the picks may take guards beyond it again", l.door,
                  gone ? "is gone" : "is unlocked again");
      return true;
    });
  }

  void CutOffTick(PPCContext &ctx, uint8_t *base, bool cutscene)
  {
    const auto now = std::chrono::steady_clock::now();
    // The test tool's lock is put back after 20 s, whatever happened.
    if (g_cut.test_door && now >= g_cut.test_until && Rd(base, g_cut.test_door, 0x4A4))
    {
      base[g_cut.test_door + 0x4A0] = g_cut.test_bytes[0];
      base[g_cut.test_door + 0x4A1] = g_cut.test_bytes[1];
      REXLOG_INFO("pdz_counterop: cut off: TEST door {:08X}: its lock bytes put back ({} / {})", g_cut.test_door,
                  g_cut.test_bytes[0], g_cut.test_bytes[1]);
      g_cut.test_door = 0;
    }
    const bool his_body = REXCVAR_GET(pdz_counterop_cutoff) && !g.out && Ptr(g.current_guard) && !cutscene &&
                          !g.kill_guard && !g.revive_participant && !g.wait_getup && !PdzCounterOpSpiderDriving() &&
                          Rd(base, g.p1c, 0x200) && Rd(base, g.p2c, 0x200) &&
                          !counter_op_jetpack::VehicleOf(base, g.p2c);
    // Round 18 (item 7): the test tool asks the after-cutscene check.
    {
      static int32_t test_scene_last = 0;
      const int32_t t = REXCVAR_GET(pdz_counterop_test_after_cutscene);
      if (t != test_scene_last && t != 0)
      {
        g_cut.after_scene_at = now + std::chrono::seconds(1);
        REXLOG_INFO("pdz_counterop: cut off: TEST (pdz_counterop_test_after_cutscene {}): the after-cutscene check in 1 s", t);
      }
      test_scene_last = t;
    }
    // Round 18 (item 4): the doors are looked at whether or not he has a body (a lock while he waits counts too).
    if (Rd(base, g.p1c, 0x200) && !cutscene)
    {
      DoorLook(base, now, his_body);
    }
    if (!his_body)
    {
      g_cut.section_since = {};
      g_cut.far_since = {};
      g_cut.door = 0;
      return;
    }
    const float dist = CutDistance(base, g.p1c, g.p2c);
    // Round 18 (item 7, pdz_counterop_cutoff_after_cutscene): 1 s after a cutscene, is he left behind?
    if (g_cut.after_scene_at != std::chrono::steady_clock::time_point{} && now >= g_cut.after_scene_at)
    {
      g_cut.after_scene_at = {};
      int cur = -1, next = -1;
      float trigger_m = 0.0f, post_m = 0.0f;
      const bool tabled = counter_op_wake::HasAreaTable(base);
      if (tabled)
      {
        counter_op_wake::AreasOfJo(base, g.p1c, &cur, &next, &trigger_m);
      }
      const float p[3] = {LoadFloat(base, g.p2c + kPosition), LoadFloat(base, g.p2c + kPosition + 4),
                          LoadFloat(base, g.p2c + kPosition + 8)};
      const int his = tabled ? counter_op_wake::AreaAtPosition(base, p, &post_m) : -1;
      const bool apart = tabled && (his < 0 || (his != cur && his != next));
      // (Only another area counts: a guard far away in her own area is a body the picks may give him; round 18's
      // first test moved him off one he had just been given.)
      const bool behind = apart && dist > REXCVAR_GET(pdz_counterop_cutoff_section_min_m) * 100.0;
      const std::string her_area = tabled && cur >= 0 ? counter_op_wake::AreaName(base, cur) : "";
      const std::string his_area = his >= 0 ? counter_op_wake::AreaName(base, his) : "unknown";
      REXLOG_INFO("pdz_counterop: cut off: after the cutscene: the agent in area {} '{}', player 2 in area {} '{}', {:.1f} m "
                  "from her: {} (pdz_counterop_cutoff_after_cutscene)",
                  cur, her_area, his, his_area, dist / 100.0f, behind ? "LEFT BEHIND: he moves now" : "not behind: nothing to do");
      if (behind)
      {
        CutOffMove(ctx, base, fmt::format("(d) a cutscene moved the agent on: she is in area {} '{}', he in area {} '{}', "
                                          "{:.1f} m from her",
                                          cur, her_area, his, his_area, dist / 100.0f));
        return;
      }
    }
    // Testing: lock the closed door nearest the agent and put him behind it.
    const int32_t test = REXCVAR_GET(pdz_counterop_test_cutoff_door);
    if (test != g_cut.test_last && test != 0 && g.scratch)
    {
      g_cut.test_last = test;
      const uint32_t world = Load32(base, kGame + 24);
      const bool world_ok = Rd(base, world, 0x400);
      const uint32_t end = world_ok ? Load32(base, world + kWorldEnd) : 0;
      uint32_t node = world_ok ? Load32(base, world + kWorldFirst) : 0, best = 0;
      float best_d = 3000.0f;
      for (int i = 0; Rd(base, node, 16) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
      {
        const uint32_t o = Load32(base, node + 4);
        if (Rd(base, o, 0x4A4) && Load32(base, o) == 0x820C27C8u && Load32(base, o + 0x49C) == 1)
        {
          const float d = CutDistance(base, o, g.p1c);
          if (d < best_d)
          {
            best_d = d;
            best = o;
          }
        }
      }
      if (best)
      {
        g_cut.test_door = best;
        g_cut.test_bytes[0] = base[best + 0x4A0];
        g_cut.test_bytes[1] = base[best + 0x4A1];
        g_cut.test_until = now + std::chrono::seconds(120); // round 18: long enough for the picks after the move
        const float d[3] = {LoadFloat(base, best + kPosition), LoadFloat(base, best + kPosition + 4),
                            LoadFloat(base, best + kPosition + 8)};
        float ax = LoadFloat(base, g.p1c + kPosition) - d[0], az = LoadFloat(base, g.p1c + kPosition + 8) - d[2];
        const float len = std::max(1.0f, std::sqrt(ax * ax + az * az));
        ax /= len, az /= len;
        StoreFloat(base, g.scratch, d[0] - ax * 400.0f);
        StoreFloat(base, g.scratch + 4, LoadFloat(base, g.p1c + kPosition + 4));
        StoreFloat(base, g.scratch + 8, d[2] - az * 400.0f);
        Call(ctx, base, sub_825AD148, g.p2c, g.scratch, 0);
        // Unlocked now, locked 0.65 s later (rule (a) looks twice a second), as a door the level locks.
        base[best + 0x4A0] = 0;
        base[best + 0x4A1] = 0;
        g_cut.test_lock_at = now + std::chrono::milliseconds(650);
        REXLOG_INFO("pdz_counterop: cut off: TEST (pdz_counterop_test_cutoff_door {}): the closed door {:08X} at ({:.0f}, "
                    "{:.0f}, {:.0f}), {:.1f} m from the agent, unlocked now and LOCKED in 0.65 s (bytes were {} / {}); player "
                    "2 put 4 m behind it, now at ({:.0f}, {:.0f}, {:.0f})",
                    test, best, d[0], d[1], d[2], best_d / 100.0f, g_cut.test_bytes[0], g_cut.test_bytes[1],
                    LoadFloat(base, g.p2c + kPosition), LoadFloat(base, g.p2c + kPosition + 4),
                    LoadFloat(base, g.p2c + kPosition + 8));
      }
      else
      {
        REXLOG_INFO("pdz_counterop: cut off: TEST (pdz_counterop_test_cutoff_door {}): no closed door within 30 m of the agent", test);
      }
    }
    g_cut.test_last = test;
    if (g_cut.test_door && g_cut.test_lock_at != std::chrono::steady_clock::time_point{} && now >= g_cut.test_lock_at &&
        Rd(base, g_cut.test_door, 0x4A4))
    {
      g_cut.test_lock_at = {};
      base[g_cut.test_door + 0x4A0] = 1;
      base[g_cut.test_door + 0x4A1] = 1;
      REXLOG_INFO("pdz_counterop: cut off: TEST door {:08X}: locked now", g_cut.test_door);
      // Round 18 (item 4): the tool's lock is recorded as the agent's (Trinity's doors open by themselves, so the
      // closed-door judging of a real lock may never see it) and rule (a)'s move is made at once.
      {
        const uint32_t o = g_cut.test_door;
        float nx = LoadFloat(base, g.p1c + kPosition) - LoadFloat(base, o + kPosition);
        float nz = LoadFloat(base, g.p1c + kPosition + 8) - LoadFloat(base, o + kPosition + 8);
        const float len = std::max(1.0f, std::sqrt(nx * nx + nz * nz));
        CutOff::Locked rec;
        rec.door = o, rec.x = LoadFloat(base, o + kPosition), rec.y = LoadFloat(base, o + kPosition + 4);
        rec.z = LoadFloat(base, o + kPosition + 8), rec.nx = nx / len, rec.nz = nz / len;
        std::erase_if(g_cut.locked, [o](const CutOff::Locked &l) { return l.door == o; });
        g_cut.locked.push_back(rec);
        REXLOG_INFO("pdz_counterop: pick: TEST door {:08X} recorded as locked by the agent: no pick takes a guard on its far "
                    "side", o);
        CutOffMove(ctx, base, fmt::format("(a) TEST: the tool's door {:08X} is locked with him on its far side", o));
        return;
      }
    }
    if (g_cut.door && std::chrono::duration<double>(now - g_cut.door_at).count() >= REXCVAR_GET(pdz_counterop_cutoff_door_s))
    {
      const uint32_t o = g_cut.door;
      g_cut.door = 0;
      const bool door_ok = Rd(base, o, 0x4A4) && Load32(base, o) == 0x820C27C8u;
      const bool shut = door_ok && (base[o + 0x4A0] || base[o + 0x4A1]) && Load32(base, o + 0x49C) == 1;
      const float jx = LoadFloat(base, g.p1c + kPosition) - LoadFloat(base, o + kPosition);
      const float jz = LoadFloat(base, g.p1c + kPosition + 8) - LoadFloat(base, o + kPosition + 8);
      const float px = LoadFloat(base, g.p2c + kPosition) - LoadFloat(base, o + kPosition);
      const float pz = LoadFloat(base, g.p2c + kPosition + 8) - LoadFloat(base, o + kPosition + 8);
      if (shut && jx * px + jz * pz < 0.0f && CutDistance(base, o, g.p2c) > 300.0f)
      {
        CutOffMove(ctx, base, fmt::format("(a) the door {:08X} the agent is past is locked and closed with him on its other "
                                          "side ({:.1f} m from her)",
                                          o, dist / 100.0f));
        return;
      }
      REXLOG_INFO("pdz_counterop: cut off: door {:08X}: rule (a) not applied ({})", o,
                  shut ? "he is no longer on its other side" : "it is open or unlocked again");
    }
    // (b) She has entered a new area while he is in another one (not her next), far from her.
    if (counter_op_wake::HasAreaTable(base))
    {
      int cur = -1, next = -1;
      float trigger_m = 0.0f;
      counter_op_wake::AreasOfJo(base, g.p1c, &cur, &next, &trigger_m);
      const float p[3] = {LoadFloat(base, g.p2c + kPosition), LoadFloat(base, g.p2c + kPosition + 4),
                          LoadFloat(base, g.p2c + kPosition + 8)};
      float post_m = 0.0f;
      const int his = counter_op_wake::AreaAtPosition(base, p, &post_m);
      const double min_m = REXCVAR_GET(pdz_counterop_cutoff_section_min_m);
      if (cur != g_cut.last_area)
      {
        const bool first = g_cut.last_area == -2;
        g_cut.last_area = cur;
        const bool apart = his < 0 || (his != cur && his != next);
        if (!first && !(apart && dist > min_m * 100.0))
        {
          REXLOG_INFO("pdz_counterop: cut off: the agent entered area {} '{}'; player 2 is in area {} '{}' (post {:.1f} m), "
                      "{:.1f} m from her: rule (b) not armed ({})",
                      cur, counter_op_wake::AreaName(base, cur), his, his >= 0 ? counter_op_wake::AreaName(base, his) : "unknown",
                      post_m, dist / 100.0f, apart ? "he is close to her" : "he is in her area or her next one");
        }
        if (!first && apart && dist > min_m * 100.0)
        {
          g_cut.section_since = now;
          REXLOG_INFO("pdz_counterop: cut off: the agent entered area {} '{}' while player 2 is in area {} '{}' (post {:.1f} m "
                      "away), {:.1f} m from her: rule (b) armed ({:.0f} s for him to reach her area)",
                      cur, counter_op_wake::AreaName(base, cur), his, counter_op_wake::AreaName(base, his), post_m,
                      dist / 100.0f, REXCVAR_GET(pdz_counterop_cutoff_section_s));
        }
      }
      if (g_cut.section_since != std::chrono::steady_clock::time_point{})
      {
        if ((his >= 0 && his == cur) || dist <= min_m * 100.0)
        {
          REXLOG_INFO("pdz_counterop: cut off: rule (b) dropped: player 2 is {} ({:.1f} m from her)",
                      his == cur ? "in her area now" : "close to her", dist / 100.0f);
          g_cut.section_since = {};
        }
        else if (std::chrono::duration<double>(now - g_cut.section_since).count() >= REXCVAR_GET(pdz_counterop_cutoff_section_s))
        {
          CutOffMove(ctx, base, fmt::format("(b) the agent is in area {} '{}' and he is still in area {} '{}', {:.1f} m from her",
                                            cur, counter_op_wake::AreaName(base, cur), his,
                                            counter_op_wake::AreaName(base, his), dist / 100.0f));
          return;
        }
      }
    }
    // (c) Far from her for a long while without getting closer.
    const double far_cm = REXCVAR_GET(pdz_counterop_cutoff_far_m) * 100.0;
    if (dist > far_cm)
    {
      if (g_cut.far_since == std::chrono::steady_clock::time_point{} || dist < g_cut.far_start - 1500.0f)
      {
        g_cut.far_since = now; // (a new window each time he gains 15 m on her)
        g_cut.far_start = dist;
      }
      if (std::chrono::duration<double>(now - g_cut.far_since).count() >= REXCVAR_GET(pdz_counterop_cutoff_far_s))
      {
        CutOffMove(ctx, base, fmt::format("(c) he has been more than {:.0f} m from the agent for {:.0f} s without getting 15 m "
                                          "closer (now {:.1f} m)",
                                          REXCVAR_GET(pdz_counterop_cutoff_far_m), REXCVAR_GET(pdz_counterop_cutoff_far_s),
                                          dist / 100.0f));
        return;
      }
    }
    else
    {
      g_cut.far_since = {};
    }
  }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    if (!On())
    {
      if (g.started)
      {
        if (CoopLevel(base) && g.out)
        {
          ShowP2(ctx, base); // switched off in the level: player 2 back as Rare's co-op partner
        }
        Reset(base, "pdz_counterop off");
      }
      return;
    }
    if (g_level_over)
    {
      return; // round 8 (item 2): the level is being left; nothing of ours until the next one starts
    }
    const uint32_t so = CoopLevel(base);
    if (!so)
    {
      if (g.started)
      {
        Reset(base, "left the level");
      }
      return;
    }
    uint32_t p1p, p1c, p2p, p2c;
    FindPlayers(base, p1p, p1c, p2p, p2c);
    g_view_base = base; // Sun 22 Nov night (audit): the exported player views test the game state through it
    g_players_missing = !p1c || !p2c; // (a reload with no players yet: the exported views give 0 meanwhile)
    if (!p1c || !p2c)
    {
      return;
    }
    counter_op_pads::Enforce(base, p1p, p2p); // round 3 (item 1): player 2 on the second controller, always
    if (g.started && (so != g.state_object || p2c != g.p2c || p1c != g.p1c))
    {
      // A checkpoint reload or restart (new state object or new characters): start over with the new ones.
      REXLOG_INFO("pdz_counterop: the level was reloaded or a player re-created (state {:08X} -> {:08X}, player 2 {:08X} "
                  "-> {:08X}): re-applying",
                  g.state_object, so, g.p2c, p2c);
      Reset(base, "reload");
    }
    g.state_object = so;
    g.p1p = p1p;
    g.p1c = p1c;
    g.p2p = p2p;
    g.p2c = p2c;
    const bool cutscene = InCutscene(base);
    if (cutscene != g.in_cutscene)
    {
      g.in_cutscene = cutscene;
      if (g.started)
      {
        REXLOG_INFO("pdz_counterop: cutscene {}: player 2 at ({:.0f}, {:.0f}, {:.0f}), health {:.2f}, team bits {:04X}",
                    cutscene ? "starts" : "ends", LoadFloat(base, p2c + kPosition), LoadFloat(base, p2c + kPosition + 4),
                    LoadFloat(base, p2c + kPosition + 8), Health(base, p2c), Load16(base, p2c + kTeamBits));
        if (!cutscene && Ptr(p2c) && !g.out)
        {
          counter_op_pad::Arm(base, p2c, "cutscene ended");
        }
        if (!cutscene && REXCVAR_GET(pdz_counterop_cutoff_after_cutscene))
        {
          g_cut.after_scene_at = std::chrono::steady_clock::now() + std::chrono::seconds(1); // round 18 (item 7)
        }
      }
    }
    if (!g.started)
    {
      // Mon 23 Nov (co19, pdz_counterop_hide_at_once). Log 056 (Bridge): the level load ended at 18:33:19.5, the start
      // (and with it the hide) came at 18:33:22.2: the start waits for the opening cutscene and its 0.75 s tail, and on
      // those frames Rare's co-op player 2 stood in the world at Rare's co-op start position. Now he is hidden and
      // locked on the first tick both players are alive in the level, whatever the cutscene test says; the start
      // itself (the lists, the picks, the first spot) is as before.
      const bool a1 = Alive(ctx, base, p1c), a2 = Alive(ctx, base, p2c);
      {
        static uint32_t s_so = 0, s_p2 = 0;
        static int s_state = -1;
        static std::chrono::steady_clock::time_point s_at{};
        const auto now = std::chrono::steady_clock::now();
        if (so != s_so || p2c != s_p2)
        {
          s_so = so, s_p2 = p2c, s_state = -1, s_at = now;
        }
        const uint32_t player = Load32(base, kGame + 68);
        const uint32_t pstate = Ptr(player) && pdz::guestmem::Ok(base, player, 16) ? Load32(base, player + 12) : 0u;
        const int state = (cutscene ? 1 : 0) | (a1 ? 0 : 2) | (a2 ? 0 : 4) | (pstate == 3 ? 8 : 0);
        if (state != s_state)
        {
          s_state = state;
          REXLOG_INFO("pdz_counterop: start: {:.2f} s after the level's first tick with both players: {} (cutscene test {}"
                      ", Rare's cutscene player state {}; the agent {}, player 2 {}); player 2 at ({:.0f}, {:.0f}, {:.0f}), "
                      "{}",
                      std::chrono::duration<double>(now - s_at).count(),
                      !cutscene && a1 && a2 ? "the start is made now" : "the start waits", cutscene ? "yes" : "no", pstate,
                      a1 ? "alive" : "not alive yet", a2 ? "alive" : "not alive yet", LoadFloat(base, p2c + kPosition),
                      LoadFloat(base, p2c + kPosition + 4), LoadFloat(base, p2c + kPosition + 8),
                      g.out ? "out of the world already" : "IN THE WORLD");
        }
      }
      if (REXCVAR_GET(pdz_counterop_hide_at_once) && !g.out && a1 && a2)
      {
        HideP2(ctx, base, "from the level's first frame, before the start");
      }
      if (!cutscene && a1 && a2)
      {
        Start(ctx, base);
      }
      return;
    }
    if (g.unsupported)
    {
      return;
    }
    // Round 2: the guard side's scan (opening settled, the forced wake, script wake-ups, clone waves) and the count.
    counter_op_guards::Tick(ctx, base, g.p1c, cutscene);
    counter_op_twins::Tick(ctx, base, p1c, p2c, g.current_guard, g.out, cutscene); // round 13 d: Trinity Infiltration's twins
    counter_op_river::Tick(ctx, base, g.mission, p1c, p2c, g.out); // round 13 h: River's hovercraft (seat watch, test tools)
    PdzCounterOpMansionTick(ctx, base, p1c, p2c, g.current_guard, g.out, cutscene); // round 13 g: Mansion's last fight (Mai Hem)
    if (counter_op_mansion::GiveBackNow()) { DatacoreGiveBack(ctx, base, "mansion: Mai Hem's fight"); counter_op_mansion::AfterGiveBack(ctx, base); } // round 13 g: his body goes back to Rare's AI without a death
    PdzCounterOpOutpostTick(ctx, base, p1c, p2c, g.out, cutscene); // round 13 f: Outpost's last fight (the hovercraft's gun)
    PdzCounterOpTurretsTick(ctx, base, p1c, p2c, g.out, cutscene); // round 13b q: player 2 at a stationary turret (watch, test tool)
    PdzCounterOpRooftopsTick(ctx, base, p1c, g.current_guard, g.out, cutscene); // round 13 e: Rooftops (Jack, Killian's dropped guards)
    PdzCounterOpCompanionLog(base); // round 5 (item 5)
    PdzCounterOpHandsLate(ctx, base, g.state_object); // round 5 (item 6)
    PdzCounterOpAnimWatch(base, g.out ? 0 : p2c); // round 5 (item 0): diagnosis
    PdzCounterOpAnimProbe(ctx, base, g.out || cutscene ? 0 : p2c); // TESTING TOOL
    // Round 5 (item 4): Datacore's phase (first half / mid-level cutscene / end area).
    {
      counter_op_guards::DatacoreTestStage(base);
      const auto phase = counter_op_guards::Phase(base);
      if (phase != g_datacore_phase)
      {
        REXLOG_INFO("pdz_counterop: datacore: phase {} -> {} (script stage word {:08X})",
                    counter_op_guards::PhaseName(g_datacore_phase), counter_op_guards::PhaseName(phase),
                    counter_op_guards::DatacoreStageWord(base));
        g_datacore_phase = phase;
      }
      if (phase == counter_op_guards::DatacorePhase::kMidCutscene)
      {
        DatacoreGiveBack(ctx, base, "the mid-level cutscene is flagged");
      }
    }
    counter_op_wake::Step(ctx, base, g.current_guard); // round 4: a taken sleeping guard's group is woken
    PdzCounterOpSpiderTick(ctx, base, p1c); // round 7 (item 3): the spiderbot census and test tools
    // Round 7 (item 3): while he waits for a guard he may drive one of Rare's spiderbots (Datacore's spiderbot
    // section; never in a cutscene or while the agent is in a hacking minigame).
    PdzCounterOpSpiderDrive(ctx, base, p1c, p2c, Ptr(p2p) ? int32_t(Load32(base, p2p + 0x690)) : -1,
                            g.out && g.need_guard && !g.spectating && !cutscene && !g.kill_guard, 0);
    SpiderRideTick(ctx, base); // round 10 (item 3): his view is the bot's
    ParkTick(ctx, base, cutscene);                   // round 12 (item 5): the hidden body waits where the agent stands
    CameraProbeTick(ctx, base, cutscene);            // round 16 (item 1): a wait with no camera object gets one
    // Sat 21 Nov (spectate job): while he waits (or is out for good), Rare's spectator chase camera follows the agent.
    PdzCounterOpRareSpectateTick(ctx, base,
                                 g.started && g.out && (g.need_guard || g.spectating) && !g_ride.on && !g.kill_guard &&
                                     !g.revive_participant && !g.wait_getup && !PdzCounterOpSpiderDriving() &&
                                     Ptr(g.p2c) && !counter_op_jetpack::Riding(base, g.p2c),
                                 cutscene, g.p1c, g.p2c);
    WaitViewTick(ctx, base, cutscene);               // Sat 21 Nov: the waiting view upright, behind and above her
    PdzCounterOpEnvTick(ctx, base, p1c, p2c, g.out, cutscene); // round 12 (item 5): his view's sky / fog follow where he is
    counter_op_levels13::Tick(ctx, base, p2c, g.out); // round 13 i: the plasma rifle's cloak (log, leftovers)
    if (g.place_log > 0)
    {
      g.place_series += fmt::format("{:.0f} ", LoadFloat(base, p2c + kPosition + 4));
      if (--g.place_log == 0)
      {
        REXLOG_INFO("pdz_counterop: spawn (b) height: asked y {:.0f}; player 2's y on the next 40 frames: {}",
                    g.place_given_y, g.place_series);
      }
    }
    if (g.body_log > 0 && Ptr(g.body_log_guard) && Load32(base, g.body_log_guard) == kGuardVtable)
    {
      g.body_series += fmt::format("{:.0f} ", LoadFloat(base, g.body_log_guard + kPosition + 4));
      if (--g.body_log == 0)
      {
        REXLOG_INFO("pdz_counterop: death hand-over height: player 2 died at y {:.0f}; the guard's y on the next 40 "
                    "frames: {}",
                    g.kill_at[1], g.body_series);
      }
    }
    counter_op_jetpack::Tick(ctx, base, p2c);
    CutOffTick(ctx, base, cutscene); // Sun 22 Nov (item 9, pdz_counterop_cutoff)
    if (!g_jet_woken && counter_op_guards::OpeningSettled() && !cutscene)
    {
      // Round 2: the jetpack guards (denied for every other path) are woken at the start too, so they stand by their
      // parked jetpacks until the level's above-sea call mounts them (its wake of an awake guard is a no-op).
      g_jet_woken = true;
      constexpr uint32_t kCharList = 0x82E454A0;
      uint32_t node = Load32(base, kCharList + 0x710);
      const uint32_t end = Load32(base, kCharList + 0x714);
      std::string woke;
      for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
      {
        const uint32_t c = Load32(base, node);
        node = Load32(base, node + 8);
        if (!Ptr(c) || Load32(base, c) != kGuardVtable)
        {
          continue;
        }
        const uint32_t id = SetupIdKey(base, Load32(base, c + kSetup));
        std::string unsafe; // Mon 23 Nov (co21)
        if (id && counter_op_jetpack::Handles(id) && (Load32(base, c + kHidden) & 0x10) && Alive(ctx, base, c) &&
            HandBackSafe(base, c, unsafe))
        {
          Call(ctx, base, sub_825818E0, c, 0);
          woke += (woke.empty() ? "" : ",") + IdChars(id);
        }
      }
      if (!woke.empty())
      {
        REXLOG_INFO("pdz_counterop: jetpack guards woken at the start: {}", woke);
      }
    }
    static std::chrono::steady_clock::time_point s_left_at{};
    if (TakeGuard() && !g.ids.empty() &&
        (g_originals_left == ~0u || std::chrono::steady_clock::now() - s_left_at >= std::chrono::milliseconds(250)))
    {
      s_left_at = std::chrono::steady_clock::now(); // four times a second (Rare's alive test per original)
      // Round 3: the agent's friendlies don't count as guards left (they are never taken).
      g_originals_left = counter_op_guards::OriginalsLeft(
          ctx, base, [base](uint32_t c) { return g.taken.count(c) != 0 || !TakeableSide(base, c); });
    }
    if (g.hide_pending)
    {
      if (!g.need_guard || g.out)
      {
        g.hide_pending = false; // a spot was taken first (or he is already out): nothing to hide
      }
      else if (Ptr(g.p2_mode) || std::chrono::steady_clock::now() - g.hide_pending_since >=
                                         std::chrono::milliseconds(int(kHideWaitSeconds * 1000)))
      {
        g.hide_pending = false;
        REXLOG_INFO("pdz_counterop: start: player 2's camera {} ({:.1f} s after the start)",
                    Ptr(g.p2_mode) ? fmt::format("{:08X} noted: his view follows the agent while out", g.p2_mode)
                                   : std::string("not seen: his view may stay black while out"),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - g.hide_pending_since).count());
        HideP2(ctx, base, "until his first spot");
      }
    }
    // Testing tools (pdz_counterop_test_kill_p2 / _agent): a killing hit through Rare's player damage handler (the
    // original, so pdz_test_god_mode doesn't stop it), so Rare's own death path runs. The counts live outside State,
    // so a reload doesn't repeat them.
    {
      static int32_t kills_done = 0;
      static bool agent_done = false;
      if (REXCVAR_GET(pdz_counterop_test_agent_aim) && !cutscene && !g.spectating && !g.out && Alive(ctx, base, p2c))
      {
        AimAgentAtP2(ctx, base);
      }
      if (REXCVAR_GET(pdz_counterop_test_p2_attack) > 0 && REXCVAR_GET(pdz_counterop_test_seek) && !cutscene &&
          !g.spectating && !g.out && Alive(ctx, base, p2c) && Alive(ctx, base, p1c))
      {
        AimAgentAtP2(ctx, base, true);
      }
      if (REXCVAR_GET(pdz_counterop_test_p2_aim_friend) && !cutscene && !g.spectating && !g.out &&
          Alive(ctx, base, p2c))
      {
        // TESTING TOOL (round 8): his view at the nearest live guard.
        const float px = LoadFloat(base, p2c + kPosition), py = LoadFloat(base, p2c + kPosition + 4),
                    pz = LoadFloat(base, p2c + kPosition + 8);
        uint32_t best = 0;
        float best_d = 2500.0f;
        const uint32_t world = Load32(base, kGame + 24);
        const uint32_t end = Ptr(world) ? Load32(base, world + kWorldEnd) : 0;
        uint32_t node = Ptr(world) ? Load32(base, world + kWorldFirst) : 0;
        for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
        {
          const uint32_t o = Load32(base, node + 4);
          if (!Ptr(o) || Load32(base, o) != kGuardVtable || (Load32(base, o + kHidden) & 0x10) ||
              !(LoadFloat(base, o + kHealth) > 0.0f))
          {
            continue;
          }
          const float d = Dist3(LoadFloat(base, o + kPosition), LoadFloat(base, o + kPosition + 4),
                                LoadFloat(base, o + kPosition + 8), px, py, pz);
          if (d < best_d && d > 80.0f)
          {
            best_d = d;
            best = o;
          }
        }
        static uint32_t told = 0;
        static int64_t told_at = 0;
        const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
        if (best)
        {
          const float dx = LoadFloat(base, best + kPosition) - px, dz = LoadFloat(base, best + kPosition + 8) - pz;
          const float dy = LoadFloat(base, best + kPosition + 4) + 115.0f - (py + 160.0f);
          const float flat = std::sqrt(dx * dx + dz * dz);
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = p2c;
          frame.ctx.f1.f64 = -std::atan2(double(dy), double(flat)); // 825AD3B0's pitch is positive down
          frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
          sub_825AD3B0(frame.ctx, base);
          if (best != told || now_ms - told_at > 2000)
          {
            told = best;
            told_at = now_ms;
            const uint32_t setup = Load32(base, best + kSetup);
            REXLOG_INFO("pdz_counterop: TEST TOOL: player 2's view is on guard '{}' ({:08X}) {:.1f} m away: health "
                        "{:.1f}, alert words {:08X} / {:08X}, stimulus word (+0x1874) {:08X}",
                        IdChars(SetupIdKey(base, setup, nullptr)), best, best_d / 100.0f,
                        LoadFloat(base, best + kHealth), Load32(base, best + 6268), Load32(base, best + 6252),
                        Load32(base, best + 0x1874));
          }
        }
      }
      static std::string warped;
      const std::string warp = REXCVAR_GET(pdz_counterop_test_p2_warp);
      if (!warp.empty() && warp != warped && !cutscene && !g.out && g.scratch && Alive(ctx, base, p2c))
      {
        warped = warp;
        float x = 0, y = 0, z = 0;
        if (std::sscanf(warp.c_str(), "%f,%f,%f", &x, &y, &z) == 3)
        {
          StoreFloat(base, g.scratch, x);
          StoreFloat(base, g.scratch + 4, y);
          StoreFloat(base, g.scratch + 8, z);
          Call(ctx, base, sub_825AD148, p2c, g.scratch, 0);
          REXLOG_INFO("pdz_counterop: test warp: player 2 moved to ({:.0f}, {:.0f}, {:.0f}); now at ({:.0f}, {:.0f}, "
                      "{:.0f})",
                      x, y, z, LoadFloat(base, p2c + kPosition), LoadFloat(base, p2c + kPosition + 4),
                      LoadFloat(base, p2c + kPosition + 8));
        }
      }
      const int32_t want = REXCVAR_GET(pdz_counterop_test_kill_p2);
      if (!cutscene && want > kills_done && !g.spectating && !g.out && !g.revive_participant && !g.wait_getup &&
          Alive(ctx, base, p2c))
      {
        ++kills_done;
        REXLOG_INFO("pdz_counterop: test kill {} (value {}): player 2 health {:.2f}", kills_done, want,
                    Health(base, p2c));
        TestKill(ctx, base, p2c, p1c);
      }
      const bool kill_agent = REXCVAR_GET(pdz_counterop_test_kill_agent);
      if (!cutscene && kill_agent && !agent_done && Alive(ctx, base, p1c))
      {
        agent_done = true;
        REXLOG_INFO("pdz_counterop: test kill of the agent: health {:.2f}", Health(base, p1c));
        TestKill(ctx, base, p1c, p2c);
      }
      if (!kill_agent)
      {
        agent_done = false;
      }
      // Round 6: while the tool is on and she still lives, player 2 (when he is in the world) is put 3 m in front of
      // her every 2 s, facing her, so the fire of pdz_counterop_test_p2_attack ends the game on any level (the
      // back-to-back test needs every game to end).
      static std::chrono::steady_clock::time_point agent_place{};
      if (kill_agent && agent_done && !cutscene && !g.out && !g.spectating && g.scratch && Alive(ctx, base, p1c) &&
          Alive(ctx, base, p2c) && std::chrono::steady_clock::now() - agent_place >= std::chrono::seconds(2))
      {
        agent_place = std::chrono::steady_clock::now();
        float o[3];
        if (OriginOf(ctx, base, p1c, o))
        {
          const float yaw = LoadFloat(base, p1c + kViewYaw) * 3.14159265f / 180.0f;
          StoreFloat(base, g.scratch, o[0] + 300.0f * std::sin(yaw));
          StoreFloat(base, g.scratch + 4, o[1]);
          StoreFloat(base, g.scratch + 8, o[2] + 300.0f * std::cos(yaw));
          Call(ctx, base, sub_825AD148, p2c, g.scratch, 0);
          FaceAgent(ctx, base);
          StoreFloat(base, Load32(base, p1c + kParticipantOf) + kPartMaxHealth, 0.5f);
        }
      }
      // TESTING TOOL (round 6): the agent's single-player companion (setup team byte 1: Jack on Rooftops) is killed
      // through Rare's kill, credited to player 2, to see the level's own "companion died" rule.
      static bool companion_done = false;
      const bool kill_companion = REXCVAR_GET(pdz_counterop_test_kill_companion);
      if (!cutscene && kill_companion && !companion_done)
      {
        companion_done = true;
        uint32_t node = Load32(base, 0x82E454A0 + 0x710);
        const uint32_t end = Load32(base, 0x82E454A0 + 0x714);
        int killed = 0;
        for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
        {
          const uint32_t c = Load32(base, node);
          node = Load32(base, node + 8);
          const uint32_t setup = pdz::guestmem::Ok(base, c, kHealth + 4) ? Load32(base, c + kSetup) : 0; // (Sat 21 Nov evening)
          if (!pdz::guestmem::Ok(base, setup, kSetupTeam + 1) || base[setup + kSetupTeam] != 1 || !(LoadFloat(base, c + kHealth) > 0.0f) ||
              !pdz::guestmem::Ok(base, Load32(base, c) + 0x2E4, 4) || Load32(base, Load32(base, c) + 0x2E4) != 0x8252DE88)
          {
            continue;
          }
          const float before = LoadFloat(base, c + kHealth);
          Call(ctx, base, sub_8252DE88, c, Ptr(p2c) ? p2c : p1c, 5, 1);
          ++killed;
          REXLOG_INFO("pdz_counterop: TEST TOOL: the agent's companion {:08X} '{}' killed through Rare's kill: health "
                      "{:.1f} -> {:.1f}",
                      c, IdChars(SetupIdKey(base, setup)), before, LoadFloat(base, c + kHealth));
        }
        if (!killed)
        {
          REXLOG_INFO("pdz_counterop: TEST TOOL: no living companion of the agent (setup team 1) in this level");
        }
      }
      if (!kill_companion)
      {
        companion_done = false;
      }
    }
    // Round 3 (items 2, 8): his own death. In real play Rare's co-op death left him on the death / spectator camera
    // with no control after the respawn (the get-up never ends on a guard body), and Rare's co-op "partner down"
    // prompts ran meanwhile. He now never enters Rare's death: his health floor (+7828: Rare's player damage handler
    // 825AAF50 never takes a human below it; Datacore gives its humans 1.0) is kept at kOwnFloor, and the hit that
    // brings him down to it ends the life here: out of the world at once (the path of his first spot: no death state,
    // no get-up, Rare's gameplay camera never stops), the replaced guard restored where he stood and killed through
    // Rare's kill (the agent sees a guard die), then the next guard after kNextGuardSeconds through the same ShowP2 as
    // the first spot. A rider keeps Rare's path (his exit from the seat is Rare's rider death).
    if (REXCVAR_GET(pdz_counterop_own_death) && Ptr(p2p) && !g.out && !g.spectating && !g.revive_participant &&
        !g.wait_getup && g.lives > 0 && !g.need_guard)
    {
      if (counter_op_jetpack::Riding(base, p2c))
      {
        if (LoadFloat(base, p2c + kHealthFloor) == kOwnFloor)
        {
          StoreFloat(base, p2c + kHealthFloor, 0.0f);
        }
      }
      else
      {
        if (LoadFloat(base, p2c + kHealthFloor) != kOwnFloor)
        {
          StoreFloat(base, p2c + kHealthFloor, kOwnFloor);
        }
        if (!cutscene && Health(base, p2c) <= kOwnFloor + 0.05f && Load32(base, p2p + kPartDeathState) == 0)
        {
          OwnDeath(ctx, base);
        }
      }
    }
    // Round 3 (item 4): only the guard's weapons. A weapon in his hands whose type this life wasn't given (Rare's
    // co-op loadout handed back by a checkpoint or a revive) is removed with Rare's remove (8254C238).
    if (REXCVAR_GET(pdz_counterop_guard_weapons) && !g.out && !g.spectating && !g.life_types.empty() && !g_giving &&
        !cutscene && !g.revive_participant && !g.wait_getup)
    {
      for (const uint32_t hand : {kHand0, kHand1})
      {
        const uint32_t item = Load32(base, p2c + hand);
        const uint32_t entry = Ptr(item) ? Load32(base, item + kWeaponEntry) : 0;
        const uint32_t def = Ptr(entry) ? Load32(base, entry) : 0;
        const uint32_t type = Ptr(def) ? Load32(base, def + kWeaponDefType) : 0;
        if (!type || type == 15 /* Rare's unarmed item (8254BF38 gives it after a strip) */ ||
            !LevelGivesType(base, type) ||
            std::find(g.life_types.begin(), g.life_types.end(), type) != g.life_types.end())
        {
          continue; // empty, not a listed weapon (bare hands), or his guard's
        }
        if (++g.foreign_removed <= 10)
        {
          REXLOG_INFO("pdz_counterop: weapons: player 2 held weapon type {} (item {:08X}) that his guard didn't carry: "
                      "removed (Rare's 8254C238; #{})",
                      type, item, g.foreign_removed);
        }
        Call(ctx, base, sub_8254C238, p2c, item);
        break; // one a tick
      }
    }
    // Player 2 down: the next spot after the delay (Rare's revive first: it brings him back).
    if (g.revive_participant && std::chrono::steady_clock::now() >= g.revive_at &&
        !counter_op_jetpack::HoldRevive(ctx, base, p2c))
    {
      const uint32_t participant = g.revive_participant;
      g.revive_participant = 0;
      // Rare's revive brings him back alive; then the next spot, or out of the world while picks are retried.
      g.allow_revive = true;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = participant;
        __imp__sub_824573A8(frame.ctx, base);
      }
      g.allow_revive = false;
      g.tries = 0;
      g.diag_lines = 0;
      // Rare's revive is the only un-die (dead flag +64 bit 2, health, hands, animation state), but it also moves
      // him to his revive pad and starts Rare's get-up (animation, invulnerability). co_jungle_3: hidden at once, his
      // update didn't run and the get-up never ended (death state 2 at the 8 s cap). So he stays in the world and the
      // next spot is picked in this same frame: he gets up there (Rare's get-up finishes on his own updates). Only
      // when no spot is free is he hidden while the picks are retried.
      REXLOG_INFO("pdz_counterop: get-up: Rare's revive ran (death state {}, get-up flag {}, timer {:.2f}, "
                  "invulnerable {:.2f}, health {:.1f}, at ({:.0f}, {:.0f}, {:.0f}) = his revive pad); the next spot "
                  "now, where he gets up",
                  int32_t(Load32(base, participant + kPartDeathState)), int(base[p2c + kGetupFlag]),
                  LoadFloat(base, p2c + kGetupTimer), LoadFloat(base, participant + kPartInvulnerable),
                  LoadFloat(base, p2c + kHealth), LoadFloat(base, p2c + kPosition),
                  LoadFloat(base, p2c + kPosition + 4), LoadFloat(base, p2c + kPosition + 8));
      const auto now_revive = std::chrono::steady_clock::now();
      g.need_guard = true;
      g.after_death = true;
      g.need_since = now_revive;
      g.next_try = now_revive; // the retry block below runs in this tick
      g.hide_if_no_spot = true;
      g.kill_ready = g.kill_guard != 0; // the revive moved player 2 away: the replaced guard's kill can start
    }
    if (g.kill_guard && g.kill_ready && !cutscene)
    {
      KillTakenGuardStep(ctx, base);
      if (!g.kill_guard)
      {
        g.kill_ready = false;
      }
    }
    // Rare's get-up over (death state back to 0, flag clear, no invulnerability), or the cap: then the pick.
    if (g.wait_getup)
    {
      const auto now = std::chrono::steady_clock::now();
      const int32_t state = int32_t(Load32(base, p2p + kPartDeathState));
      const uint8_t flag = base[p2c + kGetupFlag];
      const float timer = LoadFloat(base, p2c + kGetupTimer), invulnerable = LoadFloat(base, p2p + kPartInvulnerable);
      const bool over = state == 0 && !flag && !(invulnerable > 0.0f);
      const bool cap = now - g.getup_since >= std::chrono::milliseconds(int(kGetupCapSeconds * 1000));
      if (over || cap)
      {
        if (!over)
        {
          // Rare's own countdowns end it on his next update once both are 0 (8254CD7C: timer <= 0 -> 82457B40; the
          // invulnerability is only ever counted down).
          if (flag)
          {
            StoreFloat(base, p2c + kGetupTimer, 0.0f);
          }
          StoreFloat(base, p2p + kPartInvulnerable, 0.0f);
        }
        REXLOG_INFO("pdz_counterop: get-up: {} after {:.1f} s (death state {}, flag {}, timer {:.2f}, invulnerable "
                    "{:.2f}){}; next spot",
                    over ? "over" : "NOT over at the cap", std::chrono::duration<double>(now - g.getup_since).count(),
                    state, int(flag), timer, invulnerable,
                    over ? "" : ": the get-up timer and the invulnerability set to 0 (Rare's update ends the get-up)");
        g.wait_getup = false;
        g.need_guard = true;
        g.after_death = true;
        g.need_since = now;
        g.next_try = now;
      }
    }
    // The retries: once a second outside cutscenes; after a death, out of guards after kOutOfGuardsSeconds.
    // Round 6: never while the agent is in a hacking minigame (Data Thief, demo kit, Locktopus), on any level: a
    // hack can't be done under fire. His next guard is held until she leaves it (the line stays "Choosing a
    // guard...").
    const bool hacking = REXCVAR_GET(pdz_counterop_hold_in_minigame) &&
                         (PdzMinigameRunning(p1c) || REXCVAR_GET(pdz_counterop_test_minigame));
    if (hacking != g.held_for_hack)
    {
      g.held_for_hack = hacking;
      if (g.need_guard || !hacking)
      {
        REXLOG_INFO("pdz_counterop: pick: the agent {} a hacking minigame{}: player 2's next guard is {}",
                    hacking ? "is in" : "has left",
                    REXCVAR_GET(pdz_counterop_test_minigame) ? " (TEST TOOL pdz_counterop_test_minigame)" : "",
                    hacking ? "held until she leaves it" : "no longer held");
      }
    }
    if (hacking && g.need_guard && !g.hack_hold_logged)
    {
      g.hack_hold_logged = true;
      REXLOG_INFO("pdz_counterop: pick: held: player 2 waits for a guard and the agent is in a hacking minigame: no "
                  "pick is tried until she leaves it");
    }
    if (!hacking)
    {
      g.hack_hold_logged = false;
    }
    if (g.spectating && TakeGuard() && PdzCounterOpRooftopsDropsLeft() > 0) { g.spectating = false; g.need_guard = true; } // round 13 e: out of guards, but Killian's ship can still drop him one
    if (g.spectating && TakeGuard() && counter_op_mansion::FightHolds()) { g.spectating = false; g.need_guard = true; } // round 13 g: out of guards, but Mai Hem's fight has begun
    if (g.need_guard && counter_op_mansion::Over()) // round 16 (item 8b): after Mai Hem nothing restores a body
    {
      g.need_guard = false;
      g.spectating = true;
      if (!g.out)
      {
        HideP2(ctx, base, "mansion: Mai Hem is dead: the last body is gone");
      }
      REXLOG_INFO("pdz_counterop: mansion: Mai Hem is dead: player 2 is out for the rest of the level (no earlier "
                  "guard's body, look or camera comes back), spectating the agent");
    }
    if (g.need_guard && !cutscene && !hacking &&
        g_datacore_phase != counter_op_guards::DatacorePhase::kMidCutscene &&
        std::chrono::steady_clock::now() >= g.next_try)
    {
      const auto now = std::chrono::steady_clock::now();
      g.next_try = now + std::chrono::milliseconds(int(kRetrySeconds * 1000));
      if (TakeGuard() && g.after_death && EligibleLeft() == 0 &&
          !counter_op_levels13::KeepPicking() && // round 13 i: Bridge's end waves may still bring a body
          !counter_op_mansion::FightHolds()) // round 13 g: Mansion's last fight: Mai Hem is his body
      {
        // N64: no eligible guard left = player 2 is out for the rest of the mission (spectating the agent).
        g.need_guard = false;
        g.spectating = true;
        if (!g.out)
        {
          HideP2(ctx, base, "no eligible guard left: spectating the agent");
        }
        REXLOG_INFO("pdz_counterop: no eligible guard left (of {} listed: {} replaced, {} seen dead{}): player 2 is out "
                    "for the rest of the mission, spectating the agent",
                    g.ids.size(), g.taken_ids.size(), g.dead_ids.size(),
                    TestMaxLives() > 0 ? fmt::format("; test cap {} lives", TestMaxLives()) : std::string());
      }
      else if (TryPick(ctx, base, g.after_death ? "after his death" : "first spot"))
      {
        g.need_guard = false;
        g.diag_lines = 0;
        g.hide_if_no_spot = false;
      }
      else if (g.hide_if_no_spot)
      {
        // Revived but no free spot yet: out of sight (spectating) while the picks are retried.
        g.hide_if_no_spot = false;
        if (!g.out)
        {
          HideP2(ctx, base, "revived, no spot free yet: out of sight while the picks are retried");
        }
      }
      else if (g.after_death && !TakeGuard() &&
               now - g.need_since >= std::chrono::milliseconds(int(kOutOfGuardsSeconds * 1000)))
      {
        // (Take mode never gives up by time: while eligible guards are left, e.g. ones the level hasn't built yet,
        // player 2 spectates and the picks go on; EligibleLeft() == 0 ends it above.)
        g.need_guard = false;
        g.spectating = true;
        REXLOG_INFO("pdz_counterop: out of guards ({:.0f} s with no spot left): player 2 stays out of the world "
                    "(spectating); the mission goes on",
                    kOutOfGuardsSeconds);
      }
    }
    // The N64 share stays the most he has: Rare's co-op health regeneration took him back up to a human's full bar
    // (co_jungle_6: 0.5 -> 100.5); it may still refill him up to his share.
    if (g.set_health > 0.0f && !g.out && !g.revive_participant && Alive(ctx, base, p2c) && Ptr(p2p) &&
        Health(base, p2c) > g.set_health + 0.01f)
    {
      StoreFloat(base, p2p + kPartMaxHealth, g.set_health);
      if (++g.health_clamps == 1)
      {
        REXLOG_INFO("pdz_counterop: player 2's health held at his share {:.1f} (Rare's co-op regeneration raises a "
                    "human above it); logged once",
                    g.set_health);
      }
    }
    // Round 2: Datacore keeps its humans alive with a health floor (player +7828 = 1.0; 0 on the other test levels):
    // the agent's hits landed (damage 3.0) but left him at 1.0 (logs co2i / co2k / co2l). He plays a guard, and
    // guards have no floor, so his is cleared while he is in the world.
    if (!REXCVAR_GET(pdz_counterop_own_death) && !g.out && Ptr(p2c) && LoadFloat(base, p2c + kHealthFloor) != 0.0f)
    {
      REXLOG_INFO("pdz_counterop: player 2's health floor +7828 {:.2f} (Rare's, as for the agent) cleared: he can die "
                  "like the guard he plays",
                  LoadFloat(base, p2c + kHealthFloor));
      StoreFloat(base, p2c + kHealthFloor, 0.0f);
    }
    // Taken guards stay hidden.
    if (!g.taken.empty())
    {
      KeepTakenHidden(ctx, base);
    }
    // Health check 5 s after a spawn: what regen or a reset did to the health and max he was given.
    if (g.health_check && std::chrono::steady_clock::now() >= g.health_check_at)
    {
      if (REXCVAR_GET(pdz_counterop_test_view_diff))
      {
        ViewDiff(base, p2c);
      }
      if (const uint32_t player = Load32(base, p2c + 0x19A4); Ptr(player) && !g_fp_mode368)
      {
        g_fp_mode44 = Load32(base, player + 0x44);
        g_fp_mode368 = Load32(base, player + 0x368);
      }
      g.health_check = false;
      const uint32_t participant = Load32(base, p2c + kParticipantOf);
      const float now_health = Health(base, p2c);
      const float now_max = Ptr(participant) ? LoadFloat(base, participant + kPartMaxHealth) : -1.0f;
      REXLOG_INFO("pdz_counterop: 5 s after the spawn: health {:.1f} (set {:.1f}), max {} (set {:.1f}){}; armour {:.1f}; "
                  "body {} head {}; at ({:.0f}, {:.0f}, {:.0f}); death state {}, get-up flag {}, invulnerable {:.2f}{}",
                  now_health, g.set_health, now_max >= 0.0f ? fmt::format("{:.1f}", now_max) : std::string("?"),
                  g.set_health,
                  now_health > g.set_health + 0.5f ? " (regenerated above the share: the max is not the regen cap)" : "",
                  LoadFloat(base, p2c + kArmour), Load32(base, p2c + kBodyId), Load32(base, p2c + kHeadId),
                  LoadFloat(base, p2c + kPosition), LoadFloat(base, p2c + kPosition + 4),
                  LoadFloat(base, p2c + kPosition + 8), int32_t(Load32(base, p2p + kPartDeathState)),
                  int(base[p2c + kGetupFlag]), LoadFloat(base, p2p + kPartInvulnerable),
                  (base[p2c + kGetupFlag] || LoadFloat(base, p2p + kPartInvulnerable) > 0.0f)
                      ? " (WARNING: Rare's get-up started after the spawn: he is invulnerable for a while)"
                      : "");
    }
    // Round 2: a respawn while Rare's co-op get-up is still running left him down (death state 2) for good in the
    // tests (Subway / Nightclub life 2; round 1's Outpost life 2 too): the guard body has no player get-up to finish
    // it. 0.3 s after the spawn: Rare's own countdowns are ended (get-up timer and invulnerability 0, as the get-up cap
    // does); 0.3 s later, if the state is still not 0, it is cleared (he is alive, in the world, on the guards' side).
    // While the state is 2 his view is Rare's death / get-up camera (third person; the gameplay camera 82214C48 isn't
    // updated for him at all). Clearing it lets him act within 0.6 s, but his view stays third person after a respawn
    // (not found yet: pdz_counterop_fix_view_mode's experiments).
    if (g.getup_rescue_at.time_since_epoch().count() && !g.out && !g.revive_participant && !g.wait_getup &&
        Ptr(p2p) && LoadFloat(base, p2c + kHealth) > 0.0f && std::chrono::steady_clock::now() >= g.getup_rescue_at)
    {
      const int32_t state = int32_t(Load32(base, p2p + kPartDeathState));
      if (state == 0)
      {
        if (g.getup_rescue_step)
        {
          REXLOG_INFO("pdz_counterop: get-up rescue: death state back to 0 after step {}", g.getup_rescue_step);
        }
        g.getup_rescue_at = {};
        g.getup_rescue_step = 0;
      }
      else if (g.getup_rescue_step == 0)
      {
        g.getup_rescue_step = 1;
        if (base[p2c + kGetupFlag])
        {
          StoreFloat(base, p2c + kGetupTimer, 0.0f);
        }
        StoreFloat(base, p2p + kPartInvulnerable, 0.0f);
        g.getup_rescue_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        REXLOG_INFO("pdz_counterop: get-up rescue: still in death state {} 0.3 s after the spawn (get-up flag {}): "
                    "Rare's get-up timer and invulnerability set to 0",
                    state, int(base[p2c + kGetupFlag]));
      }
      else
      {
        Store32(base, p2p + kPartDeathState, 0);
        base[p2c + kGetupFlag] = 0;
        base[p2p + kPartDead] = 0; // participant +0x3E0: what the human's dead test (vtable +232, 825A94F0) reads
        REXLOG_INFO("pdz_counterop: get-up rescue: death state {} cleared to 0 (Rare's get-up never ended on the "
                    "guard body); Rare's dead test now says {}",
                    state, objective_bots::CharacterDown(ctx, base, p2c) ? "still down" : "alive");
        const int32_t mode_fix = REXCVAR_GET(pdz_counterop_fix_view_mode);
        if (mode_fix == 4)
        {
          // Experiment 4: out of the world and straight back in, the first spot's path (first person every time).
          HideP2(ctx, base, "view reset after a respawn");
          ShowP2(ctx, base);
          REXLOG_INFO("pdz_counterop: view mode (experiment 4): player 2 hidden and shown again (the first spot's path)");
        }
        else if (mode_fix == 3)
        {
          // Experiment 3: Rare's get-up holds action locks its own end releases (never reached on a guard body);
          // ShowP2's unlock of every bit (8254D968), as after his first spot.
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = p2c;
          frame.ctx.r4.u64 = kLockAll;
          sub_8254D968(frame.ctx, base);
          REXLOG_INFO("pdz_counterop: view mode (experiment 3): Rare's action locks released (8254D968, every bit)");
        }
        else if (const uint32_t player = Load32(base, p2c + 0x19A4); mode_fix > 0 && Ptr(player) && g_fp_mode368)
        {
          const uint32_t m44 = Load32(base, player + 0x44), m368 = Load32(base, player + 0x368);
          Store32(base, player + 0x44, g_fp_mode44);
          if (mode_fix >= 2)
          {
            Store32(base, player + 0x368, g_fp_mode368);
          }
          REXLOG_INFO("pdz_counterop: view mode (experiment {}): player object +0x44 {:08X} -> {:08X}{}", mode_fix, m44,
                      g_fp_mode44,
                      mode_fix >= 2 ? fmt::format(", +0x368 {:08X} -> {:08X}", m368, g_fp_mode368) : std::string());
        }
        g.getup_rescue_at = {};
        g.getup_rescue_step = 0;
      }
    }
    // Spectate camera (log 306): one line 3 s after he went out (outside cutscenes), whether it works.
    if (g.out && !g.spectate_reported && !cutscene &&
        std::chrono::steady_clock::now() - g.out_since >= std::chrono::seconds(3))
    {
      g.spectate_reported = true;
      if (g.spectate_writes)
      {
        REXLOG_INFO("pdz_counterop: spectate: player 2's camera updated {} times in his first 3 s out of the world; "
                    "it follows the agent ({:.1f} m behind, {:.1f} m above his eye, looking where he looks; the agent's "
                    "camera updated {} times){}",
                    g.spectate_writes, kSpectateBack / 100.0f, kSpectateUp / 100.0f, g.p1_cam_updates,
                    REXCVAR_GET(pdz_counterop_fix_out_freeze)
                        ? fmt::format("; pdz_counterop_fix_out_freeze: {} calls with Rare's step, {} leftover step "
                                      "values that were not a step (largest finite {:.3g}), {} angle resets",
                                      g.spectate_calls, g.bad_leftover, g.max_leftover, g.angle_resets)
                        : std::string("; pdz_counterop_fix_out_freeze OFF (the leftover step register is passed on)"));
      }
      else
      {
        REXLOG_WARN("pdz_counterop: spectate: player 2's camera was not updated by Rare's gameplay camera in his first "
                    "3 s out of the world (his updates {}, the agent's {}, agent camera seen {}): his view can't "
                    "follow the agent this way (likely still black)",
                    g.p2_cam_updates, g.p1_cam_updates, g.agent_cam ? "yes" : "no");
      }
    }
    // Player 2's masks: re-applied whenever something reset them (a revive, Rare's betrayal switch, a script).
    if (!g.spectating && !g.out && !g.revive_participant && !g.wait_getup && Alive(ctx, base, p2c) &&
        !TeamApplied(base, p2c))
    {
      if (++g.team_fixes <= 8 || g.team_fixes % 100 == 0)
      {
        REXLOG_INFO("pdz_counterop: player 2's team bits were {:04X} / allies {:04X} / enemies {:04X} (+6276 {:08X}): "
                    "re-applied ({} time(s))",
                    Load16(base, p2c + kTeamBits), Load16(base, p2c + kAllies), Load16(base, p2c + kEnemies),
                    Load32(base, p2c + kBetrayal), g.team_fixes);
      }
      ApplyTeam(base, p2c);
    }
    // Fri 20 Nov night (pdz_counterop_team_check; log 031: the agent's crosshair green over him on Trinity Escape):
    // every 2 s while he plays a body, Rare's own friend test both ways and the agent's auto-aim target filter are
    // asked about him; a line only when one of them treats him as her friend / no target (first 10, then 1 in 50).
    if (REXCVAR_GET(pdz_counterop_team_check) && !g.spectating && !g.out && !g.revive_participant && !g.wait_getup &&
        Ptr(g.p1c) && Alive(ctx, base, p2c))
    {
      static std::chrono::steady_clock::time_point next_check{};
      static uint32_t check_lines = 0;
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_check)
      {
        next_check = now + std::chrono::seconds(2);
        const bool she = (Call(ctx, base, sub_8251EBD0, g.p1c, p2c) & 0xFF) != 0;
        const bool he = (Call(ctx, base, sub_8251EBD0, p2c, g.p1c) & 0xFF) != 0;
        const bool aim = (Call(ctx, base, sub_824EF8B8, g.p1c, p2c) & 0xFF) != 0;
        if ((she || he || !aim) && (++check_lines <= 10 || check_lines % 50 == 0))
        {
          REXLOG_WARN("pdz_counterop: team: check #{}: Rare's friend test: the agent counts player 2 a friend: {}, he "
                      "counts her one: {}; her auto-aim filter takes him: {}; masks: his {:04X} / {:04X} / {:04X}, hers "
                      "{:04X} / {:04X} / {:04X}; owner links (+104 bit 0x20): his {}, hers {}; +6276 {:08X}",
                      check_lines, she ? "YES" : "no", he ? "YES" : "no", aim ? "yes" : "NO",
                      Load16(base, p2c + kTeamBits), Load16(base, p2c + kAllies), Load16(base, p2c + kEnemies),
                      Load16(base, g.p1c + kTeamBits), Load16(base, g.p1c + kAllies), Load16(base, g.p1c + kEnemies),
                      (Load32(base, p2c + 104) & 0x20) ? "set" : "clear",
                      (Load32(base, g.p1c + 104) & 0x20) ? "set" : "clear", Load32(base, p2c + kBetrayal));
          if (!aim)
          {
            REXLOG_WARN("pdz_counterop: team: check #{}: the auto-aim filter's steps for him: +5784 byte {}, +36 {:04X} "
                        "(the viewer's layer bit), targetable flags (82519DA0, +64 {:08X}): {}, friend: {}, same root "
                        "(825195D8): {}, in water (82506C18): {}",
                        check_lines, base[p2c + 5784], Load16(base, p2c + 36), Load32(base, p2c + 64),
                        (Call(ctx, base, sub_82519DA0, p2c) & 0xFF) ? "yes" : "NO", she ? "YES" : "no",
                        (Call(ctx, base, sub_825195D8, p2c, g.p1c) & 0xFF) ? "YES" : "no",
                        (Call(ctx, base, sub_82506C18, p2c) & 0xFF) ? "YES" : "no");
          }
        }
      }
    }
    // Round 4 (item 4): his D-pad Up / Down (put away / drop the weapon) while he plays a guard.
    counter_op_pad::BlockDpad(REXCVAR_GET(pdz_counterop_guard_weapons) && REXCVAR_GET(pdz_counterop_block_dpad) &&
                              !g.out && !g.spectating && !cutscene && !g.need_guard);
    SeekTick(ctx, base, cutscene); // pdz_counterop_test_seek
    PdzCounterOpRules13Tick(ctx, base, !cutscene && !g.out && !g.spectating && Alive(ctx, base, p2c)); // round 13 a
    PromptTourTick(ctx, base, cutscene); // pdz_counterop_test_prompt_tour
    if (REXCVAR_GET(pdz_counterop_test_pos_log))
    {
      static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(10);
      if (std::chrono::steady_clock::now() - last >= std::chrono::milliseconds(500))
      {
        last = std::chrono::steady_clock::now();
        REXLOG_INFO("pdz_counterop: test eyes: agent ({:.1f}, {:.1f}, {:.1f}) player 2 ({:.1f}, {:.1f}, {:.1f})", g_eye[0][0],
                    g_eye[0][1], g_eye[0][2], g_eye[1][0], g_eye[1][1], g_eye[1][2]);
        REXLOG_INFO("pdz_counterop: test positions: the agent (pad {}) at ({:.0f}, {:.0f}, {:.0f}); player 2 (pad {}{}) at "
                    "({:.0f}, {:.0f}, {:.0f})",
                    int32_t(Load32(base, p1p + kPartUser)), LoadFloat(base, p1c + kPosition),
                    LoadFloat(base, p1c + kPosition + 4), LoadFloat(base, p1c + kPosition + 8),
                    int32_t(Load32(base, p2p + kPartUser)), g.out ? ", out of the world" : "",
                    LoadFloat(base, p2c + kPosition), LoadFloat(base, p2c + kPosition + 4),
                    LoadFloat(base, p2c + kPosition + 8));
      }
    }
    // COUNTER-OP GREET: calm guards he passes may greet him (only while he is in the world as a guard).
    if (!g.spectating && !g.out && !g.in_cutscene && !g.revive_participant && !g.wait_getup && Alive(ctx, base, p2c))
    {
      PdzCounterOpGreetTick(ctx, base, p2c);
    }
    // Probe: what player 2 holds (a pick-up shows here; picking up and objectives are not blocked yet).
    const uint32_t weapon = HandWeaponType(base, p2c);
    if (weapon != g.last_weapon)
    {
      if (g.last_weapon != ~0u)
      {
        REXLOG_INFO("pdz_counterop: player 2's weapon in hand: type {} -> {}{}", g.last_weapon, weapon,
                    weapon == 0 ? fmt::format(" (his pad's D-pad Up, Rare's put-away button, was last down {} ms ago; "
                                              "-1 = never)",
                                              counter_op_pad::MsSinceUp())
                                : std::string());
      }
      g.last_weapon = weapon;
    }
    // Round 4 (item 4): he always holds his guard's weapon. Log 053's two minutes with empty hands were Rare's
    // put-away for a human (D-pad Up; Y draws again), reproduced by a script pad (r_wpn_subway). Empty hands within
    // 2 s of a D-pad Up press are drawn again at once through his weapon switcher (the spawn's own draw); empty hands
    // for any other reason (not seen) after 5 s. Not while he is out, riding, in a cutscene, or between lives.
    if (REXCVAR_GET(pdz_counterop_guard_weapons) && !g.out && !g.spectating && !cutscene && !g.need_guard &&
        !g.revive_participant && !g.wait_getup && Ptr(g.life_item) && !counter_op_jetpack::Riding(base, p2c) &&
        !counter_op_river::Driving(base, p2c) && // round 13 h: a hovercraft driver's gun stays put away
        !PdzCounterOpOutpostOnGun() && // round 13 f: Outpost, the hovercraft's gunner keeps his own gun put away
        !PdzCounterOpTurretOnGun(base) && // round 13b q: so does the gunner of a stationary turret
        Alive(ctx, base, p2c))
    {
      const auto now = std::chrono::steady_clock::now();
      if (weapon != 0)
      {
        g.empty_since = {};
        g.redraws_in_a_row = 0;
      }
      else
      {
        if (g.empty_since == std::chrono::steady_clock::time_point{})
        {
          g.empty_since = now;
        }
        const int64_t up_ms = counter_op_pad::MsSinceUp();
        const bool put_away = up_ms >= 0 && up_ms < 2000;
        const auto waited = now - g.empty_since;
        // (2 s: a dropped weapon, Rare's RB / D-pad Down for a human, reproduced in t3_wpn, is replaced quickly)
        if ((put_away && waited >= std::chrono::milliseconds(150)) || waited >= std::chrono::seconds(2))
        {
          // The weapon to draw is looked up in his inventory every time (Rare's inventory walk 824F5C70 over the
          // item categories, as the spawn reads a guard's put-away weapon): a remembered item may be gone (t2_wpn: a
          // button press emptied his hands, the item was freed, and drawing the stale pointer crashed in the
          // switcher 824F83B8). Not in his inventory any more (dropped: D-pad Down, reproduced): given again with
          // Rare's give, as at the spawn.
          std::string given;
          uint32_t draw_item = 0;
          for (uint32_t category = 0; category < 6 && !draw_item; ++category)
          {
            const uint32_t entry = Call(ctx, base, sub_824F5C70, p2c + 0x142C, 0, category);
            const uint32_t item = Ptr(entry) ? Load32(base, entry) : 0;
            const uint32_t wentry = Ptr(item) ? Load32(base, item + kWeaponEntry) : 0;
            const uint32_t def = Ptr(wentry) ? Load32(base, wentry) : 0;
            if (Ptr(def) && Load32(base, def + kWeaponDefType) == g.last_gun)
            {
              draw_item = item;
            }
          }
          if (!draw_item && g.last_gun && LevelGivesType(base, g.last_gun))
          {
            g_giving = true;
            const uint32_t item = Call(ctx, base, sub_8254C378, p2c, g.last_gun, 0, 0, 0, 0);
            g_giving = false;
            if (Ptr(item))
            {
              draw_item = item;
              given = fmt::format("; it was no longer in his inventory (dropped): type {} given again (item {:08X})",
                                  g.last_gun, item);
            }
          }
          g.life_item = draw_item ? draw_item : g.life_item;
          const uint32_t switcher = Load32(base, p2c + 5780);
          PPCFunc *select = SwitcherOk(base, switcher) ? pdz::GuestFunction(Load32(base, Load32(base, switcher)), "counter-op draw")
                                          : nullptr;
          if (!draw_item)
          {
            select = nullptr; // nothing of his to draw (and nothing could be given): never a stale pointer
          }
          if (select)
          {
            rex::CallFrame frame(ctx);
            frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
            frame.ctx.r3.u64 = switcher;
            frame.ctx.r4.u64 = draw_item;
            frame.ctx.r5.u64 = 0;
            frame.ctx.r6.u64 = 0;
            frame.ctx.r7.u64 = 0;
            frame.ctx.r8.u64 = 0;
            frame.ctx.r9.u64 = 0;
            frame.ctx.r10.u64 = 0;
            select(frame.ctx, base);
          }
          if (++g.redraws <= 10 || g.redraws % 50 == 0)
          {
            REXLOG_INFO("pdz_counterop: weapons: player 2's hands were empty ({}): his guard's weapon (item {:08X}) "
                        "{} (#{}){}",
                        put_away ? "put away with D-pad Up" : "for 2 s", g.life_item,
                        select ? "drawn again through his weapon switcher" : "NOT drawn (no weapon of his, or no switcher)", g.redraws,
                        given);
          }
          g.empty_since = now + std::chrono::milliseconds(1500); // the draw takes a moment: no second ask meanwhile
        }
      }
    }
    else
    {
      g.empty_since = {};
    }
  }

  // ---- The look (at level load, in Rare's player factory) -----------------------------------------------------

  // A list entry (body or head) by id, found by walking the level's list (Rare's 82278788 / 822787E0 fall back to
  // body 86 / head 121, or worse, for a missing id).
  uint32_t ListEntry(const uint8_t *base, uint32_t which, uint32_t id)
  {
    const uint32_t lists = Load32(base, kGame + 8);
    const uint32_t list = Ptr(lists) ? Load32(base, lists + which) : 0;
    const int32_t count = Ptr(list) ? int32_t(Load32(base, list + kListCount)) : 0;
    const uint32_t entries = Ptr(list) ? Load32(base, list + kListEntries) : 0;
    for (int32_t i = 0; Ptr(entries) && i < count && i < 512; ++i)
    {
      const uint32_t e = Load32(base, entries + uint32_t(i) * 4);
      if (Ptr(e) && Load32(base, e) == id)
      {
        return e;
      }
    }
    return 0;
  }

  // The player hands 82414070 gives a body for a human (flag 1): its hands name (+16), else Rare's trooper hands;
  // or, when the body is already loaded (+68), what it holds at +20 (0 after a guard's flag-0 load).
  uint32_t LoaderHands(PPCContext &ctx, uint8_t *base, uint32_t entry, bool &loaded)
  {
    loaded = Load32(base, entry + kBodyAssets) != 0;
    if (loaded)
    {
      return Load32(base, entry + kBodyHands);
    }
    return Call(ctx, base, sub_82413FD8, Load32(base, entry + kBodyHandsRef), kHandsFallback, 0, 0);
  }

  // Can the level build a human with this body, without a loader crash? Nothing is requested: Rare's own lookup
  // (82413FD8, null when missing) only. Its list entry, an animation set humans are built with (+64 from the +12
  // name), the model (+4) and the ragdoll (+32, Jack's fallback) with their +8 (82414070 reads both unchecked), and
  // player hands: its own (the loader's way) or else the co-op body's (fill_hands, see Look).
  bool BodyUsable(PPCContext &ctx, uint8_t *base, uint32_t body, uint32_t coop_entry, std::string &why)
  {
    const uint32_t entry = ListEntry(base, kListsBody, body);
    if (!Ptr(entry))
    {
      why = "not in the level's body list";
      return false;
    }
    const uint32_t model_ref = Load32(base, entry + kBodyModelRef), third_ref = Load32(base, entry + kBodyThirdRef);
    if (!Ptr(model_ref) || !Ptr(third_ref))
    {
      why = fmt::format("entry {:08X} lacks a reference (model {:08X}, ragdoll {:08X})", entry, model_ref, third_ref);
      return false;
    }
    const uint32_t anim = Load32(base, entry + kBodyAnimSet);
    const std::string anim_name = GuestString(base, Load32(base, entry + kBodyAnimName));
    const bool coop_anim = Ptr(coop_entry) && anim == Load32(base, coop_entry + kBodyAnimSet);
    bool human_anim = false;
    for (const char *set : kHumanAnimSets)
    {
      human_anim = human_anim || anim_name == set;
    }
    if (!Ptr(anim) || !(coop_anim || human_anim))
    {
      why = fmt::format("entry {:08X}: animation set +64 {:08X} ('{}') is not one Rare builds humans with", entry, anim,
                        anim_name);
      return false;
    }
    const bool loaded = Load32(base, entry + kBodyAssets) != 0;
    uint32_t model = 0, third = 0;
    if (loaded)
    {
      model = Load32(base, entry + kBodyAssets);
      third = Load32(base, entry + kBodyThirdRef + 4); // +36, the loader's ragdoll
    }
    else
    {
      // Rare's constructor will call 82414070(entry, 1): the same lookups first (the model strictly, without the
      // placeholder, so player 2 never wears the placeholder body).
      model = Call(ctx, base, sub_82413FD8, model_ref, 0, 0, 0);
      third = Call(ctx, base, sub_82413FD8, third_ref, kRagdollFallback, 0, 0);
    }
    if (!Ptr(model) || !Ptr(Load32(base, model + 8)))
    {
      why = fmt::format("entry {:08X}: its model '{}' is not in the level's packages (or its +8 is empty)", entry,
                        GuestString(base, model_ref));
      return false;
    }
    if (!Ptr(third) || !Ptr(Load32(base, third + 8)))
    {
      why = fmt::format("entry {:08X}: its ragdoll '{}' (or Jack's fallback) is not in the level's packages, or its "
                        "+8 is empty (the loader reads it unchecked)",
                        entry, GuestString(base, third_ref));
      return false;
    }
    // Player hands: what the entry will hold at +20 once Rare's constructor ran (a loaded entry keeps its +20).
    bool unused = false;
    const uint32_t own = LoaderHands(ctx, base, entry, unused);
    uint32_t hands = own;
    std::string hands_why = Ptr(own) ? fmt::format("its own player hands {:08X}", own) : std::string();
    if (!Ptr(hands))
    {
      // Round 4 (item 3c): the hands pdz_counterop_hands_table names for this body, when resident.
      std::string table_why;
      hands = PdzCounterOpHandsFor(ctx, base, body, table_why);
      if (Ptr(hands))
      {
        hands_why = fmt::format("no player hands of its own: the table's {}, written to +20 {} Rare's call", table_why,
                                loaded ? "before" : "after");
      }
    }
    if (!Ptr(hands))
    {
      // Round 2 (item 10): arms that match the guard body (male / female by its animation set) from the player hands
      // the level ships; the co-op body's (Chandra's / Jo's in most missions) only when none is found.
      std::string arms_why;
      hands = PdzCounterOpViewHands(ctx, base, entry, arms_why);
      if (!Ptr(hands))
      {
        // Round 2: none of those is resident in this level: one more package with hands of the body's sex, loaded by
        // name through Rare's loader (counter_op_hands.cpp; pdz_counterop_load_hands).
        std::string load_why;
        hands = PdzCounterOpLoadHands(ctx, base, entry, load_why);
        arms_why += "; " + load_why;
      }
      if (Ptr(hands))
      {
        hands_why = fmt::format("no player hands of its own (+16 '{}', no trooper hands in this level): {}, written to "
                                "+20 {} Rare's call",
                                GuestString(base, Load32(base, entry + kBodyHandsRef)), arms_why,
                                loaded ? "before" : "after");
      }
      else
      {
        hands_why = arms_why;
        g_look.arms_why = arms_why; // logged with the co-op fallback below
      }
    }
    if (!Ptr(hands) && Ptr(coop_entry))
    {
      bool coop_loaded = false;
      hands = LoaderHands(ctx, base, coop_entry, coop_loaded);
      if (Ptr(hands))
      {
        hands_why = fmt::format("no player hands of its own (+16 '{}', no trooper hands in this level): the co-op "
                                "body's {:08X} ('{}', {}), written to +20 {} Rare's call",
                                GuestString(base, Load32(base, entry + kBodyHandsRef)), hands,
                                GuestString(base, Load32(base, coop_entry + kBodyHandsRef)),
                                coop_loaded ? "its +20" : "looked up", loaded ? "before" : "after");
        if (!g_look.arms_why.empty())
        {
          hands_why += " (guard arms: " + g_look.arms_why + ")";
        }
      }
    }
    if (!Ptr(hands))
    {
      why = fmt::format("entry {:08X}: no player hands: neither its own ('{}'), the trooper hands nor the co-op "
                        "body's are in the level's packages (log 312: null first-person hands)",
                        entry, GuestString(base, Load32(base, entry + kBodyHandsRef)));
      return false;
    }
    if (!Ptr(own))
    {
      g_look.fill_entry = entry;
      g_look.fill_hands = hands;
    }
    g_look.hands_why = hands_why;
    why = fmt::format("entry {:08X} ('{}'): {}, animation '{}'{}, model {:08X}, ragdoll {:08X}; hands: {}", entry,
                      GuestString(base, model_ref), loaded ? "already loaded by Rare" : "loaded by Rare's constructor",
                      anim_name, coop_anim ? " (the co-op body's)" : " (not the co-op body's; a human set)", model,
                      third, hands_why);
    return true;
  }

  // Can the constructor attach this head? Its entry in the head list (822787E0 would give head 121 or 0 otherwise),
  // its model (+12 when loaded, else +4 looked up strictly: 82414618 would fall back to the placeholder head) and the
  // model's +8 (82411048 reads it unchecked).
  bool HeadUsable(PPCContext &ctx, uint8_t *base, uint32_t head, std::string &why)
  {
    const uint32_t entry = ListEntry(base, kListsHead, head);
    if (!Ptr(entry))
    {
      why = "not in the level's head list";
      return false;
    }
    uint32_t model = Load32(base, entry + kHeadAssets);
    const bool loaded = Ptr(model);
    if (!loaded)
    {
      model = Call(ctx, base, sub_82413FD8, Load32(base, entry + kHeadModelRef), 0, 0, 0);
    }
    if (!Ptr(model) || !Ptr(Load32(base, model + 8)))
    {
      why = fmt::format("entry {:08X}: its model '{}' is not in the level's packages (or its +8 is empty)", entry,
                        GuestString(base, Load32(base, entry + kHeadModelRef)));
      return false;
    }
    why = fmt::format("entry {:08X} ('{}'), model {:08X} {}", entry,
                      GuestString(base, Load32(base, entry + kHeadModelRef)), model,
                      loaded ? "already loaded" : "loaded by Rare's constructor");
    return true;
  }

  // The look for player 2 in this mission: the first (body, head) pair of kLook the level can build for a human.
  // Fills g_look; true when a body was chosen (the head then is the pair's, or the co-op head if it fails).
  bool ChooseLook(PPCContext &ctx, uint8_t *base, uint32_t setup)
  {
    g_look = Look{};
    g_look.decided = true;
    std::string raw;
    g_look.mission = pdz::pace::SpLevelName(base, &raw);
    g_look.coop_body = Load32(base, setup + kDescBody);
    g_look.coop_head = Load32(base, setup + kDescHead);
    g_look.head = g_look.coop_head;
    const MissionLook *row = nullptr;
    for (const MissionLook &m : kLook)
    {
      if (g_look.mission == m.mission)
      {
        row = &m;
      }
    }
    if (!row)
    {
      g_look.why = fmt::format("no guard look for '{}' (no placed guards: Datacore / Arena, or not a campaign "
                               "mission): the co-op body {} / head {} kept, nothing requested",
                               g_look.mission, g_look.coop_body, g_look.coop_head);
      return false;
    }
    const uint32_t coop_entry = ListEntry(base, kListsBody, g_look.coop_body);
    PdzCounterOpHandsPreload(ctx, base); // round 4: the mission's hands packages, before any body is judged
    std::string tried;
    for (const LookPair &p : row->pairs)
    {
      if (!p.body)
      {
        break;
      }
      std::string why;
      if (p.body == kPlaceholderBody)
      {
        why = "the placeholder body";
      }
      else if (!BodyUsable(ctx, base, p.body, coop_entry, why))
      {
        g_look.fill_entry = g_look.fill_hands = 0;
      }
      else
      {
        g_look.chosen = true;
        g_look.body = p.body;
        std::string head_why;
        if (HeadUsable(ctx, base, p.head, head_why))
        {
          g_look.head = p.head;
        }
        else
        {
          head_why = fmt::format("the guard head {} can't be attached ({}): the co-op head {} kept", p.head, head_why,
                                 g_look.coop_head);
        }
        g_look.why = fmt::format("guard body {}: {}; head {}: {}{}", p.body, why, g_look.head, head_why,
                                 tried.empty() ? "" : "; rejected first: " + tried);
        return true;
      }
      tried += fmt::format("{}body {}: {}", tried.empty() ? "" : "; ", p.body, why);
    }
    g_look.why = fmt::format("no guard body of {} can be built for a human ({}): the co-op body {} / head {} kept, "
                             "nothing requested",
                             g_look.mission, tried.empty() ? "no candidates" : tried, g_look.coop_body,
                             g_look.coop_head);
    return false;
  }
} // namespace

// minigame_log.cpp's god mode (pdz_test_god_mode, testing): it spares the agent only, not player 2.
bool PdzCounterOpIsPlayer2(uint32_t character)
{
  return character && character == g.p2c && g.started && On();
}

// lab_stealth.cpp (stealth rollout): Counter-Operative runs for co-op games (the switch or the menu choice); the stealth
// layer is then off on every level.
bool PdzCounterOpOn()
{
  return On();
}

// counter_op_menu.cpp's debriefing text: the level that just ended was played as counter-op.
// Round 6 (item 1): a level boundary. Until now everything of a Counter-Op game was only cleared by the NEXT level's
// first tick ("the level was reloaded"), so through the debriefing, the menus and the whole of the next level's load
// the state of the game before was still set: started, the players' and guards' addresses of a level that no longer
// exists, the area table, the Datacore phase, the HUD handles of the waiting / win lines, the script game-type switch.
// Now it is cleared when the level is left for the menus (the first frontend scene) and again before Rare builds the
// next level's participants (82471EE0), whichever game ended how.
// Round 8 (item 2): set when the level is left for the menus, cleared when the next level's participants are built.
// While it is set the tick does nothing at all: rounds 6-7 cleared the state at the first frontend scene and the
// tick then STARTED AGAIN on the level that was still loaded behind the menu (a mission quit from the pause menu:
// player 2 hidden again, the look and probes run again), 2.8 s before Rare's unload crashed.
bool g_level_over = false;
bool g_level_over_from_game = false; // Sun 22 Nov: the menus boundary ended a running Counter-Op game (not a plain one)

void PdzCounterOpLevelBoundary(uint8_t *base, const char *why)
{
  if (!REXCVAR_GET(pdz_counterop_level_boundary))
  {
    return;
  }
  g_level_over = std::string(why).find("menus") != std::string::npos && REXCVAR_GET(pdz_counterop_no_restart_on_exit);
  const bool was_started = g.started;
  g_level_over_from_game = g_level_over && was_started && !g.unsupported;
  const uint32_t lives = g.lives;
  Reset(base, why); // logs 'stopped' when a game was running; clears the guard, wake, jetpack, greet, voice, HUD state
  g_datacore_phase = counter_op_guards::DatacorePhase::kNone;
  g_datacore_calm_victim = 0;
  g_played = g_played && std::string(why).find("menus") != std::string::npos; // the debriefing header still reads it
  g_giving = false;
  g_view_lines = g_view_cleared = 0;
  if (std::string(why).find("starts") != std::string::npos)
  {
    g_filter_seen = g_filter_added = g_filter_removed = g_filter_solo = 0; // the setup filter's counts are per level
  }
  PdzCounterOpViewReset();
  const bool type_fixed = PdzCounterOpTriggersReset(base);
  if (was_started || type_fixed)
  {
    REXLOG_INFO("pdz_counterop: level boundary ({}): the state of the game before is cleared (it was {}, {} lives){}",
                why, was_started ? "still set" : "clear", lives,
                type_fixed ? "; the game type was still switched to single player: put back to co-op" : "");
  }
}

// Sun 22 Nov (pdz_counterop_restart_keeps): Rare's level setup ran (music_restore.cpp's 82200738 hook: every level
// start, a mission restart included). After the menus boundary of a running Counter-Op game this is the pause menu's
// Restart mission (a new mission from the menus also passes 82471EE0, which clears the same flag): the level-over
// hold is lifted so the next tick starts Counter-Op again on the restarted level.
void PdzCounterOpLevelSetup(uint8_t *base)
{
  if (!g_level_over || !REXCVAR_GET(pdz_counterop_restart_keeps))
  {
    return;
  }
  const bool from_game = g_level_over_from_game;
  g_level_over = false;
  g_level_over_from_game = false;
  REXLOG_INFO("pdz_counterop: restart: the game set up a level again after '{}': {} (pdz_counterop_restart_keeps); "
              "Counter-Op {}",
              "back in the menus", from_game ? "the pause menu's Restart mission of a Counter-Op game" : "a level start",
              On() ? "starts again on it at the next tick" : "is not chosen (plain game)");
}

// Round 18 (item 4, pdz_counterop_pick_locked_doors): the picks ask this for every candidate.
bool PdzCounterOpPickLockedOut(const uint8_t *base, uint32_t guard, float x, float z, std::string &why)
{
  if (!REXCVAR_GET(pdz_counterop_pick_locked_doors) || !g.started)
  {
    return false;
  }
  if (const auto it = g_cut.handed_back.find(guard); it != g_cut.handed_back.end())
  {
    const double ago = std::chrono::duration<double>(std::chrono::steady_clock::now() - it->second).count();
    if (ago < REXCVAR_GET(pdz_counterop_handed_back_s))
    {
      why = fmt::format("handed back by a cut-off move {:.0f} s ago", ago);
      ++g_cut.picks_locked_out;
      return true;
    }
  }
  const float reach = float(REXCVAR_GET(pdz_counterop_locked_door_reach_m)) * 100.0f;
  for (const auto &l : g_cut.locked)
  {
    const float dx = x - l.x, dz = z - l.z;
    const float side = dx * l.nx + dz * l.nz; // < 0: the far side from the agent
    if (side < -100.0f && std::sqrt(dx * dx + dz * dz) < reach)
    {
      why = fmt::format("behind the door {:08X} the agent locked ({:.1f} m beyond it)", l.door, -side / 100.0f);
      ++g_cut.picks_locked_out;
      return true;
    }
  }
  return false;
}

bool PdzCounterOpPickPreferNext()
{
  return REXCVAR_GET(pdz_counterop_pick_locked_doors) && std::chrono::steady_clock::now() < g_cut.prefer_next_until;
}

bool PdzCounterOpPlayed()
{
  return g_played && On();
}

// counter_op_menu.cpp: clone missions (Datacore, Arena) are offered (pdz_counterop_clones).
bool PdzCounterOpClonesOn()
{
  return REXCVAR_GET(pdz_counterop_clones);
}

// pdz_counterop_test_seek: what the tick last worked out for each script pad (read on the SDK's input polls).
namespace
{
  std::atomic<int32_t> g_seek_lx[2], g_seek_ly[2], g_seek_rx[2], g_seek_rt[2];
  std::atomic<bool> g_seek_on[2];
  void SeekOff()
  {
    g_seek_on[0] = g_seek_on[1] = false;
  }
  void SeekTick(PPCContext &ctx, uint8_t *base, bool cutscene)
  {
    if (!REXCVAR_GET(pdz_counterop_test_seek) || cutscene || g.out || g.spectating || !Ptr(g.p1c) || !Ptr(g.p2c))
    {
      SeekOff();
      return;
    }
    const bool eyes = g_eye_ok[0] && g_eye_ok[1];
    const float dx = eyes ? g_eye[0][0] - g_eye[1][0] : LoadFloat(base, g.p1c + kPosition) - LoadFloat(base, g.p2c + kPosition);
    const float dy = eyes ? g_eye[0][1] - g_eye[1][1] : LoadFloat(base, g.p1c + kPosition + 4) - LoadFloat(base, g.p2c + kPosition + 4);
    const float dz = eyes ? g_eye[0][2] - g_eye[1][2] : LoadFloat(base, g.p1c + kPosition + 8) - LoadFloat(base, g.p2c + kPosition + 8);
    const float dist = std::sqrt(dx * dx + dz * dz);
    // Player 2: the yaw error to the agent (view yaw +0x1670, degrees; forward = (sin, cos)) on the right stick. The
    // stick's turning sense is found by trying: if the error grows for half a second, the sign flips.
    static int sign = 1;
    static float last_err = 0.0f;
    static auto last_check = std::chrono::steady_clock::now();
    const float want = std::atan2(dx, dz) * 57.29578f;
    const float err = std::remainder(want - LoadFloat(base, g.p2c + kViewYaw), 360.0f);
    const auto now = std::chrono::steady_clock::now();
    if (now - last_check >= std::chrono::milliseconds(500))
    {
      // (The sign was found by trying at first; that flipped wrongly near 180 degrees and left him rocking with
      // his back to her. Measured: a positive right stick turns him towards a positive yaw error. Fixed at +1.)
      sign = 1;
      last_err = err;
      last_check = now;
    }
    const float mag = std::fabs(err) < 3.0f ? 0.0f : std::fabs(err) < 10.0f ? 16000.0f : std::fabs(err) < 30.0f ? 22000.0f : 30000.0f;
    // With his back to her (over 120 degrees off) he keeps turning the way he started until he is within 90: at
    // 180 the error's sign flips every frame and he would rock on the spot.
    static int latched = 0;
    if (std::fabs(err) < 90.0f)
    {
      latched = 0;
    }
    else if (std::fabs(err) > 120.0f && latched == 0)
    {
      latched = err < 0.0f ? -1 : 1;
    }
    g_seek_rx[1] = latched ? int32_t(mag) * latched * sign : int32_t(err < 0.0f ? -mag : mag) * sign;
    g_seek_lx[1] = 0;
    g_seek_ly[1] = dist > 350.0f && std::fabs(err) < 60.0f ? 26000 : 0;
    g_seek_rt[1] = 0;
    // The agent: forward while he is further than 7 m (pdz_counterop_test_agent_aim turns her view at him); her
    // trigger only with a clear line to him inside 30 m, in bursts.
    const bool sees = dist < 1100.0f && std::fabs(dy) < 300.0f && // close: her ammunition is finite (66 rounds)
                      counter_op_guards::AgentSees(ctx, base, g.p1c, LoadFloat(base, g.p2c + kPosition),
                                                   LoadFloat(base, g.p2c + kPosition + 4) - 40.0f,
                                                   LoadFloat(base, g.p2c + kPosition + 8));
    const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    g_seek_lx[0] = 0;
    g_seek_ly[0] = dist > 700.0f ? 22000 : 0;
    g_seek_rx[0] = 0;
    const int32_t p2_attack = REXCVAR_GET(pdz_counterop_test_p2_attack);
    g_seek_rt[0] = sees && (ms % 1200) < 800 && p2_attack != 2 ? 255 : 0;
    if (p2_attack > 0 && sees && dist < 1400.0f)
    {
      g_seek_rt[1] = (ms % 1000) < 700 ? 255 : 0; // his bursts (his view is turned at her by the tick)
    }
    // Stuck against something (walking but not moving for 2 s, or no damage done from close up for 6 s): each pad
    // sidesteps for 1.5 s, the other way each time, as a player would walk round an obstacle.
    {
      struct Stuck
      {
        float x = 0, z = 0;
        std::chrono::steady_clock::time_point since{}, strafe_until{};
        int side = 1;
      };
      static Stuck stuck[2];
      const uint32_t who[2] = {g.p1c, g.p2c};
      for (int k = 0; k < 2; ++k)
      {
        Stuck &s = stuck[k];
        const float x = LoadFloat(base, who[k] + kPosition), z = LoadFloat(base, who[k] + kPosition + 8);
        const bool moved = std::fabs(x - s.x) + std::fabs(z - s.z) > 60.0f;
        const bool close_no_kill = false;
        if (moved || s.since == std::chrono::steady_clock::time_point{})
        {
          s.x = x;
          s.z = z;
          s.since = now;
        }
        const auto limit = std::chrono::milliseconds(close_no_kill ? 6000 : 2000);
        if (now >= s.strafe_until && now - s.since >= limit && (g_seek_ly[k].load() != 0 || close_no_kill))
        {
          // The same way every time (player 2 to his right, the agent to her left), for 4 s: sliding along the
          // obstacle while still facing the other, he gets round it instead of rocking in front of it.
          s.side = k == 0 ? -1 : 1;
          s.strafe_until = now + std::chrono::milliseconds(4000);
          s.since = now + std::chrono::milliseconds(4000);
        }
        if (now < s.strafe_until)
        {
          g_seek_lx[k] = 26000 * s.side;
          g_seek_ly[k] = 9000;
        }
      }
    }
    // Close to her (inside 8 m) player 2 circles her: his left stick sideways, the other way every 5 s, while his
    // right stick keeps him facing her. A pillar or a car between them then never lasts.
    if (dist < 800.0f)
    {
      const int64_t phase = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() / 5;
      g_seek_lx[1] = (phase & 1) ? 26000 : -26000;
      g_seek_ly[1] = dist > 450.0f ? 14000 : 0;
      g_seek_lx[0] = 0;
      g_seek_ly[0] = 0;
    }
    // Her ammunition: god mode doesn't refill it and a test fires far more than a player (through pillars too: the
    // line-of-sight test above lets some props through). Every 5 s the reserve of the gun in her hand is topped up
    // through Rare's own adder (82583F80, clamped to the weapon's maximum). Her shots and hits stay the game's.
    {
      static auto last_ammo = std::chrono::steady_clock::now();
      if (now - last_ammo >= std::chrono::seconds(5))
      {
        last_ammo = now;
        for (const uint32_t hand : {kHand0, kHand1})
        {
          const uint32_t item = Load32(base, g.p1c + hand);
          const uint32_t entry = Ptr(item) ? Load32(base, item + kWeaponEntry) : 0;
          if (Ptr(entry) && Ptr(Load32(base, entry + kWeaponEntryDef)))
          {
            Call(ctx, base, sub_82583F80, item, 120, 1);
          }
        }
      }
    }
    g_seek_on[0] = g_seek_on[1] = true;
    static auto last_line = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    if (now - last_line >= std::chrono::seconds(4))
    {
      last_line = now;
      REXLOG_INFO("pdz_counterop: test seek: {:.1f} m apart (height {:+.1f} m); player 2 yaw error {:+.0f} deg, turn sign "
                  "{}, pad 2 right stick {} left stick {}; the agent {} him, pad 1 left stick {}, trigger {}",
                  dist / 100.0f, dy / 100.0f, err, sign, g_seek_rx[1].load(), g_seek_ly[1].load(),
                  sees ? "has a clear line to" : "can't see", g_seek_ly[0].load(), g_seek_rt[0].load());
    }
  }
} // namespace
// test_harness.cpp (ScriptedPadState): the seek's sticks / trigger for script pad 0 / 1; false = nothing to add.
bool PdzCounterOpTestSeek(uint32_t pad, int16_t sticks[4], uint8_t &rt)
{
  if (pad > 1 || !g_seek_on[pad].load(std::memory_order_relaxed))
  {
    return false;
  }
  sticks[0] = int16_t(g_seek_lx[pad].load());
  sticks[1] = int16_t(g_seek_ly[pad].load());
  sticks[2] = int16_t(g_seek_rx[pad].load());
  sticks[3] = 0;
  rt = uint8_t(g_seek_rt[pad].load());
  return true;
}

// Round 3: a supported counter-op game is running in this level (counter_op_triggers.cpp, counter_op_hud.cpp).
bool PdzCounterOpRunning()
{
  return g.started && !g.unsupported && On() && !g_level_over; // Sun 22 Nov night (audit): never past the menus boundary
}
bool PdzCounterOpStarted() { return g.started; } // counter_op_menu.cpp (round 6): a game's state is still set
// (PdzCounterOpOn, which counter_op_pads.cpp reads before the level start, is defined above for lab_stealth.cpp too.)
// Sun 22 Nov night (audit): the two players are handed to other files' hooks only while the tick found them (not while
// a reload has no players yet) and the game state is still this level's (after a level ends the tick stops running,
// so its own reset waits for the boundary).
namespace
{
  bool ViewsValid()
  {
    if (g_players_missing || !g_view_base || !Ptr(g.state_object))
    {
      return false;
    }
    return Load32(g_view_base, kGame) == g.state_object && Load32(g_view_base, g.state_object + 8) == kStateInLevel;
  }
} // namespace
uint32_t PdzCounterOpAgent() { return PdzCounterOpRunning() && ViewsValid() ? g.p1c : 0; }
uint32_t PdzCounterOpPlayer2() { return PdzCounterOpRunning() && ViewsValid() ? g.p2c : 0; }

// Round 5 (item 4): the 82303588 / 82304188 hooks (counter_op_guards.cpp).
bool PdzCounterOpDatacoreCalm(uint32_t victim) { return victim != 0 && victim == g_datacore_calm_victim; }
void PdzCounterOpDatacoreBeforeDelete(PPCContext &ctx, uint8_t *base, uint32_t spawner)
{
  if (g.started && !g.unsupported && counter_op_guards::IsFirstHalfSpawner(base, spawner))
  {
    DatacoreGiveBack(ctx, base, "Rare's delete of the first-half guards (82304188)");
  }
}

// Round 13 f (counter_op_outpost.cpp; Outpost's last fight): player 2 gives up his body without a death, as
// DatacoreGiveBack does for Datacore: he is taken out of the world, the guard he replaced is restored alive where it
// stands (not killed, not kept hidden), and the normal pick runs again. Also ends "out for the rest of the mission".
// Nothing is touched while one of his deaths is still being worked off (the pick follows it anyway).
void PdzCounterOpReleaseBody(PPCContext &ctx, uint8_t *base, const char *why, bool at_once)
{
  if (!g.started || g.unsupported || !Ptr(g.p2c))
  {
    return;
  }
  if (g.revive_participant || g.wait_getup || g.kill_guard)
  {
    REXLOG_INFO("pdz_counterop: release ({}): one of his deaths is still being worked off: nothing to give up, the "
                "pick follows it",
                why);
    return;
  }
  const uint32_t guard = g.current_guard;
  std::string unsafe; // Mon 23 Nov (co21): a guard the level tore down meanwhile is not handed back
  const bool safe = Ptr(guard) && HandBackSafe(base, guard, unsafe);
  const bool exists = safe && counter_op_guards::StillExists(base, guard);
  const bool hidden = exists && (Load32(base, guard + kHidden) & 0x10) != 0;
  const bool was_out = g.out, was_spectating = g.spectating;
  if (!g.out)
  {
    HideP2(ctx, base, why);
  }
  if (Ptr(guard))
  {
    g.taken.erase(guard); // before the restore: the keep-hidden pass must not hide it again
    g.current_guard = 0;
    if (hidden)
    {
      Call(ctx, base, sub_825818E0, guard, 0); // Rare's restore (world, physics, team lists): alive, in its slot
    }
  }
  const auto now = std::chrono::steady_clock::now();
  g.set_health = 0.0f;
  g.health_check = false;
  g.getup_rescue_at = {};
  g.spectating = false;
  g.need_guard = true;
  g.after_death = false;
  g.need_since = now;
  g.next_try = now + std::chrono::milliseconds(int(std::max(kNextGuardSeconds, PdzCounterOpTestFirstPickS()) * 1000)); // (Sat 21 Nov: the test wait too)
  if (at_once)
  {
    g.next_try = now; // round 18 (item 8): the pick runs on the next tick (Outpost's boss, as Mansion's since round 16)
  }
  g.hide_if_no_spot = false;
  g.tries = 0;
  g.diag_lines = 0;
  REXLOG_INFO("pdz_counterop: release ({}): player 2 {} (no death, life {} not counted as a death){}; replaced guard "
              "{:08X}: {}; the pick runs again in {:.1f} s",
              why, was_out ? "was already out of the world" : "taken out of the world", g.lives,
              was_spectating ? "; he was out for the rest of the mission: no longer" : "", guard,
              !Ptr(guard)  ? std::string("none")
              : !safe      ? "NOT handed back (" + unsafe + "): it stays out of the world (pdz_counterop_safe_handback)"
              : !exists    ? "no longer in Rare's lists (deleted by the level)"
              : hidden     ? "restored alive (Rare's 825818E0(g, 0)), not killed"
                           : "was already in the world",
              at_once ? 0.0 : kNextGuardSeconds);
}

// Round 5 (item 5): a co-op game that will be (or is) a Counter-Op game: the level scripts run as in single player.
// Used before Start() too (the script's Setup runs before both players are alive).
bool PdzCounterOpSoloLevel(const uint8_t *base)
{
  if (!On() || (g.started && g.unsupported))
  {
    return false;
  }
  const uint32_t so = Load32(base, kGame);
  return Ptr(so) && (Load32(base, so + 40) == 1 || PdzCounterOpScriptTypeFlip());
}

// Stage 1: Rare's setup filter (sub_82279928, hooked in cut_scenarios.cpp) with the counter-op kind bit. rare: Rare's
// answer (co-op kind 0x20). In a co-op game with pdz_counterop: the mask must hold the 2-player bit and 0x40.
bool PdzCounterOpFilter(const uint8_t *base, uint32_t setup, bool rare)
{
  if (!On() || !Ptr(setup))
  {
    return rare;
  }
  const uint32_t so = Load32(base, kGame);
  if (!Ptr(so) || Load32(base, so + 40) != 1)
  {
    return rare;
  }
  if (!g.started && !g_filter_seen)
  {
    g_filter_added = g_filter_removed = g_filter_solo = 0;
  }
  const uint32_t mask = Load32(base, kFilterTable + uint32_t(base[setup + kSetupFilter]) * 8 + 4);
  // Round 5 (item 5): the level is built as in SINGLE PLAYER. Rare's "solo only" setups (mask 0x11: Jack on Rooftops,
  // the CI squad of Outpost, Laboratory's ladder and lift switch, Temple's door) were left out by the counter-op test
  // (2-player bit and kind 0x40), so the solo companion co-op hands to player 2 was in nobody's hands. Now a setup
  // spawns when Rare's counter-op set OR its solo set (1-player bit and kind 0x10) holds it; "co-op only" stays out.
  const bool rare_counter = (mask & 0x2u) && (mask & 0x40u);
  const bool solo = (mask & 0x1u) && (mask & 0x10u);
  const bool counter = rare_counter || (solo && REXCVAR_GET(pdz_counterop_solo_setups));
  g_filter_solo += counter && !rare_counter;
  ++g_filter_seen;
  g_filter_added += counter && !rare;
  g_filter_removed += !counter && rare;
  return counter;
}

// sub_825B3D58(setup): Rare's SP player factory. Player 2's call (player 1 exists with no next human, +6792: the
// only case Rare builds in co-op; a co-op game, pdz_counterop and pdz_counterop_guard_look on): the chosen guard
// body / head ids are written into his setup for the duration of Rare's call and restored, so Rare's own constructor
// builds him with them; the body's player hands (+20) are filled from the co-op body's when the guard body has none.
REX_HOOK_RAW(sub_825B3D58)
{
  const uint32_t setup = ctx.r3.u32;
  const uint32_t so = Load32(base, kGame);
  const uint32_t one = Load32(base, kPlayerOne);
  // Player 2's call: a co-op game, player 1 built, and not player 1's own setup (a rebuilt player 1 keeps her look).
  if (!On() || !REXCVAR_GET(pdz_counterop_guard_look) || !Ptr(setup) || !Ptr(so) || Load32(base, so + 40) != 1 ||
      !Ptr(one) || Load32(base, one + kSetup) == setup || Load32(base, one + kP1Partner) != 0)
  {
    __imp__sub_825B3D58(ctx, base);
    return;
  }
  bool chosen = false;
  {
    PPCContext saved = ctx;
    PdzCounterOpHandsProbe(saved, base, "player 2 factory");
    chosen = ChooseLook(saved, base, setup);
  }
  if (!chosen)
  {
    REXLOG_INFO("pdz_counterop: look: player 2 keeps the co-op look in {}: {}", g_look.mission, g_look.why);
    __imp__sub_825B3D58(ctx, base);
    return;
  }
  const uint32_t old_body = Load32(base, setup + kDescBody), old_head = Load32(base, setup + kDescHead);
  if (Ptr(g_look.fill_entry) && Ptr(g_look.fill_hands) && Load32(base, g_look.fill_entry + kBodyAssets) &&
      !Ptr(Load32(base, g_look.fill_entry + kBodyHands)))
  {
    // D15: log 312's cause (see Look::fill_hands). Rare's loader would store exactly this with flag 1.
    PdzCounterOpHandsLend(ctx, base, g_look.fill_entry, g_look.fill_hands); // round 8: a loan
    REXLOG_INFO("pdz_counterop: look: hands: body {}'s list entry {:08X} is loaded without player hands; {:08X} "
                "written to +20 before Rare's player factory ({})",
                g_look.body, g_look.fill_entry, g_look.fill_hands, g_look.hands_why);
  }
  Store32(base, setup + kDescBody, g_look.body);
  Store32(base, setup + kDescHead, g_look.head);
  REXLOG_INFO("pdz_counterop: look: player 2 is built with body {} in {}: setup {:08X} body {} -> {}, head {} -> {} for "
              "Rare's player factory ({})",
              g_look.body, g_look.mission, setup, old_body, g_look.body, old_head, g_look.head, g_look.why);
  __imp__sub_825B3D58(ctx, base);
  Store32(base, setup + kDescBody, old_body);
  Store32(base, setup + kDescHead, old_head);
  const uint32_t human = ctx.r3.u32;
  if (Ptr(g_look.fill_entry) && Ptr(g_look.fill_hands) && !Ptr(Load32(base, g_look.fill_entry + kBodyHands)))
  {
    // The body was not loaded before the call: the constructor's 82414070(entry, 1) found no hands for it and stored
    // 0. The first-person hands are made from +20 later, when his participant attaches (825A99D8 -> 825B43E0).
    PdzCounterOpHandsLend(ctx, base, g_look.fill_entry, g_look.fill_hands); // round 8: a loan
    REXLOG_INFO("pdz_counterop: look: hands: body {}'s list entry {:08X} got no player hands from Rare's loader; "
                "{:08X} written to +20 after the factory, before his participant attaches ({})",
                g_look.body, g_look.fill_entry, g_look.fill_hands, g_look.hands_why);
  }
  const uint32_t built_entry = Ptr(human) ? Load32(base, human + kBodyEntryOf) : 0;
  REXLOG_INFO("pdz_counterop: look: Rare's factory returned {:08X}{}; the setup's ids restored", human,
              Ptr(human) ? fmt::format(" (body {}, head {}, body instance {:08X}, head instance {:08X}, body entry "
                                       "{:08X} hands +20 {:08X}, hair bones {} / {})",
                                       Load32(base, human + kBodyId), Load32(base, human + kHeadId),
                                       Load32(base, human + 72), Load32(base, human + 4104), built_entry,
                                       Ptr(built_entry) ? Load32(base, built_entry + kBodyHands) : 0,
                                       int32_t(Load32(base, human + 4852)), int32_t(Load32(base, human + 4856)))
                         : std::string(" (none)"));
  if (Ptr(human) && (Load32(base, human + kBodyId) != g_look.body || Load32(base, human + kHeadId) != g_look.head ||
                     !Ptr(Load32(base, human + 72)) || !Ptr(Load32(base, human + 4104))))
  {
    REXLOG_WARN("pdz_counterop: look: player 2 is not built as asked (body {} / head {} asked; body {} / head {}, "
                "instances {:08X} / {:08X})",
                g_look.body, g_look.head, Load32(base, human + kBodyId), Load32(base, human + kHeadId),
                Load32(base, human + 72), Load32(base, human + 4104));
  }
}

// Sun 22 Nov (item 7): the wait's HUD state (the crosshair hooks read it; counter_op_hud.cpp's put-back check too).
std::atomic<uint32_t> g_wait_hud_p2{0};
uint64_t g_wait_crosshair_skips = 0, g_wait_mark_clears = 0;
bool PdzCounterOpWaitHudHidden() { return g_wait_hud_p2.load(std::memory_order_relaxed) != 0; }
namespace
{
  bool g_wait_hud_was = false;
  void WaitHudTick(uint8_t *base, bool on)
  {
    if (on != g_wait_hud_was)
    {
      g_wait_hud_was = on;
      REXLOG_INFO("pdz_counterop: HUD: player 2 {} (pdz_counterop_wait_hud_hidden): his weapon HUD, his crosshair and "
                  "his damage markers {}; so far {} crosshair draw(s) skipped, {} damage marker(s) cleared",
                  on ? "waits" : "has a body again", on ? "hidden" : "back as Rare's", g_wait_crosshair_skips,
                  g_wait_mark_clears);
    }
    g_wait_hud_p2.store(on ? g.p2c : 0, std::memory_order_relaxed);
    if (!on)
    {
      return;
    }
    // Rare's damage direction markers (human +0x19BC, four at +0xA38) and the health bar's flash (+0x19B4: bar off).
    if (!Rd(base, g.p2c, 0x19C0))
    {
      return;
    }
    const uint32_t marks = Load32(base, g.p2c + 0x19BC);
    if (Rd(base, marks + 0xA38, 16))
    {
      for (uint32_t i = 0; i < 4; ++i)
      {
        if (Load32(base, marks + 0xA38 + 4 * i) != 0)
        {
          ++g_wait_mark_clears;
          Store32(base, marks + 0xA38 + 4 * i, 0);
        }
      }
    }
    const uint32_t fx = Load32(base, g.p2c + 0x19B4);
    if (Rd(base, fx, 8) && base[fx])
    {
      base[fx] = 0;
      StoreFloat(base, fx + 4, 0.0f);
    }
  }

  // His gun's crosshair: the crosshair of the weapon in either of his hands (weapon +0x4FC).
  bool WaitCrosshair(const uint8_t *base, uint32_t crosshair)
  {
    // Round 18 (log 046's crash): only the player 2 waiting now (the tick's own, while it runs in a level; cleared at
    // every reset), and every read tested.
    const uint32_t p2 = g_wait_hud_p2.load(std::memory_order_relaxed);
    if (!p2 || !Ptr(crosshair) || p2 != g.p2c || !g.out || !g.started || g_level_over || !Rd(base, p2 + 4076, 8))
    {
      return false;
    }
    for (const uint32_t hand : {4076u, 4080u})
    {
      const uint32_t w = Load32(base, p2 + hand);
      if (Rd(base, w + 0x4FC, 4) && Load32(base, w + 0x4FC) == crosshair)
      {
        if (++g_wait_crosshair_skips <= 3 || g_wait_crosshair_skips % 3600 == 0)
        {
          REXLOG_INFO("pdz_counterop: HUD: player 2's crosshair {:08X} (his gun {:08X}) not drawn while he waits (#{})",
                      crosshair, w, g_wait_crosshair_skips);
        }
        return true;
      }
    }
    return false;
  }
} // namespace

REX_HOOK_RAW(sub_82263F08)
{
  if (WaitCrosshair(base, ctx.r3.u32))
  {
    return;
  }
  __imp__sub_82263F08(ctx, base);
}
REX_HOOK_RAW(sub_82264670)
{
  if (WaitCrosshair(base, ctx.r3.u32))
  {
    return;
  }
  __imp__sub_82264670(ctx, base);
}

// sub_8222F740: the co-op death states, every frame in a level. Rare's first, then the counter-op tick.
REX_HOOK_RAW(sub_8222F740)
{
  __imp__sub_8222F740(ctx, base);
  {
    // Sun 22 Nov (round 17): the jetpack destroy / watch tools (single player too; nothing when both are off).
    uint32_t p1p, p1c, p2p, p2c;
    FindPlayers(base, p1p, p1c, p2p, p2c);
    if (p1c)
    {
      PPCContext saved = ctx;
      counter_op_jetpack::TestTools(saved, base, p1c, p2c);
      if (!PdzCounterOpRunning())
      {
        PPCContext saved2 = ctx;
        PdzCounterOpTestLevelMessage(saved2, base, p1c); // (single player; in Counter-Op the spider tick sends it)
      }
    }
  }
  if (On())
  {
    PPCContext saved = ctx;
    Tick(saved, base);
  }
  else if (g.started)
  {
    PPCContext saved = ctx;
    Tick(saved, base); // puts player 2 back and resets
  }
  // Round 2 (item 4): player 2's half of the screen is dark while he is out of the world (never in a cutscene: Rare
  // plays those full screen); 'No guards left' once he is out for the rest of the mission.
  // Round 3: after 5 tries that found nothing (no guard hostile to the agent is in play yet), the line says so.
  const bool none_yet = g.need_guard && g.tries >= 5;
  // Round 6 (item 8): only once he is in play and waiting: never before the opening is settled (the level's first
  // frames, where the loading screen is still up and the opening cutscene has not started yet), never in a cutscene.
  const bool in_play = !REXCVAR_GET(pdz_counterop_cutscene_tail) || counter_op_guards::OpeningSettled();
  const int view = On() && g.started && !g.unsupported && g.out && !g.in_cutscene && in_play &&
                           !PdzCounterOpSpiderDriving()
                       ? (g.spectating ? 2 : none_yet ? (counter_op_guards::DatacoreOut() ? 4 : 3) : 1) // (4: round 14)
                       : 0;
  // Round 5 (item 2): at the end of the mission player 2's half says how it ended for him (Rare's text); the waiting
  // panel stays off meanwhile (it would cover the line).
  bool win_line = false;
  {
    PPCContext saved = ctx;
    win_line = PdzCounterOpWinScreens(saved, base, On() && g.started && !g.unsupported,
                                      Ptr(g.p2p) ? int32_t(Load32(base, g.p2p + 0x690)) : -1);
  }
  PdzCounterOpViewSet(win_line ? 0 : view);
  {
    // Round 4 (item 7): the dark panel and the line in his own HUD (Rare's text); the overlay only when that fails.
    PPCContext saved = ctx;
    const int32_t p2_pad = Ptr(g.p2p) ? int32_t(Load32(base, g.p2p + 0x690)) : -1;
    const bool spider_hud = On() && g.started && !g.unsupported && PdzCounterOpSpiderDriving();
    // Sun 22 Nov (item 7, pdz_counterop_wait_hud_hidden): while he waits, his weapon HUD is hidden as for a bot drive
    // (the same six elements, the same put-back at his next body), his crosshair is not drawn and his damage markers
    // are cleared every frame.
    const bool wait_hud = !spider_hud && REXCVAR_GET(pdz_counterop_wait_hud_hidden) && On() && g.started &&
                          !g.unsupported && g.out && Ptr(g.p2c);
    WaitHudTick(base, wait_hud);
    if (!spider_hud && !wait_hud)
    {
      PdzCounterOpSpiderHud(saved, base, false, p2_pad);
    }
    PdzCounterOpRareDark(saved, base, win_line ? 0 : view, p2_pad);
    if (spider_hud || wait_hud)
    {
      PdzCounterOpSpiderHud(saved, base, true, p2_pad);
    }
  }
}

// sub_8264EEF8: Rare's "MISSION FAILED" notification to every human participant (round 6, item 4). Rare's first;
// then player 2's own line takes its place on his half at once.
REX_EXTERN(__imp__sub_8264EEF8);
REX_HOOK_RAW(sub_8264EEF8)
{
  __imp__sub_8264EEF8(ctx, base);
  if (On() && g.started && !g.unsupported && Ptr(g.p2p))
  {
    PPCContext saved = ctx;
    PdzCounterOpWinNow(saved, base, int32_t(Load32(base, g.p2p + 0x690)), 1);
  }
}

// sub_8222FD98: co-op "game over?" (every human down). Counter-op: the agent down is enough.
REX_HOOK_RAW(sub_8222FD98)
{
  __imp__sub_8222FD98(ctx, base);
  if (!On() || !g.started || g.unsupported || (ctx.r3.u32 & 0xFF) || !Ptr(g.p1p) ||
      Load32(base, g.p1p + kPartDeathState) != 2)
  {
    return;
  }
  const uint8_t state_before = base[kLevelState];
  base[kLevelState] |= 2;
  PPCContext saved = ctx;
  Call(saved, base, sub_8222FF58, 0); // Rare's own all-down path calls it every frame too (8222FF20 from 8222FB48)
  if (!g.game_over_logged)
  {
    g.game_over_logged = true;
    REXLOG_INFO("pdz_counterop: the agent is down: mission failed (Rare's co-op game over: level state {:02X} -> "
                "{:02X}, Rare's own answer was 'not all down'; returning 'game over' so Rare's death states skip his "
                "revive, 8222FAD8); player 2 lives {}, his death state {}",
                int(state_before), int(base[kLevelState]), g.lives,
                Ptr(g.p2p) ? int32_t(Load32(base, g.p2p + kPartDeathState)) : -1);
  }
  ctx.r3.u64 = 1;
}

// sub_824573A8(participant): Rare's co-op revive, right after a human's death animation. Counter-op: player 2's is
// held 2.5 s (he watches where he died), then run by the tick and followed by the next spot.
REX_HOOK_RAW(sub_824573A8)
{
  const uint32_t participant = ctx.r3.u32;
  if (On() && g.started && !g.unsupported && !g.allow_revive && participant == g.p1p && Ptr(g.p1p) &&
      Load32(base, g.p1p + kPartDeathState) == 2)
  {
    // The agent is never revived (the game-over hook above already keeps Rare's death states from asking).
    if (++g.agent_revives_blocked <= 3)
    {
      REXLOG_INFO("pdz_counterop: a revive of the dead agent was asked (from {:08X}): not run (mission failed)",
                  uint32_t(ctx.lr));
    }
    return;
  }
  if (!On() || !g.started || g.unsupported || g.allow_revive || participant != g.p2p)
  {
    __imp__sub_824573A8(ctx, base);
    return;
  }
  if (g.revive_participant || g.wait_getup)
  {
    REXLOG_INFO("pdz_counterop: player 2's revive asked again (from {:08X}) while {}: ignored", uint32_t(ctx.lr),
                g.wait_getup ? "getting up" : "already held");
    return;
  }
  g.revive_participant = participant;
  g.revive_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(std::max(kNextGuardSeconds, PdzCounterOpTestFirstPickS()) * 1000)); // (Sat 21 Nov: the test wait too)
  // N64 take: the guard this life replaced dies now, where player 2 died (Rare's death path, next tick).
  if (TakeGuard() && Ptr(g.current_guard) && Ptr(g.p2c))
  {
    g.kill_guard = g.current_guard;
    g.current_guard = 0;
    for (uint32_t i = 0; i < 3; ++i)
    {
      g.kill_at[i] = LoadFloat(base, g.p2c + kPosition + i * 4);
    }
    g.kill_origin = false; // Rare's own co-op death path (a rider): the old placing
    g.kill_yaw_ok = false;
  }
  REXLOG_INFO("pdz_counterop: player 2 is down (life {}, revive asked from {:08X}; 8222FAE4 = Rare's death states): "
              "Rare's revive held {:.1f} s, then run out of sight",
              g.lives, uint32_t(ctx.lr), kNextGuardSeconds);
}

// sub_82586238(item, character, 4 out bytes): Rare's "can this character pick this item up?", shared by the weapon
// pick-up (virtual 82584740, which needs out 2) and the other pick-up checks (825845F8, 82554D90, bots 8253ADA8). Its
// own "no" path (825862A4..825862B8) stores 0 in all four bytes; for player 2 that answer is given without asking.
REX_HOOK_RAW(sub_82586238)
{
  const uint32_t character = ctx.r4.u32;
  // g_giving: our own give of the guard's gun at each spawn goes through this test too (log co_jungle: "give returned
  // none" while it was blocked).
  if (character != g.p2c || !g.started || g.unsupported || !Ptr(character) || !On() || g_giving ||
      !REXCVAR_GET(pdz_counterop_block_pickups))
  {
    __imp__sub_82586238(ctx, base);
    return;
  }
  for (const uint32_t out : {ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32})
  {
    if (Ptr(out))
    {
      base[out] = 0;
    }
  }
  const uint32_t item = ctx.r3.u32;
  if (++g.pickups_blocked <= 10 || g.pickups_blocked % 500 == 0)
  {
    REXLOG_INFO("pdz_counterop: pickup blocked for player 2 ({}): item {:08X} (+12 {}), asked from {:08X}",
                g.pickups_blocked, item, Ptr(item) ? int32_t(Load32(base, item + kItemType)) : -1, uint32_t(ctx.lr));
  }
}

// sub_82549040(victim, hit, attacker, ...): a character's hit handler (co-op: friend(victim, attacker) at 82549AB0
// cancels the damage). Counter-op probe, nothing changed: every hit between player 2 and anyone, and the agent's
// hits, with the health before / after, so a test shows each rule of the sides (rate-limited).
REX_HOOK_RAW(sub_82549040)
{
  const uint32_t victim = ctx.r3.u32, attacker = ctx.r5.u32;
  const float lab_before = lab_stealth::HitBefore(base, victim, attacker); // -1 unless the Laboratory stealth watches
  const counter_op_river::HitScope river_hit(base, victim, attacker); // round 13b r: River's test log of hits on the lakes (one bool test when off)
  if (!g.started || g.unsupported || !Ptr(g.p2c) || (victim != g.p2c && attacker != g.p2c) || !Ptr(victim) || !On())
  {
    // Round 14: Outpost's last fight: the hits on the agent by anyone else are logged (the measurement of Rare's fight).
    if (g.started && !g.unsupported && Ptr(victim) && victim == g.p1c && On() && PdzCounterOpOutpostFight())
    {
      const uint32_t part = Load32(base, victim + kParticipantOf);
      const float hb = Health(base, victim), ab = Ptr(part) ? LoadFloat(base, part + 988) : 0.0f;
      __imp__sub_82549040(ctx, base);
      lab_stealth::HitAfter(base, victim, attacker, lab_before);
      PdzCounterOpOutpostAgentHit(base, attacker, hb, Health(base, victim), ab, Ptr(part) ? LoadFloat(base, part + 988) : 0.0f);
      return;
    }
    __imp__sub_82549040(ctx, base);
    lab_stealth::HitAfter(base, victim, attacker, lab_before);
    return;
  }
  // Round 4: a human's armour is his participant's +988 (a guard's is +124).
  const auto armour_of = [base](uint32_t c) {
    const uint32_t part = Load32(base, c) == kHumanVtable ? Load32(base, c + kParticipantOf) : 0;
    return Ptr(part) ? LoadFloat(base, part + 988) : LoadFloat(base, c + kArmour);
  };
  const float before = Health(base, victim), armour_before = armour_of(victim);
  PdzCounterOpOutpostGunHit(victim == g.p1c && attacker == g.p2c); // round 14: his gun's damage on her is scaled
  __imp__sub_82549040(ctx, base);
  PdzCounterOpOutpostGunHit(false);
  lab_stealth::HitAfter(base, victim, attacker, lab_before);
  const float after = Health(base, victim), armour_after = armour_of(victim);
  // Round 3: the agent's hits on player 2 are always logged (the first 60): player 2's own bursts used up the
  // rate-limited lines below, so a test couldn't tell whether her shots reached him.
  // (round 4: and player 2's hits on the agent)
  const bool agent_on_p2 = ((victim == g.p2c && attacker == g.p1c) || (victim == g.p1c && attacker == g.p2c)) &&
                           ++g.agent_hit_lines <= 120;
  if (!agent_on_p2 && ++g.hit_lines > kHitLogFirst && g.hit_lines % kHitLogEvery != 0)
  {
    return;
  }
  auto who = [&](uint32_t c) -> std::string {
    if (!Ptr(c))
    {
      return "none";
    }
    if (c == g.p1c)
    {
      return "the agent";
    }
    if (c == g.p2c)
    {
      return "player 2";
    }
    const uint32_t vt = Load32(base, c);
    if (vt == kGuardVtable)
    {
      return fmt::format("guard {:08X} '{}' (masks {:04X}/{:04X}/{:04X})", c,
                         IdChars(SetupIdKey(base, Load32(base, c + kSetup))), Load16(base, c + kTeamBits),
                         Load16(base, c + kAllies), Load16(base, c + kEnemies));
    }
    return fmt::format("object {:08X} (vtable {:08X})", c, vt);
  };
  REXLOG_INFO("pdz_counterop: hit #{}: {} hit by {}: health {:.1f} -> {:.1f}, armour {:.1f} -> {:.1f}{}", g.hit_lines,
              who(victim), who(attacker), before, after, armour_before, armour_after,
              after < before || armour_after < armour_before ? "" : " (no damage)");
}

// sub_823B15F8(pad, kind, &text, f1 seconds, r7): a HUD message for one player. Its only caller is the co-op update
// 82472E98 (82473620): when a human is down, every other live human gets text 0x165 ("co-op player is in need of
// medical attention! check your radar"). Round 2: dropped while Counter-Op runs (player 2 down or choosing a guard is
// not a partner to revive). Co-op is unchanged (On() is false there).
REX_HOOK_RAW(sub_823B15F8)
{
  if (On() && g.started && !g.unsupported && REXCVAR_GET(pdz_counterop_hide_revive_message))
  {
    static uint32_t dropped = 0;
    if (++dropped <= 3 || dropped % 100 == 0)
    {
      REXLOG_INFO("pdz_counterop: HUD: Rare's co-op revive message (kind {}, from {:08X}) not shown to the agent (#{})",
                  ctx.r4.u32, uint32_t(ctx.lr), dropped);
    }
    return;
  }
  __imp__sub_823B15F8(ctx, base);
}

// sub_82214C48 (TU3 of 8220D9C8, third_person.cpp): the gameplay camera's update (r3 = camera mode, r4 = the target
// character). Counter-op, while player 2 is out of the world (log 306: his view was black): the agent's camera is
// noted, and player 2's is put 1.2 m behind and 0.3 m above the agent's eye, looking at the agent's look point. Rare's
// update runs first in both cases; nothing changes outside counter-op.
REX_HOOK_RAW(sub_82214C48)
{
  const uint32_t mode = ctx.r3.u32, target = ctx.r4.u32;
  const double step = ctx.f1.f64; // Rare's frame step for this update (f1: the rates below are multiplied by it)
  __imp__sub_82214C48(ctx, base);
  if (!On() || !Ptr(mode) || !Ptr(target))
  {
    return;
  }
  // The eye of each player's gameplay camera (testing: pdz_counterop_test_seek / _test_agent_aim aim at these).
  if ((target == g.p1c || target == g.p2c) && !g.in_spectate_call)
  {
    float *eye = target == g.p1c ? g_eye[0] : g_eye[1];
    for (uint32_t i = 0; i < 3; ++i)
    {
      eye[i] = LoadFloat(base, mode + kCamEye + i * 4);
    }
    g_eye_ok[target == g.p1c ? 0 : 1] = std::isfinite(eye[0]) && std::isfinite(eye[1]) && std::isfinite(eye[2]);
  }
  // Player 2's camera object, noted whenever Rare updates it (he is in the world then): co_jungle showed that while
  // he is out of the world Rare doesn't update his camera at all ("his updates 0"), so his view stayed black.
  if ((target == g.p2c || (Ptr(g.p2_mode) && mode == g.p2_mode)) && target != g.p1c && !g.out &&
      g_view_from != std::chrono::steady_clock::time_point{})
  {
    // Round 2: his view after a spawn (a Nightclub screenshot looked third-person after the get-up rescue).
    const double since = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_view_from).count();
    if (since < 4.0)
    {
      const float ox = LoadFloat(base, mode + 160), oy = LoadFloat(base, mode + 164), oz = LoadFloat(base, mode + 168);
      const bool off = ox != 0.0f || oy != 0.0f || oz != 0.0f;
      if (g_view_lines < 12 && (off || g_view_lines < 2) && since >= 0.25 * g_view_lines)
      {
        ++g_view_lines;
        REXLOG_INFO("pdz_counterop: view {:.2f} s after the spawn: camera {:08X} (vtable {:08X}) for {:08X}{} offset "
                    "({:.1f}, {:.1f}, {:.1f})",
                    since, mode, Load32(base, mode), target, target == g.p2c ? " (player 2)" : " (not his character)",
                    ox, oy, oz);
      }
      if (off && since >= 0.5 && REXCVAR_GET(pdz_counterop_fix_view) && (Load32(base, mode) == 0x82097CF8 || Load32(base, mode) == 0x82095EF0))
      {
        StoreFloat(base, mode + 160, 0.0f);
        StoreFloat(base, mode + 164, 0.0f);
        StoreFloat(base, mode + 168, 0.0f);
        if (g_view_cleared++ == 0)
        {
          REXLOG_INFO("pdz_counterop: view: player 2's camera offset cleared {:.2f} s after the spawn (first person)",
                      since);
        }
      }
    }
  }
  if (target == g.p2c && !g.out)
  {
    if (!g.p2_mode_logged)
    {
      // Where the camera object sits (diagnostic: a field of his character or participant, for the start's lookup).
      g.p2_mode_logged = true;
      int32_t in_char = -1, in_part = -1;
      for (uint32_t off = 0; off < 0x2400 && in_char < 0; off += 4)
      {
        in_char = Load32(base, g.p2c + off) == mode ? int32_t(off) : -1;
      }
      for (uint32_t off = 0; Ptr(g.p2p) && off < 0x1000 && in_part < 0; off += 4)
      {
        in_part = Load32(base, g.p2p + off) == mode ? int32_t(off) : -1;
      }
      REXLOG_INFO("pdz_counterop: spectate: Rare updated player 2's camera {:08X} (in his character at +{}, in his "
                  "participant at +{}; -1 = not there)",
                  mode, in_char, in_part);
    }
    g.p2_mode = mode;
  }
  // co_jungle_3: at the start Rare hadn't updated player 2's camera before his first hide, so his view stayed black
  // until his first spot. The agent's camera object is found once in the agent's character (a pointer field); player
  // 2's is then read from the same field of his.
  if (target == g.p1c && g.started && !g.cam_field_tried && Ptr(g.p2c))
  {
    g.cam_field_tried = true;
    // The character first (9 KB), then his participant (4 KB; co_rooftops / co_trinity: not in the character).
    bool in_participant = false;
    for (uint32_t off = 0; off < 0x2400; off += 4)
    {
      if (Load32(base, target + off) == mode)
      {
        g.cam_field = off;
        break;
      }
    }
    if (!g.cam_field && Ptr(g.p1p) && Ptr(g.p2p))
    {
      for (uint32_t off = 0; off < 0x1000; off += 4)
      {
        if (Load32(base, g.p1p + off) == mode)
        {
          g.cam_field = off;
          in_participant = true;
          break;
        }
      }
    }
    const uint32_t p2_mode = !g.cam_field ? 0u : Load32(base, (in_participant ? g.p2p : g.p2c) + g.cam_field);
    REXLOG_INFO("pdz_counterop: spectate: the agent's camera {:08X} is {}; player 2's at the same field: {:08X}{}", mode,
                g.cam_field ? fmt::format("at his {} +{}", in_participant ? "participant" : "character", g.cam_field)
                            : std::string("not found in his first 9 KB or his participant's first 4 KB"),
                p2_mode, Ptr(g.p2_mode) ? fmt::format(" (Rare's own update gave {:08X})", g.p2_mode) : std::string());
    if (!Ptr(g.p2_mode) && Ptr(p2_mode) && p2_mode != mode)
    {
      g.p2_mode = p2_mode;
    }
  }
  if (!g.started || !g.out)
  {
    return;
  }
  if (target == g.p1c)
  {
    ++g.p1_cam_updates;
    for (uint32_t i = 0; i < 3; ++i)
    {
      g.agent_eye[i] = LoadFloat(base, mode + kCamEye + i * 4);
      g.agent_look[i] = LoadFloat(base, mode + kCamLook + i * 4);
    }
    // Round 7 (item 3): while player 2 drives a spiderbot his out-of-the-world view follows the bot, not the agent:
    // the same write below, fed with a point over the bot and a point ahead of it.
    float follow_eye[3], follow_look[3];
    if (PdzCounterOpSpiderView(follow_eye, follow_look))
    {
      std::memcpy(g.agent_eye, follow_eye, sizeof(follow_eye));
      std::memcpy(g.agent_look, follow_look, sizeof(follow_look));
    }
    g.agent_cam = std::isfinite(g.agent_eye[0]) && std::isfinite(g.agent_look[0]);
    // Player 2 out of the world: Rare's update for his camera, run here (then the spectate write below).
    // Sun 22 Nov night (audit): his camera object (found by a field scan) is readable and of a class in the game image.
    const bool p2_mode_ok = pdz::guestmem::Ok(base, g.p2_mode, 4) && Load32(base, g.p2_mode) >= 0x82000000u &&
                            Load32(base, g.p2_mode) < 0x83000000u;
    if (p2_mode_ok && Ptr(g.p2c) && !g.in_spectate_call && !PdzCounterOpRareSpectateActive())
    {
      g.in_spectate_call = true;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = g.p2_mode;
        frame.ctx.r4.u64 = g.p2c;
        SpectateStepFix(base, frame.ctx, step, REXCVAR_GET(pdz_counterop_fix_out_freeze));
        sub_82214C48(frame.ctx, base); // through this hook: it writes the spectate view
      }
      g.in_spectate_call = false;
    }
    return;
  }
  if (target != g.p2c)
  {
    return;
  }
  ++g.p2_cam_updates;
  // Round 10 (item 3): his camera's eye as Rare's update has just left it (before any write of ours).
  for (uint32_t i = 0; i < 3; ++i)
  {
    g_ride.last_eye[i] = LoadFloat(base, mode + kCamEye + i * 4);
  }
  ++g_ride.eye_n;
  if (g_ride.on)
  {
    // His body rides with the spiderbot: the picture is Rare's own camera of it. Round 12 (item 4,
    // pdz_counterop_spiderbot_cam_roll): the camera tilts with the bot's surface. Rare's update has just set the
    // camera's up to the world's (82214C48 at 822154A8: mode +0x34 = (0, 1, 0)) and its look point (+0x10) from his
    // view angles; the CamSpy's camera is fed the same three fields from outside (825403F0: +4, +0x10, +0x34). The
    // up becomes the bot's eased up, and the look point is turned to the chase camera's own direction at Rare's
    // distance (his view angles stop short of straight up, which a bot on a wall needs). Rare's next update sets
    // both afresh, so nothing has to be put back. Off: nothing is written (round 11).
    float ride_cam[3], ride_look[3], ride_up[3];
    bool roll = false;
    if (PdzCounterOpSpiderCamera(ride_cam, ride_look, ride_up, roll) && roll)
    {
      constexpr uint32_t kCamUp = 0x34;
      float pos[3], dir[3], rare[3];
      float len = 0.0f, rare_len = 0.0f, upl = 0.0f;
      for (uint32_t i = 0; i < 3; ++i)
      {
        pos[i] = LoadFloat(base, mode + kCamPosition + i * 4);
        rare[i] = LoadFloat(base, mode + kCamLook + i * 4) - pos[i];
        dir[i] = ride_look[i] - ride_cam[i];
        len += dir[i] * dir[i];
        rare_len += rare[i] * rare[i];
        upl += ride_up[i] * ride_up[i];
      }
      len = std::sqrt(len), rare_len = std::sqrt(rare_len);
      if (std::isfinite(len) && std::isfinite(rare_len) && len > 1.0f && rare_len > 0.01f && upl > 0.9f && upl < 1.1f)
      {
        for (uint32_t i = 0; i < 3; ++i)
        {
          StoreFloat(base, mode + kCamUp + i * 4, ride_up[i]);
          StoreFloat(base, mode + kCamLook + i * 4, pos[i] + dir[i] / len * rare_len);
        }
        static uint32_t lines = 0;
        static int64_t said_for = 0;
        if (g_ride.since != said_for && lines < 12) // a new ride: one line
        {
          said_for = g_ride.since;
          ++lines;
          REXLOG_INFO("pdz_counterop: spider: camera {:08X}: tilt with the surface: after Rare's update its up (+0x34) "
                      "is set to the bot's eased up ({:.2f}, {:.2f}, {:.2f}) and its look point (+0x10) to the chase "
                      "camera's direction, {:.0f} units out as Rare had it",
                      mode, ride_up[0], ride_up[1], ride_up[2], rare_len);
        }
      }
    }
    return;
  }
  if (!g.agent_cam)
  {
    return;
  }
  // Behind along the agent's look direction in the horizontal plane, then up.
  float fx = g.agent_look[0] - g.agent_eye[0], fz = g.agent_look[2] - g.agent_eye[2];
  const float len = std::sqrt(fx * fx + fz * fz);
  if (len < 1.0f)
  {
    fx = 0.0f;
    fz = 0.0f;
  }
  else
  {
    fx /= len;
    fz /= len;
  }
  float cam[3] = {g.agent_eye[0] - fx * kSpectateBack, g.agent_eye[1] + kSpectateUp,
                  g.agent_eye[2] - fz * kSpectateBack};
  // Round 9: while player 2 drives a spiderbot the view is the bot's follow camera: its place, the point it looks
  // at, and (the camera's up vector, +0x34, three floats) the bot's own up, so that the view turns with the surface.
  // The up vector is only written when what is there is a unit vector, and is put back when the drive ends.
  bool spider_view = false; // Sat 21 Nov: the upright wait view is for the plain follow view only
  {
    constexpr uint32_t kCamUp = 0x34;
    static uint32_t rolled_mode = 0;
    static float saved_up[3] = {0.0f, 1.0f, 0.0f};
    float spider_cam[3], spider_look[3], spider_up[3];
    bool roll = false;
    const bool spider = PdzCounterOpSpiderCamera(spider_cam, spider_look, spider_up, roll);
    spider_view = spider;
    if (spider)
    {
      std::memcpy(cam, spider_cam, sizeof(cam));
      std::memcpy(g.agent_look, spider_look, sizeof(spider_look));
    }
    // Sun 22 Nov night (audit): the rolled camera is remembered past resets and levels: put back only into the same
    // game state's camera, still readable and of the same class (else the old address is dropped unwritten).
    static uint32_t rolled_state = 0, rolled_vt = 0;
    if (rolled_mode && (rolled_mode != mode || !spider || !roll))
    {
      if (rolled_state == g.state_object && pdz::guestmem::Ok(base, rolled_mode, kCamUp + 12) &&
          Load32(base, rolled_mode) == rolled_vt)
      {
        for (uint32_t i = 0; i < 3; ++i)
        {
          StoreFloat(base, rolled_mode + kCamUp + i * 4, saved_up[i]);
        }
        REXLOG_INFO("pdz_counterop: spider: camera {:08X}: its up vector is put back ({:.2f}, {:.2f}, {:.2f})", rolled_mode,
                    saved_up[0], saved_up[1], saved_up[2]);
      }
      rolled_mode = 0;
    }
    if (spider && roll)
    {
      const float ux = LoadFloat(base, mode + kCamUp), uy = LoadFloat(base, mode + kCamUp + 4),
                  uz = LoadFloat(base, mode + kCamUp + 8);
      const float len = ux * ux + uy * uy + uz * uz;
      if (!rolled_mode)
      {
        const bool unit = std::isfinite(len) && len > 0.98f && len < 1.02f;
        static uint32_t lines = 0;
        if (++lines <= 6)
        {
          REXLOG_INFO("pdz_counterop: spider: camera {:08X}: the three floats at +0x34 are ({:.3f}, {:.3f}, {:.3f}): {}",
                      mode, ux, uy, uz, unit ? "a unit vector, taken as its up: it follows the bot's up from now"
                                             : "not a unit vector: left alone (no roll)");
        }
        if (unit)
        {
          rolled_mode = mode;
          rolled_state = g.state_object, rolled_vt = Load32(base, mode); // Sun 22 Nov night (audit)
          saved_up[0] = ux, saved_up[1] = uy, saved_up[2] = uz;
        }
      }
      if (rolled_mode == mode)
      {
        for (uint32_t i = 0; i < 3; ++i)
        {
          StoreFloat(base, mode + kCamUp + i * 4, spider_up[i]);
        }
      }
    }
  }
  for (uint32_t i = 0; i < 3; ++i)
  {
    StoreFloat(base, mode + kCamPosition + i * 4, cam[i]);
    StoreFloat(base, mode + kCamLook + i * 4, g.agent_look[i]);
  }
  ++g.spectate_writes;
  if (!spider_view)
  {
    PdzCounterOpWaitViewUpright(base, mode, g.agent_eye, g.p2c); // Sat 21 Nov: pdz_counterop_wait_view_upright
  }
}

// pdz_test_keep_dropship (testing tool, off): the Jungle's "dropship destroyed" failure is a scripted message (type 8,
// subtype 6) to the Jungle level script's message handler sub_82311AB0 (vtable 820A21F4 slot 9; only the Jungle uses
// it), most likely an enemy radio operator calling it in (sender sub_825B4F88) when nobody stops him; it sets the
// trigger bits whose consequences (sub_82315BA0) play the Carrington line, fail objective 0 (Defend Dropship) and blow
// the dropship up. An unattended test whose agent stands still loses the mission to it ~12-20 s in (also solo, with
// counter-op off), so with this on that one message is dropped.
REXCVAR_DEFINE_BOOL(pdz_test_keep_dropship, false, "Testing",
                    "TESTING TOOL: in the Jungle, the scripted 'dropship destroyed' message (the failure an idle test "
                    "agent always gets ~15 s in) is ignored, so long unattended Jungle tests can run")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(__imp__sub_82311AB0);
REX_HOOK_RAW(sub_82311AB0)
{
  const uint32_t rec = ctx.r8.u32;
  if (REXCVAR_GET(pdz_test_keep_dropship) && rec >= 0x10000u && rec < 0xFFFF0000u && base[rec + 1] == 8 &&
      base[rec + 2] == 6)
  {
    static uint32_t dropped = 0;
    if (++dropped <= 3)
    {
      const uint32_t sender = ctx.r5.u32;
      REXLOG_INFO("pdz_test: Jungle dropship-destroyed message dropped (pdz_test_keep_dropship; param {}, sender {:08X} "
                  "vtable {:08X})",
                  int(base[rec + 3]), sender,
                  sender >= 0x10000u && sender < 0xFFFF0000u
                      ? (uint32_t(base[sender]) << 24 | uint32_t(base[sender + 1]) << 16 | uint32_t(base[sender + 2]) << 8 | base[sender + 3])
                      : 0u);
    }
    return;
  }
  __imp__sub_82311AB0(ctx, base);
}
