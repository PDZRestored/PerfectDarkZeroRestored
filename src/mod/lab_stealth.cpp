// Laboratory guard stealth (pdz_lab_guard_stealth, Gameplay, default ON (round 4), hot-reloadable; Laboratory only). Research:
// the Laboratory research report (2, 5, "Follow-up (9 Nov)", "Voices (9 Nov)") and its notes 6d (alert states),
// 2 (AI darkness), 8 (Laboratory voices), 1b (setup and spawner); the stealth core's design notes.
// Everything passes straight through when the switch is off or outside the Laboratory (lab_restorations'
// CurrentScript() is 0).
//
// Who takes part: the male trooper guards (voice index char +12704 = 17..19, TROOPER1-3): the whole loop. The
// scientists (15 / 16) get the detection meter only (pdz_lab_stealth_scientists): a SEARCH task would replace their
// scientist task, and they have no search lines. The female prison guards and the reinforcement spawns (21 / 22,
// WOMAN1-2) have no surprised / search animations and cannot voice the loop: Rare's behaviour, untouched. Only player 1
// is the meter's target; any other target (a co-op partner, Jack) is Rare's.
//
// 1. DETECTION METER (ours; Rare has none, 6d section 2). The NPC sight update sub_82580508 (NPC vtable +0x360, called
//    from the NPC update sub_825800F8 after the AI think sub_8252C2A0) only READS the per-pair seen bitsets
//    (sub_8218B498 -> sub_82189100). The tests themselves run in Rare's perception tick sub_8218A810 (a round-robin
//    queue of viewers, at most 5 per frame), whose visible-thing walk sub_8218A210 tests each target with
//    sub_82189F18 (its only caller is 8218A490: r9 eye, r10 facing, f1 squared range, stack +0x54 target position,
//    +0x64 viewer, +0x6C target). Our sight-update hook marks each tracked guard metered or not once a frame; the test
//    hook looks the viewer up. When Rare's test says a metered guard sees Jo, the meter (0..100) grows by
//    pdz_lab_stealth_rate x exposure x the time since his last test of her; below the threshold (100; wary 75) we answer
//    "not visible", so Rare never learns she was there (no first-sighting bark, no +6268 0x800000, no stimulus 0x100,
//    no target); at the threshold we let Rare's answer through and he does what Rare does (the SURPRISED reaction
//    within 5 m, the first-sighting bark, COMBAT). Exposure = distance (1 within pdz_lab_stealth_near_cm, then
//    near / d) x range edge (1 - d^2 / range^2, at least 0.15) x view centre (0.35 + 0.65 cos^2 of the angle off his
//    facing) x movement (0.5 standing still .. 1 at 250 cm/s) x armed (1, unarmed 0.6) x dark (pdz_lab_stealth_dark
//    when the blackout reached him: +6276 bit 0x2, set only by Rare's darkness writes) x crouch (Rare's 0.5, crouch test
//    sub_824FE928). The meter decays at pdz_lab_stealth_decay per second while he does not see her.
//    SUSPICIOUS: when the meter passes pdz_lab_stealth_notice on a hidden glimpse (not within near_cm in light: that is
//    combat at once), he gets Rare's SEARCH task at Jo's pad directly and says HEAR_UNKNOWN_SOLO (see TryInvestigate:
//    the stimulus route below sent alarm users to the alarm, so it is only the fallback). The fallback is Rare's own
//    "heard something" investigation: the listener's noise pad (profile +0x4C) = Jo's pad (+0xB4, what Rare's footstep
//    pass writes) and the per-frame "heard a sound event" input (+6260 0x4 -> stimulus 0x1000). The next DEFAULT
//    update (classifier 821D0748 / 821D1888, handler 821D151C) applies Rare's squad gate 8217CC30 (at most 3 searching,
//    never-investigates flag), starts a SEARCH task (type 0x26, vtable 820933D0) at that pad, whose start
//    (821DF7C0) plays the search-start animation 107, and says HEAR_UNKNOWN_SOLO (11). Ours on top: one investigator
//    per squad (squad from sub_8217D108), and HEAR_UNKNOWN_SOLO from us if Rare's repeat handler started the search
//    without a line.
// 2. NO SPREAD FOR SUSPICION: a suspicious guard never runs COMBAT start (821CBF48: the radius cascade and the squad
//    target copy 8217C540); only a real sighting / damage / alarm does.
// 3. LOSE CONTACT: "in combat" = his COMBAT script is alive (from COMBAT start sub_821CBF48 until its update
//    sub_821CC340 sets status 2 or its destructor sub_821CBE88 runs; the update itself does not run while a child task
//    of the script is on top). Checked once a frame: a guard in combat whose last sighting (+0x30F8, written by the
//    sight pass) and combat start are both pdz_lab_stealth_lose_s ago, or whose combat Rare ended more than 2 s after
//    his last sighting, says PLAYER_HIDES_SOLO (25; once per squad in 10 s), his squad's shared target pad / zone (squad +0x44 / +0x48, written by 8217C4A0 / 8217C540) is set to Rare's
//    empty values (0 / -1), and (if the script still runs) +6268 bit 0x8 is set: Rare's own "leave combat" request (set by Rare at 821DBA58), on
//    which that same update ends the COMBAT script (status 2, 821CC3A0-821CC3BC). After the script ends, a SEARCH task
//    at his target's last pad (+0x1830) is handed to his scheduler (as Rare's darkness loop does, 82328480-823284B8);
//    the meter starts at pdz_lab_stealth_lost_start. pdz_lab_stealth_still_hidden_s into that search he says
//    PLAYER_STILL_HIDDEN_SOLO (35). If he reacquires her (COMBAT start while lost) he says SEE_PLAYER_AGAIN_SOLO (23).
//    While the alarm sounds and he lacks Rare's soft-alarm flag (+6272 0x1000: set by the Laboratory spawner once the
//    tracking system is destroyed) he does not lose contact: Rare's rule, the alarm means combat.
// 4. SEARCH END, then WARY: when a search ends (Rare's 180 s, 821DFB28) he goes back to his routine (Rare's), and stays
//    wary for the rest of the run: meter floor pdz_lab_stealth_wary_floor, combat at pdz_lab_stealth_wary_combat, half
//    the notice level, and a HEAR_UNKNOWN_SOLO about every pdz_lab_stealth_wary_bark_s. After a lost-contact search he
//    says SEARCH_ABANDON_SOLO (41) unless Rare's own end of search just said it (821E0244).
// 5. THE ALARM: Rare's alarm runners, alarm, waves and the soft-alarm rule are untouched.
// 6. VOICES: every line goes through Rare's say-event sub_8262AA40 with r5 = 1 (Jo, so never a _MALE cue). The take
//    inside a cue is XACT's choice in the guest's XACT code (not in the SDK); before any guard has seen Jo we raise
//    only HEAR_UNKNOWN_SOLO, whose takes are all without he / she; the "she" cues (PLAYER_HIDES, STILL_HIDDEN,
//    SEARCH_ABANDON) only follow a sighting by that guard. See the notes file for Rare's own SEARCH_START_SOLO
//    "Gotta find him!".
// 7. CONFIRMED vs GLIMPSE ONLY (pdz_lab_stealth_hunting, on with the core). A guard is CONFIRMED once he had a real
//    sighting (meter full), was in Rare's COMBAT, was hurt by Jo (the hit handler sub_82549040, victim r3, attacker
//    r5: hooked in counter_op.cpp when PDZ_COUNTEROP builds it, else here), had a squad mate killed by Jo (health
//    +120 going to 0 in that handler; squad from sub_8217D108), or walked up to a body Jo killed in this run (Rare's
//    SEE_BODY_SOLO from his sight pass 825809C4, then Rare's walk to the body; a body Jo did not kill, e.g. the
//    script's, does not count). Glimpse only = none of these: the old path (a search, then WARY at his post).
// 8. HUNTING (confirmed guards, instead of WARY): after lost contact (and after a confirmed guard's search, or
//    Rare's end of his combat) he sweeps: Rare's SEARCH task (the same GiveSearch) at one pad after another, each leg
//    pdz_lab_stealth_hunt_leg_s long (Rare's 180 s end, reached by moving the task's start time back), the pads from
//    a per-squad plan: her last known pad first, then up to 8 pads around it over open pad links (pad +8 list, link
//    +0 pad / +4 next / +14 closed count, pad +16 position; within pdz_lab_stealth_hunt_hops links and
//    pdz_lab_stealth_hunt_radius_cm, the farthest per 45-degree sector, in angle order: the routes out). Each squad
//    member takes the next pad of the plan, so two free hunters of a squad sweep neighbouring pads around the same
//    point (pairs). After pdz_lab_stealth_hunt_legs legs he pauses (Rare's routine, his post) for
//    pdz_lab_stealth_hunt_pause_s, then sweeps again, for the rest of the level. The meter runs faster
//    (pdz_lab_stealth_hunt_rate), combat at pdz_lab_stealth_hunt_combat, floor pdz_lab_stealth_hunt_floor; a hidden
//    glimpse ends his leg and his next leg is at her pad (his own, not the squad's: no squad-target copy unless Rare's
//    sight sees her). A "Show yourself!" (PLAYER_STILL_HIDDEN_SOLO; HEAR_UNKNOWN_SOLO for a hunter who never saw her)
//    about every pdz_lab_stealth_hunt_bark_s, at most one per squad per half interval. Weapon up: not done (no
//    clean "weapon raised" state found; Rare's alert latch +6252 0x10 stays set from his COMBAT start anyway).
// 9. VOICE BUDGET: see point 11 (e) (round 3: every trooper line, Rare's and ours, and the tannoy).
// 10. TANNOY: the first hunter of a squad asks for Rare's TANNOY_SECURITY12 ("location unknown"), played exactly as
//    the Laboratory tick plays its area alerts (823203A0-823203C0: sub_82202D20(ref, 820A282C), then sub_822014B8
//    f1 5.0, f2 0.0, r6 1, r7 0, r8 0), through the voice budget, not while the alarm sounds (Rare's own area
//    alerts play then, 8232012C), and at most once per pdz_lab_stealth_tannoy_s. The take is XACT's choice.
// 11. ROUND 3 (hand tests 004 / 005, 10 Nov). (a) A death confirms only a guard who saw it (his sight test saw Jo
//    within 1.5 s, within 30 m, or Rare's valid-target bit: he is fighting her); the others are told nothing and may
//    find the body. (b) DEAD: when Jo's hit handler sees a guard's health reach 0, or his health (+120) is 0 after it
//    was above 0, his state is Dead at his next sight pass: nothing of ours runs for him again (no hunting, sweep,
//    tannoy, lines; his tannoy request is dropped). Every "leg's SEARCH did not start" in 004 / 005 was such a dead
//    guard. (c) Hunters of a squad share the sweep plan out: never the pad another live hunter sweeps nor one within
//    pdz_lab_stealth_hunt_spread_cm of his; her last pad goes to one hunter. A leg that never starts logs his health,
//    flags, pad and scheduler. (d) While the alarm sounds a guard out of sight for pdz_lab_stealth_alarm_lose_s also
//    loses contact (Rare's rule kept the hall guards in combat at the vent for minutes). (e) VOICE BUDGET for every
//    trooper line, Rare's and ours, at the say-event: outside a fight one voice at a time (a line's end = its take's
//    length) plus pdz_lab_stealth_voice_gap_s; in a fight at most pdz_lab_stealth_combat_voices at once; never the same
//    line from the same guard within pdz_lab_stealth_repeat_s (Rare's per-frame repeats of a request: HOLD_EXIT 135
//    times). Death / pain lines: only the repeat rule. (f) TAKES (lab_stealth_takes.inc, generated from the Laboratory
//    voice research table: take numbers, lengths and flags only): never a take naming Jo "he"; "she" takes only after
//    his own sighting or a lost contact; beyond 8 m from Jo no whispered take; not his previous take of the cue; a cue
//    without an allowed take is dropped. (g) MUTTER: while guards search or hunt, the nearest to Jo mutters a search line
//    every pdz_lab_stealth_mutter_s (8-12 s), timed from the last guard line; it replaces the per-guard hunt call-outs.
//    TESTING: pdz_test_stealth_kill (Rare's Kill on a named guard with Jo as the killer).
// LOGGING: pdz_lab_stealth_log: once a second per guard that is not unaware (or has a meter): id, meter, state,
// distance to Jo, sees-her; every state change and every say-event a tracked guard raises, at once.
// TESTING: pdz_test_stealth_search_s shortens Rare's 180 s search (the task's start time +0x78 is moved back).
// 12. ROUND 4 (hand tests 025 / 018, 10 Nov).
//    (a) WHO IS TRACKED. A guard's state is keyed by his character's address, and Rare reuses a dead character's memory
//       for a new one (log 025: the reinforcement spawn s101, a WOMAN1 voice, was created in a killed trooper's memory
//       the moment the alarm sounded, and every line of hers was logged "in state dead"). So an entry belongs to the
//       character whose setup data (+0x54) it was made for: a different setup, or a live character (health above 0) in
//       the memory of one we saw die, is a new character: the old entry is dropped and he joins as unaware (or, for a
//       character we don't track, e.g. a woman, nothing of ours applies to him). A guard carrying Rare's dormant bit
//       (+6272 0x10, SetDormant) is not in the loop until he wakes; death needs health 0 after he was seen alive.
//       pdz_lab_stealth_log prints a census line for each character the first time his sight runs.
//    (b) A REAL INVESTIGATION (lamps and bodies). Instead of one Rare SEARCH at one pad (log 018: 95 s pacing one
//       spot), the investigator walks to the pad nearest the thing and stops there, looking (the SEARCH task's own
//       Search_Look animations at its pad: search_19spinlook / 20searchlook / 21searchlook180 of the male set), for
//       pdz_lab_stealth_look_s; then checks up to pdz_lab_stealth_inv_legs more pads, one leg at a time (each a SEARCH at
//       one pad, ended pdz_lab_stealth_inv_dwell_s after he reaches it, by moving the task's start time back as the
//       hunting legs do), at least 4 m from each other and from every other investigator's pad, within
//       pdz_lab_stealth_inv_radius_cm, preferring Rare's cover pads (pad +0x34 flags 0x4 / 0x8) and the direction Jo
//       was in when it began (never within 3 m of her own spot unless he saw her then). It ends after
//       pdz_lab_stealth_lamp_search_s (lamp) / pdz_lab_stealth_body_search_s (body). A glimpse of Jo sends his next leg
//       to her pad; a real sighting is Rare's combat (round 3). After a lamp: Rare's unused "never mind" takes
//       (HEAR_NOISE_NOT_INVESTIGATE_TEAM, played by the cue swap below), back to his post, wary.
//    (c) A BODY (pdz_lab_stealth_body_rules): Rare's SEE_BODY of a body Jo left makes a guard who never saw Jo
//       investigate it alone (no confirmed contact, nobody told). Killed meanwhile: nothing is passed on and the body
//       waits for the next guard. Done without finding her: an ALARM_USER (setup +0xFD = 4) runs for the alarm if it is
//       off (Rare's own run: gate sub_821DF340, task sub_821DF4A0 handed to his scheduler), as does any other guard on
//       a pdz_lab_stealth_body_alarm_chance roll; otherwise he calls his squad (Rare's SEE_BODY_TEAM, "Got a man down
//       here!"): his live squad mates within pdz_lab_stealth_call_radius_cm search around the body with him, each on
//       his own pads, for pdz_lab_stealth_squad_search_s, then go back to their posts wary. A guard who has seen or
//       fought Jo keeps round 3's rules (confirmed, hunting).
//    (d) THE ALARM ENDS (pdz_lab_stealth_alarm_end). Rare's Laboratory alarm never stops by itself; Jo's alarm switch
//       (level event 46, 82322740-82322770) toggles it through sub_8214EDF0 (alarm off: stops the level manager's
//       alarm sound +0x50, clears the alarm byte +0x54), and the Laboratory tick then stops the six sirens (flag 193)
//       and resets the alarm props (172, 257). Here the first alarm also ends by itself through that same call after
//       pdz_lab_stealth_alarm_end_s with no confirmed sighting of Jo by any tracked guard (each sighting restarts it).
//       When an alarm ends, every searching or hunting guard goes back to his post wary for the rest of the level; one
//       still fighting goes wary when he loses her; the soft-alarm flags we set (round 3 (d)) are cleared. The
//       Laboratory has no "all clear" tannoy (TANNOY_SECURITY01-12 are intruder alerts, TRIGGER01-07 generator,
//       blackout, tracking and fire lines), so nothing is announced.
//    (e) ALARM TIERS (pdz_lab_stealth_alarm_tiers). Rare's reinforcements are the s10 waves (research 1b 4.2): while
//       the alarm is on, the Laboratory tick (82321100-823213B4) sends the level message {type 7, byte 10} for each
//       spawn (gaps 1 / 1 / 5 / 5 s, 4N + 18 in all), and the script's message handler sub_823223F0 spawns a
//       securityforcegirl clone (s10spawnholdflashbang, s10spawn, s10spawnalloutattack*) at one of four pads. During
//       the FIRST alarm those messages are dropped (no spawn, no stage counted); once the first alarm has ended, any
//       later alarm lets them through exactly as Rare's tick sends them, and that alarm does not end by itself.
// 13. ROUND 7 (hand test 048, 12 Nov): polish only.
//    (a) RARE'S OWN SEARCHES ARE LEFT AS THEY ARE. Rare's SEARCH update (821DFFF0-821E0160), whenever its go-to child
//       is over, picks its next pad itself: a random pad of the zone of his current pad (pad +62; 30 tries a frame),
//       not flagged out (+52 0x80), free for him in his squad (sub_8217CB50: not claimed by a mate, and he is the
//       squad's nearest member to it), farther than a least distance from his pad; then a go-to-pad child. A guard
//       whose zone holds no such pad, or whose go-to ends at once, stands for Rare's 180 s. The 13 guards of the
//       security squads (Rare's wake-up, level message type 7 byte 4, 82326C30-82326F2C: a SEARCH with no pad for each
//       while the blackout is on) do exactly the same with this code off (tests r7_1a / r7_1b): four walk, seven
//       stand, two run to a post under another task of Rare's. Nothing of ours touches such a search; when it is over
//       the scheduler builds his DEFAULT script (point 5 of round 5) and Rare's AI has him again.
//    (b) NO-PROGRESS FALLBACK (pdz_lab_stealth_stuck_s): on a walk of ours (Rare's walk to a body, a body / lamp /
//       sound / squad-search leg, a sweep leg) a guard who has not moved 1 m for that long (looked at from 8 s after
//       the walk began: Rare's reaction, turn and start animations) gives it up: after Rare's walk to a body the
//       pad beside it is tried; after a pad walk he looks round where he stands (a SEARCH at his own pad) and the
//       investigation ends as its rules say, or the sweep carries on. Every fallback is logged.
//    (c) ESCALATION TIERS (pdz_lab_stealth_tiers): see AreaIncident.
//    (d) A LAMP GOING OUT is noticed by a guard who sees the point 1 m under it or the floor it lit, or who stands in
//       the light it gave (LightIncident).
//    (e) CLOSE-RANGE FLOOR (pdz_lab_stealth_close_cm / _mid_cm / _mid_floor): see the sight test hook.
//    (f) In Rare's blackout the meter uses Rare's dark bit only (lab_dark_patches.cpp point 6).
//    TESTING: pdz_test_stealth_observe (watch only), pdz_test_stealth_pin (hold a guard in place).
// 14. ROUND 11 (hand test 010, 13 Nov): the last Laboratory round.
//    (a) LOUD MEANS RARE'S FULL GAME (pdz_lab_stealth_loud). The first time the alarm is raised, Jo fires an
//       unsilenced shot a live character can hear (Rare's noise radius of the shot x his hearing factor), an explosion
//       goes off within its sound radius of a live character, or the generator switch is thrown (level flags 37 / 38;
//       15 = blown up), this whole file switches off for the rest of the run (GoLoud: g_lab goes false, so every hook
//       passes straight through): no meter, no taken stimuli, no investigations, no voice rules, no alarm end, and
//       the s10 reinforcement waves are never held back (12 (e) is gone). Kept: the two crash guards (the task stack
//       guard and the ended SEARCH's what-next handler) and pdz_lab_search_fallback. It never goes back to quiet.
//    (b) FOOTSTEPS: walking (Rare's move stick at pdz_lab_footsteps_walk_stick or less) is heard within
//       pdz_lab_footsteps_walk_cm (100), running within pdz_lab_footsteps_run_cm (400), crouching never; a hearing
//       meter of its own (Guard::hear), so the wary floor of the detection meter gives no head start.
//    (c) A SIGHTING LINE AT A SIGHTING: Rare's WARN_TEAM raised in the second after a guard's sighting becomes his
//       sighting line; any other WARN_TEAM needs a live squad mate within pdz_lab_stealth_warn_team_cm.
//    (d) A LAMP IS HEARD within pdz_lab_stealth_lamp_hear_cm (500) whatever way he faces (the sound mask blocks it).
//    (e) INVESTIGATIONS: a lamp / noise check looks at the spot for 10-15 s, then at most one spot within
//       pdz_lab_stealth_spot_near_cm; heard footsteps are such a noise check; a squad called to a body only sends
//       guards on the body's floor with a route on that floor (the rest: wary at their posts); a walk with no
//       progress for pdz_lab_stealth_stuck_s (2) is given up at once: back to his post, wary.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
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

#include "lab_restorations.h"
#include "lab_stealth.h"
#include "restorations.h"
#include "lab_dark_patches.h"
#include "guest_function.h"
#include "lab_light_level.h" // Luma (Mansion helper: the moon's brightness)
#include "voice_volume.h"
#include "render/game_pace.h" // SpLevelName (stealth rollout: which level is played)
#include "aim120_Wed18Nov.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: Id() reads only mapped memory

REXCVAR_DEFINE_BOOL(pdz_lab_guard_stealth, true, "Gameplay",
                    "THE MASTER SWITCH OF THE STEALTH LAYER (Laboratory, Nightclub docks, Subway after the garage, "
                    "Mansion grounds; solo and plain co-op, never Counter-Operative or multiplayer). An OPTIONAL layer "
                    "for the QUIET part only. On: while a guard is calm he notices Jo more slowly in the dark at range "
                    "(a detection meter), hears her steps, investigates noises, lamps and bodies with Rare's own lines "
                    "and can be crept up on from behind. The moment Rare's own sight has her in plain view, or she "
                    "goes loud (an unsilenced shot, an explosion, an alarm, a hit, a fight near him), that guard is "
                    "Rare's own again until Rare has him calm (pdz_stealth_rares_guard): Rare's combat and search, "
                    "untouched. Each level also has its own switch (pdz_lab_rareplus for the Laboratory, "
                    "pdz_stealth_nightclub / _subway / _mansion). OFF = RARE'S GAME EXACTLY ON EVERY LEVEL: every hook "
                    "of the layer passes straight through, and the dark patches (pdz_lab_dark_patches) do nothing")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_log, false, "Diagnostics",
                    "pdz_lab_guard_stealth: once a second per guard that is not unaware: meter, state, distance to Jo, "
                    "sees her; every state change and every line a tracked guard says")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_rate, 200.0, "Gameplay",
                      "pdz_lab_guard_stealth: meter gain per second of a full-exposure look (within near_cm, in his view "
                      "centre, Jo moving and armed, lit). 100 = combat, so 200 makes a close clear look reach it in "
                      "half a second")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_near_cm, 500.0, "Gameplay",
                      "pdz_lab_guard_stealth: within this distance (cm) the distance factor is 1; beyond, near / distance")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_notice, 20.0, "Gameplay",
                      "pdz_lab_guard_stealth: meter level at which a hidden glimpse makes him suspicious (investigate)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_decay, 4.0, "Gameplay",
                      "pdz_lab_guard_stealth: meter loss per second while he does not see her")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_dark, 0.35, "Gameplay",
                      "pdz_lab_guard_stealth: meter factor for a guard the blackout darkened (Rare's dark bit)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_lose_s, 10.0, "Gameplay",
                      "pdz_lab_guard_stealth: seconds without seeing Jo before a guard in combat loses contact and "
                      "searches her last position")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_lost_start, 60.0, "Gameplay",
                      "pdz_lab_guard_stealth: the meter of a guard who just lost contact (he is looking for her); "
                      "with pdz_lab_stealth_hunting a hunter starts at pdz_lab_stealth_hunt_floor instead")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_still_hidden_s, 25.0, "Gameplay",
                      "pdz_lab_guard_stealth: seconds into a lost-contact search before \"Can't hide forever!\" "
                      "(PLAYER_STILL_HIDDEN); with pdz_lab_stealth_hunting, about when a hunter first calls out; "
                      "0 = never (hunters: after pdz_lab_stealth_hunt_bark_s)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_wary_floor, 25.0, "Gameplay",
                      "pdz_lab_guard_stealth: a wary guard's meter never decays below this")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_wary_combat, 75.0, "Gameplay",
                      "pdz_lab_guard_stealth: a wary guard sees Jo (combat) at this meter level instead of 100")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_wary_bark_s, 60.0, "Gameplay",
                      "pdz_lab_guard_stealth: a wary guard says a HEAR_UNKNOWN line about this often (s); 0 = never")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_scientists, true, "Gameplay",
                    "pdz_lab_guard_stealth: the scientists get the detection meter too (no investigation: they keep "
                    "Rare's scientist behaviour once they see Jo)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_hunting, true, "Gameplay",
                    "pdz_lab_guard_stealth: after a CONFIRMED contact (a real sighting, a fight, hurt by Jo, a squad "
                    "mate killed by Jo, a body she left found) a guard who loses her HUNTS for the rest of the level: "
                    "Rare's search carried from pad to pad around her last known position and the ways out of it, squad "
                    "mates on neighbouring pads, a faster meter, \"Show yourself!\" now and then, short pauses at his "
                    "post between sweeps. A guard who only glimpsed her still searches once and goes wary. Off = every "
                    "guard goes wary after a search")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_rate, 3.0, "Gameplay",
                      "pdz_lab_guard_stealth: a hunting guard's meter fills this many times faster")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_combat, 55.0, "Gameplay",
                      "pdz_lab_guard_stealth: a hunting guard sees Jo (combat) at this meter level instead of 100")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_floor, 30.0, "Gameplay",
                      "pdz_lab_guard_stealth: a hunting guard's meter never decays below this")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_leg_s, 30.0, "Gameplay",
                      "pdz_lab_guard_stealth: a hunter searches each spot of his sweep this long (s) before moving on "
                      "(Rare's searches last 180 s)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_stealth_hunt_legs, 3, "Gameplay",
                     "pdz_lab_guard_stealth: spots a hunter searches in one sweep before a pause at his post")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_pause_s, 25.0, "Gameplay",
                      "pdz_lab_guard_stealth: a hunter's pause at his post between sweeps (s); then he sweeps again")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_bark_s, 20.0, "Gameplay",
                      "pdz_lab_guard_stealth: a hunter calls out (\"Show yourself!\") about this often (s); 0 = never")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_radius_cm, 2500.0, "Gameplay",
                      "pdz_lab_guard_stealth: the sweep covers AI pads up to this far (cm) from her last known spot")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_stealth_hunt_hops, 3, "Gameplay",
                     "pdz_lab_guard_stealth: the sweep follows open pad links up to this many steps from her last "
                     "known spot")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_voice_gap_s, 0.6, "Gameplay",
                      "pdz_lab_guard_stealth: voice budget: while no guard fights Jo, one guard voice at a time in the "
                      "Laboratory (Rare's lines and ours), with this many seconds of quiet after a line before the next "
                      "(call-outs and the tannoy wait a second more)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_stealth_combat_voices, 2, "Gameplay",
                     "pdz_lab_guard_stealth: voice budget in a fight: at most this many guard lines at once")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_repeat_s, 10.0, "Gameplay",
                      "pdz_lab_guard_stealth: voice budget: a guard never says the same line again within this many "
                      "seconds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_mutter_s, 10.0, "Gameplay",
                      "pdz_lab_guard_stealth: while guards search or hunt, one of them (the nearest to Jo) mutters a "
                      "search line about this often (s, spread 8-12 at 10); 0 = never")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_duck_db, 4.0, "Gameplay",
                      "pdz_lab_guard_stealth: while a guard line plays outside a fight, the level music is this many dB "
                      "quieter (down in 0.25 s, back in 0.6 s); 0 = never")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_sight_block, true, "Gameplay",
                    "pdz_lab_guard_stealth: Laboratory sight blocks (a short data list): where Rare's collision leaves a "
                    "gap the level looks closed, a guard's line of sight to Jo through it counts as blocked. Today one "
                    "entry: the bridge's east edge above the stair head (Rare's fall-guard plane there does not block "
                    "AI sight), so the guard at gold door 9 no longer sees Jo on the top stairs")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_alarm_lose_s, 20.0, "Gameplay",
                      "pdz_lab_guard_stealth: while the alarm sounds, a guard who has not seen Jo for this many seconds "
                      "loses contact too (then searches or hunts); 0 = Rare's rule (the alarm means combat until it "
                      "stops)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_hunt_spread_cm, 400.0, "Gameplay",
                      "pdz_lab_guard_stealth: hunters of a squad take different pads of the sweep, at least this far "
                      "(cm) from each other's")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_tannoy, true, "Gameplay",
                    "pdz_lab_guard_stealth: when the first guard of a squad starts hunting, the tannoy says Rare's "
                    "\"location unknown\" announcement (TANNOY_SECURITY12), not while the alarm sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_tannoy_s, 240.0, "Gameplay",
                      "pdz_lab_guard_stealth: the \"location unknown\" announcement plays at most once in this many "
                      "seconds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_investigation, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who goes to look at a lamp that went out stops beside it "
                    "and looks, then checks a few nearby spots one after another. Off = round 3 (one search at the pad "
                    "beside the lamp)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_lamp_search_s, 35.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard who goes to look at a lamp that went out gives up after this many "
                      "seconds (a look beside it, then a few nearby spots), says \"never mind\" and goes back to his post")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_body_search_s, 40.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard who finds a body searches around it alone for this many seconds "
                      "before he raises the alarm or calls his squad")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_look_s, 5.0, "Gameplay",
                      "pdz_lab_guard_stealth: an investigation's first stop, beside the lamp or body: he stands and looks "
                      "this many seconds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_inv_dwell_s, 6.0, "Gameplay",
                      "pdz_lab_guard_stealth: an investigation's later stops: he searches each nearby spot this many "
                      "seconds once he gets there")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_stealth_inv_legs, 3, "Gameplay",
                     "pdz_lab_guard_stealth: an investigation checks up to this many nearby spots after the first look")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_inv_radius_cm, 1500.0, "Gameplay",
                      "pdz_lab_guard_stealth: an investigation's spots lie within this distance (cm) of the lamp or body")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_body_rules, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who finds a body Jo left (and never saw her) searches around it "
                    "alone first; if he finds nothing an alarm user runs for the alarm, anyone else calls his squad to "
                    "search the area with him, and then they all go back to their posts wary. Off = round 3 (the finder "
                    "hunts for the rest of the level)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_call_radius_cm, 2000.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard calling his squad to a body is answered by squad mates within "
                      "this distance (cm) of the body who stand on the body's floor and have a walking route to it on "
                      "that floor no longer than 1.5 times this (pdz_lab_stealth_squad_reach); the other squad mates "
                      "stay at their posts, wary")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_squad_search_s, 60.0, "Gameplay",
                      "pdz_lab_guard_stealth: a squad called to a body searches around it this many seconds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_body_alarm_chance, 0.0, "Gameplay",
                      "pdz_lab_guard_stealth: the chance (0-1) that a guard who is not an alarm user runs for the alarm "
                      "after finding a body, instead of calling his squad")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_alarm_end, true, "Gameplay",
                    "pdz_lab_guard_stealth: the Laboratory alarm also ends by itself (as Jo's alarm switch ends it) "
                    "after pdz_lab_stealth_alarm_end_s without a guard seeing Jo; when an alarm ends the searching and "
                    "hunting guards go back to their posts, wary for the rest of the level")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_alarm_end_s, 120.0, "Gameplay",
                      "pdz_lab_guard_stealth: the alarm ends by itself after this many seconds without a guard seeing "
                      "Jo (every sighting starts the count again)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_loud, true, "Gameplay",
                    "pdz_lab_guard_stealth: loud means Rare's full game. Once the alarm is raised, Jo fires an "
                    "unsilenced shot a guard can hear, an explosion goes off in a guard's hearing or the generator "
                    "switch is thrown, the stealth code switches off for the rest of the level: every guard is Rare's "
                    "own again (his squad's warnings, his hearing of gunfire, his alarm and reinforcement waves), and "
                    "it never goes back to quiet. Off = round 10 (the stealth code stays on through an alarm)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_warn_team_cm, 1500.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard only shouts for his squad (Rare's WARN_TEAM, \"Get the hell over "
                      "here!\") when a live squad mate is within this distance (cm) to answer it; at a sighting he "
                      "says his sighting line instead")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_spot_look_min_s, 10.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard checking a lamp or a noise looks round at the spot itself for "
                      "at least this many seconds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_spot_look_max_s, 15.0, "Gameplay",
                      "pdz_lab_guard_stealth: ... and at most this many")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_spot_near_cm, 600.0, "Gameplay",
                      "pdz_lab_guard_stealth: after that look he checks at most one more spot, within this distance "
                      "(cm) of the lamp or noise, then goes back to his post, wary")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_squad_reach, true, "Gameplay",
                    "pdz_lab_guard_stealth: a squad called to a body only sends guards who can reach it (on the body's "
                    "floor, a walking route on that floor, within pdz_lab_stealth_call_radius_cm); the rest stay at "
                    "their posts, wary. Off = round 10 (everyone within the radius is sent)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_warn_lines, true, "Gameplay",
                    "pdz_lab_guard_stealth: a WARNING line (a guard's \"huh?\" at a glimpse, at Jo's steps or at a "
                    "sound, a lamp reaction, a scientist's warnings) always plays: it waits for the gap (story speech, "
                    "another speaker's line), then skips Rare's squad speech throttle and the stealth code's own line "
                    "limits, and is asked for again until it has really started. Off = round 11 (one request, which "
                    "Rare's speech may drop)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_warn_wait_s, 8.0, "Gameplay",
                      "pdz_lab_stealth_warn_lines: a warning line that could not start within this many seconds is "
                      "given up (logged as NOT PLAYED). Story speech does not count against it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_scientist_warn, true, "Gameplay",
                    "pdz_lab_guard_stealth: a calm Laboratory scientist (voices SCI1 / SCI2) who is making Jo out warns "
                    "before he runs for the alarm, in his own recorded lines: at pdz_lab_scientist_warn_1 on his meter "
                    "\"Who's there?\" / \"What was that?\" and he turns to her; at pdz_lab_scientist_warn_2 \"Is "
                    "someone creeping about?\" and a step or two towards her; at 100 \"Someone call security!\" and "
                    "pdz_lab_scientist_hesitate_s before Rare's sight (his alarm run) takes over. Each once per climb "
                    "of his meter (again after it fell below 20). Off = round 11 (silent until he runs)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_scientist_warn_1, 30.0, "Gameplay",
                      "pdz_lab_scientist_warn: the meter level of a scientist's first warning (\"huh? who's there?\")")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_scientist_warn_2, 70.0, "Gameplay",
                      "pdz_lab_scientist_warn: the meter level of a scientist's second, more alarmed warning")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_scientist_hesitate_s, 1.0, "Gameplay",
                      "pdz_lab_scientist_warn: how long a scientist hesitates after his sighting line starts, before "
                      "Rare's sight (and his alarm run) takes over. At most 3 s more while the line waits for the gap")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_scientist_warn_moves, false, // (rareplus: off; the moves used go-to-object tasks that followed Jo)
                    "Gameplay",
                    "pdz_lab_scientist_warn: the scientist also MOVES at his warnings (Rare's own go-to-object and "
                    "look-at-object task parts, as in a guard's walk to a body): he turns to Jo at the first, takes "
                    "pdz_lab_scientist_step_cm towards her at the second. Off = lines and the hesitation only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_scientist_step_cm, 200.0, "Gameplay",
                      "pdz_lab_scientist_warn_moves: how far (cm) a scientist steps towards Jo at his second warning")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_lamp_talk, true, "Gameplay",
                    "pdz_lab_guard_stealth (pdz_restore_light_speech is no longer needed for it): the scientists' "
                    "recorded lights-out conversation also plays for lamps Jo shot out, before she goes loud: a calm scientist who knows of "
                    "pdz_lab_lamp_talk_lamps dead lamps within pdz_lab_lamp_talk_cm of him (or stands where a dead "
                    "lamp shone and knows of one more) has it with a second calm scientist within 15 m (one asks, the "
                    "other answers 4 s later); alone he says his solo line. Once per scientist; only takes that suit a "
                    "few dead lamps. Off = only after the generator blackout")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_lamp_talk_cm, 800.0, "Gameplay",
                      "pdz_lab_lamp_talk: dead lamps within this distance (cm) of a scientist count for his dark area")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_lamp_talk_lamps, 2, "Gameplay",
                     "pdz_lab_lamp_talk: this many shot-out lamps (shots that put lamps out) he noticed within "
                     "pdz_lab_lamp_talk_cm make a scientist's area dark")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_dark_lamp_notice, true, "Gameplay",
                    "pdz_lab_guard_stealth: a calm guard who gets a lamp Jo shot out in view (within "
                    "pdz_lab_lamp_notice_cm, in front of him, a clear line) without having noticed it go out says so, "
                    "walks over for a look of pdz_lab_dark_lamp_look_min_s to _max_s under it and goes back to what he "
                    "was doing, calm or wary as before. Once per lamp: a lamp somebody went to look at is never "
                    "checked again. A scientist who sees one only comments. Off = round 11")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_dark_lamp_look_min_s, 5.0, "Gameplay",
                      "pdz_lab_dark_lamp_notice: the shortest look (s) under a lamp found dark")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_dark_lamp_look_max_s, 10.0, "Gameplay",
                      "pdz_lab_dark_lamp_notice: the longest look (s) under a lamp found dark")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_scientist_warn_gap_s, 1.2, "Gameplay",
                      "pdz_lab_scientist_warn: the least time (s) between the real start of a scientist's warning and "
                      "his next one (warning 1 -> warning 2 -> his sighting line), however fast his meter fills. He "
                      "stays hidden from Rare's sight while they play out (8 s at most after his meter is full)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_expecting_cm, 1000.0, "Gameplay",
                      "pdz_lab_guard_stealth: a searching guard's \"he is looking for her\" speed-up "
                      "(pdz_lab_stealth_expecting_mult) only applies while Jo is within this distance (cm) of his "
                      "eye; beyond it his meter fills at the normal rate. 0 = at any distance (round 10)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_view_crouch, true, "Gameplay",
                    "pdz_lab_guard_stealth: Jo counts as crouched for the guards' sight meter and for her footsteps "
                    "when Rare's own crouch test says so (her body's animation) OR her view is crouched (the player's "
                    "crouch, the value Rare's own gun spread reads). After she leaves cover crouched her body plays "
                    "standing animations for a few seconds while the view stays down. Every disagreement is logged "
                    "(pdz_lab_stealth: STANCE). Off = the body's animation only (round 11b)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_dark_lamp_walk, true, "Gameplay",
                    "pdz_lab_dark_lamp_notice: a guard who FINDS a lamp dark walks to it with Rare's plain go-to-pad "
                    "task (his ordinary walk, no search behaviour) and only looks round once he stands under it. Off "
                    "= round 11b (Rare's SEARCH task walks him there, gun up)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_warn_hold, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard cannot fully see Jo before his warning line has been heard: while "
                    "a warning line of his is waiting to play, and for pdz_lab_stealth_warn_hold_gap_s after it "
                    "really started, his meter fills to pdz_lab_stealth_warn_hold_meter and no further. The hold ends "
                    "8 s after the line was due if it could not play. Off = round 11c")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_warn_hold_meter, 70.0, "Gameplay",
                      "pdz_lab_stealth_warn_hold: the meter level a guard is held at (never above his sighting level "
                      "less 5: a wary guard's is 75, a hunting guard's 55)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_warn_hold_min_cm, 400.0, "Gameplay",
                      "pdz_lab_stealth_warn_hold: no hold while Jo is within this distance (cm) of his eye: face to "
                      "face he recognises her, his meter fills as without the hold (the close-range floor included). "
                      "A hold that is running ends the moment she comes that close. 0 = the hold at any distance")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_warn_hold_gap_s, 1.2, "Gameplay",
                      "pdz_lab_stealth_warn_hold: the hold lasts this long (s) after his warning line really started")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_glimpse_takes, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard's line at a GLIMPSE of Jo (sight) is only a take that fits seeing "
                    "something (HEAR_UNKNOWN_SOLO takes 1, 4, 5, 6: \"Anybody there?\", \"Huh? Weird.\", \"Whoa! What was "
                    "that?\", \"Is that you?\"; HEAR_UNKNOWN_TEAM takes 4, 6, 7: \"Shit! What was that?\", \"Wonder what "
                    "that was?\", \"What's that?\"), never one that says he HEARD something. A real sound keeps every "
                    "take. Off = round 11e (any take)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_second_death, true, "Gameplay",
                    "pdz_lab_guard_stealth: per squad, the first body found or death witnessed (without seeing Jo) gives "
                    "the search as before; a SECOND one in that squad (or a body found by a guard who already knows of "
                    "one) starts an alarm run: the finder if he is an alarm user, else he shouts and the nearest living "
                    "alarm user of his squad who can run does, else the nearest alarm user within 30 m. Off = round 11e")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_alarm_announce, true, "Gameplay",
                    "pdz_lab_guard_stealth: before Jo goes loud every alarm run is announced: he shouts first (troopers "
                    "WARN_TEAM takes 1 and 2, \"Sound the alarm!\" / \"Hit the alarm!\"; scientists WARN_TEAM, \"Someone "
                    "call security!\"), as a warning line that always plays, hesitates pdz_lab_stealth_alarm_announce_s "
                    "after the line really started, then the run starts (Rare's alarm task, interruptible as before). "
                    "Rare's own alarm runs are held back the same way. Off = round 11e")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_alarm_announce_s, 1.0, "Gameplay",
                      "pdz_lab_stealth_alarm_announce: the hesitation (s) between the real start of his shout and the "
                      "start of his alarm run")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_voice_near_cm, 1500.0, "Gameplay",
                      "pdz_lab_guard_stealth: the one-voice-at-a-time rule only applies between speakers within this "
                      "distance (cm) of each other; speakers further apart may talk at the same time. A warning line "
                      "also only waits for a line that is PLAYING within this distance (and drops lines still waiting "
                      "there). 0 = everywhere (round 11f)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_squad_combat, true, "Gameplay",
                    "pdz_lab_guard_stealth: before Jo goes loud a guard who has not himself seen her, been hit or heard "
                    "an unsilenced shot does not join a squad mate's fight at once (Rare's \"alerted by a friend\" "
                    "stimulus is taken from him): he says his alert line and goes to look where the squad mate is "
                    "fighting, his meter filling as usual if he sees her. Off = Rare's instant squad combat (round 11f)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_near_miss, true, "Gameplay",
                    "pdz_lab_guard_stealth: before Jo goes loud a guard who is shot at and missed with a SILENCED weapon, "
                    "and whose meter is under 100, does not go into combat (Rare's near-miss stimulus is taken from "
                    "him): Rare's own near-miss line plays, he is alert, searches towards where the shot came from and "
                    "his meter fills faster. A hit, an unsilenced shot or a full meter is unchanged. Off = round 11f")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_alarm_min_s, 2.5, "Gameplay",
                      "pdz_lab_stealth_alarm_announce: from the moment an alarm runner's shout really starts the alarm "
                      "cannot be raised for this many seconds, however close the panel is: raised sooner, it is held "
                      "and sounds when the time is up, if he is still alive then. 0 = off (round 11f)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_back_to_post, true, "Gameplay",
                    "pdz_lab_guard_stealth: every guard's post (where he stood when the level started) is remembered. "
                    "When a check, investigation, search or hunt of the stealth code ends, he is walked back there "
                    "with Rare's plain go-to-pad task and left to Rare's default behaviour there. No progress for "
                    "pdz_lab_stealth_stuck_s: he stays where he is, wary. Off = round 11h (Rare's default behaviour "
                    "starts wherever his search ended)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_glimpse_reach_cm, 3000.0, "Gameplay",
                      "pdz_lab_guard_stealth: a search started by a glimpse or a noise only heads for a spot the guard can "
                      "walk to: he stands on its floor and an open pad route of at most this length (cm) leads to it "
                      "(the test round 11 uses for a squad mate's way to a body). If not, he looks round where he "
                      "stands and is wary. 0 = no test (round 11i)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_closer_s, 10.0, "Gameplay",
                      "pdz_lab_guard_stealth: a walk or search of the stealth code with a target spot is given up when "
                      "his distance to the spot has not shrunk by pdz_lab_stealth_closer_cm in this many seconds "
                      "(moving, but not getting closer): back to his post, wary. Not counted once he has been within "
                      "3 m of the spot. 0 = off (round 11i)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_closer_cm, 200.0, "Gameplay",
                      "pdz_lab_stealth_closer_s: how much nearer (cm) he must get in that time")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_glimpse_search_s, 45.0, "Gameplay",
                      "pdz_lab_guard_stealth: a search started by a glimpse ends after this many seconds if he has not "
                      "confirmed her; then back to his post, wary. During it a new glimpse only restarts the time when "
                      "his meter is above pdz_lab_stealth_glimpse_extend_meter or she is within "
                      "pdz_lab_stealth_glimpse_extend_cm. 0 = no limit (round 11i: Rare's own search time)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_glimpse_extend_meter, 50.0, "Gameplay",
                      "pdz_lab_stealth_glimpse_search_s: a new glimpse restarts the time when his meter is above this")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_glimpse_extend_cm, 1500.0, "Gameplay",
                      "pdz_lab_stealth_glimpse_search_s: ... or when she is within this distance (cm) of him")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 11k.
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_loud_on_noise, false, "Gameplay",
                    "pdz_lab_guard_stealth (round 11k, default off): Jo's unsilenced shots and explosions set LOUD as in "
                    "rounds 11 to 11j. Off: LOUD only comes from the level alarm or the generator switch; her shots and "
                    "explosions are noise the guards who hear them go to (pdz_lab_stealth_gunfire_hearing)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_gunfire_hearing, true, "Gameplay",
                    "pdz_lab_guard_stealth (round 11k): before LOUD, gunfire and Rare's sound events (explosions) are "
                    "heard only when the line from the listener's ears to the source is clear of geometry with Rare's "
                    "block-AI-sound flag (collision mask 0x40, both ways) and within his zone-hop limit, besides "
                    "Rare's own distance test. A guard who hears a guard's gunfire joins in (Rare's reaction, pointed "
                    "at the shooter, with a HEAR_GUNFIRE line); one who hears Jo's unsilenced shot or an explosion "
                    "without seeing her goes straight to the spot with a HEAR_UNKNOWN line. Off: Rare's distance-only "
                    "hearing and round 11j's reactions")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_noticed_takes, true, "Gameplay",
                    "pdz_lab_guard_stealth (round 11k): the line fits what was noticed. A trooper's glimpse warning "
                    "plays PLAYER_CLOAKED_NEXT_SOLO takes 1, 2, 3, 4, 6 (\"Must be seeing things.\", \"Thought I saw "
                    "something!\", \"Don't know what it is, but something's moving.\", \"Hey! Ah, just a trick of the "
                    "light.\", \"There's someone here, I can feel it.\"); a wary call-out plays "
                    "PLAYER_CLOAKED_NEXT_TEAM takes 3, 6 and (TROOPER1 / 2) 4 with a squad mate near (\"I got a bad "
                    "feeling about this.\", \"Okay, don't anybody make any sudden moves.\", \"Stay alert, people.\"), "
                    "else PLAYER_CLOAKED_NEXT_SOLO takes 1 and 6. HEAR_UNKNOWN only for something heard. Off: round "
                    "11j's HEAR_UNKNOWN takes")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_voice_set_s, 30.0, "Gameplay",
                      "pdz_lab_guard_stealth (round 11k): a take is not played again by the same voice set (TROOPER1-3, "
                      "SCI1-2, WOMAN1-2) within this many seconds: another take of the cue is picked, or the line is "
                      "not said when none is left (warning, alert, pain and death lines always get a take: the one "
                      "said longest ago). 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_voice_set_wait, true, "Gameplay",
                    "pdz_lab_guard_stealth (round 11k): two speakers with the same voice set never speak at once: the "
                    "second one's line waits (up to 6 s) until the first has ended. A warning or alert line goes "
                    "first among those waiting; a line that has started is never cut")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_sighting_combat, true, "Gameplay",
                    "pdz_lab_guard_stealth (round 11k): when a trooper's sighting is confirmed, Rare's \"alert now\" "
                    "input is raised for him at once (COMBAT on his script's next update instead of after Rare's own "
                    "sight delay), his first-contact line (SEE_PLAYER) is asked for in that same frame and is exempt "
                    "from the fight's cap on lines. His first shot is logged against the start of that line")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_loud_alarm_announce, true, "Gameplay",
                    "pdz_lab_guard_stealth (round 11k): after LOUD too, an alarm run Rare starts is announced: the "
                    "shout (WARN_TEAM, played on him), pdz_lab_stealth_alarm_announce_s of hesitation, the run kept "
                    "until the alarm is raised or he dies, and no alarm before pdz_lab_stealth_alarm_min_s after the "
                    "shout. Off: after LOUD Rare's alarm runs are his own")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_scientist_stuck_s, 10.0, "Gameplay",
                      "pdz_lab_guard_stealth (round 11k), before and after LOUD: a scientist on an alarm run who has "
                      "moved less than pdz_lab_scientist_stuck_cm in this many seconds gives up and cowers (Rare's own "
                      "cower start). 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_scientist_stuck_cm, 100.0, "Gameplay", "pdz_lab_scientist_stuck_s: the distance (cm)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_loud_log_s, 5.0, "Diagnostics",
                      "pdz_lab_stealth_log (round 11k): after LOUD, one line per live guard and scientist this often "
                      "(s): his flags, task stack, pad, how far he moved, STUCK under 1 m. 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 11l.
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_sight_log, true, "Diagnostics",
                    "pdz_lab_stealth_log (round 11l): for every calm tracked guard and scientist, the heading of the "
                    "facing vector Rare passes to his sight test against his body's yaw, every 10 s and whenever the "
                    "difference between the two changes by more than 15 degrees (an idle look-around shows as a "
                    "moving difference when the vector follows his head)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_siren_retry, true, "Gameplay",
                    "Laboratory (round 11l): while the alarm is on, a siren the sound system refused to make when the "
                    "alarm went on (it was out of hearing then; Rare's script only tries in that one frame), or whose "
                    "sound has ended, is made again with Rare's own calls, twice a second, until it plays")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3, Sun 15 Nov (L, item 9 (c)): off by default: the question's take is XACT's own random pick every time.
REXCVAR_DEFINE_BOOL(pdz_lab_mesa_first, false, "Gameplay",
                    "Laboratory (round 11l): the first time the scientists' lights-out conversation plays in a run, "
                    "SCI1's question is take 3 (\"this never used to happen in Mesa\"); after that XACT's own pick. "
                    "Off by default: every time XACT's own random pick of the takes allowed")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// "Rare's rules plus" (Sat 14 Nov).
REXCVAR_DEFINE_BOOL(pdz_lab_rareplus, true, "Gameplay",
                    "Laboratory, RARE'S RULES PLUS (default on): Rare's own AI for everything (sight once the meter is "
                    "full, hearing, alarms, reinforcement waves, combat, searching). Off for good: the LOUD switch, "
                    "every stimulus taken away from Rare, our investigations / hunts / sweeps / squad searches / "
                    "escalation tiers / alarm-end / body rules, the scientists' moves and our tannoy. Kept on top of "
                    "Rare: the light detection meter with its two sight lines and a look round where he stands, no "
                    "instant squad knowledge, walls block sound, the alarm run's announcement, the lamp checks, the "
                    "chatter and voice rules, the fixes and logs. Off = rounds 3 to 11l as they were")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_no_squad_knowledge, true, "Gameplay",
                    "pdz_lab_rareplus: a guard only learns where Jo is from his own sight or hearing. The \"alert now\" "
                    "input that every COMBAT start (821CBF48) raises on the friends in range is taken off again, and "
                    "the squad's shared target position / pad / zone (8217C540) is only written for a member who "
                    "himself sees a valid target")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Stealth rollout (Sun 15 Nov): the same "Rare's rules plus" layer on other levels, inside a zone made from Rare's own
// level data (kStealthLevels below). A level with an empty zone, or with its switch off, is Rare's game unchanged.
// The Laboratory keeps pdz_lab_rareplus (its zone is the whole level). Counter-Operative: off on every level.
REXCVAR_DEFINE_BOOL(pdz_stealth_nightclub, true, "Gameplay",
                    "Nightclub: the stealth layer (pdz_lab_rareplus's meter, no squad knowledge, walls block sound, "
                    "announced alarm run, lamp lures, body lines, chatter) for the guards of the docks zone only "
                    "(kStealthLevels in lab_stealth.cpp); everyone else is Rare's. Off = Rare's game")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_subway, true, "Gameplay",
                    "Subway: the stealth layer for the guards of the stealth section after the garage and the subway "
                    "(kStealthLevels in lab_stealth.cpp); the garage battle and everyone else are Rare's. Off = Rare's "
                    "game")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_mansion, true, "Gameplay",
                    "Mansion: the stealth layer for the guards of the grounds outside (kStealthLevels in "
                    "lab_stealth.cpp); inside is Rare's. Off = Rare's game")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_light, true, "Gameplay",
                    "Stealth layer zones whose row asks for it (the Nightclub docks): the meter fills slower where Jo "
                    "stands in less light (the Laboratory's light measure, every scene lamp's falloff and the ambient; "
                    "needs pdz_lab_dark_patches as on the Laboratory). Only the meter: no local darkness, no lamp "
                    "chains, Rare's sight range unchanged. Off = the meter by distance, movement, crouch and view only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Mansion helper (Sun 15 Nov): the moon on the Mansion grounds (the row's moon term).
REXCVAR_DEFINE_BOOL(pdz_stealth_moon, true, "Gameplay",
                    "pdz_stealth_light on the Mansion grounds: the moon counts in the light at Jo where a ray from her "
                    "towards the moon is not blocked (Rare's AI sight ray), so open moonlit ground reads lit and the "
                    "shadow of a wall, tower or roof reads dark. Off = the candles and the ambient only (the grounds "
                    "then read almost entirely dark)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_log, false, "Diagnostics",
                    "Stealth layer zones: on the Laboratory, Nightclub, Subway and Mansion, one line at level load "
                    "(the layer on or off and why, and the zone: squads, ids, never-ids, voices), one line per "
                    "character the first time he is seen (setup id, setup data, voice set, pad, Rare's AI zone, squad, "
                    "role, position, under the layer or not and why), one line when Jo enters or leaves a zone, one "
                    "line when Jo's pad zone number changes, and the Mansion's radio calls. Off by default")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Follow-up (Sun 15 Nov): the lamp escalation as its own switch. Under pdz_lab_rareplus it was only a log line
// ("ESCALATION: X was killed while he looked at a lamp ... nobody walks up alone in this area (tier 2)"): the two
// rules that read it (the body tier in NoticeBody, the squad search of a lamp in LightIncident) are rounds 3 to 11l's
// and off under rareplus. With this switch on it acts under rareplus too, in the one place that fits Rare's rules
// plus: the lamp lure's walk.
REXCVAR_DEFINE_BOOL(pdz_stealth_lamp_escalation, false, "Gameplay",
                    "Stealth layer, a balance rule against \"lure with a lamp, kill, repeat\" (off by default: until "
                    "now it was only a log line). On: once a guard has been killed while he looked at a lamp that "
                    "went out, for pdz_lab_stealth_tier_s (300 s) nobody walks up alone to another lamp that goes out "
                    "within pdz_lab_stealth_tier_area_cm (25 m) of that lamp: the guards who notice it stay where "
                    "they are, say their lamp line, look round and are alert (their meters fill faster for "
                    "pdz_lab_stealth_alert_s). Nothing else changes: no squad search, no alarm. Needs "
                    "pdz_lab_stealth_tiers (on)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_rareplus_second, 70.0, "Gameplay",
                      "pdz_lab_rareplus: the meter level of a guard's second, stronger sight line (\"There's someone "
                      "here, I can feel it.\"); never above his sighting level - 5")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_rareplus_episode_s, 15.0, "Gameplay",
                      "pdz_lab_rareplus: after this many seconds without a glimpse of her a guard's two sight lines "
                      "may be said again")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_alarm, "", "Testing",
                      "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, \"on...\" raises the alarm and "
                      "\"off...\" switches it off, with Rare's own alarm calls (those of the alarm switch). Applied once "
                      "per new value (on1, off1, on2, ...)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_find_body, "", "Testing",
                      "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, the live tracked guard with this "
                      "setup id finds the nearest body Jo left, as if Rare's sight had shown it to him (Rare's "
                      "SEE_BODY line, then the body rules). Applied once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_test_stealth_search_s, 0.0, "Testing",
                      "TESTING TOOL: with pdz_lab_guard_stealth, a tracked guard's SEARCH ends after this many seconds "
                      "instead of Rare's 180 (0 = Rare's)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_lab_stealth_death_rules, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who sees or hears a friend die without seeing Jo does not go into "
                    "combat (Rare gave the whole squad her position): the nearest one searches round the spot alone, as "
                    "for a body, the others stay alert. Rare's WARNER warns his squad only once he has seen Jo. Off = "
                    "round 4 (Rare's reaction)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_noise_rules, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who only hears something (gunfire, steps) goes to look at the sound "
                    "alone, spot by spot, as for a lamp; nobody runs for the alarm who has not seen Jo, except a guard "
                    "who found a body and searched round it. Off = round 4 (Rare's reaction: an alarm user runs for the "
                    "alarm at any sound)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_alert_s, 30.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard who noticed something but does not go to look (a lamp, a death, a "
                      "sound another guard investigates) stays alert this many seconds: his meter fills faster")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_alert_rate, 2.0, "Gameplay",
                      "pdz_lab_guard_stealth: an alert guard's meter fills this many times faster")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_alert_look_s, 4.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard who becomes alert stops where he stands and looks round (Rare's "
                      "search look) for this many seconds; 0 = no look")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_body_sight, true, "Gameplay",
                    "pdz_lab_guard_stealth: every guard who is not fighting (walking, wary, searching, investigating, "
                    "hunting) notices a body Jo left when it is in his view with a clear line of sight, once per body: "
                    "nearer in the dark, as for Jo; then the body rules. Off = round 4 (only Rare's own body sight, "
                    "which searching guards do not run)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_body_range_cm, 2500.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard notices a body within this distance (cm) in full light; in the "
                      "dark the distance shrinks as his sight of Jo does")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_lamp_sight, true, "Gameplay",
                    "pdz_lab_guard_stealth: only guards who could see a lamp that goes out react to it (eye to lamp, "
                    "walls, floors and doors in the way block it), or guards within pdz_lab_stealth_lamp_hear_cm with an "
                    "open way to it (they hear the pop). Off = round 4 (everyone within 25 m)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_lamp_hear_cm, 1200.0, "Gameplay",
                      "pdz_lab_guard_stealth: a lamp that breaks is HEARD by a guard or scientist within this distance "
                      "(cm) of it, whatever way he faces; walls and floors block it (Rare's block-AI-sound geometry on "
                      "the line from the lamp to his ears). 0 = off (round 10: sight only)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_stealth_lamp_investigators, 8, "Gameplay",
                     "pdz_lab_guard_stealth: at most this many of the guards who noticed a lamp go out walk over to look "
                     "(round 11b: 8, which is every guard who noticed it; each takes his own spot under it); the "
                     "others stop, look round and stay alert")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_lamp_spree, true, "Gameplay",
                    "pdz_lab_guard_stealth: a third lamp going out within 60 s and 20 m of two others counts like a "
                    "body: the guards of that area search it together for pdz_lab_stealth_squad_search_s, then stay wary")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_scientist_lamps, true, "Gameplay",
                    "pdz_lab_guard_stealth: a scientist who could see a lamp go out says one of Rare's unused "
                    "LIGHT_SHOT_OUT lines and is alert for a while (he never goes to look)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_scientist_spook, false, "Gameplay",
                    "pdz_lab_guard_stealth: a second lamp going out near a scientist within 30 s sends him for the alarm "
                    "(his alarm-user role). Off by default (to try)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_all_clear, false, "Gameplay",
                    "pdz_lab_guard_stealth: after the tannoy's \"cannot locate the intruder\" (an alarm that ended by "
                    "itself), one trooper says Rare's \"All clear, move out!\" as they go back to their posts. Off by "
                    "default (to try)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_story_first, true, "Gameplay",
                    "pdz_lab_guard_stealth: while one of Rare's scripted Laboratory lines plays (Jo, Chandra, the "
                    "scripted scientists, the tannoy), guards who are not fighting hold their mutters, comments and "
                    "search lines, and our own tannoy lines wait")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_lost_metered, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who has lost contact (and one whose combat Rare keeps running "
                    "though he has not seen Jo since) sees her through the meter again, at the hunters' level, instead "
                    "of Rare's instant sight: he no longer seems to know where she is. Off = round 4")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_gunfire_rules, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who hears an UNSILENCED shot or an explosion (within Rare's own "
                    "hearing range) reacts as Rare has it: an alarm user runs for the alarm at once. A guard who hears a "
                    "SILENCED shot (or any other sound) goes to look at it alone, like a lamp, and no alarm run follows "
                    "unless he finds a body or sees Jo. Off = every sound is looked into alone (no alarm run without "
                    "a sighting)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_lamp_front, "", "Testing",
                      "TESTING (pdz_lab_guard_stealth): \"id,cm[,side]\": a lamp incident (no lamp is touched) that many "
                      "cm straight in front of the named guard at his eye height (side: cm to his right; negative cm = "
                      "behind him)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_lamp, "", "Testing",
                      "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, \"x,y,z\" (cm; any text after a "
                      "fourth comma makes a new value): the guards react as if a lamp had just gone out at that point "
                      "(no lamp is touched). Applied once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_sound, "", "Testing",
                      "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, \"<setup id>,loud\" / \",silenced\" / "
                      "\",explosion\": the live tracked guard with that id hears such a sound at Jo's pad (Rare's own "
                      "heard-a-sound input for a third of a second; the kind is given to the gunfire rule directly). "
                      "Applied once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_body_walk, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who investigates a body first walks to the body itself (the game's own "
                    "walk-to-body task) and looks round beside it; false: he goes to the nearest pad instead")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_same_floor, true, "Gameplay",
                    "pdz_lab_guard_stealth: every spot of a body, sound, lamp-spree or squad search is on the same floor "
                    "as the thing searched for (height within 1 m, a walking route that stays on that floor); a body's "
                    "finder checks at most pdz_lab_stealth_body_spots spots within pdz_lab_stealth_body_spot_cm, and "
                    "with no such spot he stays at the body and looks round again. false: the round 5 choice of spots")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_stealth_body_spots, 2, "Gameplay",
                     "pdz_lab_guard_stealth: how many spots round a body its finder checks after his look beside it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_body_spot_cm, 800.0, "Gameplay",
                      "pdz_lab_guard_stealth: how far from the body those spots may be (cm, on the body's floor)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_body_lines, true, "Gameplay",
                    "pdz_lab_guard_stealth: a guard who notices a body says SEE_BODY_SOLO before any other guard line that "
                    "is not part of a fight (it waits for the line playing and for scripted speech, and is tried again "
                    "for pdz_lab_stealth_body_line_s seconds); his squad call after the search is SEE_BODY_TEAM (\"Got a "
                    "man down here!\"), with the round 5 call as the fallback; a witness's FRIEND_DIES line never "
                    "overlaps another guard line. false: the round 5 lines")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_body_line_s, 8.0, "Gameplay",
                      "pdz_lab_guard_stealth: how long a body line is tried again while other speech holds it back")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_lamp_notice_cm, 1100.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard notices a lamp going out when he stands in its light (within the "
                      "lamp's own reach, facing any way), or when it is within this distance, in front of him (inside "
                      "his view cone) and he has a line of sight to the fitting, the point under it or the floor it "
                      "lit. Otherwise not at all")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_lamp_notice_scientist_cm, 800.0, "Gameplay",
                      "pdz_lab_lamp_notice_cm for the scientists")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_lamp_comment_s, 30.0, "Gameplay",
                      "pdz_lab_guard_stealth: a scientist comments on a lamp going out at most once in this many seconds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_expecting_mult, 2.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard who is looking for Jo at her own pad (he glimpsed or heard her) "
                      "fills his detection meter this many times as fast when he sees her")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_alert_lines, true, "Gameplay",
                    "pdz_lab_guard_stealth: the one-voice rule is for chatter only. Alert lines (a sighting SEE_PLAYER*, "
                    "Rare's WARN_TEAM call as an alarm run starts, BEEN_SHOT / HURT / PAIN, the body call-in "
                    "SEE_BODY_TEAM) always play: over any chatter, and the ambient generic tannoy does not hold them "
                    "back (story speech still comes first; the fight's cap on lines at once stays). Off = round 9")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_footsteps, false, "Gameplay",
                    "pdz_lab_guard_stealth: OFF by default since round 11d (it felt unfair in play): before she goes "
                    "loud Jo's footsteps never alert anyone; guards notice her by sight, lamps, unsilenced gunfire "
                    "and bodies only. On = calm Laboratory guards hear Jo's footsteps. Rare's game already makes the "
                    "noise and marks the guards who are within it, but nothing reacts. With this on a calm guard hears "
                    "her steps: walking (the stick at half or less) within pdz_lab_footsteps_walk_cm, running within "
                    "pdz_lab_footsteps_run_cm, crouching never. After about a second of steps in his hearing he turns, "
                    "says \"Did you hear something?\" and checks the spot (steps alone never start a fight or an "
                    "alarm). Not through walls or floors. Off = Rare's original")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_footsteps_run_s, 0.6, "Gameplay",
                      "pdz_lab_footsteps: a guard's hearing meter is full after this many seconds of Jo RUNNING within "
                      "pdz_lab_footsteps_run_cm of him. It fills every frame she moves there, not per footstep sound")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_footsteps_walk_s, 1.1, "Gameplay",
                      "pdz_lab_footsteps: ... and after this many seconds of Jo WALKING within "
                      "pdz_lab_footsteps_walk_cm of him")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_footsteps_drain, 3.0, "Gameplay",
                      "pdz_lab_footsteps: a guard's hearing meter (full at pdz_lab_stealth_notice, 20) drains by this "
                      "much a second while Jo is out of range, still, crouching or behind a wall")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_footsteps_walk_cm, 100.0, "Gameplay",
                      "pdz_lab_footsteps: Jo's walking steps are heard within this distance (cm)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_footsteps_run_cm, 400.0, "Gameplay",
                      "pdz_lab_footsteps: Jo's running steps are heard within this distance (cm)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_footsteps_walk_stick, 0.5, "Gameplay",
                      "pdz_lab_footsteps: Jo walks while her movement stick (Rare's own move value, 0..1) is at this "
                      "or less; beyond it she runs. A keyboard's movement keys are a full stick: running")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_lab_footsteps_wall, 0, "Gameplay",
                     "pdz_lab_footsteps: what stops her steps being heard, besides Rare's zone-hop limit (always "
                     "applied): 0 = geometry carrying Rare's block-AI-sound flag on the line from his ears to her feet "
                     "(the Laboratory's walls and floors carry it), 1 = also anything that blocks AI sight on that line")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_behind, "", "Testing",
                      "TESTING TOOL: \"id,distance[,height]\" (e.g. s1m1,150): player 1 is put that many cm behind "
                      "the live tracked Laboratory guard with this setup id (opposite to his facing), on his floor "
                      "(plus the height, cm; e.g. -700 for the floor below a walkway guard), looking at his back, "
                      "with Rare's own move. Once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_test_footsteps_radius, 0.0, "Testing",
                      "TESTING TOOL: pdz_lab_footsteps uses this radius (cm) instead of Rare's 200 / 400, to test the "
                      "wall and floor rules from farther away; 0 = Rare's")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_test_stealth_blind, false, "Testing",
                    "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, tracked guards never see Jo (Rare's "
                    "sight test of her answers \"not visible\"), so that hearing can be tested on its own")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_test_footsteps_trace, false, "Testing",
                    "TESTING TOOL: pdz_lab_footsteps logs, twice a second for each calm guard within 12 m of Jo, her "
                    "footstep noise as Rare's code holds it, her speed and his distance")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_footsteps_probe, "", "Testing",
                      "TESTING TOOL: \"x1,y1,z1,x2,y2,z2\" (cm): logs whether the line between the two points is "
                      "blocked for AI sight (collision mask 0x20) and for AI sound (0x40). Once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_test_lights_out_take, 0, "Testing",
                     "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, the next SCI1 lights-out QUESTION "
                     "that plays uses this take of its five (3 = \"this never happened in Mesa\") instead of XACT's "
                     "random pick. Once per new value; 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_search_fallback, true, "Gameplay",
                    "Laboratory (part of the stealth layer: off with pdz_lab_guard_stealth): a guard in one of Rare's own "
                    "searches who stands because Rare's pick finds him no pad (none of his own zone over 10 m away, or "
                    "a walk that ends at once) is given one: a pad of his zone at a shorter distance, else one of a "
                    "linked neighbouring zone, over Rare's pad links and not claimed by a squad mate. He then walks "
                    "as Rare's searchers do; Rare's 180 s and what follows are unchanged. Off = Rare's original (he "
                    "stands for the 180 s)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_tiers, true, "Gameplay",
                    "pdz_lab_guard_stealth: escalation tiers. The first incident in an area is looked at by one guard "
                    "alone. After that (a second body or death in the same area, or an investigator who was killed "
                    "there), and for a body or death seen from a distance, nobody walks up alone: the guard who sees "
                    "it reports it (Rare's own lines) and his squad searches the area together. Off = round 6 (every "
                    "body is walked up to)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_tier_far_cm, 1000.0, "Gameplay",
                      "pdz_lab_stealth_tiers: a body or death seen from farther than this is reported, not walked up to")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_tier_area_cm, 2500.0, "Gameplay",
                      "pdz_lab_stealth_tiers: incidents within this distance of each other are in the same area")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_tier_s, 300.0, "Gameplay",
                      "pdz_lab_stealth_tiers: how long an incident counts for its area (seconds)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_close_cm, 200.0, "Gameplay",
                      "pdz_lab_guard_stealth: Jo within this distance in front of a guard (inside 60 degrees of his "
                      "facing) who can see her is noticed at once, however dark it is. 0 = off (round 6)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_mid_cm, 450.0, "Gameplay",
                      "pdz_lab_guard_stealth: Jo within this distance in front of a guard who can see her fills his "
                      "meter at pdz_lab_stealth_mid_floor of the full speed at least, however dark it is. 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_mid_floor, 0.5, "Gameplay",
                      "pdz_lab_guard_stealth: the least exposure (0..1; 1 = full speed, the meter full in half a second) "
                      "within pdz_lab_stealth_mid_cm in front of a guard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_stealth_stuck_s, 2.0, "Gameplay",
                      "pdz_lab_guard_stealth: a guard on one of the stealth code's walks (to a body, a lamp, a noise, a "
                      "squad search spot) who moves less than 1 m in this many seconds gives that spot up at once and "
                      "goes back to his post, wary (a sweep leg: he looks round where he stands and carries on). "
                      "Counted from 4 s after the walk began (Rare's turn and start animation). 0 = off (round 6)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_stealth_stack_guard, true, "Gameplay",
                    "pdz_lab_guard_stealth: crash guard for the guards' task stacks (10 tasks at most, Rare's limit): no "
                    "new search is handed to a guard whose stack is deep or who still has a search under another task, "
                    "and a task that would not fit is refused the way Rare refuses one. Off = round 4 (it crashed)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_test_stealth_tracking_off, false, "Testing",
                    "TESTING TOOL: the stealth code's tannoy rules treat the Laboratory's tracking system as destroyed "
                    "(Rare's own state is not touched)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_test_stealth_cut_s, 0.0, "Testing",
                      "TESTING TOOL (crash stress): with pdz_lab_guard_stealth in the Laboratory, every this many seconds "
                      "each tracked guard's running SEARCH is cut wherever he is (mid-walk too), as a glimpse or the "
                      "alarm's calm-down cuts it, and each hunter / investigator gets a glimpse of Jo's pad; 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_test_stealth_push_log, false, "Testing",
                    "TESTING TOOL: log the first 400 tasks stacked on tracked Laboratory guards (class, caller, the "
                    "stack)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_test_stealth_no_stand_rule, false, "Testing",
                    "TESTING TOOL (crash stress): a SEARCH may be cut anywhere, without round 3's rule (at its pad or "
                    "standing still)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_test_stealth_no_alarm_run, false, "Testing",
                    "TESTING TOOL: in the Laboratory no character starts Rare's alarm run (the alarm-run builder "
                    "sub_821DF4A0 answers \"no task\", as it does when Rare's own gate refuses), so lose contact can be "
                    "tested; off = Rare's alarm runners")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(pdz_test_stealth_kill, "", "Testing",
                      "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, kill the live tracked guard with this "
                      "setup id (e.g. s1m5) as if Jo had: Rare's own Kill (guard vtable +0x2E4, death type 5, all his "
                      "health as damage) with Jo as the killer, and the stealth code's kill bookkeeping (witnesses, the "
                      "body). Applied once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_pin, "", "Testing",
                      "TESTING TOOL: with pdz_lab_guard_stealth in the Laboratory, the live tracked guard with this "
                      "setup id is held where he stands (his position is put back every frame), so that a walk of his "
                      "makes no progress (the no-progress fallback's test). Empty = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_test_stealth_observe, false, "Testing",
                    "TESTING TOOL: in the Laboratory, once a second each live trooper's position, pad, zone, task "
                    "stack and SEARCH task are logged (pdz_lab_stealth: observe lines), with pdz_lab_guard_stealth on "
                    "or off. It changes nothing: for comparing Rare's own behaviour with the stealth code's")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_821DF4A0); // alarm-run builder (r3 character): the alarm task, or 0 (821DF524) when Rare's gate refuses
REX_EXTERN(__imp__sub_82580508); // NPC sight update (r3 character, r4 force a full pass); NPC vtable +0x360
REX_EXTERN(__imp__sub_82189F18); // one viewer / target sight test (see the header); returns r3 = visible
REX_EXTERN(__imp__sub_821CC340); // COMBAT script update (r3 script: +0x0C status, +0x10 character, +0x24 scheduler)
REX_EXTERN(__imp__sub_821CBF48); // COMBAT script start (r3 script): alert latch, radius cascade, squad target copy
REX_EXTERN(__imp__sub_821DF9E0); // SEARCH task update (r3 task: +0x10 character, +0x20 done, +0x78 start time)
REX_EXTERN(__imp__sub_821CBE88); // COMBAT script destructor (r3 script, r4 delete flag)
REX_EXTERN(__imp__sub_821DF7C0); // SEARCH task start (r3 task): +6272 0x40000000, animation 107, SEARCH_START
REX_EXTERN(__imp__sub_821CF7F8); // sequence task start (vtable 820929F0, type 0x16): the investigate walk / look
REX_EXTERN(__imp__sub_821CF228); // sequence task destructor (r3 task, r4 delete flag)
REX_EXTERN(__imp__sub_821DF110); // alarm task start (class 8209339C, ctor sub_821DEE58; r3 task, +0x10 character)
REX_EXTERN(__imp__sub_8262AA40); // say-event (r3 character + 0x3190, r4 event, r5 1 = female subject, r6, f1)
REX_EXTERN(sub_8262AA40);
REX_EXTERN(__imp__sub_8262A4E0); // speech update (r3 speech, f1 dt): a queued request (+216) becomes a speech task (+64)
REX_EXTERN(__imp__sub_821AEA48); // speech task update (r3 task: +16 character, +52 event, +68 delay, +88 bit 1 = started)
REX_EXTERN(__imp__sub_82152E20); // squad speech arbiter (r3 squad speech, r4 character, r5 event): 0 = go
REX_EXTERN(sub_82153038);        // event -> its line group
REX_EXTERN(__imp__sub_8262CCB8); // stop the speaker's sound (r3 speech; +4 the sound)
REX_EXTERN(sub_8262CCB8);
REX_EXTERN(sub_821433D8); // set a weak object holder (r3 holder, r4 object or 0): Rare's target writes 82580A78, clear 821CC414
REX_EXTERN(sub_824FE928); // crouch test (r3 character): Rare's concealment 0.5 uses it (825A93D4)
REX_EXTERN(sub_8217D108); // a character's squad (r3 squad manager [[0x82D6BFF4]+0x2F4], r4 character)
REX_EXTERN(sub_8239D740); // allocate (r3 bytes, r4 tag)
REX_EXTERN(sub_825F3978); // round 3c (B): a hit on a light surface (r3 = &hit position, r4 = prop or 0); hooked in lab_light_shot_log.cpp
REX_EXTERN(sub_821DF550); // build a SEARCH task in place (r3 memory, r4 pad or 0, r5..r7 0); returns it
REX_EXTERN(sub_821712D8); // hand a task to a character's scheduler (r3 character + 0x18D4, r4 task)
REX_EXTERN(sub_82202D20); // cue name -> cue reference (r3 ref, r4 name); ref +256 < 0: not in the loaded banks
REX_EXTERN(sub_822014B8); // queue a 2D line (r3 ref, f1, f2, r6, r7, r8): the Laboratory tick's tannoy call
REX_EXTERN(sub_8252DE88);
REX_EXTERN(__imp__sub_82C2BD80); // XACT variation pick (r3 variation list; [r3+8] -> count, last index +4) -> r3 index
REX_EXTERN(sub_82171210);
REX_EXTERN(__imp__sub_82171210);  // run a task on top of the current one (r3 scheduler, r4 task): queued, pushed by the update
REX_EXTERN(sub_82171F80);         // end and free a task (r3 scheduler, r4 task): Rare's refusal path in sub_82171210
REX_EXTERN(sub_82259B80);         // physics ray (r3 mask, r4 from, r5 to, r6 visitor, r7 1 = one-sided); r3 = hit
// Round 6: Rare's walk to a body (the SEE_BODY reaction 821D1298-821D1464), built from its own parts.
REX_EXTERN(__imp__sub_82559318);  // corpse constructor (r3 corpse, r4 the dead character, r5 managed flag)
REX_EXTERN(sub_8218B340);         // nearest object of a type in a perception profile's visible list (r3 slot, r4 type, r5 0)
REX_EXTERN(sub_82151210);         // an object's position (r3 object) -> r3 vec3
REX_EXTERN(sub_821784F0);         // allocate a small task (r3 bytes)
REX_EXTERN(sub_8217CE38);         // squad: claim a search pad for a member (r3 squad, r4 character, r5 pad)
REX_EXTERN(sub_825AD148);         // move a character (r3 character, r4 &position, r5 0): the test tools
REX_EXTERN(sub_825AD3B0);         // a human's look angles (r3 character, f1 pitch, f2 yaw; radians)
REX_EXTERN(sub_821A3438);         // go-to-pad task constructor (r3 memory, r4 pad, r5..r8 0)
REX_EXTERN(sub_8217F0F0);         // run a child task in a task's own scheduler (r3 [task + 40], r4 the child)
REX_EXTERN(sub_821CA7E0);         // composite task constructor (r3 memory, r4..r6 0)
REX_EXTERN(sub_821A9D60);         // its animation part (r3 memory, r4 the animation record)
REX_EXTERN(sub_821A1538);         // its look-at-object part (r3 memory, r4 object, r5 1, r6 0, r7 0)
REX_EXTERN(sub_821D5678);         // its go-to-object part (r3 memory, r4 object, r5 1, r6 1, f1 distance, r8..r10 0)
REX_EXTERN(sub_821CA8C0);         // composite: set its parts (r3 composite, r4 go-to, r5 look-at, r6 animation, 0)
REX_EXTERN(sub_821DF340);         // Rare's alarm-run gate (r3 character): a squad mate going, alarm on, target near -> 0
REX_EXTERN(sub_821DF4A0);         // the alarm-run builder (hooked below for the test switch)
REX_EXTERN(sub_821FF3C8);         // play a cue on a character (r3 cue reference, r4 0, r6 character, r7 1, f1 0)
REX_EXTERN(sub_8214EDF0);         // level alarm OFF (r3 level manager): Rare's alarm switch when the alarm is on
REX_EXTERN(sub_8214EBD0);         // level alarm ON (r3 level manager): the switch when it is off
REX_EXTERN(__imp__sub_8214EBD0);  // (hooked below, round 11g: not before the runner's shout has been heard)
REX_EXTERN(__imp__sub_82142AD8);  // spawn a character from a setup by name (r3 name, r4 position, r5 pad, r6 vector)
REX_EXTERN(sub_822061C8);         // DarkAudioSound::Play (r3 sound, f1 delay)
REX_EXTERN(sub_821FF428);         // a positioned sound from a cue reference (r3 ref, r4 -> position, r5 0, r6 0): the sound or 0
REX_EXTERN(sub_82519050);         // a character's transform from a skeleton node (r3 character, r4 node + 1088, r5 -> matrix; 82545390)
REX_EXTERN(sub_8217FF00);         // a task's own scheduler: end its running child tasks (r3 [task + 40]; 82319E98)
namespace lab_cut // lab_cut_content.cpp (round 11k)
{
  uint32_t SciTaskOf(const uint8_t *base, uint32_t character);
  bool AlarmRunning(const uint8_t *base, uint32_t character);
  void SayOnCharacter(PPCContext &ctx, uint8_t *base, uint32_t character, const char *cue);
} // namespace lab_cut
REX_EXTERN(__imp__sub_822061C8); // DarkAudioSound::Play (r3 sound, f1 delay); +340 state: 2 prepared, 3 delayed, 4 playing // guard vtable +0x2E4: Kill(r3 guard, r4 killer, r5 death type, r6 apply damage) (counter_op.cpp)

namespace
{
  constexpr uint32_t kAiClock = 0x82E2F334;        // float seconds, per level
  constexpr uint32_t kPlayerOne = 0x82E3C3E4;      // player 1's character
  constexpr uint32_t kLevelState = 0x82D6BFF4;     // [0x82D6BFD8 + 0x1C]: +0x2F4 squad manager, +0x2D8 -> +0x54 alarm on
  constexpr uint32_t kProfileTable = 0x82E5D590;   // perception profiles, 0x50 bytes; +0x4C heard pad
  constexpr uint32_t kProfileSlots = 171;
  constexpr uint32_t kPosition = 48;               // vec3, cm
  constexpr uint32_t kData = 0x54;                 // -> setup data; +0x34 -> setup id string
  constexpr uint32_t kPad = 0xB4;                  // a character's current pad
  constexpr uint32_t kProfileSlot = 0x354;
  constexpr uint32_t kStimuli = 0x1874;            // +6260 per-frame inputs (0x4 heard a sound event)
  constexpr uint32_t kSight = 0x187C;              // +6268 (0x8 leave combat, 0x800000 valid target seen)
  constexpr uint32_t kConfig = 0x1880;             // +6272 (0x1000 soft alarm, 0x40000000 searching)
  constexpr uint32_t kAiFlags = 0x1884;            // +6276 (0x2 dark aim: the blackout reached him)
  constexpr uint32_t kTargetPad = 0x1830;          // the target's pad at his last sighting (82580D00)
  constexpr uint32_t kLastSeen = 0x30F8;           // double, AI clock of his last sighting (82580D0C)
  constexpr uint32_t kVoice = 0x3190;              // speech component (say-event r3)
  constexpr uint32_t kVoiceIndex = 12704;          // 15 SCI1, 16 SCI2, 17-19 TROOPER1-3, 21-22 WOMAN1-2
  constexpr uint32_t kScheduler = 0x18D4;
  constexpr uint32_t kHand0 = 0xFEC, kHand1 = 0xFF0;
  constexpr uint32_t kSquadPad = 0x44, kSquadZone = 0x48;
  constexpr uint32_t kHeardPad = 0x4C;
  constexpr uint32_t kStimHeard = 0x4, kLeaveCombat = 0x8, kSoftAlarm = 0x1000, kSearching = 0x40000000u, kDarkAim = 0x2;
  constexpr uint32_t kValidTarget = 0x800000;
  constexpr uint32_t kNeverInvestigates = 0x20000000;
  constexpr uint32_t kTargetHolder = 0x3168, kFallbackHolder = 0x3150;
  constexpr float kSeesHold = 1.0f; // s: "sees her" holds this long after Rare's last visible test
  constexpr uint32_t kEvHearUnknown = 11, kEvSeePlayer = 1, kEvSeeAgain = 23, kEvHides = 25, kEvStillHidden = 35, kEvAbandon = 41;
  constexpr uint32_t kEvSeeBody = 45;              // SEE_BODY_SOLO: Rare's sight pass on a first-seen body (825809C4)
  constexpr float kStillSpeed = 250.0f;            // cm/s: Jo at this speed or faster counts as fully moving
  constexpr uint32_t kHealth = 120;                // float, an NPC's health (counter_op.cpp)
  constexpr uint32_t kTannoyCue = 0x820A282C;      // "XACT_SOUNDBANK_LV05_SPEECH_TANNOY_SECURITY12" (823203A0)
  constexpr uint32_t kCueRefSize = 1024;           // cue reference buffer (Rare's are 272 bytes apart; lab_restorations)
  constexpr float kBodyNearCm = 700.0f;            // a found body counts when Jo killed someone this close to where he stops
  // Confirmed-contact reasons (Guard::confirm_kinds; each logged once per guard).
  constexpr uint32_t kCfSighting = 1, kCfCombat = 2, kCfHurt = 4, kCfMateKilled = 8, kCfBody = 16;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
  }
  void Store16(uint8_t *base, uint32_t a, uint16_t v)
  {
    base[a] = uint8_t(v >> 8);
    base[a + 1] = uint8_t(v);
  }
  float LoadF(const uint8_t *base, uint32_t a)
  {
    const uint32_t v = Load32(base, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  double LoadD(const uint8_t *base, uint32_t a)
  {
    const uint64_t v = (uint64_t(Load32(base, a)) << 32) | Load32(base, a + 4);
    double d;
    std::memcpy(&d, &v, 8);
    return d;
  }
  void StoreD(uint8_t *base, uint32_t a, double d)
  {
    uint64_t v;
    std::memcpy(&v, &d, 8);
    Store32(base, a, uint32_t(v >> 32));
    Store32(base, a + 4, uint32_t(v));
  }
  bool Ptr(uint32_t a)
  {
    return a >= 0x10000u && a < 0xFFFF0000u;
  }
  float Clock(const uint8_t *base)
  {
    return LoadF(base, kAiClock);
  }
  // Round 5 (log 025 07:06:39: two characters with the setup id s3g4): everything is keyed by the character (his
  // address and setup data), never by the name; in the log the second character of a name is "s3g4#2", and so on.
  std::unordered_map<std::string, std::vector<uint64_t>> g_name_owners; // setup id -> (address << 32 | setup data), in order seen
  std::string Id(const uint8_t *base, uint32_t c)
  {
    if (!Ptr(c))
    {
      // Round 11i (the crash of log 017, 21:43:02: "read of guest 0x00000054" = Id(base, 0), kData of nobody):
      // never read through a character pointer that is not one.
      return c ? fmt::format("{:08X}", c) : std::string("nobody");
    }
    // Sat 21 Nov evening (the crash of log 034 in the heard-level meter's copy of this helper: an object whose
    // +0x54 -> +0x34 held name text, not a pointer): every step is tested as mapped memory, printable text only.
    uint8_t *const mem = const_cast<uint8_t *>(base);
    const uint32_t data = pdz::guestmem::Readable(mem, c, kData + 4) ? Load32(base, c + kData) : 0;
    const uint32_t text = Ptr(data) && pdz::guestmem::Readable(mem, data, 0x38) ? Load32(base, data + 0x34) : 0;
    if (!Ptr(text) || !pdz::guestmem::Readable(mem, text, 32))
    {
      return fmt::format("{:08X}", c);
    }
    std::string s;
    for (int i = 0; i < 31 && base[text + i] >= 32 && base[text + i] < 127; ++i)
    {
      s += char(base[text + i]);
    }
    if (s.empty())
    {
      return fmt::format("{:08X}", c);
    }
    const uint64_t key = (uint64_t(c) << 32) | data;
    std::vector<uint64_t> &owners = g_name_owners[s];
    const auto it = std::find(owners.begin(), owners.end(), key);
    const size_t index = size_t(it - owners.begin());
    if (it == owners.end())
    {
      owners.push_back(key);
    }
    return index == 0 ? s : fmt::format("{}#{}", s, index + 1);
  }
  bool ArmedItem(const uint8_t *base, uint32_t item)
  {
    const uint32_t cls = Ptr(item) ? Load32(base, item + 8) : 0;
    return Ptr(cls) && (Load32(base, cls) & 0x10) != 0;
  }
  float Dist(const uint8_t *base, uint32_t a, uint32_t b)
  {
    if (!Ptr(a) || !Ptr(b))
    {
      return 1e9f; // (round 11i: nobody is far from everybody)
    }
    const float dx = LoadF(base, a + kPosition) - LoadF(base, b + kPosition);
    const float dy = LoadF(base, a + kPosition + 4) - LoadF(base, b + kPosition + 4);
    const float dz = LoadF(base, a + kPosition + 8) - LoadF(base, b + kPosition + 8);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  uint32_t ProfileOf(const uint8_t *base, uint32_t c)
  {
    const int32_t slot = int32_t(Load32(base, c + kProfileSlot));
    return (slot >= 0 && slot < int32_t(kProfileSlots)) ? kProfileTable + 0x50 * uint32_t(slot) : 0;
  }
  bool AlarmOn(const uint8_t *base)
  {
    const uint32_t level = Load32(base, kLevelState);
    const uint32_t alarm = Ptr(level) ? Load32(base, level + 0x2D8) : 0;
    return Ptr(alarm) && base[alarm + 0x54] != 0;
  }
  // Rare's speech event names: 171 string pointers at 0x82D2ADE8 (0 MIN_SOUND_NOT_USED .. 170 MAX_SOUND_NOT_USED).
  std::string EventName(const uint8_t *base, uint32_t ev)
  {
    constexpr uint32_t kSpeechNames = 0x82D2ADE8;
    if (ev >= 171)
    {
      return "event";
    }
    const uint32_t text = Load32(base, kSpeechNames + 4 * ev);
    if (!Ptr(text))
    {
      return "event";
    }
    std::string name;
    for (int i = 0; i < 48 && base[text + i] >= 0x20 && base[text + i] < 0x7F; ++i)
    {
      name += char(base[text + i]);
    }
    return name.empty() ? std::string("event") : name;
  }

  enum class St : uint8_t
  {
    Unaware,
    Suspicious, // glimpse handed to Rare's investigation; waiting for his SEARCH to start
    Searching,  // a SEARCH task (ours via Rare's investigation, or Rare's own: noise, body, alarm, darkness)
    Combat,
    Lost,       // lost contact: Rare's COMBAT script is ending (+6268 0x8)
    LostSearch, // searching her last position after losing contact (pdz_lab_stealth_hunting off)
    Wary,
    Hunting,    // confirmed contact lost: a sweep leg (Rare's SEARCH at one pad of the squad's plan)
    HuntPause,  // between sweeps: Rare's routine (his post) for pdz_lab_stealth_hunt_pause_s
    Dead,       // killed or removed: no stealth state at all any more (Rare's death, untouched)
    Investigating, // round 4: a lamp, a body, or his squad's search around a body (one pad at a time)
  };
  enum class Inv : uint8_t
  {
    None,
    Lamp,
    Body,
    Squad,
    Noise, // round 5: a sound he heard (gunfire, steps)
  };
  const char *InvName(Inv k)
  {
    return k == Inv::Lamp    ? "lamp"
           : k == Inv::Body  ? "body"
           : k == Inv::Squad ? "squad search"
           : k == Inv::Noise ? "sound"
                             : "none";
  }
  // What he is looking at (the log's words).
  const char *InvThing(Inv k)
  {
    return k == Inv::Lamp ? "lamp" : k == Inv::Noise ? "sound" : "body";
  }
  const char *StName(St s)
  {
    switch (s)
    {
    case St::Unaware: return "unaware";
    case St::Suspicious: return "suspicious";
    case St::Searching: return "searching";
    case St::Combat: return "combat";
    case St::Lost: return "lost (combat ending)";
    case St::LostSearch: return "lost (searching)";
    case St::Wary: return "wary";
    case St::Hunting: return "hunting";
    case St::HuntPause: return "hunting (pause at post)";
    case St::Dead: return "dead";
    case St::Investigating: return "investigating";
    }
    return "?";
  }

  struct Guard
  {
    St st = St::Unaware;
    bool scientist = false;
    bool was_wary = false;      // the state to return to after a refused investigation
    bool spotted = false;       // the meter reached the threshold: Rare's sight sees her
    bool metered = false;       // his sight tests of Jo go through the meter (set each frame by the sight update)
    // Wed 18 Nov night (lab_stealth_r9_Wed18Nov.inc): he is Rare's guard (seen or loud): no rule of the layer for him.
    bool r9_rares = false, r9_end_walk = false;
    uint8_t r9_rule = 0;
    int r9_n = 0;
    float r9_at = -1e9f, r9_calm_from = -1e9f, r9_body_scan = -1e9f;
    std::vector<uint32_t> r9_followed; // bodies whose one follow-up of Rare's was let through (no freezing)
    // Thu 19 Nov (lab_stealth_r10_Thu19Nov.inc): far gunfire (a run to the guard who fired), the hand-overs to Rare.
    bool r10_far = false;
    int r10_stage = 0, r10_legs = 0;
    uint32_t r10_by = 0, r10_walk = 0, r10_pad = 0, r10_search_pad = 0;
    float r10_pos[3] = {};
    float r10_at = -1e9f, r10_try = -1e9f, r10_best = 0.0f, r10_prog_at = -1e9f;
    float r10_search_from = -1e9f, r10_search_try = -1e9f;
    std::string r10_search_why;
    const char *r10_how = "";
    float r10_still_pos[3] = {};
    float r10_still_since = -1e9f, r10_after_body = -1e9f, r10_sure_act = -1e9f;
    std::vector<std::pair<uint32_t, float>> r10_body_lets; // bodies whose "a body" was let through after he stood still
    bool sees = false;          // Rare's test saw her recently (shown or hidden), or Rare's valid-target bit
    bool need_line = false;     // say HEAR_UNKNOWN if Rare's investigation did not
    bool still_said = false;
    bool seen_jo = false;       // this guard has had a real sighting (the "she" lines are his to say)
    float meter = 0.0f;
    float last_frame = -1e9f;   // his last sight update (decay step)
    float last_test = -1e9f;    // his last sight test of Jo (meter step)
    float last_visible = -1e9f; // Rare's test last said visible
    float glimpse_time = -1e9f; // the last hidden glimpse
    float glimpse_used = -1e9f; // the last glimpse the state machine looked at
    float since = 0.0f;         // AI clock of the last state change
    float spotted_at = -1e9f;
    float combat_seen = -1e9f;  // COMBAT update last run
    float search_seen = -1e9f;  // SEARCH update last run
    float combat_start = -1e9f;
    float last_log = -1e9f;
    float last_busy_log = -1e9f;
    float last_glimpse_log = -1e9f;
    float next_bark = 0.0f;
    float last_event_time = -1e9f;
    uint32_t last_event = 0;
    uint32_t lost_pad = 0;
    uint32_t squad = 0;
    uint32_t combat_script = 0; // his live COMBAT script (start .. status 2 / destructor)
    uint32_t inv_task = 0;      // the investigate sequence task Rare's hear handler built (821D15C0-821D1644)
    float inv_since = -1e9f;
    float inv_ended = -1e9f;
    uint32_t say_logged = 0;    // pdz_lab_stealth_log: the last event logged, when, and repeats skipped
    float say_logged_at = -1e9f;
    uint32_t say_skipped = 0;
    float sighting_said = -1e9f; // the last sighting line (SEE_PLAYER / SEE_PLAYER_AGAIN) let through
    float glimpse_pos[3] = {};
    // Confirmed contact and hunting (header points 7 / 8).
    bool confirmed = false;
    bool fought = false;         // he has been in Rare's COMBAT in this run
    bool dead = false;           // killed (the hit handler saw his health reach 0)
    bool end_leg = false;        // end his current SEARCH at its next update (a glimpse while hunting)
    bool body_pending = false;   // Rare's SEE_BODY: decide when his walk to the body is over
    uint32_t confirm_kinds = 0;  // kCf* bits already logged
    uint32_t search_pad = 0;     // the pad of the investigation SEARCH we gave him (the hunt's centre after it)
    uint32_t leg_pad = 0;        // the pad of his current sweep leg (0: Rare's own search)
    uint32_t redirect_pad = 0;   // his next leg's pad (her pad at a glimpse), before the squad plan's next
    uint32_t legs_done = 0;      // legs in this sweep
    uint32_t leg_fails = 0;      // legs in a row whose SEARCH never ran
    float leg_started = -1e9f;
    float pause_until = 0.0f;
    float redirect_at = -1e9f;
    float body_at = -1e9f;
    float moved_at = -1e9f;      // his last move of more than 40 cm (body walk end)
    float pos_at = -1e9f;
    float pos_prev[3] = {};
    float squad_checked = -1e9f; // SquadOf refreshed every 5 s
    float last_skip_log = -1e9f; // voice-budget refusals logged at most every 5 s per guard
    // Round 3.
    bool alive_seen = false;     // his health was above 0 once (so 0 means dead, not "no health")
    uint32_t voice = 0;          // voice index (+12704): 17-19 TROOPER1-3
    std::unordered_map<uint32_t, float> said_at;     // event -> AI clock of his last line of it (repeat rule)
    std::unordered_map<uint32_t, uint8_t> last_take; // event -> the take we picked last time (no repeat)
    uint32_t say_refused = 0;    // lines the budget / take rule refused (all, counted)
    uint32_t refused_ev = 0;     // the last refusal logged, when, and requests refused since
    float refused_at = -1e9f;
    uint32_t refused_repeats = 0;
    std::vector<uint32_t> swept; // round 3: the pads of his last legs (he moves on to new ones)
    uint32_t pend_ev = 0;        // the line let through last: its take is applied when its sound is primed
    int pend_take = 0;
    uint32_t pend_n = 0;
    float pend_at = -1e9f;
    // Round 10b: an alert line of his held while his own chatter line is cut (raised again when his speech is free).
    uint32_t hold_ev = 0;
    uint32_t hold_r5 = 1;
    float hold_at = -1e9f;
    bool hold_ours = false;
    std::string hold_what;
    bool investigator_lamp = false; // dark patches: his search is for a lamp that went out
    // Round 4.
    uint32_t data = 0;           // the setup data (+0x54) this entry was made for (another one here = a new character)
    std::string who;             // his setup id when the entry was made
    uint8_t role = 0;            // setup +0xFD: 4 = ALARM_USER
    bool dormant_seen = false;   // he was seen with Rare's dormant bit: joins the loop when he wakes
    Inv inv = Inv::None;         // his investigation (St::Investigating)
    float inv_centre[3] = {};    // the lamp / body
    float inv_jo[3] = {};        // Jo when it began
    bool inv_jo_seen = false;    // he saw her then: her own spot may be a leg
    uint32_t inv_centre_pad = 0;
    float inv_start = -1e9f, inv_until = 0.0f;
    uint32_t inv_pad = 0;        // the pad of his current leg
    int inv_leg = 0;             // legs started (the first is the look beside it)
    float inv_leg_at = -1e9f;
    float inv_arrived = -1e9f;   // he reached the leg's pad
    bool inv_end_leg = false;    // end the running leg at its next SEARCH update
    int inv_fails = 0;
    int inv_body = -1;           // g_kills index of the body
    uint32_t inv_redirect = 0;   // her pad at a glimpse: his next leg
    std::vector<uint32_t> inv_done;
    bool end_search = false;     // end his running SEARCH at its next update (the alarm is over)
    float end_search_at = -1e9f;
    bool calm_req = false;       // the alarm is over: back to his post, wary
    bool calm_after_combat = false; // ... and if he is fighting, wary when he loses her
    bool soft_by_us = false;     // round 3 (d) set Rare's soft-alarm flag on him
    float nevermind_at = -1e9f;  // his lamp "never mind" is being said (take mask for the swapped cue)
    float nevermind_until = -1e9f; // ... to be said until then (retried while the voice budget is busy)
    float nevermind_try = -1e9f;
    float call_until = -1e9f;    // his squad call ("Get the hell over here!") is due until then
    float call_try = -1e9f;
    uint32_t mask_ev = 0;        // a take mask for one event (the squad call: WARN_TEAM take 3 only)
    uint32_t mask_takes = 0;
    float mask_at = -1e9f;
    // Round 5.
    float hurt_at = -1e9f;       // Jo's last hit on him
    float hurt_full_at = -1e9f;  // follow-up Sun 15 Nov (item 16): hurt by Jo: his meter is full and no longer asked
                                 // (HurtKnown); Rare's own sight decides; no warning lines
    float staff_witness_at = -1e9f; // follow-up Sun 15 Nov (item 11): a scientist saw a colleague killed by Jo
    float warn_over_log = -1e9f;    // ... (items 9, 16) the last "warning line not asked for" log line
    // Round 2, Sun 15 Nov (lab_stealth_round2_Sun15Nov.inc).
    float r2_sight_at = -1e9f;      // Rare's sight last had Jo as his valid target (a real sighting)
    float r2_search_from = -1e9f;   // his running search episode began (searches under 3 s apart are one episode)
    float r2_search_last = -1e9f;   // ... its last SEARCH update
    bool r2_search_unconfirmed = false; // ... it began without a real sighting: the cap applies
    bool r2_search_capped = false;  // ... the cap has ended it
    bool r2_cap_post = false;       // a capped search just ended: wary, back to his post, his line
    float r2_born = -1e9f;          // the layer's first tick for him (a search older than that is the level's own)
    float r2_cap_end = -1e9f;       // his last capped search ended then
    float r2_cap_line = -1e9f;      // ... the check that his give-up line was heard
    int r2_chain = 0;               // 1 = one capped search (a restart is allowed), 2 = the restart is used up
    float r2_drop_log = -1e9f;
    float r2_blind_at = -1e9f;      // his COMBAT began blind then: a search is to be handed over when it has ended
    float r2_blind_first = -1e9f;   // ... the first of the blind starts counted in r2_blind_n (15 s window)
    int r2_blind_n = 0;
    uint32_t r2_blind_pad = 0;      // ... the pad of the last place he could know about
    const char *r2_blind_where = nullptr;
    float r2_blind_try = -1e9f;
    float r2_blind_line_until = -1e9f; // ... no sighting line until then unless he really sees her
    size_t r2_kills_n = 0;          // the kills on record he has been tested against (witness rule)
    float r2_witness_at = -1e9f;    // he saw Jo kill someone then
    float r2_witness_until = -1e9f; // ... Rare's "a friend died in view" input is held up until then
    float r2_witness_line = -1e9f;  // ... the check that a line followed
    float r2_witness_pos[3] = {};   // ... where the victim fell
    // Round 3, Sun 15 Nov, helper L (lab_stealth_r3_L_Sun15Nov.inc): his give-up line after a search end (R3EndLine).
    float r3l_at = -1e9f;           // a search / investigation of his ended then and its give-up line is due
    float r3l_next = -1e9f;         // ... the next ask no earlier than this
    float r3l_done = -1e9f;         // ... the last one was heard (or given up) then: one line per ended search
    float r3l_log = -1e9f;          // ... the last "no line" log line
    uint32_t r3l_ev = 0;            // ... the event of our last ask
    int r3l_tries = 0;              // ... asks so far
    bool r3l_inv = false;           // ... a lamp / noise check: "never mind" first
    std::string r3l_why;            // ... what ended
    float death_seen_at = -1e9f; // Rare's "a friend died / was hurt in view" input reached him (and was taken from Rare)
    float death_used = -1e9f;
    float noise_at = -1e9f;      // Rare's "heard a sound" input reached him (and was taken from Rare)
    float noise_used = -1e9f;
    uint32_t noise_pad = 0;      // ... the pad of the sound (profile +0x4C)
    float alert_until = -1e9f;   // alert: his meter fills faster until then
    float look_started = -1e9f;  // his look round where he stands (Rare's SEARCH at his own pad) began
    uint32_t look_pad = 0;
    uint32_t stim_logged = 0;    // pdz_lab_stealth_log: the last stimulus word / dropped bits logged, and when
    uint32_t stim_dropped = 0;
    float stim_logged_at = -1e9f;
    uint64_t bodies_seen = 0;    // g_kills indices he has noticed (once per body per guard)
    float body_check_at = -1e9f;
    float eye[3] = {}, dir[3] = {}; // his eye and facing at his last sight test (Rare's), and when
    float eye_at = -1e9f;
    float alarm_refused_log = -1e9f;
    float lamp_seen_at = -1e9f;  // a scientist: the last lamp he saw go out (the spook rule)
    bool stale = false;          // in Rare's COMBAT that did not end after he lost her: his sight is metered
    float search_start_log = -1e9f;
    float loud_at = -1e9f;       // he heard an unsilenced shot or an explosion (Rare's reaction, the alarm run, allowed)
    float loud_log = -1e9f;
    bool noise_silenced = false; // the sound he looks into was a silenced shot (the log's word)
    int test_sound = 0;          // pdz_test_stealth_sound: 1 loud shot, 2 silenced shot, 3 explosion, until test_sound_until
    float test_sound_until = -1e9f;
    bool search_top = true;      // his running SEARCH is the top task of his stack (only then may it be cut)
    int want_body = -1;          // a body he noticed while in a search: its investigation starts when the search is over
    float want_body_at = -1e9f;
    float allclear_at = -1e9f;   // his "All clear" is being said (the _TEAM cue's take 2 at Prime)
    int depth_logged = 0;        // the task stack depth last logged (4 or more)
    float next_cut = -1e9f;      // pdz_test_stealth_cut_s
    bool cut_test = false;       // ... his running SEARCH is being cut by the test tool
    // Round 6.
    // Round 7 (item 2): the no-progress fallback.
    float prog_ref[3] = {0.0f, 0.0f, 0.0f}; // where he was when he last had moved 1 m
    float prog_at = -1e9f;       // ... and when
    bool prog_moved = false;     // he has moved 1 m since this walk began
    float lamp_line_at = -1e9f;  // a scientist's last lamp comment (round 8: the repeat rule)
    float steps_log = -1e9f;     // round 9: his last footsteps log line
    float trace_log = -1e9f;     // ... his last trace line (testing)
    float expect_until = -1e9f;  // round 10: he is looking for Jo at her own pad (the suspicious route) until then
    float face[3] = {0.0f, 0.0f, 0.0f}; // his facing at his last sight test of Jo (Rare's own vector)
    float face_at = -1e9f;
    float steps_at = -1e9f;      // ... he last heard Jo's steps
    float hear = 0.0f;           // round 11: his hearing meter for her steps (no wary floor, no wary notice level)
    float warn_log = -1e9f;      // round 11: his last WARN_TEAM rule line in the log
    float lamp_heard_at = -1e9f; // round 11: he heard (not saw) the lamp of his investigation
    // Round 11b.
    uint32_t warn_ev = 0;        // a warning line of his that must play (asked for until it has really started)
    float warn_from = -1e9f, warn_until = -1e9f, warn_try = -1e9f, warn_queued = -1e9f;
    int warn_tries = 0;
    std::string warn_what;
    std::string sci_cue;         // a scientist's own line waiting for the gap (played on him as a sound, no speech event)
    uint32_t sci_mask = 0;       // ... the takes it may be
    float sci_len = 2.0f;
    float sci_from = -1e9f, sci_until = -1e9f;
    std::string sci_what;
    bool sci_sighting = false;   // ... it is his sighting line: his hesitation counts from its start
    int sci_stage = 0;           // warnings given in this climb of his meter (1 "who's there?", 2 alarmed, 3 the sighting)
    float sci_hold_until = -1e9f; // he hesitates until then before Rare's sight (his alarm run) takes over
    bool inv_brief = false;      // his lamp look is the short one for a lamp found dark (item 7)
    bool inv_from_wary = false;  // ... and he was wary before it
    float dark_check_at = -1e9f; // his last look for lamps that are out
    bool lamp_talked = false;    // a scientist: he has had the lights-out talk for shot-out lamps
    // Round 11c.
    uint32_t warn_takes = 0;     // a trooper's warning line: only these takes (0 = the usual rule)
    std::string sci_bank;        // a scientist's warning line: the set its cue is in (SCI1 / SCI2)
    float sci_line_at = -1e9f;   // his last warning line really started (or was given up) then
    float sci_full_at = -1e9f;   // his meter was first full in this climb then
    // Round 11d.
    float warn_free_at = -1e9f;  // a trooper: his warning line started; his meter is held until then
    float hold_since = -1e9f;    // ... his meter is being held for his warning since then (the log)
    // Round 11f.
    const char *warn_alt_cue = nullptr; // a trooper's warning line: the other cue Rare may play for it (_TEAM) ...
    uint32_t warn_alt_takes = 0;        // ... and the takes that one may be
    const char *alt_cue = nullptr;      // ... armed for the Prime of his line (PrimeBegin)
    uint32_t alt_takes = 0;
    float alt_at = -1e9f;
    bool run_pending = false;    // his alarm run is announced first: it starts at run_at
    float run_from = -1e9f;      // ... asked for then
    float run_at = -1e9f;        // ... starts then (set when his shout really started, or was given up)
    float run_go_until = -1e9f;  // ... announced: the alarm-run builder lets him through until then
    float announced_at = -1e9f;  // his alarm shout really started then
    std::string run_why;
    uint64_t deaths_known = 0;   // g_kills indices he knows of (a body he noticed, a death he saw)
    // Round 11g.
    float near_miss_at = -1e9f;  // a silenced near miss was taken from Rare then (he searches towards the shot)
    float friend_at = -1e9f;     // Rare's "alerted by a friend" was taken from him then (he goes to look at the fight)
    bool r6_truce = false;       // overnight Tue 17 Nov: Rare's unarmed truce holds for him now (R6TruceTick): Rare's own
    const char *noise_why = nullptr; // what the "sound" of his noise investigation really is (the log's words)
    // Round 11h: an announced alarm run is a commitment.
    bool run_committed = false;  // his alarm shout has started: he runs for the alarm until it is raised or he dies
    float run_commit_at = -1e9f;
    uint32_t run_task = 0;       // his alarm task (Rare's), as its start hook saw it
    float run_give_at = -1e9f;   // ... handed to him then
    int run_gives = 0;           // ... so many times
    int run_fails = 0;           // ... and it could not be built so many times in a row
    // Round 11i.
    uint32_t run_ready = 0;      // the alarm task Rare's builder made at the check before his shout (handed over later)
    float run_block_until = -1e9f; // no new alarm run of his before then (his last one found no route)
    float run_check_log = -1e9f;
    float post_pos[3] = {0.0f, 0.0f, 0.0f}; // his post: where he stood when the level started ...
    float post_yaw = 0.0f;       // ... his facing there (recorded, logged; not forced on him)
    uint32_t post_pad = 0;       // ... and his pad there
    float post_due = -1e9f;      // a search of ours ended then: he is to walk back to his post
    float post_try_at = -1e9f;
    uint32_t post_task = 0;      // his walk back (Rare's go-to-pad task in a composite)
    float post_walk_at = -1e9f;
    // Round 11j.
    int post_tries = 0;          // walks back started for this "back to his post" (a walk that ends at once is tried again)
    uint32_t post_target = 0;    // the pad of the walk that is running
    uint32_t closer_key = 0;     // the target spot of his walk or search (a pad; 1 his post; 2 the thing he walks to)
    float closer_d = 0.0f;       // ... his best distance to it so far
    float closer_at = -1e9f;     // ... reached then
    bool closer_done = false;    // ... he has been within 3 m of it
    float glimpse_search_at = -1e9f; // his search started by a glimpse began (or was restarted) then
    float reach_refused_at = -1e9f;  // his last glimpse / noise whose spot he cannot reach
    bool sci_set1 = false;       // a scientist: he speaks the SCI1 set (his voice number, or pdz_lab_lights_out_sci1)
    std::unordered_map<std::string, uint8_t> sci_last; // a scientist: cue -> the take (0-based) he said last
    bool pinned = false;         // TESTING: pdz_test_stealth_pin holds him at pin_pos
    float pin_pos[3] = {0.0f, 0.0f, 0.0f};
    int stuck = 0;               // 1: Rare's walk made no progress (the pad route is tried); 2: a pad walk made none: a look in place
    bool stuck_look = false;     // his current leg is that look round where he stands
    uint32_t inv_walk = 0;       // Rare's walk-to-body task on his stack (the first step of a body investigation)
    float inv_walk_at = -1e9f;
    std::vector<uint32_t> inv_bad; // pads beside the body he did not get to
    bool inv_walk_wait = false;  // no corpse object yet when he noticed the body: the walk starts when Rare has made it
    bool inv_walk_cut = false;   // his leg is ended for that walk
    float inv_walk_chk = -1e9f;
    float inv_walk_cut_at = -1e9f;
    bool inv_look = false;       // his current leg is a look round where the body lies
    int inv_looks = 0;           // looks at the body done (started)
    int inv_spots = 0;           // other spots started
    uint32_t inv_body_pad = 0;   // the pad he stood on beside the body (his later looks are there)
    float inv_floor = 0.0f;      // the floor height of this investigation (every spot within 1 m of it)
    float inv_dwell = 6.0f;      // how long he stays at his current leg's pad
    bool inv_floor_set = false;
    int prio = 0;                // a priority body line is due: 1 SEE_BODY_SOLO (he noticed it), 2 the squad call
    float prio_from = -1e9f;     // ... since
    float prio_until = -1e9f;    // ... until (pushed back while Rare's scripted speech plays)
    float prio_try = -1e9f;
    float prio_queued = -1e9f;   // ... its say-event was accepted then (the real start is logged by the play hook)
    float bodycall_at = -1e9f;   // his SEE_BODY_TEAM squad call is being said (take mask at Prime)
    // Round 11k.
    const char *warn_swap = nullptr; // his warning line plays as this cue (without the voice set) ...
    uint32_t warn_swap_takes = 0;    // ... one of these takes
    float sight_now_until = -1e9f;   // his sighting was confirmed: Rare's "alert now" input is kept up until his COMBAT starts
    bool sight_line_due = false;     // ... and his first-contact line is asked for at once
    float first_shot_for = -1e9f;    // the sighting (spotted_at) whose first shot has been logged
    float gunfire_line_at = -1e9f;   // a guard's gunfire was heard then: his HEAR_GUNFIRE line is due
    float gunfire_line_said = -1e9f;
    bool noise_fast = false;         // his noise is Jo's unsilenced shot or an explosion: he goes at once
    float stuck_pos[3] = {0.0f, 0.0f, 0.0f}; // a scientist on an alarm run: where he was when he last moved 1 m
    float stuck_at = -1e9f;
    float stuck_chk = -1e9f;
    float loud_line_at = -1e9f;      // after LOUD: his last 5 s line
    float loud_line_pos[3] = {0.0f, 0.0f, 0.0f};
    bool loud_line_seen = false;
    // Round 11l.
    float heard_at = -1e9f;          // he really heard something then (steps, a sound, gunfire): HEAR_UNKNOWN fits
    // Rare's rules plus.
    int rp_stage = 0;                // his sight lines of this glimpse episode: 1 the first, 2 the stronger one
    float rp_glimpse_at = -1e9f;     // his last glimpse of her
    bool rare_busy = false;          // Rare has him fighting, searching or on an alarm run (not calm for the lamps)
    float sight_log_at = -1e9f, sight_log_full = -1e9f, sight_diff_logged = 1e9f;
    // Mansion helper (Sun 15 Nov): Rare's radio call.
    float radio_at = -1e9f;          // his radio line (158 / 159) was asked for then (the announced call)
    uint32_t radio_ev = 0;
    bool radio_nolook_logged = false;
    // Round 3, Sun 15 Nov, helper W (lab_stealth_r3_W_Sun15Nov.inc). Item 5: one witness event per body. The body he
    // last REACTED to (Rare's "a friend died in view", 0010, let through for it): readable by other round 3 code (a
    // walk to the body may rely on it); a later 0010 for the same body is taken from Rare's word.
    uint32_t r3_once_victim = 0;     // ... the victim (0: no kill of Jo's on record for it)
    float r3_once_pos[3] = {};       // ... where the victim fell (valid when r3_once_has_pos)
    bool r3_once_has_pos = false;
    float r3_once_at = -1e9f;        // ... the AI clock of his reaction
    float r3_once_killed_at = -1e9f; // ... the AI clock of the death (the g_kills entry's `at`)
    std::vector<std::pair<uint32_t, float>> r3_once_done; // every body he has reacted to: (victim, AI clock of the death)
    int r3_once_repeats = 0;         // Rare's repeats taken since his reaction (the log)
    float r3_once_log = -1e9f;
    float r3_tower_log = -1e9f;      // item 4: the last "tower witness" log line
    float r3_cap_log = -1e9f;        // item 7: the last boost cap / far dark log lines
    float r3_far_log = -1e9f;
  };

  // A squad's sweep plan (header point 8), keyed by squad (or the guard when he has none).
  struct Hunt
  {
    uint32_t centre = 0;          // her last known pad
    std::vector<uint32_t> pads;   // centre first, then the routes out in angle order
    size_t next = 0;
    bool announced = false;       // the tannoy was asked for when its first hunter started
    float last_bark = -1e9f;
    uint32_t redirect_pad = 0;    // round 3: the pad one hunter was sent to for a glimpse, and when
    float redirect_at = -1e9f;
    uint32_t Next()
    {
      if (pads.empty())
      {
        return centre;
      }
      return pads[next++ % pads.size()];
    }
  };

  struct Kill
  {
    uint32_t victim = 0;
    float pos[3] = {};
    float at = 0.0f;
    std::string who;
    bool escalated = false;      // round 4: a finder raised the alarm or called his squad for it
    uint32_t finder = 0;         // ... the guard investigating it now (0 none)
    std::vector<uint32_t> witnesses; // round 5: guards who saw him die without seeing Jo
    float witness_at = -1e9f;        // ... the first of them
    bool witness_done = false;       // ... the nearest was sent to search round the spot
  };

  std::recursive_mutex g_lock;
  std::unordered_map<uint32_t, Guard> g_guards;
  uint32_t g_run_script = 0;
  int g_stack_deepest = 0; // round 7: the deepest task stack seen on a tracked guard in this run (logged when it grows)
  float g_clock_seen = -1.0f;
  float g_prev_clock = 0.0f;
  bool g_lab = false;         // the Laboratory runs and the stealth code is on (round 11: and Jo has not gone loud)
  bool g_lab_run = false;     // the Laboratory runs, loud or not (the crash guards)
  bool g_loud = false;        // round 11: Jo went loud in this run: everything here is off until the next run
  bool g_loud_released = false; // ... and what the stealth code had put on the guards was taken off again
  std::unordered_map<uint32_t, uint32_t> g_chars; // every character whose sight ran in this run -> his setup data
  bool g_any_sighting = false;
  bool g_alarm_was = false;
  std::unordered_map<uint32_t, float> g_squad_hides; // squad -> AI clock of its last PLAYER_HIDES
  float g_jo_prev[3] = {};
  float g_jo_t = -1.0f;
  float g_jo_speed = 0.0f;
  uint32_t g_rand = 0x2468ACE1u;
  std::unordered_map<uint32_t, Hunt> g_hunts; // squad (or guard) -> sweep plan
  std::vector<Kill> g_kills;                  // Jo's kills in this run (bodies that count)
  // Round 11f (item 3): per squad (or lone guard), the kills of hers its guards know of (a body noticed, a death
  // seen without seeing her), and the ones an alarm run was started for.
  std::unordered_map<uint32_t, std::vector<int>> g_squad_deaths, g_squad_escalated;
  // Round 11g (item 4): the alarm may not be raised before then (the runner's shout + pdz_lab_stealth_alarm_min_s);
  // the runner; the level manager of an alarm that was raised too soon and is held (0 none).
  float g_alarm_not_before = -1e9f;
  uint32_t g_alarm_runner = 0;
  uint32_t g_alarm_deferred = 0;
  struct Corpse
  {
    uint32_t object = 0, victim = 0;
    float at = 0.0f;
  };
  std::vector<Corpse> g_corpses;              // round 6: the corpse objects Rare made in this run
  struct LampOut
  {
    float pos[3] = {};
    float at = -1e9f;
  };
  std::vector<LampOut> g_lamps_out; // round 5: the lamps that went out in the last 60 s (the spree rule)
  // Round 11b (items 6 / 7): every shot of Jo's that put lamps out in this run (not lamps dark at level start, not
  // the generator blackout: LightIncident is only raised for a shot).
  struct DeadLamp
  {
    float pos[3] = {};
    float at = -1e9f;
    bool checked = false;          // a guard went to look under it: nobody checks it again
    float reach = 0.0f;            // how far its light reached (0: unknown)
    float below[3] = {}, floor[3] = {}; // what can be seen of it (LampView)
    bool has_floor = false;
    std::vector<uint32_t> noticed; // who knows it is out (saw or heard it go, or saw it dark)
    std::vector<uint32_t> commented; // round 11j: the scientists who commented on it (one of a pair within 15 m)
  };
  std::vector<DeadLamp> g_dead_lamps;
  bool g_next_brief = false;       // the next StartInvestigation is the short look at a lamp found dark
  // Round 11b: the takes a line played on a speaker may be (cue name without XACT_SOUNDBANK_, bit i = take i + 1),
  // applied when DarkAudioSound::Prime runs for that cue within 3 s.
  struct TakeOrder
  {
    std::string cue;
    uint32_t takes = 0;
    float at = 0.0f;
  };
  std::vector<TakeOrder> g_take_orders;
  float g_voice_last = -1e9f;                 // AI clock of the last line we raised (guards or tannoy)
  float g_rare_last = -1e9f;                  // a tracked guard's last line of Rare's own
  bool g_our_line = false;                    // inside our own say-event call
  float g_tannoy_want = -1.0f;                // a hunt start asked for the tannoy (AI clock), -1 none
  float g_tannoy_last = -1e9f;                // when it last played
  bool g_tannoy_cannot_locate = false;        // round 5: the request is the "cannot locate" take after an alarm's own end
  std::string g_tannoy_why;
  uint32_t g_cue_ref = 0;                     // guest buffer for the tannoy's cue reference
  uint32_t g_tannoy_by = 0;                   // the hunter who asked for it (dead: dropped)
  // Round 3 voice budget: the guard lines let through recently (start, expected end), Rare's and ours.
  struct Line
  {
    float start = 0.0f, end = 0.0f;
    uint32_t c = 0, ev = 0;
    bool stealth = false; // let through outside a fight (the music ducks under it)
    bool played = false;  // DarkAudioSound::Play started it (the duck and the mutter timing follow real starts only)
    bool alert = false;   // round 10b: an alert line (never cut by another line of his)
    bool warn = false;    // round 11g: a warning line of ours (never dropped for another warning)
  };
  // Round 10b: every line let through to Rare's speech is followed to its real start. Rare's speech can still lose it
  // after the say-event (research takes_and_playlog.md steps 4-7): a busy speaker's new request is dropped, the squad's
  // speech arbiter can hold a waiting line until its speech task gives up (2 s), the sound can be refused or stopped
  // before it starts. Each of those is logged ("pdz_voice_drop") and gives the budget's slot back.
  struct Said
  {
    uint32_t c = 0, ev = 0;
    float at = 0.0f;
    int stage = 0;        // 0 request queued (speech +216), 1 speech task waiting, 2 sound made (primed)
    uint32_t task = 0, sound = 0, squad = 0;
    int arbiter = -1;     // the squad arbiter's last answer (0 = go)
    int start = -1;       // the last start try (0 = sound made)
    int tries = 0;        // start tries refused
    bool strong = false;  // a sighting, the alarm call, the body call-in
    bool overridden = false;
    float wait_from = -1e9f; // round 11k: held since then: another speaker of his voice set is speaking
    bool wait_over = false;
    std::string what;
  };
  std::vector<Said> g_said_lines;
  int g_holds = 0;                            // guards with a held alert line
  bool g_hold_retry = false;                  // inside the raise of a held alert line
  uint32_t g_not_played = 0;                  // lines said that Rare's speech lost after the say-event
  float g_duck_clock = -1.0f;  // the AI clock of the last duck step
  float g_duck_db = 0.0f;      // the music duck now (dB below Rare's level)
  std::vector<Line> g_lines;
  float g_mutter_next = -1.0f;                // AI clock of the next mutter (searching / hunting guards)
  bool g_said = false;                        // our last Say() got through the budget
  bool g_optional = false;                    // inside Say(): an optional call-out
  float g_last_line_end = -1e9f;              // the end of the last guard line let through
  bool g_take_hook_ok = true;                 // the variation-pick hook steers the take (false: gendered cues drop)
  uint32_t g_mutter_ev = 0;                   // the last mutter's event (the next one differs)
  uint32_t g_admit_serial = 0;                // counts Admit() calls (the say hook sees whether it admitted one)
  float g_last_play_end = -1e9f;              // the end of the last guard line that really started
  uint32_t g_mutter_wait = 0;                 // the guard whose mutter was queued and has not started yet
  float g_mutter_wait_at = -1e9f;
  uint32_t g_mutter_wait_ev = 0;
  std::unordered_map<uint32_t, float> g_mutter_skip; // guards whose last mutter Rare's speech dropped (when)
  uint32_t g_rare_dropped = 0;                // lines admitted by the budget that Rare's speech did not queue
  // Round 4: the alarms of this run (12 (d) / (e)).
  bool g_alarm_on = false;                    // the alarm byte at the last alarm tick
  float g_alarm_tick = -1.0f;                 // AI clock of the last alarm tick
  int g_alarm_no = 0;                         // alarms raised in this run
  int g_alarms_ended = 0;                     // alarms that have ended (0: the next / current one is the first)
  float g_alarm_start = 0.0f;                 // when the current alarm was raised
  float g_alarm_seen = 0.0f;                  // the last confirmed sighting of Jo during it
  float g_alarm_seen_log = -1e9f;
  bool g_alarm_by_us = false;                 // we ended it (the timer), not Jo's switch
  bool g_alarm_by_test = false;               // the test tool switched it
  float g_alarm_ended_at = -1e9f;             // when the last alarm ended
  bool g_alarm_run_ours = false;              // inside our own AlarmRun (a body finder's escalation)
  uint32_t g_waves_held = 0;                  // s10 spawn messages dropped during the current alarm
  uint32_t g_waves_passed = 0;                // ... let through
  uint32_t g_waves_spawned = 0;               // s10 characters spawned during it
  float g_waves_log = -1e9f;
  std::unordered_map<uint64_t, bool> g_census; // (character << 32 | setup) already listed
  std::string g_test_alarm_done, g_test_body_done;
  // Round 5 (item 4, the gunfire rule): every hand-held shot (Rare's noise writer sub_825882E8) and every explosion's
  // sound event (sub_8218AD40 from the three explosion callers) of the last seconds.
  struct Shot
  {
    uint32_t shooter = 0;
    float at = -1e9f;
    bool silenced = false;
    float log_at = -1e9f;
  };
  std::vector<Shot> g_shots; // one entry per shooter (his latest shot)
  struct Blast
  {
    float pos[3] = {};
    float radius = 0.0f;
    float at = -1e9f;
  };
  std::vector<Blast> g_blasts;
  std::string g_test_sound_done, g_test_lamp_done;
  // Round 5 (item 7): Rare's scripted Laboratory speech (cues LV05_SPEECH_*) playing now.
  uint32_t g_story_sound = 0;
  float g_story_at = -1e9f;
  std::string g_story_name;
  float g_story_log = -1e9f;
  bool g_mesa_done = false; // round 11l: the lights-out conversation has played in this run (its first question: take 3)
  // Follow-up Sun 15 Nov (item 4): a guard in Rare's COMBAT is never metered (see CombatOnStack).
  uint32_t g_combat_vtable_seen = 0; // the vtable of the script at the last COMBAT start (sub_821CBF48)
  bool g_combat_note = false;        // the once-per-run log line of item 4 was written
  uint32_t g_combat_exempt = 0;      // sight tests of Jo handed back to Rare because the viewer was in COMBAT (this run)
  uint32_t g_setting_logged = 0xFFFFFFFFu; // difficulty << 1 | tracking runs, as last logged
  // Round 5 (item 6): the tannoy take to force at its Prime (1 location unknown, 2 tracking offline, 3 cannot
  // locate), a bit mask, and when it was queued; the all-clear line that follows take 3.
  uint32_t g_tannoy_takes = 0;
  float g_tannoy_takes_at = -1e9f;
  float g_allclear_at = -1e9f;   // "All clear" is due from then (after the tannoy)
  float g_allclear_until = -1e9f;
  uint32_t g_allclear_by = 0;
  float g_allclear_try = -1e9f;

  // A scripted line is playing: the sound is still in state 4 (playing) under the same name, 15 s at most.
  bool StoryPlaying(const uint8_t *base, float now)
  {
    if (!g_story_sound || !REXCVAR_GET(pdz_lab_stealth_story_first) || now < g_story_at || now - g_story_at > 15.0f)
    {
      return false;
    }
    if (!pdz::guestmem::Ok(base, g_story_sound, 344) || // (Sun 22 Nov night (audit): the sound may be freed)
        Load32(base, g_story_sound + 340) != 4 ||
        std::strncmp(reinterpret_cast<const char *>(base + g_story_sound + 52), g_story_name.c_str(), 250) != 0)
    {
      g_story_sound = 0;
      return false;
    }
    return true;
  }
  uint32_t Difficulty(const uint8_t *base)
  {
    const uint32_t game = Load32(base, 0x82D6BFD8);
    return Ptr(game) ? Load32(base, game + 4) : 0;
  }
  // The tracking system runs: Rare's own test, the same at its four Laboratory sites (the alarm tannoy 82320218 /
  // 8232036C, 823204F0, the tick's search hand-out 823213B4, the reinforcements' route 823266C4): Secret Agent or
  // above AND objective 3 of the Laboratory script not complete. Round 6 (item 5, log 062 "difficulty 0 (Agent),
  // tracking system running"): on Agent (difficulty 0) it never runs; rounds 4 / 5 tested the objective alone.
  bool TrackingRuns(const uint8_t *base)
  {
    const uint32_t script = lab_restorations::CurrentScript();
    const uint32_t objectives = Ptr(script) ? Load32(base, script + 0x3F0) : 0;
    return Ptr(objectives) && Difficulty(base) >= 1 && Load32(base, objectives + 3 * 0x34) != 2 &&
           !REXCVAR_GET(pdz_test_stealth_tracking_off);
  }
  const char *TrackingText(const uint8_t *base)
  {
    return TrackingRuns(base) ? "running" : Difficulty(base) == 0 ? "not running (Agent: it never runs)" : "destroyed";
  }

  float Rand01()
  {
    g_rand = g_rand * 1664525u + 1013904223u;
    return float(g_rand >> 8) / float(1u << 24);
  }

  bool Logging()
  {
    return REXCVAR_GET(pdz_lab_stealth_log);
  }
  // "Rare's rules plus" (pdz_lab_rareplus): Rare's AI decides; only the additive pieces of this file run.
  bool RarePlus()
  {
    return REXCVAR_GET(pdz_lab_rareplus) && !lab_stealth::CounterOpGameNow();
  }

  // ---- Stealth rollout (Sun 15 Nov): the layer per level and per zone -------------------------------------------
  // One row per supported level. A level helper adds a level by filling its row's DATA STRINGS only (no new code
  // path); see research notes: core\CORE_NOTES_Sun15Nov.md. Who is under the layer:
  //   - whole_level (the Laboratory): every guard whose voice is listed, Jo always inside: exactly lab-rareplus.
  //   - otherwise a character is under the layer when his voice is listed AND (his setup id is in `ids`, or his
  //     squad number is in `squads`, or the AI zone of his pad the first time the layer looks at him is in `zones`),
  //     and his setup id is not in `not_ids`.
  //     The decision is made once per character (and setup) and kept for the run, so a guard never changes side
  //     mid-chase. Jo is inside a zone while the AI zone of her own pad is in `zones` or her position is in one of
  //     `jo_boxes` (the log only: what a guard does depends on HIS membership).
  //   - Rare's AI zone of a pad = the signed 16-bit number at pad +62 (Load32(pad + 60) & 0xFFFF), the zone Rare's
  //     search pick and the zone-hop table 8216D908 use (CONFIRMED in this file: ZoneHops, Observe). A character's
  //     current pad is character +0xB4 (kPad; Jo's too: the footsteps rule reads it).
  // A row with no zones and no ids has the layer OFF (Rare's game, every hook passes straight through).
  enum class LevelSwitch
  {
    kLab,
    kNightclub,
    kSubway,
    kMansion
  };
  struct StealthLevel
  {
    const char *mission;      // pdz::pace::SpLevelName(): "laboratory", "nightclub", "subway", "mansion"
    const char *name;         // for the log
    LevelSwitch sw;           // its switch (the Lab: pdz_lab_rareplus)
    bool whole_level;         // every listed-voice guard is under the layer, Jo always inside (the Laboratory)
    const char *guard_voices; // voice numbers (character +12704; Rare's voice table 82D2B098) that take part as guards
    const char *staff_voices; // voice numbers that take part as scientists (meter + warnings; the Lab's 15,16 only)
    const char *zones;        // Rare's AI zone numbers (pad +62, built at load: read them from the survey log)
    const char *squads;       // Rare's squad numbers (setup +60, a 32-bit word; counter_op_wake.cpp kSetupSquad)
    const char *ids;          // setup ids (Id() without "#2") always under the layer: "s20a,s19b"
    const char *not_ids;      // setup ids never under the layer (wins over zones, squads and ids)
    const char *jo_boxes;     // Jo's zone for the enter / leave log, besides `zones`: boxes "x0 y0 z0 x1 y1 z1;..." (cm,
                              // corners in any order) made from Rare's data (trigger volumes, pad-graph planes)
    bool lamps;               // lamp lures from Rare's own light switch-off (the Lab keeps its own lab_dark path)
    bool alarm_announce;      // the alarm run's announcement and its 2.5 s hold (Rare's builder sub_821DF4A0)
    // Nightclub helper (Sun 15 Nov):
    const char *jo_zones;     // Jo's zone for the enter / leave log only (Rare's AI zones; membership never reads it)
    const char *jo_out_boxes; // Jo's zone for the log: she is in it when she is in NONE of these boxes (the club's
                              // own extent; "" = not used). jo_boxes / zones still count her in first
    const char *no_look;      // setup ids under the layer that get NO look round and NO lamp walk (meter and lines only)
    int lamp_lookers;         // guards who walk to look under one lamp (0 = pdz_lab_stealth_lamp_investigators)
    bool light;               // the meter's light term from the light at Jo (lab_dark's measure, pdz_lab_dark_patches)
    // Mansion helper (Sun 15 Nov):
    const char *jo_hold_boxes = ""; // Jo stays inside while she is in one of these (a band: enter by jo_boxes, leave
                                    // only outside these; "" = no band)
    const char *no_walk = "";       // setup ids under the layer that may look round at their own pad but are never
                                    // sent walking (no lamp walk, no late "finds a lamp dark" walk): the tower snipers
    float light_reference = 0.0f;   // the light that counts as fully lit on this level (0 = pdz_lab_dark_reference)
    bool moon = false;              // the moon (the level's directional light) counts where a ray towards it is clear
    bool radio = false;             // Rare's radio users (LVL4): the announced call, and no look round of ours for a
                                    // radio user who can still call (his DEFAULT reaction must stay ready)
  };
  // Voice numbers (Rare's table 82D2B098, 0x24 bytes each, the name at +4; CONFIRMED from the TU3 image): 0 CARRINGTON,
  // 1 CHANDRA, 2 CLAY, 3 DRCARROL, 4-6 ELITE1-3, 7-9 GANGSTA1-3, 10 JACK, 11 JO, 12 JON, 13 KILLIAN, 14 MAIHEM,
  // 15 SCI1, 16 SCI2, 17-19 TROOPER1-3, 20 VIRGIL, 21 WOMAN1, 22 WOMAN2, 23 ZEIGLER, 24-25 ZHANGLI.
  constexpr StealthLevel kStealthLevels[] = {
      // The Laboratory: the whole level, exactly lab-rareplus (troopers 17-19 the whole loop, scientists 15-16).
      {"laboratory", "Laboratory", LevelSwitch::kLab, true, "17,18,19", "15,16", "", "", "", "", "", false, true, "", "", "",
       0, false},
      // Nightclub: ONLY the docks outside (Perfect and Dark Agent's 8 extra guards included). FILL IN HERE (helper n):
      // guard_voices (the dock guards' voices, e.g. "7,8,9" = GANGSTA1-3), zones (Rare's AI zones of the docks) and/or
      // ids (the dock guards' setup ids), not_ids (anyone of the club who stands in a dock zone).
      // Filled (helper nl, Sun 15 Nov; survey n\SURVEY_nightclub_Sun15Nov.md): the 19 dock guards by setup id (Rare's
      // setup sections 1, 3, 4, 5, 6, 7, 18; all GANGSTA1-3). The bouncers s02a / s02b and everyone inside the club are
      // Rare's (not_ids for the bouncers, who stand on the dock side). The 8 extra Perfect / Dark Agent guards are all
      // inside the club: none added. No alarm user on the level: no alarm announcement. Jo's zone (log only): the
      // docks' pad zones from the level file (pad +34; the nine club doors cut the pad graph, + roof island and
      // ringleader 3's balcony). Look round / lamp walk: not for the conversation guards (the objective) nor the
      // WARNER s04a (Rare's rule: any stimulus = an alert). One guard walks under a lamp (it splits the van trio).
      {"nightclub", "Nightclub", LevelSwitch::kNightclub, false, "7,8,9", "", "", "",
       // Overnight Mon 16 / Tue 17 Nov (item 4, the user's decision 22:09): the two front-door bouncers s02a / s02b stand
       // OUTSIDE the club and are under the layer like every dock guard (they were in not_ids: Rare's own knowledge of
       // Jo drove them straight at her, hand test 22:06). Rare's unarmed truce at the door is kept by R6Truce (the sight
       // update hook): while a bouncer is passive (+12512 1-3, unprovoked) and Jo's hands are empty he is Rare's.
       "s19a,s19b,s01a,rin1,s01b,s03b,rin2,r2bu,s04a,s05a,s05b,s05c,s05d,s06a,s03a,rin3,s18a,s18b,s18c,s02a,s02b", "",
       // jo_boxes: the side-pier roof island (s03a) and ringleader 3's balcony (survey n: docks)
       "4050 2200 -1030 4280 2500 -350;950 1550 -900 3460 1800 -615",
       true, false,
       // jo_zones: none (the level file's pad zones are not Rare's runtime pad +62 zones: smoke log 09:16:05, Jo's
       // start pad is runtime zone 6, file zone 74; and Jo's own pad +0xB4 does not follow her warps)
       "",
       // jo_out_boxes: the club = the extent of the 1722 club pads of the pad graph cut at the nine club doors
       // (survey n, doorcut.py): its ground and kitchen floors, and its upper floors (+70 cm for Jo above a pad)
       "-2836 -700 -6952 3178 700 -427;-3309 600 -5153 3241 3000 -585",
       "rin1,rin2,rin3,s01b,r2bu,s04a", 1, true},
      // Subway: ONLY the stealth section after the garage, and the subway itself (the 6 extra guards of the harder
      // difficulties included; AlarmSpawner1-3's reinforcements: by zone). FILL IN HERE (helper s); the garage bikers
      // go in not_ids if they share a zone.
      // (alarm_announce false: the Subway has no alarm users (role +0xFD never 4) and its alarm call 8214EBD0 must stay
      // Rare's; Subway survey s.)
      // Filled (helper sl, Sun 15 Nov; survey s\SURVEY_subway_Sun15Nov.md): by Rare's squad numbers (setup +60) of the
      // script areas 1 (lorry yard, factory front: 2, 3), 2 (warehouse armoury: 4, 15), 4 (concourse: 8, 9, with the
      // Secret Agent+ extras sub1-6 in squad 8) and 5 (trains, bridge: 10, 11, 12, 16), and area 3's subway entrance
      // squads 7, 18. NOT squad 17: it holds only Rare's scripted guards set straight on Jo (8252D8D8): the alarm
      // spawner templates s17a-c (AlarmSpawner1-3, whose clones carry squad 17) and the locked-door spawns s20a-c; the
      // meter would hide Jo from them until it filled, weakening Rare's reinforcements, so they stay Rare's. The
      // garage (squad 1) and the safe house (5, 6) are Rare's. not_ids: the mechanic lor1 (Jo talks to him; his death
      // ends the mission), the wall-spawner template was1, the cut jack / zieg, and squad 17's ids (belt and braces).
      // GANGSTA1-3 (Killian's men) and TROOPER1-3 (Zhang's men). No alarm users: alarm_announce off (8214EBD0 stays
      // Rare's: AlarmSpawner1-3 send their three clones). Jo's zone (log only), boxes from the spawntrigger volumes:
      // the yard / factory / warehouse from spawntrigger1a's near edge (z -3514) to the area-3 exits spawntrigger3a /
      // 3b (z -8873; west of spawntrigger3a's x 2247 on to z -13000: the dumpster yard and its streetlights), and the
      // subway below ground beyond spawntrigger4 (z -12904, y < -100). no_look: the awake door kickers war1 / fac3
      // (scripted). Lamps: the fittings (warehouse_light, striplight, streetlight, tunnellight) through Rare's prop
      // path of 825F3978, the lamps with their own light (trainstriplight, striplightyell) through the bullet-hit
      // handler 825EE9D0 (lab_light_shot_log.cpp).
      // Follow-up (Sun 15 Nov): THE WHOLE LEVEL EXCEPT THE GARAGE. Every squad of the level's setup but the garage's
      // (squad 1: bik1-5, the cut jack / zieg) is listed: + the safe house's 5 and 6 (saf1-5), which the first build
      // left to Rare (the hand test left the zone on the street with the neon signs, 10:49:46, and the safe-house
      // guards were outside the layer). Rare's scripted reinforcements stay Rare's, exactly as he sends them at Jo:
      // squad 17 (the alarm clones of s17a-c, the locked-door spawns s20a-c) as before, and now also the safe
      // attackers sak1 / sak2 (templates safeattacker1 / 2, squad 4; their clones keep the template's setup id): they
      // are spawned when the briefcase is taken and Rare's alarm goes off (hand test 10:54:42: three of them were
      // under the layer and our "finds a lamp dark" look reached them as they spawned). The layer stays on for
      // everyone else after that alarm (rareplus has no "loud" end). Jo's zone (log only): everything beyond the
      // garage, from spawntrigger1a's near edge (z -3514) on: the level runs towards -z from there.
      {"subway", "Subway", LevelSwitch::kSubway, false, "7,8,9,17,18,19", "", "", "2,3,4,15,5,6,7,18,8,9,10,11,12,16", "",
       "lor1,was1,jack,zieg,s17a,s17b,s17c,s20a,s20b,s20c,sak1,sak2",
       "-99999 -99999 -99999 99999 99999 -3514",
       true, false, "", "", "war1,fac3", 1, true},
      // Mansion: ONLY the grounds outside (tower snipers included). FILL IN HERE (helper m). The radio call is Rare's
      // (LVL4 events 158-161), not an alarm panel: alarm_announce only covers Rare's alarm-run builder.
      // Filled (helper ml, Sun 15 Nov; survey m\SURVEY_mansion_Sun15Nov.md): the grounds = Rare's setup sections
      // s01-s08, by setup id (the tower snipers tow1-4, the front gate fre1-3, the low-garden patrols pat1-3, the moon
      // garden mgg1 (two records) / mgp1, the wall patrols wpt1 / wpt2, the outer courtyard ocg1 / ocp1; co-op only
      // fre4 / fre5 / s06c). TROOPER1 / TROOPER2 only. not_ids: the offstage speakers rdo1 / rdo2 / chan (placed inside
      // the grounds), the radio reinforcement templates s4s1 / s4s2 / s5s1 / s5s2 (their RadioSpawn clones carry the
      // template's setup id, as the Subway's AlarmSpawner clones do: Rare sends them at Jo), and the inner courtyard's
      // greeters / main-door guards / warner (belt and braces: not listed anyway). Jo's zone (log only): the plane of
      // the inner gate, z -1737 (only 4 pad links cross it, all at doors 3 / 6): she enters at z > -1700 and leaves at
      // z < -1780. No alarm on this level (no alarm user, no panel): alarm_announce off. Lamps: no lamp props, only
      // candle scene lights: the scene-light lure stays on. Tower snipers: no walk of ours (their 4-pad islands keep
      // Rare's own search on the tower). Light: the candles + the ambient + the moon where a ray towards it is clear,
      // against a Mansion reference (1.2: open moonlit ground 0.93, a candle pool 1, a wall's shadow 0.15).
      {"mansion", "Mansion", LevelSwitch::kMansion, false, "17,18", "", "", "",
       "tow1,tow2,tow3,tow4,fre1,fre2,fre3,pat1,pat2,pat3,mgg1,mgp1,wpt1,wpt2,ocg1,ocp1,fre4,fre5,s06c",
       "rdo1,rdo2,chan,s4s1,s4s2,s5s1,s5s2,cpt1,lsp1,rsp1,end1,end2,civ1,civ2,civ3,mai1",
       "-99999 -99999 -1700 99999 99999 99999", true, false, "", "", "", 1, true,
       "-99999 -99999 -1780 99999 99999 99999", "tow1,tow2,tow3,tow4", 1.2f, true, true},
  };
  const StealthLevel *const kLabRow = &kStealthLevels[0];
  const StealthLevel *g_level = nullptr;      // the row the layer runs on now (nullptr: none)
  const StealthLevel *g_survey_row = nullptr; // the supported level being played (for the survey log), on or off
  std::string g_level_off_why;                // why the layer is off on g_survey_row (empty: it is on)

  // Comma-separated list tests (no allocation: these run in hot hooks).
  bool CsvHasInt(const char *list, int v)
  {
    for (const char *p = list; p && *p;)
    {
      while (*p == ' ' || *p == ',')
      {
        ++p;
      }
      if (!*p)
      {
        break;
      }
      char *end = nullptr;
      const long n = std::strtol(p, &end, 10);
      if (end == p)
      {
        ++p;
        continue;
      }
      if (n == v)
      {
        return true;
      }
      p = end;
    }
    return false;
  }
  bool CsvHasText(const char *list, const std::string &item)
  {
    for (const char *p = list; p && *p;)
    {
      while (*p == ' ' || *p == ',')
      {
        ++p;
      }
      const char *q = p;
      while (*q && *q != ',' && *q != ' ')
      {
        ++q;
      }
      if (q > p && size_t(q - p) == item.size() && std::strncmp(p, item.c_str(), item.size()) == 0)
      {
        return true;
      }
      p = q;
    }
    return false;
  }
  bool HasZoneData(const StealthLevel &l)
  {
    return l.whole_level || (l.zones && *l.zones) || (l.squads && *l.squads) || (l.ids && *l.ids);
  }
  const StealthLevel &RowNow()
  {
    return g_level ? *g_level : *kLabRow; // (no level running: the Laboratory's rules, as before)
  }
  // His voice takes part as a guard (the Laboratory: TROOPER1-3, 17..19, as before) / as a scientist (15, 16).
  bool GuardVoice(uint32_t voice)
  {
    return voice < 64 && CsvHasInt(RowNow().guard_voices, int(voice));
  }
  bool StaffVoice(uint32_t voice)
  {
    return voice < 64 && CsvHasInt(RowNow().staff_voices, int(voice));
  }
  const char *VoiceTableName(uint32_t voice)
  {
    static const char *const kNames[] = {"CARRINGTON", "CHANDRA", "CLAY",     "DRCARROL", "ELITE1",   "ELITE2",  "ELITE3",
                                         "GANGSTA1",   "GANGSTA2", "GANGSTA3", "JACK",     "JO",       "JON",     "KILLIAN",
                                         "MAIHEM",     "SCI1",     "SCI2",     "TROOPER1", "TROOPER2", "TROOPER3", "VIRGIL",
                                         "WOMAN1",     "WOMAN2",   "ZEIGLER",  "ZHANGLI",  "ZHANGLI"};
    return voice < std::size(kNames) ? kNames[voice] : "?";
  }
  bool LevelSwitchOn(const StealthLevel &l)
  {
    switch (l.sw)
    {
    case LevelSwitch::kLab: return RarePlus();
    case LevelSwitch::kNightclub: return REXCVAR_GET(pdz_stealth_nightclub);
    case LevelSwitch::kSubway: return REXCVAR_GET(pdz_stealth_subway);
    case LevelSwitch::kMansion: return REXCVAR_GET(pdz_stealth_mansion);
    }
    return false;
  }
  const char *SwitchName(const StealthLevel &l)
  {
    switch (l.sw)
    {
    case LevelSwitch::kLab: return "pdz_lab_rareplus";
    case LevelSwitch::kNightclub: return "pdz_stealth_nightclub";
    case LevelSwitch::kSubway: return "pdz_stealth_subway";
    case LevelSwitch::kMansion: return "pdz_stealth_mansion";
    }
    return "?";
  }
  bool StealthLog()
  {
    return REXCVAR_GET(pdz_stealth_log);
  }
  // Rare's game type ([82D6BFD8] +40: 0 solo, 1 co-op, 2 / 3 multiplayer); -1 = no game state.
  int GameType(const uint8_t *base)
  {
    const uint32_t so = Load32(base, 0x82D6BFD8);
    return Ptr(so) ? int32_t(Load32(base, so + 40)) : -1;
  }
} // namespace
// Follow-up Sun 15 Nov (helper A: items 1, 11, 16).
REXCVAR_DEFINE_BOOL(pdz_stealth_one_voice, false, "Gameplay",
                    "Stealth layer: the old 'one voice at a time' rules (a line refused or dropped because another "
                    "guard nearby is talking or about to, and the cap on lines at once in a fight). Off (the default): "
                    "no line is ever refused because another speaker is talking; Rare's own speech rules apply")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_hurt_full, true, "Gameplay",
                    "Stealth layer: a guard or scientist hurt by the player (a hit, a near miss Rare counts on him, an "
                    "explosion of hers) has his detection meter filled at once, gets no warning line after that, and "
                    "Rare's own hurt lines are not held back by the layer's repeat rule. Off: the meter keeps its pace")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_staff_witness, true, "Gameplay",
                    "Stealth layer: a scientist who has a colleague killed by the player in his view (or right beside "
                    "him) is treated as having seen her: his sighting line, then Rare's own alarm run or flight")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 2, Sun 15 Nov (lab_stealth_round2_Sun15Nov.inc).
REXCVAR_DEFINE_DOUBLE(pdz_stealth_search_cap_s, 25.0, "Gameplay",
                      "Stealth layer: a guard's search that began without a real sighting of the player (a glimpse, a "
                      "noise, a lamp, a body) ends after this many seconds instead of Rare's 180: he goes back to his "
                      "post, wary. One more search may follow from a new glimpse or noise. Searches while the alarm "
                      "sounds and after a real sighting are Rare's. 0 = Rare's 180 seconds")
    .range(0.0, 180.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_blind_combat_search, true, "Gameplay",
                    "Stealth layer: when Rare starts a guard's combat although he has not seen the player and was not "
                    "hit (a sound, a friend's death), it becomes a search of the last place he could know about "
                    "instead of a fight with no target. A real sighting, a hit or the alarm is full combat")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_witness_any_squad, true, "Gameplay",
                    "Stealth layer: a guard who sees the player kill someone reacts as Rare's guards do to a squad "
                    "mate's death, whatever squad the victim was in. Not for the Mansion's tower snipers: their "
                    "deaths are seen only by their own squad (round 3). Off: Rare's own rule")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3, Sun 15 Nov, helper L (lab_stealth_r3_L_Sun15Nov.inc).
REXCVAR_DEFINE_BOOL(pdz_stealth_end_line, true, "Gameplay",
                    "Stealth layer: whenever a guard's search, look round or check of a lamp, noise or body ends with "
                    "nothing found, the player hears one give-up line from him (Rare's own \"lost her\" lines, or his "
                    "recorded \"never mind\" lines when Rare's speech refuses the first). It is asked for until it "
                    "really plays, at most 15 s, once per ended search. Not when he sees the player, fights or is dead. "
                    "Off: only the lines Rare's speech lets through")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3, Sun 15 Nov, helper W (lab_stealth_r3_W_Sun15Nov.inc).
REXCVAR_DEFINE_BOOL(pdz_stealth_explosion_muffle, true, "Gameplay",
                    "Stealth layer: walls and floors muffle an explosion less than other sounds. Each wall or floor "
                    "between an explosion and a guard takes pdz_stealth_explosion_muffle_wall off the distance it is "
                    "heard within (instead of half), never below pdz_stealth_explosion_muffle_floor of Rare's own "
                    "distance. Gunfire and every other sound keep the halving. Needs pdz_stealth_wall_muffle")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_explosion_muffle_wall, 0.25, "Gameplay",
                      "pdz_stealth_explosion_muffle: the part of the distance each wall or floor takes off (0.25: one "
                      "wall leaves 75%, two 56%)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_explosion_muffle_floor, 0.6, "Gameplay",
                      "pdz_stealth_explosion_muffle: walls never take an explosion's distance below this part of "
                      "Rare's own distance")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_witness_once, true, "Gameplay",
                    "Stealth layer: a guard reacts once to each death he sees. Rare repeats \"a friend died in view\" "
                    "about every second while the body is in view; the repeats for a body he has already reacted to "
                    "no longer restart his alert or his search. Off: every repeat is a new reaction (Rare's rule)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_candles_never_out, true, "Gameplay",
                    "Stealth layer (Mansion grounds): a candle or lantern flame never counts as a lamp that went out "
                    "(no lamp lure, no lamp line, no lamp investigation), whatever a shot near it does to Rare's light")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_lamp_check_s, 3.0, "Gameplay",
                      "Stealth layer (Nightclub, Subway, Mansion): after a shot on a lamp the light's real state is "
                      "watched for this many seconds; it counts as out once it is off or under half its brightness "
                      "(or still fading out at the end), and never if Rare has it on again")
    .range(0.5, 10.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_light_shot, "", "Testing",
                      "TESTING TOOL: with the stealth layer running, \"x,y,z\" (cm; any text after a fourth comma makes a "
                      "new value): Rare's own light-surface routine runs as if a bullet had hit the scene light nearest "
                      "to that point (a lamp that can be shot out goes out; the layer counts it only if it really does). "
                      "Applied once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_boost_limit, true, "Gameplay",
                    "Stealth layer: the detection meter's boosts do not stack far away. Beyond "
                    "pdz_stealth_boost_limit_cm the boosts together (a wary guard's lower sighting level, the faster "
                    "fill after an alert) are capped at pdz_stealth_boost_limit_cap, and a look at an exposure under "
                    "pdz_stealth_boost_limit_exposure (far and dark, or far and crouched) can make a guard suspicious "
                    "(his warning lines, a look round) but never makes him see the player. Off: the boosts multiply "
                    "at any distance")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_boost_limit_cm, 2500.0, "Gameplay",
                      "pdz_stealth_boost_limit: the distance (cm, from his eye to the player) beyond which it applies")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_boost_limit_cap, 2.0, "Gameplay",
                      "pdz_stealth_boost_limit: the largest combined boost far away (2 = one boost, not two)")
    .range(1.0, 16.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_boost_limit_exposure, 0.08, "Gameplay",
                      "pdz_stealth_boost_limit: a look far away whose exposure (the meter's light, distance, stance and "
                      "movement factor, as the log prints it) is under this only makes him suspicious")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3, Sun 15 Nov, helper H (lab_stealth_r3_H_Sun15Nov.inc).
// Stealth4 quick job, Mon 16 Nov (helper A, fix 5; lab_stealth_r4_A_Mon16Nov.inc).
REXCVAR_DEFINE_BOOL(pdz_stealth_sneak_behind, true, "Gameplay",
                    "Stealth layer: sneaking up behind a guard who does not know of Jo. Behind him (outside his sight "
                    "cone) and walking or crouched she is not noticed, however close: an all-round sight answer of the "
                    "game's for her is hidden, \"saw a target\" is taken from his reactions, and a fight that starts "
                    "with no sighting is turned into a search. Running behind him within pdz_stealth_sneak_run_cm is "
                    "heard (his line, he stops and looks round); bumping into him (pdz_stealth_sneak_bump_cm) is "
                    "heard at once. In front of him the sight rules are unchanged. Off = the layer as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_sneak_run_cm, 300.0, "Gameplay",
                      "pdz_stealth_sneak_behind: Jo's running steps behind a guard are heard within this distance (cm)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_sneak_bump_cm, 60.0, "Gameplay",
                      "pdz_stealth_sneak_behind: Jo within this distance (cm, on the ground plan) of a guard has bumped "
                      "into him: heard at once, whatever her gait")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_heard_react, true, "Gameplay",
                    "Stealth layer: when a calm guard hears something through the wall check (gunfire, an explosion, "
                    "a body falling or another of the game's sounds) and the game's own reaction does not follow within "
                    "half a second, the layer starts one: his \"what was that\" or gunfire line, he is wary, and for "
                    "gunfire or an explosion within 30 m he goes to look where the sound came from (at most two guards "
                    "per sound, ended by the search cap); otherwise he stops and looks round where he stands. Guards "
                    "who must stay at their post only say the line")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_witness_walk, true, "Gameplay",
                    "Stealth layer: a guard who saw the player kill someone and whose fight began without a sighting "
                    "walks to the body and looks round there (ended by the search cap) instead of searching where he "
                    "stands. Once per body. Off: the game's search where he stands")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_shout_heard, true, "Gameplay",
                    "Stealth layer: calm guards within 20 m of a guard's alarm shout (half the distance per wall or "
                    "floor between) say a line, become wary and alert and turn to the one who shouted. They do not "
                    "learn where the player is")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3c, Sun 15 Nov night, helper C (lab_stealth_r3c_C_Sun15Nov.inc, fix 4): witness tuning.
REXCVAR_DEFINE_DOUBLE(pdz_stealth_witness_range_cm, 2000.0, "Gameplay",
                      "Stealth layer: a guard or scientist counts as a witness of a kill only within this distance (cm) "
                      "of it, and only when the kill is inside his own sight cone, in clear view, and no further away "
                      "than the light at that spot would let him see the player there. 0: no distance cap (the cone "
                      "and the light still apply)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_no_steer_unseen, true, "Gameplay",
                    "Stealth layer: while a guard has not seen the player (his meter is below his sighting level), the "
                    "game's own target on her is taken from him, so that he does not turn or aim at a player he has "
                    "not seen; he faces what he last glimpsed. Off: the game's target stays")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_killian_alert, true, "Gameplay",
                    "Stealth layer, Subway: Killian's escape line counts as spotting the player: his hangar guards "
                    "are alerted and search for her (they know she is in the hangar, not where). His escape itself "
                    "is unchanged. Off: the game's own (the guards stay calm)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Stealth round 5, Mon 16 Nov (helper C, fix 5; lab_stealth_r5_C_Mon16Nov.inc).
REXCVAR_DEFINE_BOOL(pdz_stealth_camera_noise, true, "Gameplay",
                    "Stealth layer: a security camera shot down makes a breaking noise that nearby guards hear (walls "
                    "muffle it) or see: a line, and one or two walk over to look. Off: the game's own (a destroyed "
                    "camera is silent to the guards)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Overnight Mon 16 / Tue 17 Nov (lab_stealth_r6_Tue17Nov.inc).
REXCVAR_DEFINE_STRING(pdz_stealth_camera_models, "camera,cctv,seccam", "Gameplay",
                      "Stealth layer: a prop is a security camera when its setup name or its model name holds one of "
                      "these words (commas between them, any case). The log's PROP DOWN line gives both names of "
                      "every prop a bullet takes down")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_camera_lens_quiet, true, "Gameplay",
                    "Stealth layer: a shot that hits a security camera without bringing it down (the lens crack) is "
                    "quiet: no guard hears it; only a guard who is looking at the camera at that moment reacts (a look "
                    "round). Off: such a hit is nothing to the guards at all")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_truce, true, "Gameplay",
                    "Stealth layer: a guard of the layer who is passive towards an empty-handed player (the game's own "
                    "unarmed truce: the Nightclub's front-door bouncers) is left to the game while he is calm, "
                    "unprovoked and her hands are empty; the layer's rules apply from the moment that ends")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Tue 17 Nov evening (lab_stealth_r7_Tue17Nov.inc): one owner per guard.
REXCVAR_DEFINE_BOOL(pdz_stealth_combat_owner, true, "Gameplay",
                    "Stealth layer: one owner per guard. A guard in the game's own combat whom the layer agrees has "
                    "seen Jo is left to that combat completely: the layer hands him no search, look or walk, does not "
                    "empty his target and takes nothing from his reactions, so he aims and shoots as in the original "
                    "game. The place of a sighting the layer has confirmed is passed to his squad as the game does. "
                    "For a calm guard the layer no longer keeps emptying a target the game refills at once. Off = "
                    "the layer as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_owner_log, true, "Gameplay",
                    "Stealth layer: one log line when a guard changes owner (the game's combat, the layer's search, "
                    "calm), for diagnosing a guard who twitches or freezes. Log only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Tue 17 Nov night (fix2, lab_stealth_r8_A/B_Tue17Nov.inc).
REXCVAR_DEFINE_BOOL(pdz_stealth_one_sight_truth, true, "Gameplay",
                    "Stealth layer: once the layer has confirmed that a guard saw Jo, and for as long as it holds him "
                    "as having seen her, none of its own sight rules (darkness range, sight blocks) hide her from the "
                    "game's own sight test for that guard; the game's own range, cone and walls still apply. When the "
                    "layer lets him lose her, its rules apply again. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_blind_search_clean, true, "Gameplay",
                    "Stealth layer: a fight the game starts with no sighting (a shot heard, a near miss) becomes one "
                    "search: towards where the shot came from when no other place is known, and further sounds do not "
                    "restart that search over and over while it runs. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_body_first, true, "Gameplay",
                    "Stealth layer: a guard checking a body (his walk to it and his look round there) finishes that "
                    "check before a weak glimpse, a sound or a dark lamp sends him elsewhere; a real sighting still "
                    "ends it at once. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_late_body, true, "Gameplay",
                    "Stealth layer: a calm guard (seated or standing) who was near a kill but not looking that way at "
                    "the moment notices the body when it later comes into his view (the same sight test as at the "
                    "kill). Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Wed 18 Nov night (lab_stealth_r9_Wed18Nov.inc): the rework. Seen or loud = the game's own guard.
REXCVAR_DEFINE_BOOL(pdz_stealth_rares_guard, true, "Gameplay",
                    "Stealth layer: the layer only shapes the quiet part. A guard who has seen the player, or who has "
                    "heard her go loud, is the game's own guard from that moment until the game itself has him calm "
                    "again: none of the layer's rules apply to him (see pdz_stealth_seen_is_rares and "
                    "pdz_stealth_loud_is_rares). Off = the layer as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_seen_is_rares, true, "Gameplay",
                    "Stealth layer: seen = the game's guard. The moment the game's own sight has the player for a "
                    "guard, he is the game's at once, whatever the layer's detection meter says; any body walk, look, "
                    "lamp check or search the layer gave him ends. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_loud_is_rares, true, "Gameplay",
                    "Stealth layer: loud = the game's guard. An unsilenced shot, an explosion, the alarm, Killian's "
                    "escape line, a camera shot down loudly, a hit on him or a fight near him makes a guard the game's "
                    "own for the rest of that alert: the layer does not turn his fight into a search, cut his search "
                    "short, empty his target or apply its witness rules. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_plain_view, true, "Gameplay",
                    "Stealth layer: plain view. When the game's own sight test sees the player and she is close "
                    "(pdz_stealth_plain_view_cm) or stands in the light (pdz_stealth_plain_view_light), the guard sees "
                    "her at once, as in the original game. The detection meter only delays a glimpse in the dark at "
                    "range. Off = the meter decides every sighting, as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_plain_view_cm, 1000.0, "Gameplay",
                      "pdz_stealth_plain_view: within this distance (cm) of his eye the player is in plain view "
                      "whatever the light")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_plain_view_light, 0.65, "Gameplay",
                      "pdz_stealth_plain_view: the light where the player stands (0.3 dark to 1 lit) from which she is "
                      "in plain view at any distance the game's own sight reaches")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_rares_calm_s, 20.0, "Gameplay",
                      "pdz_stealth_rares_guard: a guard who was the game's own comes back to the layer's rules once "
                      "the game has had him calm (no fight, no search, no target, no alarm) for this many seconds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_rares_ends_walk, true, "Gameplay",
                    "pdz_stealth_rares_guard: a walk the layer gave a guard (to a body, a lamp, a sound) ends the "
                    "moment he becomes the game's own, so the game's reaction starts at once. Off = the walk runs out "
                    "first")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_rares_cascade, true, "Gameplay",
                    "pdz_stealth_rares_guard: when a guard who is the game's own starts to fight, the friends near "
                    "him are alerted as in the original game. Off = each still learns of the player only by his own "
                    "sight or hearing")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_late_body_any, true, "Gameplay",
                    "Stealth layer: a calm guard notices any body that comes into his view later, also one whose kill "
                    "he was nowhere near, and also while he searches. Off = only bodies of kills he was near")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_body_no_freeze, true, "Gameplay",
                    "Stealth layer: a guard who went to look at a body as a witness and stays beside it gets the "
                    "game's own follow-up for that body once (his search, his alarm run), instead of standing still. "
                    "Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_lamp_heard_walk, true, "Gameplay",
                    "Stealth layer: a calm guard who hears a lamp break close by walks over to check it also when it "
                    "is round a corner or behind a door (one check per lamp). Off = he only goes when a spot with a "
                    "view of the lamp is in his reach")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Thu 19 Nov (lab_stealth_r10_Thu19Nov.inc): fixes to the calm-guard part after the Thu 19 Nov hand test.
REXCVAR_DEFINE_BOOL(pdz_stealth_far_gunfire_to_shooter, true, "Gameplay",
                    "Stealth layer: a calm guard alerted only by another guard's gunfire (or the game's friend alert), "
                    "who has not seen the player and was not hit, does not learn where she is: he runs to the guard who "
                    "fired and joins the fight there, or searches that spot if the fight is over. He is the game's own "
                    "guard at once when he sees or hears her or is hit. Off = the original game: he knows her position")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_far_gunfire_searching, true, "Gameplay",
                    "Stealth layer (with pdz_stealth_far_gunfire_to_shooter): a guard already in the game's own search "
                    "who has not seen the player gets the same far-gunfire rule when only another guard's gunfire or the "
                    "friend alert reaches him: he runs to the guard who fired instead of learning where she is (Thu 19 "
                    "Nov night). Off = he learns her position, as the game has it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_second_body, true, "Gameplay",
                    "Stealth layer: a guard who already knows of a body, or is sure someone is here, and then sees a "
                    "friend killed or another body is the game's own guard at once. Off = he looks first again")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_no_giveup_after_body, true, "Gameplay",
                    "Stealth layer: after a body the layer never plays a give-up line; when its check of the body is "
                    "over the guard is handed to the game's own follow-up (search, alarm run). Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_post_release, true, "Gameplay",
                    "Stealth layer: when a guard's walk back to his post finds no route, the walk ends and the game's own "
                    "behaviour has him (no look round of the layer's). Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_body_still_followup, true, "Gameplay",
                    "Stealth layer: a guard who knows of a body and has stood still for 8 s gets the game's own "
                    "reaction to that body again (at most 3 times per body, 20 s apart). Off = once only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_sure_moves, true, "Gameplay",
                    "Stealth layer: a guard who is sure someone is here and stands still with nothing to do searches "
                    "towards what he glimpsed (the game's own search). Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_no_steer_once, true, "Gameplay",
                    "Stealth layer: once the game has taken a guard's target back three times, the layer leaves it to "
                    "the game for the rest of that alert (no tug of war). Off = it tries again after 20 s")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_lamp_fresh_hearing, true, "Gameplay",
                    "Stealth layer: a breaking lamp is heard only at the moment the glass breaks; a lamp that takes "
                    "longer to go dark is noticed only by guards who see it. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_friend_alert_log, true, "Gameplay",
                    "Stealth layer, log only: when a guard's fight starts from the game's 'alerted by a friend', one "
                    "line says who or what raised it, where, and whether the layer has that friend down as having seen "
                    "the player. No behaviour change")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_witness_look_first, true, "Gameplay",
                    "Stealth layer: a guard who saw a kill but not the player goes to look first: he says his line, "
                    "walks to the body and checks round it. Only when he then sees the player, or finds nothing at the "
                    "body, does he call it in (his radio call or alarm run). Kill him on the way and nothing is passed "
                    "on. A guard who saw the player herself reacts at once. Off: the game's reaction at once")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Stealth fixes Mon 16 Nov evening (helper B, lab_stealth_r4_B_Mon16Nov.inc): fixes 2, 3 and 4.
REXCVAR_DEFINE_BOOL(pdz_stealth_body_line_acts, true, "Gameplay",
                    "Stealth layer: a guard who says a body or dead-friend line acts on it: he stops, walks to the "
                    "body and looks round there, then the witness and body rules apply. Off: the line only, as the "
                    "game raised it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_witness_everyone, true, "Gameplay",
                    "Stealth layer: the witness rule (look first, walk to the body, then call it in) applies to every "
                    "guard. A guard whose own conversation is a mission objective stays in place only while nobody of "
                    "that conversation has been killed. Off: those guards and the scripted ones never leave their spot")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_body_look_from_afar, true, "Gameplay",
                    "Stealth layer: a guard who cannot walk to a body looks at it from the nearest point he could get "
                    "to, when he has a clear view of it from there. Off: he gives up")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_glimpse_walk, true, "Gameplay",
                    "Stealth layer: a guard who thinks he saw something walks to the place to look (our noise check: "
                    "capped, his give-up line, back to his post), when he can walk there on his own floor; once per "
                    "spot. Off: he only looks round where he stands")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_shot_from_search, true, "Gameplay",
                    "Stealth layer: a witness whose friend is shot dead beside him goes to look towards where the shot "
                    "came from (a silenced shot gives only the direction of the hit). Off: he looks round the body")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_witness_body_test, true, "Gameplay",
                    "Stealth layer: a guard who could not see a kill (outside his cone, too dark or too far) does not "
                    "notice that body either until it is inside his cone, in clear view and within the distance the "
                    "light there allows. Off: the game's own body sighting")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3b, Sun 15 Nov (lab_stealth_r3b_Sun15Nov.inc): no new switch for the fixes; one test tool.
REXCVAR_DEFINE_STRING(pdz_test_stealth_kill_at, "", "Testing",
                      "TESTING TOOL: \"id,x,y,z\" (cm): the live tracked guard with this setup id is put at x, z on his own "
                      "floor (he must be within 2 m of y; his height is left to the game) for a second, then killed "
                      "there as pdz_test_stealth_kill does (the game's own kill, the player as the killer). Once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_witness, "", "Testing",
                      "TESTING TOOL: \"victim,witness,cm[,deg]\": the live tracked guard `victim` is put cm in front of the "
                      "guard `witness` (the way he looks, turned deg to his right) for a second, then killed there as "
                      "pdz_test_stealth_kill_at does. Once per new value. NOTE: only the position the stealth layer reads "
                      "is moved; the game's own body, sounds and sight stay where he really stood, so use "
                      "pdz_test_stealth_witness_when for a true distance")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_witness_when, "", "Testing",
                      "TESTING TOOL: \"victim,witness,min_cm,max_cm\": armed until it fires: as soon as the live tracked "
                      "guard `victim` really is between min_cm and max_cm from the guard `witness`, inside his sight "
                      "cone and in clear view, he is killed where he stands (the game's own kill, the player as the "
                      "killer). Nobody is moved. Once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3c, Sun 15 Nov night, helper D (lab_stealth_r3c_D_Sun15Nov.inc, fix 5): the Mansion's radio reinforcements.
REXCVAR_DEFINE_BOOL(pdz_stealth_mansion_reinforcements, true, "Gameplay",
                    "Stealth layer, Mansion grounds: the reinforcements a guard's radio call brings are not told where "
                    "the player is. They go to where the call came from (the caller's spot, the body he found, or the "
                    "place he last saw the player), search round it spread out, and find the player only by their own "
                    "sight and hearing. The call still counts as a detection. Off: the game sends them at the player")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_mansion_reinforcements_search_s, 60.0, "Gameplay",
                      "Stealth layer, Mansion grounds: the radio reinforcements search round the place of the call for "
                      "this long (s) after the first of their spots is reached, then stay there, wary")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_stealth_mansion_reinforcements_spots, 4, "Gameplay",
                     "Stealth layer, Mansion grounds: spots each radio reinforcement checks round the place of the call")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_stealth_radio, "", "Testing",
                      "TESTING TOOL: on the Mansion grounds, the live tracked guard with this setup id is given the game's "
                      "own \"a friend died in view\" input for half a second, with no body: a radio user on Secret Agent "
                      "or higher answers it with the game's radio call. Once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
#if PDZ_COUNTEROP
bool PdzCounterOpOn(); // counter_op.cpp: Counter-Operative runs for co-op games (pdz_counterop, or chosen in the menu)
#endif
namespace
{
  // Counter-Operative: Rare has no such game type; the mod plays it as a co-op game (type 1) while its switch or the
  // menu choice is on (counter_op.cpp On()). Plain co-op (the mod off) is not Counter-Op: the layer runs as on the Lab.
  // Counter-Op round 14 (Tue 17 Nov): the answer of the last sight update, for the parts of the layer that test the
  // switches without a level row (guard footsteps, the sneak rule, the Rare-plus rules): all off in Counter-Operative.
  std::atomic<bool> g_counter_op_game{false};
  bool CounterOpGame(const uint8_t *base)
  {
#if PDZ_COUNTEROP
    return GameType(base) == 1 && PdzCounterOpOn();
#else
    (void)base;
    return false;
#endif
  }
  // The supported level being played (solo or co-op only), or nullptr.
  const StealthLevel *RowOfLevel(const uint8_t *base)
  {
    const int type = GameType(base);
    if (type != 0 && type != 1)
    {
      return nullptr;
    }
    const std::string mission = pdz::pace::SpLevelName(base);
    for (const StealthLevel &l : kStealthLevels)
    {
      if (mission == l.mission)
      {
        return &l;
      }
    }
    return nullptr;
  }
  int PadZone(const uint8_t *base, uint32_t pad)
  {
    return Ptr(pad) ? int(int16_t(Load32(base, pad + 60) & 0xFFFF)) : -1;
  }
  std::string BaseId(const std::string &id)
  {
    const size_t hash = id.find('#');
    return hash == std::string::npos ? id : id.substr(0, hash);
  }
  constexpr uint32_t kSetupSquad = 60; // setup +0x3C: Rare's squad number (counter_op_wake.cpp, Subway survey)
  uint32_t SquadNumber(const uint8_t *base, uint32_t c)
  {
    const uint32_t data = Ptr(c) ? Load32(base, c + kData) : 0;
    return Ptr(data) ? Load32(base, data + kSetupSquad) : 0;
  }
  // Jo's position in one of the row's boxes ("x0 y0 z0 x1 y1 z1;...").
  bool InJoBoxes(const char *boxes, const float p[3])
  {
    for (const char *q = boxes; q && *q;)
    {
      float v[6];
      int used = 0;
      if (std::sscanf(q, " %f %f %f %f %f %f%n", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &used) != 6)
      {
        return false;
      }
      bool in = true;
      for (int i = 0; i < 3; ++i)
      {
        in = in && p[i] >= std::min(v[i], v[i + 3]) && p[i] <= std::max(v[i], v[i + 3]);
      }
      if (in)
      {
        return true;
      }
      q += used;
      while (*q == ';' || *q == ' ')
      {
        ++q;
      }
    }
    return false;
  }
  bool R3cDReinforcement(const StealthLevel &l, const std::string &id); // (round 3c D: lab_stealth_r3c_D_Sun15Nov.inc)
  // The table's answer for one character (voice not checked here).
  bool DecideMember(const StealthLevel &l, const std::string &id, int zone, uint32_t squad, std::string &why)
  {
    if (l.whole_level)
    {
      why = "the whole level";
      return true;
    }
    if (R3cDReinforcement(l, id)) // (round 3c D: the Mansion's radio reinforcements of a guard's call)
    {
      why = fmt::format("setup id {} is a radio reinforcement of a guard's call: under the layer "
                        "(pdz_stealth_mansion_reinforcements)",
                        id);
      return true;
    }
    if (CsvHasText(l.not_ids, id))
    {
      why = fmt::format("setup id {} is in not_ids", id);
      return false;
    }
    if (CsvHasText(l.ids, id))
    {
      why = fmt::format("setup id {} is listed", id);
      return true;
    }
    if (squad && squad < 100000 && CsvHasInt(l.squads, int(squad)))
    {
      why = fmt::format("his squad number {} is listed", squad);
      return true;
    }
    if (zone >= 0 && CsvHasInt(l.zones, zone))
    {
      why = fmt::format("his pad's AI zone {} is listed", zone);
      return true;
    }
    why = HasZoneData(l) ? fmt::format("outside: his setup id {}, squad number {} and pad AI zone {} are not listed", id,
                                       squad, zone)
                         : std::string("outside: this level's zone is still empty");
    return false;
  }
  struct Member
  {
    bool in = false;
    std::string why;
  };
  std::unordered_map<uint64_t, Member> g_members; // (character << 32 | setup data) -> under the layer or not
  std::string Id(const uint8_t *base, uint32_t c);
  const Member &MemberOf(const uint8_t *base, uint32_t c)
  {
    const uint32_t data = Load32(base, c + kData);
    const uint64_t key = (uint64_t(c) << 32) | data;
    auto it = g_members.find(key);
    if (it == g_members.end())
    {
      Member m;
      const int zone = PadZone(base, Load32(base, c + kPad));
      m.in = DecideMember(RowNow(), BaseId(Id(base, c)), zone, SquadNumber(base, c), m.why);
      if (!m.in && zone < 0)
      {
        // No pad yet (a spawn not placed): out for now, asked again next time (not kept).
        static Member pending;
        pending = m;
        return pending;
      }
      it = g_members.emplace(key, std::move(m)).first;
    }
    return it->second;
  }
  // The zone test for one character: true = the layer may act for him. The Laboratory (whole level) and no level
  // running: always true (as before). On a zone level: his kept membership.
  bool InLayer(const uint8_t *base, uint32_t c)
  {
    if (!g_level || g_level->whole_level)
    {
      return true;
    }
    if (!Ptr(c))
    {
      return false;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    return MemberOf(base, c).in;
  }
  // A zone level (not the Laboratory) runs the layer now.
  bool ZoneLevel()
  {
    return g_level && !g_level->whole_level;
  }
  // Subway helper: the character Rare's COMBAT update and the squad's target copy work on for him (8217C554-8217C578,
  // 821CC370-821CC394: the holder +0x3168 if it holds one, else the fallback +0x3150; holder +4 = the character), or 0.
  uint32_t TargetOf(const uint8_t *base, uint32_t c)
  {
    if (!Ptr(c))
    {
      return 0;
    }
    const uint32_t t = Load32(base, c + 0x316C);
    return t ? t : Load32(base, c + 0x3154);
  }
  // Subway helper: on a zone level, his target is someone other than a human player (character +12 == 2): Subway's gang
  // war (Killian's men against Zhang's men). That fight is Rare's: the layer acts only on stimuli about Jo. The
  // Laboratory: never (as before).
  bool FightsNonPlayer(const uint8_t *base, uint32_t c)
  {
    if (!ZoneLevel())
    {
      return false;
    }
    // Sat 21 Nov evening (sweep after the crash of log 034): c can be any perception slot's owner (a grenade, a
    // spiderbot), so both reads are tested as mapped memory first.
    uint8_t *const mem = const_cast<uint8_t *>(base);
    if (!Ptr(c) || !pdz::guestmem::Readable(mem, c + 0x3154, 0x1C))
    {
      return false;
    }
    const uint32_t t = TargetOf(base, c);
    return Ptr(t) && pdz::guestmem::Readable(mem, t, 16) && Load32(base, t + 12) != 2;
  }
  // Is he a guard of the layer (the Laboratory: a trooper, 17..19, as before)?
  template <typename G> bool LayerGuard(const G &g)
  {
    return !g.scientist && GuardVoice(g.voice);
  }
  // Nightclub helper: under the layer, but no look round and no lamp walk of ours (the row's no_look: the Nightclub's
  // conversation guards, whose talk is the objective, and Rare's WARNER). The Laboratory: never.
  bool NoLook(const std::string &who)
  {
    return ZoneLevel() && CsvHasText(g_level->no_look, BaseId(who));
  }
  // Mansion helper: under the layer, may look round at his own pad, but is never sent walking by us (the row's no_walk:
  // the Mansion's tower snipers; no_look ids too). The Laboratory: never.
  bool NoWalk(const std::string &who)
  {
    return NoLook(who) || (ZoneLevel() && CsvHasText(g_level->no_walk, BaseId(who)));
  }
  // Stealth fixes Mon 16 Nov evening (helper B, fix 3; lab_stealth_r4_B_Mon16Nov.inc): the WITNESS rule's own tests.
  // R4BNoLook: the row's no_look still keeps him in place (his own talk is the objective and nobody of it was killed).
  // R4BNoWalk: the row's no_walk list itself (the tower snipers), without the other no_look ids.
  bool R4BNoLook(const std::string &who);
  const char *R4BWitnessStays(const uint8_t *base, uint32_t c, const Guard &g);
  // (round 5 B, Mon 16 Nov: lab_stealth_r5_B_Mon16Nov.inc: the glimpse walk, the shot-from search)
  bool R5BGlimpseWalk(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_search);
  void R5BKillSync(const uint8_t *base, float now);
  void R5BShotFrom(uint8_t *base, uint32_t c, Guard &g, int ki, float walk_pos[3], float now);
  void R5BReset();
  bool R4BNoWalk(const std::string &who)
  {
    return R4BNoLook(who) || (ZoneLevel() && CsvHasText(g_level->no_walk, BaseId(who)));
  }
  // Mansion helper: Rare's radio call (LVL4). The row has radio users (the Mansion grounds).
  bool RadioRow()
  {
    return ZoneLevel() && g_level->radio;
  }
  // Rare's radio line events: 158 LVL4_PLAYER_SPOTTED_RADIO_IN (net live), 159 LVL4_TRY_RADIO_NETWORK_DOWN (net down),
  // raised by his DEFAULT reaction at 821D0988 (sub_821D0748; survey m 3.1).
  bool RadioEvent(uint32_t ev)
  {
    return RadioRow() && (ev == 158 || ev == 159);
  }
  constexpr uint32_t kRadioInUse = 0x4000; // character +0x187C: "radio in use" (set at 821D0844, cleared 821D0730)
  // His role (vtable +0x350 = sub_825801A8: setup byte +0xFD) is RADIO_USER (3) (821D07E4-821D0804).
  bool RadioUser(const uint8_t *base, uint32_t c)
  {
    const uint32_t data = Ptr(c) ? Load32(base, c + 0x54) : 0;
    return Ptr(data) && base[data + 0xFD] == 3;
  }
  // Rare's call tests besides the role (821D0808-821D0830): difficulty [[82D6BFD8] +4] >= 1 (Secret Agent and up) and
  // the radio state [[[82D6BFD8 +0x1C] +0x2D8] +0] 0 (net live) or 2 (net destroyed). -1 = no state.
  int RadioDifficulty(const uint8_t *base)
  {
    const uint32_t so = Load32(base, 0x82D6BFD8);
    return Ptr(so) ? int32_t(Load32(base, so + 4)) : -1;
  }
  int RadioState(const uint8_t *base)
  {
    const uint32_t a = Load32(base, 0x82D6BFD8 + 0x1C);
    const uint32_t m = Ptr(a) ? Load32(base, a + 0x2D8) : 0;
    return Ptr(m) ? int32_t(Load32(base, m)) : -1;
  }
  // A radio user who could start a call at his next stimulus: Rare's DEFAULT reaction must stay ready for him, so no
  // look round of ours (a SEARCH of ours runs instead of his DEFAULT script, and from it Rare's sight goes to combat
  // without a call: survey m 3.1).
  bool RadioReady(const uint8_t *base, uint32_t c)
  {
    if (!RadioRow() || !RadioUser(base, c) || RadioDifficulty(base) < 1)
    {
      return false;
    }
    const int st = RadioState(base);
    return st == 0 || st == 2;
  }
  // The look round of ours is skipped for a radio user who can still call (logged once per guard).
  bool RadioNoLook(const uint8_t *base, uint32_t c, Guard &g, const char *at)
  {
    if (!RadioReady(base, c))
    {
      return false;
    }
    if (!g.radio_nolook_logged && StealthLog())
    {
      g.radio_nolook_logged = true;
      REXLOG_INFO("pdz_stealth_radio: {} is a radio user (difficulty {}, radio state {}): no look round of ours at {} (his "
                  "line only): Rare's DEFAULT reaction stays ready, so his next stimulus can start Rare's radio call",
                  Id(base, c), RadioDifficulty(base), RadioState(base), at);
    }
    return true;
  }
  // The meter's light term on a zone level whose row asks for it (lab_dark's light measure at Jo, meter only).
  bool ZoneLight()
  {
    return ZoneLevel() && g_level->light && REXCVAR_GET(pdz_stealth_light) && lab_dark::MeasureSwitch();
  }
  // The meter reads the light at Jo: the Laboratory's dark patches, or a zone row's light term.
  bool LightTerm()
  {
    return lab_dark::On() || ZoneLight();
  }
  // A voice the trooper-only rules of the Laboratory run for: TROOPER1-3, and on a zone level the row's guard voices
  // (Nightclub helper: the warning queue, the body-line priority, back to his post, search mutters). The Laboratory:
  // 17-19 only, as before.
  bool LayerVoice(uint32_t voice)
  {
    return (voice >= 17 && voice <= 19) || (ZoneLevel() && GuardVoice(voice));
  }
  // GANGSTA1-3 (7-9) on a zone level: their own takes of the meter's lines (survey n, the transcripts).
  bool Gangster(uint32_t voice)
  {
    return ZoneLevel() && voice >= 7 && voice <= 9 && GuardVoice(voice);
  }
  int g_jo_zone_seen = -2;     // Jo's pad zone at the last look (the survey log)
  int g_jo_inside = -1;        // Jo inside the active zone (-1 unknown)
  std::unordered_map<uint64_t, bool> g_survey_seen; // characters the survey line was written for
  uint32_t SquadOf(PPCContext &ctx, uint8_t *base, uint32_t c);
  // Once per AI clock step: Jo's zone (the enter / leave line and the zone-number line).
  void JoZoneTick(const uint8_t *base, float now)
  {
    if (!g_survey_row || g_survey_row->whole_level || !StealthLog()) // (the Laboratory: nothing new is logged)
    {
      return;
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(jo))
    {
      return;
    }
    const uint32_t pad = Load32(base, jo + kPad);
    const int zone = PadZone(base, pad);
    if (zone != g_jo_zone_seen)
    {
      REXLOG_INFO("pdz_stealth: {}: Jo's pad AI zone {} -> {} (pad {:08X}) at ({:.0f} {:.0f} {:.0f}), {:.1f} s",
                  g_survey_row->name, g_jo_zone_seen, zone, pad, LoadF(base, jo + kPosition), LoadF(base, jo + kPosition + 4),
                  LoadF(base, jo + kPosition + 8), now);
      g_jo_zone_seen = zone;
    }
    if (!g_level)
    {
      return;
    }
    const float at[3] = {LoadF(base, jo + kPosition), LoadF(base, jo + kPosition + 4), LoadF(base, jo + kPosition + 8)};
    const bool by_zone = zone >= 0 && (CsvHasInt(g_level->zones, zone) || CsvHasInt(g_level->jo_zones, zone));
    const bool by_box = !by_zone && (InJoBoxes(g_level->jo_boxes, at) ||
                                     (g_jo_inside == 1 && InJoBoxes(g_level->jo_hold_boxes, at))); // (Mansion: the band)
    const bool by_out = !by_zone && !by_box && g_level->jo_out_boxes && *g_level->jo_out_boxes &&
                        !InJoBoxes(g_level->jo_out_boxes, at); // (Nightclub: outside the club's own extent)
    const int inside = g_level->whole_level || by_zone || by_box || by_out ? 1 : 0;
    if (inside != g_jo_inside)
    {
      REXLOG_INFO("pdz_stealth: {}: Jo {} the stealth zone ({}; her pad's AI zone {}) at ({:.0f} {:.0f} {:.0f})",
                  g_level->name, inside ? "ENTERS" : g_jo_inside < 0 ? "starts outside" : "LEAVES",
                  g_level->whole_level ? "the whole level is the zone"
                  : by_zone            ? "her pad's zone is in zones / jo_zones"
                  : by_box             ? "her position is in jo_boxes"
                  : by_out             ? "her position is outside every jo_out_boxes box"
                  : g_level->jo_out_boxes && *g_level->jo_out_boxes ? "her position is inside a jo_out_boxes box"
                                                                    : "neither in zones nor in jo_boxes",
                  zone, at[0], at[1], at[2]);
      g_jo_inside = inside;
    }
  }
  // Once per character (and setup) on a supported level: everything a level helper needs to fill the table.
  void SurveyLine(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    if (!g_survey_row || g_survey_row->whole_level || !StealthLog() || !Ptr(c)) // (the Laboratory: its census line)
    {
      return;
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    if (c == jo)
    {
      return;
    }
    const uint32_t data = Load32(base, c + kData);
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g_survey_seen.emplace((uint64_t(c) << 32) | data, true).second)
    {
      return;
    }
    const std::string id = Id(base, c);
    const uint32_t voice = Load32(base, c + kVoiceIndex);
    const uint32_t pad = Load32(base, c + kPad);
    const int zone = PadZone(base, pad);
    const uint32_t squad = SquadOf(ctx, base, c);
    const int role = Ptr(data) ? int(base[data + 0xFD]) : -1;
    std::string verdict;
    if (g_level)
    {
      const bool guard = GuardVoice(voice), staff = StaffVoice(voice);
      const Member &m = MemberOf(base, c);
      verdict = !guard && !staff ? fmt::format("Rare's: voice {} is not a guard voice of this level (guard_voices \"{}\")",
                                               VoiceTableName(voice), g_level->guard_voices)
                : m.in           ? fmt::format("UNDER THE LAYER as a {} ({})", guard ? "guard" : "scientist", m.why)
                                 : fmt::format("Rare's ({})", m.why);
    }
    else
    {
      std::string why;
      const bool would = DecideMember(*g_survey_row, BaseId(id), zone, SquadNumber(base, c), why);
      verdict = fmt::format("Rare's: the layer is off on this level ({}); the table would say {} ({})", g_level_off_why,
                            would ? "in" : "out", why);
    }
    REXLOG_INFO("pdz_stealth: guard: {} {} setup {:08X} voice {} ({}) pad {:08X} AI zone {} squad number {} (object "
                "{:08X}) role {} health {:.0f} at ({:.0f} {:.0f} {:.0f}): {}",
                g_survey_row->name, id, data, int32_t(voice), VoiceTableName(voice), pad, zone, SquadNumber(base, c), squad,
                role,
                LoadF(base, c + kHealth), LoadF(base, c + kPosition), LoadF(base, c + kPosition + 4),
                LoadF(base, c + kPosition + 8), verdict);
  }

  // Round 11 (item 1): LOUD MEANS RARE'S FULL GAME. From here on g_lab is false until the next run: every hook of
  // this file passes straight through (LoudRelease, at the next sight update, takes the stealth code's own flags and
  // the music duck off again).
  void GoLoud(const uint8_t *base, const std::string &why)
  {
    if (g_loud || !REXCVAR_GET(pdz_lab_stealth_loud) || RarePlus()) // (rareplus: there is no stealth-off moment)
    {
      return;
    }
    g_loud = true;
    g_loud_released = false;
    g_lab = false;
    REXLOG_INFO("pdz_lab_stealth: LOUD: {} ({:.1f} s into the level): the stealth code is OFF for the rest of the level: "
                "every guard is Rare's own again (his sight, his hearing of gunfire, his squad's warnings, his alarm and "
                "reinforcement waves). It does not go back to quiet",
                why, Clock(base));
  }

  // Round 3c (Sun 15 Nov night, fix 1; lab_stealth_r3c_A2_Sun15Nov.inc): a mission restart keeps the AI clock running
  // and the level's row, so neither test below saw it (log 005, 20:46:49: a dark lamp of the attempt before was "found"
  // by pat2#2). A level start is now also Rare's own level setup (lab_stealth::LevelStartSignal: 1 the level music
  // setup, 2 the Laboratory script's setup) and, in a solo game, a new player-one character; each one clears the
  // layer's whole per-level memory (R3cClearLayerMemory) and logs "pdz_stealth_r3c: layer memory cleared (why)".
  std::atomic<int> g_r3c_signal{0};
  const char *R3cLevelStart(const uint8_t *base, float now, uint32_t run_key, bool lab);
  void R3cClearLayerMemory(const uint8_t *base, const char *why);

  // Switch on and the Laboratory running; a new run (another script instance, the AI clock going back) starts over.
  bool LabActive(const uint8_t *base)
  {
    if (!REXCVAR_GET(pdz_lab_guard_stealth))
    {
      return false;
    }
    const float now = Clock(base);
    if (now != g_clock_seen || g_r3c_signal.load(std::memory_order_relaxed) != 0) // (round 3c: or a level start signalled)
    {
      g_clock_seen = now;
      const uint32_t script = lab_restorations::CurrentScript();
      // Stealth rollout (Sun 15 Nov): the Laboratory as before (its script ticks), or a zone level of kStealthLevels
      // (its switch on, pdz_lab_rareplus on, a zone filled in). Counter-Operative: off on every level.
      const StealthLevel *row = script != 0 ? kLabRow : RowOfLevel(base);
      const bool counter_op_game = CounterOpGame(base);
      g_counter_op_game.store(counter_op_game, std::memory_order_relaxed);
      const bool counter_op = row && counter_op_game;
      const StealthLevel *active = nullptr;
      std::string off_why;
      if (script != 0)
      {
        active = counter_op ? nullptr : kLabRow;
        off_why = counter_op ? "a Counter-Operative game: every guard is Rare's" : "";
      }
      else if (row && row != kLabRow)
      {
        off_why = counter_op               ? "a Counter-Operative game: every guard is Rare's"
                  : !RarePlus()            ? "pdz_lab_rareplus is off"
                  : !LevelSwitchOn(*row)   ? fmt::format("{} is off", SwitchName(*row))
                  : !HasZoneData(*row)     ? std::string("its zone is still empty (kStealthLevels)")
                                           : std::string();
        active = off_why.empty() ? row : nullptr;
      }
      else if (row)
      {
        // Follow-up (Sun 15 Nov): the Laboratory is loaded but its script has not ticked yet (the layer starts with
        // the script). The line used to say "OFF ()" here and was never corrected once the script ran.
        off_why = "waiting for the Laboratory's script to start";
      }
      if (row != g_survey_row || off_why != g_level_off_why)
      {
        // (Not the "waiting" moment: one line per level load, once the answer is known.)
        if (row && StealthLog() && (active || script != 0 || row != kLabRow))
        {
          REXLOG_INFO("pdz_stealth: {} ({} game): the stealth layer is {}", row->name,
                      GameType(base) == 1 ? "co-op" : "solo",
                      active ? (active->whole_level
                                    ? fmt::format("ON for the whole level (guard voices \"{}\", staff voices \"{}\"{})",
                                                  active->guard_voices, active->staff_voices,
                                                  RarePlus() ? "" : "; pdz_lab_rareplus is off: rounds 3 to 11l's rules")
                                    : fmt::format("ON for its zone: squads \"{}\", ids \"{}\", pad zones \"{}\", never "
                                                  "(not_ids) \"{}\", guard voices \"{}\", no look round \"{}\", no walk "
                                                  "\"{}\"; Jo counts as inside (log only) in the boxes \"{}\"{}{}",
                                                  active->squads, active->ids, active->zones, active->not_ids,
                                                  active->guard_voices, active->no_look, active->no_walk, active->jo_boxes,
                                                  *active->jo_out_boxes ? " or outside the boxes \"" : "",
                                                  *active->jo_out_boxes ? std::string(active->jo_out_boxes) + "\"" : std::string()))
                             : fmt::format("OFF ({})", off_why));
        }
        if (row != g_survey_row)
        {
          std::lock_guard<std::recursive_mutex> hold(g_lock);
          g_survey_seen.clear();
          g_jo_zone_seen = -2;
        }
        g_survey_row = row;
        g_level_off_why = off_why;
      }
      const uint32_t run_key = script != 0 ? script : active ? 0x100u + uint32_t(active - kStealthLevels) : 0;
      const bool lab = active != nullptr;
      g_level = active;
      g_lab_run = lab;
      if (const char *r3c_start = R3cLevelStart(base, now, run_key, lab))
      {
        R3cClearLayerMemory(base, r3c_start); // round 3c (fix 1): everything the layer remembers of the level, at once
      }
      if (lab && (run_key != g_run_script || now + 1.0f < g_prev_clock))
      {
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        g_members.clear();
        g_survey_seen.clear();
        g_jo_zone_seen = -2;
        g_jo_inside = -1;
        g_loud = g_loud_released = false;
        g_chars.clear();
        g_guards.clear();
        g_story_sound = 0;
        g_shots.clear();
        g_blasts.clear();
        g_setting_logged = 0xFFFFFFFFu;
        g_tannoy_takes = 0;
        g_allclear_at = g_allclear_until = -1e9f;
        g_lamps_out.clear();
        g_dead_lamps.clear();
        g_take_orders.clear();
        g_next_brief = false;
        g_name_owners.clear();
        g_squad_hides.clear();
        g_hunts.clear();
        g_kills.clear();
        g_squad_deaths.clear();
        g_squad_escalated.clear();
        g_alarm_not_before = -1e9f;
        g_alarm_runner = g_alarm_deferred = 0;
        g_corpses.clear();
        g_voice_last = -1e9f;
        g_rare_last = -1e9f;
        g_tannoy_want = -1.0f;
        g_tannoy_last = -1e9f;
        g_tannoy_by = 0;
        g_lines.clear();
        g_said_lines.clear();
        g_holds = 0;
        g_mutter_next = -1.0f;
        g_last_line_end = -1e9f;
        g_any_sighting = false;
        g_alarm_was = false;
        g_jo_t = -1.0f;
        g_alarm_on = false;
        g_alarm_tick = -1.0f;
        g_alarm_no = 0;
        g_alarms_ended = 0;
        g_alarm_by_us = g_alarm_by_test = false;
        g_waves_held = g_waves_passed = g_waves_spawned = 0;
        g_census.clear();
        g_stack_deepest = 0;
        g_mesa_done = false;
        g_combat_note = false; // (follow-up Sun 15 Nov, item 4: its log line once per level load)
        g_combat_exempt = 0;
        g_run_script = run_key;
        if (script != 0)
        {
          REXLOG_INFO("pdz_lab_stealth: Laboratory run (script {:08X}): guard stealth on", script);
        }
        else
        {
          REXLOG_INFO("pdz_stealth: {} run: the stealth layer is on for its zone", active->name);
        }
      }
      if (!lab && row && now + 1.0f < g_prev_clock)
      {
        // (A supported level with the layer off: the survey lines start over for each run.)
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        g_survey_seen.clear();
        g_jo_zone_seen = -2;
      }
      g_prev_clock = now;
      JoZoneTick(base, now);
      if (lab)
      {
        const bool alarm = AlarmOn(base);
        if (alarm != g_alarm_was && Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: ALARM {} (level alarm byte)", alarm ? "raised" : "off");
        }
        g_alarm_was = alarm;
        if (!g_loud && REXCVAR_GET(pdz_lab_stealth_loud) && script != 0) // (the Laboratory's script flags only)
        {
          // Round 11: the alarm, or the generator (the Laboratory script's flags 37 / 38: the switch was pulled, the
          // lights are out; 15: it was blown up).
          const uint32_t flags = Load32(base, script + 1004);
          const uint32_t w0 = Ptr(flags) ? Load32(base, flags) : 0, w1 = Ptr(flags) ? Load32(base, flags + 4) : 0;
          if (alarm)
          {
            GoLoud(base, "the alarm was raised");
          }
          else if (w1 & 0x60)
          {
            GoLoud(base, "the generator switch was thrown (Rare's blackout)");
          }
          else if (w0 & 0x8000)
          {
            GoLoud(base, "the generator was blown up");
          }
        }
      }
      g_lab = lab && !g_loud;
    }
    return g_lab;
  }
  // The Laboratory runs with the switch on, loud or not: for the crash guards, which stay on after Jo went loud.
  bool LabRun(const uint8_t *base)
  {
    LabActive(base);
    return g_lab_run && REXCVAR_GET(pdz_lab_guard_stealth);
  }

  bool Hunter(const Guard &g)
  {
    return g.st == St::Hunting || g.st == St::HuntPause;
  }
  // After a lost contact (the "she" lines, SEE_PLAYER_AGAIN): Lost / LostSearch, or a hunter who saw or fought her
  // (a hunter from a body or a squad mate's death alone never saw her).
  bool AfterContact(const Guard &g)
  {
    return g.st == St::Lost || g.st == St::LostSearch || (Hunter(g) && (g.seen_jo || g.fought));
  }
  // Round 4: an investigator who was wary before keeps the wary meter rules.
  bool WaryLike(const Guard &g)
  {
    return g.st == St::Wary || (g.st == St::Investigating && g.was_wary);
  }
  // Round 5 (item 11): lost contact, or a COMBAT of Rare's that did not end though he has not seen her since.
  bool LostLike(const Guard &g)
  {
    return REXCVAR_GET(pdz_lab_stealth_lost_metered) && !g.scientist && (g.st == St::Lost || (g.st == St::Combat && g.stale));
  }
  float Threshold(const Guard &g)
  {
    return WaryLike(g) ? float(REXCVAR_GET(pdz_lab_stealth_wary_combat))
           : Hunter(g) || LostLike(g) ? float(REXCVAR_GET(pdz_lab_stealth_hunt_combat))
                            : 100.0f;
  }
  float Notice(const Guard &g)
  {
    const float n = float(REXCVAR_GET(pdz_lab_stealth_notice));
    return WaryLike(g) || Hunter(g) ? n * 0.5f : n;
  }
  float Floor(const Guard &g)
  {
    return WaryLike(g) ? float(REXCVAR_GET(pdz_lab_stealth_wary_floor))
           : Hunter(g) || LostLike(g) ? float(REXCVAR_GET(pdz_lab_stealth_hunt_floor))
                            : 0.0f;
  }

  void Set(const uint8_t *base, uint32_t c, Guard &g, St s, float now, const std::string &why)
  {
    // Round 11i (item 3): a check, investigation, search or hunt of ours ends calm or wary: he is due back at his
    // post (PostTick walks him there). Any other new state drops a walk back that is due or running.
    if ((s == St::Wary || s == St::Unaware) &&
        (g.st == St::Investigating || g.st == St::Searching || g.st == St::LostSearch || g.st == St::Hunting ||
         g.st == St::HuntPause || g.st == St::Suspicious))
    {
      g.post_due = now;
      g.post_tries = 0;
    }
    else if (s != St::Wary && s != St::Unaware)
    {
      g.post_due = -1e9f;
      g.post_task = 0;
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} {} -> {} ({}), meter {:.0f}{}", Id(base, c), StName(g.st), StName(s), why, g.meter,
                  g.confirmed ? ", confirmed" : "");
    }
    const bool was_hunter = Hunter(g);
    if (s != St::Investigating && g.inv != Inv::None)
    {
      // Round 4: leaving an investigation (done, combat, death): the body he was looking at waits for the next guard.
      if (g.inv_body >= 0 && size_t(g.inv_body) < g_kills.size() && g_kills[g.inv_body].finder == c)
      {
        g_kills[g.inv_body].finder = 0;
      }
      g.inv = Inv::None;
      g.inv_pad = 0;
      g.inv_redirect = 0;
      g.inv_end_leg = false;
    }
    g.st = s;
    g.since = now;
    g.stale = false;
    if (s == St::Combat)
    {
      g.meter = 100.0f; // he knows she is here; Rare's sight runs unmetered
    }
    if (s == St::Lost && REXCVAR_GET(pdz_lab_stealth_lost_metered) && !g.scientist)
    {
      g.meter = float(REXCVAR_GET(pdz_lab_stealth_hunt_floor)); // round 5: he looks for her through the meter again
    }
    if (s == St::Wary)
    {
      g.meter = std::max(g.meter, float(REXCVAR_GET(pdz_lab_stealth_wary_floor)));
      const float bark = float(REXCVAR_GET(pdz_lab_stealth_wary_bark_s));
      g.next_bark = bark > 0.0f ? now + bark * (0.5f + Rand01()) : 0.0f;
    }
    if (Hunter(g))
    {
      const float floor = float(REXCVAR_GET(pdz_lab_stealth_hunt_floor));
      // From combat (100) or lost (lost_start) down to the floor: above the hunt threshold any look would be combat.
      g.meter = was_hunter ? std::max(g.meter, floor) : floor;
      if (!was_hunter)
      {
        // The first call-out: pdz_lab_stealth_still_hidden_s into the hunt (Rare's STILL_HIDDEN moment), spread.
        const float first = float(REXCVAR_GET(pdz_lab_stealth_still_hidden_s));
        const float bark = float(REXCVAR_GET(pdz_lab_stealth_hunt_bark_s));
        g.next_bark = bark > 0.0f ? now + (first > 0.0f ? first : bark) * (0.8f + 0.4f * Rand01()) : 0.0f;
      }
    }
  }

  // --- Round 3: takes and the voice budget for every guard line ---

  // Per trooper voice and cue (lab_stealth_takes.inc): takes naming Jo "he" / "she", whispered takes, lengths.
  struct TakeSet
  {
    uint32_t voice;
    const char *cue;
    uint32_t n, he, she, quiet;
    float secs[20];
  };
  const TakeSet kTakes[] = {
#include "lab_stealth_takes.inc"
  };
  std::unordered_map<uint32_t, const TakeSet *> g_take_cache; // (voice << 8 | event) -> set or nullptr

  const TakeSet *Takes(const uint8_t *base, uint32_t voice, uint32_t ev)
  {
    const uint32_t key = (voice << 8) | (ev & 0xFF);
    const auto it = g_take_cache.find(key);
    if (it != g_take_cache.end())
    {
      return it->second;
    }
    const std::string name = EventName(base, ev);
    const TakeSet *found = nullptr;
    for (const TakeSet &s : kTakes)
    {
      if (s.voice == voice && name == s.cue)
      {
        found = &s;
        break;
      }
    }
    g_take_cache[key] = found;
    return found;
  }

  const TakeSet *TakesByCue(uint32_t voice, const std::string &cue)
  {
    for (const TakeSet &s : kTakes)
    {
      if (s.voice == voice && cue == s.cue)
      {
        return &s;
      }
    }
    return nullptr;
  }

  // The takes of `set` this guard may say now (bit i = take i + 1): never "he"; "she" only after his sighting or a lost
  // contact; beyond 8 m from Jo not whispered (if any other is left); not his previous take of the cue.
  // Round 4: in the SEE_BODY cues "he" is the dead guard, not Jo, so those takes may play.
  bool HeIsTheBody(const char *cue)
  {
    return std::strncmp(cue, "SEE_BODY", 8) == 0;
  }
  uint32_t AllowedTakes(const TakeSet &set, bool may_she, float dist_jo, uint8_t last)
  {
    const uint32_t all = set.n >= 32 ? 0xFFFFFFFFu : (1u << set.n) - 1;
    uint32_t allowed = HeIsTheBody(set.cue) ? all : all & ~set.he;
    if (!may_she)
    {
      allowed &= ~set.she;
    }
    if (dist_jo > 800.0f && (allowed & ~set.quiet))
    {
      allowed &= ~set.quiet;
    }
    if (last >= 1 && (allowed & ~(1u << (last - 1))))
    {
      allowed &= ~(1u << (last - 1));
    }
    return allowed;
  }

  int PickTake(uint32_t allowed, uint32_t n, float r)
  {
    int bits[32];
    int nb = 0;
    for (uint32_t i = 0; i < n && i < 32; ++i)
    {
      if (allowed & (1u << i))
      {
        bits[nb++] = int(i);
      }
    }
    return nb ? bits[std::min(nb - 1, int(r * float(nb)))] + 1 : 0;
  }

  // Round 11k (item 3): SHARED VOICE SETS. Several characters speak with the same voice set (three trooper sets, two
  // scientist sets, two women's). Every take that is primed for a tracked speaker is remembered by its full cue name
  // (TROOPER1_HEAR_UNKNOWN_TEAM): a take said by the set in the last pdz_lab_stealth_voice_set_s is not picked again,
  // and while a line of the set is really playing, another speaker of the set waits (the speech task hook).
  struct SetPlay
  {
    std::string cue;
    int take = 0;
    float at = 0.0f, end = 0.0f;
    uint32_t owner = 0, sound = 0;
    bool played = false;
  };
  std::vector<SetPlay> g_set_plays;
  std::unordered_map<std::string, uint32_t> g_cue_n; // cue -> its number of takes (learned at the variation pick)
  struct SetRelease
  {
    float at = -1e9f;
    uint32_t c = 0;
  };
  std::unordered_map<std::string, SetRelease> g_set_release; // voice set -> whose line was let go last
  const char *g_next_swap = nullptr; // the next warning line queued plays as this cue (WarnGlimpse)
  uint32_t g_next_swap_takes = 0;
  float g_loud_commit_until = -1e9f; // after LOUD: an announced alarm runner may be committed until then

  const char *VoiceSetName(uint32_t voice)
  {
    switch (voice)
    {
    case 15: return "SCI1";
    case 16: return "SCI2";
    case 17: return "TROOPER1";
    case 18: return "TROOPER2";
    case 19: return "TROOPER3";
    case 21: return "WOMAN1";
    case 22: return "WOMAN2";
    default:
      // Stealth rollout: on a zone level, any other guard voice of the level (GANGSTA1-3, ELITE1-3 ...) is his own
      // set too (the Laboratory: unchanged, ZoneLevel() is false there).
      return ZoneLevel() && GuardVoice(voice) ? VoiceTableName(voice) : nullptr;
    }
  }
  // Round 11l: the take memory and the one-speaker wait are for the characters' SPEECH voice sets only: a cue whose
  // name starts with a voice set (TROOPER1_, SCI2_, WOMAN1_ ...). Never a stock sound, a siren, an alarm, a door, a
  // prop, music or story speech (LV05_SPEECH_*).
  bool SpeechSetCue(const std::string &cue)
  {
    static const char *const kSets[] = {"TROOPER1_", "TROOPER2_", "TROOPER3_", "SCI1_", "SCI2_", "WOMAN1_", "WOMAN2_"};
    for (const char *set : kSets)
    {
      if (cue.compare(0, std::strlen(set), set) == 0)
      {
        return true;
      }
    }
    for (uint32_t v = 0; ZoneLevel() && v < 26; ++v) // (stealth rollout: the zone level's other guard voices)
    {
      const char *set = VoiceTableName(v);
      const size_t n = std::strlen(set);
      if (GuardVoice(v) && cue.size() > n && cue.compare(0, n, set) == 0 && cue[n] == '_')
      {
        return true;
      }
    }
    return false;
  }
  // The takes of `cue` (bit i = take i + 1) its voice set said in the window.
  uint32_t RecentTakes(const std::string &cue, float now)
  {
    const float window = float(REXCVAR_GET(pdz_lab_stealth_voice_set_s));
    uint32_t mask = 0;
    if (window <= 0.0f)
    {
      return 0;
    }
    for (const SetPlay &p : g_set_plays)
    {
      if (p.take >= 1 && p.take <= 32 && now >= p.at && now - p.at < window && p.cue == cue)
      {
        mask |= 1u << (p.take - 1);
      }
    }
    return mask;
  }
  // `allowed` without the takes said in the window. None left: `all_recent`, and the one said longest ago.
  uint32_t NotRecent(const std::string &cue, uint32_t allowed, float now, bool &all_recent)
  {
    const uint32_t recent = RecentTakes(cue, now);
    all_recent = allowed != 0 && (allowed & ~recent) == 0;
    if (!all_recent)
    {
      return allowed & ~recent;
    }
    int best = -1;
    float best_at = 1e9f;
    for (int i = 0; i < 32; ++i)
    {
      if (!(allowed & (1u << i)))
      {
        continue;
      }
      float last = -1e9f;
      for (const SetPlay &p : g_set_plays)
      {
        if (p.take == i + 1 && p.cue == cue && p.at <= now)
        {
          last = std::max(last, p.at);
        }
      }
      if (last < best_at)
      {
        best_at = last;
        best = i;
      }
    }
    return best >= 0 ? 1u << best : allowed;
  }
  void NoteSetPrime(const std::string &cue, int take, uint32_t owner, uint32_t sound, float now)
  {
    g_set_plays.erase(std::remove_if(g_set_plays.begin(), g_set_plays.end(),
                                     [now](const SetPlay &p) { return now - p.at > 90.0f || p.at > now + 1.0f; }),
                      g_set_plays.end());
    if (g_set_plays.size() > 256)
    {
      g_set_plays.erase(g_set_plays.begin(), g_set_plays.begin() + 64);
    }
    SetPlay p;
    p.cue = cue;
    p.take = take;
    p.at = now;
    p.end = now;
    p.owner = owner;
    p.sound = sound;
    g_set_plays.push_back(p);
  }
  void NoteSetPlay(uint32_t sound, uint32_t owner, float now, float len)
  {
    for (auto r = g_set_plays.rbegin(); r != g_set_plays.rend(); ++r)
    {
      if (r->sound == sound && r->owner == owner && !r->played)
      {
        r->played = true;
        r->at = now;
        r->end = now + len + 0.25f;
        return;
      }
    }
  }
  // A line of this voice set is really playing now, from another speaker than `self`.
  bool SetBusy(const uint8_t *base, const char *set, uint32_t self, float now, uint32_t *who = nullptr)
  {
    const size_t n = std::strlen(set);
    for (const SetPlay &p : g_set_plays)
    {
      if (p.played && p.owner != self && now >= p.at && now < p.end && p.cue.size() > n && p.cue[n] == '_' &&
          p.cue.compare(0, n, set) == 0 && Ptr(p.sound) && Load32(base, p.sound + 340) == 4)
      {
        if (who)
        {
          *who = p.owner;
        }
        return true;
      }
    }
    return false;
  }

  thread_local int t_force_take = 0; // the take (1-based) the variation pick must return for the line being started
  thread_local uint32_t t_force_n = 0; // ... out of this many (a check against the cue's own variation count)
  thread_local uint32_t t_force_mask = 0; // round 11b: or any take of this mask (a line played on a speaker)
  thread_local bool t_ordered = false;    // ... the sound being primed is such a line

  bool Fighting();
  void Mutter(PPCContext &ctx, uint8_t *base, uint32_t jo, float now);
  void IncidentTick(PPCContext &ctx, uint8_t *base, float now);
  void DeathTick(PPCContext &ctx, uint8_t *base, float now);
  void SciTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now);
  bool SciHold(const uint8_t *base, uint32_t c, Guard &g, float now);
  void DarkLampTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool busy);
  void LampTalkTick(PPCContext &ctx, uint8_t *base, float now);
  void StaffWitness(PPCContext &ctx, uint8_t *base, uint32_t victim, float now); // (lab_stealth_hurt_Sun15Nov.inc)
  void StaffWitnessTick(uint8_t *base, uint32_t c, Guard &g, float now);
  // Wed 18 Nov night (lab_stealth_r9_Wed18Nov.inc): seen or loud = Rare's guard; the quiet-part fixes.
  bool R9Tick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search);
  bool R9Make(const uint8_t *base, uint32_t c, Guard &g, int rule, const std::string &why, float now);
  const char *R9PlainView(const float eye[3], const float at[3], float &dist);
  bool R9Stimulus(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now);
  void R9WalkNote(const uint8_t *base, uint32_t c, uint32_t task);
  bool R9BodyFollowUp(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, bool found_by_rare, float now);
  void R9LateBodyOffer(const uint8_t *base, uint32_t c, Guard &g, float now);
  bool R9IsRares(uint32_t c);
  void R9Reset();
  // Thu 19 Nov (lab_stealth_r10_Thu19Nov.inc): far gunfire, second body, no give-up after a body, no freezing, lamps.
  float g_r10_lamp_age = 0.0f; // the lamp watch sets it around its LightIncident call: seconds since the glass broke
  bool R10FarOn();
  bool R10FarStimulus(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now, uint32_t &drop);
  void R10RaresTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search);
  void R10CalmTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search);
  bool R10SecondBody(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, const std::string &who, float now);
  bool R10NoGiveUpAfterBody(const uint8_t *base, uint32_t c, Guard &g, float now, const char *what);
  bool R10PostNoRoute(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const std::string &why);
  bool R10BodyStill(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, float now);
  void R10LampCause(const uint8_t *base, uint32_t c, const float lamp[3], float d, bool saw, bool heard, int walls, const char *seen_how);
  bool R10LampHearFresh(const char *&why_not);
  // Round 2, Sun 15 Nov (lab_stealth_round2_Sun15Nov.inc): the search cap, blind combat, a witness from any squad.
  bool R2SearchCap(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age, bool is_top, bool settled);
  bool R2NoRestart(const uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, const char *what);
  uint32_t R2StimulusDrop(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now);
  bool R2BlindCombat(uint8_t *base, uint32_t c, Guard &g, bool sighting, float now);
  bool R2BlindLine(const uint8_t *base, uint32_t c, const Guard &g, float now);
  // Stealth4 quick job, Mon 16 Nov (lab_stealth_r4_A_Mon16Nov.inc): the kill and COMBAT START lines, sneaking up behind.
  bool R4ASneakOn();
  void R4AGait(PPCContext &ctx, uint8_t *base, uint32_t jo, float now);
  bool R5ASprinting(float now); // (round 5, fix 1)
  void R5ADown(const uint8_t *base, uint32_t c, Guard &g, float now, float hp); // (round 5, fix 6; lab_stealth_r5_A_Mon16Nov.inc)
  void R5AMeleeBefore(const uint8_t *base, uint32_t attacker);
  void R5AMeleeAfter(const uint8_t *base, uint32_t attacker);
  bool R4AStepsCount(const uint8_t *base, uint32_t c, const Guard &g, uint32_t jo, float d, float now, float &heard_cm, bool &bump);
  bool R4AHeard(const uint8_t *base, uint32_t c, Guard &g, uint32_t jo, bool bump, float d, float now);
  bool R4ASneakHides(const uint8_t *base, uint32_t viewer, Guard &g, uint32_t jo, const float eye[3], const float dir[3],
                     const float at[3], float now);
  bool R4ASneakBlind(const uint8_t *base, uint32_t c, const Guard &g, float now);
  void R4ACombatLog(const uint8_t *base, uint32_t c, const Guard &g, bool sighting, float now, uint32_t caller);
  void R2Tick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search);
  // Round 3, Sun 15 Nov (lab_stealth_r3_L_Sun15Nov.inc): every search end has a give-up line.
  void R3EndLine(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const char *why);
  void R3EndTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat);
  void R3RareAbandon(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now);
  // Round 3, Sun 15 Nov (lab_stealth_r3_W_Sun15Nov.inc, helper W): explosion muffle, tower witness, one witness event
  // per body, the boost limit.
  float R3WWithin(const uint8_t *base, uint32_t c, uint32_t source, const float src[3], float reach, int walls, float d, float now);
  bool R3WTowerKill(const uint8_t *base, uint32_t c, Guard &g, const Kill &k, float d);
  uint32_t R3WStimulusDrop(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now);
  float R3WBoostCap(const uint8_t *base, uint32_t c, Guard &g, float rate, float d, float now);
  void R3WFarDark(const uint8_t *base, uint32_t c, Guard &g, float f, float d, float before, float close_gain, float now);
  // Round 3, Sun 15 Nov (lab_stealth_r3_H_Sun15Nov.inc, helper H): heard means a reaction, the witness walk to the
  // body, an alarm shout is heard.
  void R3HTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search);
  bool R3HInvOurs(const Guard &g, uint32_t c);
  // Round 3b, Sun 15 Nov (lab_stealth_r3b_Sun15Nov.inc, helper B): the scientists' witness uses the guards' test (the
  // test itself is round 2's R2WitnessSees), one reaction per body found, search restarts keep the cap, the witness
  // walk to the body whatever Rare does.
  const char *R2WitnessSees(PPCContext &ctx, uint8_t *base, uint32_t c, const Guard &g, const float body[3], float now, float eye_up,
                            float &d, float &scale, bool &beside);
  std::string R3BSaysName(const uint8_t *base, const Guard &g, uint32_t event, bool warn);
  uint32_t R3BBodyDrop(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now);
  void R3BCause(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now);
  void R3BSearchStart(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now);
  int R3BEpisode(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age);
  bool R3BCappedRestart(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age, bool is_top, bool settled);
  bool R3BTakeOver(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age, bool is_top, bool settled);
  void R3BTestKillAt(PPCContext &ctx, uint8_t *base, uint32_t jo, float now);
  // Round 3c, Sun 15 Nov night (lab_stealth_r3c_{A,B,C,D}_Sun15Nov.inc). A: the layer's per-level memory is cleared on
  // every level start / restart / reload (R3cClearLayerMemory calls each file's own reset). B: only lights that really
  // go out count; Jo's shots. C: witness tuning. D: Mansion radio reinforcements under the layer; tower restarts.
  void R3cClearLayerMemory(const uint8_t *base, const char *why);
  void R3cATestKillJo(uint8_t *base, uint32_t jo, float now); // (A, testing: pdz_test_stealth_kill_jo)
  void R3cBReset();
  void R3cBLampTick(PPCContext &ctx, uint8_t *base, float now); // (B: the watched lights; pdz_test_stealth_light_shot)
  void R3cBJoShot(const uint8_t *base, uint32_t weapon, uint32_t mode, uint32_t caller, bool silenced, float now);
  void R3cBJoHit(const uint8_t *base, uint32_t victim, float before, float after, float now);
  bool R3cBLampShot(const uint8_t *base, uint32_t light, const float hit[3], uint32_t prop, uint32_t caller, uint32_t before,
                    uint32_t after, int lamps);
  void R3cCReset();
  // (helper C, fix 4: witness tuning)
  void R3cCSightArgs(uint32_t viewer, float range2, float back, float k, float slope, float back_scale, bool need_cone, bool near_flag,
                     float now);
  const char *R3cCSees(PPCContext &ctx, uint8_t *base, uint32_t c, const Guard &g, const float body[3], float now, float eye_up,
                       float cap_cm, float &d, float &scale, bool &beside, bool &in_cone);
  uint32_t R3cCStimulus(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now, uint32_t &keep);
  void R3cCTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search);
  bool R3cCWalkOver(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, int kill);
  bool R3cCBodyHidden(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t victim, const float *where, float now);
  bool R3cCStaffSoft(const uint8_t *base, uint32_t c, Guard &g, const std::string &victim, float d, float scale, float now);
  bool R3cCStaffIsSoft(uint32_t c, const Guard &g);
  void R3cCTestWitness(const uint8_t *base, float now);
  bool R3cCSlowStart(uint32_t c, const Guard &g, double age);
  void R3cCTestWhen(PPCContext &ctx, uint8_t *base, float now);
  bool R3cCBegin(uint8_t *base, uint32_t c, Guard &g, int pass, float pd, float ps, bool pc, float now);
  bool R3cCSoundOnce(const uint8_t *base, uint32_t c, Guard &g, float now);
  void R3cCRefused(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, const std::string &who, float kill_at, float d,
                   float scale, const char *why);
  void R3cDReset();
  bool R3cDInvOurs(const Guard &g, uint32_t c);
  void R3cDTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search);
  void R3cDTestRadio(const uint8_t *base, float now);
  bool R3cDTowerRestart(const uint8_t *base, uint32_t c, const Guard &g, int restarts, float now);
  void R4CReset(); // (round 4 C, Mon 16 Nov: lab_stealth_r4_C_Mon16Nov.inc)
  void R4CCue(const uint8_t *base, uint32_t name);
  void R4CTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search);
  void R5CReset(); // (round 5 C, Mon 16 Nov: lab_stealth_r5_C_Mon16Nov.inc: a shot-down security camera is a noise)
  void R5CTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search);
  void R5CPropHit(const uint8_t *base, uint32_t prop, uint32_t vtable, const char *name, const char *model, const float pos[3],
                  float before, float after, bool gone);
  // (overnight Mon 16 / Tue 17 Nov: lab_stealth_r6_Tue17Nov.inc: the unarmed truce, the friend alert log, cameras)
  void R6Reset();
  bool R6Truce(const uint8_t *base, uint32_t c, uint32_t jo);
  bool R6TruceTick(const uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now);
  void R6AlertNote(const uint8_t *base, uint32_t c, uint32_t source, int kind, float now);
  void R6AlertWord(const uint8_t *base, uint32_t c, uint32_t word, uint32_t inputs_before, uint32_t flags_before, float now);
  void R6FriendAlertLog(const uint8_t *base, uint32_t c, const Guard &g, float now);
  bool R6CameraWords(const char *name, const char *model);

  // A guard line may play now? nullptr = yes, else the reason. In a fight (a tracked guard in combat or losing
  // contact): Rare's lines, at most pdz_lab_stealth_combat_voices at once. Otherwise one voice at a time, with
  // pdz_lab_stealth_voice_gap_s of quiet after the previous line (optional call-outs a second more). Always: not the
  // same line from the same guard within pdz_lab_stealth_repeat_s. `exempt` (death and pain): only the repeat rule.
  // Round 10: `alert` (a sighting, the alarm call, shot / hurt, the body call-in): the one-voice rule does not apply
  // (it is for chatter); the fight's cap does.
  // Follow-up Sun 15 Nov (item 1): the "one voice at a time" rule is scrapped. No line is refused, dropped or held
  // back because ANOTHER speaker is talking or about to: not the one-voice rule, not the fight's cap on lines at
  // once, not "a warning line is due near him". What stays: Rare's own speech rules, our repeat rule, the take
  // rules, story speech first, and a speaker's own lines (one mouth). pdz_stealth_one_voice = true brings the old
  // rules back.
  bool OneVoice() { return REXCVAR_GET(pdz_stealth_one_voice); }
  // Follow-up Sun 15 Nov (item 16): Rare's own shot / hurt lines (BEEN_SHOT_*, MINOR_HURT, PAIN) are not refused by
  // OUR repeat rule; only Rare's speech rules apply to them.
  bool HurtLineName(const std::string &n)
  {
    return n.find("BEEN_SHOT") != std::string::npos || n.find("HURT") != std::string::npos || n.find("PAIN") != std::string::npos;
  }
  // Follow-up Sun 15 Nov (item 16): he was hurt by Jo (a hit, a near miss Rare counted on him, an explosion): his
  // meter is full for good, his sight of her is Rare's own from then on, and he gets no warning line.
  bool HurtKnown(const Guard &g) { return g.hurt_full_at > -1e8f && REXCVAR_GET(pdz_stealth_hurt_full); }
  // Follow-up Sun 15 Nov (item 9): the order is always notice -> stronger -> sighting. Once he has had his sighting
  // line (for 30 s), or Rare's COMBAT has started for him, or he was hurt by Jo, no warning line is queued or played
  // for him (nullptr = a warning line may still come).
  // (`what`: the meter's own lines are the "glimpse ..." ones; a lamp or a sound heard later is a new thing to say.)
  const char *WarnOver(const Guard &g, float now, const char *what)
  {
    if (HurtKnown(g))
    {
      return "he was hurt by Jo (no warning line after that)";
    }
    if (g.st == St::Combat || g.st == St::Lost || g.combat_script != 0)
    {
      return "he is fighting now (Rare's own lines)";
    }
    if (!g.scientist && what && std::strncmp(what, "glimpse", 7) == 0 &&
        (g.spotted || (now >= g.sighting_said && now - g.sighting_said < 30.0f)))
    {
      return "he has seen her (his sighting line): the order is notice, stronger, sighting, never a warning after it";
    }
    return nullptr;
  }
  const char *VoiceRefusal(const Guard &g, uint32_t ev, float now, bool optional, bool exempt, bool alert = false,
                           uint32_t self = 0, const uint8_t *base = nullptr, bool no_repeat_rule = false)
  {
    g_lines.erase(std::remove_if(g_lines.begin(), g_lines.end(),
                                 [now](const Line &l) { return l.end < now - 15.0f || l.start > now + 1.0f; }),
                  g_lines.end());
    const auto it = g.said_at.find(ev);
    if (!no_repeat_rule && it != g.said_at.end() && now >= it->second &&
        now - it->second < float(REXCVAR_GET(pdz_lab_stealth_repeat_s)))
    {
      return "he said this line less than pdz_lab_stealth_repeat_s ago";
    }
    if (exempt || !OneVoice())
    {
      return nullptr; // (follow-up Sun 15 Nov, item 1: never refused because another speaker is talking)
    }
    // A line holds its slot while it plays, and from its request until it really starts (Rare's speech task waits up
    // to 2 s for the squad arbiter: research takes_and_playlog.md step 4), so a late start cannot overlap another.
    const auto holds = [now](const Line &l) {
      return l.end > now || (!l.played && now >= l.start && now - l.start < 2.5f);
    };
    if (Fighting())
    {
      const int max_voices = std::max(1, int(REXCVAR_GET(pdz_lab_stealth_combat_voices)));
      // Round 10b: an alert line cuts the speaker's own chatter line, so that line does not count against it.
      const int playing = int(std::count_if(g_lines.begin(), g_lines.end(), [&](const Line &l) {
        return holds(l) && !(alert && self && l.c == self && !l.alert);
      }));
      return playing >= max_voices ? "fight: the most guard lines at once are already playing" : nullptr;
    }
    if (alert)
    {
      return nullptr;
    }
    const float gap = float(REXCVAR_GET(pdz_lab_stealth_voice_gap_s)) + (optional ? 1.0f : 0.0f);
    const float near_cm = float(REXCVAR_GET(pdz_lab_stealth_voice_near_cm));
    if (base && self && near_cm > 0.0f)
    {
      // Round 11g (item 1): one voice at a time only between speakers within pdz_lab_stealth_voice_near_cm of each
      // other (a scripted staff line has no speaker on record: it counts as near).
      const bool busy = std::any_of(g_lines.begin(), g_lines.end(), [&](const Line &l) {
        if (!(holds(l) || (now >= l.end && now - l.end < gap)))
        {
          return false;
        }
        return !Ptr(l.c) || l.c == self || Dist(base, self, l.c) <= near_cm;
      });
      return busy ? "another line is playing within pdz_lab_stealth_voice_near_cm of him (one voice at a time)" : nullptr;
    }
    if ((now >= g_last_line_end - 30.0f && now < g_last_line_end + gap) || std::any_of(g_lines.begin(), g_lines.end(), holds))
    {
      return "another guard line is playing (one voice at a time)";
    }
    return nullptr;
  }

  void Admit(uint32_t c, Guard &g, uint32_t ev, float now, float len, bool alert = false)
  {
    Line l;
    l.alert = alert;
    l.start = now;
    l.end = now + len;
    l.c = c;
    l.ev = ev;
    l.stealth = !Fighting();
    g_lines.push_back(l);
    ++g_admit_serial;
    if (l.end > g_last_line_end || g_last_line_end > now + 30.0f)
    {
      g_last_line_end = l.end;
    }
    g.said_at[ev] = now;
  }

  Guard *Find(uint32_t c);

  // Round 10: the ALERT lines (the design rule): a sighting, the alarm call, shot / hurt, the body call-in.
  bool StrongAlertEvent(const uint8_t *base, uint32_t ev)
  {
    const std::string n = EventName(base, ev);
    return n.find("SEE_PLAYER") != std::string::npos || ev == 143 || n.find("SEE_BODY_TEAM") != std::string::npos ||
           (RarePlus() && n.find("SEE_BODY") != std::string::npos); // (rareplus item 9: Rare's body lines are always heard)
  }
  bool RadioEvent(uint32_t ev);
  bool AlertEvent(const uint8_t *base, uint32_t ev)
  {
    const std::string n = EventName(base, ev);
    return StrongAlertEvent(base, ev) || RadioEvent(ev) || // (Mansion helper: a waiting radio line is never cut) n.find("BEEN_SHOT") != std::string::npos || n.find("HURT") != std::string::npos ||
           n.find("PAIN") != std::string::npos;
  }

  // Round 10b: the budget's slot of a line that will not play is given back.
  void FreeSlot(uint32_t c, uint32_t ev)
  {
    for (auto it = g_lines.rbegin(); it != g_lines.rend(); ++it)
    {
      if (it->c == c && it->ev == ev && !it->played)
      {
        g_lines.erase(std::next(it).base());
        break;
      }
    }
    float end = -1e9f;
    for (const Line &l : g_lines)
    {
      end = std::max(end, l.end);
    }
    g_last_line_end = end;
  }

  // Round 10b: a line our code let through that Rare's speech lost: said plainly, with the reason.
  void NotPlayed(const uint8_t *base, const Said &s, float now, const std::string &why)
  {
    if (Guard *g = Find(s.c))
    {
      const auto it = g->said_at.find(s.ev);
      if (it != g->said_at.end() && it->second == s.at)
      {
        g->said_at.erase(it); // not said: the repeat rule does not count it
      }
    }
    FreeSlot(s.c, s.ev);
    if (g_mutter_wait == s.c)
    {
      g_mutter_skip[s.c] = now; // another searcher mutters instead
      g_mutter_wait = 0;
      g_mutter_next = now;
    }
    ++g_not_played;
    if (RadioEvent(s.ev) && StealthLog())
    {
      REXLOG_INFO("pdz_stealth_radio: {} ANNOUNCED CALL line NOT STARTED: {} ({:.2f} s after it was asked for)", Id(base, s.c),
                  why, now - s.at);
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_voice_drop: {} {} ({}, {}) said {:.2f} s ago, NOT PLAYED: {} | not played so far {}", Id(base, s.c),
                  EventName(base, s.ev), s.ev, s.what, now - s.at, why, g_not_played);
    }
  }

  // Round 10b: a strong alert line cuts the speaker's own chatter line: its trace, its slot and its play end here.
  void CutPlays(uint32_t c, float now);
  void CutOwnLine(const uint8_t *base, uint32_t c, float now, uint32_t ev, bool started, const std::string &by)
  {
    for (auto it = g_said_lines.begin(); it != g_said_lines.end();)
    {
      it = it->c == c ? g_said_lines.erase(it) : std::next(it);
    }
    for (auto it = g_lines.begin(); it != g_lines.end();)
    {
      if (it->c == c && !it->played)
      {
        it = g_lines.erase(it);
        continue;
      }
      if (it->c == c && it->end > now)
      {
        it->end = now;
      }
      ++it;
    }
    float end = -1e9f;
    for (const Line &l : g_lines)
    {
      end = std::max(end, l.end);
    }
    g_last_line_end = end;
    CutPlays(c, now);
    if (g_mutter_wait == c)
    {
      g_mutter_wait = 0;
    }
    if (!started)
    {
      ++g_not_played;
    }
    if (Logging())
    {
      REXLOG_INFO("{}: {} {} ({}) {}: his alert line {} takes its place (it starts when his speech is free)",
                  started ? "pdz_voice_cut" : "pdz_voice_drop", Id(base, c), EventName(base, ev), ev,
                  started ? "was playing and is CUT" : "was waiting to start, NOT PLAYED", by);
    }
  }

  // Rare's say-event, as restorations.cpp raises it (r5 = 1: Jo, the plain cue, never _MALE). The say-event hook
  // applies the take choice and the voice budget to it like to Rare's own lines. `optional`: a call-out that also
  // waits a second more. False when the budget (or the take rule) refused it.
  const char *g_say_what = "";
  thread_local uint32_t t_name_c = 0; // inside Rare's speech task's cue-name build (sub_8262AF60): the speaker
  // Round 10b: the speech task being updated (the arbiter and the start try inside it are noted for its line).
  thread_local uint32_t t_task = 0, t_task_squad = 0;
  thread_local int t_task_arbiter = -1, t_task_start = -1;
  thread_local bool t_task_strong = false, t_task_overridden = false;
  void DisarmSwap();
  void ArmSwapName(uint8_t *base, uint32_t c, uint32_t voice, const std::string &target, const char *only, float now,
                   const char *only2 = nullptr);
  bool g_say_force = false; // the next Say() of ours skips Rare's gap and category rules (the "All clear" line)
  bool g_warn_saying = false; // round 11b: inside the raise of a warning line (it skips our own line rules)
  bool Say(PPCContext &ctx, uint8_t *base, uint32_t c, uint32_t ev, bool optional, const char *what)
  {
    const Guard *g = Find(c);
    if (g && g->st == St::Dead)
    {
      DisarmSwap();
      return false;
    }
    g_said = false;
    g_our_line = true;
    g_optional = optional;
    g_say_what = what;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c + kVoice;
      frame.ctx.r4.u64 = ev;
      frame.ctx.r5.u64 = 1;
      // r6 != 0: Rare's own "skip the speaker's gap and category rules" (sub_8262AA40: the test at speech +104 / +105
      // and sub_8262B378 are skipped; the event's group rule sub_8262BC00 still applies).
      frame.ctx.r6.u64 = g_say_force ? 1 : 0;
      frame.ctx.f1.f64 = 0.0;
      sub_8262AA40(frame.ctx, base);
    }
    g_our_line = false;
    g_optional = false;
    g_say_what = "";
    return g_said;
  }

  void Confirm(const uint8_t *base, uint32_t c, Guard &g, uint32_t kind, const std::string &why)
  {
    if (!(g.confirm_kinds & kind) && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} CONFIRMED contact: {} (state {}{})", Id(base, c), why, StName(g.st),
                  g.confirmed ? ", already confirmed" : "");
    }
    g.confirm_kinds |= kind;
    g.confirmed = true;
  }

  uint32_t SquadOf(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    const uint32_t level = Load32(base, kLevelState);
    const uint32_t manager = Ptr(level) ? Load32(base, level + 0x2F4) : 0;
    if (!Ptr(manager))
    {
      return 0;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = manager;
    frame.ctx.r4.u64 = c;
    sub_8217D108(frame.ctx, base);
    return Ptr(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
  }

  // Round 11c (item 5; log 005 19:24:22-26.6: Jo left cover crouched, her view stayed down, her body played standing
  // animations and Rare's crouch test said "standing" for four seconds, in which s1m1 made her out). Rare's test
  // (sub_824FE928) reads the BODY: with an animation state object (vtable +664) she is crouched only while the body's
  // current animation is one of the crouch set (ids 76-84), else by the flags +7332 / +7336. The VIEW's crouch is
  // the float at +0x1684 (below 0 while crouched: Rare's own gun-spread cap reads it, 825880E8). For the sight meter
  // and her footsteps she is crouched when either says so; every moment the two disagree is logged.
  bool Crouched(PPCContext &ctx, uint8_t *base, uint32_t jo)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = jo;
    sub_824FE928(frame.ctx, base);
    const bool body = (frame.ctx.r3.u32 & 0xFF) != 0;
    if (jo != Load32(base, kPlayerOne))
    {
      return body;
    }
    const float v = LoadF(base, jo + 0x1684);
    const bool view = std::isfinite(v) && v < 0.0f;
    static bool was = false;
    static float since = 0.0f, logged = -1e9f;
    const bool differ = view != body;
    const bool use_view = REXCVAR_GET(pdz_lab_stealth_view_crouch);
    if (Logging())
    {
      const float now = Clock(base);
      if (differ && (!was || now - logged >= 1.0f || now < logged))
      {
        if (!was)
        {
          since = now;
        }
        logged = now;
        REXLOG_INFO("pdz_lab_stealth: STANCE: Jo's view is {} (crouch value {:.1f}) but her body is {} by Rare's crouch test "
                    "(body flags {} / {}), for {:.1f} s now at ({:.0f} {:.0f} {:.0f}): the stealth checks count her as {}",
                    view ? "CROUCHED" : "standing", v, body ? "CROUCHED" : "standing", Load32(base, jo + 7332),
                    Load32(base, jo + 7336), now - since, LoadF(base, jo + kPosition), LoadF(base, jo + kPosition + 4),
                    LoadF(base, jo + kPosition + 8), (use_view ? (body || view) : body) ? "crouched" : "standing");
      }
      else if (!differ && was)
      {
        REXLOG_INFO("pdz_lab_stealth: STANCE: Jo's view and body agree again ({}) after {:.1f} s", body ? "crouched" : "standing",
                    now - since);
      }
    }
    was = differ;
    return use_view ? (body || view) : body;
  }

  // Round 5 (the two crash dumps of 10 / 11 Nov, both PDZRestored.exe+0xFFAB7): a character's scheduler (+0x18D4) keeps
  // a stack of at most 10 running tasks, 132 bytes each, at scheduler +0 .. +1320, with the top pointer at +1320
  // itself and the tasks waiting to be pushed in a queue at +1416 (count +1552). Rare's scheduler update sub_82171668
  // pushes every waiting task without a bounds check; in both dumps the 11th entry was being built at scheduler
  // +1320, over the top pointer. Entry: +0 task (vtable, +12 status: 0 new, 1 running, 2 / 3 over), +128 the number
  // of tasks running beside it.
  constexpr uint32_t kStackTop = 1320, kStackEntry = 132, kStackMax = 10, kWaitingCount = 1552;
  constexpr uint32_t kSearchVtable = 0x820933D0;
  int StackDepth(const uint8_t *base, uint32_t c)
  {
    const uint32_t sched = c + kScheduler;
    const uint32_t top = Load32(base, sched + kStackTop);
    if (top < sched || top > sched + kStackTop || (top - sched) % kStackEntry != 0)
    {
      return -1;
    }
    return int((top - sched) / kStackEntry);
  }
  int StackWaiting(const uint8_t *base, uint32_t c)
  {
    const uint32_t n = Load32(base, c + kScheduler + kWaitingCount);
    return n < 64 ? int(n) : -1;
  }
  // The SEARCH tasks on his stack (and, in `top_is_search`, whether the running one is a SEARCH).
  int StackSearches(const uint8_t *base, uint32_t c, bool &top_is_search)
  {
    top_is_search = false;
    const int depth = StackDepth(base, c);
    int n = 0;
    for (int i = 0; i < depth; ++i)
    {
      const uint32_t task = Load32(base, c + kScheduler + kStackEntry * uint32_t(i));
      // (An ended one, status 2 or 3, left under a DEFAULT script by Rare's own end of a search, does not count.)
      const bool search = Ptr(task) && Load32(base, task) == kSearchVtable && Load32(base, task + 12) <= 1;
      n += search ? 1 : 0;
      top_is_search = search && i == depth - 1;
    }
    return n;
  }
  // Follow-up Sun 15 Nov (item 4): Rare's COMBAT script is running on his task stack (status 1), whatever runs above
  // it (its cover task 82092950, a reaction). Rare's COMBAT script is vtable 820924F4 (every combat guard's stack in
  // the four hand-test logs of Sun 15 Nov); the vtable of the script seen at the last COMBAT start (sub_821CBF48)
  // counts too. A script that has not started yet (status 0, queued under a reaction) does not count.
  constexpr uint32_t kCombatVtable = 0x820924F4;
  bool CombatOnStack(const uint8_t *base, uint32_t c)
  {
    const int depth = StackDepth(base, c);
    for (int i = 0; i < depth; ++i)
    {
      const uint32_t task = Load32(base, c + kScheduler + kStackEntry * uint32_t(i));
      if (task < 0x10000u || task >= 0xFFFF0000u)
      {
        continue;
      }
      const uint32_t vt = Load32(base, task);
      if ((vt == kCombatVtable || (g_combat_vtable_seen && vt == g_combat_vtable_seen)) && Load32(base, task + 12) == 1)
      {
        return true;
      }
    }
    return false;
  }
  std::string StackText(const uint8_t *base, uint32_t c)
  {
    const int depth = StackDepth(base, c);
    std::string s = fmt::format("task stack {} of {}, {} waiting:", depth, kStackMax, StackWaiting(base, c));
    for (int i = 0; i < depth; ++i)
    {
      const uint32_t entry = c + kScheduler + kStackEntry * uint32_t(i);
      const uint32_t task = Load32(base, entry);
      if (!Ptr(task))
      {
        s += " [none]";
        continue;
      }
      const uint32_t vt = Load32(base, task);
      s += fmt::format(" [{}{:08X} status {} beside {}]", vt == kSearchVtable ? "SEARCH " : "", vt, Load32(base, task + 12),
                       Load32(base, entry + 128));
    }
    return s;
  }
  uint32_t g_give_refused = 0;
  float g_give_refused_log = -1e9f;
  uint32_t g_push_logged = 0;
  uint32_t g_stack_refused = 0;
  float g_stack_refused_log = -1e9f;
#include "lab_stealth_r7_Tue17Nov.inc" // (Tue 17 Nov evening: one owner per guard; the write record for the swing lines)
#include "lab_stealth_r8_A_Tue17Nov.inc" // (Tue 17 Nov night, fix2: one truth for a sighting, the sight record, the blind search)

  // Rare's SEARCH task at a pad, built and handed over as the darkness loop does (82328480-823284B8) and with the
  // arguments of the investigate handler (821D170C-821D171C: r4 pad, r5..r7 0).
  // Round 5 (the crash, reproduced by r5_1a_crash_repro; stack readouts in its log). A SEARCH task's end routine
  // (sub_821DF708, run whenever the task is ended, popped OR discarded) queues a fresh DEFAULT script for the
  // character with sub_82171210, which the scheduler then pushes on top of whatever is there. Rare only ever starts a
  // SEARCH in place of a DEFAULT script, and only ends one that is the top task: the SEARCH is popped, the DEFAULT
  // comes back, depth 1. Rounds 3 / 4 broke that in two ways:
  //  (a) handing a new SEARCH to a guard who was in one (the hand-over sub_821712D8 swaps the top task and discards
  //      the old one): the old SEARCH's end queued a DEFAULT that landed ON TOP of the new SEARCH, burying it;
  //  (b) cutting a SEARCH that was not the top task (under the alarm Rare runs a soft-alarm guard's search beside
  //      his DEFAULT script, a new one every frame): every cut queued one more DEFAULT on the stack. Ten: the crash.
  // So: a SEARCH is handed over only to a guard with NO search of any kind (Rare's searching bit clear, no SEARCH on
  // his stack, nothing waiting, a shallow stack); the callers wait and try again. And a SEARCH is cut only when it is
  // the top task of his stack (the SEARCH update hook). False = refused.
  constexpr int kGiveDepth = 5;
  bool GiveSearch(PPCContext &ctx, uint8_t *base, uint32_t c, uint32_t pad)
  {
    if (R7Refuse(base, c, "hand him a SEARCH")) // (Tue 17 Nov: he is Rare's COMBAT's)
    {
      return false;
    }
    if (REXCVAR_GET(pdz_lab_stealth_stack_guard))
    {
      bool top_is_search = false;
      const int depth = StackDepth(base, c), waiting = StackWaiting(base, c);
      const int searches = StackSearches(base, c, top_is_search);
      const char *why = depth < 0 || waiting < 0 ? "his task stack cannot be read"
                        : waiting > 0            ? "a task is waiting to be pushed"
                        : depth >= kGiveDepth    ? "his task stack is deep"
                        : searches > 0           ? "a running SEARCH is on his task stack (it must end first)"
                        : (Load32(base, c + kConfig) & kSearching) ? "he is in a search of Rare's (it must end first)"
                                                                   : nullptr;
      if (why)
      {
        ++g_give_refused;
        const float now = Clock(base);
        if (Logging() && (now - g_give_refused_log > 1.0f || now < g_give_refused_log))
        {
          g_give_refused_log = now;
          REXLOG_INFO("pdz_lab_stealth: {} no SEARCH handed over now ({}; {} refusals so far): {}", Id(base, c), why,
                      g_give_refused, StackText(base, c));
        }
        return false;
      }
    }
    uint32_t memory = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 0xA8;
      frame.ctx.r4.u64 = 10;
      sub_8239D740(frame.ctx, base);
      memory = frame.ctx.r3.u32;
    }
    if (!Ptr(memory))
    {
      return false;
    }
    uint32_t task = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = memory;
      frame.ctx.r4.u64 = Ptr(pad) ? pad : 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      sub_821DF550(frame.ctx, base);
      task = frame.ctx.r3.u32;
    }
    if (!Ptr(task))
    {
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c + kScheduler;
    frame.ctx.r4.u64 = task;
    sub_821712D8(frame.ctx, base);
    R7Wrote(base, c, "a task handed over in place of his top task"); // (Tue 17 Nov: the write record)
    return true;
  }

  void SampleJo(const uint8_t *base, uint32_t jo, float now)
  {
    float p[3];
    for (int i = 0; i < 3; ++i)
    {
      p[i] = LoadF(base, jo + kPosition + 4 * i);
    }
    if (g_jo_t < 0.0f || now < g_jo_t)
    {
      std::memcpy(g_jo_prev, p, sizeof(p));
      g_jo_t = now;
      g_jo_speed = 0.0f;
      return;
    }
    const float dt = now - g_jo_t;
    if (dt < 0.25f)
    {
      return;
    }
    const float dx = p[0] - g_jo_prev[0], dz = p[2] - g_jo_prev[2];
    g_jo_speed = dt < 2.0f ? std::sqrt(dx * dx + dz * dz) / dt : 0.0f;
    std::memcpy(g_jo_prev, p, sizeof(p));
    g_jo_t = now;
  }

  // The once-per-pass exposure of Jo to this viewer (see the header).
  float Exposure(PPCContext &ctx, uint8_t *base, uint32_t viewer, uint32_t jo, const float eye[3], const float dir[3],
                 const float at[3], float range2, bool &crouched)
  {
    const float dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float near_cm = std::max(1.0f, float(REXCVAR_GET(pdz_lab_stealth_near_cm)));
    const float f_dist = d <= near_cm ? 1.0f : near_cm / d;
    const float f_edge = range2 > 1.0f ? std::clamp(1.0f - d * d / range2, 0.15f, 1.0f) : 1.0f;
    const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float cosv = 1.0f;
    if (dl > 1e-4f && d > 1e-3f)
    {
      cosv = (dx * dir[0] + dy * dir[1] + dz * dir[2]) / (d * dl);
    }
    const float c = std::max(0.0f, cosv);
    const float f_centre = 0.35f + 0.65f * c * c;
    const float f_move = 0.5f + 0.5f * std::min(1.0f, g_jo_speed / kStillSpeed);
    const bool armed = ArmedItem(base, Load32(base, jo + kHand0)) || ArmedItem(base, Load32(base, jo + kHand1));
    const float f_armed = armed ? 1.0f : 0.6f;
    // Dark patches (lab_dark_patches.cpp): the light where Jo stands (0.3 dark .. 1 lit) instead of the blackout's dark bit.
    // (Nightclub helper: also a zone row's light term, LightTerm.)
    const float f_dark = LightTerm() && !lab_dark::RareBlackout() ? lab_dark::MeterFactor()
                         : (Load32(base, viewer + kAiFlags) & kDarkAim) ? float(REXCVAR_GET(pdz_lab_stealth_dark))
                                                                        : 1.0f;
    crouched = Crouched(ctx, base, jo);
    const float f_crouch = crouched ? 0.5f : 1.0f;
    return f_dist * f_edge * f_centre * f_move * f_armed * f_dark * f_crouch;
  }

  void StartInvestigation(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, Inv kind, const float centre[3], int body,
                          float now, const std::string &why, uint32_t centre_pad);

  // Round 11b (item 4; log 003 18:30:53: s3g7 glimpsed Jo and said HEAR_UNKNOWN_SOLO, Rare's squad speech arbiter
  // held it for 2 s and dropped it, so nothing warned her). A WARNING line of ours (a guard's "huh?" at a glimpse, at
  // her steps or at a sound; a lamp reaction) must be heard. It waits for the gap (story speech, another speaker's
  // line, his own speech, a body line that is due), is then raised with Rare's "skip the speaker's gap" argument and
  // marked so that the squad's speech arbiter does not hold it (as round 10b's alert lines), skips our own line
  // rules (the repeat rule, the fight's cap), and is asked for again until DarkAudioSound::Play has really started it
  // or pdz_lab_stealth_warn_wait_s have passed.
  bool PrioWaiting(float now);
  // (Follow-up Sun 15 Nov, item 1: a body line that is due holds back only its OWN speaker's other lines.)
  bool PrioHolds(const Guard &g, float now) { return OneVoice() ? PrioWaiting(now) : (g.prio && now < g.prio_until); }
  // Round 11g (item 1): a warning line of ours is due (not asked of Rare's speech yet) from a speaker within
  // pdz_lab_stealth_voice_near_cm of `c` (not `c` himself).
  bool WarnDueNear(const uint8_t *base, uint32_t c, float now)
  {
    const float near_cm = float(REXCVAR_GET(pdz_lab_stealth_voice_near_cm));
    for (const auto &[other, o] : g_guards)
    {
      if (other != c && o.warn_ev && o.warn_queued < -1e8f && !o.dead && o.st != St::Dead && now >= o.warn_from &&
          now - o.warn_from < 10.0f && (near_cm <= 0.0f || Dist(base, c, other) <= near_cm))
      {
        return true;
      }
    }
    return false;
  }
  // Round 11g (item 1): the ordinary lines still WAITING to start (asked for, no sound yet) of `me` and of speakers
  // near him are dropped for his warning line: a pending request is cleared, a waiting speech task is given Rare's
  // own give-up (it ends without a sound). A line that has started is never touched; alert and warning lines are
  // never dropped.
  // Round 11h (item 2; log 015 21:16:44.3: s1m5's alarm shout was due 1 ms after Rare had asked for his near-miss
  // line BEEN_SHOT_MISS, an "alert" line, which had not started yet; it then started and played for 2 s, and the
  // shout waited for it): the SPEAKER'S OWN waiting lines are dropped whatever they are, his shot / hurt lines
  // included; only for his alarm shout (`own_all`) also a waiting sighting line. Other speakers' alert lines are
  // never dropped.
  void DropWaitingLines(uint8_t *base, uint32_t me, float now, float near_cm, const std::string &for_line, bool own_all)
  {
    bool any = false;
    for (auto it = g_lines.begin(); it != g_lines.end();)
    {
      const Line l = *it;
      const bool keep_alert = l.alert && !(l.c == me && (own_all || !StrongAlertEvent(base, l.ev)));
      if (l.played || keep_alert || l.warn || RadioEvent(l.ev) || // (Mansion: Rare's radio line is never dropped)
          !Ptr(l.c) || now < l.start || now - l.start >= 2.5f ||
          !(l.c == me || near_cm <= 0.0f || Dist(base, me, l.c) <= near_cm))
      {
        ++it;
        continue;
      }
      const uint32_t voice = l.c + kVoice;
      const uint32_t pending = Load32(base, voice + 216), task = Load32(base, voice + 64);
      bool dropped = false;
      if (pending && (pending == l.ev || pending == l.ev + 1))
      {
        Store32(base, voice + 216, 0);
        dropped = true;
      }
      else if (!pending && Ptr(task) && !(base[task + 88] & 1) &&
               (Load32(base, task + 52) == l.ev || Load32(base, task + 52) == l.ev + 1))
      {
        const float gone = -100.0f; // past Rare's give-up time: the task ends without a sound
        uint32_t bits;
        std::memcpy(&bits, &gone, 4);
        Store32(base, task + 68, bits);
        dropped = true;
      }
      if (!dropped)
      {
        ++it;
        continue;
      }
      for (auto sd = g_said_lines.begin(); sd != g_said_lines.end();)
      {
        sd = (sd->c == l.c && sd->ev == l.ev) ? g_said_lines.erase(sd) : std::next(sd);
      }
      if (g_mutter_wait == l.c)
      {
        g_mutter_wait = 0;
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_voice_drop: {} {} ({}) was waiting to start: DROPPED (not delayed) for the warning line {} of {}",
                    Id(base, l.c), EventName(base, l.ev), l.ev, for_line, Id(base, me));
      }
      it = g_lines.erase(it);
      any = true;
    }
    if (any)
    {
      float end = -1e9f;
      for (const Line &l : g_lines)
      {
        end = std::max(end, l.end);
      }
      g_last_line_end = end;
    }
  }
  void WarnTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (!g.warn_ev)
    {
      return;
    }
    const uint32_t ev = g.warn_ev;
    if (g.st == St::Dead || g.dead || !(LoadF(base, c + kHealth) > 0.0f) || now < g.warn_from)
    {
      g.warn_ev = 0;
      return;
    }
    // Round 11c (item 1): a scientist's line is Rare's LIGHT_SHOT_OUT event (which his speech plays), its cue name
    // swapped to the cue wanted (HEAR_UNKNOWN_SOLO, WARN_TEAM, a lights-out line...) in his own speech task.
    const bool staff = g.scientist && !g.sci_cue.empty();
    const std::string line = staff ? g.sci_bank + "_" + g.sci_cue : EventName(base, ev);
    const auto settled = [&](bool played) {
      if (g.run_pending && g.run_at < -1e8f)
      {
        // Round 11f (item 4): his alarm shout has started (or could not play): the run follows.
        const float hes = std::max(0.0f, float(REXCVAR_GET(pdz_lab_stealth_alarm_announce_s)));
        g.run_at = played ? now + hes : now;
        if (played)
        {
          g.announced_at = now;
        }
        // Round 11g (item 4): a real chance to stop him: no alarm before pdz_lab_stealth_alarm_min_s from here.
        const float min_s = float(REXCVAR_GET(pdz_lab_stealth_alarm_min_s));
        if (min_s > 0.0f)
        {
          g_alarm_not_before = now + min_s;
          g_alarm_runner = c;
        }
        // Round 11h (item 1): from here the run is a commitment (RunKeepTick).
        g.run_committed = !RarePlus(); // (rareplus: after the shout and the hesitation the run is Rare's as normal)
        g.run_commit_at = now;
        g.run_task = 0;
        g.run_gives = g.run_fails = 0;
        g.run_give_at = -1e9f;
        REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN ANNOUNCED: his shout {} ({}) {}, {:.1f} s after the run was asked for: {}",
                    Id(base, c), line, g.run_why,
                    played ? "REALLY STARTED" : "could NOT play (other speech held it; see the NOT PLAYED line)", now - g.run_from,
                    played ? fmt::format("he hesitates {:.1f} s, then runs", hes) : std::string("he runs now, unannounced"));
      }
      else if (g.scientist && g.sci_sighting && played)
      {
        g.announced_at = now; // (a scientist's sighting line is his alarm shout)
      }
      if (!g.scientist)
      {
        // Round 11d (item 1): his meter stays held for pdz_lab_stealth_warn_hold_gap_s after the line started.
        g.warn_free_at = played ? now + std::max(0.0f, float(REXCVAR_GET(pdz_lab_stealth_warn_hold_gap_s))) : -1e9f;
      }
      if (g.scientist)
      {
        g.sci_line_at = now; // (item 2: his next warning waits pdz_lab_scientist_warn_gap_s from here)
        if (g.sci_sighting)
        {
          g.sci_hold_until = std::min(g.sci_hold_until, now + std::max(0.0f, float(REXCVAR_GET(pdz_lab_scientist_hesitate_s))));
          g.sci_sighting = false;
        }
      }
      g.warn_ev = 0;
    };
    const auto drop = [&](const char *why) {
      // (Always logged: a noticed player must hear it, so a line that could not play is worth a line in any log.)
      REXLOG_INFO("pdz_lab_stealth: {} warning line {} ({}) NOT PLAYED: {} ({:.1f} s after it was due, asked for {} time(s))",
                  Id(base, c), line, g.warn_what, why, now - g.warn_from, g.warn_tries);
      settled(false);
    };
    if (const char *over = g.run_pending ? nullptr : WarnOver(g, now, g.warn_what.c_str()))
    {
      drop(over); // (follow-up Sun 15 Nov, items 9 and 16: also after his sighting line, and once he was hurt by Jo)
      return;
    }
    if (g.warn_queued > -1e8f)
    {
      const bool traced = std::any_of(g_said_lines.begin(), g_said_lines.end(), [&](const Said &s) { return s.c == c && s.ev == ev; });
      if (traced)
      {
        return; // asked for: Rare's speech has it (followed to its real start)
      }
      const bool played = std::any_of(g_lines.begin(), g_lines.end(), [&](const Line &l) {
        return l.c == c && l.played && l.start >= g.warn_queued - 0.05f;
      });
      if (played)
      {
        REXLOG_INFO("pdz_lab_stealth: {} WARNING LINE PLAYED: {} ({}), {:.1f} s after it was due (asked for {} time(s))",
                    Id(base, c), line, g.warn_what, now - g.warn_from, g.warn_tries);
        settled(true);
        return;
      }
      g.warn_queued = -1e9f; // Rare's speech lost it after all: asked for again
    }
    // Round 11g (item 1; log 013 20:52:10-14: s1m1's alarm shout waited 4.3 s behind another guard's lines): a
    // warning line goes to the FRONT of the queue. It plays alongside story speech. Ordinary lines still waiting to
    // start (his own and those of speakers within pdz_lab_stealth_voice_near_cm) are dropped; it waits only for a
    // line that is already PLAYING within that distance (never cut), and for his own speech to be free.
    if (now >= g.warn_until)
    {
      drop("other speech or Rare's speech rules held it");
      return;
    }
    if (now - g.warn_try < 0.3f || g.hold_ev || PrioHolds(g, now))
    {
      return;
    }
    // (Follow-up Sun 15 Nov, item 1: with the one-voice rule scrapped, only HIS OWN lines are dropped or waited for:
    // a "near" distance that only he himself is within.)
    const float near_cm = OneVoice() ? float(REXCVAR_GET(pdz_lab_stealth_voice_near_cm)) : 0.01f;
    DropWaitingLines(base, c, now, near_cm, line, g.run_pending);
    if (std::any_of(g_lines.begin(), g_lines.end(), [&](const Line &l) {
          return l.played && l.end > now && Ptr(l.c) && (l.c == c || near_cm <= 0.0f || Dist(base, c, l.c) <= near_cm);
        }))
    {
      return; // a line is playing near him: it plays to its end, then this one
    }
    if (REXCVAR_GET(pdz_lab_stealth_voice_set_wait))
    {
      // Round 11k (item 3): a line of his voice set is playing somewhere: it plays to its end first.
      const char *set = staff ? (g.sci_bank == "SCI1" ? "SCI1" : "SCI2") : VoiceSetName(g.voice);
      if (set && SetBusy(base, set, c, now))
      {
        return;
      }
    }
    if (Load32(base, c + kVoice + 216) || Ptr(Load32(base, c + kVoice + 64)))
    {
      return; // his own speech is busy (a line of his that is playing, or one that is just being dropped)
    }
    g.warn_try = now;
    ++g.warn_tries;
    bool swapped = false;
    if (staff)
    {
      const std::string own = std::string(g.voice == 15 ? "SCI1" : "SCI2") + "_LIGHT_SHOT_OUT";
      if (line != own)
      {
        ArmSwapName(base, c, g.voice, line, "LIGHT_SHOT_OUT", now);
        swapped = true;
      }
      // Round 11j (item 4): never the take he said last time of this cue, when another of its takes is allowed.
      uint32_t takes = g.sci_mask;
      if (const auto last = g.sci_last.find(line); last != g.sci_last.end() && (takes & ~(1u << last->second)))
      {
        takes &= ~(1u << last->second);
      }
      lab_stealth::OrderTake(base, line, takes);
    }
    else if (g.warn_swap && VoiceSetName(g.voice))
    {
      // Round 11k (item 2): the cue Rare's speech builds for this event plays as the cue chosen for what he noticed.
      ArmSwapName(base, c, g.voice, std::string(VoiceSetName(g.voice)) + "_" + g.warn_swap, "HEAR_UNKNOWN_SOLO", now,
                  "HEAR_UNKNOWN_TEAM");
      swapped = true;
      g.alt_cue = g.warn_swap;
      g.alt_takes = g.warn_swap_takes;
      g.alt_at = now;
    }
    else
    {
      if (g.warn_takes)
      {
        g.mask_ev = ev; // (only these takes of the cue)
        g.mask_takes = g.warn_takes;
        g.mask_at = now;
      }
      if (g.warn_alt_cue)
      {
        g.alt_cue = g.warn_alt_cue; // (round 11f: ... and of the cue Rare may play instead, at its Prime)
        g.alt_takes = g.warn_alt_takes;
        g.alt_at = now;
      }
    }
    g_warn_saying = true;
    g_say_force = true;
    const bool ok = Say(ctx, base, c, ev, false, g.warn_what.c_str());
    g_say_force = false;
    g_warn_saying = false;
    if (ok)
    {
      g.warn_queued = now;
    }
    else if (swapped)
    {
      DisarmSwap();
    }
  }
  // A trooper's warning line is put in line (raised by WarnTick, at once when the gap is there).
  void QueueWarn(Guard &g, uint32_t ev, float now, const char *what, uint32_t takes = 0, const char *alt_cue = nullptr,
                 uint32_t alt_takes = 0)
  {
    g.warn_ev = ev;
    g.warn_from = now;
    g.warn_until = now + std::max(1.0f, float(REXCVAR_GET(pdz_lab_stealth_warn_wait_s)));
    g.warn_try = g.warn_queued = -1e9f;
    g.warn_tries = 0;
    g.warn_what = what;
    g.warn_takes = takes;
    g.warn_alt_cue = alt_cue;
    g.warn_alt_takes = alt_takes;
    g.warn_swap = g_next_swap; // (round 11k: a glimpse warning plays as a "saw something" cue)
    g.warn_swap_takes = g_next_swap_takes;
    g_next_swap = nullptr;
    g.sci_cue.clear();
  }
  void Warn(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t ev, float now, const char *what, uint32_t takes = 0,
            const char *alt_cue = nullptr, uint32_t alt_takes = 0)
  {
    if (!ev || g.st == St::Dead || g.dead)
    {
      return;
    }
    if (what && std::strncmp(what, "heard", 5) == 0)
    {
      g.heard_at = now; // round 11l: a real sound (her steps, a sound, gunfire)
    }
    if (const char *over = WarnOver(g, now, what))
    {
      // Follow-up Sun 15 Nov (items 9 and 16): no warning line once he has seen her, fights, or was hurt by Jo.
      if (Logging() && (now - g.warn_over_log > 5.0f || now < g.warn_over_log))
      {
        g.warn_over_log = now;
        REXLOG_INFO("pdz_lab_stealth: {} warning line ({}) not asked for: {}", Id(base, c), what ? what : "", over);
      }
      return;
    }
    if (!REXCVAR_GET(pdz_lab_stealth_warn_lines) || g.scientist || !LayerVoice(g.voice))
    {
      Say(ctx, base, c, ev, false, what); // (round 11: one request)
      return;
    }
    if (g.warn_ev && (g.warn_queued > -1e8f || g.run_pending))
    {
      return; // a warning line of his is on its way already (or his alarm shout is due)
    }
    QueueWarn(g, ev, now, what, takes, alt_cue, alt_takes);
    WarnTick(ctx, base, c, g, now);
  }
  // Round 11f (item 1; log 011 20:30:03: s3g2 glimpsed Jo and said "Heard something!"): a guard's line at a GLIMPSE
  // (sight) is a take that fits seeing something. The 9 Nov transcriptions, the same pattern in all three voices:
  //   HEAR_UNKNOWN_SOLO (7 takes): 1 "Anybody there?" 4 "Huh? Weird." / "Strange." / "That's odd." 5 "Whoa! What was
  //     that?" 6 "Is that you?" fit; 2 "Must be hearing things." 3 "What's that noise?" 7 "I'm a little jumpy." /
  //     "Every little sound." do not.
  //   HEAR_UNKNOWN_TEAM (8 takes; Rare plays it for a guard with squad mates): 4 "Shit! What was that?" 6 "Wonder what
  //     that was?" 7 "What's that? Scared the shit out of me!" fit; 1 "Thought I heard a noise." 2 "That does not
  //     sound good." 3 "Heard something!" 5 "Where'd that come from?" 8 "You guys hear that?" do not.
  // A real sound (a lamp heard, a silenced shot) keeps every take.
  // Round 11k (item 2; log 021 08:59:34-09:00:50: the glimpse takes above are still HEAR_UNKNOWN lines): HEAR_UNKNOWN
  // only when he heard something. A glimpse: he saw movement and has not identified her. The 9 Nov transcriptions,
  // PLAYER_CLOAKED_NEXT_SOLO (7 takes, the same script in all three trooper sets): 1 "Must be seeing things." 2
  // "Thought I saw something!" / "I could've sworn I saw something!" 3 "Don't know what it is, but something's moving."
  // 4 "Hey! Ah, just a trick of the light." 6 "There's someone here, I can feel it." / "I know someone's here, I know
  // it!" fit; 5 "I can't shoot when I can't see." 7 "I can smell ya!" do not. The line is still asked for as Rare's
  // HEAR_UNKNOWN event (every rule of that event holds); the cue his speech task builds for it (HEAR_UNKNOWN_SOLO, or
  // _TEAM with squad mates) is swapped for that cue, and the take is chosen at its Prime.
  void WarnGlimpse(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const char *what)
  {
    if (REXCVAR_GET(pdz_lab_stealth_noticed_takes) && !g.scientist && ((g.voice >= 17 && g.voice <= 19) || Gangster(g.voice)))
    {
      // (Nightclub helper: GANGSTA1-3 have 5 takes; 1 "You still there?" 3 "Where are you?" fit a first notice; the
      // trooper mask would pick take 4 "Gonna find you!", too strong. Survey n, section 5.)
      g_next_swap = "PLAYER_CLOAKED_NEXT_SOLO";
      g_next_swap_takes = Gangster(g.voice) ? 0x05u : 0x2Fu;
      Warn(ctx, base, c, g, kEvHearUnknown, now, what);
      g_next_swap = nullptr;
    }
    else if (REXCVAR_GET(pdz_lab_stealth_glimpse_takes) && g.voice >= 15 && g.voice <= 19) // (take masks of SCI / TROOPER sets)
    {
      Warn(ctx, base, c, g, kEvHearUnknown, now, what, 0x39, "HEAR_UNKNOWN_TEAM", 0x68);
    }
    else
    {
      Warn(ctx, base, c, g, kEvHearUnknown, now, what);
    }
  }

  bool PadPos(const uint8_t *base, uint32_t pad, float out[3]);
  bool CanReachOnFloor(const uint8_t *base, uint32_t c, const float pos[3], float max_walk, float &out_walk);
  void Alert(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const std::string &why);
  // A glimpse becomes Rare's "heard something" investigation at Jo's pad (see the header, point 1).
  void TryInvestigate(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now)
  {
    if (Load32(base, c + kConfig) & (kSearching | kNeverInvestigates))
    {
      return; // already in a SEARCH of Rare's, or Rare's "never investigates" (8217CC6C)
    }
    g.squad = SquadOf(ctx, base, c);
    if (g.squad)
    {
      for (const auto &[other, o] : g_guards)
      {
        if (other != c && o.squad == g.squad &&
            (o.st == St::Suspicious || o.st == St::Searching || o.st == St::LostSearch ||
             (o.st == St::Investigating && o.inv != Inv::Squad)))
        {
          if (Logging() && now - g.last_busy_log > 5.0f)
          {
            g.last_busy_log = now;
            REXLOG_INFO("pdz_lab_stealth: {} glimpsed Jo (meter {:.0f}) but squad mate {} already investigates", Id(base, c),
                        g.meter, Id(base, other));
          }
          return;
        }
      }
    }
    // Log 2 (stealth_a): the stimulus route sent s1m1 to the alarm. Rare's DEFAULT reaction runs the role dispatch
    // before any stimulus handler (821D07E4-821D0A28: RADIO_USER radios in, ALARM_USER runs for the alarm via
    // 821DF340 / 821DF4A0), so a glimpse given as "heard something" escalates for the 93 alarm users. So the glimpse
    // becomes Rare's SEARCH task at Jo's pad directly (the task the hear handler builds with a pad, 821D170C-821D171C:
    // it walks to the pad, plays 107 at its start and searches), handed over as the darkness loop does, plus Rare's
    // HEAR_UNKNOWN_SOLO from us. The stimulus route is kept only as a fallback if the task can't be built.
    const uint32_t pad = Load32(base, jo + kPad);
    if (const float reach_cm = float(REXCVAR_GET(pdz_lab_stealth_glimpse_reach_cm)); reach_cm > 0.0f)
    {
      // Round 11j (item 1; log 019 21:58:41.7: s1m2 glimpsed Jo about 30 m away on another level, got Rare's SEARCH at
      // her pad and paced one corridor for over a minute): the search only heads for a spot he can walk to (his own
      // floor, an open pad route of at most pdz_lab_stealth_glimpse_reach_cm). Otherwise he says his line, looks
      // round where he stands and is wary.
      float pp[3] = {}, walk = 0.0f;
      if (!PadPos(base, pad, pp) || !CanReachOnFloor(base, c, pp, reach_cm, walk))
      {
        if (now - g.reach_refused_at >= 10.0f || now < g.reach_refused_at)
        {
          g.reach_refused_at = now;
          REXLOG_INFO("pdz_lab_stealth: {} glimpsed Jo (meter {:.0f}, {:.0f} cm away) but cannot walk to her pad {:08X} at "
                      "({:.0f} {:.0f} {:.0f}) (another floor, or no open pad route within {:.0f} cm): NO SEARCH THERE: he "
                      "looks round where he stands, wary",
                      Id(base, c), g.meter, Dist(base, c, jo), pad, pp[0], pp[1], pp[2], reach_cm);
          WarnGlimpse(ctx, base, c, g, now, "glimpse (sight takes)");
          Alert(ctx, base, c, g, now, "glimpsed her where he cannot walk to");
          if (g.st == St::Unaware)
          {
            Set(base, c, g, St::Wary, now, "glimpsed her where he cannot walk to: he looks round where he stands, wary");
          }
        }
        return;
      }
    }
    if (now >= g.steps_at && now - g.steps_at < 0.3f && REXCVAR_GET(pdz_lab_stealth_investigation))
    {
      // Round 11 (item 5): her footsteps are a noise like any other: he walks to where he heard them, looks round
      // there for 10-15 s, checks at most one spot near it and goes back to his post, wary (round 9 / 10: one search
      // of Rare's at her pad, which wandered for up to 180 s).
      StartInvestigation(ctx, base, c, g, Inv::Noise, g.glimpse_pos, -1, now,
                         fmt::format("heard her footsteps at ({:.0f} {:.0f} {:.0f}): he goes to look alone", g.glimpse_pos[0],
                                     g.glimpse_pos[1], g.glimpse_pos[2]),
                         0);
      if (g.st == St::Investigating)
      {
        g.expect_until = now + 60.0f; // (round 10: he is looking for her: he knows her faster when he sees her)
        Warn(ctx, base, c, g, kEvHearUnknown, now, "heard her steps");
      }
      return;
    }
    g.was_wary = g.st == St::Wary;
    g.need_line = false;
    const bool direct = GiveSearch(ctx, base, c, pad);
    g.search_pad = Ptr(pad) ? pad : 0;
    if (!direct)
    {
      const uint32_t profile = ProfileOf(base, c);
      if (profile && Ptr(pad))
      {
        Store32(base, profile + kHeardPad, pad);
      }
      Store32(base, c + kStimuli, Load32(base, c + kStimuli) | kStimHeard);
      g.need_line = true;
    }
    const bool steps = now >= g.steps_at && now - g.steps_at < 0.3f; // round 9: he heard her steps (no glimpse)
    g.expect_until = now + 60.0f; // round 10: he goes to look for HER: he will know what he sees
    g.glimpse_search_at = now;    // round 11j (item 3): his glimpse search has a time limit
    Set(base, c, g, St::Suspicious, now,
        fmt::format("{} at ({:.0f} {:.0f} {:.0f}), pad {:08X}: {}", steps ? "her footsteps" : "glimpse", g.glimpse_pos[0],
                    g.glimpse_pos[1], g.glimpse_pos[2], pad, direct ? "SEARCH at her pad" : "Rare's hear stimulus (fallback)"));
    if (direct)
    {
      if (steps)
      {
        Warn(ctx, base, c, g, kEvHearUnknown, now, "heard her steps");
      }
      else
      {
        WarnGlimpse(ctx, base, c, g, now, "glimpse (sight takes)");
      }
    }
  }

  // Rare's own "no target": his current and fallback target holders (+0x3168 / +0x3150, read by the COMBAT update
  // 821CC370-821CC39C, which ends the script when both are empty) set to 0 through the holder setter, as Rare clears a
  // holder (821CC40C-821CC414). Only a fresh sighting (82580A78) gives him a target again.
  void ClearTarget(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    if (R7Refuse(base, c, "empty his target holders")) // (Tue 17 Nov: he is Rare's COMBAT's; not at a lose contact)
    {
      return;
    }
    R7Wrote(base, c, "his target holders emptied");
    for (const uint32_t holder : {kTargetHolder, kFallbackHolder})
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c + holder;
      frame.ctx.r4.u64 = 0;
      sub_821433D8(frame.ctx, base);
    }
  }

  bool AlarmCombat(const uint8_t *base, uint32_t c)
  {
    return AlarmOn(base) && !(Load32(base, c + kConfig) & kSoftAlarm); // Rare's rule: the alarm means combat
  }

  // Lose contact (header point 3). `ended`: Rare already ended his COMBAT script, so no leave-combat request.
  void LoseContact(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, float unseen, bool ended)
  {
    const uint32_t squad = SquadOf(ctx, base, c);
    g.squad = squad;
    // Round 3 (r3_c_alarm: under the alarm Rare's alert reaction restarted every lost guard's COMBAT within 0.1 s, no
    // sighting): a guard who loses contact while the alarm sounds gets Rare's own soft-alarm flag (+6272 0x1000, which
    // event 12 of the Laboratory script sets for the placed guards once the tracking system is destroyed, Secret Agent
    // and above), so for him the alarm no longer means combat
    // and he can hunt. Only with pdz_lab_stealth_alarm_lose_s.
    if (AlarmCombat(base, c) && REXCVAR_GET(pdz_lab_stealth_alarm_lose_s) > 0.0)
    {
      Store32(base, c + kConfig, Load32(base, c + kConfig) | kSoftAlarm);
      g.soft_by_us = true; // round 4: cleared again when the alarm ends
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} loses contact while the alarm sounds: Rare's soft-alarm flag set for him", Id(base, c));
      }
    }
    // Her last pad: his own (+0x1830, written when he last saw his target), else the squad's shared one (before it is
    // expired below), else a squad mate's from his own lose contact. None: no SEARCH (the Lost state goes wary).
    g.lost_pad = Load32(base, c + kTargetPad);
    if (!Ptr(g.lost_pad) && squad)
    {
      g.lost_pad = Load32(base, squad + kSquadPad);
    }
    if (!Ptr(g.lost_pad) && squad)
    {
      for (const auto &[other, o] : g_guards)
      {
        if (other != c && o.squad == squad && Ptr(o.lost_pad))
        {
          g.lost_pad = o.lost_pad;
          break;
        }
      }
    }
    if (!Ptr(g.lost_pad))
    {
      g.lost_pad = 0;
    }
    {
      const R7Leaving leaving; // (Tue 17 Nov: the layer's deliberate end of his fight: not held back)
      ClearTarget(ctx, base, c); // log 6 (c2): with his own target still Jo, Rare restarted COMBAT without a sighting
    }
    if (squad)
    {
      Store32(base, squad + kSquadPad, 0); // Rare's "no pad" (8217C5C0: zone -1)
      Store16(base, squad + kSquadZone, 0xFFFF);
    }
    if (!ended)
    {
      Store32(base, c + kSight, Load32(base, c + kSight) | kLeaveCombat);
    }
    g.spotted = false;
    bool say = true;
    if (squad)
    {
      const auto it = g_squad_hides.find(squad);
      say = it == g_squad_hides.end() || now - it->second >= 10.0f;
    }
    bool said = false;
    if (say)
    {
      said = Say(ctx, base, c, kEvHides, false, "lost contact");
      if (said && squad)
      {
        g_squad_hides[squad] = now;
      }
    }
    Set(base, c, g, St::Lost, now,
        fmt::format("no sighting for {:.1f} s: {}, squad target expired{}", unseen,
                    ended ? "Rare ended his combat" : "Rare's leave-combat request",
                    !say ? ", a squad mate already said PLAYER_HIDES" : said ? "" : ", PLAYER_HIDES refused by the voice budget"));
  }

  // --- Hunting (header point 8) ---

  bool PadPos(const uint8_t *base, uint32_t pad, float out[3])
  {
    if (!Ptr(pad))
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      out[i] = LoadF(base, pad + 16 + 4 * i);
      if (!std::isfinite(out[i]) || std::fabs(out[i]) > 1e6f)
      {
        return false;
      }
    }
    return true;
  }

  uint32_t HuntKey(const Guard &g, uint32_t c)
  {
    return g.squad ? g.squad : c;
  }

  // The squad's sweep plan around `centre`: the centre pad, then the farthest pad per 45-degree sector among the
  // pads reached over open links (the routes out), in angle order, so consecutive legs are neighbouring pads. Layout
  // check: the centre pad's position (+16) must be readable and within 250 m of the hunter, else the plan is the
  // centre pad alone.
  void PlanSweep(const uint8_t *base, Hunt &h, uint32_t key, uint32_t centre, uint32_t hunter, const std::string &why)
  {
    h.centre = centre;
    h.pads.assign(1, centre);
    h.next = 0;
    float cp[3];
    bool layout = PadPos(base, centre, cp);
    if (layout)
    {
      const float dx = cp[0] - LoadF(base, hunter + kPosition), dy = cp[1] - LoadF(base, hunter + kPosition + 4),
                  dz = cp[2] - LoadF(base, hunter + kPosition + 8);
      layout = dx * dx + dy * dy + dz * dz < 25000.0f * 25000.0f; // the whole Laboratory (under the alarm a far squad hunts too)
    }
    if (!layout)
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: sweep plan (squad {:08X}, {}): centre pad {:08X}: pad layout check failed (position "
                    "unreadable or over 250 m from {}): the centre pad only",
                    key, why, centre, Id(base, hunter));
      }
      return;
    }
    const int hops = std::clamp(int(REXCVAR_GET(pdz_lab_stealth_hunt_hops)), 1, 6);
    const float radius = float(REXCVAR_GET(pdz_lab_stealth_hunt_radius_cm));
    std::vector<std::pair<uint32_t, int>> seen{{centre, 0}};
    for (size_t i = 0; i < seen.size() && seen.size() < 128; ++i)
    {
      if (seen[i].second >= hops)
      {
        continue;
      }
      uint32_t link = Load32(base, seen[i].first + 8);
      for (int n = 0; Ptr(link) && n < 32; ++n, link = Load32(base, link + 4))
      {
        const uint32_t to = Load32(base, link);
        if (base[link + 14] != 0 || base[link + 15] == 0 || !Ptr(to))
        {
          continue; // closed by an obstacle box (link +14), or a line-of-sight-only link (+15: Rare's pathfinder skips it)
        }
        if (std::none_of(seen.begin(), seen.end(), [to](const std::pair<uint32_t, int> &s) { return s.first == to; }))
        {
          seen.push_back({to, seen[i].second + 1});
        }
      }
    }
    uint32_t best[8] = {}, mid[8] = {};
    float best_d[8] = {}, mid_d[8] = {};
    for (const auto &[pad, hop] : seen)
    {
      float p[3];
      if (hop == 0 || !PadPos(base, pad, p))
      {
        continue;
      }
      const float dx = p[0] - cp[0], dy = p[1] - cp[1], dz = p[2] - cp[2];
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d < 250.0f || d > radius)
      {
        continue;
      }
      const float pi = 3.14159265f;
      const int s = std::clamp(int((std::atan2(dz, dx) + pi) / (2.0f * pi) * 8.0f), 0, 7);
      if (!best[s] || d > best_d[s])
      {
        best[s] = pad;
        best_d[s] = d;
      }
      if (d >= 400.0f && (!mid[s] || d < mid_d[s]))
      {
        mid[s] = pad; // round 3: a nearer pad in the same direction, so five hunters find five spread pads
        mid_d[s] = d;
      }
    }
    std::string list;
    for (int s = 0; s < 8; ++s)
    {
      if (mid[s] && mid[s] != best[s] && best_d[s] - mid_d[s] >= 300.0f)
      {
        h.pads.push_back(mid[s]);
        list += fmt::format(" {:08X} ({:.0f} cm, near)", mid[s], mid_d[s]);
      }
      if (best[s])
      {
        h.pads.push_back(best[s]);
        list += fmt::format(" {:08X} ({:.0f} cm)", best[s], best_d[s]);
      }
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: sweep plan (squad {:08X}, {}): centre pad {:08X} at ({:.0f} {:.0f} {:.0f}), {} pads "
                  "within {} links, {} routes out:{}",
                  key, why, centre, cp[0], cp[1], cp[2], seen.size(), hops, h.pads.size() - 1,
                  list.empty() ? std::string(" none") : list);
    }
  }

  // His next sweep leg: his own lead (her pad at a glimpse) first, else the squad plan's next pad (a squad mate on the
  // previous one: the pair sweeps neighbouring pads), else his own pad. No SEARCH: a pause, then another try.
  bool NextLeg(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const std::string &why)
  {
    const uint32_t key = HuntKey(g, c);
    const auto it = g_hunts.find(key);
    uint32_t pad = g.redirect_pad;
    const bool lead = Ptr(pad);
    g.redirect_pad = 0;
    g.end_leg = false;
    g.stuck_look = false;
    g.prog_at = now;
    g.prog_moved = false;
    for (int i = 0; i < 3; ++i)
    {
      g.prog_ref[i] = LoadF(base, c + kPosition + 4 * i);
    }
    if (g.stuck == 2 && !lead)
    {
      // Round 7 (item 2): his last leg made no progress: this one is a look round where he stands.
      pad = Load32(base, c + kPad);
      g.stuck_look = Ptr(pad);
    }
    g.stuck = 0;
    // Round 3 (hand test 005: the hall hunters bunched at her last spot): the plan's pads are shared out. A pad another
    // live hunter of the squad is sweeping, or one within pdz_lab_stealth_hunt_spread_cm of his, is skipped; her last
    // known pad (the plan's first) goes to one hunter only. If every pad is taken: the one farthest from the others.
    std::vector<std::pair<uint32_t, std::array<float, 3>>> others;
    for (const auto &[other, o] : g_guards)
    {
      float p[3];
      if (other != c && o.st == St::Hunting && HuntKey(o, other) == key && PadPos(base, o.leg_pad, p))
      {
        others.push_back({other, {p[0], p[1], p[2]}});
      }
    }
    std::string spread;
    if (!Ptr(pad) && it != g_hunts.end() && !it->second.pads.empty())
    {
      Hunt &h = it->second;
      const float spread_cm = std::max(0.0f, float(REXCVAR_GET(pdz_lab_stealth_hunt_spread_cm)));
      uint32_t best = 0;
      float best_gap = -1.0f;
      size_t best_i = 0;
      for (size_t k = 0; k < h.pads.size(); ++k)
      {
        const size_t i = (h.next + k) % h.pads.size();
        float p[3];
        if (!PadPos(base, h.pads[i], p))
        {
          continue;
        }
        float gap = 1e9f;
        for (const auto &o : others)
        {
          const float dx = p[0] - o.second[0], dy = p[1] - o.second[1], dz = p[2] - o.second[2];
          gap = std::min(gap, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        if (std::find(g.swept.begin(), g.swept.end(), h.pads[i]) != g.swept.end() && h.pads.size() > g.swept.size())
        {
          gap = std::min(gap, 0.5f * spread_cm); // a pad he swept lately: only if nothing new is free
        }
        if (gap >= spread_cm)
        {
          best = h.pads[i];
          best_gap = gap;
          best_i = i;
          break;
        }
        if (gap > best_gap)
        {
          best = h.pads[i];
          best_gap = gap;
          best_i = i;
        }
      }
      if (Ptr(best))
      {
        pad = best;
        h.next = best_i + 1;
        spread = others.empty() ? std::string()
                 : best_gap >= 1e8f ? std::string()
                                    : fmt::format(", {:.0f} cm from the nearest other hunter's pad{}", best_gap,
                                                  best_gap < spread_cm ? " (every pad of the plan is taken)" : "");
      }
    }
    if (!Ptr(pad))
    {
      pad = Load32(base, c + kPad);
    }
    std::string pair;
    if (!others.empty())
    {
      pair = fmt::format(", pair: {} ({} hunters of the squad sweep)", Id(base, others.front().first), others.size() + 1);
    }
    pair += spread;
    g.leg_started = now;
    if (Ptr(pad) && GiveSearch(ctx, base, c, pad))
    {
      g.leg_pad = pad;
      g.swept.push_back(pad);
      if (g.swept.size() > 4)
      {
        g.swept.erase(g.swept.begin());
      }
      Set(base, c, g, St::Hunting, now,
          fmt::format("{}: leg {} at pad {:08X}{}{}", why, g.legs_done + 1, pad, lead ? " (his own lead: her glimpse)" : "",
                      pair));
      return true;
    }
    g.leg_pad = 0;
    g.pause_until = now + float(REXCVAR_GET(pdz_lab_stealth_hunt_pause_s));
    Set(base, c, g, St::HuntPause, now, why + ": could not build the SEARCH task (a pause, then another try)");
    return false;
  }

  // A confirmed guard starts (or restarts) hunting around `centre` (0: his own pad). The squad's plan is re-centred
  // when the centre is new (a later lost contact knows a newer spot); the squad's first hunter asks for the tannoy.
  void StartHunt(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t centre, float now, const std::string &why)
  {
    if (!g.squad)
    {
      g.squad = SquadOf(ctx, base, c);
    }
    const uint32_t key = HuntKey(g, c);
    Hunt &h = g_hunts[key];
    if (!Ptr(centre))
    {
      centre = Load32(base, c + kPad);
    }
    if (Ptr(centre) && centre != h.centre)
    {
      PlanSweep(base, h, key, centre, c, why);
    }
    if (!h.announced)
    {
      h.announced = true;
      if (g_tannoy_want < 0.0f)
      {
        g_tannoy_want = now;
        g_tannoy_by = c;
        g_tannoy_why = fmt::format("{} is the first hunter of squad {:08X}", Id(base, c), key);
      }
    }
    g.legs_done = 0;
    g.leg_fails = 0;
    NextLeg(ctx, base, c, g, now, why);
  }

  // A hidden glimpse while hunting: his next leg goes to her pad (his own lead; the squad plan is not told). With
  // `end_current` his running leg ends at its next SEARCH update (Rare's own end, see the SEARCH update hook).
  void Redirect(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool end_current)
  {
    g.redirect_at = now;
    const uint32_t pad = Load32(base, jo + kPad);
    if (!Ptr(pad) || pad == g.leg_pad)
    {
      return;
    }
    // Round 3 (r3_c crash: three hunters of one squad re-tasked onto her pad in one frame): one hunter of a squad follows
    // a glimpse; the others keep their legs (their own sight and meter still run).
    const auto hs = g_hunts.find(HuntKey(g, c));
    if (hs != g_hunts.end())
    {
      if (hs->second.redirect_pad == pad && now >= hs->second.redirect_at && now - hs->second.redirect_at < 10.0f)
      {
        return;
      }
      hs->second.redirect_pad = pad;
      hs->second.redirect_at = now;
    }
    g.redirect_pad = pad;
    g.end_leg = end_current;
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} glimpsed Jo while hunting (meter {:.0f}, at ({:.0f} {:.0f} {:.0f})): {}next leg at "
                  "her pad {:08X}",
                  Id(base, c), g.meter, g.glimpse_pos[0], g.glimpse_pos[1], g.glimpse_pos[2],
                  end_current ? "his leg ends, " : "", pad);
    }
    WarnGlimpse(ctx, base, c, g, now, "glimpse while hunting (sight takes)");
  }

  // Round 5: he knows Jo is here and where: Rare's sight has her (the meter filled, or his valid-target bit), he is in
  // Rare's combat or just leaving it, or she hit him in the last 10 s. A glimpse the meter hid is not knowing.
  bool Knows(const uint8_t *base, uint32_t c, const Guard &g, float now)
  {
    return g.spotted || g.st == St::Combat || g.st == St::Lost || g.combat_script != 0 ||
           (Load32(base, c + kSight) & kValidTarget) != 0 || (now >= g.hurt_at && now - g.hurt_at < 10.0f);
  }

  // Round 5 (5b): he noticed something but does not go to look: alert for pdz_lab_stealth_alert_s (his meter fills
  // faster), and, when he is calm at his routine, he stops where he stands and looks round for
  // pdz_lab_stealth_alert_look_s: Rare's SEARCH at his own pad (its Search_Look animations), ended by Rare's own end.
  void Alert(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const std::string &why)
  {
    const float secs = float(REXCVAR_GET(pdz_lab_stealth_alert_s));
    if (secs <= 0.0f || g.st == St::Dead || g.dead)
    {
      return;
    }
    const bool was = now < g.alert_until;
    g.alert_until = now + secs;
    bool look = false;
    const float look_s = float(REXCVAR_GET(pdz_lab_stealth_alert_look_s));
    const uint32_t pad = Load32(base, c + kPad);
    if (look_s > 0.0f && !g.scientist && (g.st == St::Unaware || g.st == St::Wary) && now - g.search_seen > 1.0f &&
        now - g.look_started > look_s + 6.0f && !(Load32(base, c + kConfig) & (kSearching | kNeverInvestigates)) &&
        g.combat_script == 0 && Ptr(pad) && !NoLook(g.who) && // (Nightclub helper: not for the row's no_look ids)
        !RadioNoLook(base, c, g, "his alert"))                // (Mansion helper: a radio user who can still call)
    {
      look = GiveSearch(ctx, base, c, pad);
      if (look)
      {
        g.look_started = now;
        g.look_pad = pad;
      }
    }
    if (Logging() && (!was || look))
    {
      REXLOG_INFO("pdz_lab_stealth: {} is alert for {:.0f} s ({}): his meter fills {:.1f} times faster{}", Id(base, c), secs,
                  why, float(REXCVAR_GET(pdz_lab_stealth_alert_rate)),
                  look ? fmt::format("; he stops and looks round for {:.0f} s", look_s) : std::string());
    }
  }
  // His look round where he stands is running (the state machine leaves it alone; its lines are not said).
  bool Looking(const Guard &g, float now)
  {
    return g.look_started > -1e8f && now >= g.look_started &&
           now - g.look_started < float(REXCVAR_GET(pdz_lab_stealth_alert_look_s)) + 6.0f;
  }

  // Rare's SEE_BODY walk is over: the body counts when Jo killed someone within kBodyNearCm of where he stopped.
  bool BodyCheck(const uint8_t *base, uint32_t c, Guard &g, float now)
  {
    g.body_pending = false;
    const Kill *best = nullptr;
    float best_d = kBodyNearCm;
    for (const Kill &k : g_kills)
    {
      const float dx = k.pos[0] - LoadF(base, c + kPosition), dy = k.pos[1] - LoadF(base, c + kPosition + 4),
                  dz = k.pos[2] - LoadF(base, c + kPosition + 8);
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d <= best_d)
      {
        best = &k;
        best_d = d;
      }
    }
    if (!best)
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} saw a body, but Jo killed nobody within {:.0f} cm of where his walk ended (the "
                    "script's body?): not a confirmed contact",
                    Id(base, c), kBodyNearCm);
      }
      return false;
    }
    Confirm(base, c, g, kCfBody,
            fmt::format("found the body of {} ({:.0f} cm away, killed by Jo {:.0f} s ago)", best->who, best_d,
                        now - best->at));
    return true;
  }

  // The tannoy's "location unknown" (header point 10), asked for by a squad's first hunter.
  void TryTannoy(PPCContext &ctx, uint8_t *base, float now)
  {
    if (g_tannoy_want < 0.0f)
    {
      return;
    }
    const float cooldown = float(REXCVAR_GET(pdz_lab_stealth_tannoy_s));
    const char *drop = nullptr;
    if (!REXCVAR_GET(pdz_lab_stealth_tannoy))
    {
      drop = "pdz_lab_stealth_tannoy is off";
    }
    else if (AlarmOn(base))
    {
      drop = "the alarm sounds (Rare's own area alerts play then)";
    }
    else if (!g_tannoy_cannot_locate && now >= g_tannoy_last && now - g_tannoy_last < cooldown)
    {
      drop = "it played less than pdz_lab_stealth_tannoy_s ago";
    }
    else if (now < g_tannoy_want || now - g_tannoy_want > 15.0f)
    {
      drop = "the voice budget stayed busy for 15 s";
    }
    // Round 5 (item 6, log 027 07:33:16: "Tracking system is offline!" on Secret Agent while it ran). Rare plays
    // SECURITY12 only on Agent or once the tracking system is destroyed (82320124-823203C0), so ours follows:
    // a lost contact: not at all while the tracking system runs; take 1 ("location unknown") on Agent (where it never
    // runs: round 6), take 1 or 2 ("tracking system is offline") once it is destroyed. An alarm that ended by itself:
    // take 3 ("We cannot locate the intruder! Stay on your guard!"), only when the tracking system is not running
    // (so on Agent too, since round 6).
    const bool tracking = TrackingRuns(base);
    const bool agent = Difficulty(base) == 0;
    uint32_t takes = g_tannoy_cannot_locate ? 0x4u : agent ? 0x1u : 0x3u;
    if (!drop && tracking)
    {
      drop = "the tracking system runs (Rare never plays this tannoy then)";
    }
    if (drop)
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: tannoy TANNOY_SECURITY12 not played ({}): {}", g_tannoy_why, drop);
      }
      g_tannoy_want = -1.0f;
      g_tannoy_cannot_locate = false;
      return;
    }
    if (StoryPlaying(base, now))
    {
      g_tannoy_want = now; // round 5 (item 7): our tannoy waits for Rare's scripted line
      return;
    }
    if (Ptr(g_tannoy_by) && !g_tannoy_cannot_locate)
    {
      const Guard *by = Find(g_tannoy_by);
      if (by && by->st == St::Dead)
      {
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: tannoy TANNOY_SECURITY12 not played ({}): he died before it could play", g_tannoy_why);
        }
        g_tannoy_want = -1.0f;
        return;
      }
    }
    if (now >= g_last_line_end - 30.0f && now < g_last_line_end + float(REXCVAR_GET(pdz_lab_stealth_voice_gap_s)) + 1.0f)
    {
      return; // wait for the budget: one voice at a time
    }
    g_tannoy_want = -1.0f;
    if (!g_cue_ref)
    {
      g_cue_ref = REX_KERNEL_MEMORY()->SystemHeapAlloc(kCueRefSize);
      if (!g_cue_ref)
      {
        return;
      }
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g_cue_ref;
      frame.ctx.r4.u64 = kTannoyCue; // Rare's own string, as 823203A0 passes it
      sub_82202D20(frame.ctx, base);
    }
    if (int32_t(Load32(base, g_cue_ref + 256)) < 0)
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: tannoy TANNOY_SECURITY12 not in the loaded banks ({})", g_tannoy_why);
      }
      return;
    }
    {
      // The tick's own call (823203AC-823203C0): f1 = [82089540] 5.0, f2 = 0.0, r6 1, r7 0, r8 0.
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g_cue_ref;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 1;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.f1.f64 = 5.0;
      frame.ctx.f2.f64 = 0.0;
      sub_822014B8(frame.ctx, base);
    }
    g_voice_last = now;
    g_tannoy_last = now;
    g_tannoy_takes = takes; // forced when the sound is primed (PrimeBegin)
    g_tannoy_takes_at = now;
    const bool cannot = g_tannoy_cannot_locate;
    g_tannoy_cannot_locate = false;
    g_last_line_end = now + 4.8f; // the announcement (3.9 - 4.4 s) and its 0.3 s start delay: guards wait for it
    if (cannot && REXCVAR_GET(pdz_lab_stealth_all_clear))
    {
      g_allclear_at = now + 5.5f;
      g_allclear_until = now + 14.0f;
      g_allclear_by = 0;
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: tannoy TANNOY_SECURITY12 queued, take {} ({}; tracking system {}, difficulty {}): {}",
                  takes == 0x4 ? "3" : takes == 0x1 ? "1" : "1 or 2",
                  takes == 0x4   ? "\"We cannot locate the intruder! Stay on your guard!\""
                  : takes == 0x1 ? "\"Security breach! Location unknown!\""
                                 : "\"location unknown\" or \"tracking system is offline\"",
                  TrackingText(base), Difficulty(base), g_tannoy_why);
    }
  }

  // Round 5 (item 6, pdz_lab_stealth_all_clear): after the "cannot locate" tannoy one live trooper who went back to
  // his post says Rare's SEARCH_ABANDON "All clear" take (take 2 of the _TEAM cue in every trooper voice: "All clear.
  // Move it!" / "All clear, move out!" / "All clear here! Move out!"). Asked for as SEARCH_ABANDON_SOLO: Rare makes it
  // the _TEAM cue for a guard whose squad has more than one member (only such a guard is asked); the take is forced
  // when that cue is primed.
  void AllClearTick(PPCContext &ctx, uint8_t *base, float now)
  {
    if (now < g_allclear_at || now >= g_allclear_until || now - g_allclear_try < 0.7f || StoryPlaying(base, now))
    {
      return;
    }
    g_allclear_try = now;
    const uint32_t jo = Load32(base, kPlayerOne);
    uint32_t best = 0;
    float best_d = 1e9f;
    for (auto &[c, g] : g_guards)
    {
      if (g.scientist || g.dead || g.st != St::Wary || g.voice < 17 || g.voice > 19 || !g.squad || !Ptr(jo) ||
          int16_t(Load32(base, g.squad + 24) & 0xFFFF) <= 1) // squad +26: its member count (Rare's _TEAM rule, sub_82152FB0)
      {
        continue;
      }
      const float d = Dist(base, c, jo);
      if (d < best_d)
      {
        best = c;
        best_d = d;
      }
    }
    if (!best)
    {
      g_allclear_until = -1e9f;
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: \"All clear\" not said: no wary trooper with squad mates (a lone guard has no such take)");
      }
      return;
    }
    Guard &g = g_guards[best];
    g.allclear_at = now;
    g.mask_ev = kEvAbandon;
    g.mask_takes = 0xFFFFFFFFu; // any take of the _SOLO cue passes the say-event; the _TEAM cue's take 2 is forced at Prime
    g.mask_at = now;
    g_say_force = true;
    const bool said = Say(ctx, base, best, kEvAbandon, false, "all clear");
    g_say_force = false;
    if (said)
    {
      g_allclear_until = -1e9f;
      g_allclear_by = best;
    }
  }

  // --- Round 4: investigations, bodies, the alarm (header point 12) ---

  uint32_t EventIndex(const uint8_t *base, const std::string &name);
  void ArmSwap(uint8_t *base, uint32_t c, uint32_t voice, const char *cue, float now);

  constexpr uint32_t kDormant = 0x10;    // +6272 0x10: Rare's dormant bit (sub_825818E0, SetDormant)
  constexpr uint32_t kPadFlags = 0x34;   // pad flags: 0x4 / 0x8 = cover (the cover planner's pad test, ai_state_log.cpp)
  constexpr uint8_t kRoleAlarmUser = 4;  // setup +0xFD (6d 1.1)

  float Dist3(const float a[3], const float b[3])
  {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  void PosOf(const uint8_t *base, uint32_t c, float out[3])
  {
    for (int i = 0; i < 3; ++i)
    {
      out[i] = LoadF(base, c + kPosition + 4 * i);
    }
  }

  // Pads reached over open links from `from` (at most `hops` links, `cap` pads), with their link counts.
  std::vector<std::pair<uint32_t, int>> PadsAround(const uint8_t *base, uint32_t from, int hops, size_t cap)
  {
    std::vector<std::pair<uint32_t, int>> seen;
    if (!Ptr(from))
    {
      return seen;
    }
    seen.push_back({from, 0});
    for (size_t i = 0; i < seen.size() && seen.size() < cap; ++i)
    {
      if (seen[i].second >= hops)
      {
        continue;
      }
      uint32_t link = Load32(base, seen[i].first + 8);
      for (int n = 0; Ptr(link) && n < 32; ++n, link = Load32(base, link + 4))
      {
        const uint32_t to = Load32(base, link);
        if (base[link + 14] != 0 || base[link + 15] == 0 || !Ptr(to))
        {
          continue; // closed by an obstacle box (link +14), or a line-of-sight-only link (+15: Rare's pathfinder skips it)
        }
        if (std::none_of(seen.begin(), seen.end(), [to](const std::pair<uint32_t, int> &s) { return s.first == to; }))
        {
          seen.push_back({to, seen[i].second + 1});
        }
      }
    }
    return seen;
  }

  // The pad nearest `pos` reachable from his own pad (within 40 links: a squad mate may be called from 40 m away).
  uint32_t NearestPadTo(const uint8_t *base, uint32_t from, const float pos[3], float &out_d)
  {
    uint32_t best = 0;
    out_d = 1e9f;
    for (const auto &[pad, hop] : PadsAround(base, from, 40, 900))
    {
      float p[3];
      if (PadPos(base, pad, p))
      {
        const float d = Dist3(p, pos);
        if (d < out_d)
        {
          best = pad;
          out_d = d;
        }
      }
    }
    return best;
  }

  // Round 7 (item 2): the no-progress rule only looks from this long after a walk began (tests r7_2b: a free guard
  // stands up to 6 s at the start of Rare's walk to a body and of a SEARCH leg: Rare's reaction, turn and start
  // animations); from then on, pdz_lab_stealth_stuck_s without 1 m of progress.
  constexpr float kStuckStart = 4.0f; // round 11 (was 8)
  // Round 5: Rare's AI sight ray (the physics ray sub_82259B80 with mask 0x20, as his sight test's sub_8216D3A8 casts
  // it, and as the bots' shot test in cut_scenarios.cpp has long called it): true when nothing that blocks AI sight
  // lies on the segment a -> b (cm). A mesh triangle is only hit from its front, so SightClear tests both ways.
  bool SegmentClear(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3], uint32_t mask = 0x20)
  {
    constexpr uint32_t kPhysicsWorld = 0x82E3C2F4; // sub_82259B80 reads [82E3C2F4] and [82E3C2F0]
    if (!Ptr(Load32(base, kPhysicsWorld)) || !Ptr(Load32(base, kPhysicsWorld - 4)))
    {
      return true; // no level
    }
    // Guest stack scratch below the caller's frame: from +0, to +16, the visitor +32 (vtable, hit byte +4).
    const uint32_t buf = (ctx.r1.u32 - 0x400) & ~0xFu;
    for (uint32_t i = 0; i < 3; ++i)
    {
      uint32_t bits;
      std::memcpy(&bits, &a[i], 4);
      Store32(base, buf + i * 4, bits);
      std::memcpy(&bits, &b[i], 4);
      Store32(base, buf + 16 + i * 4, bits);
    }
    Store32(base, buf + 12, 0);
    Store32(base, buf + 28, 0);
    Store32(base, buf + 32, 0x820906BC); // Rare's any-hit visitor (the one sub_8216D3A8 builds)
    Store32(base, buf + 36, 0);
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = buf - 0x800; // 16-byte aligned
    frame.ctx.r3.u64 = mask;        // 0x20 blocks AI sight (0x40: Rare's block-AI-sound flag)
    frame.ctx.r4.u64 = buf;
    frame.ctx.r5.u64 = buf + 16;
    frame.ctx.r6.u64 = buf + 32;
    frame.ctx.r7.u64 = 1;
    sub_82259B80(frame.ctx, base);
    return (frame.ctx.r3.u32 & 0xFF) == 0;
  }
  bool SightClear(PPCContext &ctx, uint8_t *base, const float thing[3], const float eye[3])
  {
    return SegmentClear(ctx, base, thing, eye) && SegmentClear(ctx, base, eye, thing);
  }

  // Round 7 (item 4; log 048 06:44:17: s3g4, 7.6 m from a ceiling lamp Jo shot, had "no line of sight": the ray to a
  // point 40 cm under the lamp hit its own fitting or the ceiling beams). What a guard can see of a lamp going out:
  // a point 1 m under it, or the floor it lit (found by Rare's sight ray straight down, 1 m steps, 12 m at most; the
  // point 30 cm above the last clear step). Built once per lamp; a closed door still blocks either ray.
  struct LampView
  {
    float below[3]; // 1 m under the lamp
    float floor[3]; // the lit floor under it
    bool has_floor = false;
  };
  LampView LampViewOf(PPCContext &ctx, uint8_t *base, const float pos[3])
  {
    LampView v;
    const float top[3] = {pos[0], pos[1] - 60.0f, pos[2]};
    v.below[0] = pos[0];
    v.below[1] = pos[1] - 100.0f;
    v.below[2] = pos[2];
    float clear_y = top[1];
    for (int k = 1; k <= 12; ++k)
    {
      const float p[3] = {pos[0], pos[1] - 60.0f - 100.0f * float(k), pos[2]};
      if (!SegmentClear(ctx, base, top, p))
      {
        v.has_floor = k > 1;
        break;
      }
      clear_y = p[1];
    }
    v.floor[0] = pos[0];
    v.floor[1] = clear_y + 30.0f;
    v.floor[2] = pos[2];
    return v;
  }
  // 0 not seen, 1 the point under the lamp, 2 the lit floor under it.
  int LampSeen(PPCContext &ctx, uint8_t *base, const LampView &v, const float eye[3])
  {
    if (SightClear(ctx, base, v.below, eye))
    {
      return 1;
    }
    return v.has_floor && SightClear(ctx, base, v.floor, eye) ? 2 : 0;
  }

  // His eye: from his last sight test (Rare's own eye point, at most 3 s old), else 150 cm above his feet.
  void EyeOf(const uint8_t *base, uint32_t c, const Guard &g, float now, float out[3])
  {
    PosOf(base, c, out);
    if (now >= g.eye_at && now - g.eye_at < 3.0f && Dist3(out, g.eye) < 300.0f)
    {
      std::memcpy(out, g.eye, 3 * sizeof(float));
    }
    else
    {
      out[1] += 150.0f;
    }
  }

  // True when an open walking route of at most max_cm leads from pad `from` to pad `to` over Rare's pad links
  // (walkable links only, closed ones skipped): Dijkstra over the link lengths.
  bool OpenPathWithin(const uint8_t *base, uint32_t from, uint32_t to, float max_cm)
  {
    if (!Ptr(from) || !Ptr(to))
    {
      return false;
    }
    std::vector<std::pair<uint32_t, float>> best{{from, 0.0f}};
    std::vector<bool> done{false};
    for (size_t step = 0; step < 600; ++step)
    {
      size_t at = best.size();
      for (size_t i = 0; i < best.size(); ++i)
      {
        if (!done[i] && (at == best.size() || best[i].second < best[at].second))
        {
          at = i;
        }
      }
      if (at == best.size())
      {
        return false;
      }
      done[at] = true;
      const uint32_t pad = best[at].first;
      const float d = best[at].second;
      if (pad == to)
      {
        return true;
      }
      float p[3];
      if (!PadPos(base, pad, p))
      {
        continue;
      }
      uint32_t link = Load32(base, pad + 8);
      for (int n = 0; Ptr(link) && n < 32; ++n, link = Load32(base, link + 4))
      {
        const uint32_t next = Load32(base, link);
        float q[3];
        if (!Ptr(next) || base[link + 14] != 0 || base[link + 15] == 0 || !PadPos(base, next, q))
        {
          continue;
        }
        const float nd = d + Dist3(p, q);
        if (nd > max_cm)
        {
          continue;
        }
        const auto it = std::find_if(best.begin(), best.end(), [next](const auto &b) { return b.first == next; });
        if (it == best.end())
        {
          best.push_back({next, nd});
          done.push_back(false);
        }
        else if (nd < it->second)
        {
          it->second = nd;
        }
      }
    }
    return false;
  }

  void StartInvestigation(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, Inv kind, const float centre[3], int body,
                          float now, const std::string &why, uint32_t centre_pad = 0);
  void Alert(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const std::string &why);

  // ---- Round 6 (item 1): a body investigation that looks like one ----
  // Log 062 16:55:35: s1m5's "pad beside the body" was the nearest pad in 3D, on the floor BELOW the walkway the body
  // lay on, so he walked away along the walkway to find a way down. Every spot is now on the body's floor.
  constexpr float kFloorCm = 100.0f;

  // The pads on one floor: reached from `from` over open links through pads whose height is within kFloorCm of
  // `floor_y` only, with the walking distance along those links (cm), up to `max_walk`.
  std::vector<std::pair<uint32_t, float>> FloorPads(const uint8_t *base, uint32_t from, float floor_y, float max_walk)
  {
    std::vector<std::pair<uint32_t, float>> best;
    float fp[3];
    if (!PadPos(base, from, fp) || std::fabs(fp[1] - floor_y) > kFloorCm)
    {
      return best;
    }
    best.push_back({from, 0.0f});
    std::vector<bool> done(1, false);
    for (int guard = 0; guard < 400; ++guard)
    {
      int at = -1;
      for (size_t i = 0; i < best.size(); ++i)
      {
        if (!done[i] && (at < 0 || best[i].second < best[size_t(at)].second))
        {
          at = int(i);
        }
      }
      if (at < 0)
      {
        break;
      }
      done[size_t(at)] = true;
      const uint32_t pad = best[size_t(at)].first;
      const float d = best[size_t(at)].second;
      float p[3];
      if (!PadPos(base, pad, p))
      {
        continue;
      }
      uint32_t link = Load32(base, pad + 8);
      for (int n = 0; Ptr(link) && n < 32; ++n, link = Load32(base, link + 4))
      {
        const uint32_t next = Load32(base, link);
        float q[3];
        if (!Ptr(next) || base[link + 14] != 0 || base[link + 15] == 0 || !PadPos(base, next, q) ||
            std::fabs(q[1] - floor_y) > kFloorCm)
        {
          continue;
        }
        const float nd = d + Dist3(p, q);
        if (nd > max_walk)
        {
          continue;
        }
        const auto it = std::find_if(best.begin(), best.end(), [next](const auto &b) { return b.first == next; });
        if (it == best.end())
        {
          if (best.size() < 300)
          {
            best.push_back({next, nd});
            done.push_back(false);
          }
        }
        else if (nd < it->second)
        {
          it->second = nd;
        }
      }
    }
    return best;
  }

  // The pad beside a thing on the thing's own floor: the nearest pad he can walk to (from his own pad) whose height is
  // within kFloorCm of it and which is within `reach` of it on the ground plan. 0: none (a ledge without pads).
  uint32_t FloorPadAt(const uint8_t *base, uint32_t from, const float pos[3], float reach, float &out_d)
  {
    uint32_t best = 0;
    out_d = 1e9f;
    for (const auto &[pad, hop] : PadsAround(base, from, 40, 900))
    {
      float p[3];
      if (!PadPos(base, pad, p) || std::fabs(p[1] - pos[1]) > kFloorCm)
      {
        continue;
      }
      const float dx = p[0] - pos[0], dz = p[2] - pos[2];
      const float d = std::sqrt(dx * dx + dz * dz);
      if (d <= reach && d < out_d)
      {
        best = pad;
        out_d = d;
      }
    }
    return best;
  }

  // Round 11 (item 5; log 010 17:01:04: s1m1 was sent from the floor below to pads 22 m away, s1m3 34 m along the
  // floor and never moved): can this guard walk to the thing? He stands on its floor (his own pad within kFloorCm of
  // its height, as FloorPads has it) and a route over open pad links that stays on that floor, no longer than
  // max_walk, ends at a pad within 6 m of it on the ground plan. out_walk: that route's length.
  bool CanReachOnFloor(const uint8_t *base, uint32_t c, const float pos[3], float max_walk, float &out_walk)
  {
    out_walk = 1e9f;
    const uint32_t from = Load32(base, c + kPad);
    if (!Ptr(from))
    {
      return false;
    }
    for (const auto &[pad, walk] : FloorPads(base, from, pos[1], max_walk))
    {
      float p[3];
      if (!PadPos(base, pad, p))
      {
        continue;
      }
      const float dx = p[0] - pos[0], dz = p[2] - pos[2];
      if (dx * dx + dz * dz <= 600.0f * 600.0f && walk < out_walk)
      {
        out_walk = walk;
      }
    }
    return out_walk < 1e8f;
  }

  // The corpse objects Rare made in this run (his corpse constructor sub_82559318): the body of a kill is the newest
  // one made from the victim's character.
  constexpr uint32_t kCorpseVtable = 0x820C0830, kCorpseType = 43;
  bool IsCorpse(const uint8_t *base, uint32_t object)
  {
    return Ptr(object) && pdz::guestmem::Ok(base, object, 16) && // (Sun 22 Nov night (audit): cached corpses may be freed)
           Load32(base, object) == kCorpseVtable && Load32(base, object + 12) == kCorpseType;
  }
  bool ObjectPos(PPCContext &ctx, uint8_t *base, uint32_t object, float out[3])
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = object;
    sub_82151210(frame.ctx, base);
    const uint32_t v = frame.ctx.r3.u32;
    if (!Ptr(v))
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      out[i] = LoadF(base, v + 4 * uint32_t(i));
      if (!std::isfinite(out[i]) || std::fabs(out[i]) > 1e6f)
      {
        return false;
      }
    }
    return true;
  }
  // The corpse of a kill, for this guard: the one Rare made from the victim, else the nearest body in his own visible
  // list (Rare's SEE_BODY reaction's pick, 821D12D4-821D12E0); within 5 m of where the victim fell. 0: none.
  uint32_t CorpseOf(PPCContext &ctx, uint8_t *base, uint32_t c, const Kill &k, float where[3], const char *&how)
  {
    for (auto it = g_corpses.rbegin(); it != g_corpses.rend(); ++it)
    {
      if (it->victim == k.victim && it->at >= k.at - 1.0f && IsCorpse(base, it->object) && ObjectPos(ctx, base, it->object, where) &&
          Dist3(where, k.pos) < 500.0f)
      {
        how = "the corpse Rare made of him";
        return it->object;
      }
    }
    const int32_t slot = int32_t(Load32(base, c + kProfileSlot));
    if (slot >= 0 && uint32_t(slot) < kProfileSlots)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = uint32_t(slot);
      frame.ctx.r4.u64 = kCorpseType;
      frame.ctx.r5.u64 = 0;
      sub_8218B340(frame.ctx, base);
      const uint32_t object = frame.ctx.r3.u32;
      if (IsCorpse(base, object) && ObjectPos(ctx, base, object, where) && Dist3(where, k.pos) < 500.0f)
      {
        how = "the nearest body in his own view";
        return object;
      }
    }
    return 0;
  }

  // Rare's walk to a body, as his SEE_BODY reaction builds it (821D12F0-821D1464): a composite task (class 820922A4)
  // of a go-to-object part (class 82092E88, sub_821D5678 with the corpse and Rare's distance [8208A408]), a
  // look-at-object part (sub_821A1538) and the reaction's animation part (sub_821A9D60, record 82D2D240), queued on
  // top of his running task with sub_82171210 as the reaction's caller does (821D1868). Its two bigger parts come
  // from Rare's task pool 82E5D43C (its free list first, then the allocator with tag 10), the small ones from
  // sub_821784F0. Returns the composite, or 0 (nothing queued).
  uint32_t PoolTask(PPCContext &ctx, uint8_t *base, uint32_t bytes)
  {
    constexpr uint32_t kPool = 0x82E5D43C;
    const uint32_t head = Load32(base, kPool + 4);
    if (Ptr(head))
    {
      Store32(base, kPool + 4, Load32(base, head));
      Store32(base, kPool + 16, Load32(base, kPool + 16) + 1);
      return head;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = bytes;
    frame.ctx.r4.u64 = 10;
    sub_8239D740(frame.ctx, base);
    return Ptr(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
  }
  uint32_t WalkToBody(PPCContext &ctx, uint8_t *base, uint32_t c, uint32_t corpse)
  {
    if (R7Refuse(base, c, "walk him to a body")) // (Tue 17 Nov: he is Rare's COMBAT's)
    {
      return 0;
    }
    bool top_is_search = false;
    const int depth = StackDepth(base, c), waiting = StackWaiting(base, c);
    if (depth < 1 || depth >= kGiveDepth || waiting != 0 || StackSearches(base, c, top_is_search) > 0 ||
        (Load32(base, c + kConfig) & kSearching) != 0 || !IsCorpse(base, corpse))
    {
      return 0;
    }
    const uint32_t composite = PoolTask(ctx, base, 96);
    if (!composite)
    {
      return 0;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      sub_821CA7E0(frame.ctx, base);
      Store32(base, composite, 0x820922A4);
    }
    uint32_t anim = 0, look = 0, go = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 88;
      sub_821784F0(frame.ctx, base);
      anim = Ptr(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
    }
    if (anim)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = anim;
      frame.ctx.r4.u64 = 0x82D2D240;
      sub_821A9D60(frame.ctx, base);
      anim = frame.ctx.r3.u32;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 80;
      sub_821784F0(frame.ctx, base);
      look = Ptr(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
    }
    if (look)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = look;
      frame.ctx.r4.u64 = corpse;
      frame.ctx.r5.u64 = 1;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      sub_821A1538(frame.ctx, base);
      look = frame.ctx.r3.u32;
    }
    go = PoolTask(ctx, base, 144);
    if (go)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = go;
      frame.ctx.r4.u64 = corpse;
      frame.ctx.r5.u64 = 1;
      frame.ctx.r6.u64 = 1;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.r9.u64 = 0;
      frame.ctx.r10.u64 = 0;
      frame.ctx.f1.f64 = double(LoadF(base, 0x8208A408));
      sub_821D5678(frame.ctx, base);
      Store32(base, go, 0x82092E88);
    }
    {
      // (The reaction passes its parts through with 0 for one that could not be allocated: Rare's list ends there.)
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = go;
      frame.ctx.r5.u64 = look;
      frame.ctx.r6.u64 = anim;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.r9.u64 = 0;
      frame.ctx.r10.u64 = 0;
      sub_821CA8C0(frame.ctx, base);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c + kScheduler;
    frame.ctx.r4.u64 = composite;
    sub_82171210(frame.ctx, base);
    R9WalkNote(base, c, composite); // (Wed 18 Nov night: only a walk of ours is ever ended early)
    return composite;
  }
  // Round 11c (item 3): a plain walk to a pad: Rare's go-to-pad task (sub_821A3438, the one his SEARCH gives a
  // searcher as a child) as the only part of a composite task queued on top of his running task, as WalkToBody
  // queues the walk to a body. No search behaviour: his ordinary walk. Returns the composite, or 0.
  // Thu 19 Nov: `hurry` is Rare's own hurry byte of the go-to-pad task (r5; the chase route and the bots pass 1).
  uint32_t WalkToPad(PPCContext &ctx, uint8_t *base, uint32_t c, uint32_t pad, uint32_t hurry = 0)
  {
    if (R7Refuse(base, c, "walk him to a pad")) // (Tue 17 Nov: he is Rare's COMBAT's)
    {
      return 0;
    }
    bool top_is_search = false;
    const int depth = StackDepth(base, c), waiting = StackWaiting(base, c);
    if (!Ptr(pad) || depth < 1 || depth >= kGiveDepth || waiting != 0 || StackSearches(base, c, top_is_search) > 0 ||
        (Load32(base, c + kConfig) & kSearching) != 0)
    {
      return 0;
    }
    const uint32_t composite = PoolTask(ctx, base, 96);
    if (!composite)
    {
      return 0;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      sub_821CA7E0(frame.ctx, base);
      Store32(base, composite, 0x820922A4);
    }
    uint32_t go = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 96;
      sub_821784F0(frame.ctx, base);
      go = Ptr(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
    }
    if (go)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = go;
      frame.ctx.r4.u64 = pad;
      frame.ctx.r5.u64 = hurry ? 1 : 0;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      sub_821A3438(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = go;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.r9.u64 = 0;
      frame.ctx.r10.u64 = 0;
      sub_821CA8C0(frame.ctx, base);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c + kScheduler;
    frame.ctx.r4.u64 = composite;
    sub_82171210(frame.ctx, base);
    R9WalkNote(base, c, composite); // (Wed 18 Nov night: only a walk of ours is ever ended early)
    return composite;
  }
  // A task is on his stack or waiting to be pushed.
  bool TaskLive(const uint8_t *base, uint32_t c, uint32_t task)
  {
    const int depth = StackDepth(base, c);
    for (int i = 0; i < depth; ++i)
    {
      if (Load32(base, c + kScheduler + kStackEntry * uint32_t(i)) == task)
      {
        return true;
      }
    }
    return StackWaiting(base, c) > 0;
  }

  // Round 5 (item 3): he noticed a body Jo left (his own sight check below, or he saw the death). Once per body and
  // guard. A guard who never saw or fought Jo: the body rules (he searches round it alone, then his squad or the
  // alarm), unless another guard is on it or it was already reported (then he is alert). One who has seen her: a
  // confirmed contact as in round 3, and a hunter takes the spot as his next leg.
  // Round 7 (item 3; log 048 06:45:38 and 06:46:05: two guards in turn walked up to the spot where Jo had just
  // killed, and died there). ESCALATION TIERS (pdz_lab_stealth_tiers). Every lone investigation (a body, a lamp, a
  // sound) is an incident of its area. Tier 1: the first incident of an area: one guard looks alone (rounds 4 to 6).
  // Tier 2: no lone walk-up: the guard who notices a body or a death reports it at once and his squad comes together
  // (the body rules' squad call, SEE_BODY_TEAM, and the squad search; never the alarm run), when
  //  (a) he is farther than pdz_lab_stealth_tier_far_cm from it (a distant body or death: back off and report), or
  //  (b) an investigator of this area was killed (he never came back), or
  //  (c) another incident lies within pdz_lab_stealth_tier_area_cm and pdz_lab_stealth_tier_s.
  // A lamp going out where a body was found or an investigator was lost is searched by the squad too (the spree rule's
  // search). Only Rare's lines are said.
  struct AreaIncident // (the escalation tiers)
  {
    float pos[3] = {};
    float at = 0.0f;
    uint32_t who = 0;   // the guard who went to look
    std::string name;   // ... his id
    const char *kind = "";
    bool body = false;  // a body or a death (not a lamp or a sound)
    bool lost = false;  // he was killed while he looked
  };
  std::vector<AreaIncident> g_area;
  void AreaNote(const uint8_t *base, uint32_t c, const char *kind, bool body, const float pos[3], float now)
  {
    g_area.erase(std::remove_if(g_area.begin(), g_area.end(),
                                [now](const AreaIncident &a) { return now < a.at || now - a.at > 900.0f; }),
                 g_area.end());
    if (g_area.size() >= 64)
    {
      g_area.erase(g_area.begin());
    }
    AreaIncident a;
    std::memcpy(a.pos, pos, sizeof(a.pos));
    a.at = now;
    a.who = c;
    a.name = Id(base, c);
    a.kind = kind;
    a.body = body;
    g_area.push_back(a);
  }
  // His death while he looked at something: the incident's investigator never came back.
  void AreaLost(const uint8_t *base, uint32_t c)
  {
    for (auto it = g_area.rbegin(); it != g_area.rend(); ++it)
    {
      if (it->who == c && !it->lost)
      {
        it->lost = true;
        if (Logging())
        {
          // Follow-up (Sun 15 Nov): the line says what really follows (under rareplus only the lamp rule of
          // pdz_stealth_lamp_escalation reads this; rounds 3 to 11l: the body tier and the lamp squad search).
          const bool acts = REXCVAR_GET(pdz_lab_stealth_tiers) && (!RarePlus() || REXCVAR_GET(pdz_stealth_lamp_escalation));
          REXLOG_INFO("pdz_lab_stealth: ESCALATION: {} was killed while he looked at a {} at ({:.0f} {:.0f} {:.0f}): he never "
                      "comes back: {}",
                      it->name, it->kind, it->pos[0], it->pos[1], it->pos[2],
                      !acts        ? "noted only, nothing changes (pdz_stealth_lamp_escalation is off)"
                      : RarePlus() ? "pdz_stealth_lamp_escalation: for pdz_lab_stealth_tier_s nobody walks up alone to a lamp "
                                     "that goes out within pdz_lab_stealth_tier_area_cm of here (they stay, say so, are alert)"
                                   : "from now on nobody walks up alone in this area (tier 2)");
        }
        return;
      }
    }
  }
  // Tier 2 for something at `pos` noticed by guard c (0: any guard)? `bodies_only`: only earlier bodies / deaths and
  // lost investigators count (a lamp: the spree rule covers lamps on their own). `why` says which rule.
  bool AreaTier2(const float pos[3], uint32_t c, float now, bool bodies_only, std::string &why)
  {
    if (!REXCVAR_GET(pdz_lab_stealth_tiers))
    {
      return false;
    }
    const float area = float(REXCVAR_GET(pdz_lab_stealth_tier_area_cm));
    const float keep = float(REXCVAR_GET(pdz_lab_stealth_tier_s));
    const AreaIncident *best = nullptr;
    for (const AreaIncident &a : g_area)
    {
      if (now < a.at || now - a.at > keep || Dist3(a.pos, pos) > area || (a.who == c && !a.lost) || (bodies_only && !a.body && !a.lost))
      {
        continue;
      }
      if (Dist3(a.pos, pos) < 100.0f && !a.lost && now - a.at < 1.0f)
      {
        continue; // this very incident
      }
      if (!best || (a.lost && !best->lost))
      {
        best = &a;
      }
    }
    if (!best)
    {
      return false;
    }
    why = best->lost ? fmt::format("{} went to look at a {} here {:.0f} s ago and never came back", best->name, best->kind,
                                   now - best->at)
                     : fmt::format("the second incident in this area ({} looked at a {} {:.0f} cm from here {:.0f} s ago)",
                                   best->name, best->kind, Dist3(best->pos, pos), now - best->at);
    return true;
  }
  bool CallSquad(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, int body, float now, const std::string &why);

  bool AlarmRun(PPCContext &ctx, uint8_t *base, uint32_t c, std::string &refusal);
  // Round 11f (item 3; log 011 20:31-20:34: the whole generator-room squad was shot one by one with a silenced gun;
  // one guard saw a mate die and searched, nothing escalated). Per squad: the first body found or death witnessed
  // (without seeing Jo) gives the search as before. A SECOND one in that squad (a second body, a second death seen,
  // or a body found by a guard who already knows of one) starts an ALARM RUN: the guard himself if he is an alarm
  // user (Rare's role); else he shouts ("Sound the alarm!") and the nearest living alarm user of his squad for whom
  // Rare's alarm run can be built runs; with none, the nearest alarm user within 30 m. True: the guard himself runs
  // (nothing else for him to do); false: the rules as before go on for him.
  bool SecondDeath(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, int index, float now, const char *how)
  {
    if (index < 0 || index >= 64 || g.scientist || g.voice < 17 || g.voice > 19 || g.dead || g.st == St::Dead ||
        g.st == St::Combat || g.st == St::Lost)
    {
      return false;
    }
    const uint32_t key = g.squad ? g.squad : c;
    std::vector<int> &known = g_squad_deaths[key];
    const bool squad_other = std::any_of(known.begin(), known.end(), [index](int i) { return i != index; });
    const bool own_other = (g.deaths_known & ~(uint64_t(1) << index)) != 0;
    if (std::find(known.begin(), known.end(), index) == known.end())
    {
      known.push_back(index);
    }
    g.deaths_known |= uint64_t(1) << index;
    std::vector<int> &done = g_squad_escalated[key];
    if (!REXCVAR_GET(pdz_lab_stealth_second_death) || (!squad_other && !own_other) ||
        std::find(done.begin(), done.end(), index) != done.end() || AlarmOn(base))
    {
      return false;
    }
    done.push_back(index);
    const std::string reason = fmt::format("{} {} of {} ({}), and {} already knows of {} other death(s) of hers", Id(base, c), how,
                                           g_kills[size_t(index)].who, squad_other ? "his squad" : "he",
                                           squad_other ? "his squad" : "he",
                                           squad_other ? known.size() - 1 : size_t(1));
    // Who runs: he himself, the alarm users of his squad nearest first, then any alarm user within 30 m.
    struct Cand
    {
      float d;
      uint32_t c;
      bool squad;
    };
    std::vector<Cand> cands;
    for (const auto &[other, o] : g_guards)
    {
      if (other == c || o.role != kRoleAlarmUser || o.dead || o.st == St::Dead || o.st == St::Combat || o.st == St::Lost ||
          o.run_committed ||
          Load32(base, other + kData) != o.data || !(LoadF(base, other + kHealth) > 0.0f) ||
          (Load32(base, other + kConfig) & kDormant) != 0)
      {
        continue;
      }
      const bool mate = g.squad && o.squad == g.squad;
      const float d = Dist(base, c, other);
      if (mate || d <= 3000.0f)
      {
        cands.push_back({d, other, mate});
      }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.squad != b.squad ? a.squad : a.d < b.d; });
    uint32_t runner = 0;
    std::string tried;
    if (g.role == kRoleAlarmUser)
    {
      std::string refusal;
      if (AlarmRun(ctx, base, c, refusal))
      {
        runner = c;
      }
      else
      {
        tried += fmt::format(" {} himself (an alarm user): {};", Id(base, c), refusal);
      }
    }
    for (size_t i = 0; !runner && i < cands.size(); ++i)
    {
      std::string refusal;
      if (AlarmRun(ctx, base, cands[i].c, refusal))
      {
        runner = cands[i].c;
        tried += fmt::format(" {} ({}, {:.0f} cm from him): runs;", Id(base, runner),
                             cands[i].squad ? "alarm user of his squad" : "no alarm user of his squad could run: the nearest within 30 m",
                             cands[i].d);
      }
      else
      {
        tried += fmt::format(" {} ({:.0f} cm): {};", Id(base, cands[i].c), cands[i].d, refusal);
      }
    }
    if (runner && runner != c)
    {
      // He shouts for it (WARN_TEAM takes 1 and 2: "Sound the alarm!" / "Hit the alarm!").
      Warn(ctx, base, c, g, EventIndex(base, "WARN_TEAM"), now, "second death: shouts for the alarm", 0x3);
    }
    if (runner)
    {
      g_kills[size_t(index)].escalated = true;
    }
    REXLOG_INFO("pdz_lab_stealth: ESCALATION: SECOND DEATH: {}: {}{}", reason,
                runner == c ? "he is an alarm user: HE RUNS FOR THE ALARM (announced first)"
                : runner    ? fmt::format("he is not an alarm user (role {}): he shouts, and {} RUNS FOR THE ALARM (announced "
                                          "first)",
                                          int(g.role), Id(base, runner))
                            : std::string("NO ALARM RUN: no living alarm user could run (the search rules go on)"),
                tried.empty() ? std::string() : " | tried:" + tried);
    return runner == c;
  }

  void NoticeBody(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, int index, float now, const std::string &how)
  {
    Kill &k = g_kills[index];
    if (index < 64)
    {
      g.bodies_seen |= uint64_t(1) << index;
    }
    if (SecondDeath(ctx, base, c, g, index, now, "found the body"))
    {
      g.body_pending = false;
      g.want_body = -1;
      if (g.st == St::Investigating)
      {
        Set(base, c, g, St::Wary, now, "a second death: he runs for the alarm");
      }
      return;
    }
    const bool rules = REXCVAR_GET(pdz_lab_stealth_body_rules) && REXCVAR_GET(pdz_lab_stealth_investigation);
    const char *what;
    if (g.seen_jo || g.fought || g.confirmed || !rules)
    {
      Confirm(base, c, g, kCfBody, fmt::format("saw the body of {} ({})", k.who, how));
      what = "he has seen Jo before: a confirmed contact, no search of his own";
      if (Hunter(g))
      {
        float d = 0.0f;
        const uint32_t pad = NearestPadTo(base, Load32(base, c + kPad), k.pos, d);
        if (Ptr(pad) && now - g.redirect_at > 5.0f)
        {
          g.redirect_pad = pad;
          g.redirect_at = now;
          what = "he hunts her already: his next leg is the body";
        }
      }
      Say(ctx, base, c, kEvSeeBody, false, "sees a body");
    }
    else if (k.escalated)
    {
      what = "it was already reported: he is alert";
      Alert(ctx, base, c, g, now, fmt::format("saw the body of {} (already reported)", k.who));
    }
    else if (k.finder && k.finder != c)
    {
      what = "another guard already searches round it: he is alert";
      Alert(ctx, base, c, g, now, fmt::format("saw the body of {} ({} searches round it)", k.who, Id(base, k.finder)));
    }
    else if (g.st == St::Investigating && (g.inv == Inv::Body || g.inv == Inv::Squad))
    {
      what = "he is busy with another body";
    }
    else if ((Load32(base, c + kConfig) & kSearching) != 0 || now - g.search_seen < 0.5f)
    {
      // He is in a search (Rare's, a leg of ours): it ends first (only the top task may be cut), then the body's
      // investigation starts (After).
      what = "the body rules: his body line at once; his search ends first, then he searches round it";
      if (index < 64)
      {
        g.bodies_seen &= ~(uint64_t(1) << index);
      }
      if (g.want_body != index)
      {
        g.want_body = index;
        g.want_body_at = now;
        if (REXCVAR_GET(pdz_lab_stealth_body_lines) && LayerGuard(g) && !g.prio)
        {
          // Round 11f (item 2; log 011 20:34:07: s3g8 noticed s3g3's body from 10 m mid-search and said nothing for
          // 12 s, "his search ends first"): he reacts at once: his body line (SEE_BODY_TEAM takes 1-5: "Got a man
          // down here!", "Body on the ground." kind), the priority line that always plays (PrioLine: before every
          // other guard line, past Rare's squad throttle), while his running search is being ended.
          g.prio = 2;
          g.prio_from = now;
          g.prio_until = now + float(REXCVAR_GET(pdz_lab_stealth_body_line_s));
          g.prio_try = g.prio_queued = -1e9f;
        }
      }
      if (g.st == St::Hunting)
      {
        g.end_leg = true;
      }
      else if (g.st == St::Investigating)
      {
        g.inv_end_leg = true;
      }
      else if (!g.end_search)
      {
        g.end_search = true;
        g.end_search_at = now;
      }
    }
    else if (std::string tier; REXCVAR_GET(pdz_lab_stealth_tiers) &&
                               ([&] {
                                 float me[3];
                                 PosOf(base, c, me);
                                 const float d = Dist3(me, k.pos);
                                 const float far_cm = float(REXCVAR_GET(pdz_lab_stealth_tier_far_cm));
                                 if (AreaTier2(k.pos, c, now, false, tier))
                                 {
                                   return true;
                                 }
                                 if (far_cm > 0.0f && d > far_cm)
                                 {
                                   tier = fmt::format("a distant body ({:.0f} cm, over pdz_lab_stealth_tier_far_cm {:.0f})", d, far_cm);
                                   return true;
                                 }
                                 return false;
                               })())
    {
      // Round 7 (item 3): tier 2: no lone walk-up: he reports it and his squad comes together (or the alarm run).
      what = "ESCALATION TIER 2: no lone walk-up: he stays back and reports it";
      g.body_pending = false;
      g.want_body = -1;
      AreaNote(base, c, "body", true, k.pos, now);
      REXLOG_INFO("pdz_lab_stealth: ESCALATION TIER 2: {} noticed the body of {} ({}): {}: he does not walk up; he reports it "
                  "and his squad comes together",
                  Id(base, c), k.who, how, tier);
      // (The local squad, never the alarm run: the alarm is the last tier, raised by a guard who saw Jo or by an
      // alarm user whose own look at a first body found nothing.)
      if (CallSquad(ctx, base, c, g, index, now, "tier 2 (" + tier + ")"))
      {
        k.escalated = true;
      }
      else
      {
        Alert(ctx, base, c, g, now, fmt::format("saw the body of {} (tier 2), nobody of his squad to call: he stays back", k.who));
      }
    }
    else
    {
      what = "the body rules: he searches round it alone";
      g.body_pending = false;
      g.want_body = -1;
      StartInvestigation(ctx, base, c, g, Inv::Body, k.pos, index, now,
                         fmt::format("saw the body of {} ({}; killed by Jo {:.0f} s ago); not a confirmed contact, nobody "
                                     "is told",
                                     k.who, how, now - k.at));
      if (g.st == St::Investigating && REXCVAR_GET(pdz_lab_stealth_body_lines))
      {
        // Round 6 (item 2 (a)): SEE_BODY_SOLO before every other guard line outside a fight (PrioLine).
        g.prio = 1;
        g.prio_from = now;
        g.prio_until = now + float(REXCVAR_GET(pdz_lab_stealth_body_line_s));
        g.prio_try = g.prio_queued = -1e9f;
      }
      else if (g.st == St::Investigating)
      {
        Say(ctx, base, c, kEvSeeBody, false, "sees a body");
      }
      else
      {
        if (index < 64)
        {
          g.bodies_seen &= ~(uint64_t(1) << index); // no search could start now: he looks again in a few seconds
        }
        g.body_check_at = now + 3.0f;
        what = "no search could start now: he looks again";
      }
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} NOTICED THE BODY of {} ({}), state {}: {}", Id(base, c), k.who, how, StName(g.st), what);
    }
  }

  // Round 5 (item 3): our own body sight for every tracked trooper who is not fighting (log 025: s1m3, searching,
  // walked past s1m4's body within 2 m several times; Rare's body sight runs only in his DEFAULT script's reaction).
  // A body Jo left counts when it is within pdz_lab_stealth_body_range_cm (shrunk by the light where it lies, as his
  // sight of Jo is), within 75 degrees of his facing (or within 4 m whatever he faces), and Rare's sight ray from the
  // body to his eye is clear.
  void BodySight(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    float eye[3];
    EyeOf(base, c, g, now, eye);
    const bool facing = now >= g.eye_at && now - g.eye_at < 3.0f;
    const float range = float(REXCVAR_GET(pdz_lab_stealth_body_range_cm));
    for (size_t i = 0; i < g_kills.size() && i < 64; ++i)
    {
      Kill &k = g_kills[i];
      if ((g.bodies_seen >> i) & 1 || k.victim == c || now - k.at < 1.5f)
      {
        continue;
      }
      float body[3] = {k.pos[0], k.pos[1] + 30.0f, k.pos[2]};
      if (Ptr(k.victim) && Load32(base, k.victim + kData) != 0 && !(LoadF(base, k.victim + kHealth) > 0.0f))
      {
        float p[3];
        PosOf(base, k.victim, p); // where he lies now, if it is still his memory
        if (Dist3(p, k.pos) < 600.0f)
        {
          body[0] = p[0];
          body[1] = p[1] + 30.0f;
          body[2] = p[2];
        }
      }
      const float d = Dist3(eye, body);
      if (d > range)
      {
        continue;
      }
      const float scale = lab_dark::On() ? lab_dark::RangeScaleAt(base, body, range) : 1.0f;
      if (d > range * scale)
      {
        continue;
      }
      float cosv = 1.0f;
      if (facing && d > 400.0f)
      {
        const float dl = std::sqrt(g.dir[0] * g.dir[0] + g.dir[1] * g.dir[1] + g.dir[2] * g.dir[2]);
        cosv = dl > 1e-4f ? ((body[0] - eye[0]) * g.dir[0] + (body[1] - eye[1]) * g.dir[1] + (body[2] - eye[2]) * g.dir[2]) /
                                (d * dl)
                          : 1.0f;
        if (cosv < 0.26f)
        {
          continue;
        }
      }
      else if (!facing && d > 800.0f)
      {
        continue; // his facing is unknown: only a body close by
      }
      if (!SightClear(ctx, base, body, eye))
      {
        continue;
      }
      NoticeBody(ctx, base, c, g, int(i), now,
                 fmt::format("his own sight: {:.0f} cm away, light range {:.0f}%, {}", d, 100.0f * scale,
                             facing ? fmt::format("{:.0f} deg off his facing", std::acos(std::clamp(cosv, -1.0f, 1.0f)) * 57.2958f)
                                    : std::string("close by")));
      return; // one body at a time
    }
  }

  float InvSeconds(Inv kind)
  {
    return kind == Inv::Lamp || kind == Inv::Noise ? float(REXCVAR_GET(pdz_lab_stealth_lamp_search_s))
           : kind == Inv::Body  ? float(REXCVAR_GET(pdz_lab_stealth_body_search_s))
           : kind == Inv::Squad ? float(REXCVAR_GET(pdz_lab_stealth_squad_search_s))
                                : 0.0f;
  }

  // His next investigation pad (12 (b)): the first leg is the pad beside the lamp / body (a squad searcher's first leg
  // is chosen like the others, so the squad starts on different pads); then her pad if he glimpsed her; else the best
  // of the pads around the centre: within pdz_lab_stealth_inv_radius_cm, 4 m from his earlier legs and from every
  // other investigator's pad, never within 3 m of where Jo was when it began unless he saw her then, scored for
  // Rare's cover flags, the direction Jo was in, a middle distance (4-11 m), a short walk and a little chance.
  uint32_t PickInvPad(const uint8_t *base, uint32_t c, Guard &g, std::string &why)
  {
    const bool floor = REXCVAR_GET(pdz_lab_stealth_same_floor) && g.inv_floor_set;
    const bool body_rule = floor && g.inv == Inv::Body;
    g.inv_look = false;
    g.stuck_look = false;
    if (g.stuck == 2)
    {
      // Round 7 (item 2): the pad walk made no progress either: one look round where he stands, then he carries on.
      const uint32_t own = Load32(base, c + kPad);
      if (Ptr(own))
      {
        why = "NO-PROGRESS FALLBACK 2: no way to the spot: he looks round where he stands";
        g.inv_look = true;
        g.stuck_look = true;
        return own;
      }
    }
    if (g.inv_leg == 0 && g.inv != Inv::Squad && Ptr(g.inv_centre_pad))
    {
      why = "beside it: he stops and looks";
      g.inv_look = true;
      return g.inv_centre_pad;
    }
    if (Ptr(g.inv_redirect))
    {
      const uint32_t pad = g.inv_redirect;
      g.inv_redirect = 0;
      why = "her pad: he glimpsed her";
      return pad;
    }
    // Round 11 (item 5; log 010 17:00:20: s1m5 looked for 5 s, then walked 7 m off): a lamp or noise check is the
    // look at the spot itself, then at most ONE spot near it.
    const bool spot_check = g.inv == Inv::Lamp || g.inv == Inv::Noise;
    if (spot_check && g.inv_brief)
    {
      why = "a short look under a lamp he found dark: no other spot";
      return 0;
    }
    if (spot_check && g.inv_spots >= 1)
    {
      why = "his one nearby spot is checked";
      return 0;
    }
    if (body_rule && !g.inv_bad.empty() && g.inv_bad.size() <= 2 && g.inv_bad.back() == g.inv_centre_pad)
    {
      // He did not get to the pad beside the body: the next nearest pad of its floor, within 4 m of it.
      uint32_t alt = 0;
      float alt_d = 400.0f;
      for (const auto &[pad, walk] : FloorPads(base, g.inv_centre_pad, g.inv_floor, 900.0f))
      {
        float q[3];
        if (std::find(g.inv_bad.begin(), g.inv_bad.end(), pad) != g.inv_bad.end() || !PadPos(base, pad, q))
        {
          continue;
        }
        const float qx = q[0] - g.inv_centre[0], qz = q[2] - g.inv_centre[2];
        const float d = std::sqrt(qx * qx + qz * qz);
        if (d < alt_d)
        {
          alt = pad;
          alt_d = d;
        }
      }
      if (alt)
      {
        g.inv_centre_pad = alt;
        g.inv_look = true;
        why = "beside it (he did not get to the nearer pad): he stops and looks";
        return alt;
      }
    }
    // Round 6 (item 1 (b) / (c)): a body's finder checks at most pdz_lab_stealth_body_spots spots, on the body's floor
    // and within pdz_lab_stealth_body_spot_cm; with none he looks round at the body again (three looks at most).
    const auto look_again = [&](const char *text) -> uint32_t {
      if (g.inv_looks < 3 && Ptr(g.inv_centre_pad))
      {
        why = text;
        g.inv_look = true;
        return g.inv_centre_pad;
      }
      why = "he has looked round the body three times";
      return 0;
    };
    if (body_rule && g.inv_spots >= std::max(0, int(REXCVAR_GET(pdz_lab_stealth_body_spots))))
    {
      if (g.inv_spots == 0)
      {
        return look_again("pdz_lab_stealth_body_spots 0: he stays at the body and looks round again");
      }
      why = "his spots round the body are checked";
      return 0;
    }
    const float radius = body_rule    ? float(REXCVAR_GET(pdz_lab_stealth_body_spot_cm))
                         : spot_check ? std::max(200.0f, float(REXCVAR_GET(pdz_lab_stealth_spot_near_cm)))
                                      : std::max(400.0f, float(REXCVAR_GET(pdz_lab_stealth_inv_radius_cm)));
    constexpr float kSpread = 400.0f;
    float me[3];
    PosOf(base, c, me);
    float jd[3] = {g.inv_jo[0] - g.inv_centre[0], 0.0f, g.inv_jo[2] - g.inv_centre[2]};
    const float jl = std::sqrt(jd[0] * jd[0] + jd[2] * jd[2]);
    // Every other investigator's current pad.
    std::vector<std::array<float, 3>> taken;
    for (const auto &[other, o] : g_guards)
    {
      float p[3];
      if (other != c && o.st == St::Investigating && PadPos(base, o.inv_pad, p))
      {
        taken.push_back({p[0], p[1], p[2]});
      }
    }
    std::vector<std::pair<uint32_t, int>> cands;
    if (floor)
    {
      // Only pads on the floor of the thing, reached by a walk that stays on that floor (item 1 (b) / (d)).
      for (const auto &[pad, walk] : FloorPads(base, g.inv_centre_pad, g.inv_floor, radius * 1.6f))
      {
        cands.push_back({pad, int(walk)});
      }
    }
    else
    {
      cands = PadsAround(base, g.inv_centre_pad, 6, 260);
    }
    // The directions (from the thing) of the spots he has been to: a new spot lies another way (60 degrees or more).
    std::vector<std::array<float, 2>> dirs;
    if (body_rule)
    {
      for (const uint32_t done : g.inv_done)
      {
        float q[3];
        if (PadPos(base, done, q))
        {
          const float qx = q[0] - g.inv_centre[0], qz = q[2] - g.inv_centre[2];
          const float ql = std::sqrt(qx * qx + qz * qz);
          if (ql > 150.0f)
          {
            dirs.push_back({qx / ql, qz / ql});
          }
        }
      }
    }
    for (int pass = 0; pass < 2; ++pass)
    {
      uint32_t best = 0;
      float best_s = -1e9f, best_d = 0.0f, best_cos = 0.0f;
      bool best_cover = false;
      for (const auto &[pad, hop] : cands)
      {
        float p[3];
        if (!PadPos(base, pad, p))
        {
          continue;
        }
        const float d = Dist3(p, g.inv_centre);
        if (d > radius || std::find(g.inv_done.begin(), g.inv_done.end(), pad) != g.inv_done.end())
        {
          continue;
        }
        if (body_rule)
        {
          const float bx = p[0] - g.inv_centre[0], bz = p[2] - g.inv_centre[2];
          const float bl = std::sqrt(bx * bx + bz * bz);
          if (bl < 250.0f)
          {
            continue; // at the body itself: not another spot
          }
          bool same_way = false;
          for (const auto &dir : dirs)
          {
            same_way = same_way || (bx * dir[0] + bz * dir[1]) / bl > 0.5f;
          }
          if (same_way)
          {
            continue; // (both passes: a second spot the same way is not a second spot)
          }
        }
        if (pass == 0)
        {
          bool near_other = false;
          for (const uint32_t done : g.inv_done)
          {
            float q[3];
            near_other = near_other || (PadPos(base, done, q) && Dist3(p, q) < kSpread);
          }
          for (const auto &t : taken)
          {
            const float q[3] = {t[0], t[1], t[2]};
            near_other = near_other || Dist3(p, q) < kSpread;
          }
          if (near_other || (!g.inv_jo_seen && Dist3(p, g.inv_jo) < 300.0f))
          {
            continue;
          }
        }
        float cosj = 0.0f;
        const float px = p[0] - g.inv_centre[0], pz = p[2] - g.inv_centre[2];
        const float pl = std::sqrt(px * px + pz * pz);
        if (jl > 200.0f && pl > 50.0f)
        {
          cosj = (px * jd[0] + pz * jd[2]) / (pl * jl);
        }
        const bool cover = (Load32(base, pad + kPadFlags) & 0xC) != 0;
        float s = cosj + (cover ? 0.6f : 0.0f) + (d >= 400.0f && d <= 1100.0f ? 0.3f : 0.0f) - 0.0004f * Dist3(me, p) +
                  0.3f * Rand01();
        if (s > best_s)
        {
          best = pad;
          best_s = s;
          best_d = d;
          best_cos = cosj;
          best_cover = cover;
        }
      }
      if (best)
      {
        why = fmt::format("{}{}, {:.0f} deg off the way Jo was{}", best_cover ? "cover pad" : "open pad",
                          pass ? " (every pad 4 m from the others was taken)" : "",
                          jl > 200.0f ? std::acos(std::clamp(best_cos, -1.0f, 1.0f)) * 57.2958f : 0.0f,
                          jl > 200.0f ? "" : " (Jo's direction unknown)");
        (void)best_d;
        return best;
      }
    }
    if (body_rule)
    {
      return look_again(g.inv_spots == 0 ? "no other spot on the body's floor within reach (a walkway, a corridor): he stays at the "
                                           "body and looks round again"
                                         : "no further spot on the body's floor another way: he looks round at the body again");
    }
    why = floor ? "no pad left around it on its floor" : "no pad left around it";
    return 0;
  }

  bool InvLeg(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    std::string why;
    const uint32_t pad = PickInvPad(base, c, g, why);
    if (!Ptr(pad) || !GiveSearch(ctx, base, c, pad))
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} {} investigation: no further leg ({})", Id(base, c), InvName(g.inv),
                    Ptr(pad) ? "the SEARCH task could not be built" : why);
      }
      return false;
    }
    g.inv_pad = pad;
    g.search_pad = pad;
    ++g.inv_leg;
    if (g.inv_look)
    {
      // The first look: pdz_lab_stealth_look_s. Later looks at the body (no other spot): the time left, shared out.
      // Round 11: the look at the spot of a lamp or a noise: 10-15 s (pdz_lab_stealth_spot_look_min_s / _max_s).
      const float look_lo = float(REXCVAR_GET(pdz_lab_stealth_spot_look_min_s));
      const float look_hi = std::max(look_lo, float(REXCVAR_GET(pdz_lab_stealth_spot_look_max_s)));
      const bool spot_look = (g.inv == Inv::Lamp || g.inv == Inv::Noise) && !g.stuck_look;
      const float brief_lo = float(REXCVAR_GET(pdz_lab_dark_lamp_look_min_s));
      const float brief_hi = std::max(brief_lo, float(REXCVAR_GET(pdz_lab_dark_lamp_look_max_s)));
      g.inv_dwell = g.inv_looks == 0 ? (spot_look && g.inv_brief ? brief_lo + (brief_hi - brief_lo) * Rand01() // round 11b (item 7)
                                        : spot_look ? look_lo + (look_hi - look_lo) * Rand01() : float(REXCVAR_GET(pdz_lab_stealth_look_s)))
                                     : std::clamp((g.inv_until - now) / float(std::max(1, 3 - g.inv_looks)), 5.0f, 15.0f);
      ++g.inv_looks;
      if (g.inv == Inv::Body)
      {
        g.inv_body_pad = pad;
      }
    }
    else
    {
      g.inv_dwell = float(REXCVAR_GET(pdz_lab_stealth_inv_dwell_s));
      ++g.inv_spots;
    }
    g.inv_leg_at = now;
    g.inv_arrived = -1e9f;
    g.inv_end_leg = false;
    g.prog_at = now;
    g.prog_moved = false;
    PosOf(base, c, g.prog_ref);
    g.inv_done.push_back(pad);
    if (Logging())
    {
      float p[3] = {}, me[3];
      PadPos(base, pad, p);
      PosOf(base, c, me);
      REXLOG_INFO("pdz_lab_stealth: {} {} investigation leg {} at pad {:08X} ({:.0f} {:.0f} {:.0f}): {}; {:.0f} cm from the "
                  "{}, {:.0f} cm from him, {:.0f} s into it",
                  Id(base, c), InvName(g.inv), g.inv_leg, pad, p[0], p[1], p[2], why, Dist3(p, g.inv_centre),
                  g.inv == Inv::Squad && g.inv_body < 0 ? "lamps" : InvThing(g.inv), Dist3(p, me), now - g.inv_start);
    }
    return true;
  }

  void StartInvestigation(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, Inv kind, const float centre[3], int body,
                          float now, const std::string &why, uint32_t centre_pad)
  {
    if (R8BodyFirstRefuses(base, c, g, kind, why, now))
    {
      return; // (Tue 17 Nov night, fix2, item 3: he finishes his body check first)
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    if (g.st != St::Investigating)
    {
      g.was_wary = g.st == St::Wary || g.was_wary;
    }
    g.inv = kind;
    g.inv_brief = g_next_brief && kind == Inv::Lamp; // round 11b (item 7): the short look at a lamp found dark
    g_next_brief = false;
    std::memcpy(g.inv_centre, centre, sizeof(g.inv_centre));
    if (Ptr(jo))
    {
      PosOf(base, jo, g.inv_jo);
    }
    g.inv_jo_seen = now >= g.last_visible && now - g.last_visible < 2.0f;
    float d = 0.0f;
    g.inv_centre_pad = NearestPadTo(base, Load32(base, c + kPad), centre, d);
    float cp[3];
    if (Ptr(centre_pad) && PadPos(base, centre_pad, cp))
    {
      g.inv_centre_pad = centre_pad; // round 5: the spot chosen for a lamp (below it, in reach, with a view of it)
      d = Dist3(cp, centre);
    }
    // Round 6 (item 1): the floor of this investigation. A body (alone or with the squad): the body's own height, and
    // the pad beside it is the nearest one ON THAT FLOOR (log 062: the nearest pad in 3D was under the walkway). A
    // lamp or a sound: the floor of the pad beside it.
    g.inv_floor_set = false;
    if (REXCVAR_GET(pdz_lab_stealth_same_floor))
    {
      if (body >= 0 && (kind == Inv::Body || kind == Inv::Squad))
      {
        float fd = 0.0f;
        g.inv_centre_pad = FloorPadAt(base, Load32(base, c + kPad), centre, kind == Inv::Body ? 600.0f : 1500.0f, fd);
        d = g.inv_centre_pad ? fd : 0.0f;
        g.inv_floor = centre[1];
        g.inv_floor_set = true;
      }
      else
      {
        if (kind == Inv::Squad && !Ptr(centre_pad))
        {
          // A lamp spree: the floor under the lamps (the nearest pad in 3D to a ceiling lamp can be on the floor above).
          uint32_t below = 0;
          float below_d = 1e9f;
          for (const auto &[pad, hop] : PadsAround(base, Load32(base, c + kPad), 40, 900))
          {
            float q[3];
            if (!PadPos(base, pad, q) || q[1] > centre[1] + 50.0f || centre[1] - q[1] > 500.0f)
            {
              continue;
            }
            const float qx = q[0] - centre[0], qz = q[2] - centre[2];
            const float qd = std::sqrt(qx * qx + qz * qz);
            if (qd <= 800.0f && qd < below_d)
            {
              below = pad;
              below_d = qd;
            }
          }
          if (below)
          {
            g.inv_centre_pad = below;
            d = below_d;
          }
        }
        if (PadPos(base, g.inv_centre_pad, cp))
        {
          g.inv_floor = cp[1];
          g.inv_floor_set = true;
        }
      }
    }
    g.inv_walk = 0;
    g.stuck = 0;
    g.stuck_look = false;
    g.prog_at = now;
    g.prog_moved = false;
    PosOf(base, c, g.prog_ref);
    g.inv_walk_wait = g.inv_walk_cut = false;
    g.inv_bad.clear();
    g.inv_look = false;
    g.inv_looks = g.inv_spots = 0;
    g.inv_body_pad = 0;
    g.inv_start = now;
    g.inv_until = now + std::max(5.0f, InvSeconds(kind));
    g.inv_leg = 0;
    g.inv_fails = 0;
    g.inv_redirect = 0;
    g.inv_end_leg = false;
    g.inv_done.clear();
    g.inv_body = body;
    g.need_line = false;
    if (body >= 0 && size_t(body) < g_kills.size() && kind == Inv::Body)
    {
      g_kills[body].finder = c;
    }
    if (kind != Inv::Squad)
    {
      AreaNote(base, c, InvThing(kind), kind == Inv::Body, centre, now); // round 7: the escalation tiers
    }
    Set(base, c, g, St::Investigating, now,
        fmt::format("{}: investigates the {} at ({:.0f} {:.0f} {:.0f}) for {:.0f} s; the pad beside it {:08X} ({:.0f} cm "
                    "from it); Jo was {:.0f} cm from it{}",
                    why, kind == Inv::Squad && body < 0 ? "lamps" : InvThing(kind), centre[0], centre[1], centre[2],
                    g.inv_until - now,
                    g.inv_centre_pad, d, Dist3(g.inv_jo, centre), g.inv_jo_seen ? " (he saw her then)" : ""));
    if (kind == Inv::Body && body >= 0 && size_t(body) < g_kills.size() && REXCVAR_GET(pdz_lab_stealth_body_walk) &&
        g.st == St::Investigating)
    {
      // Round 6 (item 1 (a)): first to the body itself, with Rare's own walk-to-body task.
      float where[3] = {};
      const char *how = "";
      const uint32_t corpse = CorpseOf(ctx, base, c, g_kills[size_t(body)], where, how);
      const uint32_t task = corpse ? WalkToBody(ctx, base, c, corpse) : 0;
      if (task)
      {
        g.inv_walk = task;
        g.inv_walk_at = now;
        g.inv_until = std::max(g.inv_until, now + 60.0f); // the walk's time; the look and the spots start at its end
        g.inv_leg_at = now;
        if (Logging())
        {
          float me[3];
          PosOf(base, c, me);
          REXLOG_INFO("pdz_lab_stealth: {} WALKS TO THE BODY of {} (Rare's walk-to-body task {:08X}; {} {:08X} at ({:.0f} {:.0f} "
                      "{:.0f}), {:.0f} cm from him): {}",
                      Id(base, c), g_kills[size_t(body)].who, task, how, corpse, where[0], where[1], where[2], Dist3(me, where),
                      StackText(base, c));
        }
        return;
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} no walk to the body itself ({}): he goes to the pad beside it on its floor", Id(base, c),
                    corpse ? "Rare's task could not be queued now" : "no corpse object of this kill within 5 m of where he fell");
      }
      g.inv_walk_wait = true; // tried again while his first leg runs (InvTick)
    }
    if (g.inv_brief && REXCVAR_GET(pdz_lab_dark_lamp_walk) && g.st == St::Investigating)
    {
      // Round 11c (item 3; log 005 19:17:31: s1m5 found a lamp dark and went to it with Rare's SEARCH, gun up, as if
      // he had seen it shot): a lamp he FINDS dark: his ordinary walk to the spot under it; the look starts when
      // the walk is over (InvTick, as after the walk to a body).
      const uint32_t task = WalkToPad(ctx, base, c, g.inv_centre_pad);
      if (task)
      {
        g.inv_walk = task;
        g.inv_walk_at = now;
        g.inv_until = std::max(g.inv_until, now + 60.0f);
        g.inv_leg_at = now;
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} WALKS OVER to the lamp he found dark (Rare's go-to-pad task {:08X}, pad {:08X}: his "
                      "ordinary walk, no search): {}",
                      Id(base, c), task, g.inv_centre_pad, StackText(base, c));
        }
        return;
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} no plain walk to the lamp he found dark (his task stack is busy): Rare's search "
                    "walks him there",
                    Id(base, c));
      }
    }
    if (!InvLeg(ctx, base, c, g, now))
    {
      g.inv = Inv::None;
      Set(base, c, g, g.was_wary ? St::Wary : St::Unaware, now, "no pad to search: the investigation is over");
    }
  }

  // Rare's own alarm run (12 (c)): his gate, then the task handed to his scheduler. False with the reason.
  void SciSay(const uint8_t *base, Guard &g, const char *cue, uint32_t takes, float len, float now, const char *what,
              bool sighting);
  // Round 11f (item 4): LOUD is permanent, so the player always hears it coming: before any alarm run starts, he
  // shouts first, as a warning line that always plays (troopers: WARN_TEAM takes 1 and 2, "Sound the alarm!" /
  // "Activate the alarm!", "Sound the alert!" / "Trigger the alarm!", "Alert! Sound the alert!" / "Hit the alarm!";
  // scientists: WARN_TEAM, "Someone call security!"), hesitates pdz_lab_stealth_alarm_announce_s from the line's
  // real start (WarnTick), and then the run starts (AlarmRunTick): Rare's own alarm task, which ends as before when
  // he is killed or downed on the way.
  void StartAnnounce(const uint8_t *base, uint32_t c, Guard &g, float now, const char *why)
  {
    if (g.run_pending)
    {
      return;
    }
    g.run_pending = true;
    g.run_from = now;
    g.run_at = -1e9f;
    g.run_why = why;
    const char *line = "no line of his (not a guard or scientist voice): he runs now";
    if (LayerGuard(g)) // (the Lab: troopers 17-19; any guard voice says his own set's WARN_TEAM)
    {
      QueueWarn(g, EventIndex(base, "WARN_TEAM"), now, "alarm shout", 0x3);
      line = "he shouts WARN_TEAM (\"Sound the alarm!\" kind) first";
    }
    else if (g.voice == 15 || g.voice == 16)
    {
      SciSay(base, g, "WARN_TEAM", 0xF, 2.4f, now, "alarm shout", false);
      line = "he shouts WARN_TEAM (\"Someone call security!\" kind) first";
    }
    if (!g.warn_ev)
    {
      g.run_at = now;
    }
    REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN asked for ({}), state {}: {}", Id(base, c), why, StName(g.st), line);
  }

  bool AlarmRunNow(PPCContext &ctx, uint8_t *base, uint32_t c, std::string &refusal, bool gate = true);
  bool AlarmRun(PPCContext &ctx, uint8_t *base, uint32_t c, std::string &refusal)
  {
    Guard *g = Find(c);
    const float now = Clock(base);
    if (g && (g->run_pending || g->run_committed))
    {
      return true; // (already announced: it starts by itself, and is kept)
    }
    if (!REXCVAR_GET(pdz_lab_stealth_alarm_announce) || !g || (now >= g->announced_at && now - g->announced_at < 10.0f))
    {
      return AlarmRunNow(ctx, base, c, refusal);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c;
      sub_821DF340(frame.ctx, base);
      if ((frame.ctx.r3.u32 & 0xFF) == 0)
      {
        refusal = "Rare's gate: a squad mate already runs for it, the alarm is on, or a target is within 2.8 m";
        return false;
      }
    }
    {
      // Round 11i (item 2): the check first: Rare's builder must make the task; that task is kept for him.
      uint32_t ready = 0;
      g_alarm_run_ours = true;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = c;
        sub_821DF4A0(frame.ctx, base);
        ready = frame.ctx.r3.u32;
      }
      g_alarm_run_ours = false;
      if (!Ptr(ready))
      {
        refusal = "no alarm task (no alarm in reach, or no route to one): no shout, no run";
        REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN CHECK (the stealth rules, state {}): Rare's builder makes no alarm task (no "
                    "alarm in reach, or no route to one): NO SHOUT, no run",
                    Id(base, c), StName(g->st));
        return false;
      }
      g->run_ready = ready;
      REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN CHECK (the stealth rules, state {}): Rare's builder made the alarm task {:08X}: "
                  "an alarm is in reach: he shouts, hesitates, then gets that task",
                  Id(base, c), StName(g->st), ready);
    }
    StartAnnounce(base, c, *g, now, "the stealth rules");
    return true;
  }
  // His announced alarm run starts (After, every frame).
  void AlarmRunTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (!g.run_pending)
    {
      return;
    }
    if (g.dead || g.st == St::Dead || !(LoadF(base, c + kHealth) > 0.0f) || now < g.run_from)
    {
      g.run_pending = false;
      return;
    }
    if (g.run_at < -1e8f)
    {
      if (g.warn_ev && now - g.run_from < 9.0f)
      {
        return; // his shout has not started yet
      }
      g.run_at = now;
    }
    if (now < g.run_at)
    {
      return;
    }
    g.run_pending = false;
    g.run_go_until = g.run_committed ? -1e9f : now + 6.0f; // (not committed: Rare's own next request is let through)
    std::string refusal;
    bool ok = false;
    if (Ptr(g.run_ready))
    {
      // Round 11i: the task Rare's builder made at the check is handed over now (the same task, the same panel).
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c + kScheduler;
      frame.ctx.r4.u64 = g.run_ready;
      sub_821712D8(frame.ctx, base);
      g.run_task = g.run_ready;
      g.run_ready = 0;
      ok = true;
    }
    else
    {
      ok = AlarmRunNow(ctx, base, c, refusal, !g.run_committed); // (committed: Rare's gate is not asked)
    }
    g.run_give_at = now;
    g.run_gives = 1;
    REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN {} ({}; asked for {:.1f} s ago, his shout {}), state {}", Id(base, c),
                ok ? "STARTS NOW" : "not started by us: " + refusal + " (Rare's own request is let through for 6 s)", g.run_why,
                now - g.run_from,
                now >= g.announced_at && now - g.announced_at < 10.0f ? fmt::format("started {:.1f} s ago", now - g.announced_at)
                                                                     : std::string("did not play"),
                StName(g.st));
    if (ok && g.st == St::Investigating)
    {
      Set(base, c, g, St::Wary, now, "runs for the alarm");
    }
  }
  bool AlarmRunNow(PPCContext &ctx, uint8_t *base, uint32_t c, std::string &refusal, bool gate)
  {
    if (gate)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c;
      sub_821DF340(frame.ctx, base);
      if ((frame.ctx.r3.u32 & 0xFF) == 0)
      {
        refusal = "Rare's gate: a squad mate already runs for it, the alarm is on, or a target is within 2.8 m";
        return false;
      }
    }
    uint32_t task = 0;
    {
      g_alarm_run_ours = true;
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c;
      sub_821DF4A0(frame.ctx, base);
      task = frame.ctx.r3.u32;
      g_alarm_run_ours = false;
    }
    if (!Ptr(task))
    {
      refusal = "no alarm task (no alarm in reach, or pdz_test_stealth_no_alarm_run)";
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c + kScheduler;
    frame.ctx.r4.u64 = task;
    sub_821712D8(frame.ctx, base);
    R7Wrote(base, c, "a task handed over in place of his top task"); // (Tue 17 Nov: the write record)
    return true;
  }

  // Round 11h (item 1; log 015 21:16:48.9-49.4: s1m5 shouted "Sound the alarm!", his run started, and 0.4 s later it
  // was gone: handing him the alarm task had ended his COMBAT script, our own state machine read that as "combat
  // ended: hunting" and gave him a search, which replaced the alarm task). AN ANNOUNCED ALARM RUN IS A COMMITMENT.
  // From the real start of his shout until the alarm is raised or he dies:
  //   - nothing of ours is given to him (After returns early for him; he is not "calm" for the lamp and squad rules);
  //   - Rare cannot take him off it by sight or by a stimulus: his sight tests of Jo answer "not seen" and his
  //     stimulus word is emptied (he is hit: he keeps running);
  //   - when his alarm task (class 8209339C) is no longer on his task stack and no alarm is being held, Rare's
  //     alarm-run builder makes it again and it is handed back (Rare's builder picks the panel as before), at most
  //     every 0.7 s, six times; three builds in a row with no task mean no route: nothing is raised.
  bool RunTaskLive(const uint8_t *base, uint32_t c, uint32_t task)
  {
    const int depth = StackDepth(base, c);
    for (int i = 0; i < depth; ++i)
    {
      const uint32_t t = Load32(base, c + kScheduler + kStackEntry * uint32_t(i));
      if (Ptr(t) && (t == task || Load32(base, t) == 0x8209339C))
      {
        return true;
      }
    }
    return StackWaiting(base, c) > 0 || lab_cut::AlarmRunning(base, c); // (round 11k: or it ran this half second)
  }
  void RunKeepTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    const auto end = [&](const std::string &how, bool wary) {
      REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN ended: {} ({:.1f} s after his shout, the task handed to him {} time(s))",
                  Id(base, c), how, now - g.run_commit_at, g.run_gives);
      g.run_committed = false;
      g.run_pending = false;
      g.run_ready = 0;
      if (wary)
      {
        g.run_block_until = now + 60.0f; // (round 11i: no new shout for a run that found no route)
      }
      if (wary && g.st != St::Dead && !g.dead)
      {
        Set(base, c, g, St::Wary, now, "his alarm run is over without an alarm: wary");
      }
    };
    if (g.dead || g.st == St::Dead || !(LoadF(base, c + kHealth) > 0.0f))
    {
      end("the runner was killed: no alarm from him", false);
      return;
    }
    if (AlarmOn(base))
    {
      end("the alarm was raised", false);
      return;
    }
    if (now < g.run_commit_at || now - g.run_commit_at > 45.0f)
    {
      end("given up after 45 s: he did not get to a panel", true);
      return;
    }
    if (g.run_pending || g_alarm_deferred)
    {
      return; // he hesitates, or he has used the panel and the alarm is being held
    }
    if (RunTaskLive(base, c, g.run_task) || now - g.run_give_at < 0.7f)
    {
      return;
    }
    if (g.run_gives >= 6)
    {
      end("no route: his alarm task was dropped six times, nothing is raised", true);
      return;
    }
    const std::string why = fmt::format("{}; state {}; {}",
                                        g.combat_script ? "Rare's COMBAT took over" : now - g.search_seen < 0.5f ? "a search took over"
                                                                                                                : "his alarm task ended or was replaced",
                                        StName(g.st), StackText(base, c));
    std::string refusal;
    const bool ok = AlarmRunNow(ctx, base, c, refusal, false);
    g.run_give_at = now;
    if (ok)
    {
      ++g.run_gives;
      g.run_fails = 0;
      REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN KEPT: Rare dropped his alarm task ({}), given back (hand-over {} of 6)",
                  Id(base, c), why, g.run_gives);
    }
    else if (++g.run_fails >= 3)
    {
      end("no route: Rare's alarm-run builder made no task three times (" + refusal + "): nothing is raised", true);
    }
  }

  // He found nothing at the body: his squad mates within pdz_lab_stealth_call_radius_cm come and search around it
  // with him. False when nobody can come.
  bool CallSquad(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, int body, float now, const std::string &why)
  {
    const Kill *k = body >= 0 && size_t(body) < g_kills.size() ? &g_kills[body] : nullptr;
    float centre[3];
    std::memcpy(centre, k ? k->pos : g.inv_centre, sizeof(centre));
    g.squad = SquadOf(ctx, base, c);
    const float radius = float(REXCVAR_GET(pdz_lab_stealth_call_radius_cm));
    // Round 11 (item 5): only guards who can reach the body are sent: within the radius, on its floor, with a walking
    // route on that floor of at most 1.5 x the radius. The other calm squad mates stay at their posts, wary. The same
    // test for the caller himself: one who cannot reach it reports it and stays where he is.
    const bool reach_rule = REXCVAR_GET(pdz_lab_stealth_squad_reach);
    const auto reach_no = [&](uint32_t who, float d, std::string &text) -> bool {
      if (!reach_rule)
      {
        return false;
      }
      float walk = 0.0f;
      float wp[3];
      PosOf(base, who, wp);
      if (std::fabs(wp[1] - centre[1]) > 150.0f)
      {
        text = fmt::format("on another floor ({:+.0f} cm): he stays at his post, wary", wp[1] - centre[1]);
        return true;
      }
      if (!CanReachOnFloor(base, who, centre, radius * 1.5f, walk))
      {
        text = fmt::format("no walking route to it on its floor within {:.0f} cm: he stays at his post, wary", radius * 1.5f);
        return true;
      }
      (void)d;
      return false;
    };
    std::vector<uint32_t> mates, stay;
    std::string list, left;
    for (auto &[other, o] : g_guards)
    {
      if (other == c || !g.squad || o.squad != g.squad || o.scientist || o.voice < 17 || o.voice > 19)
      {
        continue;
      }
      float p[3];
      PosOf(base, other, p);
      const float d = Dist3(p, centre);
      const bool calm_mate = o.st == St::Unaware || o.st == St::Wary;
      std::string reach_text;
      const char *no = o.st == St::Dead || o.dead || !(LoadF(base, other + kHealth) > 0.0f) ? "dead"
                       : (Load32(base, other + kConfig) & kDormant)                       ? "dormant"
                       : !calm_mate                                                        ? StName(o.st)
                       : d > radius ? (reach_rule ? "too far: he stays at his post, wary" : "too far")
                       : reach_no(other, d, reach_text) ? reach_text.c_str()
                                                        : nullptr;
      if (no)
      {
        left += fmt::format(" {} ({}, {:.0f} cm)", Id(base, other), no, d);
        if (reach_rule && calm_mate && !o.dead && o.st != St::Dead && LoadF(base, other + kHealth) > 0.0f &&
            !(Load32(base, other + kConfig) & kDormant))
        {
          stay.push_back(other);
        }
        continue;
      }
      mates.push_back(other);
      list += fmt::format(" {} ({:.0f} cm)", Id(base, other), d);
    }
    if (!mates.empty())
    {
      for (const uint32_t s : stay)
      {
        Guard &o = g_guards[s];
        if (o.st == St::Unaware)
        {
          Set(base, s, o, St::Wary, now, fmt::format("{} called the squad to a body he cannot reach: he stays at his post, wary", Id(base, c)));
        }
      }
    }
    if (mates.empty())
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} body decision: {}; nobody of his squad {:08X} can come (within {:.0f} cm and calm):{}",
                    Id(base, c), why, g.squad, radius, left.empty() ? std::string(" no squad mates tracked") : left);
      }
      return false;
    }
    // The call: Rare's WARN_TEAM take 3 only ("Get the hell over here! Hurry!" / "...quick!" / "...now!"; its other
    // takes ask for the alarm), said by CallLine() and retried for 4 s if the budget or Rare's speech drops it.
    // Round 6 (item 2 (c)): Rare's SEE_BODY_TEAM first ("Got a man down here!", "Sir, I found a body."...), a priority
    // line (PrioLine); WARN_TEAM take 3 is its fallback.
    const bool body_lines = REXCVAR_GET(pdz_lab_stealth_body_lines);
    if (body_lines)
    {
      g.prio = 2;
      g.prio_from = now;
      g.prio_until = now + float(REXCVAR_GET(pdz_lab_stealth_body_line_s));
      g.prio_try = g.prio_queued = -1e9f;
    }
    else
    {
      g.call_until = now + 4.0f;
      g.call_try = -1e9f;
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} body decision: {}: CALLS HIS SQUAD {:08X} ({}): coming:{}; not coming:{}", Id(base, c), why,
                  g.squad, body_lines ? "SEE_BODY_TEAM \"Got a man down here!\", else WARN_TEAM" : "WARN_TEAM \"Get the hell over here!\"",
                  list, left.empty() ? std::string(" nobody") : left);
    }
    const std::string who = k ? k->who : std::string("?");
    float self[3];
    PosOf(base, c, self);
    std::string self_no;
    if (Dist3(self, centre) > radius && reach_rule)
    {
      self_no = "too far";
    }
    if (!self_no.empty() || reach_no(c, Dist3(self, centre), self_no))
    {
      // The caller cannot reach it himself: he has reported it and stays where he is.
      Set(base, c, g, St::Wary, now,
          fmt::format("he called his squad to the body but cannot reach it himself ({:.0f} cm, {}): he stays at his post, wary",
                      Dist3(self, centre), self_no));
    }
    else
    {
      StartInvestigation(ctx, base, c, g, Inv::Squad, centre, body, now, "he called his squad: they search around the body");
    }
    for (const uint32_t m : mates)
    {
      Guard &o = g_guards[m];
      StartInvestigation(ctx, base, m, o, Inv::Squad, centre, body, now,
                         fmt::format("{} called him to the body of {}", Id(base, c), who));
    }
    return true;
  }

  // 12 (c): the body investigation is over and he found nothing: the alarm or his squad.
  void Escalate(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, int body, float now, const std::string &done)
  {
    Kill *k = body >= 0 && size_t(body) < g_kills.size() ? &g_kills[body] : nullptr;
    if (k && k->finder == c)
    {
      k->finder = 0;
    }
    if (k && k->escalated)
    {
      Set(base, c, g, St::Wary, now, done + ": the body was already reported: back to his post, wary");
      R3EndLine(ctx, base, c, g, now, "body check over (already reported)"); // round 3 (L)
      return;
    }
    const bool alarm_user = g.role == kRoleAlarmUser;
    const bool alarm = AlarmOn(base);
    const double chance = std::clamp(double(REXCVAR_GET(pdz_lab_stealth_body_alarm_chance)), 0.0, 1.0);
    bool run = false;
    std::string why;
    if (alarm)
    {
      why = "the alarm already sounds";
    }
    else if (alarm_user)
    {
      run = true;
      why = "he is an ALARM_USER (Rare's role) and the alarm is off";
    }
    else if (chance > 0.0)
    {
      const float roll = Rand01();
      run = roll < chance;
      why = fmt::format("not an alarm user; pdz_lab_stealth_body_alarm_chance {:.2f}, roll {:.2f}: {}", chance, roll,
                        run ? "he runs for the alarm anyway" : "no");
    }
    else
    {
      why = "not an alarm user (role " + std::to_string(int(g.role)) + "; pdz_lab_stealth_body_alarm_chance 0)";
    }
    if (run)
    {
      std::string refusal;
      if (AlarmRun(ctx, base, c, refusal))
      {
        if (k)
        {
          k->escalated = true;
        }
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} body decision: {}: RUNS FOR THE ALARM (Rare's alarm run)", Id(base, c), why);
        }
        Set(base, c, g, St::Wary, now, done + ": runs for the alarm");
        return;
      }
      why += "; Rare's alarm run refused (" + refusal + ")";
    }
    if (CallSquad(ctx, base, c, g, body, now, why))
    {
      if (k)
      {
        k->escalated = true;
      }
      return;
    }
    Set(base, c, g, St::Wary, now, done + ": nobody to call: back to his post, wary");
    R3EndLine(ctx, base, c, g, now, "body check over (nobody to call)"); // round 3 (L)
  }

  // The look is over (12 (b) end / (c)).
  void FinishInvestigation(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const char *why)
  {
    const Inv kind = g.inv;
    const int body = g.inv_body;
    const std::string done = fmt::format("{} investigation over ({}) after {:.0f} s, {} leg(s), Jo not found", InvName(kind), why,
                                         now - g.inv_start, g.inv_leg);
    if (kind == Inv::Body)
    {
      Escalate(ctx, base, c, g, body, now, done);
      return;
    }
    if (kind == Inv::Lamp && g.inv_brief)
    {
      // Round 11b (item 7): a lamp he found dark: back to what he was doing, calm or wary as before; no "never mind".
      g.inv_brief = false;
      Set(base, c, g, g.inv_from_wary ? St::Wary : St::Unaware, now,
          done + (g.inv_from_wary ? ": a lamp he found dark: back to his post, wary as before"
                                  : ": a lamp he found dark: back to what he was doing, calm"));
      R3EndLine(ctx, base, c, g, now, "lamp check over (a lamp he found dark)"); // round 3 (L): now he says one
      return;
    }
    Set(base, c, g, St::Wary, now, done + ": back to his post, wary");
    if (REXCVAR_GET(pdz_stealth_end_line) && RarePlus() && (kind == Inv::Lamp || kind == Inv::Noise))
    {
      R3EndLine(ctx, base, c, g, now, kind == Inv::Lamp ? "lamp check over" : "noise check over"); // round 3 (L)
    }
    else if (kind == Inv::Lamp || kind == Inv::Noise)
    {
      g.nevermind_until = now + 5.0f; // said by NeverMind() from the state machine, retried while the voice budget is busy
      g.nevermind_try = -1e9f;
    }
  }

  // The squad call to a body (CallSquad), once, retried every 0.5 s while it is due.
  void CallLine(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (now >= g.call_until || now - g.call_try < 0.5f)
    {
      return;
    }
    g.call_try = now;
    const uint32_t ev = EventIndex(base, "WARN_TEAM");
    g.mask_ev = ev;
    g.mask_takes = 0x4; // take 3
    g.mask_at = now;
    if (ev && Say(ctx, base, c, ev, false, "calls his squad to the body"))
    {
      g.call_until = -1e9f;
      return;
    }
    if (now + 0.5f >= g.call_until && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} squad call not said: the voice budget or Rare's speech refused it for 4 s", Id(base, c));
    }
  }

  // Round 6 (item 2): a body line with priority (log 062: every SEE_BODY_SOLO was refused, "another guard line is
  // playing" or "Rare's scripted speech is playing"). While one is due, every other guard line that is not part of a
  // fight is held (the say-event hook), so the line playing ends and this one is next; it is tried every quarter of
  // a second for pdz_lab_stealth_body_line_s, waits for Rare's scripted speech and is tried again after it. Asked
  // for with Rare's "skip the speaker's gap" argument. Kind 1: SEE_BODY_SOLO when he notices the body (the cue name
  // is set to the _SOLO one: Rare plays _TEAM for a guard with squad mates), takes 1-7 ("He's dead!", "He been
  // shot?", "Somebody killed him!"...; 8-13 are jokes). Kind 2: his squad call: SEE_BODY_TEAM takes 1-5 ("Sir, I found
  // a body.", "Body on the ground.", "Got a man down here!"...), and for the last part of the time WARN_TEAM take 3
  // (round 4's call). The play hook logs the real start and ends it.
  bool g_prio_saying = false;
  bool PrioWaiting(float now)
  {
    for (const auto &[c, o] : g_guards)
    {
      if (o.prio && now < o.prio_until && o.st != St::Dead && !o.dead)
      {
        return true;
      }
    }
    return false;
  }
  void PrioLine(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (g.st == St::Dead || g.dead || g.st == St::Combat || g.st == St::Lost || !LayerVoice(g.voice))
    {
      g.prio = 0;
      return;
    }
    const float line_s = float(REXCVAR_GET(pdz_lab_stealth_body_line_s));
    if (now >= g.prio_until)
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} body line ({}) NOT PLAYED: other speech or Rare's speech rules held it for {:.1f} s",
                    Id(base, c), g.prio == 1 ? "SEE_BODY_SOLO: he noticed a body" : "his squad call", now - g.prio_from);
      }
      g.prio = 0;
      return;
    }
    if (now - g.prio_try < 0.25f || (g.prio_queued > -1e8f && now - g.prio_queued < 2.5f))
    {
      return; // (queued: its real start is awaited; the play hook ends it)
    }
    g.prio_try = now;
    const bool call = g.prio == 2;
    const bool warn = call && now - g.prio_from > 0.6f * line_s;
    const uint32_t ev = warn ? EventIndex(base, "WARN_TEAM") : kEvSeeBody;
    if (!ev)
    {
      return;
    }
    if (g.prio_queued > -1e8f)
    {
      g.said_at.erase(ev); // queued 2.5 s ago and never started: asked for again
      g.prio_queued = -1e9f;
    }
    g.mask_ev = ev;
    g.mask_takes = warn ? 0x4u : 0x7Fu;
    g.mask_at = now;
    if (!warn)
    {
      ArmSwap(base, c, g.voice, call ? "SEE_BODY_TEAM" : "SEE_BODY_SOLO", now);
      g.bodycall_at = call ? now : -1e9f;
    }
    g_prio_saying = true;
    g_say_force = true;
    const bool ok = Say(ctx, base, c, ev, false, call ? "calls his squad to the body" : "sees a body");
    g_say_force = false;
    g_prio_saying = false;
    if (ok)
    {
      g.prio_queued = now;
    }
    else if (!warn)
    {
      DisarmSwap();
    }
  }

  // Rare's recorded but never raised "never mind" (asked for as HEAR_UNKNOWN_TEAM, the cue name swapped), once.
  void NeverMind(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (now >= g.nevermind_until || now - g.nevermind_try < 0.5f)
    {
      return;
    }
    g.nevermind_try = now;
    const uint32_t ev = EventIndex(base, "HEAR_UNKNOWN_TEAM");
    g.nevermind_at = now;
    ArmSwap(base, c, g.voice, "HEAR_NOISE_NOT_INVESTIGATE_TEAM", now);
    const uint32_t asked = g_admit_serial;
    if (ev && Say(ctx, base, c, ev, false, "never mind"))
    {
      g.nevermind_until = -1e9f;
      return;
    }
    DisarmSwap();
    if (!ev || g_admit_serial != asked)
    {
      // Round 11b (item 2; log 002 18:09:05-09: s1m4 asked five times in 4 s, Rare's speech refused each): the line
      // reached Rare's speech once and was refused: dropped, never asked for again. (While our own one-voice rule
      // holds it back nothing is asked of Rare; that wait is at most 5 s.)
      g.nevermind_until = -1e9f;
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} \"never mind\" not said: asked once, Rare's speech refused it: dropped (no retry)",
                    Id(base, c));
      }
      return;
    }
    if (now + 0.5f >= g.nevermind_until && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} \"never mind\" not said: other lines were playing for 5 s (never asked of Rare's "
                  "speech)",
                  Id(base, c));
    }
  }

  void NoticeBody(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, int index, float now, const std::string &how);

  // Round 5 (item 2): a kill of Jo's seen by guards who did not see her: 0.4 s after the first witness (Rare's sight
  // passes of the others have run by then) the nearest one who may (a trooper who never saw or fought her, not busy
  // with another body) searches round the spot alone, the body rules from there (squad call or alarm after his
  // search); the other witnesses are alert; a witness who already hunts her takes the spot as his next leg.
  void DeathTick(PPCContext &ctx, uint8_t *base, float now)
  {
    for (size_t i = 0; i < g_kills.size(); ++i)
    {
      Kill &k = g_kills[i];
      if (k.witness_done || k.witnesses.empty() || now - k.witness_at < 0.4f)
      {
        continue;
      }
      k.witness_done = true;
      uint32_t finder = 0;
      float best = 1e9f;
      const bool rules = REXCVAR_GET(pdz_lab_stealth_body_rules) && REXCVAR_GET(pdz_lab_stealth_investigation);
      for (const uint32_t w : k.witnesses)
      {
        Guard *o = Find(w);
        if (!o || o->st == St::Dead || o->dead || o->scientist || o->seen_jo || o->fought || o->combat_script != 0 ||
            o->st == St::Combat || o->st == St::Lost || Hunter(*o) ||
            (o->st == St::Investigating && (o->inv == Inv::Body || o->inv == Inv::Squad)))
        {
          continue;
        }
        float p[3];
        PosOf(base, w, p);
        const float d = Dist3(p, k.pos);
        if (d < best)
        {
          best = d;
          finder = w;
        }
      }
      if (!rules || k.finder || k.escalated)
      {
        finder = 0;
      }
      for (const uint32_t w : k.witnesses)
      {
        Guard *o = Find(w);
        if (!o || o->st == St::Dead || o->dead)
        {
          continue;
        }
        if (i < 64)
        {
          o->bodies_seen |= uint64_t(1) << i;
        }
        if (w == finder)
        {
          NoticeBody(ctx, base, w, *o, int(i), now,
                     fmt::format("he saw him die without seeing Jo, the nearest of {} witness(es), {:.0f} cm",
                                 k.witnesses.size(), best));
        }
        else if (Hunter(*o))
        {
          float d = 0.0f;
          const uint32_t pad = NearestPadTo(base, Load32(base, w + kPad), k.pos, d);
          if (Ptr(pad))
          {
            o->redirect_pad = pad;
            o->redirect_at = now;
          }
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} (hunting) saw {} die without seeing Jo: his next leg is the spot (pad {:08X})",
                        Id(base, w), k.who, pad);
          }
        }
        else
        {
          Alert(ctx, base, w, *o, now,
                fmt::format("saw {} die without seeing Jo{}", k.who,
                            finder ? fmt::format("; {} searches round the spot", Id(base, finder)) : std::string()));
        }
      }
    }
  }

  // The St::Investigating step of the state machine.
  void InvTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search,
               bool glimpse)
  {
    if (in_combat)
    {
      Set(base, c, g, St::Combat, now, fmt::format("Rare's combat during his {} investigation", InvName(g.inv)));
      return;
    }
    if (glimpse && g.meter >= Notice(g) && now - g.redirect_at > 5.0f)
    {
      g.redirect_at = now;
      const uint32_t pad = Load32(base, jo + kPad);
      float jp[3] = {};
      const bool on_floor = !(REXCVAR_GET(pdz_lab_stealth_same_floor) && g.inv_floor_set) ||
                            (PadPos(base, pad, jp) && std::fabs(jp[1] - g.inv_floor) <= kFloorCm &&
                             Dist3(jp, g.inv_centre) <= std::max(400.0f, float(REXCVAR_GET(pdz_lab_stealth_inv_radius_cm))));
      if (Ptr(pad) && pad != g.inv_pad && !on_floor)
      {
        // Round 6 (item 1: log 062 16:56:06, his second leg was her pad 41 m from the body): a glimpse of her on
        // another floor or far from the thing does not take him there; his meter decides.
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} glimpsed Jo during his {} investigation (meter {:.0f}), but her pad {:08X} is on "
                      "another floor or over {:.0f} cm from the {}: he stays with it",
                      Id(base, c), InvName(g.inv), g.meter, pad, std::max(400.0f, float(REXCVAR_GET(pdz_lab_stealth_inv_radius_cm))),
                      InvThing(g.inv));
        }
        WarnGlimpse(ctx, base, c, g, now, "glimpse while investigating (sight takes)");
      }
      else if (Ptr(pad) && pad != g.inv_pad)
      {
        g.inv_redirect = pad;
        g.inv_end_leg = true;
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} glimpsed Jo during his {} investigation (meter {:.0f}): his leg ends, next leg at "
                      "her pad {:08X}",
                      Id(base, c), InvName(g.inv), g.meter, pad);
        }
        WarnGlimpse(ctx, base, c, g, now, "glimpse while investigating (sight takes)");
      }
    }
    if (now >= g.inv_until)
    {
      g.inv_end_leg = true;
    }
    if (g.inv_walk)
    {
      // Round 6 (item 1 (a)): Rare's walk to the body runs until its task leaves his stack (60 s at most).
      const bool live = TaskLive(base, c, g.inv_walk);
      const float t = now - g.inv_walk_at;
      float me[3];
      PosOf(base, c, me);
      // Round 7 (item 2; log 048 06:44:57, s3g7 walked on the spot for 60 s, 10 m from the body): no 1 m of progress
      // in pdz_lab_stealth_stuck_s while he is still over 4 m from it: the walk is given up for the pad beside it.
      const float stuck_s = float(REXCVAR_GET(pdz_lab_stealth_stuck_s));
      const float bx = me[0] - g.inv_centre[0], bz = me[2] - g.inv_centre[2];
      const bool no_progress = live && stuck_s > 0.0f && t >= kStuckStart &&
                               now - g.prog_at >= stuck_s && now >= g.prog_at &&
                               bx * bx + bz * bz > 400.0f * 400.0f;
      if (live && !no_progress && t < 60.0f && now < g.inv_until && !g.spotted && !(Load32(base, c + kSight) & kValidTarget))
      {
        return;
      }
      if (no_progress)
      {
        // Round 11 (item 5): no pad route is tried: he gives the body up at once. A short look round where he stands
        // (Rare's SEARCH at his own pad, as an alert guard's) takes Rare's walk off him; then his post, wary.
        g.prog_at = now;
        g.prog_moved = false;
        g.inv_walk = 0;
        const uint32_t own_pad = Load32(base, c + kPad);
        const bool look = Ptr(own_pad) && GiveSearch(ctx, base, c, own_pad);
        if (look)
        {
          g.look_started = now;
          g.look_pad = own_pad;
        }
        REXLOG_INFO("pdz_lab_stealth: {} NO PROGRESS: Rare's walk to the body moved him less than 1 m in {:.1f} s ({:.0f} cm "
                    "from it, {:.1f} s into the walk): he gives it up at once{}: {}",
                    Id(base, c), stuck_s, std::sqrt(bx * bx + bz * bz), t,
                    look ? " (a short look round where he stands, then his post)" : "", StackText(base, c));
        Set(base, c, g, g.inv_brief && !g.inv_from_wary ? St::Unaware : St::Wary, now,
            g.inv_brief ? "no way to the lamp he found dark (no progress on his walk): back to what he was doing"
                        : "no way to the body (no progress on his walk): back to his post, wary");
        return;
      }
      const uint32_t own = Load32(base, c + kPad);
      float op[3];
      const float gx = me[0] - g.inv_centre[0], gz = me[2] - g.inv_centre[2];
      if (PadPos(base, own, op) && std::fabs(op[1] - g.inv_floor) <= kFloorCm && Dist3(op, g.inv_centre) <= 400.0f)
      {
        g.inv_centre_pad = own; // he looks round where he stopped
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} {} after {:.1f} s: he is {:.0f} cm from the body (height {:+.0f} cm); his look is at pad "
                    "{:08X}: {}",
                    Id(base, c),
                    no_progress ? "walk to the body given up (no progress)"
                    : live ? "walk to the body not over"
                    : g.inv_brief ? "HIS WALK TO THE LAMP HE FOUND DARK IS OVER (the lamp is the 'body' of this line)"
                    : std::sqrt(gx * gx + gz * gz) <= 400.0f ? "REACHED THE BODY (Rare's walk is over)"
                                                             : "Rare's walk ended short of the body",
                    t,
                    std::sqrt(gx * gx + gz * gz), me[1] - g.inv_centre[1], g.inv_centre_pad, StackText(base, c));
      }
      g.inv_walk = 0;
      if (g.spotted || (Load32(base, c + kSight) & kValidTarget) != 0)
      {
        return; // Rare's sight has her: his COMBAT decides
      }
      g.inv_until = now + (live ? 20.0f : 30.0f); // the look, then at most pdz_lab_stealth_body_spots spots
      if (now >= g.inv_until)
      {
        FinishInvestigation(ctx, base, c, g, now, "time up");
      }
      else if (!InvLeg(ctx, base, c, g, now))
      {
        FinishInvestigation(ctx, base, c, g, now, "no further spot");
      }
      return;
    }
    if (g.inv_walk_wait && (g.inv != Inv::Body || g.inv_body < 0 || size_t(g.inv_body) >= g_kills.size() || now - g.inv_start > 45.0f ||
                            g.inv_leg > 1))
    {
      g.inv_walk_wait = false;
    }
    const bool cut_pending = g.inv_walk_wait && g.inv_walk_cut && !in_search;
    if (g.inv_walk_wait && (cut_pending || now - g.inv_walk_chk >= 0.5f) && !g.spotted && !(Load32(base, c + kSight) & kValidTarget))
    {
      // Round 6 (test r6_1d: a body noticed 2 s after the death, before Rare had made its corpse object): once the
      // corpse exists his leg is ended (by the SEARCH rules: standing, at its pad, or 20 s in) and Rare's walk starts.
      g.inv_walk_chk = now;
      float where[3] = {};
      const char *how = "";
      const uint32_t corpse = CorpseOf(ctx, base, c, g_kills[size_t(g.inv_body)], where, how);
      if (corpse && in_search)
      {
        if (!g.inv_walk_cut && Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} the body can be walked to now ({} {:08X}): his leg ends for Rare's walk", Id(base, c), how,
                      corpse);
        }
        g.inv_walk_cut = true;
        g.inv_end_leg = true;
        g.inv_walk_cut_at = now;
      }
      else if (corpse && (cut_pending || now - g.inv_leg_at >= 0.5f))
      {
        const uint32_t task = WalkToBody(ctx, base, c, corpse);
        if (!task && cut_pending && now - g.inv_walk_cut_at < 2.0f)
        {
          return; // the ended SEARCH is still on his stack: tried again next frame
        }
        g.inv_walk_wait = false;
        if (task)
        {
          g.inv_walk = task;
          g.inv_walk_at = now;
          g.inv_until = std::max(g.inv_until, now + 60.0f);
          g.inv_leg_at = now;
          g.inv_leg = 0;
          g.inv_looks = g.inv_spots = 0;
          g.inv_look = false;
          g.inv_end_leg = false;
          g.inv_done.clear();
          if (Logging())
          {
            float me[3];
            PosOf(base, c, me);
            REXLOG_INFO("pdz_lab_stealth: {} WALKS TO THE BODY of {} (Rare's walk-to-body task {:08X}; {} {:08X} at ({:.0f} {:.0f} "
                        "{:.0f}), {:.0f} cm from him; {:.0f} s after he noticed it): {}",
                        Id(base, c), g_kills[size_t(g.inv_body)].who, task, how, corpse, where[0], where[1], where[2], Dist3(me, where),
                        now - g.inv_start, StackText(base, c));
          }
          return;
        }
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} no walk to the body itself (Rare's task could not be queued: {})", Id(base, c),
                      StackText(base, c));
        }
      }
    }
    if (in_search || now - g.inv_leg_at < 2.0f || g.spotted || (Load32(base, c + kSight) & kValidTarget) != 0)
    {
      return; // the leg runs (or Rare's sight has her: his COMBAT decides)
    }
    if (g.search_seen < g.inv_leg_at)
    {
      if (now - g.inv_leg_at < 4.0f)
      {
        return;
      }
      ++g.inv_fails;
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} {} investigation leg {} at pad {:08X} never started its SEARCH (+6272 {:08X})",
                    Id(base, c), InvName(g.inv), g.inv_leg, g.inv_pad, Load32(base, c + kConfig));
      }
      if (g.inv_fails >= 2)
      {
        FinishInvestigation(ctx, base, c, g, now, "two legs never started");
        return;
      }
    }
    else
    {
      g.inv_fails = 0;
    }
    const int legs = RarePlus() ? 0 : std::max(0, int(REXCVAR_GET(pdz_lab_stealth_inv_legs))); // (rareplus: the lamp's spot only)
    const int max_legs = legs + (g.inv == Inv::Squad ? 0 : 1);
    if (g.stuck == 3)
    {
      // Round 11 (item 5): his walk to a spot made no progress: the spot is given up, no look in place, no other spot.
      g.stuck = 0;
      g.stuck_look = false;
      const std::string kind = InvName(g.inv);
      Set(base, c, g, St::Wary, now,
          fmt::format("{} investigation given up after {:.0f} s (no progress on his walk to the spot): back to his post, wary",
                      kind, now - g.inv_start));
      R3EndLine(ctx, base, c, g, now, (kind + " check given up (no progress on his walk)").c_str()); // round 3 (L)
      return;
    }
    if (g.stuck_look)
    {
      g.stuck_look = false;
      g.stuck = 0;
      FinishInvestigation(ctx, base, c, g, now, "no way to the spot: he looked round where he stood");
      return;
    }
    if (now >= g.inv_until)
    {
      FinishInvestigation(ctx, base, c, g, now, "time up");
      return;
    }
    if (g.inv_leg >= max_legs && !Ptr(g.inv_redirect))
    {
      if (g.inv == Inv::Squad)
      {
        g.inv_done.erase(g.inv_done.begin(), g.inv_done.begin() + std::min<size_t>(g.inv_done.size(), 1)); // keep going round
      }
      else
      {
        FinishInvestigation(ctx, base, c, g, now, "every spot checked");
        return;
      }
    }
    if (g.want_body >= 0)
    {
      return; // a body he noticed: its investigation starts now that his leg is over (After)
    }
    if (!InvLeg(ctx, base, c, g, now))
    {
      FinishInvestigation(ctx, base, c, g, now, "no further spot");
    }
  }

  // 12 (d): the alarm is over: searching / hunting guards back to their posts, wary; fighters when they lose her.
  void CalmDown(PPCContext &ctx, uint8_t *base, float now, const char *why, bool timed)
  {
    bool rare_soft = false; // Rare's own soft-alarm flags (the tracking system is down): leave them all
    for (const auto &[c, g] : g_guards)
    {
      rare_soft = rare_soft || (!g.soft_by_us && g.st != St::Dead && pdz::guestmem::Ok(base, c, kConfig + 4) &&
                                (Load32(base, c + kConfig) & kSoftAlarm));
    }
    int calm = 0, fighting = 0;
    std::string list;
    for (auto &[c, g] : g_guards)
    {
      if (g.st == St::Dead || g.dead || g.scientist)
      {
        continue;
      }
      // Sun 22 Nov night (audit): still his memory (mapped, the same setup), as LoudRelease tests, before any write.
      if (!pdz::guestmem::Ok(base, c, kTargetHolder + 8) || Load32(base, c + kData) != g.data)
      {
        continue;
      }
      if (g.soft_by_us && !rare_soft)
      {
        Store32(base, c + kConfig, Load32(base, c + kConfig) & ~kSoftAlarm);
      }
      g.soft_by_us = false;
      const bool sees = (Load32(base, c + kSight) & kValidTarget) != 0 || (now >= g.last_visible && now - g.last_visible < 2.0f);
      if (timed && g.st == St::Combat && !sees)
      {
        // The alarm ended by itself: nobody has seen Jo for pdz_lab_stealth_alarm_end_s, so a guard still in Rare's
        // combat has no sight of her (Rare's alarm restarted it). He leaves it quietly (Rare's own leave-combat request,
        // his target cleared) and goes wary; else his COMBAT update would run for the alarm again at once (821CC358).
        {
          const R7Leaving leaving; // (Tue 17 Nov: the layer's deliberate end of his fight: not held back)
          ClearTarget(ctx, base, c);
        }
        Store32(base, c + kSight, Load32(base, c + kSight) | kLeaveCombat);
        g.spotted = false;
        g.calm_after_combat = true;
        Set(base, c, g, St::Lost, now, "the alarm ended by itself and he has not seen Jo: he leaves his combat");
        ++calm;
        list += " " + Id(base, c) + " (combat without a sight of her: leaves it)";
      }
      else if (g.st == St::Combat || g.st == St::Lost)
      {
        g.calm_after_combat = true;
        ++fighting;
        list += " " + Id(base, c) + " (fighting: wary when he loses her)";
      }
      else if (g.st != St::Unaware && g.st != St::Wary)
      {
        g.calm_req = true;
        ++calm;
        list += " " + Id(base, c) + " (" + StName(g.st) + ")";
      }
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: the alarm is over ({}): {} guard(s) back to their posts wary, {} still fighting:{}", why,
                  calm, fighting, list.empty() ? std::string(" nobody") : list);
    }
  }

  uint32_t AlarmManager(const uint8_t *base)
  {
    const uint32_t level = Load32(base, kLevelState);
    const uint32_t alarm = Ptr(level) ? Load32(base, level + 0x2D8) : 0;
    return Ptr(alarm) ? alarm : 0;
  }

  // Once per AI frame: the alarm's number, its end by itself, and the calm-down when it ends.
  void AlarmTick(PPCContext &ctx, uint8_t *base, float now)
  {
    if (now == g_alarm_tick)
    {
      return;
    }
    g_alarm_tick = now;
    // pdz_test_stealth_alarm (testing): Rare's own alarm calls, as the alarm switch makes them.
    const std::string want = REXCVAR_GET(pdz_test_stealth_alarm);
    if (want != g_test_alarm_done)
    {
      g_test_alarm_done = want;
      const uint32_t m = AlarmManager(base);
      const bool on = want.rfind("on", 0) == 0, off = want.rfind("off", 0) == 0;
      if (Ptr(m) && (on || off))
      {
        REXLOG_INFO("pdz_lab_stealth: test: Rare's alarm {} call ({})", on ? "on (sub_8214EBD0)" : "off (sub_8214EDF0)", want);
        g_alarm_by_test = true;
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = m;
        if (on)
        {
          sub_8214EBD0(frame.ctx, base);
        }
        else
        {
          sub_8214EDF0(frame.ctx, base);
        }
      }
    }
    const bool tiers = false; // round 11: no alarm tiers (the waves are never held back)
    if (g_alarm_on && AlarmOn(base) && REXCVAR_GET(pdz_lab_stealth_alarm_end) && !RarePlus() && (!tiers || g_alarms_ended == 0))
    {
      // The end by itself, before the transition below, so the end is booked in this same tick (test r4_i: with the end
      // booked a tick later, the hub scientist's alarm run in that frame still got through and raised alarm 2).
      const float end_s = float(REXCVAR_GET(pdz_lab_stealth_alarm_end_s));
      const float quiet = now - std::max(g_alarm_start, g_alarm_seen);
      const uint32_t m = AlarmManager(base);
      if (end_s > 0.0f && quiet >= end_s && Ptr(m))
      {
        REXLOG_INFO("pdz_lab_stealth: ALARM {} ends by itself: {:.0f} s without a confirmed sighting of Jo (raised {:.0f} s "
                    "ago): Rare's switch-off (sub_8214EDF0)",
                    g_alarm_no, quiet, now - g_alarm_start);
        g_alarm_by_us = true;
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = m;
        sub_8214EDF0(frame.ctx, base);
      }
    }
    const bool on = AlarmOn(base);
    {
      // Diagnostics (round 4): the Laboratory script's flags 71 / 72 (W+8 0x80 / 0x100): Rare's own update raises the
      // alarm (flag 72, 823216D8) 5 s after flag 71 was set by the message case at 823246D4 (82324738).
      static uint32_t last = 0xFFFFFFFFu;
      const uint32_t script = lab_restorations::CurrentScript();
      const uint32_t flags = Ptr(script) ? Load32(base, script + 1004) : 0;
      const uint32_t w8 = Ptr(flags) ? (Load32(base, flags + 8) & 0x180) : 0;
      if (w8 != last && last != 0xFFFFFFFFu && Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: Rare's level flags: 71 (an alarm in 5 s) {}, 72 (alarm raised by the level script) {}",
                    (w8 & 0x80) ? "set" : "clear", (w8 & 0x100) ? "set" : "clear");
      }
      last = w8;
    }
    if (on && !g_alarm_on)
    {
      ++g_alarm_no;
      g_alarm_start = g_alarm_seen = now;
      int cancelled = 0;
      for (auto &[c, g] : g_guards)
      {
        // A new alarm: a calm-down still pending from the last one's end is void (test r4_g).
        cancelled += (g.calm_req || g.calm_after_combat) ? 1 : 0;
        if (g.calm_req)
        {
          g.end_search = false;
        }
        g.calm_req = g.calm_after_combat = false;
      }
      if (cancelled && Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: alarm {}: {} guard(s) still on their way back from the last alarm carry on as before",
                    g_alarm_no, cancelled);
      }
      REXLOG_INFO("pdz_lab_stealth: ALARM {} raised{}: {}", g_alarm_no, g_alarm_by_test ? " (test tool)" : "",
                  !tiers                  ? "pdz_lab_stealth_loud off: the stealth code stays on; Rare's reinforcement waves as Rare sends them"
                  : g_alarms_ended == 0   ? "the FIRST alarm: the placed guards respond, Rare's reinforcement waves (s10) are "
                                            "held back"
                                          : "an alarm after the first one ended: Rare's reinforcement waves (s10) come in as "
                                            "Rare sends them; this alarm does not end by itself");
      g_alarm_by_test = false;
    }
    else if (!on && g_alarm_on)
    {
      ++g_alarms_ended;
      g_alarm_ended_at = now;
      const char *how = g_alarm_by_us     ? "by itself (no confirmed sighting for pdz_lab_stealth_alarm_end_s)"
                        : g_alarm_by_test ? "by the test tool (Rare's switch-off call)"
                                          : "switched off (Jo's alarm switch, or another script of Rare's)";
      REXLOG_INFO("pdz_lab_stealth: ALARM {} ended {} after {:.0f} s; s10 wave spawns held {}, let through {}, characters "
                  "spawned {}",
                  g_alarm_no, how, now - g_alarm_start, g_waves_held, g_waves_passed, g_waves_spawned);
      g_waves_held = g_waves_passed = g_waves_spawned = 0; // (counted from the raise, which can come a frame before the tick)
      const bool timed = g_alarm_by_us;
      g_alarm_by_us = g_alarm_by_test = false;
      if (REXCVAR_GET(pdz_lab_stealth_alarm_end))
      {
        CalmDown(ctx, base, now, how, timed);
      }
      if (timed && REXCVAR_GET(pdz_lab_stealth_tannoy))
      {
        // Round 5 (item 6): the alarm ended by itself: the tannoy's "cannot locate the intruder" once (TryTannoy
        // drops it while the tracking system runs).
        g_tannoy_want = now;
        g_tannoy_by = 0;
        g_tannoy_cannot_locate = true;
        g_tannoy_why = fmt::format("alarm {} ended by itself", g_alarm_no);
      }
    }
    g_alarm_on = on;
  }

  // A confirmed sighting during the alarm restarts its count (12 (d)).
  void AlarmSighting(const uint8_t *base, uint32_t c, float now)
  {
    if (!g_alarm_on)
    {
      return;
    }
    g_alarm_seen = now;
    if (Logging() && (now - g_alarm_seen_log >= 10.0f || now < g_alarm_seen_log))
    {
      g_alarm_seen_log = now;
      REXLOG_INFO("pdz_lab_stealth: {} sees Jo during alarm {}: its count starts again (it would end {:.0f} s from now)",
                  Id(base, c), g_alarm_no, float(REXCVAR_GET(pdz_lab_stealth_alarm_end_s)));
    }
  }

  // After his sight pass: the state machine (once per NPC update).
  // Round 9: GUARDS HEAR JO'S FOOTSTEPS (pdz_lab_footsteps). What Rare built and left without a reaction:
  //  - every step of a character adds to his footstep noise (footstep code 821FF7C0 -> noise setter 8218B150 type 0:
  //    perception profile +0x28 += 200 walking (horizontal speed under 480 cm/s) or 400 running, clamped 5..400);
  //  - the hearing loop (8218AAC8, 2 listeners a frame) marks a listener within that distance of a step-maker (profile
  //    +0x44 |= 0x40 | 0x80, 8218ACB0-8218ACC8) and writes the maker's pad to +0x4C; then 0x40 is cleared unless a
  //    sound event was heard too (8218AD24) and 0x80 has no reader: nothing ever starts from a footstep;
  //  - Rare's sound events are limited by the zone hops between the listener's pad and the event's pad (8218AA34-
  //    8218AA64: sub_8216D908 against the listener's limit, profile +0x3C); footsteps have no such test and no wall
  //    test (the collision data's PDZ_HitmaskBlockAiSound, 0x40, has no AI user).
  // What this adds: for Jo's steps only (never a squad mate's; not in a game with a second human player), a CALM
  // guard (unaware or wary, not a scientist) within hearing has his hearing meter filled every frame she moves
  // (round 11b: full after pdz_lab_footsteps_run_s / _walk_s), never above his notice level + 5; at the notice level the
  // existing SUSPICIOUS route runs (TryInvestigate: one investigator per squad, Rare's SEARCH at her pad with its
  // turn and start animation, HEAR_UNKNOWN_SOLO through the voice rules). A guard who already searches or hunts is
  // left to Rare's own pointer (+0x4C). Not heard: beyond Rare's zone-hop limit (always), and through geometry with
  // Rare's block-AI-sound flag (collision mask 0x40) on the line from his ears to her legs: the Laboratory's walls
  // and floors carry it (probe lines through the Lab One balcony floor, the hall floor, the hall walkway and across
  // the level's walls are all blocked for 0x40 as for the sight mask 0x20; open air is clear). pdz_lab_footsteps_wall
  // 1 adds the sight mask as well. OURS, off: pdz_lab_footsteps_crouch_quiet halves the radius while she crouches.
  float g_step_value = 0.0f;   // Jo's footstep noise (profile +0x28) at the last look
  float g_step_at = -1e9f;     // the AI clock of her last step (the noise rose)
  float g_step_radius = 0.0f;  // ... its radius (200 walking, 400 running)
  float g_step_seen = -1e9f;   // the AI clock of the last look

  int ZoneHops(const uint8_t *base, uint32_t pad_a, uint32_t pad_b)
  {
    const int za = int(int16_t(Load32(base, pad_a + 60) & 0xFFFF)), zb = int(int16_t(Load32(base, pad_b + 60) & 0xFFFF));
    if (za == zb)
    {
      return 0;
    }
    const int zones = int(int16_t(Load32(base, 0x82E2F364) >> 16));
    const uint32_t mgr = Load32(base, 0x82D6BFD8 + 144);
    const uint32_t holder = Ptr(mgr) ? Load32(base, mgr + 1116) : 0;
    const uint32_t table = Ptr(holder) ? Load32(base, holder) : 0;
    if (!Ptr(table) || za < 0 || zb < 0 || za >= zones || zb >= zones)
    {
      return 0;
    }
    return int(int16_t(Load32(base, table + 2 * uint32_t(zones * za + zb)) >> 16)); // Rare's sub_8216D908
  }

  // Round 11: Jo's movement stick as Rare's controller object holds it (mnk_layout.cpp: character +6560 -> her
  // participant, its controller object at +1680, the move outputs +28 strafe / +32 walk, each -1..1 after Rare's
  // square mapping sub_82273910): the larger axis is the stick's deflection. -1: not readable.
  float MoveStick(const uint8_t *base, uint32_t jo)
  {
    const uint32_t part = Load32(base, jo + 6560);
    if (!Ptr(part) || (part & 3) != 0)
    {
      return -1.0f;
    }
    const float x = LoadF(base, part + 1680 + 28), y = LoadF(base, part + 1680 + 32);
    if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > 1.5f || std::fabs(y) > 1.5f)
    {
      return -1.0f;
    }
    return std::max(std::fabs(x), std::fabs(y));
  }

  // Round 11i (item 3; log 017 21:38:53: s3g5's squad search ended "back to his post, wary", Rare rebuilt his DEFAULT
  // script, and he stood on his last search spot on the walkway for over a minute). "Back to his post" really
  // happens: 0.8 s after a search of ours ended (Rare's DEFAULT script is up again by then) he is walked to his post
  // with Rare's plain go-to-pad task (WalkToPad: his ordinary walk) and left to Rare's default behaviour there. No
  // 1 m of progress in pdz_lab_stealth_stuck_s (from 4 s into the walk): a short look round where he stands takes
  // the walk off him and he stays there, wary. Anything else that takes him (a sighting, a new check) ends it.
  void PostTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
  {
    if (g.post_due < -1e8f && !g.post_task)
    {
      return;
    }
    const bool calm = g.st == St::Unaware || g.st == St::Wary;
    if (!REXCVAR_GET(pdz_lab_stealth_back_to_post) || g.scientist || !LayerVoice(g.voice) || g.dead ||
        !(LoadF(base, c + kHealth) > 0.0f))
    {
      g.post_due = -1e9f;
      g.post_task = 0;
      return;
    }
    float me[3];
    PosOf(base, c, me);
    const float dx = me[0] - g.post_pos[0], dz = me[2] - g.post_pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (g.post_task)
    {
      const float t = now - g.post_walk_at;
      if (!calm || in_combat || in_search)
      {
        REXLOG_INFO("pdz_lab_stealth: {} BACK TO POST: interrupted after {:.1f} s, {:.0f} cm from his post (state {}{}): "
                    "something else has him now",
                    Id(base, c), t, d, StName(g.st), in_combat ? ", fighting" : in_search ? ", searching" : "");
        g.post_task = 0;
        g.post_due = -1e9f;
        return;
      }
      if (!TaskLive(base, c, g.post_task))
      {
        // Round 11j (item 2; log 019 21:58:29.9 and 22:00:24.0: two of four walks back were gone 40 ms after they were
        // handed over). A walk that is off his task list within a second, with him still away from his post, did not
        // run: it is tried again (three walks in all, 1.5 s apart, the second with the other pad of his post), and
        // each time the log says what the pads and the route test say.
        const bool at_once = t < 1.0f && d > 300.0f;
        const uint32_t cur = Load32(base, c + kPad);
        float tp[3] = {};
        const bool have_tp = PadPos(base, g.post_target, tp);
        const std::string facts =
            fmt::format("his pad {:08X}, the walk's pad {:08X}{}; an open pad route between them: {}; {}", cur, g.post_target,
                        have_tp ? fmt::format(" at ({:.0f} {:.0f} {:.0f})", tp[0], tp[1], tp[2]) : std::string(" (no position)"),
                        cur == g.post_target ? "he counts as AT that pad already" : OpenPathWithin(base, cur, g.post_target, 6000.0f) ? "yes" : "NO",
                        StackText(base, c));
        if (at_once && g.post_tries < 3)
        {
          REXLOG_INFO("pdz_lab_stealth: {} BACK TO POST: the walk ENDED AT ONCE (walk {} of 3, {:.0f} cm from his post): {}: "
                      "tried again in 1.5 s",
                      Id(base, c), g.post_tries, d, facts);
          g.post_task = 0;
          g.post_due = now + 0.7f;
          return;
        }
        REXLOG_INFO("pdz_lab_stealth: {} BACK TO POST: {} ({:.0f} cm from his post at ({:.0f} {:.0f} {:.0f}), {:.0f} cm above / "
                    "below it, after {:.1f} s, walk {} of 3){}: Rare's default behaviour from here ({})",
                    Id(base, c),
                    d <= 300.0f ? "arrived" : at_once ? "no route: the walk ended at once three times" : "his walk ended short of it", d,
                    g.post_pos[0], g.post_pos[1], g.post_pos[2], me[1] - g.post_pos[1], t, g.post_tries,
                    d <= 300.0f ? std::string() : ": " + facts, StName(g.st));
        g.post_task = 0;
        return;
      }
      const float stuck_s = float(REXCVAR_GET(pdz_lab_stealth_stuck_s));
      const bool stuck = stuck_s > 0.0f && t >= kStuckStart && now >= g.prog_at && now - g.prog_at >= stuck_s;
      if (stuck || t > 90.0f)
      {
        // Thu 19 Nov (no freezing, 4a): released to Rare: our walk ends, no look round of ours.
        if (R10PostNoRoute(ctx, base, c, g, now,
                           fmt::format("{}; {:.0f} cm from his post, {:.1f} s into the walk",
                                       stuck ? fmt::format("no 1 m of progress in {:.1f} s", stuck_s) : std::string("not there after 90 s"), d, t)))
        {
          g.post_task = 0;
          g.post_due = -1e9f;
          return;
        }
        const uint32_t own = Load32(base, c + kPad);
        const bool look = Ptr(own) && GiveSearch(ctx, base, c, own);
        if (look)
        {
          g.look_started = now;
          g.look_pad = own;
        }
        REXLOG_INFO("pdz_lab_stealth: {} BACK TO POST: no route ({}; {:.0f} cm from his post, {:.1f} s into the walk): he stays "
                    "where he is, {}{}",
                    Id(base, c), stuck ? fmt::format("no 1 m of progress in {:.1f} s", stuck_s) : std::string("not there after 90 s"), d,
                    t, StName(g.st), look ? " (a short look round takes the walk off him)" : "");
        g.post_task = 0;
      }
      return;
    }
    if (!calm || in_combat)
    {
      g.post_due = -1e9f;
      return;
    }
    if (now < g.post_due || now - g.post_due < 0.8f || now - g.post_try_at < 0.3f)
    {
      return;
    }
    g.post_try_at = now;
    if (d <= 200.0f)
    {
      REXLOG_INFO("pdz_lab_stealth: {} BACK TO POST: he is at his post already ({:.0f} cm from it)", Id(base, c), d);
      g.post_due = -1e9f;
      return;
    }
    if (!Ptr(g.post_pad))
    {
      float pd = 0.0f;
      const uint32_t near_pad = NearestPadTo(base, Load32(base, c + kPad), g.post_pos, pd);
      g.post_pad = Ptr(near_pad) && pd <= 300.0f ? near_pad : 0;
    }
    const bool busy = in_search || (Load32(base, c + kConfig) & kSearching) != 0 || Looking(g, now) || g.body_pending;
    // Round 11j: the pad of the walk: the pad nearest to where he stood (within 3 m of it), found from where he is
    // now; the pad he had at level start otherwise, and on the second walk of three.
    uint32_t target = g.post_pad;
    {
      float nd = 0.0f;
      const uint32_t nearest = NearestPadTo(base, Load32(base, c + kPad), g.post_pos, nd);
      const bool use_nearest = Ptr(nearest) && nd <= 300.0f;
      if (use_nearest && (g.post_tries != 1 || !Ptr(g.post_pad)))
      {
        target = nearest;
      }
    }
    const uint32_t task = Ptr(target) && !busy ? WalkToPad(ctx, base, c, target) : 0;
    if (task)
    {
      g.post_target = target;
      ++g.post_tries;
    }
    if (!task)
    {
      if (!Ptr(g.post_pad) || now - g.post_due > 15.0f)
      {
        REXLOG_INFO("pdz_lab_stealth: {} BACK TO POST: no route ({}; {:.0f} cm from his post): he stays where he is, {}",
                    Id(base, c), Ptr(g.post_pad) ? "his task stack stayed busy for 15 s" : "no pad of his post in reach", d,
                    StName(g.st));
        g.post_due = -1e9f;
      }
      return;
    }
    g.post_task = task;
    g.post_walk_at = now;
    g.post_due = -1e9f;
    g.prog_at = now;
    g.prog_moved = false;
    std::memcpy(g.prog_ref, me, sizeof(me));
    REXLOG_INFO("pdz_lab_stealth: {} BACK TO POST: walks back (walk {} of 3; {:.0f} cm to his post at ({:.0f} {:.0f} {:.0f}); from "
                "his pad {:08X} to pad {:08X}, an open pad route between them: {}; Rare's go-to-pad task {:08X}: his ordinary "
                "walk), state {}: {}",
                Id(base, c), g.post_tries, d, g.post_pos[0], g.post_pos[1], g.post_pos[2], Load32(base, c + kPad), target,
                Load32(base, c + kPad) == target ? "he counts as at that pad already"
                : OpenPathWithin(base, Load32(base, c + kPad), target, 6000.0f) ? "yes" : "NO",
                task, StName(g.st), StackText(base, c));
  }

  // Round 11j (item 1; log 019 21:58:41-22:00: Rare's search shuttled s1m2 along one corridor for over a minute; the
  // no-progress check saw nothing, he kept moving). MOVING BUT NOT GETTING CLOSER: every walk or search of ours with
  // a target spot (his walk back to his post, a walk to a body or a lamp, an investigation leg's pad, the pad of a
  // glimpse search, a hunt leg's pad) is given up when his distance to the spot (ground plan) has not shrunk by
  // pdz_lab_stealth_closer_cm in pdz_lab_stealth_closer_s. Not counted once he has been within 3 m of the spot (a
  // search looks round from there).
  void CloserTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    const float secs = float(REXCVAR_GET(pdz_lab_stealth_closer_s));
    if (secs <= 0.0f || g.scientist || g.dead || g.st == St::Dead)
    {
      g.closer_key = 0;
      return;
    }
    float spot[3] = {};
    uint32_t key = 0;
    const char *what = "";
    if (g.post_task)
    {
      std::memcpy(spot, g.post_pos, sizeof(spot));
      key = 1;
      what = "his post";
    }
    else if (g.st == St::Investigating && g.inv_walk)
    {
      std::memcpy(spot, g.inv_centre, sizeof(spot));
      key = 2;
      what = "the thing he walks to";
    }
    else if (g.st == St::Investigating && Ptr(g.inv_pad) && PadPos(base, g.inv_pad, spot))
    {
      key = g.inv_pad;
      what = "the spot of his investigation leg";
    }
    else if ((g.st == St::Suspicious || g.st == St::Searching) && Ptr(g.search_pad) && PadPos(base, g.search_pad, spot))
    {
      key = g.search_pad;
      what = "the spot of his search";
    }
    else if (g.st == St::Hunting && Ptr(g.leg_pad) && PadPos(base, g.leg_pad, spot))
    {
      key = g.leg_pad;
      what = "the spot of his hunt leg";
    }
    if (!key)
    {
      g.closer_key = 0;
      return;
    }
    float me[3];
    PosOf(base, c, me);
    const float dx = me[0] - spot[0], dz = me[2] - spot[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (key != g.closer_key || now < g.closer_at)
    {
      g.closer_key = key;
      g.closer_d = d;
      g.closer_at = now;
      g.closer_done = false;
      return;
    }
    if (g.closer_done)
    {
      return;
    }
    if (d <= 300.0f)
    {
      g.closer_done = true;
      return;
    }
    const float need = std::max(10.0f, float(REXCVAR_GET(pdz_lab_stealth_closer_cm)));
    if (d <= g.closer_d - need)
    {
      g.closer_d = d;
      g.closer_at = now;
      return;
    }
    if (now - g.closer_at < secs)
    {
      return;
    }
    REXLOG_INFO("pdz_lab_stealth: {} NOT GETTING CLOSER: {} at ({:.0f} {:.0f} {:.0f}) is {:.0f} cm away, {:.0f} cm at best {:.0f} s "
                "ago (not {:.0f} cm nearer since), state {}: he gives that spot up{}",
                Id(base, c), what, spot[0], spot[1], spot[2], d, g.closer_d, now - g.closer_at, need, StName(g.st),
                g.st == St::Hunting ? ": his next hunt leg" : ": back to his post, wary");
    g.closer_key = 0;
    const auto look = [&]() {
      // (a short look round where he stands takes Rare's walk off him, as the no-progress rule does)
      const uint32_t own = Load32(base, c + kPad);
      if (Ptr(own) && GiveSearch(ctx, base, c, own))
      {
        g.look_started = now;
        g.look_pad = own;
      }
    };
    if (g.post_task)
    {
      look();
      g.post_task = 0;
      g.post_tries = 3;
      return;
    }
    if (g.st == St::Hunting)
    {
      g.end_leg = true;
      return;
    }
    if (g.st == St::Investigating)
    {
      if (g.inv_walk)
      {
        g.inv_walk = 0;
        look();
      }
      else
      {
        g.end_search = true;
        g.end_search_at = now;
      }
      const std::string r3_kind = InvName(g.inv); // (round 3, L: Set() below clears it)
      Set(base, c, g, St::Wary, now, "not getting closer to the spot: he gives it up: back to his post, wary");
      R3EndLine(ctx, base, c, g, now, (r3_kind + " check given up (not getting closer)").c_str()); // round 3 (L)
      return;
    }
    g.glimpse_search_at = -1e9f;
    g.end_search = true; // (his search ends at its next update: wary, then back to his post)
    g.end_search_at = now;
  }

  bool SecondHuman(const uint8_t *base)
  {
    int humans = 0;
    uint32_t p = Load32(base, 0x82E34224);
    for (int i = 0; Ptr(p) && i < 64; ++i, p = Load32(base, p + 4))
    {
      humans += Load32(base, p + 944) == 2 ? 1 : 0;
    }
    return humans > 1;
  }

  // Round 11k (item 4; log 021 09:00:48.958-49.836: s3g4's sighting was confirmed and his COMBAT started 0.9 s
  // later, from a squad mate's "alerted by a friend", not from his own sight). When our meter confirms a sighting
  // Rare's sight test answers "visible" from then on, but Rare's own sight step still takes its time before it raises
  // "saw a valid target". So Rare's "alert now" input (+6260 0x40, stimulus 0x8: what a squad mate's COMBAT start
  // sets on him) is raised for him every frame until his COMBAT script starts (1.5 s at most), and his first-contact
  // line is asked for in the frame of the sighting.
  void SightNowTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (g.sight_now_until > -1e8f)
    {
      if (g.combat_script != 0 || g.st == St::Combat || g.dead || g.st == St::Dead || now >= g.sight_now_until ||
          now + 5.0f < g.sight_now_until)
      {
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} sighting -> COMBAT: {} {:.2f} s after the sighting was confirmed", Id(base, c),
                      g.combat_script != 0 || g.st == St::Combat ? "his COMBAT started" : "his COMBAT did NOT start within 1.5 s; given up",
                      now - g.spotted_at);
        }
        g.sight_now_until = -1e9f;
      }
      else
      {
        Store32(base, c + kStimuli, Load32(base, c + kStimuli) | 0x40);
        R6AlertNote(base, c, 0, 2, now); // (overnight Tue 17 Nov, item 5: the layer itself raised "alert now" on him)
      }
    }
    if (g.sight_line_due)
    {
      g.sight_line_due = false;
      if (now - g.sighting_said > 3.0f || now < g.sighting_said)
      {
        Say(ctx, base, c, AfterContact(g) ? kEvSeeAgain : kEvSeePlayer, false, "first contact, asked for at his sighting");
      }
    }
  }

  // Round 11k (item 1): he heard gunfire: the player hears that he did. A trooper: HEAR_GUNFIRE ("Gunshots!", "We got
  // gunfire!"), a warning line. A calm scientist whose own hearing (Rare's gunfire input +6260 0x2, behind the wall
  // test) heard it: HEAR_GUNFIRE_SOLO ("Was that a gun?"), at most once in 20 s.
  void GunfireLineTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (!REXCVAR_GET(pdz_lab_stealth_gunfire_hearing) || g.dead || g.st == St::Dead)
    {
      return;
    }
    if (g.scientist)
    {
      if ((g.voice == 15 || g.voice == 16) && (g.st == St::Unaware || g.st == St::Wary) && !g.warn_ev &&
          (Load32(base, c + kStimuli) & 0x2) && (now - g.gunfire_line_said > 20.0f || now < g.gunfire_line_said))
      {
        g.gunfire_line_said = now;
        SciSay(base, g, "HEAR_GUNFIRE_SOLO", 0xF, 2.0f, now, "heard gunfire", false);
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} (scientist) heard GUNFIRE (Rare's gunfire input, walls and zone hops counted): "
                      "his line HEAR_GUNFIRE_SOLO; what he does is Rare's (state {})",
                      Id(base, c), StName(g.st));
        }
      }
      return;
    }
    if (g.gunfire_line_at > g.gunfire_line_said)
    {
      g.gunfire_line_said = g.gunfire_line_at;
      if (const uint32_t ev = EventIndex(base, "HEAR_GUNFIRE_SOLO"))
      {
        Warn(ctx, base, c, g, ev, now, "heard gunfire");
      }
      else
      {
        Warn(ctx, base, c, g, kEvHearUnknown, now, "heard gunfire");
      }
    }
  }

  // Round 11k (item 7; log 021: a scientist on alert stuck in a doorway; Rare's music picker plays Silence for as
  // long as anybody is "running to an alarm", music_cause_log.cpp). A scientist whose alarm run (Rare's alarm task
  // ran for him in the last half second, or his announced run is committed) has moved him less than
  // pdz_lab_scientist_stuck_cm in pdz_lab_scientist_stuck_s gives up and cowers: Rare's own cower start, the calls of
  // the scientist task at 82319E88-82319ED4 (end the task's child tasks, the cower animation part as its child, the
  // cowering byte +0x4C, the stamp +0x58). Before and after LOUD.
  void StuckSciTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    const float limit_s = float(REXCVAR_GET(pdz_lab_scientist_stuck_s));
    if (limit_s <= 0.0f || (g.voice != 15 && g.voice != 16))
    {
      return;
    }
    if (now >= g.stuck_chk && now - g.stuck_chk < 0.25f)
    {
      return;
    }
    g.stuck_chk = now;
    float p[3];
    PosOf(base, c, p);
    const uint32_t task = lab_cut::SciTaskOf(base, c);
    const bool cowering = task && base[task + 0x4C] != 0;
    const bool running = (g.run_committed && !g.run_pending) || lab_cut::AlarmRunning(base, c);
    if (!task || cowering || !running || !(LoadF(base, c + kHealth) > 0.0f) || g.stuck_at < -1e8f || now < g.stuck_at ||
        Dist3(p, g.stuck_pos) >= float(REXCVAR_GET(pdz_lab_scientist_stuck_cm)))
    {
      std::memcpy(g.stuck_pos, p, sizeof(p));
      g.stuck_at = now;
      return;
    }
    if (now - g.stuck_at < limit_s)
    {
      return;
    }
    const uint32_t own = Load32(base, task + 40);
    if (!Ptr(own))
    {
      g.stuck_at = now;
      return;
    }
    REXLOG_INFO("pdz_lab_stealth: {} (scientist) STUCK on his alarm run: he moved {:.0f} cm in {:.0f} s at ({:.0f} {:.0f} "
                "{:.0f}): he gives up and COWERS (Rare's own cower start){}: {}",
                Id(base, c), Dist3(p, g.stuck_pos), now - g.stuck_at, p[0], p[1], p[2], g_loud ? ", after LOUD" : "",
                StackText(base, c));
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = own;
      sub_8217FF00(frame.ctx, base);
    }
    uint32_t part = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 88;
      sub_821784F0(frame.ctx, base);
      part = frame.ctx.r3.u32;
    }
    if (Ptr(part))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = part;
      frame.ctx.r4.u64 = 0;
      sub_821A9D60(frame.ctx, base);
      part = frame.ctx.r3.u32;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = own;
      frame.ctx.r4.u64 = Ptr(part) ? part : 0;
      sub_8217F0F0(frame.ctx, base);
    }
    base[task + 0x4C] = 1;
    StoreD(base, task + 0x58, double(LoadF(base, kAiClock)));
    g.run_committed = false;
    g.run_pending = false;
    g.run_ready = 0;
    g.run_block_until = now + 60.0f;
    g.stuck_at = now;
  }

  // Round 11k (item 6): AFTER LOUD the alarm run keeps its warning. Rare asks his alarm-run builder (821DF4A0) for a
  // guard's or a scientist's run: the builder is run now; when it makes the task (an alarm in reach), he shouts first
  // (WARN_TEAM played on him: troopers takes 1 and 2, the "Sound the alarm!" kind; scientists "Someone call
  // security!"), hesitates pdz_lab_stealth_alarm_announce_s, then gets that very task (AlarmRunTick), the run is kept
  // (RunKeepTick) and the alarm cannot sound before pdz_lab_stealth_alarm_min_s after the shout (the alarm hook). True:
  // the request is answered here (r3 = no task now).
  bool LoudAlarmAsk(PPCContext &ctx, uint8_t *base, uint32_t who)
  {
    const float now = Clock(base);
    const uint32_t voice = Load32(base, who + kVoiceIndex);
    const uint32_t data = Load32(base, who + kData);
    if (const auto it = g_guards.find(who); it != g_guards.end() && it->second.data && it->second.data != data)
    {
      g_guards.erase(it);
    }
    Guard &a = g_guards[who];
    if (!a.data)
    {
      a.data = data;
      a.who = Id(base, who);
      a.scientist = voice == 15 || voice == 16;
    }
    a.voice = voice;
    if (!(LoadF(base, who + kHealth) > 0.0f))
    {
      return false;
    }
    if (a.run_committed || a.run_pending)
    {
      ctx.r3.u64 = 0; // (his task is ours to give)
      return true;
    }
    if ((now >= a.announced_at && now - a.announced_at < 10.0f) || now < a.run_go_until)
    {
      return false; // announced: Rare's own request goes through
    }
    if (now < a.run_block_until)
    {
      ctx.r3.u64 = 0;
      return true;
    }
    uint32_t ready = 0;
    {
      g_alarm_run_ours = true;
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = who;
      sub_821DF4A0(frame.ctx, base);
      ready = frame.ctx.r3.u32;
      g_alarm_run_ours = false;
    }
    if (!Ptr(ready))
    {
      if (now - a.run_check_log >= 10.0f || now < a.run_check_log)
      {
        a.run_check_log = now;
        REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN CHECK (after LOUD; Rare asked for his alarm run): Rare's builder makes no "
                    "alarm task (no alarm in reach, the alarm is on, or no route): NO SHOUT, no run",
                    Id(base, who));
      }
      ctx.r3.u64 = 0;
      return true;
    }
    a.run_ready = ready;
    const char *set = VoiceSetName(voice);
    std::string cue = "no line of his voice";
    if (set)
    {
      cue = std::string(set) + "_WARN_TEAM";
      if (voice <= 19)
      {
        lab_stealth::OrderTake(base, cue, a.scientist ? 0xFu : 0x3u);
      }
      lab_cut::SayOnCharacter(ctx, base, who, ("XACT_SOUNDBANK_" + cue).c_str());
      lab_stealth::WatchLine(cue);
    }
    const float hes = std::max(0.0f, float(REXCVAR_GET(pdz_lab_stealth_alarm_announce_s)));
    const float min_s = float(REXCVAR_GET(pdz_lab_stealth_alarm_min_s));
    a.run_pending = true;
    a.run_from = now;
    a.run_at = now + hes;
    a.run_why = "Rare's own alarm run, after LOUD";
    a.announced_at = now;
    a.run_committed = true;
    a.run_commit_at = now;
    a.run_task = 0;
    a.run_gives = a.run_fails = 0;
    a.run_give_at = -1e9f;
    a.warn_ev = 0;
    if (min_s > 0.0f)
    {
      g_alarm_not_before = now + min_s;
      g_alarm_runner = who;
    }
    g_loud_commit_until = now + 50.0f;
    REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN ANNOUNCED AFTER LOUD ({}): Rare's builder made the alarm task {:08X}; his shout "
                "{} is played on him now; he hesitates {:.1f} s, then gets that task; no alarm before {:.1f} s from now",
                Id(base, who), a.scientist ? "scientist" : "guard", ready, cue, hes, std::max(0.0f, min_s));
    ctx.r3.u64 = 0;
    return true;
  }

  // Round 11k (items 6, 7, 8): what still runs for a character AFTER LOUD (once per sight update of his; every other
  // hook of this file passes straight through): his announced alarm run, the stuck scientist, and the log line.
  void LoudTick(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(jo) || c == jo)
    {
      return;
    }
    const float now = Clock(base);
    if (g_alarm_deferred && now >= g_alarm_not_before)
    {
      const uint32_t manager = g_alarm_deferred;
      g_alarm_deferred = 0;
      const bool alive = Ptr(g_alarm_runner) && pdz::guestmem::Ok(base, g_alarm_runner, kHealth + 4) && // (Sun 22 Nov night (audit))
                         LoadF(base, g_alarm_runner + kHealth) > 0.0f;
      REXLOG_INFO("pdz_lab_stealth: ALARM HELD (after LOUD) is {}: the runner {} {} {:.1f} s after his shout",
                  alive ? "raised now" : "NOT raised", Ptr(g_alarm_runner) ? Id(base, g_alarm_runner) : std::string("?"),
                  alive ? "is still alive" : "was killed or is gone", float(REXCVAR_GET(pdz_lab_stealth_alarm_min_s)));
      if (alive)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = manager;
        sub_8214EBD0(frame.ctx, base);
      }
    }
    const uint32_t voice = Load32(base, c + kVoiceIndex);
    if (!(voice >= 15 && voice <= 19) && voice != 21 && voice != 22)
    {
      return;
    }
    const uint32_t data = Load32(base, c + kData);
    if (const auto it = g_guards.find(c); it != g_guards.end() && it->second.data && it->second.data != data)
    {
      g_guards.erase(it);
    }
    Guard &g = g_guards[c];
    if (!g.data)
    {
      g.data = data;
      g.who = Id(base, c);
      g.scientist = voice == 15 || voice == 16;
    }
    g.voice = voice;
    const float hp = LoadF(base, c + kHealth);
    const bool alive = std::isfinite(hp) && hp > 0.0f && (Load32(base, c + kConfig) & kDormant) == 0;
    if (g.run_pending || g.run_committed)
    {
      g.dead = !alive; // (AlarmRunTick / RunKeepTick end the run of a dead runner)
      g.st = alive ? (g.st == St::Dead ? St::Unaware : g.st) : St::Dead;
      AlarmRunTick(ctx, base, c, g, now);
      if (g.run_committed)
      {
        RunKeepTick(ctx, base, c, g, now);
      }
    }
    if (!alive)
    {
      return;
    }
    StuckSciTick(ctx, base, c, g, now);
    const float period = float(REXCVAR_GET(pdz_lab_stealth_loud_log_s));
    if (Logging() && period > 0.0f && (now - g.loud_line_at >= period || now < g.loud_line_at))
    {
      float p[3];
      PosOf(base, c, p);
      if (g.loud_line_seen)
      {
        const float moved = Dist3(p, g.loud_line_pos);
        const uint32_t sight = Load32(base, c + kSight), config = Load32(base, c + kConfig), latch = Load32(base, c + 6252);
        const uint32_t sci_task = g.scientist ? lab_cut::SciTaskOf(base, c) : 0;
        const int depth = StackDepth(base, c);
        const uint32_t top = depth > 0 ? Load32(base, c + kScheduler + kStackEntry * uint32_t(depth - 1)) : 0;
        const uint32_t pad = Load32(base, c + kPad), target_pad = Load32(base, c + kTargetPad);
        float pp[3] = {0.0f, 0.0f, 0.0f};
        const bool has_target_pad = Ptr(target_pad) && PadPos(base, target_pad, pp);
        std::string state = (latch & 0x10) ? "ALERTED" : "calm";
        state += (sight & kValidTarget) ? ", sees a target" : "";
        state += (config & kSearching) ? ", searching" : "";
        state += lab_cut::AlarmRunning(base, c) ? ", RUNNING FOR AN ALARM" : "";
        state += g.run_pending ? ", alarm shout / hesitation" : "";
        state += sci_task ? (base[sci_task + 0x4C] ? ", cowering" : fmt::format(", scientist task state {}", Load32(base, sci_task + 0x38))) : "";
        REXLOG_INFO("pdz_lab_stealth: AFTER LOUD: {} ({}) | {} | top task {:08X} | at ({:.0f} {:.0f} {:.0f}) pad {:08X} | his "
                    "target's last pad {} | moved {:.0f} cm in {:.1f} s{} | {}",
                    Id(base, c), g.scientist ? "scientist" : "guard", state, Ptr(top) ? Load32(base, top) : 0, p[0], p[1], p[2],
                    Ptr(pad) ? pad : 0,
                    has_target_pad ? fmt::format("{:08X} ({:.0f} {:.0f} {:.0f})", target_pad, pp[0], pp[1], pp[2]) : std::string("none"),
                    moved, now - g.loud_line_at, moved < 100.0f ? " STUCK" : "", StackText(base, c));
      }
      g.loud_line_seen = true;
      g.loud_line_at = now;
      std::memcpy(g.loud_line_pos, p, sizeof(p));
    }
  }

  // Round 11l (item 4): a searcher's mutter and a hunter's call-out are HEAR_UNKNOWN only when that guard really
  // heard something: he is on a noise investigation, or a "heard ..." line of his was due in the last 3 minutes.
  bool HeardLately(const Guard &g, float now)
  {
    return (g.st == St::Investigating && g.inv == Inv::Noise) || (now >= g.heard_at && now - g.heard_at < 180.0f) ||
           (now >= g.loud_at && now - g.loud_at < 180.0f);
  }
  // ... else the uneasy line of round 11k's wary call-out: asked for as the HEAR_UNKNOWN event, the cue swapped
  // (PLAYER_CLOAKED_NEXT_TEAM takes 3, 6 and, TROOPER1 / 2, 4 with a live squad mate within
  // pdz_lab_stealth_warn_team_cm; else PLAYER_CLOAKED_NEXT_SOLO takes 1 and 6).
  bool SayUneasy(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool optional, const char *what)
  {
    if (!REXCVAR_GET(pdz_lab_stealth_noticed_takes) || g.voice < 17 || g.voice > 19 || g.scientist)
    {
      return Say(ctx, base, c, kEvHearUnknown, optional, what);
    }
    bool mate = false;
    const float reach = float(REXCVAR_GET(pdz_lab_stealth_warn_team_cm));
    for (const auto &[other, o] : g_guards)
    {
      mate = mate || (other != c && g.squad && o.squad == g.squad && !o.scientist && !o.dead && o.st != St::Dead &&
                      LoadF(base, other + kHealth) > 0.0f && Dist(base, c, other) <= reach);
    }
    g.alt_cue = mate ? "PLAYER_CLOAKED_NEXT_TEAM" : "PLAYER_CLOAKED_NEXT_SOLO";
    g.alt_takes = mate ? (g.voice == 19 ? 0x24u : 0x2Cu) : 0x21u;
    g.alt_at = now;
    ArmSwapName(base, c, g.voice, std::string(VoiceSetName(g.voice)) + "_" + g.alt_cue, "HEAR_UNKNOWN_SOLO", now,
                "HEAR_UNKNOWN_TEAM");
    const bool said = Say(ctx, base, c, kEvHearUnknown, optional, what);
    if (!said)
    {
      DisarmSwap();
      g.alt_cue = nullptr;
    }
    return said;
  }

  // Round 11l (item 3): WHAT THE SIGHT DIRECTION FOLLOWS. Rare's perception pass (sub_8218A810) asks each character's
  // class for a transform and an eye point (its virtual function at vtable +376: r4 -> a 4x3 matrix, r5 -> the eye)
  // and passes the matrix's third row (its forward axis) on as the facing of the sight test (sub_8218A210 r4, then
  // sub_82189F18 r10): that is the vector our meter uses. Logged against his body's yaw (+0x1670) so that a hand
  // test shows whether it moves with his head while his body stands still.
  void SightFacingLog(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g)
  {
    if (!Logging() || !REXCVAR_GET(pdz_lab_stealth_sight_log) || g.dead || !(g.st == St::Unaware || g.st == St::Wary))
    {
      return;
    }
    const float now = g.eye_at;
    if (now >= g.sight_log_at && now - g.sight_log_at < 0.5f)
    {
      return;
    }
    g.sight_log_at = now;
    const float heading = std::atan2(g.dir[0], g.dir[2]) * 57.29578f;
    const float pitch = std::asin(std::clamp(g.dir[1], -1.0f, 1.0f)) * 57.29578f;
    const float yaw = LoadF(base, c + 0x1670); // (degrees: 345 lines of the first 11l run agree with the heading within 1)
    if (!std::isfinite(heading) || !std::isfinite(yaw))
    {
      return;
    }
    float diff = std::fmod(heading - yaw, 360.0f);
    diff = diff > 180.0f ? diff - 360.0f : diff < -180.0f ? diff + 360.0f : diff;
    float moved = std::fabs(diff - g.sight_diff_logged);
    moved = moved > 180.0f ? 360.0f - moved : moved;
    if (!(now - g.sight_log_full >= 10.0f || now < g.sight_log_full || moved > 15.0f))
    {
      return;
    }
    g.sight_log_full = now;
    g.sight_diff_logged = diff;
    const int depth = StackDepth(base, c);
    const uint32_t top = depth > 0 ? Load32(base, c + kScheduler + kStackEntry * uint32_t(depth - 1)) : 0;
    // The other branch of Rare's eye transform (sub_82545318, the guards' vtable +376 through 8252AF38): with a
    // skeleton it takes the transform of one skeleton node ([char +72] entry [82FBE3C0] + 15, +1088) through
    // sub_82519050. Asked for here for the log only (which node that is, is not known: if its heading moves during a
    // look-around while the sight heading stands still, it is the head).
    std::string node_text = "no skeleton node";
    const uint32_t skeleton = Load32(base, c + 72);
    const uint32_t node_index = Load32(base, 0x82FBE3C0);
    if (Ptr(skeleton) && Load32(base, skeleton + 60) != 0 && node_index < 64)
    {
      const uint32_t node = Load32(base, skeleton + 4 * (node_index + 15));
      if (Ptr(node))
      {
        const uint32_t m = (ctx.r1.u32 - 0x600) & ~0xFu; // guest stack scratch below the caller's frame
        std::memset(base + m, 0, 64);
        rex::CallFrame frame(ctx);
        frame.ctx.r1.u64 = m - 0x400;
        frame.ctx.r3.u64 = c;
        frame.ctx.r4.u64 = node + 1088;
        frame.ctx.r5.u64 = m;
        sub_82519050(frame.ctx, base);
        const float fx = LoadF(base, m + 32), fy = LoadF(base, m + 36), fz = LoadF(base, m + 40);
        node_text = fmt::format("skeleton node {} forward: heading {:.0f} deg, pitch {:.0f} deg", node_index,
                                std::atan2(fx, fz) * 57.29578f, std::asin(std::clamp(fy, -1.0f, 1.0f)) * 57.29578f);
      }
    }
    REXLOG_INFO("pdz_lab_stealth: SIGHT FACING: {} ({}) sight heading {:.0f} deg, pitch {:.0f} deg (the vector Rare's sight "
                "test gets); his yaw value (+0x1670) {:.0f} deg; difference {:+.0f} deg{} | {} | eye ({:.0f} {:.0f} {:.0f}) | "
                "top task {:08X}",
                Id(base, c), g.scientist ? "scientist" : "guard", heading, pitch, yaw, diff,
                moved > 15.0f && moved < 400.0f ? " (CHANGED)" : "", node_text, g.eye[0], g.eye[1], g.eye[2],
                Ptr(top) ? Load32(base, top) : 0);
  }

  void Footsteps(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, float frame_dt)
  {
    if (REXCVAR_GET(pdz_test_footsteps_trace) && REXCVAR_GET(pdz_lab_footsteps))
    {
      // TESTING: Jo's own movement and noise twice a second, whoever is near.
      static float last = -1e9f;
      if (now - last >= 0.5f || now < last)
      {
        last = now;
        const uint32_t p = ProfileOf(base, jo);
        REXLOG_INFO("pdz_lab_footsteps: trace: Jo at ({:.0f} {:.0f} {:.0f}), speed {:.0f} cm/s, footstep noise (Rare's) {:.0f}, "
                    "last step {:.2f} s ago at radius {:.0f}",
                    LoadF(base, jo + kPosition), LoadF(base, jo + kPosition + 4), LoadF(base, jo + kPosition + 8), g_jo_speed,
                    p ? LoadF(base, p + 0x28) : -1.0f, now - g_step_at, g_step_radius);
      }
    }
    // Stealth4, Mon 16 Nov (fix 5): with pdz_lab_footsteps off, the sneak rule still hears her RUN behind him, or a bump.
    const bool r4_sneak = !REXCVAR_GET(pdz_lab_footsteps) && R4ASneakOn() && LayerGuard(g);
    R4AGait(ctx, base, jo, now);
    if ((!REXCVAR_GET(pdz_lab_footsteps) && !r4_sneak) || g.scientist || !(g.st == St::Unaware || g.st == St::Wary) || g.spotted ||
        g.sees)
    {
      g.hear = 0.0f;
      return;
    }
    // Round 11b: it drains slowly (pdz_lab_footsteps_drain) in every frame that does not fill it (below).
    const float hear_before = g.hear;
    g.hear = std::max(0.0f, g.hear - std::max(0.0f, float(REXCVAR_GET(pdz_lab_footsteps_drain))) * frame_dt);
    const uint32_t jp = ProfileOf(base, jo);
    if (!jp)
    {
      return;
    }
    if (now != g_step_seen)
    {
      // Once a frame: Jo's own footstep noise as Rare's setter leaves it. A rise is a new step.
      g_step_seen = now;
      const float v = LoadF(base, jp + 0x28);
      if (v > g_step_value + 50.0f)
      {
        g_step_at = now;
        g_step_radius = v > 300.0f ? 400.0f : 200.0f;
      }
      g_step_value = v;
    }
    float radius = std::max(g_step_value, now >= g_step_at && now - g_step_at < 0.7f ? g_step_radius : 0.0f);
    if (REXCVAR_GET(pdz_test_footsteps_trace) && now - g.trace_log >= 0.5f)
    {
      float a[3], b[3];
      PosOf(base, c, a);
      PosOf(base, jo, b);
      if (Dist3(a, b) < 1200.0f)
      {
        g.trace_log = now;
        REXLOG_INFO("pdz_lab_footsteps: trace: {} is {:.0f} cm from Jo; her footstep noise (Rare's) {:.0f}, last step {:.2f} s "
                    "ago at radius {:.0f}, her speed {:.0f} cm/s, his meter {:.0f}",
                    Id(base, c), Dist3(a, b), g_step_value, now - g_step_at, g_step_radius, g_jo_speed, g.meter);
      }
    }
    // Round 11b (item 1; log 002 18:07:31.8: Jo ran 4 m up to s1m5's back in 2.7 s and his hearing reached 10 of 20;
  // 18:05:16 the same): the meter only filled in the frames Rare's own footstep noise value was up (one sound event
  // about every 0.7 s). Now it fills in EVERY frame she is moving within range at that gait: she moves when her
  // movement stick is pushed or her sampled speed is 30 cm/s or more. Rare's step noise is only the fallback for the
  // gait when the stick cannot be read.
    const float stick = MoveStick(base, jo);
    if (!(g_jo_speed >= 30.0f || stick > 0.15f) || SecondHuman(base))
    {
      return; // she stands still, or not a one-player game
    }
    // Round 11 (item 2; log 010 17:02:47: a wary guard heard her 398 cm behind him at once, his wary meter floor being
    // above his halved notice level). Her gait: crouching (Rare's crouch test) is never heard; walking (the movement
    // stick at pdz_lab_footsteps_walk_stick or less) within pdz_lab_footsteps_walk_cm; running within
    // pdz_lab_footsteps_run_cm. The stick is Rare's own move value (MoveStick); unreadable: Rare's step noise decides
    // (200 = a walking step, 400 = a running one). A hearing meter of his own (Guard::hear) fills while her steps are
    // in his hearing and he notices at the plain notice level: a wary guard hears no farther and no sooner.
    const bool rare_running = radius > 300.0f;
    const bool crouching = Crouched(ctx, base, jo);
    // (round 5, fix 1: under the sneak rule only Rare's sprint, weapon put away, is running; a weapon out is a walk)
    const bool running = r4_sneak ? R5ASprinting(now)
                                  : !crouching && (stick >= 0.0f ? stick > float(REXCVAR_GET(pdz_lab_footsteps_walk_stick)) + 0.02f : rare_running);
    const char *gait = crouching ? "CROUCHING" : running ? (r4_sneak ? "SPRINTING" : "RUNNING") : "WALKING";
    const float walk_cm = r4_sneak ? 0.0f : std::max(0.0f, float(REXCVAR_GET(pdz_lab_footsteps_walk_cm)));
    const float run_cm = std::max(0.0f, float(r4_sneak ? REXCVAR_GET(pdz_stealth_sneak_run_cm) : REXCVAR_GET(pdz_lab_footsteps_run_cm)));
    float heard_cm = crouching ? 0.0f : running ? run_cm : walk_cm;
    const float test_radius = float(REXCVAR_GET(pdz_test_footsteps_radius));
    if (test_radius > 0.0f && !crouching)
    {
      heard_cm = test_radius; // TESTING only
    }
    float me[3], her[3];
    PosOf(base, c, me);
    PosOf(base, jo, her);
    const float d = Dist3(me, her);
    if (d >= std::max({walk_cm, run_cm, test_radius}))
    {
      return; // beyond any hearing of steps
    }
    bool r4_bump = false;
    if (r4_sneak && !R4AStepsCount(base, c, g, jo, d, now, heard_cm, r4_bump))
    {
      return; // (stealth4, fix 5: she is in front of him: the sight rules, not her steps)
    }
    const float notice = float(REXCVAR_GET(pdz_lab_stealth_notice)); // the plain level: no wary bonus
    const std::string how = fmt::format("{} ({}), {} cm away, hearing meter {:.1f} of {:.0f}", gait,
                                        stick >= 0.0f ? fmt::format("stick {:.2f}", stick)
                                                      : std::string(rare_running ? "stick not readable: Rare's running step"
                                                                                 : "stick not readable: Rare's walking step"),
                                        int(d), hear_before, notice);
    if (d >= heard_cm)
    {
      if (Logging() && now - g.steps_log >= 2.0f)
      {
        g.steps_log = now;
        REXLOG_INFO("pdz_lab_footsteps: {} does NOT hear Jo's steps: {}: {}", Id(base, c), how,
                    crouching ? std::string("crouching is never heard")
                              : fmt::format("{} is heard within {:.0f} cm only", running ? "running" : "walking", heard_cm));
      }
      return;
    }
    const char *blocked = nullptr;
    const uint32_t mp = ProfileOf(base, c);
    const uint32_t my_pad = Load32(base, c + kPad), her_pad = Load32(base, jo + kPad);
    const int limit = mp ? int(int32_t(Load32(base, mp + 0x3C))) : -1;
    int hops = 0;
    if (Ptr(my_pad) && Ptr(her_pad) && limit >= 0 && (hops = ZoneHops(base, my_pad, her_pad)) > limit)
    {
      blocked = "beyond Rare's zone-hop limit";
    }
    else
    {
      const float ears[3] = {me[0], me[1] + 60.0f, me[2]};  // (positions are at hip height)
      const float feet[3] = {her[0], her[1] - 40.0f, her[2]}; // her legs: not so low that uneven ground hides the point
      if (!SegmentClear(ctx, base, ears, feet, 0x40) || !SegmentClear(ctx, base, feet, ears, 0x40))
      {
        blocked = "geometry with Rare's block-AI-sound flag between them (a wall or a floor)";
      }
      else if (REXCVAR_GET(pdz_lab_footsteps_wall) >= 1 && !SightClear(ctx, base, feet, ears))
      {
        blocked = "a wall, floor or ceiling between his ears and her feet (the AI sight mask)";
      }
    }
    if (blocked)
    {
      if (Logging() && now - g.steps_log >= 2.0f)
      {
        g.steps_log = now;
        REXLOG_INFO("pdz_lab_footsteps: {} does NOT hear Jo's steps: {} (heard within {:.0f} cm): {} (zone hops {}, his limit {})",
                    Id(base, c), how, heard_cm, blocked, hops, limit);
      }
      return;
    }
    const float full_s = r4_bump ? 0.05f // (stealth4, fix 5: a bump is heard at once)
                                 : std::max(0.05f, float(running ? REXCVAR_GET(pdz_lab_footsteps_run_s) : REXCVAR_GET(pdz_lab_footsteps_walk_s)));
    const float before = hear_before;
    g.hear = std::min(notice + 5.0f, hear_before + notice / full_s * frame_dt); // (this frame fills: no drain)
    const bool heard = before < notice && g.hear >= notice;
    if (heard)
    {
      g.hear = 0.0f;
      if (r4_sneak && R4AHeard(base, c, g, jo, r4_bump, d, now))
      {
        return; // (stealth4, fix 5: round 3's heard reaction takes it)
      }
      g.steps_at = now;
      g.glimpse_time = now; // the state machine's input (After): where she is
      std::memcpy(g.glimpse_pos, her, sizeof(her));
      g.meter = std::max(g.meter, Notice(g));
    }
    if (Logging() && (now - g.steps_log >= 0.5f || heard))
    {
      g.steps_log = now;
      REXLOG_INFO("pdz_lab_footsteps: {} hears Jo's steps: {} (heard within {:.0f} cm; zone hops {}, his limit {}), state {}: "
                  "hearing now {:.1f} of {:.0f} (full after {:.1f} s of this){}",
                  Id(base, c), how, heard_cm, hops, limit, StName(g.st), heard ? notice : g.hear, notice, full_s,
                  heard ? ": HEARD: he goes to look where she was" : "");
    }
  }

  // TESTING: pdz_test_stealth_behind: Jo behind a named guard, looking at his back.
  void TestBehind(PPCContext &ctx, uint8_t *base, uint32_t jo, float now)
  {
    static std::string done;
    static uint32_t scratch = 0;
    const std::string want = REXCVAR_GET(pdz_test_stealth_behind);
    if (want == done)
    {
      return;
    }
    done = want;
    const size_t comma = want.find(',');
    if (comma == std::string::npos)
    {
      return;
    }
    const std::string id = want.substr(0, comma);
    float dist = 0.0f, height = 0.0f;
    if (std::sscanf(want.c_str() + comma + 1, "%f,%f", &dist, &height) < 1)
    {
      return;
    }
    for (auto &[c, g] : g_guards)
    {
      if (g.st == St::Dead || g.dead || Id(base, c) != id || !(LoadF(base, c + kHealth) > 0.0f))
      {
        continue;
      }
      float me[3];
      PosOf(base, c, me);
      float fx = g.face[0], fz = g.face[2];
      const char *how = "his facing at his last sight test";
      if (!(now >= g.face_at && now - g.face_at < 5.0f) || fx * fx + fz * fz < 1e-6f)
      {
        const float yaw = LoadF(base, c + 0x1670);
        fx = std::sin(yaw);
        fz = std::cos(yaw);
        how = "his yaw (+0x1670)";
      }
      const float fl = std::sqrt(fx * fx + fz * fz);
      fx /= fl;
      fz /= fl;
      const float pos[3] = {me[0] - fx * dist, me[1] - 76.0f + height, me[2] - fz * dist};
      if (!scratch)
      {
        scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
      }
      if (!scratch)
      {
        return;
      }
      for (int i = 0; i < 3; ++i)
      {
        uint32_t bits;
        std::memcpy(&bits, &pos[i], 4);
        Store32(base, scratch + 4 * uint32_t(i), bits);
      }
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = jo;
        frame.ctx.r4.u64 = scratch;
        frame.ctx.r5.u64 = 0;
        sub_825AD148(frame.ctx, base);
      }
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = jo;
        frame.ctx.f1.f64 = 0.0;
        frame.ctx.f2.f64 = double(std::atan2(fx, fz));
        sub_825AD3B0(frame.ctx, base);
      }
      REXLOG_INFO("pdz_lab_stealth: test: Jo put {:.0f} cm behind {} (height {:+.0f} cm) at ({:.0f} {:.0f} {:.0f}), looking at "
                  "his back; he stands at ({:.0f} {:.0f} {:.0f}), facing ({:.2f} {:.2f}) from {}, state {}",
                  dist, id, height, pos[0], pos[1], pos[2], me[0], me[1], me[2], fx, fz, how, StName(g.st));
      return;
    }
    REXLOG_INFO("pdz_lab_stealth: test: no live tracked guard {} to put Jo behind", id);
  }

  void TestProbe(PPCContext &ctx, uint8_t *base)
  {
    static std::string done;
    const std::string want = REXCVAR_GET(pdz_test_footsteps_probe);
    if (want == done)
    {
      return;
    }
    done = want;
    float a[3], b[3];
    if (std::sscanf(want.c_str(), "%f,%f,%f,%f,%f,%f", &a[0], &a[1], &a[2], &b[0], &b[1], &b[2]) != 6)
    {
      return;
    }
    REXLOG_INFO("pdz_lab_footsteps: probe ({:.0f} {:.0f} {:.0f}) -> ({:.0f} {:.0f} {:.0f}), {:.0f} cm: AI sight mask 0x20: {} / "
                "{} (there / back); AI sound mask 0x40: {} / {}",
                a[0], a[1], a[2], b[0], b[1], b[2], Dist3(a, b), SegmentClear(ctx, base, a, b, 0x20) ? "clear" : "BLOCKED",
                SegmentClear(ctx, base, b, a, 0x20) ? "clear" : "BLOCKED", SegmentClear(ctx, base, a, b, 0x40) ? "clear" : "BLOCKED",
                SegmentClear(ctx, base, b, a, 0x40) ? "clear" : "BLOCKED");
  }

  // ------------------------------------------------------------------------------------------------------------
  // RARE'S RULES PLUS (pdz_lab_rareplus, Sat 14 Nov). Rare's AI decides everything; what runs here is additive.
  // ------------------------------------------------------------------------------------------------------------
  // He stops and looks round WHERE HE STANDS: Rare's SEARCH at his own pad (its Search_Look animations, ended after
  // pdz_lab_stealth_alert_look_s by the search hook), only from Rare's calm routine. No walk, no faster meter.
  bool LookRound(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
  {
    const float look_s = float(REXCVAR_GET(pdz_lab_stealth_alert_look_s));
    const uint32_t pad = Load32(base, c + kPad);
    if (!(look_s > 0.0f) || g.scientist || !(g.st == St::Unaware || g.st == St::Wary) || now - g.search_seen <= 1.0f ||
        now - g.look_started <= look_s + 6.0f || (Load32(base, c + kConfig) & (kSearching | kNeverInvestigates)) != 0 ||
        g.combat_script != 0 || !Ptr(pad) || NoLook(g.who) || // (Nightclub helper: not for the row's no_look ids)
        RadioNoLook(base, c, g, "his meter line"))            // (Mansion helper: a radio user who can still call)
    {
      return false;
    }
    if (R2NoRestart(base, c, g, Load32(base, kPlayerOne), now, "glimpse")) // (round 2, item 1: the search cap's one restart)
    {
      return false;
    }
    if (!GiveSearch(ctx, base, c, pad))
    {
      return false;
    }
    g.look_started = now;
    g.look_pad = pad;
    return true;
  }
  // One line every pdz_lab_stealth_loud_log_s per live guard and scientist: our state and meter, what Rare has him
  // doing, his top task, his position, how far he moved, STUCK under 1 m.
  void StateLine(const uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
  {
    const float period = float(REXCVAR_GET(pdz_lab_stealth_loud_log_s));
    if (!Logging() || !(period > 0.0f) || !(now - g.loud_line_at >= period || now < g.loud_line_at))
    {
      return;
    }
    float p[3];
    PosOf(base, c, p);
    if (g.loud_line_seen)
    {
      const float moved = Dist3(p, g.loud_line_pos);
      const uint32_t sight = Load32(base, c + kSight), latch = Load32(base, c + 6252);
      const uint32_t sci_task = g.scientist ? lab_cut::SciTaskOf(base, c) : 0;
      const int depth = StackDepth(base, c);
      const uint32_t top = depth > 0 ? Load32(base, c + kScheduler + kStackEntry * uint32_t(depth - 1)) : 0;
      std::string rare = (latch & 0x10) ? "alerted" : "calm";
      rare += in_combat ? ", COMBAT" : "";
      rare += in_search ? (Looking(g, now) ? ", looking round where he stands" : ", SEARCH") : "";
      rare += (sight & kValidTarget) ? ", sees a target" : "";
      rare += lab_cut::AlarmRunning(base, c) ? ", RUNNING FOR AN ALARM" : "";
      rare += g.run_pending ? ", alarm shout / hesitation" : "";
      rare += sci_task ? (base[sci_task + 0x4C] ? ", cowering" : fmt::format(", scientist task state {}", Load32(base, sci_task + 0x38))) : "";
      REXLOG_INFO("pdz_lab_stealth: STATE: {} ({}) | ours {} meter {:.0f}{} | Rare: {} | top task {:08X} | at ({:.0f} {:.0f} {:.0f}) | "
                  "moved {:.0f} cm in {:.1f} s{}",
                  Id(base, c), g.scientist ? "scientist" : "guard", StName(g.st), g.meter,
                  g.st == St::Investigating ? " (lamp check)" : "", rare, Ptr(top) ? Load32(base, top) : 0, p[0], p[1], p[2], moved,
                  now - g.loud_line_at, moved < 100.0f ? " STUCK" : "");
    }
    g.loud_line_seen = true;
    g.loud_line_at = now;
    std::memcpy(g.loud_line_pos, p, sizeof(p));
  }
  void InvTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search,
               bool glimpse);
  // The per-guard tick in place of the rounds 3 to 11l state machine.
  void RarePlusTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search,
                    bool glimpse)
  {
    g.body_pending = g.need_line = g.calm_req = g.calm_after_combat = false;
    if (in_combat)
    {
      if (g.st != St::Combat)
      {
        Set(base, c, g, St::Combat, now, "Rare's combat");
      }
    }
    else if (g.st == St::Combat || g.st == St::Lost)
    {
      Set(base, c, g, St::Wary, now, "Rare's combat is over: what he does next is Rare's (his search, his post); the meter decides again");
      g.meter = std::min(g.meter, float(REXCVAR_GET(pdz_lab_stealth_lost_start)));
      g.confirmed = false;
    }
    else if (g.st == St::Investigating)
    {
      if (g.inv == Inv::Lamp || R3HInvOurs(g, c) || R3cDInvOurs(g, c)) // (round 3 H: and the noise checks of items 1 and 6)
      {
        InvTick(ctx, base, c, g, jo, now, in_combat, in_search, glimpse); // (the lamp check: the one thing we walk him to)
      }
      else
      {
        Set(base, c, g, St::Wary, now, "rareplus: no investigation of ours but a lamp check");
      }
    }
    else if (g.st != St::Unaware && g.st != St::Wary && g.st != St::Dead)
    {
      Set(base, c, g, St::Wary, now, "rareplus: no search or hunt of ours");
    }
    if (g.st == St::Wary && g.nevermind_until > now)
    {
      NeverMind(ctx, base, c, g, now); // after a lamp check
    }
    if (LayerGuard(g) && (g.st == St::Unaware || g.st == St::Wary) && !in_combat) // (the Lab: troopers 17-19)
    {
      // THE LIGHT METER'S TWO SIGHT LINES. Below full, Rare's sight test answers "not visible" (the meter hook). At the
      // notice level: one sight line and a look round where he stands. As the meter passes pdz_lab_rareplus_second: a
      // second, stronger line (the meter is held there until it has started: the warning hold). Full: Rare's sight.
      if (g.rp_stage && now - g.rp_glimpse_at > float(REXCVAR_GET(pdz_lab_rareplus_episode_s)))
      {
        g.rp_stage = 0;
      }
      if (glimpse)
      {
        g.rp_glimpse_at = now;
        const float second = std::min(float(REXCVAR_GET(pdz_lab_rareplus_second)), Threshold(g) - 5.0f);
        if (g.meter >= Notice(g) && g.rp_stage < 1)
        {
          g.rp_stage = 1;
          WarnGlimpse(ctx, base, c, g, now, "glimpse (he thought he saw something)");
          const bool r5_walk = R5BGlimpseWalk(ctx, base, c, g, jo, now, in_search); // (round 5 B: he goes to look)
          const bool look = !r5_walk && LookRound(ctx, base, c, g, now);
          REXLOG_INFO("pdz_lab_stealth: {} NOTICED something (meter {:.0f}, notice level {:.0f}, state {}): his sight line, and {}; "
                      "{}",
                      Id(base, c), g.meter, Notice(g), StName(g.st),
                      r5_walk ? "our noise check walks him to the place"
                      : look  ? "he stops and looks round where he stands (Rare's SEARCH at his own pad)"
                              : "no look round (Rare has him busy, or he looked round a moment ago)",
                      r5_walk ? "he walks off to look (pdz_stealth_glimpse_walk)" : "he does not walk off to look");
          if (g.st == St::Unaware)
          {
            Set(base, c, g, St::Wary, now, "noticed something: wary");
          }
        }
        else if (g.meter >= second && g.rp_stage == 1 && !g.warn_ev)
        {
          g.rp_stage = 2;
          // (Stealth rollout: take 6 is a TROOPER1-3 take of the generic banks (lab_stealth_takes.inc); any other voice
          // says his own HEAR_UNKNOWN: his own set, never a borrowed line.)
          // GANGSTA1-3 (7-9): no take 6; takes 2 "I know you're close." and 5 "I know you're out there!" (Nightclub
          // survey n, section 5; Subway survey s had take 5).
          const uint32_t second_takes = g.voice >= 17 && g.voice <= 19 ? 0x20u : g.voice >= 7 && g.voice <= 9 ? 0x12u : 0u;
          g_next_swap = second_takes ? "PLAYER_CLOAKED_NEXT_SOLO" : nullptr; // take 6: "There's someone here, I can feel it." / "I know someone's here, I know it!"
          g_next_swap_takes = second_takes;
          Warn(ctx, base, c, g, kEvHearUnknown, now, "glimpse (someone is here: the stronger line)");
          g_next_swap = nullptr;
          REXLOG_INFO("pdz_lab_stealth: {} is SURE someone is here (meter {:.0f} passed {:.0f}, his sighting level {:.0f}): his "
                      "second, stronger line; the meter is held until it has started",
                      Id(base, c), g.meter, second, Threshold(g));
        }
      }
      if (g.st == St::Wary && g.next_bark > 0.0f && now >= g.next_bark && !g.sees && !in_search)
      {
        const float bark = float(REXCVAR_GET(pdz_lab_stealth_wary_bark_s));
        g.next_bark = SayUneasy(ctx, base, c, g, now, true, "wary call-out")
                          ? (bark > 0.0f ? now + bark * (0.5f + Rand01()) : 0.0f)
                          : now + 2.0f + 3.0f * Rand01();
      }
    }
    StateLine(base, c, g, now, in_combat, in_search);
  }

  void After(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, float frame_dt)
  {
    const R7Rule r7_rule("the layer's per-frame tick (sighting, footsteps, warning lines, back to post, lamps)");
    // Follow-up Sun 15 Nov (item 4): Rare's COMBAT script does not update while a task of its own (the cover task
    // 82092950) is on top, so a guard in cover for over a minute, or one who was not tracked yet when his COMBAT
    // started, read as "combat over": his sight of Jo went back to the meter while Rare had him fighting. With
    // pdz_lab_rareplus on, a COMBAT script running on his task stack is combat, whatever is above it.
    const bool combat_stack = RarePlus() && CombatOnStack(base, c);
    if (g.combat_script && now - g.combat_seen > 60.0f && !combat_stack)
    {
      g.combat_script = 0; // no update and no destructor seen for a minute: treat it as gone
    }
    const bool in_combat = g.combat_script != 0 || now - g.combat_seen < 0.5f || combat_stack;
    const bool in_search = now - g.search_seen < 0.5f;
    const bool hunting_on = REXCVAR_GET(pdz_lab_stealth_hunting) && !g.scientist;
    if (const int depth = StackDepth(base, c) + std::max(0, StackWaiting(base, c)); depth > g_stack_deepest)
    {
      g_stack_deepest = depth; // round 7: the long run's proof that the stacks stay bounded
      REXLOG_INFO("pdz_lab_stealth: DEEPEST TASK STACK so far in this run: {} of {} (with the tasks waiting), {} in state {}: {}",
                  depth, kStackMax, Id(base, c), StName(g.st), StackText(base, c));
    }
    if (g.metered)
    {
      g.sees = now - g.last_visible < kSeesHold;
      if (!g.sees && ((g.st != St::Combat && g.st != St::Lost) || LostLike(g)))
      {
        g.meter = std::max(Floor(g), g.meter - float(REXCVAR_GET(pdz_lab_stealth_decay)) * frame_dt);
      }
    }
    else
    {
      g.sees = (Load32(base, c + kSight) & kValidTarget) != 0; // Rare's own "saw a valid target"
      if (g.sees)
      {
        g.seen_jo = true;
      }
    }
    if (R9Tick(ctx, base, c, g, jo, now, in_combat, in_search))
    {
      // Wed 18 Nov night: he is Rare's guard (seen or loud): no rule of the layer runs for him, only the detectors
      // (the owner record, the facing-swing line).
      R7Tick(base, c, g, now);
      const R7Rule rule("the detectors only (he is Rare's guard)");
      R4CTick(ctx, base, c, g, now, in_combat, in_search);
      StateLine(base, c, g, now, in_combat, in_search);
      return;
    }
    // Thu 19 Nov: a calm guard: his stillness, the hand-over after a body, a sure guard who stands (r10).
    R10CalmTick(ctx, base, c, g, jo, now, in_combat, in_search);
    if (g.r9_rares)
    {
      StateLine(base, c, g, now, in_combat, in_search);
      return; // (handed to Rare just now: the layer's rules no longer run for him)
    }
    const double cut_s = REXCVAR_GET(pdz_test_stealth_cut_s);
    if (cut_s > 0.0 && !g.scientist && (now >= g.next_cut || now + 2.0f * float(cut_s) < g.next_cut))
    {
      // TESTING (crash stress): cut his running SEARCH wherever he is, and give a hunter / investigator a glimpse.
      g.next_cut = now + float(cut_s) * (0.6f + 0.8f * Rand01());
      if (in_search && !in_combat)
      {
        g.cut_test = true;
        if (g.st == St::Hunting)
        {
          g.end_leg = true;
        }
        else if (g.st == St::Investigating)
        {
          g.inv_end_leg = true;
        }
        else
        {
          g.end_search = true;
          g.end_search_at = now;
        }
      }
      if (Hunter(g) || g.st == St::Investigating)
      {
        g.glimpse_time = now;
        g.meter = std::max(g.meter, Notice(g) + 1.0f);
        g.redirect_at = -1e9f;
        const auto hs = g_hunts.find(HuntKey(g, c));
        if (hs != g_hunts.end())
        {
          hs->second.redirect_at = -1e9f;
        }
        for (int i = 0; i < 3; ++i)
        {
          g.glimpse_pos[i] = LoadF(base, jo + kPosition + 4 * i);
        }
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: test cut: {} (state {}, in search {}): {}", Id(base, c), StName(g.st), in_search ? 1 : 0,
                    StackText(base, c));
      }
    }
    SightNowTick(ctx, base, c, g, now);            // round 11k: a confirmed sighting: COMBAT and his line at once
    GunfireLineTick(ctx, base, c, g, now);         // round 11k: gunfire heard: his line
    Footsteps(ctx, base, c, g, jo, now, frame_dt); // round 9: her steps fill a calm guard's meter
    WarnTick(ctx, base, c, g, now);                // round 11b: his warning line waits for the gap, then plays
    AlarmRunTick(ctx, base, c, g, now);            // round 11f: his announced alarm run starts
    StuckSciTick(ctx, base, c, g, now);            // round 11k: a scientist stuck on his alarm run cowers
    if (g.run_committed)
    {
      RunKeepTick(ctx, base, c, g, now);           // round 11h: ... and is kept until the alarm is raised or he dies
      if (g.run_committed)
      {
        return; // nothing else of ours for a committed runner
      }
    }
    PostTick(ctx, base, c, g, now, in_combat, in_search); // round 11i: "back to his post" really happens
    SciTick(ctx, base, c, g, jo, now);             // ... a scientist's warnings
    StaffWitnessTick(base, c, g, now);             // follow-up Sun 15 Nov (item 11): ... who saw a colleague killed
    DarkLampTick(ctx, base, c, g, now, in_combat || in_search); // ... a lamp that is already out
    const bool glimpse = g.glimpse_time > g.glimpse_used;
    g.glimpse_used = g.glimpse_time;
    if (g.spotted && !in_combat && now - g.spotted_at > 3.0f)
    {
      g.spotted = false; // Rare saw her and did not fight (a greeting, a scientist): the meter decides again
    }
    if (now - g.pos_at >= 0.5f || now < g.pos_at)
    {
      // His movement (sampled every 0.5 s): Rare's walk to a body (SEE_BODY reaction 821D1298) is over when he has not
      // moved 40 cm for 1.5 s; a SEARCH is ended early only once he stands (round 3, see the SEARCH update hook).
      float p[3];
      for (int i = 0; i < 3; ++i)
      {
        p[i] = LoadF(base, c + kPosition + 4 * i);
      }
      const float dx = p[0] - g.pos_prev[0], dz = p[2] - g.pos_prev[2];
      if (g.pos_at < -1e8f || now < g.pos_at || dx * dx + dz * dz > 40.0f * 40.0f)
      {
        g.moved_at = now;
      }
      const float rx = p[0] - g.prog_ref[0], rz = p[2] - g.prog_ref[2];
      if (g.prog_at < -1e8f || now < g.prog_at || rx * rx + rz * rz >= 100.0f * 100.0f)
      {
        g.prog_moved = g.prog_at > -1e8f && now >= g.prog_at;
        std::memcpy(g.prog_ref, p, sizeof(p)); // round 7: 1 m of progress
        g.prog_at = now;
      }
      std::memcpy(g.pos_prev, p, sizeof(p));
      g.pos_at = now;
    }
    if (g.st == St::Combat && g.sees && !g.scientist)
    {
      AlarmSighting(base, c, now); // round 4: a confirmed sighting restarts the alarm's count
    }
    const bool rp = RarePlus();
    g.rare_busy = in_combat || in_search || (Load32(base, c + kConfig) & kSearching) != 0 || g.run_pending;
    if (rp)
    {
      // (Tue 17 Nov: each rule runs under its name, so the write record can say which one wrote to him.)
      R7Tick(base, c, g, now); // his owner and his frame record
      {
        const R7Rule rule("round 3c: a witness who looks first");
        R3cCTick(ctx, base, c, g, now, in_combat, in_search); // round 3c (C, fix 4): a witness who looks first
      }
      {
        const R7Rule rule("round 3c: the Mansion's radio reinforcements");
        R3cDTick(ctx, base, c, g, now, in_combat, in_search); // round 3c (D): the Mansion's radio reinforcements
      }
      {
        const R7Rule rule("round 4: an unseen Jo does not steer (NO STEER), Killian's line, the camera noise");
        R4CTick(ctx, base, c, g, now, in_combat, in_search); // round 4, Mon 16 Nov (C): unseen = no target, swing line, Killian
      }
      {
        const R7Rule rule("round 3: the heard reaction, the witness walk, a shout heard");
        R3HTick(ctx, base, c, g, now, in_combat, in_search); // round 3, Sun 15 Nov (H): heard reaction, witness walk, shout
      }
      {
        const R7Rule rule("round 2: the search cap, the blind-combat hand-over, the witness");
        R2Tick(ctx, base, c, g, now, in_combat, in_search); // round 2, Sun 15 Nov: search cap, blind combat, witness
      }
      {
        const R7Rule rule("fix2: a body seen later");
        R8LateBodyTick(ctx, base, c, g, now, in_combat, in_search); // Tue 17 Nov night (fix2, item 4)
      }
      {
        const R7Rule rule("round 3: the give-up line");
        R3EndTick(ctx, base, c, g, now, in_combat);         // round 3, Sun 15 Nov (L): his give-up line is heard
      }
      const R7Rule rule("the layer's state rules (glimpse, look round, investigation, lose contact)");
      RarePlusTick(ctx, base, c, g, jo, now, in_combat, in_search, glimpse);
    }
    else
    {
    const bool body_rules = REXCVAR_GET(pdz_lab_stealth_body_rules) && !g.scientist;
    if (g.death_seen_at > g.death_used)
    {
      // Round 5 (item 2): Rare's "a friend died in view" reached him and he has not seen Jo: he is a witness of the
      // newest kill of hers (4 s); DeathTick sends the nearest witness to search round the spot.
      g.death_used = g.death_seen_at;
      int best = -1;
      float me[3];
      PosOf(base, c, me);
      for (size_t i = 0; i < g_kills.size(); ++i)
      {
        if (now >= g_kills[i].at && now - g_kills[i].at < 4.0f && g_kills[i].victim != c &&
            (best < 0 || Dist3(g_kills[i].pos, me) < Dist3(g_kills[best].pos, me)))
        {
          best = int(i);
        }
      }
      if (best >= 0)
      {
        Kill &k = g_kills[best];
        if (std::find(k.witnesses.begin(), k.witnesses.end(), c) == k.witnesses.end())
        {
          if (k.witnesses.empty())
          {
            k.witness_at = now;
          }
          k.witnesses.push_back(c);
          SecondDeath(ctx, base, c, g, best, now, "saw the death"); // round 11f (item 3)
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} saw {} die ({:.0f} cm away) without seeing Jo, state {}: no combat (Rare's "
                        "reaction is not run); the nearest witness will search round the spot",
                        Id(base, c), k.who, Dist3(k.pos, me), StName(g.st));
          }
        }
      }
    }
    if (g.want_body >= 0)
    {
      if (size_t(g.want_body) >= g_kills.size() || g.st == St::Combat || g.st == St::Lost || in_combat ||
          now - g.want_body_at > 40.0f)
      {
        g.want_body = -1;
      }
      else if (!in_search && !(Load32(base, c + kConfig) & kSearching) && now - g.search_seen > 0.3f)
      {
        const int index = g.want_body;
        g.want_body = -1;
        g.end_search = g.end_leg = false;
        NoticeBody(ctx, base, c, g, index, now, "noticed during his search, which is over now");
      }
    }
    else if (REXCVAR_GET(pdz_lab_stealth_body_sight) && !g.scientist && g.st != St::Combat && g.st != St::Lost && !in_combat &&
             !g_kills.empty() && (now >= g.body_check_at + 0.5f || now + 10.0f < g.body_check_at))
    {
      g.body_check_at = now;
      BodySight(ctx, base, c, g, now);
    }
    if (g.noise_at > g.noise_used)
    {
      // Round 5 (item 4): he heard something (Rare's sound stimulus, taken from Rare) and has not seen Jo.
      g.noise_used = g.noise_at;
      const bool fast = g.noise_fast; // (round 11k: Jo's unsilenced shot or an explosion: at once, whoever else looks)
      g.noise_fast = false;
      float centre[3];
      const bool calm = (g.st == St::Unaware || g.st == St::Wary) && !in_combat && !in_search && !g.body_pending;
      if (PadPos(base, g.noise_pad, centre))
      {
        if (calm && (fast || now - g.since > 1.0f))
        {
          uint32_t taken_by = 0;
          for (const auto &[other, o] : g_guards)
          {
            if (other != c && o.st == St::Investigating && Dist3(o.inv_centre, centre) < 1200.0f)
            {
              taken_by = other;
            }
          }
          if (taken_by && !fast)
          {
            Alert(ctx, base, c, g, now,
                  fmt::format("heard a sound at ({:.0f} {:.0f} {:.0f}) that {} already looks into", centre[0], centre[1],
                              centre[2], Id(base, taken_by)));
          }
          else if (float nw = 0.0f; float(REXCVAR_GET(pdz_lab_stealth_glimpse_reach_cm)) > 0.0f &&
                                    !CanReachOnFloor(base, c, centre, float(REXCVAR_GET(pdz_lab_stealth_glimpse_reach_cm)), nw))
          {
            // Round 11j (item 1): a noise he cannot walk to: his line, a look round where he stands, wary.
            REXLOG_INFO("pdz_lab_stealth: {} heard {} at ({:.0f} {:.0f} {:.0f}) but cannot walk there (another floor, or no "
                        "open pad route within {:.0f} cm): NO SEARCH THERE: he looks round where he stands, wary",
                        Id(base, c), g.noise_why ? g.noise_why : "a sound", centre[0], centre[1], centre[2],
                        float(REXCVAR_GET(pdz_lab_stealth_glimpse_reach_cm)));
            Warn(ctx, base, c, g, kEvHearUnknown, now, "heard a sound");
            Alert(ctx, base, c, g, now, "heard a sound where he cannot walk to");
            if (g.st == St::Unaware)
            {
              Set(base, c, g, St::Wary, now, "heard a sound where he cannot walk to: he looks round where he stands, wary");
            }
          }
          else
          {
            StartInvestigation(ctx, base, c, g, Inv::Noise, centre, -1, now,
                               fmt::format("heard {} at ({:.0f} {:.0f} {:.0f}) without seeing Jo: he goes to look "
                                           "alone (no alarm run, no combat)",
                                           g.noise_why ? g.noise_why : g.noise_silenced ? "a SILENCED shot" : "a sound (not gunfire, not an explosion)",
                                           centre[0], centre[1], centre[2]));
            if (g.st == St::Investigating)
            {
              Warn(ctx, base, c, g, kEvHearUnknown, now, "heard a sound");
            }
          }
        }
        else if (Hunter(g) && now - g.redirect_at > 5.0f && g.noise_pad != g.leg_pad)
        {
          g.redirect_at = now;
          g.redirect_pad = g.noise_pad; // his next leg: the sound
        }
        else if (g.st == St::Investigating && g.inv != Inv::Body && now - g.redirect_at > 5.0f && g.noise_pad != g.inv_pad)
        {
          g.redirect_at = now;
          g.inv_redirect = g.noise_pad;
        }
      }
    }
    CloserTick(ctx, base, c, g, now); // round 11j (item 1): moving but not getting closer
    if ((g.st == St::Suspicious || g.st == St::Searching) && g.glimpse_search_at > -1e8f && !g.confirmed && !in_combat)
    {
      // Round 11j (item 3; log 019 22:00:57-22:02:40: s1m4's glimpse search ran about 100 s on glimpses at 30 m with his
      // meter in the teens): it ends after pdz_lab_stealth_glimpse_search_s unless he confirms her; a new glimpse only
      // restarts the time when his meter is above pdz_lab_stealth_glimpse_extend_meter or she is within
      // pdz_lab_stealth_glimpse_extend_cm.
      const float limit = float(REXCVAR_GET(pdz_lab_stealth_glimpse_search_s));
      const float jd = Ptr(jo) ? Dist(base, c, jo) : 1e9f;
      if (glimpse && (g.meter > float(REXCVAR_GET(pdz_lab_stealth_glimpse_extend_meter)) ||
                      jd <= float(REXCVAR_GET(pdz_lab_stealth_glimpse_extend_cm))))
      {
        if (now - g.glimpse_search_at > 1.0f && Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} GLIMPSE SEARCH: its {:.0f} s start again (a new glimpse: meter {:.0f}, {:.0f} cm from "
                      "Jo), {:.0f} s into it",
                      Id(base, c), limit, g.meter, jd, now - g.glimpse_search_at);
        }
        g.glimpse_search_at = now;
      }
      else if (limit > 0.0f && now - g.glimpse_search_at >= limit && now >= g.glimpse_search_at && in_search && !g.end_search)
      {
        REXLOG_INFO("pdz_lab_stealth: {} GLIMPSE SEARCH OVER after {:.0f} s without confirming her (meter {:.0f}, {:.0f} cm from "
                    "Jo): his search is ended: back to his post, wary",
                    Id(base, c), now - g.glimpse_search_at, g.meter, jd);
        g.end_search = true;
        g.end_search_at = now;
        g.glimpse_search_at = -1e9f;
      }
    }
    else if (g.st != St::Suspicious && g.st != St::Searching)
    {
      g.glimpse_search_at = -1e9f;
    }
    bool calming = false;
    if (g.calm_req && g.st != St::Combat && g.st != St::Lost && g.st != St::Dead && !in_combat)
    {
      // Round 4 (12 (d)): the alarm is over. A running SEARCH ends at its next update (at its pad or when he stands);
      // then he is wary at his post (Rare's routine takes him back).
      calming = true;
      if (in_search && g.search_top)
      {
        if (!g.end_search)
        {
          g.end_search = true;
          g.end_search_at = now;
        }
      }
      else
      {
        g.calm_req = false;
        g.end_search = false;
        g.confirmed = false;
        g.leg_pad = g.redirect_pad = 0;
        g.body_pending = false;
        Set(base, c, g, St::Wary, now,
            in_search ? "the alarm is over: wary for the rest of the level (the search Rare runs for him ends by itself)"
                      : "the alarm is over: back to his post, wary for the rest of the level");
      }
    }
    switch (calming ? St::Dead : g.st)
    {
    case St::Unaware:
    case St::Wary:
      if (in_combat)
      {
        Set(base, c, g, St::Combat, now, "Rare's combat");
      }
      else if (in_search && (Looking(g, now) || (g.st == St::Wary && !g.search_top && now - g.since < 185.0f)))
      {
        // Round 5: his look round where he stands (alert): not a search of Rare's, he stays as he was.
      }
      else if (in_search)
      {
        g.was_wary = g.st == St::Wary;
        g.search_pad = 0;
        Set(base, c, g, St::Searching, now, "Rare's search");
      }
      else if (g.body_pending && now - g.body_at > 2.0f && (now - g.moved_at > 1.5f || now - g.body_at > 20.0f))
      {
        if (body_rules && g.inv_body >= 0 && size_t(g.inv_body) < g_kills.size())
        {
          // Round 4 (12 (c)): his walk to the body is over: he searches around it alone.
          // Round 5: through the one body rule (another finder on it, or already reported: he is only alert).
          g.body_pending = false;
          NoticeBody(ctx, base, c, g, g.inv_body, now, "Rare's own body sight");
        }
        else if (BodyCheck(base, c, g, now) && hunting_on)
        {
          StartHunt(ctx, base, c, g, Load32(base, c + kPad), now, "found a body Jo left: sweeping from where it lies");
        }
      }
      else if (glimpse && !g.scientist && g.meter >= Notice(g))
      {
        TryInvestigate(ctx, base, c, g, jo, now);
      }
      else if (g.st == St::Wary && !g.scientist && g.next_bark > 0.0f && now >= g.next_bark && !g.sees)
      {
        const float bark = float(REXCVAR_GET(pdz_lab_stealth_wary_bark_s));
        // Round 11k (item 2): a wary call-out is a neutral, nervous line, not "what was that noise?". With a live
        // squad mate within pdz_lab_stealth_warn_team_cm: PLAYER_CLOAKED_NEXT_TEAM takes 3 "I got a bad feeling about
        // this." 6 "Okay, don't anybody make any sudden moves." and, TROOPER1 / 2, 4 "Stay alert, people." (TROOPER3's
        // take 4 names her). Alone: PLAYER_CLOAKED_NEXT_SOLO takes 1 "Must be seeing things." 6 "There's someone here,
        // I can feel it." Asked for as the HEAR_UNKNOWN event, the cue swapped (as the glimpse warning).
        const bool swap = REXCVAR_GET(pdz_lab_stealth_noticed_takes) && g.voice >= 17 && g.voice <= 19;
        if (swap)
        {
          bool mate = false;
          const float reach = float(REXCVAR_GET(pdz_lab_stealth_warn_team_cm));
          for (const auto &[other, o] : g_guards)
          {
            mate = mate || (other != c && g.squad && o.squad == g.squad && !o.scientist && !o.dead && o.st != St::Dead &&
                            LoadF(base, other + kHealth) > 0.0f && Dist(base, c, other) <= reach);
          }
          g.alt_cue = mate ? "PLAYER_CLOAKED_NEXT_TEAM" : "PLAYER_CLOAKED_NEXT_SOLO";
          g.alt_takes = mate ? (g.voice == 19 ? 0x24u : 0x2Cu) : 0x21u;
          g.alt_at = now;
          ArmSwapName(base, c, g.voice, std::string(VoiceSetName(g.voice)) + "_" + g.alt_cue, "HEAR_UNKNOWN_SOLO", now,
                      "HEAR_UNKNOWN_TEAM");
        }
        const bool said = Say(ctx, base, c, kEvHearUnknown, true, "wary call-out");
        if (swap && !said)
        {
          DisarmSwap();
          g.alt_cue = nullptr;
        }
        if (said)
        {
          g.next_bark = bark > 0.0f ? now + bark * (0.5f + Rand01()) : 0.0f;
        }
        else
        {
          g.next_bark = now + 2.0f + 3.0f * Rand01();
        }
      }
      break;
    case St::Suspicious:
      if (in_combat)
      {
        Set(base, c, g, St::Combat, now, "Rare's combat");
      }
      else if (in_search)
      {
        Set(base, c, g, St::Searching, now, "his SEARCH started");
      }
      else
      {
        const bool walking = g.inv_task != 0 && now - g.inv_since < 20.0f;
        const bool just_walked = now - g.inv_ended < 2.0f; // the SEARCH follows the walk
        if (!walking && !just_walked && now - g.since > 3.0f)
        {
          const char *why = g.inv_task ? "investigation still running after 20 s (cap)"
                            : g.inv_ended > g.since ? "investigation walk ended, no SEARCH followed"
                                                    : "no investigation task started (Rare's squad gate or not in DEFAULT)";
          g.inv_task = 0;
          Set(base, c, g, g.was_wary ? St::Wary : St::Unaware, now, why);
        }
      }
      break;
    case St::Searching:
    case St::LostSearch:
      if (in_combat)
      {
        Set(base, c, g, St::Combat, now, "Rare's combat");
      }
      else if (in_search && g.body_pending && body_rules && g.inv_body >= 0 && now - g.body_at > 2.0f && !g.end_search)
      {
        g.end_search = true; // round 4: Rare's own search for the body ends; his investigation of it follows
        g.end_search_at = now;
      }
      else if (!in_search && now - g.since > 2.0f && g.body_pending && body_rules && g.inv_body >= 0 &&
               size_t(g.inv_body) < g_kills.size() && g.st == St::Searching)
      {
        g.body_pending = false;
        g.end_search = false;
        NoticeBody(ctx, base, c, g, g.inv_body, now, "Rare's own body sight, during a search that is over now");
      }
      else if (!in_search && now - g.since > 2.0f)
      {
        if (g.body_pending)
        {
          BodyCheck(base, c, g, now); // Rare's second body: a SEARCH instead of the walk
        }
        if (hunting_on && g.confirmed)
        {
          StartHunt(ctx, base, c, g, g.st == St::LostSearch ? g.lost_pad : g.search_pad, now,
                    "search over after a confirmed contact: hunting, not back to his post");
        }
        else
        {
          if (g.st == St::LostSearch && !(g.last_event == kEvAbandon && now - g.last_event_time < 3.0f))
          {
            Say(ctx, base, c, kEvAbandon, true, "search abandoned");
          }
          Set(base, c, g, St::Wary, now,
              g.confirmed ? "his search is over or Rare gave him another task: Rare's AI has him again, wary "
                            "(pdz_lab_stealth_hunting off)"
                          : "his search is over or Rare gave him another task: Rare's AI has him again, wary (glimpse "
                            "only: never a confirmed contact)");
        }
      }
      else if (g.st == St::LostSearch && !g.still_said)
      {
        const float s = float(REXCVAR_GET(pdz_lab_stealth_still_hidden_s));
        if (s > 0.0f && now - g.since >= s && !g.sees)
        {
          g.still_said = Say(ctx, base, c, kEvStillHidden, true, "still hidden");
        }
      }
      break;
    case St::Combat:
    {
      const float unseen = now - std::max(float(LoadD(base, c + kLastSeen)), g.combat_start);
      // Rare's rule (the alarm means combat) unless pdz_lab_stealth_alarm_lose_s: then, while the alarm sounds, a
      // longer time out of sight (hand test 005: the hall guards held the vent exit in Rare's combat for minutes).
      const float alarm_lose = float(REXCVAR_GET(pdz_lab_stealth_alarm_lose_s));
      const bool alarm = AlarmCombat(base, c);
      const bool may_lose = !alarm || alarm_lose > 0.0f;
      const float lose_after = alarm ? alarm_lose : float(REXCVAR_GET(pdz_lab_stealth_lose_s));
      if (in_combat)
      {
        if (!g.scientist && !g.sees && may_lose && unseen > lose_after)
        {
          LoseContact(ctx, base, c, g, now, unseen, false);
        }
      }
      else if (!g.scientist && !g.sees && unseen > 2.0f && may_lose && (!alarm || unseen > lose_after))
      {
        LoseContact(ctx, base, c, g, now, unseen, true);
      }
      else if (g.calm_after_combat || g.calm_req)
      {
        g.calm_after_combat = g.calm_req = false;
        g.end_search = false;
        g.confirmed = false;
        Set(base, c, g, St::Wary, now,
            fmt::format("combat ended (Rare's), last sighting {:.1f} s ago, after the alarm was over: back to his post, wary",
                        unseen));
      }
      else if (hunting_on)
      {
        StartHunt(ctx, base, c, g, Load32(base, c + kTargetPad), now,
                  fmt::format("combat ended (Rare's), last sighting {:.1f} s ago: hunting, not back to his post", unseen));
      }
      else
      {
        Set(base, c, g, St::Wary, now, fmt::format("combat ended (Rare's), last sighting {:.1f} s ago", unseen));
      }
      break;
    }
    case St::Lost:
      if (!in_combat && (g.calm_after_combat || g.calm_req))
      {
        g.calm_after_combat = g.calm_req = false;
        g.end_search = false;
        g.confirmed = false;
        Set(base, c, g, St::Wary, now, "lost her after the alarm was over: back to his post, wary (no hunt)");
      }
      else if (!in_combat && hunting_on)
      {
        StartHunt(ctx, base, c, g, g.lost_pad, now,
                  g.lost_pad ? "lost contact after a confirmed contact: sweeping from her last pad"
                             : "lost contact, her last pad unknown: sweeping from his own");
      }
      else if (!in_combat && !g.lost_pad)
      {
        Set(base, c, g, St::Wary, now, "no last pad known (his, the squad's or a squad mate's): no SEARCH");
      }
      else if (!in_combat)
      {
        if (GiveSearch(ctx, base, c, g.lost_pad))
        {
          g.meter = float(REXCVAR_GET(pdz_lab_stealth_lost_start));
          g.still_said = false;
          Set(base, c, g, St::LostSearch, now, fmt::format("searching her last pad {:08X}", g.lost_pad));
        }
        else
        {
          Set(base, c, g, St::Wary, now, "could not build the SEARCH task");
        }
      }
      else if (now - g.since > 5.0f)
      {
        g.combat_start = now; // the next try only after another pdz_lab_stealth_lose_s
        Set(base, c, g, St::Combat, now, "his COMBAT script did not end");
        if (REXCVAR_GET(pdz_lab_stealth_lost_metered))
        {
          // Round 5 (item 11, log 025 s11b / s11d): Rare keeps his COMBAT running without a sight of her; until his
          // meter fills again he sees her through it (Rare's instant sight made him seem to know where she was).
          g.stale = true;
          g.meter = float(REXCVAR_GET(pdz_lab_stealth_hunt_floor));
        }
      }
      break;
    case St::Hunting:
      if (in_combat)
      {
        Set(base, c, g, St::Combat, now, "Rare's combat");
        break;
      }
      if (glimpse && g.meter >= Notice(g) && now - g.redirect_at > 5.0f)
      {
        Redirect(ctx, base, c, g, jo, now, in_search);
      }
      // Round 3: no new leg while Rare's sight has her (spotted, or his valid-target bit: his COMBAT is about to start)
      // and not within a second of his last SEARCH update (the r3_c crash came in the frame after three such re-tasks).
      if (!in_search && now - g.leg_started > 2.0f && now - g.search_seen > 1.0f && !g.spotted &&
          (Load32(base, c + kSight) & kValidTarget) == 0)
      {
        const bool ran = g.search_seen >= g.leg_started;
        if (!ran && Logging())
        {
          // Round 3: why a leg never started (hand tests 004 / 005: every such leg was a guard killed 0.5 s before).
          float p[3] = {};
          const bool pos = PadPos(base, g.leg_pad, p);
          const float dx = p[0] - LoadF(base, c + kPosition), dz = p[2] - LoadF(base, c + kPosition + 8);
          REXLOG_INFO("pdz_lab_stealth: {} leg {} at pad {:08X} never started its SEARCH: health {:.0f}, +6272 {:08X} "
                      "(searching bit {}), +6268 {:08X}, in combat {}, pad {} ({:.0f} cm away), scheduler top {:08X}",
                      Id(base, c), g.legs_done + 1, g.leg_pad, LoadF(base, c + kHealth), Load32(base, c + kConfig),
                      (Load32(base, c + kConfig) & kSearching) ? 1 : 0, Load32(base, c + kSight), in_combat ? 1 : 0,
                      pos ? "readable" : "UNREADABLE", pos ? std::sqrt(dx * dx + dz * dz) : -1.0f,
                      Load32(base, c + kScheduler + 4));
        }
        ++g.legs_done;
        g.leg_fails = ran ? 0 : g.leg_fails + 1;
        const int legs = std::max(1, int(REXCVAR_GET(pdz_lab_stealth_hunt_legs)));
        const float pause = float(REXCVAR_GET(pdz_lab_stealth_hunt_pause_s));
        if (g.leg_fails >= 2)
        {
          g.leg_pad = 0;
          g.pause_until = now + pause;
          Set(base, c, g, St::HuntPause, now, "two sweep legs in a row never started a SEARCH: a pause, then another try");
        }
        else if (g.redirect_pad)
        {
          NextLeg(ctx, base, c, g, now, "his leg ended for her glimpse");
        }
        else if (int(g.legs_done) >= legs)
        {
          g.leg_pad = 0;
          g.pause_until = now + pause;
          Set(base, c, g, St::HuntPause, now,
              fmt::format("sweep of {} legs over: a pause at his post for {:.0f} s, then another sweep", g.legs_done, pause));
        }
        else
        {
          NextLeg(ctx, base, c, g, now, ran ? "leg over" : "the leg's SEARCH did not start");
        }
      }
      break;
    case St::HuntPause:
      if (in_combat)
      {
        Set(base, c, g, St::Combat, now, "Rare's combat");
      }
      else if (in_search)
      {
        g.leg_pad = 0;
        g.leg_started = now;
        g.legs_done = 0;
        Set(base, c, g, St::Hunting, now, "Rare's own search during his pause: counted as a sweep leg");
      }
      else if (g.spotted || (Load32(base, c + kSight) & kValidTarget) != 0)
      {
        // Rare's sight has her: his COMBAT decides (round 3), no new leg now.
      }
      else if (glimpse && g.meter >= Notice(g) && now - g.redirect_at > 5.0f)
      {
        Redirect(ctx, base, c, g, jo, now, false);
        if (g.redirect_pad)
        {
          g.legs_done = 0;
          NextLeg(ctx, base, c, g, now, "glimpse during his pause");
        }
      }
      else if (now >= g.pause_until)
      {
        g.legs_done = 0;
        NextLeg(ctx, base, c, g, now, "pause over: sweeping again");
      }
      break;
    case St::Investigating:
      InvTick(ctx, base, c, g, jo, now, in_combat, in_search, glimpse);
      break;
    case St::Dead: // (also: this frame belongs to the alarm's calm-down, above)
      break;
    }
    if (g.st == St::Wary && g.nevermind_until > now)
    {
      NeverMind(ctx, base, c, g, now); // round 4: after a lamp investigation
    }
    if (g.st == St::Investigating && g.call_until > now)
    {
      CallLine(ctx, base, c, g, now); // round 4: his squad call to a body
    }
    if (g.prio)
    {
      PrioLine(ctx, base, c, g, now); // round 6: a body line with priority
    }
    if (g.need_line && (g.st == St::Suspicious || g.st == St::Searching) && now - g.since > 1.0f)
    {
      g.need_line = false;
      if (!((g.last_event == kEvHearUnknown || g.last_event == kEvHearUnknown + 1) && now - g.last_event_time < 3.0f))
      {
        WarnGlimpse(ctx, base, c, g, now, "glimpse (sight takes)");
      }
    }
    if (Hunter(g) && !g.scientist && g.next_bark > 0.0f && now >= g.next_bark && !g.sees &&
        REXCVAR_GET(pdz_lab_stealth_mutter_s) <= 0.0)
    {
      // (Round 3: with pdz_lab_stealth_mutter_s the shared mutter replaces these per-guard call-outs.)
      // "Show yourself!": PLAYER_STILL_HIDDEN_SOLO for a hunter who saw or fought her, else the neutral HEAR_UNKNOWN.
      const float bark = float(REXCVAR_GET(pdz_lab_stealth_hunt_bark_s));
      const auto h = g_hunts.find(HuntKey(g, c));
      if (bark <= 0.0f)
      {
        g.next_bark = 0.0f;
      }
      else if (h != g_hunts.end() && now >= h->second.last_bark && now - h->second.last_bark < bark * 0.5f)
      {
        g.next_bark = now + 2.0f + 3.0f * Rand01(); // a squad mate just called out
      }
      else if ((g.seen_jo || g.fought)    ? Say(ctx, base, c, kEvStillHidden, true, "hunt call-out")
               : HeardLately(g, now)      ? Say(ctx, base, c, kEvHearUnknown, true, "hunt call-out")
                                          : SayUneasy(ctx, base, c, g, now, true, "hunt call-out (he heard nothing: an uneasy line)"))
      {
        if (h != g_hunts.end())
        {
          h->second.last_bark = now;
        }
        g.next_bark = now + bark * (0.75f + 0.5f * Rand01());
      }
      else
      {
        g.next_bark = now + 2.0f + 3.0f * Rand01();
      }
    }
    } // (the rounds 3 to 11l state machine: only with pdz_lab_rareplus off)
    if (Logging())
    {
      const uint32_t setting = (Difficulty(base) << 1) | (TrackingRuns(base) ? 1u : 0u);
      if (setting != g_setting_logged)
      {
        g_setting_logged = setting;
        static const char *const kNames[4] = {"Agent", "Secret Agent", "Perfect Agent", "Dark Agent"};
        REXLOG_INFO("pdz_lab_stealth: difficulty {} ({}), tracking system {}", setting >> 1,
                    (setting >> 1) < 4 ? kNames[setting >> 1] : "?", TrackingText(base));
      }
    }
    if (!rp)
    {
      TryTannoy(ctx, base, now); // (rareplus: our "Security breach! Location unknown!" tannoy is off)
    }
    Mutter(ctx, base, jo, now);
    IncidentTick(ctx, base, now);
    R3cBLampTick(ctx, base, now); // round 3c (B): a shot lamp counts once its light is really out
    LampTalkTick(ctx, base, now); // round 11b: the scientists' lights-out talk for shot-out lamps
    if (g_alarm_deferred && now >= g_alarm_not_before)
    {
      // Round 11g (item 4): an alarm that was raised too soon after the runner's shout: the time is up.
      const uint32_t manager = g_alarm_deferred;
      g_alarm_deferred = 0;
      const Guard *runner = Find(g_alarm_runner);
      const bool alive = runner && !runner->dead && runner->st != St::Dead &&
                         pdz::guestmem::Ok(base, g_alarm_runner, kHealth + 4) && // (Sun 22 Nov night (audit))
                         Load32(base, g_alarm_runner + kData) == runner->data &&
                         LoadF(base, g_alarm_runner + kHealth) > 0.0f;
      if (alive)
      {
        REXLOG_INFO("pdz_lab_stealth: ALARM HELD is raised now: {} is still alive {:.1f} s after his shout started", Id(base, g_alarm_runner),
                    float(REXCVAR_GET(pdz_lab_stealth_alarm_min_s)));
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = manager;
        sub_8214EBD0(frame.ctx, base);
      }
      else
      {
        REXLOG_INFO("pdz_lab_stealth: ALARM HELD is NOT raised: the runner {} was killed or is gone before the {:.1f} s after his "
                    "shout were up: no alarm",
                    runner ? runner->who : std::string("?"), float(REXCVAR_GET(pdz_lab_stealth_alarm_min_s)));
      }
    }
    if (!rp)
    {
      DeathTick(ctx, base, now);
      AllClearTick(ctx, base, now);
    }
    if (Logging())
    {
      // Round 5: his task stack, whenever its depth changes at 4 tasks or more (10 is Rare's limit).
      const int depth = StackDepth(base, c);
      if (depth != g.depth_logged && (depth >= 4 || g.depth_logged >= 4))
      {
        REXLOG_INFO("pdz_lab_stealth: {} (state {}) {}", Id(base, c), StName(g.st), StackText(base, c));
        g.depth_logged = depth;
      }
    }
    if (Logging() && (g.st != St::Unaware || g.meter > 0.5f) && now - g.last_log >= 1.0f)
    {
      g.last_log = now;
      REXLOG_INFO("pdz_lab_stealth: {} meter {:.0f} state {} | {:.0f} cm from Jo | at ({:.0f} {:.0f} {:.0f}) | sees her {}{}{}",
                  Id(base, c), g.meter, StName(g.st), Dist(base, c, jo), LoadF(base, c + kPosition),
                  LoadF(base, c + kPosition + 4), LoadF(base, c + kPosition + 8), g.sees ? 1 : 0,
                  g.scientist ? " (scientist)" : "",
                  Hunter(g) && g.leg_pad ? fmt::format(" | leg {} pad {:08X}", g.legs_done + 1, g.leg_pad)
                  : g.st == St::Investigating
                      ? fmt::format(" | {} leg {} pad {:08X}{} | {:.0f} s left", InvName(g.inv), g.inv_leg, g.inv_pad,
                                    g.inv_arrived > -1e8f ? " (there)" : "", g.inv_until - now)
                      : std::string());
    }
  }

  Guard *Find(uint32_t c)
  {
    const auto it = g_guards.find(c);
    return it == g_guards.end() ? nullptr : &it->second;
  }

  // Round 3 item 6: the music ducks pdz_lab_stealth_duck_db under a guard line let through outside a fight (our
  // multiplier on Rare's category push, voice_volume.cpp; Rare's levels untouched). Once per AI frame, ramped.
  void DuckStep(PPCContext &ctx, uint8_t *base, float now)
  {
    if (now == g_duck_clock)
    {
      return;
    }
    const float dt = std::clamp(now - g_duck_clock, 0.0f, 0.25f);
    g_duck_clock = now;
    voice_volume::FollowVoiceVolume(ctx, base);
    const float depth = std::max(0.0f, float(REXCVAR_GET(pdz_lab_stealth_duck_db)));
    const bool line = std::any_of(g_lines.begin(), g_lines.end(),
                                  [now](const Line &l) { return l.stealth && l.played && l.start <= now && l.end > now; });
    const float want = line ? depth : 0.0f;
    if (want > g_duck_db)
    {
      g_duck_db = std::min(want, g_duck_db + depth / 0.25f * dt);
    }
    else if (want < g_duck_db)
    {
      g_duck_db = std::max(want, g_duck_db - std::max(depth, 1.0f) / 0.6f * dt);
    }
    if (g_duck_db > 0.0f || voice_volume::MusicDuck() < 0.999f)
    {
      voice_volume::SetMusicDuck(ctx, base, std::pow(10.0f, -g_duck_db / 20.0f));
    }
  }

  // A fight is on: a live tracked trooper is in combat or losing contact (the voice budget's combat rule).
  bool Fighting()
  {
    for (const auto &[c, g] : g_guards)
    {
      if (!g.scientist && g.st != St::Dead && (g.st == St::Combat || g.st == St::Lost))
      {
        return true;
      }
    }
    return false;
  }

  // Round 3 item 5: while guards search or hunt (and nobody fights), the nearest of them to Jo mutters a search line
  // every pdz_lab_stealth_mutter_s (x 0.8-1.2), timed from the last guard line, through the take choice and the
  // budget. Lines: a searcher who never saw her HEAR_UNKNOWN_SOLO / SEARCH_START_SOLO / SEARCH_QUERY_TEAM /
  // SEARCH_REPORT_TEAM (neutral takes); a guard who saw or fought her PLAYER_STILL_HIDDEN_SOLO / PLAYER_HIDES_SOLO /
  // SEARCH_QUERY_TEAM / SEARCH_REPORT_TEAM.
  void Mutter(PPCContext &ctx, uint8_t *base, uint32_t jo, float now)
  {
    const float every = float(REXCVAR_GET(pdz_lab_stealth_mutter_s));
    if (every <= 0.0f || Fighting())
    {
      g_mutter_next = -1.0f;
      return;
    }
    if (g_mutter_wait)
    {
      if (now >= g_mutter_wait_at && now - g_mutter_wait_at < 2.5f)
      {
        return; // queued: wait for the real start
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} mutter {} queued but never started (Rare's speech: busy speaker or squad "
                    "arbiter): another searcher tries now",
                    Id(base, g_mutter_wait), EventName(base, g_mutter_wait_ev));
      }
      g_mutter_skip[g_mutter_wait] = now;
      g_mutter_wait = 0;
      g_mutter_next = now;
    }
    const float quiet_since = std::max(g_last_play_end, g_voice_last);
    if (g_mutter_next < 0.0f || g_mutter_next > now + 60.0f || g_mutter_next < quiet_since)
    {
      g_mutter_next = std::max(now, quiet_since) + every * (0.8f + 0.4f * Rand01());
      return;
    }
    if (now < g_mutter_next)
    {
      return;
    }
    uint32_t best = 0;
    float best_d = 1e9f;
    for (const auto &[c, g] : g_guards)
    {
      const bool searching = g.st == St::Searching || g.st == St::LostSearch || g.st == St::Hunting || g.st == St::Investigating ||
                             (RarePlus() && now - g.search_seen < 0.5f && g.combat_script == 0 && !Looking(g, now)); // (Rare's search)
      if (!searching || g.scientist || g.dead || g.sees || !LayerVoice(g.voice))
      {
        continue;
      }
      const auto sk = g_mutter_skip.find(c);
      if (sk != g_mutter_skip.end() && now >= sk->second && now - sk->second < 8.0f)
      {
        continue; // Rare's speech just dropped his mutter: someone else
      }
      const float d = Dist(base, c, jo);
      if (d < best_d)
      {
        best = c;
        best_d = d;
      }
    }
    if (!best)
    {
      g_mutter_next = now + 2.0f; // nobody searching: look again soon
      return;
    }
    Guard &g = g_guards[best];
    const bool knows = g.seen_jo || g.fought || AfterContact(g);
    const uint32_t neutral[] = {kEvHearUnknown, 43, 136, 137};
    const uint32_t hunter[] = {kEvStillHidden, kEvHides, 136, 137};
    // Round 4: a lone investigator (nobody else searching within 15 m) asks nobody "See anything?" (SEARCH_QUERY_TEAM).
    const uint32_t alone[] = {kEvHearUnknown, 43, 137, kEvHearUnknown};
    bool lone = g.st == St::Investigating && g.inv != Inv::Squad;
    for (const auto &[other, o] : g_guards)
    {
      const bool searching = o.st == St::Searching || o.st == St::LostSearch || Hunter(o) || o.st == St::Investigating;
      lone = lone && !(other != best && searching && !o.dead && Dist(base, other, best) < 1500.0f);
    }
    const uint32_t *list = knows ? hunter : lone ? alone : neutral;
    uint32_t ev = list[std::min(3, int(Rand01() * 4.0f))];
    if (ev == g_mutter_ev)
    {
      ev = list[(std::find(list, list + 4, ev) - list + 1) % 4];
    }
    // Round 11l (item 4): HEAR_UNKNOWN only for a searcher who heard something; else the uneasy line.
    const bool unheard = ev == kEvHearUnknown && !HeardLately(g, now);
    if (unheard ? SayUneasy(ctx, base, best, g, now, false, "mutter (he heard nothing: an uneasy line)")
                : Say(ctx, base, best, ev, false, "mutter"))
    {
      g_mutter_ev = ev;
      g_mutter_next = now + every * (0.8f + 0.4f * Rand01());
      g_mutter_wait = best;
      g_mutter_wait_at = now;
      g_mutter_wait_ev = ev;
    }
    else
    {
      g_mutter_skip[best] = now; // refused (the repeat rule, Rare's own gap): another searcher, soon
      g_mutter_next = now + 0.5f + 0.5f * Rand01();
    }
  }
} // namespace

namespace
{
  // pdz_test_stealth_kill (testing): Rare's Kill on the named live guard with Jo as the killer, wrapped in the hit
  // bookkeeping (HitBefore / HitAfter) exactly as Jo's hit handler would run it. Once per new value.
  std::string g_test_kill_done;
  void TestKill(PPCContext &ctx, uint8_t *base, uint32_t jo)
  {
    const std::string want = REXCVAR_GET(pdz_test_stealth_kill);
    if (want == g_test_kill_done)
    {
      return;
    }
    g_test_kill_done = want;
    if (want.empty())
    {
      return;
    }
    for (auto &[c, g] : g_guards)
    {
      if (g.st == St::Dead || g.dead || Id(base, c) != want || !(LoadF(base, c + kHealth) > 0.0f))
      {
        continue;
      }
      const uint32_t vt = Load32(base, c);
      if (!Ptr(vt) || Load32(base, vt + 0x2E4) != 0x8252DE88)
      {
        REXLOG_INFO("pdz_lab_stealth: test kill of {}: not a guard with Rare's Kill (vtable +0x2E4)", want);
        return;
      }
      REXLOG_INFO("pdz_lab_stealth: test kill: Rare's Kill on {} ({:08X}) with Jo as the killer", want, c);
      const float before = lab_stealth::HitBefore(base, c, jo);
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = c;
        frame.ctx.r4.u64 = jo;
        frame.ctx.r5.u64 = 5;
        frame.ctx.r6.u64 = 1;
        sub_8252DE88(frame.ctx, base);
      }
      lab_stealth::HitAfter(base, c, jo, before);
      return;
    }
    REXLOG_INFO("pdz_lab_stealth: test kill: no live tracked guard {}", want);
  }

  // pdz_test_stealth_find_body (testing): the named guard finds the nearest body Jo left, as Rare's SEE_BODY would
  // make him (his line, through the say-event hook's body rule), with Rare's walk to it taken as over.
  // pdz_test_stealth_sound (testing): the named guard hears a sound at Jo's pad: Rare's heard pad and heard-a-sound
  // input, held for a third of a second; the kind goes to the gunfire rule directly.
  void TestSound(uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now)
  {
    const std::string want = REXCVAR_GET(pdz_test_stealth_sound);
    if (want != g_test_sound_done && !want.empty())
    {
      const size_t comma = want.find(',');
      const std::string id = want.substr(0, comma), kind = comma == std::string::npos ? std::string() : want.substr(comma + 1);
      if (Id(base, c) == id)
      {
        g_test_sound_done = want;
        g.test_sound = kind.rfind("loud", 0) == 0 ? 1 : kind.rfind("silenced", 0) == 0 ? 2 : kind.rfind("explosion", 0) == 0 ? 3 : 0;
        g.test_sound_until = now + 0.35f;
        REXLOG_INFO("pdz_lab_stealth: test: {} hears {} at Jo's pad (Rare's heard-a-sound input)", id,
                    g.test_sound == 1 ? "an unsilenced shot" : g.test_sound == 2 ? "a silenced shot" : g.test_sound == 3 ? "an explosion" : "?");
      }
    }
    if (g.test_sound && now < g.test_sound_until)
    {
      const uint32_t profile = ProfileOf(base, c);
      const uint32_t pad = Ptr(jo) ? Load32(base, jo + kPad) : 0;
      if (profile && Ptr(pad))
      {
        Store32(base, profile + kHeardPad, pad);
      }
      Store32(base, c + kStimuli, Load32(base, c + kStimuli) | kStimHeard);
    }
    else
    {
      g.test_sound = 0;
    }
  }

  void TestFindBody(PPCContext &ctx, uint8_t *base, uint32_t jo)
  {
    const std::string want = REXCVAR_GET(pdz_test_stealth_find_body);
    if (want == g_test_body_done)
    {
      return;
    }
    g_test_body_done = want;
    if (want.empty())
    {
      return;
    }
    for (auto &[c, g] : g_guards)
    {
      if (g.st == St::Dead || g.dead || Id(base, c) != want || !(LoadF(base, c + kHealth) > 0.0f))
      {
        continue;
      }
      float me[3];
      PosOf(base, c, me);
      const Kill *best = nullptr;
      for (const Kill &k : g_kills)
      {
        if (!best || Dist3(k.pos, me) < Dist3(best->pos, me))
        {
          best = &k;
        }
      }
      REXLOG_INFO("pdz_lab_stealth: test: {} finds a body ({}): Rare's SEE_BODY line and the body rules", want,
                  best ? fmt::format("{}, {:.0f} cm away", best->who, Dist3(best->pos, me)) : std::string("Jo killed nobody"));
      Say(ctx, base, c, kEvSeeBody, false, "test: finds a body");
      if (best && g.inv_body < 0 && REXCVAR_GET(pdz_lab_stealth_body_rules) && !g.seen_jo && !g.fought &&
          (g.st == St::Unaware || g.st == St::Wary))
      {
        // The say-event's own match takes bodies within 25 m; the tool takes the nearest at any distance.
        g.body_pending = true;
        g.inv_body = int(best - g_kills.data());
      }
      if (g.body_pending)
      {
        const float now = Clock(base);
        g.body_at = now - 3.0f; // Rare's walk to it counts as over
        g.moved_at = now - 2.0f;
      }
      return;
    }
    REXLOG_INFO("pdz_lab_stealth: test: no live tracked guard {} to find a body", want);
  }

  // TESTING (round 7): pdz_test_stealth_observe: a trooper's position, pad, zone, task stack and SEARCH task once a
  // second, stealth on or off. Reads only. A SEARCH task: +132 its go-to child (0 = it is choosing a pad: Rare's
  // update 821DFFF0-821E0160 picks a random pad of his own pad's zone (pad +62), up to 30 tries a frame), +148 a pad
  // given to it, +152 the pad it walks to.
  std::unordered_map<uint32_t, float> g_observed;
  void Observe(const uint8_t *base, uint32_t c)
  {
    const uint32_t voice = Load32(base, c + kVoiceIndex);
    const float hp = LoadF(base, c + kHealth);
    if (voice < 17 || voice > 19 || (Load32(base, c + kConfig) & 0x10) != 0 || !(hp > 0.0f))
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    float &last = g_observed[c];
    if (now - last < 1.0f && now >= last)
    {
      return;
    }
    last = now;
    const uint32_t pad = Load32(base, c + kPad);
    std::string search;
    const int depth = StackDepth(base, c);
    for (int i = 0; i < depth; ++i)
    {
      const uint32_t task = Load32(base, c + kScheduler + kStackEntry * uint32_t(i));
      if (Ptr(task) && Load32(base, task) == kSearchVtable)
      {
        const uint32_t to = Load32(base, task + 152);
        search += fmt::format(" | SEARCH {:08X} status {} age {:.0f} s go-to {:08X} given {:08X} to pad {:08X}", task,
                              Load32(base, task + 12), double(now) - LoadD(base, task + 0x78), Load32(base, task + 132),
                              Load32(base, task + 148), to);
        if (Ptr(to))
        {
          search += fmt::format(" ({:.0f} {:.0f} {:.0f})", LoadF(base, to + 16), LoadF(base, to + 20), LoadF(base, to + 24));
        }
      }
    }
    // His zone's pads as Rare's pick sees them (sub_8216D780's count; the pad list [[82D6BFD8 + 144] + 1112] + 12: +0
    // pad pointers, +4 each zone's first index, +8 the pad total): how many are flagged out (+52 0x80) and how many
    // lie farther from his pad than the pick's least distance (squared distance against the float at 820A06C0).
    if (Ptr(pad))
    {
      const uint32_t mgr = Load32(base, 0x82D6BFD8 + 144);
      const uint32_t list = Ptr(mgr) ? Load32(base, mgr + 1112) + 12 : 0;
      const int zone = int(int16_t(Load32(base, pad + 60) & 0xFFFF));
      const int zones = int(int16_t(Load32(base, 0x82E2F364) >> 16));
      if (Ptr(list) && zone >= 0 && zone < zones && Ptr(Load32(base, list)) && Ptr(Load32(base, list + 4)))
      {
        const uint32_t starts = Load32(base, list + 4), pads = Load32(base, list);
        const uint32_t first = Load32(base, starts + 4 * uint32_t(zone));
        const uint32_t end = zone == zones - 1 ? Load32(base, list + 8) : Load32(base, starts + 4 * uint32_t(zone + 1));
        const float least = LoadF(base, 0x820A06C0);
        int n = 0, flagged = 0, far = 0;
        for (uint32_t i = first; i < end && i < first + 4096; ++i)
        {
          const uint32_t q = Load32(base, pads + 4 * i);
          if (!Ptr(q))
          {
            continue;
          }
          ++n;
          const bool out = (Load32(base, q + 52) & 0x80) != 0;
          flagged += out ? 1 : 0;
          const float dx = LoadF(base, q + 16) - LoadF(base, pad + 16), dy = LoadF(base, q + 20) - LoadF(base, pad + 20),
                      dz = LoadF(base, q + 24) - LoadF(base, pad + 24);
          far += !out && dx * dx + dy * dy + dz * dz > least ? 1 : 0;
        }
        search += fmt::format(" | zone {}: {} pad(s), {} flagged out, {} usable beyond the least distance (squared {:.0f})", zone,
                              n, flagged, far, least);
      }
    }
    REXLOG_INFO("pdz_lab_stealth: observe: {} at ({:.0f} {:.0f} {:.0f}) pad {:08X} zone {} flags {:08X} | {}{}", Id(base, c),
                LoadF(base, c + kPosition), LoadF(base, c + kPosition + 4), LoadF(base, c + kPosition + 8), pad,
                Ptr(pad) ? int(int16_t(Load32(base, pad + 60) & 0xFFFF)) : -1, Load32(base, c + kConfig), StackText(base, c),
                search);
  }
} // namespace

// NPC sight update: decides whether this guard's pass is metered, then runs the state machine.
namespace
{
  void LoudRelease(PPCContext &ctx, uint8_t *base); // (defined with the lamp comments' cue swap, below)

  // Round 11 (item 1): a live, awake character (tracked or not; not Jo) within `radius` of a loud sound, the radius
  // scaled by his hearing factor (setup +0x48) as Rare's gunfire test scales it (8218ABB0). 0: nobody.
  uint32_t LoudHearer(const uint8_t *base, const float pos[3], float radius, bool factor, float &out_d)
  {
    const uint32_t jo = Load32(base, kPlayerOne);
    uint32_t best = 0;
    out_d = 1e9f;
    for (const auto &[c, data] : g_chars)
    {
      if (c == jo || !Ptr(c) || Load32(base, c + kData) != data || (Load32(base, c + kConfig) & kDormant) != 0)
      {
        continue;
      }
      const float hp = LoadF(base, c + kHealth);
      if (!std::isfinite(hp) || !(hp > 0.0f))
      {
        continue;
      }
      const float f = factor && Ptr(data) ? LoadF(base, data + 0x48) : 1.0f;
      const float dx = LoadF(base, c + kPosition) - pos[0], dy = LoadF(base, c + kPosition + 4) - pos[1],
                  dz = LoadF(base, c + kPosition + 8) - pos[2];
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d < radius * (std::isfinite(f) && f > 0.0f ? f : 1.0f) && d < out_d)
      {
        best = c;
        out_d = d;
      }
    }
    return best;
  }
} // namespace

namespace
{
  extern std::atomic<int> g_alarm_snd_n;        // (round 11l: defined with the alarm sound log, below)
  extern std::atomic<uint32_t> g_siren_known;
  void AlarmSoundTick(const uint8_t *base);
  void SirenRetryTick(PPCContext &ctx, uint8_t *base);
} // namespace

namespace
{
  // Mansion helper (Sun 15 Nov): THE MOON on the grounds. Rare's light measure (lab_dark) sums the scene lights
  // (types 0 / 2: the candles) and the ambient; the moon is the level's one directional light (type 1), which it skips,
  // so the moonlit grounds read dark (survey m 5.3). Once a second, for each of the three samples at Jo (feet +20,
  // +100, +170 cm), a segment of 50 m towards the moon is tested with Rare's AI sight ray (SightClear, both ways:
  // walls, towers, roofs block it; foliage may not, UNSURE); each clear sample adds the moon's luma. The moon's colour
  // and direction are read from the level's type 1 light at run time (light list 0x82E19AE0: colour +0x2C0, direction
  // +0x2E4); the survey's values (luma 0.311, towards (0.59, 0.61, 0.53)) if none is found.
  constexpr float kMoonLumaSurvey = 0.311f;
  constexpr float kMoonTowardsSurvey[3] = {0.59f, 0.61f, 0.53f};
  struct Moon
  {
    float at = -1e9f;       // AI clock of the last ray test
    float luma = kMoonLumaSurvey;
    float towards[3] = {kMoonTowardsSurvey[0], kMoonTowardsSurvey[1], kMoonTowardsSurvey[2]};
    uint32_t list = 0;      // the light list the values were read from
    bool from_level = false;
    int clear = 0;          // samples with a clear ray (0..3)
    float extra = 0.0f;
    float log_at = -1e9f, log_f = -1.0f;
    float set_extra = 0.0f, set_reference = 0.0f; // what lab_dark was last handed (its start values are 0 / 0)
  } g_moon;
  void MoonRead(const uint8_t *base)
  {
    const uint32_t first = Load32(base, 0x82E19AE0);
    if (first == g_moon.list)
    {
      return;
    }
    g_moon.list = first;
    g_moon.from_level = false;
    g_moon.luma = kMoonLumaSurvey;
    std::memcpy(g_moon.towards, kMoonTowardsSurvey, sizeof(g_moon.towards));
    const uint32_t count = std::min<uint32_t>(Load32(base, 0x82E3C5F4), 1024);
    uint32_t found = 0;
    float col[3] = {0, 0, 0}, dir[3] = {0, 0, 0};
    for (uint32_t i = 0; i < count && !found; ++i)
    {
      const uint32_t a = Load32(base, 0x82E19AE0 + 4 * i);
      if (Ptr(a) && Load32(base, a + 0x2A8) == 1)
      {
        found = a;
        for (int k = 0; k < 3; ++k)
        {
          col[k] = LoadF(base, a + 0x2C0 + 4 * k);
          dir[k] = LoadF(base, a + 0x2E4 + 4 * k);
        }
      }
    }
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    const float luma = lab_light_level::Luma(col);
    if (found && std::isfinite(len) && len > 0.5f && len < 2.0f && std::isfinite(luma) && luma > 0.01f && luma < 4.0f)
    {
      // The record's direction is the way the light travels (down); towards the moon is the other way (y up).
      const float sign = dir[1] < 0.0f ? -1.0f : 1.0f;
      for (int k = 0; k < 3; ++k)
      {
        g_moon.towards[k] = sign * dir[k] / len;
      }
      g_moon.luma = luma;
      g_moon.from_level = true;
    }
    if (StealthLog())
    {
      REXLOG_INFO("pdz_stealth_light: {}: the moon {} (light {:08X}: colour ({:.3f} {:.3f} {:.3f}), direction ({:.2f} {:.2f} "
                  "{:.2f})): luma {:.3f}, towards ({:.2f} {:.2f} {:.2f}) (survey: 0.311, (0.59 0.61 0.53)); reference {:.2f}",
                  g_level ? g_level->name : "?",
                  g_moon.from_level ? "read from the level's directional light" : "NOT found in the light list: the survey's values",
                  found, col[0], col[1], col[2], dir[0], dir[1], dir[2], g_moon.luma, g_moon.towards[0], g_moon.towards[1],
                  g_moon.towards[2], g_level ? g_level->light_reference : 0.0f);
    }
  }
  // Once per sight update on a zone level with the light term: the moon (at most once a second) and the row's reference
  // are handed to the light measure; then the light at Jo (once a second) and the log line.
  void ZoneLightTick(PPCContext &ctx, uint8_t *base, float now)
  {
    const StealthLevel &row = *g_level;
    const bool moon = row.moon && REXCVAR_GET(pdz_stealth_moon);
    const uint32_t jo = Load32(base, kPlayerOne);
    if (moon && Ptr(jo) && (now - g_moon.at >= 1.0f || now < g_moon.at))
    {
      g_moon.at = now;
      MoonRead(base);
      const float x = LoadF(base, jo + kPosition), y = LoadF(base, jo + kPosition + 4), z = LoadF(base, jo + kPosition + 8);
      int clear = 0;
      for (const float h : {20.0f, 100.0f, 170.0f})
      {
        const float a[3] = {x, y + h, z};
        const float b[3] = {x + 5000.0f * g_moon.towards[0], y + h + 5000.0f * g_moon.towards[1],
                            z + 5000.0f * g_moon.towards[2]};
        clear += SightClear(ctx, base, a, b) ? 1 : 0;
      }
      g_moon.clear = clear;
      g_moon.extra = g_moon.luma * float(clear);
    }
    const float extra = moon ? g_moon.extra : 0.0f;
    if (extra != g_moon.set_extra || row.light_reference != g_moon.set_reference)
    {
      g_moon.set_extra = extra;
      g_moon.set_reference = row.light_reference;
      lab_dark::SetZoneLight(extra, row.light_reference); // (only when it changes: this runs per sight update)
    }
    const float f = lab_dark::Fraction(base, now);
    if (row.moon && StealthLog() && Ptr(jo) && (std::fabs(f - g_moon.log_f) >= 0.1f || now - g_moon.log_at >= 10.0f || now < g_moon.log_at))
    {
      float lit = 0, amb = 0, extra = 0, ref = 0, frac = 0;
      if (lab_dark::LastReading(lit, amb, extra, ref, frac))
      {
        g_moon.log_f = f;
        g_moon.log_at = now;
        REXLOG_INFO("pdz_stealth_light: {}: Jo at ({:.0f} {:.0f} {:.0f}): ambient {:.3f} (x3 {:.2f}), lamps {:.2f}, moon {:.2f} "
                    "({}), light {:.2f} = {:.2f} / reference {:.2f}: meter factor {:.2f}{}",
                    row.name, LoadF(base, jo + kPosition), LoadF(base, jo + kPosition + 4), LoadF(base, jo + kPosition + 8), amb,
                    3.0f * amb, std::max(0.0f, lit - 3.0f * amb - extra), extra,
                    !moon            ? std::string("no moon term")
                    : g_moon.clear == 3 ? std::string("moonlit: 3 of 3 rays clear")
                    : g_moon.clear == 0 ? std::string("in shadow: 0 of 3 rays clear")
                                        : fmt::format("half shadow: {} of 3 rays clear", g_moon.clear),
                    frac, lit, ref, lab_dark::MeterFactor(), lab_dark::RareBlackout() ? " (Rare's blackout: not applied)" : "");
      }
    }
  }
} // namespace

namespace pdz::guardsteps
{
  void Tick(PPCContext &ctx, uint8_t *base, uint32_t character); // guard_footsteps_Mon16Nov.cpp
}
namespace pdz::restored_sounds
{
  void CharacterTick(const uint8_t *base, uint32_t character); // restored_sounds_Tue17Nov.cpp: dead figures, for the body fall
}

REX_HOOK_RAW(sub_82580508)
{
  const uint32_t c = ctx.r3.u32;
  pdz::guardsteps::Tick(ctx, base, c); // every level: a guard who walks without step events gets Rare's step routine
  pdz::restored_sounds::CharacterTick(base, c); // every level: a dead figure is noted for the body-fall sound
  pdz::aim120::CharacterTick(base, c); // aim120_Wed18Nov.cpp: pdz_jo_hit_log (log only): facing of guards holding Jo
  if (g_alarm_snd_n.load(std::memory_order_relaxed) > 0 && REXCVAR_GET(pdz_lab_stealth_log))
  {
    AlarmSoundTick(base); // round 11l (item 1): the alarm sound and the sirens, followed
  }
  if (g_siren_known.load(std::memory_order_relaxed) != 0 && REXCVAR_GET(pdz_lab_siren_retry) && REXCVAR_GET(pdz_lab_guard_stealth) && !lab_stealth::CounterOpGameNow())
  {
    SirenRetryTick(ctx, base); // round 11l (item 1): a siren that was refused is made again while the alarm is on
  }
  if (lab_dark::On())
  {
    lab_dark::Tick(ctx, base, Clock(base));
  }
  else if (ZoneLight())
  {
    // (Nightclub helper: the light at Jo for the meter only, once a second; Mansion helper: the moon and the row's
    // reference; a row without them hands 0 / 0, the light measure as before)
    ZoneLightTick(ctx, base, Clock(base));
  }
  if (REXCVAR_GET(pdz_test_stealth_observe) && Ptr(c) && lab_restorations::CurrentScript() != 0)
  {
    Observe(base, c);
  }
  const bool quiet = LabActive(base);
  if (g_survey_row && Ptr(c))
  {
    SurveyLine(ctx, base, c); // stealth rollout: once per character on a supported level (pdz_stealth_log)
  }
  if (g_loud && !g_loud_released && g_lab_run)
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    LoudRelease(ctx, base); // round 11: once, at the first sight update after Jo went loud
  }
  if (!quiet || !Ptr(c))
  {
    __imp__sub_82580508(ctx, base);
    if (g_loud && g_lab_run && Ptr(c) && REXCVAR_GET(pdz_lab_guard_stealth))
    {
      std::lock_guard<std::recursive_mutex> hold(g_lock);
      LoudTick(ctx, base, c); // round 11k: the alarm run's warning, the stuck scientist, the log
    }
    return;
  }
  const uint32_t voice = Load32(base, c + kVoiceIndex);
  // Stealth rollout: the level's guard / scientist voices (the Laboratory: 17-19 / 15-16, as before), and on a zone
  // level only a character inside the zone (InLayer: always true on the Laboratory). Anyone else is Rare's.
  const bool layer = InLayer(base, c);
  const bool guard = layer && GuardVoice(voice);
  const bool scientist = layer && StaffVoice(voice) && REXCVAR_GET(pdz_lab_stealth_scientists);
  const uint32_t jo = Load32(base, kPlayerOne);
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  const float now = Clock(base);
  AlarmTick(ctx, base, now);
  const uint32_t data = Load32(base, c + kData);
  const uint8_t role = Ptr(data) ? base[data + 0xFD] : 0;
  const float hp = LoadF(base, c + kHealth);
  const bool dormant = (Load32(base, c + kConfig) & kDormant) != 0;
  if (Ptr(jo) && jo != c)
  {
    g_chars[c] = data; // round 11: everyone who could hear a loud shot or an explosion (tracked or not)
  }
  if (Logging() && Ptr(jo) && jo != c)
  {
    // Round 4: one census line per character (and setup) the first time his sight runs in this run.
    const uint64_t key = (uint64_t(c) << 32) | data;
    if (g_census.emplace(key, true).second)
    {
      REXLOG_INFO("pdz_lab_stealth: census: {} ({:08X}) voice {} role {}{} health {:.0f}{} at ({:.0f} {:.0f} {:.0f}): {}",
                  Id(base, c), c, int32_t(voice), int(role), role == kRoleAlarmUser ? " ALARM_USER" : "", hp,
                  dormant ? " DORMANT" : "", LoadF(base, c + kPosition), LoadF(base, c + kPosition + 4),
                  LoadF(base, c + kPosition + 8),
                  guard ? "a trooper: the whole loop" : scientist ? "a scientist: the meter only" : "not tracked (Rare's AI)");
    }
  }
  const auto known = g_guards.find(c);
  if ((!guard && !scientist) || !Ptr(jo) || jo == c)
  {
    if (known != g_guards.end())
    {
      // Round 4 (log 025, s101): a character we don't track now lives in a tracked guard's memory.
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} (voice {}) now lives in the memory of {} (state {}): not tracked, the old entry "
                    "is dropped",
                    Id(base, c), int32_t(voice), known->second.who, StName(known->second.st));
      }
      for (Kill &k : g_kills)
      {
        k.victim = k.victim == c ? 0 : k.victim;
        k.finder = k.finder == c ? 0 : k.finder;
      }
      g_guards.erase(known);
    }
    __imp__sub_82580508(ctx, base);
    return;
  }
  if (known != g_guards.end() &&
      ((known->second.data && known->second.data != data) ||
       (known->second.st == St::Dead && hp > 0.0f && std::isfinite(hp) && now - known->second.since > 1.0f)))
  {
    // Round 4: another character in this memory (another setup, or alive in a dead guard's memory): a new entry.
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} is a new character in the memory of {} (state {}, setup {:08X} -> {:08X}, health "
                  "{:.0f}): he joins the loop as unaware",
                  Id(base, c), known->second.who, StName(known->second.st), known->second.data, data, hp);
    }
    for (Kill &k : g_kills)
    {
      k.victim = k.victim == c ? 0 : k.victim;
      k.finder = k.finder == c ? 0 : k.finder;
    }
    g_guards.erase(known);
  }
  const bool fresh = g_guards.find(c) == g_guards.end();
  Guard &g = g_guards[c];
  if (fresh)
  {
    g.data = data;
    g.who = Id(base, c);
    // Round 11i (item 3): his post: where he is when the stealth code first sees him (the level's start).
    PosOf(base, c, g.post_pos);
    g.post_yaw = LoadF(base, c + 0x1670);
    g.post_pad = Ptr(Load32(base, c + kPad)) ? Load32(base, c + kPad) : 0;
  }
  g.role = role;
  g.scientist = scientist;
  g.voice = voice;
  if (dormant && g.st != St::Dead)
  {
    // Round 4: Rare's dormant bit (SetDormant): not in the loop while he sleeps, never "dead" for it.
    if (!g.dormant_seen && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} is dormant (Rare's sleep bit, health {:.0f}): not in the stealth loop until he wakes",
                  Id(base, c), hp);
    }
    g.dormant_seen = true;
    g.metered = false;
    __imp__sub_82580508(ctx, base);
    return;
  }
  // Overnight Mon 16 / Tue 17 Nov (item 4): Rare's unarmed truce (the Nightclub's front-door bouncers, now under the
  // layer): while he is passive towards an empty-handed Jo, unprovoked and calm, he is Rare's own, as a dormant guard
  // is: no meter, no layer rule. The layer takes him the moment that ends (a weapon in her hand, the script's
  // "ignored the warning", a provocation).
  if (R6TruceTick(base, c, g, jo, now))
  {
    g.metered = false;
    __imp__sub_82580508(ctx, base);
    return;
  }
  if (g.dormant_seen && g.st == St::Unaware && !g.alive_seen && Logging())
  {
    REXLOG_INFO("pdz_lab_stealth: {} woke (Rare's sleep bit cleared, health {:.0f}): he joins the stealth loop, unaware",
                Id(base, c), hp);
  }
  // Round 3 (hand tests 004 07:03:00, 005 07:08:04: a killed guard went hunting and asked for the tannoy): on death his
  // stealth state ends at once. Dead = Jo's hit handler saw his health reach 0, or his health (+120) is 0 after it was
  // above 0 (any other killer). From then on his sight passes are Rare's, and nothing of ours runs for him.
  if (std::isfinite(hp) && hp > 0.0f && !g.dead)
  {
    g.alive_seen = true;
  }
  if (g.st != St::Dead && (g.dead || (g.alive_seen && !(hp > 0.0f))))
  {
    if (!g.dead)
    {
      R5ADown(base, c, g, now, hp); // (round 5, fix 6: down with no kill of Jo's counted, e.g. a melee: logged, a body)
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} died (health {:.0f}) in state {}{}: his stealth state ends (no hunting, sweep, "
                  "tannoy or lines)",
                  Id(base, c), hp, StName(g.st),
                  g.inv == Inv::Body ? " (investigating a body alone: nothing is passed on, the body waits for the next guard)"
                  : g.inv != Inv::None ? fmt::format(" ({} investigation)", InvName(g.inv))
                                       : std::string());
    }
    StaffWitness(ctx, base, c, now); // follow-up Sun 15 Nov (item 11): a scientist who saw it is treated as having seen Jo
    if (g.inv_body >= 0 && size_t(g.inv_body) < g_kills.size() && g_kills[g.inv_body].finder == c)
    {
      g_kills[g.inv_body].finder = 0;
    }
    if (g.inv != Inv::None && g.inv != Inv::Squad)
    {
      AreaLost(base, c); // round 7: the investigator never came back
    }
    g.inv = Inv::None;
    g.inv_pad = 0;
    g.calm_req = g.calm_after_combat = g.end_search = false;
    g.st = St::Dead;
    g.dead = true;
    g.since = now;
    g.leg_pad = g.redirect_pad = 0;
    g.next_bark = 0.0f;
    g.body_pending = g.need_line = g.spotted = g.end_leg = false;
    g.combat_script = 0;
  }
  if (g.st == St::Dead)
  {
    g.metered = false;
    __imp__sub_82580508(ctx, base);
    return;
  }
  SampleJo(base, jo, now);
  DuckStep(ctx, base, now);
  const float frame_dt = std::clamp(now - g.last_frame, 0.0f, 0.5f);
  g.last_frame = now;
  g.metered = !g.spotted && !HurtKnown(g) /* (item 16: hurt by Jo: Rare's own sight) */ && ((g.st != St::Combat && g.st != St::Lost) || LostLike(g));
  __imp__sub_82580508(ctx, base);
  if (now < g.squad_checked || now - g.squad_checked >= 5.0f)
  {
    g.squad_checked = now; // squad mates for the kill rule, pairs and the plan (sub_8217D108)
    g.squad = SquadOf(ctx, base, c);
    if (g.scientist)
    {
      // Round 11j (item 4): which of Rare's two scientist sets he speaks: his voice number's, or SCI1 when
      // pdz_lab_lights_out_sci1 names him (s2s1: the hub pair are two men, not one voice twice).
      g.sci_set1 = restorations::SpeaksSci1(base, c);
    }
  }
  After(ctx, base, c, g, jo, now, frame_dt);
  if (const std::string lamp = REXCVAR_GET(pdz_test_stealth_lamp); lamp != g_test_lamp_done)
  {
    g_test_lamp_done = lamp;
    float pos[3] = {};
    if (std::sscanf(lamp.c_str(), "%f,%f,%f", &pos[0], &pos[1], &pos[2]) == 3)
    {
      REXLOG_INFO("pdz_lab_stealth: test: a lamp goes out at ({:.0f} {:.0f} {:.0f}) (no lamp is touched)", pos[0], pos[1], pos[2]);
      lab_stealth::LightIncident(ctx, base, pos, 1);
      return; // (the incident may have changed this guard's entry)
    }
  }
  {
    static std::string front_done;
    if (const std::string want = REXCVAR_GET(pdz_test_stealth_lamp_front); want != front_done)
    {
      front_done = want;
      const size_t comma = want.find(',');
      float cm = 0.0f, side = 0.0f;
      if (comma != std::string::npos && std::sscanf(want.c_str() + comma + 1, "%f,%f", &cm, &side) >= 1)
      {
        const std::string id = want.substr(0, comma);
        for (auto &[oc, og] : g_guards)
        {
          if (og.st == St::Dead || og.dead || Id(base, oc) != id)
          {
            continue;
          }
          float eye[3];
          EyeOf(base, oc, og, now, eye);
          float fx = og.dir[0], fz = og.dir[2];
          const char *how = "Rare's facing vector at his last sight test";
          if (!(now >= og.eye_at && now - og.eye_at < 3.0f) || fx * fx + fz * fz < 1e-6f)
          {
            const float yaw = LoadF(base, oc + 0x1670);
            fx = std::sin(yaw);
            fz = std::cos(yaw);
            how = "his yaw (+0x1670)";
          }
          const float fl = std::sqrt(fx * fx + fz * fz);
          fx /= fl;
          fz /= fl;
          float pos[3] = {eye[0] + fx * cm + fz * side, eye[1], eye[2] + fz * cm - fx * side};
          REXLOG_INFO("pdz_lab_stealth: test: a lamp goes out {:.0f} cm in front of {} (side {:+.0f}) at ({:.0f} {:.0f} {:.0f}); "
                      "his eye ({:.0f} {:.0f} {:.0f}), facing ({:.2f} {:.2f}) from {}, state {} (no lamp is touched)",
                      cm, id, side, pos[0], pos[1], pos[2], eye[0], eye[1], eye[2], fx, fz, how, StName(og.st));
          lab_stealth::LightIncident(ctx, base, pos, 1);
          return; // (the incident may have changed this guard's entry)
        }
        REXLOG_INFO("pdz_lab_stealth: test: no live tracked guard {} for pdz_test_stealth_lamp_front", id);
      }
    }
  }
  TestSound(base, c, g, jo, now);
  TestProbe(ctx, base);
  TestBehind(ctx, base, jo, now);
  TestKill(ctx, base, jo);
  R3cCTestWitness(base, now);        // round 3c C (testing): pdz_test_stealth_witness (sets up the kill_at below)
  R3cCTestWhen(ctx, base, now);      // round 3c C (testing): pdz_test_stealth_witness_when (the same)
  R3BTestKillAt(ctx, base, jo, now); // round 3b (testing): pdz_test_stealth_kill_at
  R3cATestKillJo(base, jo, now);     // round 3c A (testing): pdz_test_stealth_kill_jo (the next hit on Jo is her death)
  TestFindBody(ctx, base, jo);
  R3cDTestRadio(base, now); // round 3c D (testing): pdz_test_stealth_radio
  if (const std::string pin = REXCVAR_GET(pdz_test_stealth_pin); !pin.empty() && g.who == pin && g.st != St::Dead)
  {
    // TESTING: held where he stands.
    if (!g.pinned)
    {
      g.pinned = true;
      PosOf(base, c, g.pin_pos);
      REXLOG_INFO("pdz_lab_stealth: test: {} is held at ({:.0f} {:.0f} {:.0f}) from now on (pdz_test_stealth_pin)", g.who,
                  g.pin_pos[0], g.pin_pos[1], g.pin_pos[2]);
    }
    for (int i = 0; i < 3; i += 2) // x and z; his height is left to the game
    {
      uint32_t bits;
      std::memcpy(&bits, &g.pin_pos[i], 4);
      Store32(base, c + kPosition + 4 * uint32_t(i), bits);
    }
  }
  else
  {
    g.pinned = false;
  }
}

namespace
{
  // Round 3 item 7: Laboratory sight blocks (cm, level axes). Rare's sight ray (sub_82189F18 -> sub_8216D3A8 /
  // sub_82259B80, hitmask 0x20 PDZ_HitmaskBlockAiSight, one ray target -> eye) is blocked only by surfaces whose
  // material carries 0x20. Research (round 3 stairwell_sight.md, the level collision decoded offline): from s1m1's post
  // (1594 -65 -3686, gold door 9) to the stair head the ray passes just north of the stairwell's east wall and corner
  // pillar (both 0x20) and crosses the bridge's east edge at x 545, where the only collision is a handrail and an
  // invisible fall-guard plane (material 149, mask 0x0A5286: blocks the player and AI movement, not AI sight). So
  // Rare's own test says "visible" there with the stealth code off too. An entry: when Jo's target point is inside
  // `zone` and the eye -> target segment crosses `box`, the test answers "not visible".
  struct SightBlock
  {
    float zone_lo[3], zone_hi[3]; // Jo's target point inside this box
    float box_lo[3], box_hi[3];   // the occluder the segment must cross
    const char *what;
  };
  const SightBlock kSightBlocks[] = {
      {{230, 560, -4820}, {525, 800, -4515}, {540, 600, -4560}, {550, 1150, -4320},
       "bridge east edge above the stair head (fall-guard plane x 545)"},
  };
  uint32_t g_sight_blocked = 0;
  float g_sight_block_log = -1e9f;

  bool InBox(const float p[3], const float lo[3], const float hi[3])
  {
    for (int i = 0; i < 3; ++i)
    {
      if (p[i] < lo[i] || p[i] > hi[i])
      {
        return false;
      }
    }
    return true;
  }
  // Segment a -> b against an axis-aligned box (slab test).
  bool SegmentHitsBox(const float a[3], const float b[3], const float lo[3], const float hi[3])
  {
    float t0 = 0.0f, t1 = 1.0f;
    for (int i = 0; i < 3; ++i)
    {
      const float d = b[i] - a[i];
      if (std::fabs(d) < 1e-6f)
      {
        if (a[i] < lo[i] || a[i] > hi[i])
        {
          return false;
        }
        continue;
      }
      float u = (lo[i] - a[i]) / d, v = (hi[i] - a[i]) / d;
      if (u > v)
      {
        std::swap(u, v);
      }
      t0 = std::max(t0, u);
      t1 = std::min(t1, v);
      if (t0 > t1)
      {
        return false;
      }
    }
    return true;
  }
  const SightBlock *SightBlocked(const float eye[3], const float at[3])
  {
    if (!REXCVAR_GET(pdz_lab_stealth_sight_block) || ZoneLevel()) // (the boxes are Laboratory coordinates)
    {
      return nullptr;
    }
    for (const SightBlock &b : kSightBlocks)
    {
      if (InBox(at, b.zone_lo, b.zone_hi) && SegmentHitsBox(eye, at, b.box_lo, b.box_hi))
      {
        return &b;
      }
    }
    return nullptr;
  }
  void LogSightBlock(const uint8_t *base, uint32_t viewer, const SightBlock &b, const float eye[3], const float at[3],
                     float now)
  {
    ++g_sight_blocked;
    if (Logging() && (now - g_sight_block_log >= 1.0f || now < g_sight_block_log))
    {
      g_sight_block_log = now;
      REXLOG_INFO("pdz_lab_stealth: sight block ({}): {} would have seen Jo, eye ({:.0f} {:.0f} {:.0f}) -> ({:.0f} {:.0f} "
                  "{:.0f}): not visible ({} blocked tests so far)",
                  b.what, Ptr(viewer) ? Id(base, viewer) : std::string("?"), eye[0], eye[1], eye[2], at[0], at[1], at[2],
                  g_sight_blocked);
    }
  }
} // namespace

// One viewer / target test. Rare runs these in the perception tick sub_8218A810 (a queue of up to 5 viewers per frame,
// results kept in the per-pair seen bitsets that sub_82189100 reads), NOT inside the NPC sight update, which only reads
// the bitsets back (sub_8218B498). So the viewer is identified here from the stack and looked up: a metered guard's test
// of Jo goes through the meter, and Jo stays hidden from him until it is full.
namespace
{
  thread_local float t_sight_range2 = 0.0f; // inside Rare's sight test: its squared range (f1), for the concealment
  struct SightRangeScope
  {
    explicit SightRangeScope(float r2) { t_sight_range2 = r2; }
    ~SightRangeScope() { t_sight_range2 = 0.0f; }
  };
} // namespace

// Follow-up Sun 15 Nov (item 10): close range. Lab 11:29:12: Jo stood 1.2-2 m from a scientist for 1.5 s and his meter
// went 1 -> 3, because the room was dim.
REXCVAR_DEFINE_BOOL(pdz_stealth_close_range, true, "Gameplay",
                    "Stealth layer: when Jo is within pdz_stealth_close_range_cm of a guard or scientist and in his "
                    "view, the light no longer hides her: his meter fills to full in about "
                    "pdz_stealth_close_range_fill_s (a little faster when she stands or moves; a crouched, still Jo "
                    "at the edge of the range gets about twice as long). Behind him (more than "
                    "pdz_stealth_close_range_view_deg off the way he looks) does not count as in view. Off = the "
                    "light and her stance slow the meter at any distance, as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_close_range_cm, 300.0, "Gameplay",
                      "pdz_stealth_close_range: the distance (cm) from his eye within which the rule applies")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_close_range_fill_s, 0.5, "Gameplay",
                      "pdz_stealth_close_range: the time (s) in which his meter goes from empty to full")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_stealth_close_range_view_deg, 90.0, "Gameplay",
                      "pdz_stealth_close_range: in his view = within this many degrees of the way he looks (the "
                      "direction Rare's sight test uses)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(bool, pdz_ai_cover_fixes); // (ai_cover_fixes.cpp: named in the combat note below)

#if PDZ_COUNTEROP
// Counter-Op round 14 (Tue 17 Nov): counter_op_unheard.cpp's rules for these three routines (its own hooks until the
// merge with the release candidate; one hook only can exist). Each answers at once when no Counter-Op game runs.
bool PdzCounterOpSightAnswered(PPCContext &ctx, uint8_t *base);     // true: answered 'not seen' in r3
bool PdzCounterOpSoundEventDropped(PPCContext &ctx, uint8_t *base); // true: the sound event is dropped
int PdzCounterOpHearingMute(uint8_t *base, uint32_t muted[8]);
void PdzCounterOpHearingUnmute(uint8_t *base, const uint32_t muted[8], int n);
#endif

REX_HOOK_RAW(sub_82189F18)
{
#if PDZ_COUNTEROP
  if (PdzCounterOpSightAnswered(ctx, base))
  {
    return;
  }
#endif
  const SightRangeScope range_scope(float(ctx.f1.f64));
  R8SightScope r8_scope(ctx, base); // (Tue 17 Nov night, fix2: every test of Jo goes into the record)
  if ((!g_lab && !(g_loud && g_lab_run && Clock(base) < g_loud_commit_until)) || !REXCVAR_GET(pdz_lab_guard_stealth))
  {
    __imp__sub_82189F18(ctx, base);
    return;
  }
  const uint32_t sp = ctx.r1.u32;
  const uint32_t target = Load32(base, sp + 0x6C);
  const uint32_t jo = Load32(base, kPlayerOne);
  if (target != jo || !Ptr(jo))
  {
    __imp__sub_82189F18(ctx, base);
    return;
  }
  const uint32_t viewer = Load32(base, sp + 0x64);
  r8_scope.viewer = viewer;
  r8_scope.jo = true;
  const uint32_t eye_p = ctx.r9.u32, dir_p = ctx.r10.u32, at_p = Load32(base, sp + 0x54);
  bool metered = false, committed = false, holds = false;
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    Guard *g = Ptr(viewer) ? Find(viewer) : nullptr;
    metered = g && g->metered;
    committed = g && g->run_committed;
    holds = g && (R8Holds(*g) || g->r9_rares); // (fix2, item 1: the layer holds him as having seen Jo; Wed 18 Nov: or he is Rare's guard)
    // Follow-up Sun 15 Nov (item 4): THE METER NEVER REACHES A GUARD IN RARE'S COMBAT. `metered` is set once a frame
    // by his sight update, so it could be a frame old, and "in combat" was lost while Rare's COMBAT script stood
    // under its cover task for over a minute or had started before he was tracked. Here, live: with pdz_lab_rareplus
    // on, a guard whose COMBAT script runs (ours, or on his task stack) gets Rare's own answer.
    if (metered && RarePlus() && (g->combat_script != 0 || CombatOnStack(base, viewer)))
    {
      metered = false;
      ++g_combat_exempt;
    }
    if (g && !g_combat_note && (Logging() || StealthLog()))
    {
      g_combat_note = true; // once per level load
      REXLOG_INFO("pdz_stealth: COMBAT AND THE METER (checked from the code, Sun 15 Nov): pdz_lab_rareplus is {}. A guard in "
                  "Rare's COMBAT (his COMBAT script {:08X} running on his task stack, whatever is above it, the cover task "
                  "82092950 included) is never metered: every sight test of Jo he makes (sub_82189F18) gets Rare's own "
                  "answer, from the frame the COMBAT starts. The cover task's own tests (sub_821CEE88: \"no peek point "
                  "has seen the target for 5 s\") do not use that sight test at all: they are Rare's ray tests from the "
                  "cover and peek points (sub_82199EB8 / sub_82199DE8), which no stealth hook touches. pdz_ai_cover_fixes "
                  "({}) only writes the cover task's own fields (grace timers, peek roll, snap-back count): no clash with "
                  "the meter",
                  RarePlus() ? "on" : "OFF (the rounds 3-11l rules: a guard who lost contact is metered again)", kCombatVtable,
                  REXCVAR_GET(pdz_ai_cover_fixes) ? "on" : "off");
    }
    if (g && Ptr(eye_p) && Ptr(dir_p))
    {
      for (int i = 0; i < 3; ++i)
      {
        g->eye[i] = LoadF(base, eye_p + 4 * i); // round 5: his eye and facing, for the body sight
        g->dir[i] = LoadF(base, dir_p + 4 * i);
      }
      g->eye_at = Clock(base);
      // Round 3c (helper C, fix 4): Rare's own cone for him, as this test was asked (f1 range squared, f2 / f5 the apex
      // set back behind his eye, f3 1 / cos of the half angle, f4 the up / down slope limit, r3 / r4 the cone flags).
      R3cCSightArgs(viewer, float(ctx.f1.f64), float(ctx.f2.f64), float(ctx.f3.f64), float(ctx.f4.f64), float(ctx.f5.f64),
                    (ctx.r3.u32 & 0xFF) != 0, (ctx.r4.u32 & 0xFF) != 0, g->eye_at);
      SightFacingLog(ctx, base, viewer, *g); // round 11l (item 3)
    }
  }
  if (committed)
  {
    // Round 11h (item 1): a committed alarm runner: a new sighting must not take him off his run.
    R8RareSight(ctx, base);
    R8Veto("an alarm runner on his run (round 11h)");
    ctx.r3.u64 = 0;
    return;
  }
  if (holds)
  {
    // Tue 17 Nov night (fix2, item 1): ONE TRUTH. The layer holds him as having seen Jo: Rare's own answer, with
    // nothing of ours in it (no sight block; the concealment hook adds no darkness range for him).
    t_r8_holds = true;
    R8RareSight(ctx, base);
    return;
  }
  if (!g_lab)
  {
    R8RareSight(ctx, base); // (after LOUD: only the committed runner above is ours)
    return;
  }
  if (!metered || !Ptr(eye_p) || !Ptr(dir_p) || !Ptr(at_p))
  {
    float e[3] = {}, a[3] = {};
    const bool pts = Ptr(eye_p) && Ptr(at_p);
    for (int i = 0; pts && i < 3; ++i)
    {
      e[i] = LoadF(base, eye_p + 4 * i);
      a[i] = LoadF(base, at_p + 4 * i);
    }
    R8RareSight(ctx, base);
    if (pts && (ctx.r3.u32 & 0xFF) != 0)
    {
      if (const SightBlock *b = SightBlocked(e, a))
      {
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        LogSightBlock(base, viewer, *b, e, a, Clock(base));
        R8Veto("a Laboratory sight block");
        ctx.r3.u64 = 0;
      }
    }
    // (Tue 17 Nov night, fix2: the record of this test is R8SightScope's, for every test of Jo)
    return;
  }
  float eye[3], dir[3], at[3];
  for (int i = 0; i < 3; ++i)
  {
    eye[i] = LoadF(base, eye_p + 4 * i);
    dir[i] = LoadF(base, dir_p + 4 * i);
    at[i] = LoadF(base, at_p + 4 * i);
  }
  const float range2 = float(ctx.f1.f64);
  R8RareSight(ctx, base);
  if (REXCVAR_GET(pdz_test_stealth_blind))
  {
    R8Veto("test tool: pdz_test_stealth_blind");
    ctx.r3.u64 = 0; // TESTING: he never sees her
    return;
  }
  bool visible = (ctx.r3.u32 & 0xFF) != 0;
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  if (visible)
  {
    if (const SightBlock *b = SightBlocked(eye, at))
    {
      LogSightBlock(base, viewer, *b, eye, at, Clock(base));
      R8Veto("a Laboratory sight block");
      ctx.r3.u64 = 0;
      visible = false;
    }
  }
  Guard *g = Find(viewer);
  if (!g)
  {
    return;
  }
  const float now = Clock(base);
  const float dt = std::clamp(now - g->last_test, 0.0f, 1.0f);
  g->last_test = now;
  std::memcpy(g->face, dir, sizeof(dir)); // (the test tools place Jo behind him)
  g->face_at = now;
  if (!visible)
  {
    return;
  }
  if (R4ASneakHides(base, viewer, *g, jo, eye, dir, at, now))
  {
    R8Veto("the sneak rule (she creeps up behind him)");
    ctx.r3.u64 = 0; // (stealth4, fix 5: an all-round answer of Rare's while she creeps behind him)
    return;
  }
  g->last_visible = now;
  {
    // Wed 18 Nov night (rule 2): PLAIN VIEW. Rare's own test passed and she is close or lit: he is Rare's guard at
    // once and Rare's answer stands. The meter below is only for a glimpse in the dark at range.
    float pv_d = 0.0f;
    const char *plain = R9PlainView(eye, at, pv_d);
    if (plain && R9Make(base, viewer, *g, 2,
                        fmt::format("{} ({:.0f} cm, light {:.2f})", plain, pv_d, LightTerm() ? lab_dark::MeterFactor() : 1.0f), now))
    {
      AlarmSighting(base, viewer, now);
      g_any_sighting = true;
      return; // Rare's answer (visible) stands
    }
  }
  bool crouched = false;
  float f = Exposure(ctx, base, viewer, jo, eye, dir, at, range2, crouched);
  // Round 7 (item 5; log 048 07:01:01, s11a took 3.5 s to notice Jo 3 m in front of him in the dark): a floor on what
  // a guard notices close in front of him (within 60 degrees of his facing), whatever the light and her stance.
  const char *close = "";
  {
    const float tx = at[0] - eye[0], ty = at[1] - eye[1], tz = at[2] - eye[2];
    const float td = std::sqrt(tx * tx + ty * ty + tz * tz);
    const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    const bool front = td < 1.0f || (dl > 1e-4f && (tx * dir[0] + ty * dir[1] + tz * dir[2]) / (td * dl) >= 0.5f);
    const float close_cm = float(REXCVAR_GET(pdz_lab_stealth_close_cm));
    const float mid_cm = float(REXCVAR_GET(pdz_lab_stealth_mid_cm));
    const float mid_floor = std::clamp(float(REXCVAR_GET(pdz_lab_stealth_mid_floor)), 0.0f, 1.0f);
    if (front && close_cm > 0.0f && td <= close_cm)
    {
      g->meter = std::max(g->meter, Threshold(*g));
      close = " (the close-range floor: at once)";
    }
    else if (front && mid_cm > 0.0f && td <= mid_cm && f < mid_floor)
    {
      f = mid_floor;
      close = " (the close-range floor: half speed at least)";
    }
  }
  // Follow-up Sun 15 Nov (item 10): CLOSE RANGE (pdz_stealth_close_range). Within pdz_stealth_close_range_cm and in
  // his view (within pdz_stealth_close_range_view_deg of the direction Rare's sight test uses; Rare's own test also
  // says "visible" for a Jo right behind him at arm's length, which stays slow), the light term and the other
  // exposure factors no longer slow the meter: it gains 100 in pdz_stealth_close_range_fill_s. Standing or moving:
  // 1.4 times that. Crouched and still: that speed within two thirds of the range, falling to half of it at the
  // range's edge (at 3 m she still gets a moment, at 1-2 m not).
  float close_gain = 0.0f; // meter per second, 0 = the rule does not apply
  if (REXCVAR_GET(pdz_stealth_close_range))
  {
    const float cr_cm = float(REXCVAR_GET(pdz_stealth_close_range_cm));
    const float fill_s = std::max(0.05f, float(REXCVAR_GET(pdz_stealth_close_range_fill_s)));
    const float tx = at[0] - eye[0], ty = at[1] - eye[1], tz = at[2] - eye[2];
    const float td = std::sqrt(tx * tx + ty * ty + tz * tz);
    const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    const float min_cos = std::cos(std::clamp(float(REXCVAR_GET(pdz_stealth_close_range_view_deg)), 0.0f, 180.0f) * 0.01745329f);
    const bool in_view = td < 1.0f || (dl > 1e-4f && (tx * dir[0] + ty * dir[1] + tz * dir[2]) / (td * dl) >= min_cos);
    if (cr_cm > 0.0f && td <= cr_cm && in_view)
    {
      const bool still_crouch = crouched && g_jo_speed < 0.1f * kStillSpeed;
      const float inner = cr_cm * (2.0f / 3.0f);
      const float pace = !still_crouch ? 1.4f : td <= inner ? 1.0f : 1.0f - 0.5f * (td - inner) / std::max(1.0f, cr_cm - inner);
      close_gain = 100.0f * pace / fill_s;
      if (!*close)
      {
        close = still_crouch ? " (close range, in his view: the light does not hide her; crouched and still)"
                             : " (close range, in his view: the light does not hide her)";
      }
    }
  }
  // Round 10 (hand test 033, 19:11:43: a guard searching her pad after a glimpse took 1.7 s to know her at 5.4 m):
  // a guard on the suspicious route (looking for Jo at her own pad) fills pdz_lab_stealth_expecting_mult as fast.
  // Round 11c (item 4; logs 002 18:00:30, 005 19:17:49 and 19:24:25: searching guards made her out at 16-21 m while
  // she crouched in poor light): only while she is within pdz_lab_stealth_expecting_cm of his eye.
  const float ex_cm = float(REXCVAR_GET(pdz_lab_stealth_expecting_cm));
  const float ex_d = std::sqrt((at[0] - eye[0]) * (at[0] - eye[0]) + (at[1] - eye[1]) * (at[1] - eye[1]) +
                               (at[2] - eye[2]) * (at[2] - eye[2]));
  const bool expecting = now < g->expect_until && (ex_cm <= 0.0f || ex_d <= ex_cm) &&
                         (g->st == St::Suspicious || g->st == St::Searching ||
                          (g->st == St::Investigating && g->inv == Inv::Noise));
  const float rate = float(REXCVAR_GET(pdz_lab_stealth_rate)) *
                     (Hunter(*g) || LostLike(*g) ? std::max(0.0f, float(REXCVAR_GET(pdz_lab_stealth_hunt_rate)))
                      : now < g->alert_until ? std::max(1.0f, float(REXCVAR_GET(pdz_lab_stealth_alert_rate)))
                                             : 1.0f) *
                     (expecting ? std::max(1.0f, float(REXCVAR_GET(pdz_lab_stealth_expecting_mult))) : 1.0f);
  const float meter_was = g->meter; // (round 3 item 7: R3WBoostCap before the step, R3WFarDark after it)
  g->meter = std::min(100.0f, g->meter + std::max(R3WBoostCap(base, viewer, *g, rate, ex_d, now) * f, close_gain) * dt); // (close_gain: item 10, close range)
  R3WFarDark(base, viewer, *g, f, ex_d, meter_was, close_gain, now);
  const float dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
  const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist <= float(REXCVAR_GET(pdz_lab_stealth_near_cm)) && !(Load32(base, viewer + kAiFlags) & kDarkAim) &&
      (!LightTerm() || lab_dark::RareBlackout() || lab_dark::MeterFactor() > 0.65f))
  {
    g->meter = std::max(g->meter, Threshold(*g)); // a clear look within near_cm in light: no suspicion stage
  }
  if (LayerGuard(*g))
  {
    // Round 11d (item 1; log 007 19:49: s1m1's "huh?" was due at 19:49:04, waited 5.9 s for a gap and played at
    // 19:49:10.3; he made her out at 19:49:13.3, 3 s after she first heard anything). A guard cannot fully see her
    // before his warning has been heard: while a warning line of his waits to play (8 s at most from when it was
    // due) and for pdz_lab_stealth_warn_hold_gap_s after it really started, his meter stops at
    // pdz_lab_stealth_warn_hold_meter (5 under his sighting level at most).
    const bool waiting = g->warn_ev && now >= g->warn_from && now - g->warn_from < 8.0f;
    // Round 11e: close range is exempt: within pdz_lab_stealth_warn_hold_min_cm of his eye there is no hold.
    const float hold_min = float(REXCVAR_GET(pdz_lab_stealth_warn_hold_min_cm));
    const bool face_to_face = (hold_min > 0.0f && dist <= hold_min) || close_gain > 0.0f; // (item 10: no hold at close range)
    const bool holding = REXCVAR_GET(pdz_lab_stealth_warn_hold) && !face_to_face && (waiting || now < g->warn_free_at);
    const float cap = std::min(float(REXCVAR_GET(pdz_lab_stealth_warn_hold_meter)), Threshold(*g) - 5.0f);
    if (holding && g->meter > cap)
    {
      if (g->hold_since < -1e8f)
      {
        g->hold_since = now;
        REXLOG_INFO("pdz_lab_stealth: {} METER HELD at {:.0f} (his sighting level is {:.0f}): his warning line {} (state {}, "
                    "{:.0f} cm from Jo)",
                    Id(base, viewer), cap, Threshold(*g),
                    waiting ? fmt::format("has not played yet (due {:.1f} s ago)", now - g->warn_from)
                            : fmt::format("started {:.1f} s ago", float(REXCVAR_GET(pdz_lab_stealth_warn_hold_gap_s)) - (g->warn_free_at - now)),
                    StName(g->st), dist);
      }
      g->meter = cap;
    }
    else if (!holding && g->hold_since > -1e8f)
    {
      REXLOG_INFO("pdz_lab_stealth: {} METER HOLD OVER{} after {:.1f} s: {}", Id(base, viewer),
                  face_to_face ? fmt::format(": Jo within {:.0f} cm", hold_min) : std::string(), now - g->hold_since,
                  face_to_face ? fmt::format("she is {:.0f} cm from his eye: face to face he recognises her, his meter fills "
                                             "as without the hold",
                                             dist)
                  : g->warn_ev ? std::string("his warning line could not play within 8 s of being due: his meter fills again")
                               : std::string("his warning line has been heard: his meter fills again"));
      g->hold_since = -1e9f;
    }
  }
  if (g->meter >= Threshold(*g) && SciHold(base, viewer, *g, now))
  {
    // Round 11b (item 3): a calm scientist's meter is full: his sighting line, and he hesitates before Rare's sight
    // (his alarm run) takes over: hidden from Rare for that moment.
    std::memcpy(g->glimpse_pos, at, sizeof(at));
    g->glimpse_time = now;
    R8Veto("a scientist's moment of hesitation (round 11b)");
    ctx.r3.u64 = 0;
    return;
  }
  if (g->meter >= Threshold(*g))
  {
    g->spotted = true;
    g->metered = false;
    g->stale = false;
    g->spotted_at = now;
    g->seen_jo = true;
    Confirm(base, viewer, *g, kCfSighting, fmt::format("a real sighting (meter {:.0f}, {:.0f} cm)", g->meter, dist));
    AlarmSighting(base, viewer, now); // round 4: restarts the alarm's count
    if (REXCVAR_GET(pdz_lab_stealth_sighting_combat) && LayerGuard(*g))
    {
      g->sight_now_until = now + 1.5f; // round 11k (item 4): SightNowTick, in this frame's After
      g->sight_line_due = true;
    }
    if (!g_any_sighting && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: first sighting of Jo in this run ({})", Id(base, viewer));
    }
    g_any_sighting = true;
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} sees Jo (meter {:.0f}, exposure {:.3f}{}, dt {:.2f}, {:.0f} cm, state {}): Rare's "
                  "sight takes over{}",
                  Id(base, viewer), g->meter, f, crouched ? " crouched" : "", dt, dist, StName(g->st), close);
    }
    return; // Rare's answer (visible) stands
  }
  std::memcpy(g->glimpse_pos, at, sizeof(at));
  g->glimpse_time = now;
  if (Logging() && now - g->last_glimpse_log >= 0.5f)
  {
    g->last_glimpse_log = now;
    REXLOG_INFO("pdz_lab_stealth: {} glimpse hidden from Rare (meter {:.0f}, exposure {:.3f}{}, dt {:.2f}, {:.0f} cm, eye "
                "({:.0f} {:.0f} {:.0f}) -> Jo ({:.0f} {:.0f} {:.0f})){}{}",
                Id(base, viewer), g->meter, f, crouched ? " crouched" : "", dt, dist, eye[0], eye[1], eye[2], at[0], at[1],
                at[2], close, expecting ? " (he is looking for her: the meter fills pdz_lab_stealth_expecting_mult as fast)" : "");
  }
  R8Veto("the meter: not full yet (a glimpse is hidden from Rare)");
  ctx.r3.u64 = 0; // hidden from Rare's sight: the seen bitset records "not seen"
}

// COMBAT script update: marks the guard in combat; loses contact when she has been out of sight long enough.
REX_HOOK_RAW(sub_821CC340)
{
  const uint32_t script = ctx.r3.u32;
  if (!LabActive(base) || !Ptr(script))
  {
    __imp__sub_821CC340(ctx, base);
    return;
  }
  const uint32_t c = Load32(base, script + 0x10);
  __imp__sub_821CC340(ctx, base);
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  Guard *g = Ptr(c) ? Find(c) : nullptr;
  if (!g)
  {
    return;
  }
  g->combat_seen = Clock(base);
  if (Load32(base, script + 0xC) == 2 && g->combat_script == script)
  {
    g->combat_script = 0; // Rare's update ended it (no target, or +6268 0x8)
  }
}

// COMBAT script destructor: the script is gone.
REX_HOOK_RAW(sub_821CBE88)
{
  const uint32_t script = ctx.r3.u32;
  if (LabActive(base) && Ptr(script))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, script + 0x10);
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (g && g->combat_script == script)
    {
      g->combat_script = 0;
    }
  }
  __imp__sub_821CBE88(ctx, base);
}

// Sequence task start / destructor: Rare's investigate walk (the hear handler's first task) for a suspicious guard.
REX_HOOK_RAW(sub_821CF7F8)
{
  const uint32_t task = ctx.r3.u32;
  if (LabActive(base) && Ptr(task))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, task + 0x10);
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (g && g->st == St::Suspicious && !g->inv_task)
    {
      g->inv_task = task;
      g->inv_since = Clock(base);
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} investigate walk started (sequence task {:08X})", Id(base, c), task);
      }
    }
  }
  __imp__sub_821CF7F8(ctx, base);
}

REX_HOOK_RAW(sub_821CF228)
{
  const uint32_t task = ctx.r3.u32;
  if (LabActive(base) && Ptr(task))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, task + 0x10);
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (g && g->inv_task == task)
    {
      g->inv_task = 0;
      g->inv_ended = Clock(base);
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} investigate walk ended after {:.1f} s", Id(base, c), g->inv_ended - g->inv_since);
      }
    }
  }
  __imp__sub_821CF228(ctx, base);
}

// SEARCH task start: he is searching from this frame on.
REX_HOOK_RAW(sub_821DF7C0)
{
  const uint32_t task = ctx.r3.u32;
  if (LabActive(base) && Ptr(task))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, task + 0x10);
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (g)
    {
      g->search_seen = Clock(base);
      R3BSearchStart(base, c, *g, task, Clock(base)); // round 3b: the restart warning and the cause of this start
      if (Logging())
      {
        if (const float now = Clock(base); now - g->search_start_log >= 1.0f || now < g->search_start_log)
        {
          g->search_start_log = now; // (under the alarm Rare starts one every frame)
          REXLOG_INFO("pdz_lab_stealth: {} SEARCH task started ({:08X}, state {})", Id(base, c), task, StName(g->st));
        }
      }
    }
  }
  __imp__sub_821DF7C0(ctx, base);
}

// Alarm task start (Rare's alarm run): logged only, behaviour unchanged.
REX_HOOK_RAW(sub_821DF110)
{
  const uint32_t task = ctx.r3.u32;
  const uint64_t lr = ctx.lr;
  if (g_lab && Ptr(task))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (Guard *runner = Find(Load32(base, task + 0x10)))
    {
      runner->run_task = task; // round 11h: the task a committed runner must keep
    }
  }
  if (LabActive(base) && Ptr(task) && Logging())
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, task + 0x10);
    const uint32_t jo = Load32(base, kPlayerOne);
    if (Ptr(c))
    {
      const Guard *g = Find(c);
      REXLOG_INFO("pdz_lab_stealth: ALARM RUN starts: {} (voice {}) {} | sight +6268 {:08X} (valid target {}) | {:.0f} cm "
                  "from Jo | LR {:08X}",
                  Id(base, c), int32_t(Load32(base, c + kVoiceIndex)),
                  g ? fmt::format("state {} meter {:.0f}", StName(g->st), g->meter) : std::string("not tracked"),
                  Load32(base, c + kSight), (Load32(base, c + kSight) & kValidTarget) ? 1 : 0,
                  Ptr(jo) ? Dist(base, c, jo) : -1.0f, uint32_t(lr));
    }
  }
  __imp__sub_821DF110(ctx, base);
}

// COMBAT script start: combat begins (Rare's), or a lost guard reacquires her.
REX_HOOK_RAW(sub_821CBF48)
{
  const uint32_t script = ctx.r3.u32;
  const uint32_t r4_caller = uint32_t(ctx.lr); // (stealth4, fix 1: who started it, for the COMBAT START line)
  if (!LabActive(base) || !Ptr(script))
  {
    __imp__sub_821CBF48(ctx, base);
    return;
  }
  const uint32_t c = Load32(base, script + 0x10);
  // Rare's rules plus (item 5): every COMBAT start raises "alert now" (+6260 0x40, stimulus 0x8) on the friends within
  // its radius (821CBFA0-821CC094); they are in COMBAT a frame later without having seen or heard anything. The input
  // is taken off again for everyone who did not have it before this call. (It carries no target and no position; the
  // position travels by the squad's target copy 8217C540, hooked below.)
  std::vector<uint32_t> not_alerted;
  // Stealth rollout: on a zone level only a COMBAT start of a guard INSIDE the zone is changed, and then for everyone
  // (inside or outside: the club never learns about the docks); a guard outside the zone starts Rare's cascade.
  const bool zone_level = ZoneLevel();
  // Subway helper: a COMBAT start against someone who is not a player (the gang war) is Rare's whole: his cascade
  // stands and the layer's state for him is not touched (not a sighting of Jo; no line of ours).
  const bool gang = zone_level && Ptr(c) && FightsNonPlayer(base, c);
  if (gang)
  {
    __imp__sub_821CBF48(ctx, base);
    if (StealthLog())
    {
      static float said = -1e9f;
      const float now = Clock(base);
      if (now - said >= 2.0f || now < said)
      {
        said = now;
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        const uint32_t t = TargetOf(base, c);
        REXLOG_INFO("pdz_stealth: {}: {} starts COMBAT against {} (not a player): Rare's fight, Rare's squad cascade; not a "
                    "sighting of Jo for the layer",
                    g_level->name, Id(base, c), Ptr(t) ? Id(base, t) : std::string("?"));
      }
    }
    return;
  }
  // (Wed 18 Nov night: a guard who is Rare's (seen or loud) alerts his friends as Rare has it.)
  const bool r9_starter = REXCVAR_GET(pdz_stealth_rares_cascade) && R9IsRares(c);
  const bool no_cascade = RarePlus() && REXCVAR_GET(pdz_lab_no_squad_knowledge) && (!zone_level || InLayer(base, c)) && !r9_starter;
  // Overnight Tue 17 Nov (item 5, log only): who gets Rare's "alert now" from this COMBAT start is noted either way.
  const bool r6_note = REXCVAR_GET(pdz_stealth_friend_alert_log) && Ptr(c);
  if (no_cascade || r6_note)
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (zone_level)
    {
      for (const auto &[other, data] : g_chars) // (every character whose sight ran: tracked or not)
      {
        if (other != c && Ptr(other) && Load32(base, other + kData) == data && !(Load32(base, other + kStimuli) & 0x40))
        {
          not_alerted.push_back(other);
        }
      }
    }
    else
    {
      for (const auto &[other, o] : g_guards)
      {
        if (other != c && Ptr(other) && Load32(base, other + kData) == o.data && !(Load32(base, other + kStimuli) & 0x40))
        {
          not_alerted.push_back(other);
        }
      }
    }
  }
  __imp__sub_821CBF48(ctx, base);
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  if (no_cascade || r6_note)
  {
    std::string who;
    for (const uint32_t other : not_alerted)
    {
      if (Load32(base, other + kStimuli) & 0x40)
      {
        if (r6_note)
        {
          R6AlertNote(base, other, c, no_cascade ? 3 : 1, Clock(base)); // (item 5: this friend's COMBAT start sent it)
        }
        if (no_cascade)
        {
          Store32(base, other + kStimuli, Load32(base, other + kStimuli) & ~0x40u);
          who += " " + Id(base, other);
        }
      }
    }
    if (!who.empty() && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: NO SQUAD KNOWLEDGE: {}'s COMBAT start raised Rare's \"alert now\" on:{}: taken off again (each "
                  "learns of Jo by his own sight or hearing)",
                  Ptr(c) ? Id(base, c) : std::string("?"), who);
    }
  }
  Guard *g = Ptr(c) ? Find(c) : nullptr;
  if (!g || g->st == St::Dead)
  {
    return;
  }
  const float now = Clock(base);
  const bool was_lost = g->st == St::Lost || g->st == St::LostSearch || Hunter(*g);
  const bool sighting = (Load32(base, c + kSight) & kValidTarget) != 0 || now - g->last_visible < 0.5f;
  const bool reacquired = was_lost && sighting;
  const bool again_line = reacquired && AfterContact(*g); // a body-only hunter never saw her: Rare's SEE_PLAYER stands
  const St from = g->st;
  R4ACombatLog(base, c, *g, sighting, now, r4_caller); // (stealth4, fix 1: always on, one line per COMBAT start)
  R6FriendAlertLog(base, c, *g, now); // (overnight Tue 17 Nov, item 5: who sent "alerted by a friend"; log only)
  if (was_lost && !sighting && Logging())
  {
    REXLOG_INFO("pdz_lab_stealth: {} Rare restarted his combat without a sighting ({}, {:.0f} cm from Jo)", Id(base, c),
                StName(g->st), Ptr(Load32(base, kPlayerOne)) ? Dist(base, c, Load32(base, kPlayerOne)) : -1.0f);
  }
  g->combat_script = script;
  if (const uint32_t vt = Load32(base, script); vt >= 0x82000000u && vt < 0x82200000u)
  {
    g_combat_vtable_seen = vt; // (follow-up Sun 15 Nov, item 4: CombatOnStack)
  }
  g->combat_start = now;
  g->combat_seen = now;
  // Round 2, Sun 15 Nov (item 2): a COMBAT that began with no sighting and no hit becomes a search (Rare's
  // leave-combat request now, Rare's SEARCH when the COMBAT has ended: R2Tick). Not a confirmed contact.
  if (R2BlindCombat(base, c, *g, sighting, now))
  {
    g->r2_blind_line_until = now + 6.0f;
    if (g->st != St::Combat)
    {
      Set(base, c, *g, St::Combat, now, "COMBAT start without a sighting: Rare is asked to leave it, a search follows");
    }
    return;
  }
  Confirm(base, c, *g, kCfCombat, "Rare's COMBAT started");
  g->fought = true;
  if (again_line && !g->scientist)
  {
    Say(ctx, base, c, kEvSeeAgain, false, "reacquired");
  }
  if (g->st != St::Combat)
  {
    Set(base, c, *g, St::Combat, now,
        reacquired ? (from == St::Hunting || from == St::HuntPause ? "reacquired her while hunting" : "reacquired her")
        : was_lost ? "Rare restarted his combat (no sighting)"
                   : "COMBAT start");
  }
}

// Rare's rules plus (item 5): the squad's target copy 8217C540(squad r3, character r4): the target's position (squad
// +0x38), pad (+0x44) and zone (+0x48), called from COMBAT start (821CC0B4) and from hearing enemy gunfire (825818D0).
// It is only let through for a member who himself sees a valid target (+6268 0x800000): what the squad holds is
// then where one of its members last really saw her, never a position handed on by a guard who saw nothing.
REX_EXTERN(__imp__sub_8217C540);
REX_HOOK_RAW(sub_8217C540)
{
  const uint32_t who = ctx.r4.u32;
  if (g_lab && RarePlus() && REXCVAR_GET(pdz_lab_no_squad_knowledge) && REXCVAR_GET(pdz_lab_guard_stealth) && Ptr(who) &&
      !(Load32(base, who + kSight) & kValidTarget) && InLayer(base, who) && // (zone level: only a member inside the zone)
      !FightsNonPlayer(base, who) && // (Subway helper: a gang-war target's position is Rare's to pass on)
      !R7SawForSquad(base, who) &&   // (Tue 17 Nov: a sighting the layer itself has just confirmed is a real one)
      !R9IsRares(who))               // (Wed 18 Nov night: Rare's guard passes on what Rare has him pass on)
  {
    if (Logging())
    {
      static float said = -1e9f;
      const float now = Clock(base);
      if (now - said >= 2.0f || now < said)
      {
        said = now;
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        REXLOG_INFO("pdz_lab_stealth: NO SQUAD KNOWLEDGE: the squad's target position is not written for {} (he sees no "
                    "valid target; caller {:08X})",
                    Id(base, who), uint32_t(ctx.lr));
      }
    }
    return;
  }
  __imp__sub_8217C540(ctx, base);
}

// Round 5 (item 4): Rare's noise writer for a hand-held gun's shot (r3 weapon: +0x5C its owner, +0x35C its data
// record; r4 mode: 0 normal, 1 the second function, 2 SILENCED, whose block is record +0xFC: add 40 / floor 100 / cap
// 300 cm for the P9P, DW-P5 and FAC-16 against 1500-3125 / 13500-30000 unsilenced). Remembered per shooter.
REX_EXTERN(__imp__sub_825882E8);
REX_HOOK_RAW(sub_825882E8)
{
  const uint32_t weapon = ctx.r3.u32, mode = ctx.r4.u32;
  const uint32_t r3c_caller = uint32_t(ctx.lr); // round 3c (B): which of Rare's fire routines called
  __imp__sub_825882E8(ctx, base);
  pdz::aim120::GunShot(base, weapon); // aim120_Wed18Nov.cpp: pdz_jo_hit_log (log only): a shot of a guard's at Jo
  if (!g_lab || !REXCVAR_GET(pdz_lab_guard_stealth) || !Ptr(weapon))
  {
    return;
  }
  const uint32_t owner = Load32(base, weapon + 0x5C), record = Load32(base, weapon + 0x35C);
  if (!Ptr(owner) || !Ptr(record))
  {
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  const float now = Clock(base);
  auto it = std::find_if(g_shots.begin(), g_shots.end(), [owner](const Shot &sh) { return sh.shooter == owner; });
  if (it == g_shots.end())
  {
    if (g_shots.size() >= 64)
    {
      g_shots.erase(g_shots.begin());
    }
    g_shots.push_back(Shot());
    it = g_shots.end() - 1;
    it->shooter = owner;
  }
  const bool silenced = mode == 2 && Load32(base, record + 0xFC) != 0;
  if (owner == Load32(base, kPlayerOne))
  {
    R3cBJoShot(base, weapon, mode, r3c_caller, silenced, now); // round 3c (B, fix 3): every shot of Jo's, counted and logged
  }
  const bool changed = silenced != it->silenced;
  it->at = now;
  it->silenced = silenced;
  if (Guard *sg = owner != Load32(base, kPlayerOne) ? Find(owner) : nullptr;
      sg && sg->spotted_at > -1e8f && now >= sg->spotted_at && now - sg->spotted_at < 8.0f && sg->first_shot_for != sg->spotted_at)
  {
    // Round 11k (item 4): his first shot after his sighting, against the start of his first-contact line. (The shot is
    // not held back: no proven way to hold a guard's fire was found. The line is asked for at the sighting.)
    sg->first_shot_for = sg->spotted_at;
    float line = -1e9f;
    for (const Line &l : g_lines)
    {
      if (l.c == owner && l.played && l.start >= sg->spotted_at - 0.2f &&
          (l.ev == kEvSeePlayer || l.ev == kEvSeePlayer + 1 || l.ev == kEvSeeAgain || l.ev == kEvSeeAgain + 1))
      {
        line = l.start;
      }
    }
    REXLOG_INFO("pdz_lab_stealth: {} FIRST SHOT {:.2f} s after his sighting was confirmed: his first-contact line {}",
                Id(base, owner), now - sg->spotted_at,
                line > -1e8f ? fmt::format("started {:.2f} s before the shot", now - line)
                             : std::string("HAS NOT STARTED YET (the shot came first)"));
  }
  if (!silenced && owner == Load32(base, kPlayerOne) && REXCVAR_GET(pdz_lab_stealth_loud) &&
      REXCVAR_GET(pdz_lab_stealth_loud_on_noise)) // (round 11k: LOUD only from the alarm or the generator)
  {
    // Round 11 (item 1): Jo's unsilenced shot, heard by anyone alive within its noise radius (Rare's own writer has
    // just set it; at least 15 m, as the gunfire rule has it): loud.
    const int32_t slot = int32_t(Load32(base, owner + kProfileSlot));
    const float radius = slot >= 0 && slot < int32_t(kProfileSlots) ? LoadF(base, kProfileTable + 0x50 * uint32_t(slot) + 0x30) : 0.0f;
    const float reach = std::max(std::isfinite(radius) ? radius : 0.0f, 1500.0f);
    const float at[3] = {LoadF(base, owner + kPosition), LoadF(base, owner + kPosition + 4), LoadF(base, owner + kPosition + 8)};
    float d = 0.0f;
    if (const uint32_t hearer = LoudHearer(base, at, reach, true, d))
    {
      GoLoud(base, fmt::format("Jo fired an unsilenced shot (mode {}) that {} could hear ({:.0f} cm away, the shot's noise "
                               "radius {:.0f} cm)",
                               mode, Id(base, hearer), d, reach));
      return;
    }
    if (Logging() && (changed || now - it->log_at > 5.0f || now < it->log_at))
    {
      REXLOG_INFO("pdz_lab_stealth: Jo fired an unsilenced shot nobody alive could hear (noise radius {:.0f} cm): still quiet",
                  reach);
    }
  }
  if (Logging() && (changed || now - it->log_at > 5.0f || now < it->log_at))
  {
    it->log_at = now;
    const int32_t slot = int32_t(Load32(base, owner + kProfileSlot));
    REXLOG_INFO("pdz_lab_stealth: shot by {}: {} (mode {}), noise radius now {:.0f} cm at ({:.0f} {:.0f} {:.0f})",
                owner == Load32(base, kPlayerOne) ? std::string("Jo") : Id(base, owner), silenced ? "SILENCED" : "unsilenced", mode,
                slot >= 0 && slot < int32_t(kProfileSlots) ? LoadF(base, kProfileTable + 0x50 * uint32_t(slot) + 0x30) : -1.0f,
                LoadF(base, owner + kPosition), LoadF(base, owner + kPosition + 4), LoadF(base, owner + kPosition + 8));
  }
}

// Round 5 (item 4): Rare's sound event (r3 -> position, f1 radius): the ones raised by the three explosion callers
// (grenade / plasma impact 824BAA54, projectile detonation 825B8D24, exploding props 825EE8C4) are remembered.
REX_EXTERN(__imp__sub_8218AD40);
REX_HOOK_RAW(sub_8218AD40)
{
#if PDZ_COUNTEROP
  if (PdzCounterOpSoundEventDropped(ctx, base))
  {
    return;
  }
#endif
  const uint32_t from = uint32_t(ctx.lr), pos = ctx.r3.u32;
  const float radius = float(ctx.f1.f64);
  if (g_lab && REXCVAR_GET(pdz_lab_guard_stealth) && Ptr(pos) && Logging() &&
      !(from == 0x824BAA54 || from == 0x825B8D24 || from == 0x825EE8C4))
  {
    // Round 11k: every other sound event (a bullet's impact, glass, a body), once a second per caller, for the log.
    static std::unordered_map<uint32_t, float> said;
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float t = Clock(base);
    float &last = said[from];
    if (t - last >= 1.0f || t < last)
    {
      last = t;
      REXLOG_INFO("pdz_lab_stealth: sound event at ({:.0f} {:.0f} {:.0f}), heard within {:.0f} cm (caller {:08X}: not an "
                  "explosion)",
                  LoadF(base, pos), LoadF(base, pos + 4), LoadF(base, pos + 8), radius, from);
    }
  }
  if (g_lab && REXCVAR_GET(pdz_lab_guard_stealth) && Ptr(pos) && (from == 0x824BAA54 || from == 0x825B8D24 || from == 0x825EE8C4))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    Blast b;
    for (int i = 0; i < 3; ++i)
    {
      b.pos[i] = LoadF(base, pos + 4 * i);
    }
    b.radius = radius;
    b.at = Clock(base);
    if (g_blasts.size() >= 16)
    {
      g_blasts.erase(g_blasts.begin());
    }
    g_blasts.push_back(b);
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: explosion at ({:.0f} {:.0f} {:.0f}), heard within {:.0f} cm (caller {:08X})", b.pos[0],
                  b.pos[1], b.pos[2], radius, from);
    }
    if (REXCVAR_GET(pdz_lab_stealth_loud) && REXCVAR_GET(pdz_lab_stealth_loud_on_noise)) // (round 11k: off)
    {
      float d = 0.0f; // round 11 (item 1): an explosion anyone alive can hear: loud
      if (const uint32_t hearer = LoudHearer(base, b.pos, radius, false, d))
      {
        GoLoud(base, fmt::format("an explosion at ({:.0f} {:.0f} {:.0f}) that {} could hear ({:.0f} cm away, heard within "
                                 "{:.0f} cm)",
                                 b.pos[0], b.pos[1], b.pos[2], Id(base, hearer), d, radius));
      }
    }
  }
  __imp__sub_8218AD40(ctx, base);
}

// Round 5 (item 9): Rare's speech task builds the cue name for an event (r3 speech component, +160 its character; r4
// the event): the speaker is known to the cue swap (lab_stealth::CueName) while it runs.
REX_EXTERN(__imp__sub_8262AF60);
REX_HOOK_RAW(sub_8262AF60)
{
  const uint32_t speech = ctx.r3.u32;
  const uint32_t before = t_name_c;
  t_name_c = Ptr(speech) ? Load32(base, speech + 160) : 0;
  __imp__sub_8262AF60(ctx, base);
  t_name_c = before;
  if (t_task)
  {
    t_task_start = int(ctx.r3.u32 & 0xFF); // round 10b: 0 = the sound was made
  }
}

// Round 11k (item 1): GUNFIRE IS HEARD AS GUNFIRE, AND WALLS BLOCK IT. Rare's hearing loop (sub_8218AAC8, r3 the
// listener; once per character) walks the 170 perception profiles: a profile whose gunfire radius (+0x30) times the
// listener's hearing factor (his profile +0x38) reaches him marks him as having heard that shooter (distance only:
// no wall test, no zone test), and Rare's sound events (the list at 82D2D124: +0 radius, +8 position, +20 pad;
// sub_8218A980) are heard within their radius and the listener's zone-hop limit (his profile +0x3C), with no wall test.
// Before LOUD, for a listener who is not fighting: a shooter or a sound event in reach whose line to the listener's
// ears is blocked by geometry with Rare's block-AI-sound flag (collision mask 0x40, either way: the footsteps' test),
// or that is beyond his zone-hop limit, is taken out for this one call (its radius is zeroed, and put back after it).
// So Rare's own hearing, and everything his scripts do with it (the troopers' stimulus word, the scientists' cower),
// only happens for someone who could really hear it. The test is kept for 0.3 s per listener and source.
//
// Follow-up Sun 15 Nov (item 8): pdz_stealth_wall_muffle (on by default). Walls and floors MUFFLE instead of blocking,
// and the zone-hop limit of ours is gone: zones are AI navigation areas, not sound barriers (Mansion grounds: 84 of 99
// refusals were zone hops, some 11-15 m away over open ground). Each wall or floor with Rare's block-AI-sound flag on
// the straight line from the sound to the listener's ears halves the radius the sound is heard within (kMuffleCap
// walls at most are counted). Off = the rule above, unchanged.
REXCVAR_DEFINE_BOOL(pdz_stealth_wall_muffle, true, "Gameplay",
                    "Stealth layer: walls and floors muffle sound instead of blocking it. Each wall or floor (Rare's "
                    "block-AI-sound geometry) between a gunshot, an explosion or a breaking lamp and a guard halves "
                    "the distance it is heard within (one wall: half, two: a quarter; four at most are counted), and "
                    "the layer's own zone limit on gunfire is gone, so only walls, floors and Rare's own distance "
                    "decide. Off = any wall or floor blocks the sound, and so does Rare's zone-hop limit")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  struct Heard
  {
    float at = -1e9f, log_at = -1e9f;
    bool blocked = false;
    int why = 0;    // 1 zone hops, 2 walls
    int walls = -1; // pdz_stealth_wall_muffle: the walls and floors counted on the line (-1: not counted)
  };
  std::unordered_map<uint64_t, Heard> g_heard;

  constexpr int kMuffleCap = 4;           // walls counted at most (the radius is then 1/16)
  constexpr float kMuffleStretch = 50.0f; // cm: crossings closer together than this count as one wall

  // The crossings of block-AI-sound geometry (mask 0x40) on a -> b, in that direction only (a mesh triangle is hit
  // from its front): the segment is halved until a blocked stretch is no longer than kMuffleStretch; each such stretch
  // is one crossing. Stops at `cap`.
  int SoundCrossings(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3], int cap, int depth = 0,
                     uint32_t mask = 0x40) // (Sat 21 Nov evening: the mask is a parameter for the sounds' deep wall cut)
  {
    if (cap <= 0 || SegmentClear(ctx, base, a, b, mask))
    {
      return 0;
    }
    if (depth >= 10 || Dist3(a, b) <= kMuffleStretch)
    {
      return 1;
    }
    const float mid[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
    int n = SoundCrossings(ctx, base, a, mid, cap, depth + 1, mask);
    if (n < cap)
    {
      n += SoundCrossings(ctx, base, mid, b, cap - n, depth + 1, mask);
    }
    return n > 0 ? n : 1; // (the whole was blocked: at least one)
  }
  // The walls and floors between a sound and a listener's ears: the crossings counted from the sound to the ears and
  // from the ears to the sound (a solid wall shows one face to each direction, a single-sided floor to one only); the
  // larger count, kMuffleCap at most.
  int SoundWalls(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3])
  {
    const int there = SoundCrossings(ctx, base, src, ears, kMuffleCap);
    const int back = there >= kMuffleCap ? 0 : SoundCrossings(ctx, base, ears, src, kMuffleCap);
    return std::max(there, back);
  }
  float Muffled(float radius, int walls)
  {
    return walls > 0 ? radius / float(1 << std::min(walls, kMuffleCap)) : radius;
  }
} // namespace
namespace lab_stealth
{
  // (Wed 18 Nov, sound3: the same wall count for a restored sound and a player's head; read only.)
  int SoundWallsBetween(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3])
  {
    return SoundWalls(ctx, base, src, ears);
  }
  // Sat 21 Nov evening (pdz_snd_wall_cut_deep): the surfaces on a -> b that the physics ray with `mask` hits (both
  // ways, the larger; crossings 50 cm apart count once), up to `cap`. The stealth layer's own count is unchanged.
  // Sat 21 Nov evening (pdz_snd_wall_cut_inside): anything with `mask` on a -> b, either way (two rays, no count).
  bool SegmentBlocked(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3], uint32_t mask)
  {
    return !SegmentClear(ctx, base, a, b, mask) || !SegmentClear(ctx, base, b, a, mask);
  }
  int SurfacesBetween(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3], int cap, uint32_t mask)
  {
    const int there = SoundCrossings(ctx, base, a, b, cap, 0, mask);
    const int back = there >= cap ? 0 : SoundCrossings(ctx, base, b, a, cap, 0, mask);
    return std::max(there, back);
  }
} // namespace lab_stealth
namespace
{

  // `reach`: the distance Rare hears this sound within for this listener (pdz_stealth_wall_muffle halves it per wall).
  bool SoundBlocked(PPCContext &ctx, uint8_t *base, uint32_t c, uint32_t source, const float src[3], uint32_t src_pad, float now,
                    const char *what = nullptr, float reach = 0.0f)
  {
    if (g_heard.size() > 8192)
    {
      g_heard.clear();
    }
    Heard &h = g_heard[(uint64_t(c) << 32) | source];
    if (REXCVAR_GET(pdz_stealth_wall_muffle))
    {
      float me[3];
      PosOf(base, c, me);
      if (!(now >= h.at && now - h.at < 0.3f) || h.walls < 0)
      {
        h.at = now;
        const float ears[3] = {me[0], me[1] + 60.0f, me[2]}; // (positions are at hip height)
        h.walls = SoundWalls(ctx, base, src, ears);
        h.why = h.walls ? 2 : 0;
      }
      const float d = Dist3(me, src);
      const float within = R3WWithin(base, c, source, src, reach, h.walls, d, now); // (round 3 item 2: explosions muffle less)
      // (no reach given: blocked only by the full count of walls)
      h.blocked = reach > 0.0f ? !(d < within) : h.walls >= kMuffleCap;
      if (what && Logging() && (now - h.log_at >= 5.0f || now < h.log_at))
      {
        h.log_at = now;
        REXLOG_INFO("pdz_lab_stealth: {} {} {} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away: {} wall(s) or floor(s) with Rare's "
                    "block-AI-sound flag between them{}, heard within {:.0f} cm (Rare's {:.0f} cm) (pdz_stealth_wall_muffle)",
                    Id(base, c), h.blocked ? "does NOT hear" : h.walls ? "hears (muffled)" : "hears", what, src[0], src[1], src[2],
                    d, h.walls, h.walls >= kMuffleCap ? " (or more)" : "", within, reach);
      }
      return h.blocked;
    }
    h.walls = -1;
    if (!(now >= h.at && now - h.at < 0.3f))
    {
      h.at = now;
      h.blocked = false;
      h.why = 0;
      const uint32_t profile = ProfileOf(base, c);
      const int32_t limit = profile ? int32_t(Load32(base, profile + 0x3C)) : -1;
      const uint32_t pad = Load32(base, c + kPad);
      float me[3];
      PosOf(base, c, me);
      const float ears[3] = {me[0], me[1] + 60.0f, me[2]}; // (positions are at hip height)
      if (limit >= 0 && Ptr(pad) && Ptr(src_pad) && ZoneHops(base, pad, src_pad) > limit)
      {
        h.blocked = true;
        h.why = 1;
      }
      else if (!SegmentClear(ctx, base, ears, src, 0x40) || !SegmentClear(ctx, base, src, ears, 0x40))
      {
        h.blocked = true;
        h.why = 2;
      }
    }
    if (h.blocked && what && Logging() && (now - h.log_at >= 5.0f || now < h.log_at))
    {
      h.log_at = now;
      float me[3];
      PosOf(base, c, me);
      REXLOG_INFO("pdz_lab_stealth: {} does NOT hear {} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away: {}", Id(base, c), what, src[0],
                  src[1], src[2], Dist3(me, src),
                  h.why == 1 ? "beyond his zone-hop limit" : "a wall or a floor with Rare's block-AI-sound flag is between them");
    }
    return h.blocked;
  }

  // Rare's rules plus: he fills faster for pdz_lab_stealth_alert_s (a body he found, gunfire he heard).
  void FillFaster(const uint8_t *base, uint32_t c, Guard &g, float now, const char *why)
  {
    const float secs = float(REXCVAR_GET(pdz_lab_stealth_alert_s));
    if (!(secs > 0.0f) || g.dead || g.st == St::Dead)
    {
      return;
    }
    const bool was = now < g.alert_until;
    g.alert_until = now + secs;
    if (!was && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} {}: his meter fills {:.1f} times faster for {:.0f} s; what he does is Rare's", Id(base, c), why,
                  float(REXCVAR_GET(pdz_lab_stealth_alert_rate)), secs);
    }
  }
  // Rare's rules plus: Rare's stimulus word reaches Rare untouched. Here only: a body -> the meter fills faster;
  // gunfire or an explosion he could really hear (the shots on record, the wall test) -> faster too, and his
  // HEAR_GUNFIRE line.
#include "lab_stealth_hurt_Sun15Nov.inc" // (follow-up Sun 15 Nov, items 11 and 16: HurtFull, NearMissHurt, BlastNear)
#include "lab_stealth_round2_Sun15Nov.inc" // (round 2 Sun 15 Nov, items 1-3: the search cap, blind combat, any-squad witness)
#include "lab_stealth_r3_L_Sun15Nov.inc" // (round 3 Sun 15 Nov, items 3 and 9: every search end has a line)
#include "lab_stealth_r3_W_Sun15Nov.inc" // (round 3 Sun 15 Nov, items 2, 4, 5, 7: explosion muffle, tower witness, witness once, boost limit)
#include "lab_stealth_r3_H_Sun15Nov.inc" // (round 3 Sun 15 Nov, items 1, 6, 8: heard means a reaction, witness walk, shout heard)
#include "lab_stealth_r3b_Sun15Nov.inc" // (round 3b Sun 15 Nov: scientist witness test, one reaction per body, restarts keep the cap, witness walk)
#include "lab_stealth_r3c_B_Sun15Nov.inc" // (round 3c Sun 15 Nov night, fixes 2 and 3: only lights that really go out count; Jo's shots)
#include "lab_stealth_r3c_C_Sun15Nov.inc" // (round 3c, fix 4: witness tuning: Rare's sight cone, light range, look first)
#include "lab_stealth_r3c_D_Sun15Nov.inc" // (round 3c, fixes 5 and 6: Mansion radio reinforcements under the layer; tower restarts)
#include "lab_stealth_r4_B_Mon16Nov.inc" // (stealth fixes Mon 16 Nov, fixes 2-4: a body line is acted on, the witness rule for everyone, a look from afar)
#include "lab_stealth_r4_C_Mon16Nov.inc" // (round 4 Mon 16 Nov, fixes 6 and 7: an unseen Jo does not steer a guard, the swing line; Killian's line)
#include "lab_stealth_r5_C_Mon16Nov.inc" // (round 5 Mon 16 Nov, fix 5: a shot-down security camera is a noise)
#include "lab_stealth_r3c_A_Sun15Nov.inc" // (round 3c, fix 1: the layer's per-level memory is cleared at every level start)
#include "lab_stealth_r4_A_Mon16Nov.inc" // (stealth4, Mon 16 Nov, fixes 1 and 5: the kill and COMBAT START lines; sneaking up behind)
#include "lab_stealth_r5_A_Mon16Nov.inc" // (round 5, Mon 16 Nov, fix 6: melee downs are logged and count as bodies)
#include "lab_stealth_r6_Tue17Nov.inc" // (overnight Mon 16 / Tue 17 Nov: the unarmed truce, the friend alert log, camera words)
#include "lab_stealth_r5_B_Mon16Nov.inc" // (round 5 Mon 16 Nov, fixes 2 and 4: a glimpse is walked to; a shot beside him: he looks that way)
#include "lab_stealth_r8_B_Tue17Nov.inc" // (Tue 17 Nov night, fix2: the body check comes first; a body seen later)
#include "lab_stealth_r9_Wed18Nov.inc" // (Wed 18 Nov night: seen or loud = Rare's guard; late bodies, no freezing, a lamp heard)
#include "lab_stealth_r10_Thu19Nov.inc" // (Thu 19 Nov: far gunfire runs to the shooter, second body, no give-up after a body, no freezing)
  void RarePlusStimulus(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
  {
    NearMissHurt(base, c, g, word, now); // (item 16)
    if (word & 0x40)
    {
      FillFaster(base, c, g, now, "found a body (Rare's stimulus)");
    }
    if (!(word & 0x1400) || (now >= g.loud_at && now - g.loud_at < 2.0f))
    {
      return;
    }
    float me[3];
    PosOf(base, c, me);
    for (const Shot &sh : g_shots)
    {
      if (sh.silenced || now < sh.at || now - sh.at > 2.5f || sh.shooter == c || !Ptr(sh.shooter))
      {
        continue;
      }
      if (sh.shooter != Load32(base, kPlayerOne) && FightsNonPlayer(base, sh.shooter))
      {
        continue; // (Subway helper: a gang-war shot is not about Jo: no faster meter, no gunfire line of ours)
      }
      const int32_t slot = int32_t(Load32(base, sh.shooter + kProfileSlot));
      if (slot < 0 || slot >= int32_t(kProfileSlots))
      {
        continue;
      }
      const float radius = LoadF(base, kProfileTable + 0x50 * uint32_t(slot) + 0x30);
      float src[3];
      PosOf(base, sh.shooter, src);
      if (Dist3(me, src) < std::max(std::isfinite(radius) ? radius : 0.0f, 1500.0f) &&
          !SoundBlocked(ctx, base, c, sh.shooter, src, Load32(base, sh.shooter + kPad), now, nullptr,
                        std::max(std::isfinite(radius) ? radius : 0.0f, 1500.0f)))
      {
        g.loud_at = now;
        FillFaster(base, c, g, now, sh.shooter == Load32(base, kPlayerOne) ? "heard Jo's unsilenced shot" : "heard a guard's gunfire");
        if (now - g.gunfire_line_said > 20.0f || now < g.gunfire_line_said)
        {
          g.gunfire_line_at = now; // (GunfireLineTick: HEAR_GUNFIRE)
        }
        return;
      }
    }
    uint32_t index = 0;
    for (const Blast &b : g_blasts)
    {
      ++index;
      const float age = now - b.at;
      if (age >= 0.0f && age < 1.5f + b.radius / 1500.0f && Dist3(me, b.pos) < b.radius &&
          !SoundBlocked(ctx, base, c, 0xB1A50000u + index, b.pos, 0, now, nullptr, b.radius))
      {
        g.loud_at = now;
        g.heard_at = now;
        FillFaster(base, c, g, now, "heard an explosion");
        return;
      }
    }
  }
} // namespace

REX_EXTERN(__imp__sub_8218AAC8);
REX_HOOK_RAW(sub_8218AAC8)
{
#if PDZ_COUNTEROP
  struct CounterOpMute // player 2's perception slot makes no noise while Rare's hearing loop runs
  {
    uint8_t *base;
    uint32_t muted[8];
    int n;
    explicit CounterOpMute(uint8_t *b) : base(b), n(PdzCounterOpHearingMute(b, muted)) {}
    ~CounterOpMute() { PdzCounterOpHearingUnmute(base, muted, n); }
  };
  const CounterOpMute counter_op_mute(base);
#endif
  const uint32_t c = ctx.r3.u32;
  void PdzSndHeardHearing(PPCContext &, uint8_t *, uint32_t, bool); PdzSndHeardHearing(ctx, base, c, g_lab && REXCVAR_GET(pdz_lab_guard_stealth) && REXCVAR_GET(pdz_lab_stealth_gunfire_hearing) && Ptr(c) && InLayer(base, c)); // pdz_snd_heard_log, log only (sound_heard_Fri20Nov.cpp)
  if (!g_lab || !REXCVAR_GET(pdz_lab_guard_stealth) || !REXCVAR_GET(pdz_lab_stealth_gunfire_hearing) || !Ptr(c) ||
      !InLayer(base, c)) // (stealth rollout: a listener outside the zone hears as Rare has it)
  {
    __imp__sub_8218AAC8(ctx, base);
    return;
  }
  struct Saved
  {
    uint32_t at, bits;
  };
  Saved saved[48];
  int n = 0;
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const Guard *g = Find(c);
    const bool fighting = g && (g->st == St::Combat || g->st == St::Lost || g->combat_script != 0 || g->r9_rares); // (Wed 18 Nov: Rare's guard hears as Rare has it)
    const uint32_t mine = ProfileOf(base, c);
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!fighting && mine && c != jo)
    {
      const float now = Clock(base);
      const float factor = LoadF(base, mine + 0x38);
      float me[3];
      PosOf(base, c, me);
      for (uint32_t i = 0; i < 170 && n < 40; ++i)
      {
        const uint32_t slot = kProfileTable + 0x50 * i;
        const uint32_t bits = Load32(base, slot + 0x30);
        if (!bits)
        {
          continue; // (no gunfire from this profile)
        }
        const float radius = LoadF(base, slot + 0x30);
        const uint32_t flags = Load32(base, slot + 0x44), owner = Load32(base, slot + 4);
        if (!(radius > 0.0f) || (flags & 5) != 5 || owner == c || !Ptr(owner))
        {
          continue;
        }
        if (owner != jo && FightsNonPlayer(base, owner))
        {
          continue; // (Subway helper: gang-war gunfire is heard as Rare has it)
        }
        float src[3];
        PosOf(base, owner, src);
        if (!(Dist3(me, src) < radius * factor))
        {
          continue; // out of Rare's own reach: not heard anyway
        }
        const std::string what = Logging() && radius >= 1000.0f
                                     ? fmt::format("the gunfire of {} (noise radius {:.0f} cm)", owner == jo ? std::string("Jo") : Id(base, owner), radius)
                                     : std::string();
        if (SoundBlocked(ctx, base, c, owner, src, Load32(base, owner + kPad), now, what.empty() ? nullptr : what.c_str(),
                         radius * factor))
        {
          saved[n].at = slot + 0x30;
          saved[n].bits = bits;
          ++n;
          Store32(base, slot + 0x30, 0);
        }
        else
        {
          R3HHeard(base, c, 1, owner, src, Dist3(me, src), now); // (round 3 H, item 1: heard: Rare's reaction is awaited)
        }
      }
      constexpr uint32_t kSoundEvents = 0x82D2D124; // +0 the entries (8 bytes: +2 next index, +4 -> event), +8 the first
      const uint32_t entries = Load32(base, kSoundEvents), first = Load32(base, kSoundEvents + 8);
      int index = Ptr(entries) && Ptr(first) && first >= entries ? int(int16_t(((first - entries) >> 3) & 0xFFFF)) : -1;
      for (int guard = 0; index >= 0 && guard < 128 && n < 48; ++guard)
      {
        const uint32_t entry = entries + 8 * uint32_t(index);
        const uint32_t event = Load32(base, entry + 4);
        index = int(int16_t(Load32(base, entry) & 0xFFFF));
        if (!Ptr(event))
        {
          continue;
        }
        const uint32_t bits = Load32(base, event);
        const float radius = LoadF(base, event);
        if (!bits || !(radius > 0.0f))
        {
          continue;
        }
        const float src[3] = {LoadF(base, event + 8), LoadF(base, event + 12), LoadF(base, event + 16)};
        const float d = Dist3(me, src);
        if (!(d > 1.0f) || !(d < radius))
        {
          continue;
        }
        const std::string what = Logging() ? fmt::format("a sound event (heard within {:.0f} cm)", radius) : std::string();
        if (SoundBlocked(ctx, base, c, event, src, Load32(base, event + 20), now, what.empty() ? nullptr : what.c_str(), radius))
        {
          saved[n].at = event;
          saved[n].bits = bits;
          ++n;
          Store32(base, event, 0);
        }
        else
        {
          R3HHeard(base, c, 2, event, src, d, now); // (round 3 H, item 1: heard: Rare's reaction is awaited)
        }
      }
    }
  }
  __imp__sub_8218AAC8(ctx, base);
  for (int i = 0; i < n; ++i)
  {
    Store32(base, saved[i].at, saved[i].bits);
  }
}

// Round 5 (items 2 and 4): Rare's stimulus word (sub_821D1888: r3 character, r4 -> the word, r5 -> the bits his
// script ignores), built for the DEFAULT script's reaction and for the SEARCH update. Its bits (research 6d 1.3):
// 0x2 damaged, 0x4 WARNER + any stimulus, 0x8 alerted by a friend / "alert now", 0x10 a friend died in view, 0x20 a
// hurt friend, 0x40 a body, 0x80 near miss, 0x100 saw a valid target, 0x200 the alarm, 0x400 gunfire with a target,
// 0x1000 heard a sound. Rare's handlers send a guard who saw a second death (or a hurt friend) into COMBAT without
// a sighting, where his sight is no longer metered and the squad cascade follows (log 025 07:07:18 and 07:18:25), let
// a WARNER warn his squad into combat at any stimulus, and send an ALARM_USER for the alarm at any stimulus, a sound
// included (log 027 07:43:39). For a tracked trooper who does not know where Jo is:
//   - 0x10 / 0x20 are taken out: the death rules (After: the nearest witness searches round the spot alone);
//   - 0x1000 is taken out: he investigates the sound himself, spot by spot (After);
//   - 0x4 stays only with a stimulus that means he knows (0x2, 0x8, 0x100, 0x200, 0x400).
REX_EXTERN(__imp__sub_821D1888);
REX_HOOK_RAW(sub_821D1888)
{
  const uint32_t c = ctx.r3.u32, out = ctx.r4.u32;
  // (overnight Tue 17 Nov, item 5, log only: his inputs and flag word before Rare builds the word from them; the
  // builder clears +6276 0x1000 itself. Two loads.)
  const uint32_t r6_inputs = g_lab && Ptr(c) ? Load32(base, c + kStimuli) : 0;
  const uint32_t r6_flags = g_lab && Ptr(c) ? Load32(base, c + 6276) : 0;
  __imp__sub_821D1888(ctx, base);
  if ((!g_lab && !(g_loud && g_lab_run && Clock(base) < g_loud_commit_until)) || !REXCVAR_GET(pdz_lab_guard_stealth) || !Ptr(c) ||
      !Ptr(out))
  {
    return;
  }
  const uint32_t word = Load32(base, out);
  if (!word)
  {
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  Guard *g = Find(c);
  if (!g || g->st == St::Dead || g->dead || !LayerGuard(*g) || g->r6_truce) // (the Lab: troopers 17-19; not under Rare's truce)
  {
    return;
  }
  const float now = Clock(base);
  R4AStimSeen(c, word, now); // (stealth4, fix 1: Rare's last word for him, for the COMBAT START line)
  R8StimSeen(c, *g, word, now); // (Tue 17 Nov night, fix2: Rare's last word, for the blind search's place)
  if (word & 0x8)
  {
    R6AlertWord(base, c, word, r6_inputs, r6_flags, now); // (item 5: which of Rare's inputs raised "alerted by a friend")
  }
  if (g->run_committed)
  {
    Store32(base, out, 0); // round 11h (item 1): a committed alarm runner reacts to nothing: he runs
    return;
  }
  if (!g_lab)
  {
    return; // (after LOUD: only the committed runner above is ours)
  }
  {
    uint32_t far_drop = 0;
    if (R10FarStimulus(base, c, *g, word, now, far_drop))
    {
      // (Thu 19 Nov: only another guard's gunfire or Rare's friend alert: he runs to the guard who fired, without Jo's
      // position: those bits are taken from Rare's word.)
      if (far_drop)
      {
        Store32(base, out, word & ~far_drop);
      }
      return;
    }
  }
  if (R9Stimulus(base, c, *g, word, now))
  {
    return; // (Wed 18 Nov night: seen or loud: Rare's guard: the word is Rare's, whole)
  }
  if (R7HandsOff(base, c, *g))
  {
    return; // (Tue 17 Nov: in Rare's COMBAT with the layer's agreement: nothing is taken from Rare's word)
  }
  const R7Rule r7_rule("the stimulus rules (Rare's word for his reactions)");
  uint32_t drop = 0;
  if (RarePlus())
  {
    // Round 3b, Sun 15 Nov (fix 2a): Rare's "a body" (0040) again for a body he has already found: taken (first).
    const uint32_t r3b = R3BBodyDrop(ctx, base, c, *g, word, now);
    drop |= r3b;
    RarePlusStimulus(ctx, base, c, *g, word & ~r3b, now); // (nothing is taken from Rare: his line and a faster meter only)
    // Round 2, Sun 15 Nov. Item 3: Rare's word shows "a friend died in view": our raised input has arrived.
    if ((word & 0x10) && now < g->r2_witness_until)
    {
      g->r2_witness_until = -1e9f;
    }
    // Item 1: a sound alone, after his capped search and its one restart: no third search (the one thing taken).
    drop |= R2StimulusDrop(base, c, *g, word & ~r3b, now);
    // Round 3 (helper W), items 5 and 4: a repeat of "a friend died in view" for a body he has already reacted to, or
    // Rare's own one for a tower sniper of another squad.
    // Round 3c (helper C, fix 4), before round 3's rules: a "friend died in view" he could not have seen is taken; one he
    // saw without seeing Jo is held while he goes to look first; `keep` = his held reaction, released now.
    uint32_t r3c_keep = 0;
    const uint32_t r3c = R3cCStimulus(ctx, base, c, *g, word & ~r3b, now, r3c_keep);
    drop |= r3c;
    drop |= R3WStimulusDrop(base, c, *g, word & ~(r3c | r3c_keep), now);
    // Round 3 (helper H), items 1 and 6: Rare's reaction to a sound has come; Rare's search on the spot is not started
    // while his walk to the body he saw killed is due or runs.
    drop |= R3HStimulus(base, c, *g, word, now);
    drop |= R8BlindSearchDrop(base, c, *g, word & ~drop, now); // (Tue 17 Nov night, fix2, item 2: one search, no restarts)
    drop &= ~r3c_keep; // (round 3c, C: the released reaction reaches Rare)
    R3BCause(base, c, *g, word & ~drop, now); // (round 3b, fix 2b: what a search starting from this word is for)
  }
  else if (!Knows(base, c, *g, now))
  {
    if (REXCVAR_GET(pdz_lab_stealth_death_rules))
    {
      if (word & 0x30)
      {
        drop |= word & 0x30;
        if (word & 0x10)
        {
          g->death_seen_at = now;
        }
      }
    }
    if (REXCVAR_GET(pdz_lab_stealth_noise_rules) && REXCVAR_GET(pdz_lab_stealth_investigation) && (word & 0x1000))
    {
      // The gunfire rule (item 4 as changed): what did he hear? An unsilenced shot: a shooter who fired unsilenced in
      // the last 2.5 s whose noise radius (shooter slot +0x30) times this guard's hearing factor (setup +0x48) reaches
      // him, Rare's own test (8218ABB0), with Rare's gunfire input (+6260 0x2) set. An explosion: one of the last
      // seconds whose sound event reaches him, with the sound-event input (+6260 0x4) set.
      const uint32_t inputs = Load32(base, c + kStimuli);
      const uint32_t data = Load32(base, c + kData);
      const float factor = Ptr(data) ? LoadF(base, data + 0x48) : 1.0f;
      float me[3];
      for (int i = 0; i < 3; ++i)
      {
        me[i] = LoadF(base, c + kPosition + 4 * i);
      }
      const char *kind = nullptr; // loud
      float blast_at[3] = {0.0f, 0.0f, 0.0f};
      uint32_t by = 0;
      bool silenced_shot = false;
      if (g->test_sound && now < g->test_sound_until)
      {
        kind = g->test_sound == 1 ? "an unsilenced shot (test tool)" : g->test_sound == 3 ? "an explosion (test tool)" : nullptr;
        silenced_shot = g->test_sound == 2;
      }
      else
      {
        // Round 11k (item 1; log 021 08:56:54: s3g1, 13 m from s1m4's rifle fire, was sent to "a sound (not gunfire)":
        // Rare's gunfire input was not up for him, the word came from a sound event of that fight). What the sound
        // WAS is judged by the shots on record: an unsilenced shot of the last 2.5 s within its noise radius (at
        // least 15 m; his hearing factor only widens it) that no wall or floor (mask 0x40) and no zone-hop limit
        // keeps from him is gunfire, whichever of Rare's inputs raised the word. The same for explosions.
        const bool walls = REXCVAR_GET(pdz_lab_stealth_gunfire_hearing);
        for (const Shot &sh : g_shots)
        {
          if (now < sh.at || now - sh.at > 2.5f || sh.shooter == c || !Ptr(sh.shooter))
          {
            continue;
          }
          const int32_t slot = int32_t(Load32(base, sh.shooter + kProfileSlot));
          if (slot < 0 || slot >= int32_t(kProfileSlots))
          {
            continue;
          }
          const float radius = LoadF(base, kProfileTable + 0x50 * uint32_t(slot) + 0x30);
          float src[3];
          PosOf(base, sh.shooter, src);
          const float d = Dist3(me, src);
          const float reach = std::max(std::isfinite(radius) ? radius : 0.0f, sh.silenced ? 100.0f : 1500.0f);
          const bool in_reach = walls ? d < reach * (factor > 1.0f ? factor : 1.0f) : ((inputs & 0x2) != 0 && d < reach * factor);
          if (!in_reach || (walls && SoundBlocked(ctx, base, c, sh.shooter, src, Load32(base, sh.shooter + kPad), now, nullptr,
                                                  reach * (factor > 1.0f ? factor : 1.0f))))
          {
            continue;
          }
          if (!sh.silenced)
          {
            kind = "an unsilenced shot";
            by = sh.shooter;
            break;
          }
          silenced_shot = true;
          by = sh.shooter;
        }
        if (!kind && !silenced_shot && (inputs & 0x2))
        {
          kind = "gunfire (no hand-held shot on record: counted as unsilenced)";
        }
        if (!kind && ((inputs & 0x4) || walls))
        {
          uint32_t index = 0;
          for (const Blast &b : g_blasts)
          {
            ++index;
            const float age = now - b.at;
            const float dx = b.pos[0] - me[0], dy = b.pos[1] - me[1], dz = b.pos[2] - me[2];
            if (age >= 0.0f && age < 1.5f + b.radius / 1500.0f && std::sqrt(dx * dx + dy * dy + dz * dz) < b.radius &&
                !(walls && SoundBlocked(ctx, base, c, 0xB1A50000u + index, b.pos, 0, now, nullptr, b.radius)))
            {
              kind = "an explosion";
              std::memcpy(blast_at, b.pos, sizeof(blast_at));
              break;
            }
          }
        }
      }
      if (kind && REXCVAR_GET(pdz_lab_stealth_gunfire_rules))
      {
        g->loud_at = now;
        const uint32_t jo_c = Load32(base, kPlayerOne);
        const bool hers = by == jo_c || (g->test_sound && now < g->test_sound_until) || kind[3] == 'e';
        const char *does = ": Rare's reaction is left alone";
        if (hers && REXCVAR_GET(pdz_lab_stealth_loud_on_noise))
        {
          // Round 11 (item 1): Jo's own unsilenced shot or an explosion (kind "an explosion..."), heard by this guard.
          GoLoud(base, fmt::format("{} heard {}{}", Id(base, c), kind, by ? " by Jo" : ""));
        }
        else if (hers && REXCVAR_GET(pdz_lab_stealth_gunfire_hearing))
        {
          // Round 11k (items 1 and 5): not LOUD. He goes straight to the spot (the noise investigation, started at
          // once, whoever else looks into it), with his HEAR_UNKNOWN line: taken from Rare.
          drop |= 0x1000;
          uint32_t pad = 0;
          if (kind[3] == 'e')
          {
            float pd = 0.0f;
            const uint32_t own = Load32(base, c + kPad);
            pad = Ptr(own) ? NearestPadTo(base, own, blast_at, pd) : 0;
          }
          if (!Ptr(pad) && Ptr(jo_c))
          {
            pad = Load32(base, jo_c + kPad);
          }
          g->noise_at = now;
          g->noise_pad = Ptr(pad) ? pad : 0;
          g->noise_silenced = false;
          g->noise_why = kind[3] == 'e' ? "an EXPLOSION" : "Jo's UNSILENCED shot";
          g->noise_fast = true;
          does = ": taken from Rare: not LOUD: he goes straight to the spot with his HEAR_UNKNOWN line";
        }
        else if (!hers && REXCVAR_GET(pdz_lab_stealth_gunfire_hearing) && Ptr(by))
        {
          // Round 11k (item 1): a guard's gunfire: he joins in, as in Rare's game. Rare's reaction stands, and the
          // pad it goes to is the shooter's; his HEAR_GUNFIRE line follows (GunfireLineTick).
          const uint32_t profile = ProfileOf(base, c);
          const uint32_t pad = Load32(base, by + kPad);
          if (profile && Ptr(pad))
          {
            Store32(base, profile + kHeardPad, pad);
          }
          if (now - g->gunfire_line_said > 20.0f || now < g->gunfire_line_said)
          {
            g->gunfire_line_at = now;
          }
          does = ": GUNFIRE of a guard: he joins in (Rare's reaction, pointed at the shooter's pad), his line HEAR_GUNFIRE";
        }
        if (Logging() && (now - g->loud_log > 5.0f || now < g->loud_log))
        {
          g->loud_log = now;
          REXLOG_INFO("pdz_lab_stealth: {} heard {}{} without seeing Jo (state {}, role {}){}{}",
                      Id(base, c), kind, by ? fmt::format(" by {}", by == Load32(base, kPlayerOne) ? std::string("Jo") : Id(base, by)) : std::string(),
                      StName(g->st), int(g->role), does,
                      g->role == 4 ? " (an alarm user: a run for the alarm is under the alarm-run rules)" : "");
        }
      }
      else
      {
        drop |= 0x1000;
        const uint32_t profile = ProfileOf(base, c);
        const uint32_t pad = profile ? Load32(base, profile + kHeardPad) : 0;
        g->noise_at = now;
        g->noise_pad = Ptr(pad) ? pad : 0;
        g->noise_silenced = silenced_shot;
        g->noise_why = nullptr;
        g->noise_fast = false;
      }
    }
    if (REXCVAR_GET(pdz_lab_stealth_body_sight) && REXCVAR_GET(pdz_lab_stealth_body_rules) && (word & 0x40))
    {
      drop |= 0x40; // a body: the body rules decide (log r5_2b: an alerted guard went into Rare's own body search)
    }
    const bool heard_loud = now >= g->loud_at && now - g->loud_at < 5.0f;
    const uint32_t jo_now = Load32(base, kPlayerOne);
    if (REXCVAR_GET(pdz_lab_stealth_near_miss) && (word & 0x80) && !heard_loud && g->meter < 100.0f && Ptr(jo_now))
    {
      // Round 11g (item 3; log 013 20:52:09.99: a silenced shot missed s1m5, meter 17, and Rare put him into
      // COMBAT). Jo's last shot (1 s) was silenced: the near miss is taken from Rare. Rare's own near-miss line has
      // been raised already. He is alert, his next look is where the shot came from (her pad), and his meter fills
      // faster while he looks for her.
      bool fired = false, silenced = false;
      for (const Shot &sh : g_shots)
      {
        if (sh.shooter == jo_now && now >= sh.at && now - sh.at < 1.0f)
        {
          fired = true;
          silenced = sh.silenced;
        }
      }
      if (fired && silenced)
      {
        drop |= 0x80;
        if (now - g->near_miss_at > 2.0f || now < g->near_miss_at)
        {
          g->near_miss_at = now;
          const uint32_t pad = Load32(base, jo_now + kPad);
          g->noise_at = now;
          g->noise_pad = Ptr(pad) ? pad : 0;
          g->noise_silenced = true;
          g->noise_why = "a SILENCED shot that missed him (a near miss: no fight before she is seen)";
          g->expect_until = now + 60.0f;
          g->alert_until = std::max(g->alert_until, now + float(REXCVAR_GET(pdz_lab_stealth_alert_s)));
          REXLOG_INFO("pdz_lab_stealth: {} NEAR MISS by a silenced shot (meter {:.0f}, state {}, {:.0f} cm from Jo): taken "
                      "from Rare (no COMBAT): he is alert, searches towards where the shot came from, his meter fills faster",
                      Id(base, c), g->meter, StName(g->st), Dist(base, c, jo_now));
        }
      }
    }
    if (REXCVAR_GET(pdz_lab_stealth_squad_combat) && (word & 0x8) && !heard_loud && !(word & 0x302) && g->meter < 100.0f)
    {
      // Round 11g (item 2; log 013 20:52:10.016: s1m1, 16 m away, meter 0, "alerted by a friend", in COMBAT at once).
      // A squad mate is fighting: Rare's instant squad notification is taken from him. He says his alert line and
      // goes to look where the squad mate is fighting (a noise investigation at the mate's pad); his meter fills as
      // usual if he sees her.
      uint32_t mate = 0;
      float mate_d = 1e9f;
      for (const auto &[other, o] : g_guards)
      {
        if (other == c || o.dead || o.st == St::Dead || !(o.st == St::Combat || o.st == St::Lost || o.combat_script != 0) ||
            (g->squad && o.squad != g->squad))
        {
          continue;
        }
        const float d = Dist(base, c, other);
        if (d < mate_d)
        {
          mate = other;
          mate_d = d;
        }
      }
      if (mate)
      {
        drop |= 0x8;
        if (now - g->friend_at > 10.0f || now < g->friend_at)
        {
          g->friend_at = now;
          const uint32_t pad = Load32(base, mate + kPad);
          g->noise_at = now;
          g->noise_pad = Ptr(pad) ? pad : 0;
          g->noise_silenced = false;
          g->noise_why = "his squad mate's fight (alerted by a friend: no instant squad combat before she is seen)";
          g->alert_until = std::max(g->alert_until, now + float(REXCVAR_GET(pdz_lab_stealth_alert_s)));
          REXLOG_INFO("pdz_lab_stealth: {} ALERTED BY A FRIEND ({} is fighting {:.0f} cm away; his own meter {:.0f}, state {}): "
                      "taken from Rare (no instant COMBAT): he says his alert line and goes to look there",
                      Id(base, c), Id(base, mate), mate_d, g->meter, StName(g->st));
        }
      }
    }
    if (REXCVAR_GET(pdz_lab_stealth_death_rules) && (word & 0x4) && !((word & ~drop) & 0x70A))
    {
      drop |= 0x4;
    }
  }
  drop |= R4ASneakStimulus(base, c, *g, word & ~drop, now); // (stealth4, fix 5: "saw a target" while she creeps behind him)
  if (g->r9_rares && !g->r10_far)
  {
    drop = 0; // (Thu 19 Nov: a rule above made him Rare's guard in this call (a second body): the word is Rare's, whole)
  }
  if (drop)
  {
    Store32(base, out, word & ~drop);
  }
  if (Logging() && (word != g->stim_logged || drop != g->stim_dropped || now - g->stim_logged_at > 10.0f || now < g->stim_logged_at))
  {
    g->stim_logged = word;
    g->stim_dropped = drop;
    g->stim_logged_at = now;
    static const std::pair<uint32_t, const char *> kBits[] = {
        {0x2, "damaged"}, {0x4, "warner"}, {0x8, "alerted by a friend"}, {0x10, "a friend died in view"},
        {0x20, "a hurt friend"}, {0x40, "a body"}, {0x80, "near miss"}, {0x100, "saw a valid target"},
        {0x200, "the alarm"}, {0x400, "gunfire, has a target"}, {0x1000, "heard a sound"}};
    std::string text, taken;
    for (const auto &[bit, name] : kBits)
    {
      if (word & bit)
      {
        text += std::string(text.empty() ? "" : ", ") + name;
      }
      if (drop & bit)
      {
        taken += std::string(taken.empty() ? "" : ", ") + name;
      }
    }
    REXLOG_INFO("pdz_lab_stealth: {} Rare's stimulus {:04X} ({}) in state {}{}", Id(base, c), word, text, StName(g->st),
                drop ? fmt::format(": taken from Rare: {} (he has not seen Jo: our rules)", taken) : std::string());
  }
}

// Round 5: the scheduler's "run this task on top of the current one" (r3 scheduler, r4 task): the task waits in the
// queue at +1416 and Rare's update pushes it on the 10-entry stack with no bounds check (the two crash dumps). A task
// that would not fit is refused exactly as this function refuses one itself when the character takes no tasks
// (82171264-82171270: sub_82171F80 ends and frees it, its holders are cleared).
REX_HOOK_RAW(sub_82171210)
{
  if (REXCVAR_GET(pdz_lab_stealth_stack_guard) && LabRun(base)) // (round 11: also after Jo went loud)
  {
    const uint32_t sched = ctx.r3.u32, task = ctx.r4.u32;
    const uint32_t top = Load32(base, sched + kStackTop);
    const uint32_t waiting = Load32(base, sched + kWaitingCount);
    if (Ptr(task) && top >= sched && top <= sched + kStackTop && waiting < 64 &&
        (top - sched) / kStackEntry + waiting >= kStackMax)
    {
      std::lock_guard<std::recursive_mutex> hold(g_lock);
      ++g_stack_refused;
      const float now = Clock(base);
      if (now - g_stack_refused_log > 1.0f || now < g_stack_refused_log)
      {
        g_stack_refused_log = now;
        const uint32_t c = sched - kScheduler;
        REXLOG_INFO("pdz_lab_stealth: STACK GUARD: {} task {:08X} (vtable {:08X}) refused, his task stack is full ({} "
                    "refusals so far): {}",
                    Id(base, c), task, Load32(base, task), g_stack_refused, StackText(base, c));
      }
      sub_82171F80(ctx, base);
      return;
    }
  }
  if (g_lab && REXCVAR_GET(pdz_lab_guard_stealth) && Ptr(ctx.r4.u32))
  {
    // Tue 17 Nov (the write record): the last task queued for a tracked guard, by Rare or by a layer rule.
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    R7Queued(base, ctx.r3.u32 - kScheduler, ctx.r4.u32, uint32_t(ctx.lr));
  }
  if (g_lab && REXCVAR_GET(pdz_test_stealth_push_log) && g_push_logged < 400)
  {
    // TESTING: who stacks tasks on a tracked guard (the caller's return address, the task's class).
    const uint32_t sched = ctx.r3.u32, task = ctx.r4.u32;
    const uint32_t c = sched - kScheduler;
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (Find(c) && Ptr(task))
    {
      ++g_push_logged;
      REXLOG_INFO("pdz_lab_stealth: push log: {} task {:08X} (vtable {:08X}) queued by {:08X}: {}", Id(base, c), task,
                  Load32(base, task), uint32_t(ctx.lr), StackText(base, c));
    }
  }
  __imp__sub_82171210(ctx, base);
}

// Round 6: Rare's corpse constructor: the corpse object of each dead character is noted (a body's finder walks to it).
REX_HOOK_RAW(sub_82559318)
{
  const uint32_t corpse = ctx.r3.u32, victim = ctx.r4.u32;
  __imp__sub_82559318(ctx, base);
  if (LabActive(base) && Ptr(corpse) && Ptr(victim))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (g_corpses.size() >= 96)
    {
      g_corpses.erase(g_corpses.begin(), g_corpses.begin() + 32);
    }
    Corpse k;
    k.object = corpse;
    k.victim = victim;
    k.at = Clock(base);
    g_corpses.push_back(k);
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: Rare made the corpse object {:08X} of {}", corpse, Id(base, victim));
    }
  }
}

namespace
{
  // Round 8 (decision A, 12 Nov: "standing still for Rare's 180 s cannot be intended"): pdz_lab_search_fallback.
  // Rare's SEARCH update (821DFFF0-821E0160) picks its next pad only from the zone of the guard's own pad, over 10 m
  // from it (squared 1e6 at 820A06C0), 30 random tries a frame; a guard whose room is smaller than that, or whose walk
  // to the picked pad ends at once, stands for the whole search (round 7's tests: s6s1, s6s3, s7s4, s11a-d of the
  // security squads, the same with the stealth code off). When a search of Rare's has had no go-to child for 3 s while
  // its guard stood (and it is 6 s old), a pad is chosen Rare-style and the search is given its go-to child exactly as Rare's own pick
  // gives it (821E0160-821E01B4: the squad's claim sub_8217CE38, task +152 = the pad, a go-to-pad task sub_821A3438 in
  // the holder at task +128, run in the task's scheduler +40 by sub_8217F0F0). The pad: reached from his own pad over
  // open pad links (Rare's pad graph), not flagged out (+52 0x80), not claimed by a squad mate (the squad's claim
  // list +540 / +544), over 3 m away, not one of his last four; of his own zone first, else of a linked neighbouring
  // zone; never a pad with no open link out while another is to be had; from the pad the search began at when his
  // own pad has no open link out; one of the farther half, at random. From then on that search gets its next pad half a second after each
  // walk ends. Rare's 180 s, its end and everything after it are untouched. Not for the stealth code's own legs
  // (sweeps, investigations, the alert look), which carry their own pads.
  struct Fallback
  {
    uint32_t task = 0;
    float ref[3] = {0.0f, 0.0f, 0.0f};
    float moved_at = -1e9f;   // he last moved 50 cm
    float idle_since = -1e9f; // the search has had no go-to child since
    int gives = 0;            // pads given to this search
    int dead = 0;             // pads given in a row that did not move him 1 m
    float give_pos[3] = {0.0f, 0.0f, 0.0f};
    bool off = false;         // given up for this search (no pad, or the walks do not start)
    uint32_t home = 0;        // his pad when the search got its first pad from us
    std::vector<uint32_t> last;
  };
  std::unordered_map<uint32_t, Fallback> g_fallback; // by character

  bool PadClaimed(const uint8_t *base, uint32_t squad, uint32_t c, uint32_t pad)
  {
    if (!Ptr(squad))
    {
      return false;
    }
    const uint32_t end = Load32(base, squad + 544);
    uint32_t n = Load32(base, squad + 540);
    for (int i = 0; Ptr(n) && n != end && i < 64; ++i, n = Load32(base, n + 16))
    {
      if (Load32(base, n + 8) == pad && Load32(base, n + 4) != c)
      {
        return true;
      }
    }
    return false;
  }

  // Before Rare's SEARCH update (game thread). `ours`: the stealth code runs this search as one of its own legs.
  void SearchFallback(PPCContext &ctx, uint8_t *base, uint32_t task, bool ours)
  {
    const uint32_t c = Load32(base, task + 0x10);
    if (!Ptr(c) || Load32(base, task + 12) != 1)
    {
      return;
    }
    const float now = Clock(base);
    Fallback &f = g_fallback[c];
    float me[3];
    PosOf(base, c, me);
    if (f.task != task)
    {
      f = Fallback();
      f.task = task;
      std::memcpy(f.ref, me, sizeof(me));
      f.moved_at = now;
    }
    const float mx = me[0] - f.ref[0], mz = me[2] - f.ref[2];
    if (mx * mx + mz * mz > 50.0f * 50.0f || now < f.moved_at)
    {
      std::memcpy(f.ref, me, sizeof(me));
      f.moved_at = now;
    }
    const double age = double(now) - LoadD(base, task + 0x78);
    if (Load32(base, task + 132) != 0 || Load32(base, task + 148) != 0)
    {
      f.idle_since = -1e9f; // walking, or a given pad is waiting
      return;
    }
    if (f.idle_since < -1e8f || now < f.idle_since)
    {
      f.idle_since = now;
    }
    if (ours || f.off || age < 2.0 || age > 175.0)
    {
      return;
    }
    const bool first = f.gives == 0;
    // The first pad: only after Rare's own pick has had 3 s without finding one (it tries 30 pads a frame), he has
    // stood for 3 s and the search is 6 s old (its start animation is over): a healthy search of Rare's has its
    // walk long before that and is never touched. Later pads: after half a second of Rare's pick finding none.
    if (first ? (now - f.moved_at < 3.0f || now - f.idle_since < 3.0f || age < 6.0) : now - f.idle_since < 0.5f)
    {
      return;
    }
    if (!first)
    {
      const float gx = me[0] - f.give_pos[0], gz = me[2] - f.give_pos[2];
      f.dead = gx * gx + gz * gz < 100.0f * 100.0f ? f.dead + 1 : 0;
      if (f.dead >= 6)
      {
        f.off = true;
        REXLOG_INFO("pdz_lab_search_fallback: {} six pads in a row did not move him: left to Rare's search (he stands): {}",
                    Id(base, c), StackText(base, c));
        return;
      }
    }
    const uint32_t own = Load32(base, c + kPad);
    float op[3];
    if (!PadPos(base, own, op))
    {
      return;
    }
    const int zone = int(int16_t(Load32(base, own + 60) & 0xFFFF));
    const uint32_t squad = SquadOf(ctx, base, c);
    if (!f.home)
    {
      f.home = own;
    }
    // Pads reached over open links from his pad; from the pad the search began at when his own pad has no open link
    // out (pad links run one way: tests r8_Aa, s11a and s11c walked onto such pads and stood again). A pad with no
    // open link out is only taken when there is nothing else.
    std::vector<std::pair<float, uint32_t>> same, linked, dead_ends;
    for (const uint32_t start : {own, f.home})
    {
      for (const auto &[pad, hop] : PadsAround(base, start, 14, 500))
      {
        float p[3];
        if (pad == own || !PadPos(base, pad, p) || (Load32(base, pad + 52) & 0x80) != 0 ||
            std::find(f.last.begin(), f.last.end(), pad) != f.last.end() || PadClaimed(base, squad, c, pad))
        {
          continue;
        }
        const float d = Dist3(p, op);
        if (d < 300.0f)
        {
          continue;
        }
        if (PadsAround(base, pad, 1, 3).size() < 2)
        {
          dead_ends.push_back({d, pad});
          continue;
        }
        (int(int16_t(Load32(base, pad + 60) & 0xFFFF)) == zone ? same : linked).push_back({d, pad});
      }
      if (!same.empty() || !linked.empty() || start == f.home)
      {
        break;
      }
    }
    std::vector<std::pair<float, uint32_t>> &from = !same.empty() ? same : !linked.empty() ? linked : dead_ends;
    if (from.empty())
    {
      if (f.last.empty())
      {
        f.off = true;
        REXLOG_INFO("pdz_lab_search_fallback: {} at ({:.0f} {:.0f} {:.0f}), pad {:08X} zone {}: no pad over 3 m away is reached "
                    "over open pad links from his pad: left to Rare's search (he stands)",
                    Id(base, c), me[0], me[1], me[2], own, zone);
      }
      f.last.clear(); // every pad was used lately: start again
      return;
    }
    std::sort(from.begin(), from.end());
    const size_t half = from.size() / 2;
    const size_t pick = half + std::min(from.size() - half - 1, size_t(Rand01() * float(from.size() - half)));
    const uint32_t pad = from[pick].second;
    // Rare's own hand-over of a picked pad (821E0160-821E01B4).
    if (Ptr(squad))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = squad;
      frame.ctx.r4.u64 = c;
      frame.ctx.r5.u64 = pad;
      sub_8217CE38(frame.ctx, base);
    }
    uint32_t go = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 96;
      sub_821784F0(frame.ctx, base);
      go = frame.ctx.r3.u32;
    }
    if (!Ptr(go))
    {
      return;
    }
    Store32(base, task + 152, pad);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = go;
      frame.ctx.r4.u64 = pad;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      sub_821A3438(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = task + 128;
      frame.ctx.r4.u64 = go;
      sub_821433D8(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = Load32(base, task + 40);
      frame.ctx.r4.u64 = Load32(base, task + 132);
      sub_8217F0F0(frame.ctx, base);
    }
    ++f.gives;
    std::memcpy(f.give_pos, me, sizeof(me));
    f.idle_since = -1e9f;
    f.last.push_back(pad);
    if (f.last.size() > 4)
    {
      f.last.erase(f.last.begin());
    }
    if (first || Logging())
    {
      float pp[3] = {};
      PadPos(base, pad, pp);
      REXLOG_INFO("pdz_lab_search_fallback: {} at ({:.0f} {:.0f} {:.0f}): Rare's search found him no pad{} ({:.0f} s into it): "
                  "pad {} of this search: {:08X} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away, {} ({} of his zone {} and {} of "
                  "linked zones to choose from)",
                  Id(base, c), me[0], me[1], me[2], first ? " and he stood for 3 s" : "", age, f.gives, pad, pp[0], pp[1], pp[2],
                  from[pick].first, same.empty() ? "of a linked neighbouring zone" : "of his own zone", same.size(), zone,
                  linked.size());
    }
  }
} // namespace

// SEARCH task update: marks the guard searching; pdz_test_stealth_search_s shortens Rare's 180 s.
REX_HOOK_RAW(sub_821DF9E0)
{
  const uint32_t task = ctx.r3.u32;
  uint32_t top_task = 0; // a tracked guard's SEARCH that is the top task of his stack
  uint32_t r2_beside_task = 0; // round 2: a SEARCH beside his DEFAULT script that the search cap ends in this update
  if (LabActive(base) && Ptr(task))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, task + 0x10);
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (g && g->r9_rares)
    {
      g->search_seen = Clock(base); // (Wed 18 Nov night: Rare's guard: his search is Rare's whole: no cap, no early end)
    }
    else if (g)
    {
      const float now = Clock(base);
      g->search_seen = now;
      double limit = REXCVAR_GET(pdz_test_stealth_search_s);
      const bool leg = g->st == St::Hunting;
      if (leg)
      {
        const double leg_s = REXCVAR_GET(pdz_lab_stealth_hunt_leg_s);
        if (leg_s > 0.0 && (limit <= 0.0 || leg_s < limit))
        {
          limit = leg_s; // a sweep leg: Rare's search, ended early by Rare's own 180 s path
        }
      }
      const double age = double(now) - LoadD(base, task + 0x78);
      // Round 3 (two crashes in r3_c, Rare's scheduler copying a list whose links were gone, both a frame or two after
      // a sweep leg was ended 2-8 s into its walk to the pad): a SEARCH is ended early (its start time moved back to
      // Rare's 180 s end) only once he stands still (no 40 cm move for 2 s) or is within 3 m of the search pad; a
      // glimpse while he walks only sets his next leg.
      float pp[3];
      bool at_pad = false, pad_known = false, inv_drift = false;
      float pad_d2 = 0.0f;
      if (PadPos(base, g->leg_pad ? g->leg_pad : g->search_pad, pp))
      {
        const float dx = pp[0] - LoadF(base, c + kPosition), dz = pp[2] - LoadF(base, c + kPosition + 8);
        pad_d2 = dx * dx + dz * dz;
        pad_known = true;
        at_pad = pad_d2 < 300.0f * 300.0f;
      }
      const bool standing = now - g->moved_at > 2.0f && now >= g->moved_at;
      const float stuck_s = float(REXCVAR_GET(pdz_lab_stealth_stuck_s));
      const bool no_progress = stuck_s > 0.0f && !at_pad && !g->stuck_look &&
                               age >= double(kStuckStart) && age < 180.0 &&
                               now - g->prog_at >= stuck_s && now >= g->prog_at;
      // Round 5: only the top task of his stack may be cut (see GiveSearch): a search Rare runs beside his DEFAULT
      // script (the alarm's) ends by Rare's own rules only.
      const uint32_t sched = c + kScheduler;
      const uint32_t top = Load32(base, sched + kStackTop);
      const bool is_top = !REXCVAR_GET(pdz_lab_stealth_stack_guard) ||
                          (top >= sched + kStackEntry && top <= sched + kStackTop && Load32(base, top - kStackEntry) == task &&
                           StackWaiting(base, c) == 0);
      g->search_top = is_top;
      top_task = is_top && REXCVAR_GET(pdz_lab_stealth_stack_guard) ? task : 0;
      const bool may_end = is_top && (at_pad || standing || age > 90.0 || REXCVAR_GET(pdz_test_stealth_no_stand_rule));
      // Round 4: an investigation leg ends a while after he reaches its pad (the first: his look beside the thing).
      const char *inv_why = nullptr;
      bool inv_stuck = false;
      if (g->st == St::Investigating && g->inv_pad)
      {
        if (at_pad && g->inv_arrived < -1e8f)
        {
          g->inv_arrived = now;
          if (g->inv_look)
          {
            g->inv_until = std::max(g->inv_until, now + g->inv_dwell + 1.0f); // round 11: the whole look at the spot
          }
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} reached leg {} pad {:08X} after {:.1f} s", Id(base, c), g->inv_leg, g->inv_pad,
                        now - g->inv_leg_at);
          }
        }
        const bool look = g->inv_look;
        const float dwell = g->inv_dwell;
        inv_stuck = no_progress && g->inv_arrived < -1e8f && !g->inv_end_leg &&
                    !R3cDLongLeg(c) && // (round 3c D: a reinforcement's long walk has its own watch: he pauses at gates)
                    !R3cCSlowStart(c, *g, age); // (round 3c C: a witness walk gets 8 s to set off)
        // Round 11: Rare's search, once its look at the pad is over, walks him on to a pad of its own choice: the leg
        // ends as soon as he is 5 m from the spot (no wandering).
        inv_drift = g->inv_arrived > -1e8f && pad_known && pad_d2 > 500.0f * 500.0f && !g->inv_end_leg;
        inv_why = g->inv_end_leg ? (now >= g->inv_until ? "time up" : g->inv_walk_cut ? "the body can be walked to now" : "her glimpse")
                  : inv_stuck ? "no progress: he does not get to that pad"
                  : inv_drift ? "Rare's search walks him off the spot"
                  : g->inv == Inv::Body && g->inv_look && g->inv_arrived < -1e8f && standing && age > 6.0 &&
                        REXCVAR_GET(pdz_lab_stealth_body_walk)
                      ? "he does not get to that pad"
                  : g->inv_arrived > -1e8f && now - g->inv_arrived >= dwell
                      ? (look ? "his look beside it is over" : "spot checked")
                  : age > 30.0 && !R3cDLongLeg(c) ? "30 s on this leg" // (round 3c D: not a reinforcement's long walk)
                               : nullptr;
      }
      if ((g->st == St::Unaware || g->st == St::Wary) && Looking(*g, now))
      {
        // Round 5: an alert guard's look round where he stands: over after pdz_lab_stealth_alert_look_s.
        if (now - g->look_started >= float(REXCVAR_GET(pdz_lab_stealth_alert_look_s)) && age < 180.0 && is_top)
        {
          StoreD(base, task + 0x78, double(now) - 181.0);
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} look round over after {:.0f} s: he carries on, alert for {:.0f} s more",
                        Id(base, c), now - g->look_started, std::max(0.0f, g->alert_until - now));
          }
          R3EndLine(ctx, base, c, *g, now, "his look round where he stands is over"); // round 3 (L)
        }
      }
      else if (inv_why)
      {
        // Time up or 30 s on one leg: also once he is 20 s into it (past the walk's start: round 3's crashes were 2-8 s
        // into a walk), as a guard who can't reach his pad never stands still (test r4_c).
        const bool late = is_top && age > 20.0 && (now >= g->inv_until || age > 30.0);
        const bool stuck_now = (inv_stuck || inv_drift) && is_top;
        if (age < 180.0 && (may_end || late || stuck_now))
        {
          g->inv_end_leg = false;
          g->cut_test = false;
          if (inv_stuck)
          {
            // Round 11 (item 5; was round 7's look in place): a pad walk that moved him less than 1 m in
            // pdz_lab_stealth_stuck_s: he gives that spot up at once and goes back to his post, wary (InvTick).
            g->inv_bad.push_back(g->inv_pad);
            g->stuck = 3;
            REXLOG_INFO("pdz_lab_stealth: {} NO PROGRESS: his walk to pad {:08X} ({} leg {}) moved him less than 1 m in "
                        "{:.1f} s ({:.0f} s into it): he gives that spot up at once: back to his post, wary",
                        Id(base, c), g->inv_pad, InvName(g->inv), g->inv_leg, stuck_s, age);
          }
          if (inv_why[0] == 'h' && inv_why[1] == 'e' && inv_why[3] == 'd') // "he does not get to that pad"
          {
            g->inv_bad.push_back(g->inv_pad);
          }
          StoreD(base, task + 0x78, double(now) - 181.0);
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} {} leg {} ended after {:.0f} s ({}{})", Id(base, c), InvName(g->inv), g->inv_leg,
                        age, inv_why, at_pad ? ", at its pad" : standing ? ", standing" : "");
          }
        }
      }
      else if (g->end_search && age < 180.0 && (may_end || (is_top && age > 20.0))) // never early in the walk (round 3's crashes)
      {
        g->end_search = false;
        g->cut_test = false;
        StoreD(base, task + 0x78, double(now) - 181.0);
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} search ended after {:.0f} s (state {}: {})", Id(base, c), age, StName(g->st),
                      g->cut_test ? "the test tool's cut" : g->calm_req ? "the alarm is over" : "a body he found");
        }
      }
      else if (leg && is_top && no_progress && g->leg_pad)
      {
        // Round 7 (item 2): a sweep leg that moved him less than 1 m in pdz_lab_stealth_stuck_s: the leg ends; his
        // next leg is a look round where he stands (NextLeg), then the sweep carries on.
        g->end_leg = false;
        g->stuck = 2;
        StoreD(base, task + 0x78, double(now) - 181.0);
        REXLOG_INFO("pdz_lab_stealth: {} NO-PROGRESS FALLBACK: his walk to sweep pad {:08X} moved him less than 1 m in {:.1f} s "
                    "({:.0f} s into the leg): he stops and looks round where he stands, then carries on",
                    Id(base, c), g->leg_pad, stuck_s, age);
      }
      else if (leg && g->stuck_look && is_top && age < 180.0 && age >= REXCVAR_GET(pdz_lab_stealth_look_s))
      {
        g->stuck_look = false;
        StoreD(base, task + 0x78, double(now) - 181.0); // the look in place is over
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} look round where he stands over after {:.0f} s: his sweep carries on", Id(base, c), age);
        }
      }
      else if (leg && g->end_leg && age < 180.0 && may_end)
      {
        g->end_leg = false;
        g->cut_test = false;
        StoreD(base, task + 0x78, double(now) - 181.0);
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} sweep leg ended after {:.0f} s (her glimpse: his next leg at her pad)", Id(base, c),
                      age);
        }
      }
      else if (limit > 0.0 && age > limit && age < 180.0 && may_end)
      {
        StoreD(base, task + 0x78, double(now) - 181.0); // Rare's own end of search runs this update
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} search ended after {:.0f} s ({})", Id(base, c), age,
                      leg ? "sweep leg, pdz_lab_stealth_hunt_leg_s" : "pdz_test_stealth_search_s");
        }
      }
      // Round 2, Sun 15 Nov (item 1): the search cap (lab_stealth_round2_Sun15Nov.inc). After the rules above, so a
      // search one of them has just ended (its start time is 181 s back) is left alone.
      // Rare's search after a sound runs BESIDE his DEFAULT script (the DEFAULT stays the top task; smoke run r2_lab,
      // s3g2): that form may be capped too, when the top task is the DEFAULT script and nothing waits to be pushed.
      // Its what-next handler is taken off below, so its end stacks no second DEFAULT script on the first.
      const bool r2_beside = !is_top && R2TopClass(base, c) == 0x82092A8Cu && StackWaiting(base, c) == 0;
      if (const double age_now = double(now) - LoadD(base, task + 0x78);
          R2SearchCap(base, c, *g, task, now, age_now, is_top || r2_beside, at_pad || standing) ||
          R3BTakeOver(base, c, *g, task, now, age_now, is_top || r2_beside, at_pad || standing)) // (round 3b: a witness's walk)
      {
        StoreD(base, task + 0x78, double(now) - 181.0); // Rare's own end of search runs this update
        r2_beside_task = r2_beside ? task : 0;
      }
    }
  }
  else if (g_loud && Ptr(task) && REXCVAR_GET(pdz_lab_stealth_stack_guard) && LabRun(base))
  {
    // Round 11: Jo went loud: nothing of the stealth code runs for this search, but the crash guard below stays (an
    // ended SEARCH on top of his stack loses its what-next handler, so no stack level is lost).
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, task + 0x10);
    if (Ptr(c) && Find(c))
    {
      const uint32_t sched = c + kScheduler;
      const uint32_t top = Load32(base, sched + kStackTop);
      if (top >= sched + kStackEntry && top <= sched + kStackTop && Load32(base, top - kStackEntry) == task &&
          StackWaiting(base, c) == 0)
      {
        top_task = task;
      }
    }
  }
  // (0.9.5: only with the master switch on. Stealth off must be Rare's game, his standing searches included.)
  if (REXCVAR_GET(pdz_lab_search_fallback) && REXCVAR_GET(pdz_lab_guard_stealth) && !lab_stealth::CounterOpGameNow() && Ptr(task) &&
      lab_restorations::CurrentScript() != 0)
  {
    // Round 8: a search of Rare's that finds no pad gets one (see SearchFallback). Before Rare's update: a search
    // whose own pick keeps choosing pads he cannot walk to has no go-to child at this point of the frame.
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    bool ours = false;
    if (REXCVAR_GET(pdz_lab_guard_stealth) && !g_loud)
    {
      const uint32_t c = Load32(base, task + 0x10);
      const Guard *g = Ptr(c) ? Find(c) : nullptr;
      ours = g && (g->st == St::Hunting || g->st == St::Investigating || g->st == St::Dead || g->end_search ||
                   ((g->st == St::Unaware || g->st == St::Wary) && Looking(*g, Clock(base))));
    }
    SearchFallback(ctx, base, task, ours);
  }
  __imp__sub_821DF9E0(ctx, base);
  if (r2_beside_task)
  {
    // Round 2 (item 1): the capped search ran beside his DEFAULT script, which is still there: the ended search's
    // what-next handler (a fresh DEFAULT script, pushed on top of the old one: one stack level lost) is taken off.
    const uint32_t status = Load32(base, r2_beside_task + 12), next = Load32(base, r2_beside_task + 32);
    const uint32_t handler = Load32(base, r2_beside_task + 52);
    if (status == 2 && next == 1 && handler)
    {
      Store32(base, r2_beside_task + 52, 0);
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t c = Load32(base, r2_beside_task + 0x10);
    if (Ptr(c) && Find(c) && (Logging() || StealthLog()))
    {
      REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} capped search {:08X} ran beside his DEFAULT script: status {} next {} after "
                  "Rare's update; its what-next handler {:08X} {}: {}",
                  Id(base, c), r2_beside_task, status, next, handler,
                  status == 2 && next == 1 && handler ? "taken off (his DEFAULT script is still running: none stacked on it)"
                                                       : "left alone",
                  StackText(base, c));
    }
  }
  if (top_task && Load32(base, top_task + 12) == 2 && Load32(base, top_task + 32) == 1)
  {
    // Round 5 (the crash's growth, seen in the stack readouts of r5_2b: "[SEARCH status 2] [DEFAULT]" after every
    // search that ended by time). A task that ends carries a "what next" handler (+52), run by the task's after-step
    // (sub_8216F978) the moment its status is 2: the SEARCH's queues a fresh DEFAULT script (sub_82171210), which the
    // scheduler then pushes ON TOP of the ended SEARCH before it gets to pop it, and the ended SEARCH stays under
    // it for good: one stack level lost per search (Rare's own searches too; ten, and the scheduler writes past the
    // stack: both dumps). With the handler taken off, the scheduler's next update pops the ended SEARCH and, the
    // stack being empty, builds his DEFAULT script itself (Rare's own path, sub_82171BC8 -> sub_8252EAA8): the same
    // result at depth 1. Only for a search that ended by time (+32 == 1: the DEFAULT script is next; one ended by a
    // sighting has Rare's alert script next and is left alone), and only when it is the top task.
    const uint32_t handler = Load32(base, top_task + 52);
    if (handler)
    {
      Store32(base, top_task + 52, 0);
      std::lock_guard<std::recursive_mutex> hold(g_lock);
      const uint32_t c = Load32(base, top_task + 0x10);
      Guard *g = Ptr(c) ? Find(c) : nullptr;
      if (g && Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} SEARCH {:08X} over: its what-next handler {:08X} (class {:08X}) taken off, so the "
                    "scheduler pops it and builds his DEFAULT script itself (no stack level lost): {}",
                    Id(base, c), top_task, handler, Ptr(handler) ? Load32(base, handler) : 0, StackText(base, c));
      }
    }
  }
}

// Mansion helper (Sun 15 Nov): Rare's radio call, logged only (nothing changed). The DEFAULT reaction sub_821D0748
// (r3 his DEFAULT script, r4 the character; its only caller is the DEFAULT update 821D05B0) sets "radio in use"
// (+0x187C 0x4000, 821D0844) when the call starts; the message task's run sub_821A6D70 (r3 the task, its 4 message
// bytes at +0x38) sends the level message that counts the detection (OnCommand 823326B0 type 7: script flags 7 / 8 / 9,
// the third fails the mission). Killed mid-call: the message never runs.
REX_EXTERN(__imp__sub_821D0748);
REX_EXTERN(__imp__sub_821A6D70);
namespace
{
  struct RadioCall
  {
    uint32_t c = 0;
    float at = 0.0f;
    std::string who;
  };
  std::vector<RadioCall> g_radio_calls; // calls started and not yet sent (oldest first)
  int g_radio_sent = 0;                 // calls completed in this run (the level's detections by radio)
  uint32_t g_radio_run = 0;             // the run they belong to (g_run_script); float g_radio_clock: the AI clock seen
  float g_radio_clock = -1.0f;
  void RadioRunCheck(float now)
  {
    if (g_radio_run != g_run_script || now + 1.0f < g_radio_clock)
    {
      g_radio_calls.clear();
      g_radio_sent = 0;
      g_radio_run = g_run_script;
    }
    g_radio_clock = now;
  }
} // namespace
REX_HOOK_RAW(sub_821D0748)
{
  const uint32_t c = ctx.r4.u32;
  if (!LabActive(base) || !RadioRow() || !Ptr(c))
  {
    __imp__sub_821D0748(ctx, base);
    return;
  }
  const bool before = (Load32(base, c + kSight) & kRadioInUse) != 0;
  const int state_before = RadioState(base);
  __imp__sub_821D0748(ctx, base);
  const bool after = (Load32(base, c + kSight) & kRadioInUse) != 0;
  if (!before && after)
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    RadioRunCheck(now);
    if (g_radio_calls.size() > 16)
    {
      g_radio_calls.erase(g_radio_calls.begin());
    }
    g_radio_calls.push_back({c, now, Id(base, c)});
    R3cDCallStarted(base, c, now); // round 3c (D): where the call comes from, for its reinforcements
    if (StealthLog())
    {
      REXLOG_INFO("pdz_stealth_radio: {} RADIO CALL STARTED (Rare's DEFAULT reaction; difficulty {}, radio state {} -> {}; {}) "
                  "at ({:.0f} {:.0f} {:.0f}), {:.1f} s: about 4.4 s of radio animation, then Rare's message (a detection); "
                  "killed before it = nothing sent",
                  Id(base, c), RadioDifficulty(base), state_before, RadioState(base),
                  InLayer(base, c) ? "a guard under the layer" : "Rare's guard (outside the layer)", LoadF(base, c + kPosition),
                  LoadF(base, c + kPosition + 4), LoadF(base, c + kPosition + 8), now);
    }
  }
}
REX_HOOK_RAW(sub_821A6D70)
{
  const uint32_t task = ctx.r3.u32;
  if (!LabActive(base) || !RadioRow() || !Ptr(task))
  {
    __imp__sub_821A6D70(ctx, base);
    return;
  }
  const uint8_t msg[4] = {base[task + 0x38], base[task + 0x39], base[task + 0x3A], base[task + 0x3B]};
  const int state_before = RadioState(base);
  __imp__sub_821A6D70(ctx, base);
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  const float now = Clock(base);
  RadioRunCheck(now);
  if (msg[1] != 7 && g_radio_calls.empty())
  {
    return; // (not a radio message: Rare's message tasks of other kinds)
  }
  // Follow-up (Sun 15 Nov; hand test 11:03:14: "tow3 RADIO CALL COMPLETED ... 19.47 s after the call started" was
  // tow2's call, 4.9 s old: tow3 and pat3 had been killed mid-call and their entries were still first in the list).
  // The message belongs to the task's own character (task +0x10, as the other tasks of this file) when he is in the
  // list. A call whose guard is dead, or older than 10 s (Rare's call lasts about 4.4 s), was never sent: it is
  // dropped with its own line. Without a match by character: the living call whose age is nearest 4.4 s.
  const uint32_t sender = Load32(base, task + 0x10);
  for (auto it = g_radio_calls.begin(); it != g_radio_calls.end();)
  {
    const bool alive = Ptr(it->c) && LoadF(base, it->c + kHealth) > 0.0f;
    if (it->c != sender && (!alive || now - it->at > 10.0f))
    {
      if (StealthLog())
      {
        REXLOG_INFO("pdz_stealth_radio: {} RADIO CALL NEVER SENT: it started {:.2f} s ago and {}: no message, no detection",
                    it->who, now - it->at, alive ? "is over 10 s old" : "he was killed before Rare's message");
      }
      it = g_radio_calls.erase(it);
    }
    else
    {
      ++it;
    }
  }
  RadioCall call;
  auto mine = std::find_if(g_radio_calls.begin(), g_radio_calls.end(), [sender](const RadioCall &r) { return r.c == sender; });
  if (mine == g_radio_calls.end() && !g_radio_calls.empty())
  {
    mine = std::min_element(g_radio_calls.begin(), g_radio_calls.end(), [now](const RadioCall &a, const RadioCall &b) {
      return std::fabs(now - a.at - 4.4f) < std::fabs(now - b.at - 4.4f);
    });
  }
  if (mine != g_radio_calls.end())
  {
    call = *mine;
    g_radio_calls.erase(mine);
  }
  ++g_radio_sent;
  R3cDCallCompleted(base, call.c, msg[3], now); // round 3c (D): the place its reinforcements go to
  if (StealthLog())
  {
    REXLOG_INFO("pdz_stealth_radio: {} RADIO CALL COMPLETED: Rare's message {{{:02X} {:02X} {:02X} {:02X}}} sent {:.2f} s after the "
                "call started (radio state {} -> {}): radio detection {} of this run (Rare counts it unless objective 0 is "
                "done; the third fails the mission)",
                call.who.empty() ? std::string("?") : call.who, msg[0], msg[1], msg[2], msg[3],
                call.who.empty() ? -1.0f : now - call.at, state_before, RadioState(base), g_radio_sent);
  }
}

// Round 3c, Sun 15 Nov night (helper D, fix 5): Rare's "set this character on that target" (r3 the character, r4 the
// target, r5 a flag byte; it fills his target holders +0x3158 / +0x3168 / +0x3178). The Mansion's level update calls
// it for each radio reinforcement with the player as the target (8232FFA4, 8233023C): that is how they know where Jo
// is. For a reinforcement under the layer the call is not made (lab_stealth_r3c_D_Sun15Nov.inc). Every other caller
// passes straight through (one compare).
REX_EXTERN(__imp__sub_8252D8D8);
REX_HOOK_RAW(sub_8252D8D8)
{
  const uint32_t caller = uint32_t(ctx.lr);
  if (caller >= kR3cDUpdateFrom && caller < kR3cDUpdateTo && LabActive(base))
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (R3cDNoTarget(base, ctx.r3.u32, ctx.r4.u32, caller))
    {
      return;
    }
  }
  __imp__sub_8252D8D8(ctx, base);
}

// Say-event: every line a tracked guard raises, Rare's and ours (round 3): Rare's SEE_BODY noted first, then the
// sighting-line rules, the take choice (lab_stealth_takes.inc) and the voice budget for all trooper lines.
REX_HOOK_RAW(sub_8262AA40)
{
  const uint32_t voice = ctx.r3.u32;
  uint32_t event = ctx.r4.u32;
  if (!LabActive(base) || voice <= kVoice)
  {
    __imp__sub_8262AA40(ctx, base);
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  const uint32_t c = voice - kVoice;
  Guard *g = Find(c);
  // Stealth fixes Mon 16 Nov (fix 2): a guard who says one of Rare's body / dead-friend lines acts on it.
  R4BBodyLine(base, c, g, event, Clock(base), g_our_line, uint32_t(ctx.lr));
  if (!g)
  {
    __imp__sub_8262AA40(ctx, base);
    return;
  }
  const float now = Clock(base);
  const bool ours = g_our_line;
  const char *what = ours ? g_say_what : "Rare's";
  const bool warn = ours && g_warn_saying; // round 11b: a warning line (WarnTick waited for the gap)
  // Mansion helper (Sun 15 Nov): THE ANNOUNCED RADIO CALL. Rare's DEFAULT reaction of a radio user asks for his radio
  // line (821D0988: event 158 / 159 in his own voice, r6 1) as the call starts, 20 instructions after setting his
  // "radio in use" bit; his alert script's WARN_TEAM a frame or two later took its place (survey m 3.5: never heard in
  // six calls). Here Rare's line is an ALERT line (it skips the squad speech arbiter, nothing of ours drops it, a
  // waiting one is never cut) and his own WARN_TEAM / SEE_PLAYER / SURPRISED lines are not said for
  // kRadioHold s, so the radio line is what the player hears as the call starts. Rare's call itself is untouched.
  constexpr float kRadioHold = 1.5f;     // Rare's own WARN_TEAM / SEE_PLAYER / SURPRISED wait this long
  constexpr float kRadioOursHold = 3.0f; // lines of ours (warnings, meter lines) wait this long (the radio line plays)
  bool radio_call = false;
  if (RadioEvent(event) && !ours && LoadF(base, c + kHealth) > 0.0f && LayerGuard(*g) && InLayer(base, c))
  {
    const bool in_use = (Load32(base, c + kSight) & kRadioInUse) != 0;
    const bool radio_user = RadioUser(base, c);
    radio_call = in_use && radio_user;
    if (StealthLog())
    {
      REXLOG_INFO("pdz_stealth_radio: {} ANNOUNCED CALL: Rare asks for his radio line {} ({}, {}_{}) (caller {:08X}, "
                  "radio in use {}, radio user {}, difficulty {}, radio state {}): {}",
                  Id(base, c), EventName(base, event), event, VoiceSetName(g->voice) ? VoiceSetName(g->voice) : "?",
                  EventName(base, event), uint32_t(ctx.lr), in_use ? "yes" : "no", radio_user ? "yes" : "no",
                  RadioDifficulty(base), RadioState(base),
                  radio_call ? fmt::format("an alert line (skips the squad arbiter); his WARN_TEAM / SEE_PLAYER / SURPRISED "
                                           "wait {:.1f} s", kRadioHold)
                             : std::string("not his call (Rare's line as it is)"));
    }
    if (radio_call)
    {
      g->radio_at = now;
      g->radio_ev = event;
    }
  }
  else if (RadioRow() && !RadioEvent(event) && now >= g->radio_at && now - g->radio_at < kRadioOursHold &&
           LoadF(base, c + kHealth) > 0.0f)
  {
    // (smoke 3, 10:28:14.079: tow1's queued radio line was dropped for our own warning line HEAR_GUNFIRE_SOLO 8 ms
    // later: any line of OURS waits kRadioOursHold s; Rare's own WARN_TEAM / SEE_PLAYER / SURPRISED kRadioHold s.
    // His death, pain and shot lines pass.)
    const std::string ev_name = EventName(base, event);
    const bool body_sound = ev_name.find("DEATH") != std::string::npos || ev_name.find("DIE") != std::string::npos ||
                            ev_name.find("PAIN") != std::string::npos || ev_name.find("SHOT") != std::string::npos ||
                            ev_name.find("HURT") != std::string::npos;
    if (!body_sound &&
        ((ours && !g_hold_retry) ||
         (now - g->radio_at < kRadioHold && (event == 143 || ev_name.find("SEE_PLAYER") != std::string::npos ||
                                             ev_name.find("SURPRISED") != std::string::npos))))
    {
      if (StealthLog())
      {
        REXLOG_INFO("pdz_stealth_radio: {} {} ({}, {}) not said: his radio line {} was asked for {:.2f} s ago (it is the "
                    "line heard as his call starts)",
                    Id(base, c), ev_name, event, what, EventName(base, g->radio_ev), now - g->radio_at);
      }
      ctx.r3.u64 = 0;
      return;
    }
  }
  if (event == 143 && LayerGuard(*g) && LoadF(base, c + kHealth) > 0.0f &&
      EventName(base, event) == "WARN_TEAM")
  {
    // Round 11 (item 3; log 010 17:02:47.814: s1m1, alone, saw Jo and Rare's search raised WARN_TEAM, "Get the hell
    // over here!", which sounds as if he shouts at her). (a) At a sighting (the second after his meter filled) Rare's
    // WARN_TEAM becomes his sighting line: SEE_PLAYER_SOLO, or SEE_PLAYER_AGAIN_SOLO after a lost contact. (b) Any
    // other WARN_TEAM (Rare's as an alarm run starts, our squad call's fallback) needs a live squad mate within
    // pdz_lab_stealth_warn_team_cm to answer it; else it is not said.
    const bool at_sighting = !ours && now >= g->spotted_at && now - g->spotted_at < 1.0f;
    const float reach = float(REXCVAR_GET(pdz_lab_stealth_warn_team_cm));
    uint32_t mate = 0;
    float mate_d = 1e9f;
    for (const auto &[other, o] : g_guards)
    {
      if (other == c || !g->squad || o.squad != g->squad || o.scientist || o.dead || o.st == St::Dead ||
          !(LoadF(base, other + kHealth) > 0.0f) || (Load32(base, other + kConfig) & kDormant) != 0)
      {
        continue;
      }
      const float d = Dist(base, c, other);
      if (d < mate_d)
      {
        mate = other;
        mate_d = d;
      }
    }
    const bool answered = mate && mate_d <= reach;
    if (at_sighting)
    {
      event = AfterContact(*g) ? kEvSeeAgain : kEvSeePlayer;
      ctx.r4.u64 = event;
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} WARN_TEAM (143, Rare's) at his sighting of Jo ({:.1f} s ago) becomes his sighting "
                    "line {} ({}); nearest live squad mate: {}",
                    Id(base, c), now - g->spotted_at, EventName(base, event), event,
                    mate ? fmt::format("{} at {:.0f} cm", Id(base, mate), mate_d) : std::string("none"));
      }
    }
    else if (!answered && !warn) // (round 11f: his alarm shout is said whoever is near)
    {
      if (Logging() && (now - g->warn_log >= 5.0f || now < g->warn_log))
      {
        g->warn_log = now;
        REXLOG_INFO("pdz_lab_stealth: {} WARN_TEAM (143, {}) not said: no live squad mate within {:.0f} cm to answer it "
                    "(nearest: {}) (state {})",
                    Id(base, c), what, reach, mate ? fmt::format("{} at {:.0f} cm", Id(base, mate), mate_d) : std::string("none"),
                    StName(g->st));
      }
      ctx.r3.u64 = 0;
      return;
    }
    else if (Logging() && (now - g->warn_log >= 5.0f || now < g->warn_log))
    {
      // Round 11i: THE CRASH of log 017 (21:43:02.164) was this log line. Since round 11f an alarm shout of ours is
      // said whoever is near, so it came here with no live squad mate at all (s3g8, 2.6 s after his last squad mate
      // s3g3 died): `mate` was 0 and Id(base, 0) read the setup pointer of character 0, guest address 0x54.
      g->warn_log = now;
      if (answered)
      {
        REXLOG_INFO("pdz_lab_stealth: {} WARN_TEAM (143, {}) allowed: squad mate {} is {:.0f} cm away (within {:.0f} cm)",
                    Id(base, c), what, Id(base, mate), mate_d, reach);
      }
      else
      {
        REXLOG_INFO("pdz_lab_stealth: {} WARN_TEAM (143, {}) said whoever is near (his alarm shout): nearest live squad mate: {}",
                    Id(base, c), what, mate ? fmt::format("{} at {:.0f} cm", Id(base, mate), mate_d) : std::string("none"));
      }
    }
  }
  const uint32_t admit_serial = g_admit_serial;
  const float last_end_before = g_last_line_end;
  if (radio_call && g->hold_ev && !g_hold_retry && g->hold_ev != event)
  {
    // Mansion helper (smoke 1, 10:20:20.164: ocp1's radio line was refused because his cut SEE_PLAYER_SOLO was held,
    // and that held line was then refused by the radio hold: nothing heard). His radio line takes the held line's place.
    if (StealthLog())
    {
      REXLOG_INFO("pdz_stealth_radio: {} ANNOUNCED CALL: his held alert line {} gives way to the radio line", Id(base, c),
                  EventName(base, g->hold_ev));
    }
    g->hold_ev = 0;
    g_holds = std::max(0, g_holds - 1);
  }
  if (g->hold_ev && !g_hold_retry && LoadF(base, c + kHealth) > 0.0f)
  {
    // Round 10b: an alert line of his is waiting for his cut chatter line to end: nothing else of his starts first.
    if (event != g->hold_ev && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} {} ({}, {}) not said: his alert line {} is waiting to start (state {})", Id(base, c),
                  EventName(base, event), event, what, EventName(base, g->hold_ev), StName(g->st));
    }
    ctx.r3.u64 = 0;
    return;
  }
  const auto said_before_it = g->said_at.find(event);
  const float said_before = said_before_it != g->said_at.end() ? said_before_it->second : -1e9f; // round 4: see below
  if (event == kEvSeeBody && !g->scientist && !g->body_pending && g->st != St::Combat && g->st != St::Lost &&
      g->st != St::Dead && g->st != St::Investigating) // round 4: an investigator at the body is already on it
  {
    // Rare's sight pass saw a body (825809C4) and his reaction walks him to it (821D1298): decided when he stops. Noted
    // whether or not the line itself plays.
    g->body_pending = true;
    g->body_at = now;
    g->moved_at = now;
    const Kill *near_kill = nullptr;
    int near_i = -1;
    float near_d = 2500.0f;
    for (size_t i = 0; i < g_kills.size(); ++i)
    {
      const Kill &k = g_kills[i];
      const float dx = k.pos[0] - LoadF(base, c + kPosition), dy = k.pos[1] - LoadF(base, c + kPosition + 4),
                  dz = k.pos[2] - LoadF(base, c + kPosition + 8);
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d <= near_d)
      {
        near_kill = &k;
        near_i = int(i);
        near_d = d;
      }
    }
    // Round 4 (12 (c)): a guard who never saw or fought Jo investigates the body alone first.
    if (RarePlus())
    {
      FillFaster(base, c, *g, now, "found a body (Rare's SEE_BODY)"); // (his reaction is Rare's; the line skips the arbiter)
    }
    const bool rules = !RarePlus() && REXCVAR_GET(pdz_lab_stealth_body_rules) && !g->seen_jo && !g->fought && g->voice >= 17 &&
                       g->voice <= 19 && g->st != St::Investigating && g->st != St::Dead;
    if (near_i >= 0 && near_i < 64 && ((g->bodies_seen >> near_i) & 1))
    {
      g->body_pending = false; // round 5: he noticed this body already (our body sight): nothing more to decide
    }
    if (near_kill && rules)
    {
      g->inv_body = near_i;
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} saw the body of {} ({:.0f} cm away, killed by Jo {:.0f} s ago: Rare's SEE_BODY, "
                    "state {}){}: he will search around it alone (body rules: not a confirmed contact, nobody is told)",
                    Id(base, c), near_kill->who, near_d, now - near_kill->at, StName(g->st),
                    near_kill->escalated ? " (already reported by another guard)" : "");
      }
    }
    else if (near_kill)
    {
      Confirm(base, c, *g, kCfBody,
              fmt::format("saw the body of {} ({:.0f} cm away, killed by Jo {:.0f} s ago: Rare's SEE_BODY)", near_kill->who,
                          near_d, now - near_kill->at));
      g->body_pending = false; // the hunt follows his search or walk (state machine)
    }
    if (Logging() && !(near_kill && rules))
    {
      REXLOG_INFO("pdz_lab_stealth: {} saw a body (Rare's SEE_BODY, state {}): a confirmed contact if Jo killed someone "
                  "where his walk ends",
                  Id(base, c), StName(g->st));
    }
  }
  // A line refused here never reaches Rare's speech code (r3 = 0, as the say-event returns when it plays nothing).
  // Logged once per guard and line per 5 s; Rare's per-frame repeats of a refused request are counted.
  auto refuse = [&](const std::string &why) {
    ++g->say_refused;
    if (radio_call && StealthLog())
    {
      REXLOG_INFO("pdz_stealth_radio: {} ANNOUNCED CALL line NOT STARTED: {}", Id(base, c), why);
    }
    if (Logging() && (event != g->refused_ev || now - g->refused_at >= 5.0f || now < g->refused_at))
    {
      REXLOG_INFO("pdz_lab_stealth: {} {} ({}, {}) not said: {} (state {}){}", Id(base, c), EventName(base, event), event,
                  what, why, StName(g->st),
                  g->refused_repeats ? fmt::format(" ({} refused requests since the last refusal logged)", g->refused_repeats)
                                     : std::string());
      g->refused_ev = event;
      g->refused_at = now;
      g->refused_repeats = 0;
    }
    else
    {
      ++g->refused_repeats;
    }
    ctx.r3.u64 = 0;
  };
  if (!g->scientist && (event == kEvSeePlayer || event == kEvSeePlayer + 1 || event == kEvSeeAgain || event == kEvSeeAgain + 1))
  {
    // One sighting line per moment (log 3, stealth_b). SEE_PLAYER_AGAIN only after lost contact; for a lost guard it
    // replaces Rare's first-sighting line (SEE_PLAYER); any second sighting line within 0.5 s is dropped.
    const bool lost = AfterContact(*g);
    const bool again = event == kEvSeeAgain || event == kEvSeeAgain + 1;
    const char *why = (!ours && R2BlindLine(base, c, *g, now)) // (round 2, item 2)
                          ? "his COMBAT began with no sighting (it becomes a search): no sighting line until he sees her"
                      : (again && !lost) ? "SEE_PLAYER_AGAIN without a lost contact"
                      : (!again && lost) ? "SEE_PLAYER_AGAIN replaces it after a lost contact"
                      : now - g->sighting_said < (REXCVAR_GET(pdz_lab_stealth_sighting_combat) ? 3.0f : 0.5f)
                          ? "a sighting line already played"
                                                      : nullptr;
    if (why)
    {
      refuse(why);
      return;
    }
  }
  if (!ours && (event == kEvAbandon || event == kEvAbandon + 1))
  {
    R3RareAbandon(ctx, base, c, *g, now); // round 3 (L): Rare's own end of a search: his give-up line is followed
  }
  if (!ours && (g->st == St::Unaware || g->st == St::Wary) && Looking(*g, now) &&
      (event == kEvAbandon || event == kEvAbandon + 1 || event == 43 || event == 44))
  {
    refuse("his look round where he stands (Rare's search start / end line)"); // round 5
    return;
  }
  if (!ours && g->st == St::Investigating && (event == kEvAbandon || event == kEvAbandon + 1 ||
                                               ((event == 43 || event == 44) && g->inv_leg > 1)))
  {
    // Round 4 (test r4_b): each leg is a Rare SEARCH; its start and its end say SEARCH_START / SEARCH_ABANDON. Inside
    // one investigation only the first leg's start line plays; the end of a leg is not the end of his search.
    refuse("a leg of his investigation (Rare's search start / end line inside it)");
    return;
  }
  // Round 10 (hand test 033, 19:11:45: SEE_PLAYER_SOLO "not said: another guard line is playing", then his alarm run
  // with no line): when a guard has just confirmed Jo, his sighting line always plays, and so does Rare's own call as
  // his alarm run starts (WARN_TEAM, 143: Rare raises it there; "Sound the alarm!" kind of takes). They skip the
  // one-voice rule and a waiting body line, and the ambient generic tannoy (LV05_SPEECH_TANNOY_GENERIC, not story)
  // does not hold them back; story speech (Chandra, scripted tannoys) still comes first.
  // 19:17 note: the one-voice rule is for CHATTER only (mutters, HEAR_UNKNOWN, search lines, LIGHT_SHOT_OUT, the
  // scientists' remarks). ALERT lines always play, over anybody's chatter: a sighting (SEE_PLAYER*), the alarm call
  // (WARN_TEAM), BEEN_SHOT / HURT / PAIN, the body call-in (SEE_BODY_TEAM). The fight's cap on lines at once stays.
  const bool body_call = g_prio_saying && now >= g->bodycall_at && now - g->bodycall_at < 1.0f;
  const bool must = (REXCVAR_GET(pdz_lab_stealth_alert_lines) && LayerGuard(*g) &&
                     LoadF(base, c + kHealth) > 0.0f && (AlertEvent(base, event) || body_call)) ||
                    radio_call; // (Mansion helper: Rare's radio line as his call starts)
  const bool strong = must && (StrongAlertEvent(base, event) || body_call || radio_call);
  const bool ambient_tannoy = must && g_story_name.find("TANNOY_GENERIC") != std::string::npos;
  if (!warn && !radio_call && !g_prio_saying && !g->scientist && g->st != St::Combat && g->st != St::Lost &&
      g->combat_script == 0 && !ambient_tannoy && StoryPlaying(base, now)) // (round 11g: a warning line plays alongside
                                                                           // story speech; Mansion: so does his call)
  {
    // Round 5 (item 7, log 027 07:41:52 / 07:42:04: s1m1's search mutters over the scientist bluff): Rare's scripted
    // speech first. Death and pain lines pass (they are not his to hold).
    const std::string ev_name = EventName(base, event);
    if (ev_name.find("DEATH") == std::string::npos && ev_name.find("DIE") == std::string::npos &&
        ev_name.find("SHOT") == std::string::npos && ev_name.find("PAIN") == std::string::npos && LoadF(base, c + kHealth) > 0.0f)
    {
      refuse("Rare's scripted speech is playing (story speech first)");
      return;
    }
  }
  const bool trooper = LayerGuard(*g); // (the Lab: troopers 17-19)
  const bool body_lines = REXCVAR_GET(pdz_lab_stealth_body_lines);
  if (body_lines && trooper && !must && !g_prio_saying && g->st != St::Combat && g->st != St::Lost && g->combat_script == 0 &&
      LoadF(base, c + kHealth) > 0.0f && PrioHolds(*g, now))
  {
    // Round 6 (item 2 (a)): a body line is due: it goes first. His own death and pain lines pass.
    const std::string ev_name = EventName(base, event);
    if (ev_name.find("DEATH") == std::string::npos && ev_name.find("PAIN") == std::string::npos &&
        ev_name.find("BEEN_SHOT") == std::string::npos &&
        (ev_name.find("DIE") == std::string::npos || ev_name.find("FRIEND_DIES") != std::string::npos))
    {
      refuse("a body line is due (SEE_BODY goes before the other guard lines)");
      return;
    }
  }
  if (!warn && !g_prio_saying && g->warn_ev && g->warn_queued < -1e8f && now >= g->warn_from && now - g->warn_from < 10.0f &&
      LoadF(base, c + kHealth) > 0.0f && (g->run_pending || !strong))
  {
    // Round 11h (item 2): his OWN warning line is due and not said yet: nothing else of his is asked for first (his
    // death and pain lines pass; a sighting line passes unless the line due is his alarm shout).
    const std::string ev_name = EventName(base, event);
    if (ev_name.find("DEATH") == std::string::npos && ev_name.find("PAIN") == std::string::npos &&
        (ev_name.find("DIE") == std::string::npos || ev_name.find("FRIEND_DIES") != std::string::npos))
    {
      refuse("his own warning line is due (it goes first: this line is dropped)");
      return;
    }
  }
  if (OneVoice() && !warn && !must && !g_prio_saying && LoadF(base, c + kHealth) > 0.0f && WarnDueNear(base, c, now))
  {
    // Round 11g (item 1): a warning line is due near him: ordinary lines are dropped, not delayed (his own death and
    // pain lines pass).
    const std::string ev_name = EventName(base, event);
    if (ev_name.find("DEATH") == std::string::npos && ev_name.find("PAIN") == std::string::npos &&
        ev_name.find("BEEN_SHOT") == std::string::npos &&
        (ev_name.find("DIE") == std::string::npos || ev_name.find("FRIEND_DIES") != std::string::npos))
    {
      refuse("a warning line is due within pdz_lab_stealth_voice_near_cm of him (it goes first: this line is dropped)");
      return;
    }
  }
  if (!warn && !must && !g_prio_saying && LoadF(base, c + kHealth) > 0.0f && !(trooper && Takes(base, g->voice, event)))
  {
    // Round 11k (item 3): a cue without a take table (fight chatter, the scientists' and the women's own lines):
    // when its voice set has said every one of its takes in the window, the line is not said. (The number of takes
    // is learned at the cue's first variation pick.) Pain and death lines pass.
    const char *vs = VoiceSetName(g->voice);
    const std::string ev_name = EventName(base, event);
    const auto known = vs ? g_cue_n.find(std::string(vs) + "_" + ev_name) : g_cue_n.end();
    if (known != g_cue_n.end() && known->second >= 1 && known->second <= 32 && ev_name.find("DEATH") == std::string::npos &&
        ev_name.find("DIE") == std::string::npos && ev_name.find("PAIN") == std::string::npos &&
        ev_name.find("SHOT") == std::string::npos && ev_name.find("HURT") == std::string::npos)
    {
      const uint32_t all_takes = known->second >= 32 ? 0xFFFFFFFFu : (1u << known->second) - 1;
      if ((all_takes & ~RecentTakes(known->first, now)) == 0)
      {
        refuse("his voice set said every take of this cue within pdz_lab_stealth_voice_set_s (no take left: not said)");
        return;
      }
    }
  }
  int take = 0;
  const TakeSet *set = nullptr;
  float len = 2.0f;
  if (trooper)
  {
    // The take (round 3 item 5 / 6): never one naming Jo "he"; "she" only after his own sighting or a lost contact;
    // un-whispered beyond 8 m from Jo; not his previous take of this cue. No allowed take: the cue is dropped.
    set = Takes(base, g->voice, event);
    if (set)
    {
      const bool may_she = g->seen_jo || AfterContact(*g);
      const uint32_t all = set->n >= 32 ? 0xFFFFFFFFu : (1u << set->n) - 1;
      uint32_t allowed = HeIsTheBody(set->cue) ? all : all & ~set->he;
      if (!may_she)
      {
        allowed &= ~set->she;
      }
      if (!allowed)
      {
        refuse(may_she ? "every take of the cue names Jo \"he\"" : "every take of the cue names Jo, and he has not seen her");
        return;
      }
      {
        std::string sib = set->cue;
        const size_t solo = sib.rfind("_SOLO");
        if (solo != std::string::npos && solo + 5 == sib.size())
        {
          sib.replace(solo, 5, "_TEAM");
          const TakeSet *team = TakesByCue(g->voice, sib);
          if (team && !AllowedTakes(*team, may_she, 0.0f, 0))
          {
            refuse(fmt::format("its {} version (Rare plays it with squad mates near) has no take he may say", sib));
            return;
          }
        }
      }
      if (!g_take_hook_ok && allowed != all)
      {
        refuse("the cue has takes naming Jo and the take pick could not be steered");
        return;
      }
      const uint32_t jo = Load32(base, kPlayerOne);
      const float d = Ptr(jo) ? Dist(base, c, jo) : 0.0f;
      if (d > 800.0f && (allowed & ~set->quiet))
      {
        allowed &= ~set->quiet;
      }
      const auto lt = g->last_take.find(event);
      if (lt != g->last_take.end() && lt->second >= 1 && (allowed & ~(1u << (lt->second - 1))))
      {
        allowed &= ~(1u << (lt->second - 1));
      }
      if (ours && g->mask_ev == event && now >= g->mask_at && now - g->mask_at < 1.0f)
      {
        // Round 4: a line of ours that needs one particular take (the squad call: WARN_TEAM take 3).
        const uint32_t m = (allowed & g->mask_takes) ? (allowed & g->mask_takes) : (all & g->mask_takes);
        if (m)
        {
          allowed = m;
        }
      }
      if (const char *vs = VoiceSetName(g->voice))
      {
        // Round 11k (item 3): not a take his voice set said within pdz_lab_stealth_voice_set_s. None left: the
        // line is not said; a warning or alert line gets the take said longest ago.
        bool all_recent = false;
        const uint32_t fresh = NotRecent(std::string(vs) + "_" + set->cue, allowed, now, all_recent);
        if (all_recent && !warn && !must)
        {
          refuse("his voice set said every take he may say of this cue within pdz_lab_stealth_voice_set_s (no take left: "
                 "not said)");
          return;
        }
        allowed = fresh;
      }
      if (allowed != all || g_take_hook_ok)
      {
        int bits[32];
        int nb = 0;
        for (uint32_t i = 0; i < set->n && i < 32; ++i)
        {
          if (allowed & (1u << i))
          {
            bits[nb++] = int(i);
          }
        }
        take = bits[std::min(nb - 1, int(Rand01() * float(nb)))] + 1;
        len = set->secs[take - 1];
      }
      else
      {
        len = *std::max_element(set->secs, set->secs + std::min<uint32_t>(set->n, 20));
      }
    }
    std::string name = EventName(base, event);
    // Round 6 (item 2 (d), log 062 16:55:36: a witness's FRIEND_DIES_UNAWARE started over another guard's line): a
    // living guard's "a friend died" line is a line like any other; only his own death lines skip the budget.
    const bool friend_dies = body_lines && name.find("FRIEND_DIES") != std::string::npos;
    const bool exempt = g->dead || !(LoadF(base, c + kHealth) > 0.0f) || name.find("DEATH") != std::string::npos ||
                        (name.find("DIE") != std::string::npos && !friend_dies);
    const bool alert = must && !exempt;
    // Round 11k (item 4; log 021 09:00:50.692: s3g4's SEE_PLAYER "not said: fight: the most guard lines at once"): a
    // first-contact line (SEE_PLAYER / SEE_PLAYER_AGAIN) is exempt from the fight's cap; the repeat rule stays.
    const bool first_contact = REXCVAR_GET(pdz_lab_stealth_sighting_combat) && must &&
                               (event == kEvSeePlayer || event == kEvSeePlayer + 1 || event == kEvSeeAgain || event == kEvSeeAgain + 1);
    if (const char *why = warn || radio_call
                              ? nullptr
                              : VoiceRefusal(*g, event, now, ours && g_optional, exempt || first_contact, alert, c, base,
                                             !ours && REXCVAR_GET(pdz_stealth_hurt_full) && HurtLineName(name)))
    {
      refuse(why);
      return;
    }
    if (alert)
    {
      // Round 10b (hand test 008: four alert lines "said", none played): Rare's speech drops a new request while the
      // speaker's speech task for an earlier line is alive (waiting or playing), whatever r6 says. So the speaker's
      // own line is looked at first:
      //   - a scripted line of Rare's, or an alert line of his own: this line is refused (never cut);
      //   - a chatter line: a strong alert line (a sighting, the alarm call, the body call-in) really cuts it (Rare's
      //     own stop for a playing sound, Rare's own give-up for a waiting one), and is raised again as soon as his
      //     speech is free (the speech update hook, at most 1.5 s); a shot / hurt line does not cut his own line (his
      //     scream is Rare's own sound and cuts by itself).
      const uint32_t pending = Load32(base, voice + 216);
      const uint32_t task = Load32(base, voice + 64);
      if (pending || Ptr(task))
      {
        const uint32_t busy_ev = pending ? pending : Load32(base, task + 52);
        const bool started = !pending && (base[task + 88] & 1) != 0;
        const char *busy_how = started ? "playing" : "waiting to start";
        if (!busy_ev)
        {
          refuse("he is speaking a scripted line of Rare's (never cut)");
          return;
        }
        if (AlertEvent(base, busy_ev) && !(radio_call && !started && !RadioEvent(busy_ev))) // (Mansion: his radio line
                                                                                             // cuts a line not yet started)
        {
          refuse(fmt::format("his own alert line {} is still {} (an alert line is never cut)", EventName(base, busy_ev), busy_how));
          return;
        }
        if (!strong)
        {
          refuse(fmt::format("his own line {} is still {} (a shot / hurt line does not cut it; his scream is Rare's own sound)",
                             EventName(base, busy_ev), busy_how));
          return;
        }
        // Round 11g (item 1): a line that has already started always plays to its end: it is not stopped any more.
        // The alert line is held and raised when his speech is free (the speech update hook, 5 s at most). A line
        // of his that is still waiting is dropped as before.
        if (started)
        {
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} {} ({}) waits for his own line {} to end (a line that has started is never cut)",
                        Id(base, c), name, event, EventName(base, busy_ev));
          }
        }
        else
        {
          CutOwnLine(base, c, now, busy_ev, started, name);
        }
        if (pending)
        {
          Store32(base, voice + 216, 0); // not a task yet: this line takes the request's place now
        }
        else
        {
          if (!started)
          {
            const float gone = -100.0f; // past Rare's give-up time: the task ends without a sound
            uint32_t bits;
            std::memcpy(&bits, &gone, 4);
            Store32(base, task + 68, bits);
          }
          if (!g->hold_ev)
          {
            ++g_holds;
          }
          g->hold_ev = event;
          g->hold_r5 = ctx.r5.u32;
          g->hold_at = now;
          g->hold_ours = ours;
          g->hold_what = what;
          ctx.r3.u64 = 0;
          return;
        }
      }
      // What it talks over (said plainly in the log): other speakers' chatter.
      std::string over;
      for (const Line &l : g_lines)
      {
        if (l.c != c && (l.end > now || (!l.played && now >= l.start && now - l.start < 2.5f)))
        {
          over += fmt::format(" {} {}", l.c ? Id(base, l.c) : std::string("staff"), l.ev ? EventName(base, l.ev) : std::string("(scripted line)"));
        }
      }
      if (strong)
      {
        ctx.r6.u64 = 1; // Rare's own "skip the speaker's gap and group rules": he may have spoken a moment ago
      }
      if (Logging() && !over.empty())
      {
        REXLOG_INFO("pdz_lab_stealth: {} {} ({}) is an ALERT line: it plays over the other speakers' lines still playing:{}",
                    Id(base, c), name, event, over);
      }
    }
    Admit(c, *g, event, now, len + 0.15f, alert);
    g_lines.back().warn = warn;
  }
  else if (g->scientist && ours)
  {
    // Round 10: the scientists' remarks of ours (LIGHT_SHOT_OUT) are chatter: one voice at a time, like the guards'.
    if (const char *why = warn ? nullptr : VoiceRefusal(*g, event, now, false, false, false, c, base))
    {
      refuse(why);
      return;
    }
    Admit(c, *g, event, now, warn ? g->sci_len + 0.15f : 3.0f); // (round 11c: a warning line waited for the gap itself)
    g_lines.back().warn = warn;
  }
  if (!g->scientist && (event == kEvSeePlayer || event == kEvSeePlayer + 1 || event == kEvSeeAgain || event == kEvSeeAgain + 1))
  {
    g->sighting_said = now;
  }
  g->last_event = event;
  g->last_event_time = now;
  if (!ours)
  {
    g_rare_last = now;
  }
  if (take > 0)
  {
    g->last_take[event] = uint8_t(take);
  }
  if (ours)
  {
    g_said = true;
  }
  if (Logging())
  {
    if (event == g->say_logged && now - g->say_logged_at < 2.0f)
    {
      ++g->say_skipped;
    }
    else
    {
      // (Round 3b, fix 1b: the cue really asked for, e.g. a scientist's SCI1_WARN_TEAM asked for as LIGHT_SHOT_OUT.)
      REXLOG_INFO("pdz_lab_stealth: {} says {} in state {}{}{}{}", Id(base, c), R3BSaysName(base, *g, event, warn),
                  StName(g->st), ours ? fmt::format(" (ours: {})", what) : std::string(),
                  take > 0 ? fmt::format(", take {} of {} ({:.2f} s)", take, set->n, len)
                  : set    ? std::string(", take: XACT's choice")
                           : std::string(),
                  g->say_skipped ? fmt::format(" ({} repeats of {} not logged)", g->say_skipped, EventName(base, g->say_logged))
                                 : std::string());
      g->say_logged = event;
      g->say_logged_at = now;
      g->say_skipped = 0;
    }
  }
  if (trooper)
  {
    g->pend_ev = event;
    g->pend_take = take;
    g->pend_n = set ? set->n : 0;
    g->pend_at = now;
  }
  const uint32_t queued_before = Load32(base, voice + 216);
  __imp__sub_8262AA40(ctx, base);
  // Rare's speech queues a request at speech+216 (research takes_and_playlog.md, step 3). It may queue the _TEAM
  // version of a _SOLO line, so any change there means queued; no change (and not this event) means it refused the
  // line itself (steps 1-2), and the budget's slot is given back at once.
  const uint32_t queued_after = Load32(base, voice + 216);
  const bool queued = queued_after != queued_before || queued_after == event;
  if (radio_call && StealthLog())
  {
    REXLOG_INFO("pdz_stealth_radio: {} ANNOUNCED CALL line {} by Rare's speech (request {} -> {})", Id(base, c),
                queued ? "queued: followed to its real start" : "NOT STARTED: Rare's speech did not queue it", queued_before,
                queued_after);
  }
  if (queued)
  {
    // Round 10b: followed to its real start (the speech update, speech task, stop and play hooks).
    const bool waiting = std::any_of(g_said_lines.begin(), g_said_lines.end(),
                                     [&](const Said &o) { return o.c == c && o.ev == event && o.stage == 0; });
    if (!waiting)
    {
      Said sd;
      sd.c = c;
      sd.ev = event;
      sd.at = now;
      sd.strong = strong || warn; // (round 11b: the squad's speech arbiter does not hold a warning line either)
      sd.what = what;
      g_said_lines.push_back(sd);
    }
  }
  else if (g_admit_serial == admit_serial)
  {
    // A line outside the voice budget (a scientist's own, a death line) that Rare's speech did not queue.
    ++g_rare_dropped;
    if (Logging())
    {
      REXLOG_INFO("pdz_voice_drop: {} {} ({}, {}) NOT PLAYED: Rare's speech did not queue it (its own gap or group rule, or "
                  "a line already pending) ({} so far)",
                  Id(base, c), EventName(base, event), event, what, g_rare_dropped);
    }
  }
  if (g_admit_serial != admit_serial && !g_lines.empty() && g_lines.back().c == c && g_lines.back().ev == event &&
      queued_after == queued_before && queued_after != event)
  {
    g_lines.pop_back();
    g_last_line_end = last_end_before;
    if (ours)
    {
      g->said_at[event] = said_before; // round 4: our line Rare's speech dropped was not said (its retry may come; r4_c)
    }
    g->pend_take = 0;
    ++g_rare_dropped;
    if (ours)
    {
      g_said = false;
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} {} ({}): Rare's speech did not queue it (its own gap or group rule, or a line "
                  "already pending): the voice slot is free again ({} so far){}",
                  Id(base, c), EventName(base, event), what, g_rare_dropped,
                  ours ? fmt::format(" | Rare's gate read: character flags {:08X} / {:08X}, group mask {:08X}, category "
                                     "mask {:08X}, bytes {} / {}, gap {:.2f} s",
                                     Load32(base, c + 6268), Load32(base, c + 6272), Load32(base, voice + 188),
                                     Load32(base, voice + 196), int(base[voice + 104]), int(base[voice + 105]),
                                     LoadF(base, voice + 100))
                       : std::string());
    }
  }
}

// --- Round 3: the take is applied, and every real start of a guard line logged (research: takes_and_playlog.md) ---
// The say-event only queues the line; Rare's speech task primes the sound one or more frames later
// (DarkAudioSound::Prime sub_82205C68, hooked in cue_fixes.cpp, which calls PrimeBegin / PrimeEnd). Inside Prime,
// XACT picks the variation once (sub_82C2BD80; the voice banks' cues are all one track, one PlayWave event, type 3
// random-no-repeat): take N of the research table = entry N-1 in the cue's XSB order. PrimeBegin arms the pick for a
// tracked guard's sound whose cue name ends with the event of his pending line; the pick hook returns that entry (and
// stores it as the cue's last index, as Rare's pick does). DarkAudioSound::Play (sub_822061C8) moving the sound from
// state 2 / 3 to 4 is the real start: logged (pdz_lab_stealth_log, "pdz_voice_play") with the take and how many guard
// lines were playing at that moment, and the voice budget's line moved to the real start.
namespace
{
  thread_local uint32_t t_prime_sound = 0;
  thread_local std::string t_prime_cue; // round 11k: the cue being primed for a tracked speaker (the voice set's takes)
  thread_local int t_picked = -1;
  thread_local uint32_t t_pick_n = 0;
  thread_local bool t_forced = false;
  thread_local uint32_t t_mismatch = 0;
  struct Primed
  {
    uint32_t owner = 0, ev = 0, n = 0;
    int take = 0;
    bool forced = false;
    bool ordered = false; // round 11b: a line of ours played on the speaker (no speech event)
    float at = 0.0f;
    std::string cue;
  };
  std::unordered_map<uint32_t, Primed> g_primed; // sound -> what was primed
  struct Play
  {
    float start = 0.0f, end = 0.0f;
    uint32_t owner = 0;
  };
  std::vector<Play> g_plays;
  uint32_t g_play_count = 0, g_play_overlaps = 0;

  std::string SoundName(const uint8_t *base, uint32_t sound)
  {
    const char *text = reinterpret_cast<const char *>(base + sound + 52);
    std::string name(text, strnlen(text, 255));
    if (name.rfind("XACT_SOUNDBANK_", 0) == 0)
    {
      name.erase(0, 15);
    }
    return name;
  }
  bool EndsWith(const std::string &s, const std::string &tail)
  {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
  }
  void CutPlays(uint32_t c, float now)
  {
    for (Play &pl : g_plays)
    {
      if (pl.owner == c && pl.end > now)
      {
        pl.end = now;
      }
    }
  }

  // Why the squad's speech arbiter (sub_82152E20) holds a line: its own rules, read back.
  std::string ArbiterWhy(PPCContext &ctx, uint8_t *base, const Said &s)
  {
    if (s.arbiter <= 0 || !Ptr(s.squad))
    {
      return "the sound system refused the sound (too far from the listener, a category limit, or no free cue)";
    }
    bool listed = false;
    for (int i = 0; i < 10; ++i)
    {
      listed = listed || Load32(base, s.squad + 524 + 8 * i) == s.c;
    }
    if (base[s.squad + 600] == 10 && !listed)
    {
      return "Rare's squad speech arbiter: the squad's list of speakers is full (10)";
    }
    uint32_t group = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = s.ev;
      sub_82153038(frame.ctx, base);
      group = frame.ctx.r3.u32;
    }
    if (s.arbiter == 1 || group > 64)
    {
      return fmt::format("Rare's squad speech arbiter: line group {} is closed", group);
    }
    constexpr uint32_t kGroups = 0x82E44B78; // 8 bytes per group: +6 the squad-wide gap in seconds
    const float since = LoadF(base, 0x82E2F334) - float(LoadD(base, s.squad + 8 * group));
    return fmt::format("Rare's squad speech arbiter: the squad said a line of its group ({}) {:.1f} s ago, and the group's "
                       "squad-wide gap is {} s",
                       group, since, int(base[kGroups + 8 * group + 6]));
  }
} // namespace

// Round 10b: the speech update. A queued request becomes a speech task only when the speaker has none; else Rare
// clears it (the line is lost: logged). A held alert line is raised here once his speech is free.
REX_HOOK_RAW(sub_8262A4E0)
{
  const uint32_t speech = ctx.r3.u32;
  if (!g_lab || !REXCVAR_GET(pdz_lab_guard_stealth) || speech <= kVoice)
  {
    __imp__sub_8262A4E0(ctx, base);
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  if (g_said_lines.empty() && g_holds <= 0)
  {
    __imp__sub_8262A4E0(ctx, base);
    return;
  }
  const uint32_t c = speech - kVoice;
  Guard *g = Find(c);
  if (!g)
  {
    __imp__sub_8262A4E0(ctx, base);
    return;
  }
  const float now = Clock(base);
  if (g->hold_ev)
  {
    const uint32_t ev = g->hold_ev;
    const bool alive = !g->dead && g->st != St::Dead && LoadF(base, c + kHealth) > 0.0f;
    const bool free_now = !Load32(base, speech + 64) && !Load32(base, speech + 216);
    if (!alive || free_now || now - g->hold_at > 5.0f || now < g->hold_at) // (round 11g: his own line plays to its end)
    {
      const std::string what = g->hold_what;
      const bool ours = g->hold_ours;
      const uint32_t r5 = g->hold_r5;
      const float held = now - g->hold_at;
      g->hold_ev = 0;
      g_holds = std::max(0, g_holds - 1);
      if (alive && free_now)
      {
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} alert line {} ({}): his speech is free {:.2f} s after the cut: raised now",
                      Id(base, c), EventName(base, ev), what, held);
        }
        const bool was_ours = g_our_line;
        const char *was_what = g_say_what;
        g_hold_retry = true;
        g_our_line = ours;
        g_say_what = ours ? what.c_str() : "";
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = speech;
          frame.ctx.r4.u64 = ev;
          frame.ctx.r5.u64 = r5;
          frame.ctx.r6.u64 = 1;
          frame.ctx.f1.f64 = 0.0;
          sub_8262AA40(frame.ctx, base);
        }
        g_our_line = was_ours;
        g_say_what = was_what;
        g_hold_retry = false;
      }
      else
      {
        ++g_not_played;
        if (Logging())
        {
          REXLOG_INFO("pdz_voice_drop: {} {} ({}, {}) held {:.2f} s, NOT PLAYED: {} | not played so far {}", Id(base, c),
                      EventName(base, ev), ev, what, held,
                      alive ? "his cut line's speech task did not end in time" : "he died", g_not_played);
        }
      }
    }
  }
  const uint32_t req = Load32(base, speech + 216);
  const uint32_t task_before = Load32(base, speech + 64);
  __imp__sub_8262A4E0(ctx, base);
  const uint32_t task_after = Load32(base, speech + 64);
  for (size_t i = 0; i < g_said_lines.size();)
  {
    Said &s = g_said_lines[i];
    if (s.c != c)
    {
      ++i;
      continue;
    }
    if (req && s.stage == 0)
    {
      if (Ptr(task_after) && task_after != task_before)
      {
        s.stage = 1;
        s.task = task_after;
      }
      else
      {
        const uint32_t busy_ev = Ptr(task_before) ? Load32(base, task_before + 52) : 0;
        const Said copy = s;
        g_said_lines.erase(g_said_lines.begin() + i);
        NotPlayed(base, copy, now,
                  !Ptr(task_before) ? std::string("no speech task could be made for it")
                                    : fmt::format("Rare's speech dropped the request: the speaker is busy with his earlier "
                                                  "line {} ({})",
                                                  busy_ev ? EventName(base, busy_ev) : std::string("(a scripted line)"),
                                                  (base[task_before + 88] & 1) ? "playing" : "waiting to start"));
        continue;
      }
    }
    if (now - s.at > 5.0f || now + 1.0f < s.at)
    {
      const Said copy = s;
      g_said_lines.erase(g_said_lines.begin() + i);
      NotPlayed(base, copy, now,
                copy.stage == 0   ? "the request was never taken up (no speech update)"
                : copy.stage == 1 ? "its speech task ended or was removed without a sound (his script changed, or he died)"
                                  : "its sound was made but did not start within 5 s (refused, or stopped)");
      continue;
    }
    ++i;
  }
}

// Round 10b: the squad's speech arbiter, asked by a waiting speech task every frame.
REX_HOOK_RAW(sub_82152E20)
{
  const uint32_t squad = ctx.r3.u32;
  __imp__sub_82152E20(ctx, base);
  if (t_task)
  {
    t_task_arbiter = int(ctx.r3.s32);
    t_task_squad = squad;
    if (t_task_strong && ctx.r3.u32 != 0 && REXCVAR_GET(pdz_lab_stealth_alert_lines))
    {
      // An alert line always plays: the squad-wide gap of its line group does not hold a sighting, the alarm call or
      // the body call-in back (Rare's arbiter would wait up to 2 s and then drop the line without a sound).
      t_task_overridden = true;
      ctx.r3.u64 = 0;
    }
  }
}

// Round 10b: the speech task's update: waiting (the arbiter, the sound system), started, or given up.
REX_HOOK_RAW(sub_821AEA48)
{
  const uint32_t task = ctx.r3.u32;
  if (!g_lab || !REXCVAR_GET(pdz_lab_guard_stealth) || !Ptr(task))
  {
    __imp__sub_821AEA48(ctx, base);
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  const auto find = [task]() {
    return std::find_if(g_said_lines.begin(), g_said_lines.end(), [task](const Said &s) { return s.stage == 1 && s.task == task; });
  };
  auto it = find();
  if (it == g_said_lines.end())
  {
    __imp__sub_821AEA48(ctx, base);
    return;
  }
  if (REXCVAR_GET(pdz_lab_stealth_voice_set_wait) && !(base[task + 88] & 1))
  {
    // Round 11k (item 3; log 021 09:00:23: s3g7 and s3g4, both TROOPER1, said HEAR_UNKNOWN take 4 at the same time):
    // two speakers with the same voice set never speak at once. While a line of his set is really playing from
    // another speaker, this task's update is not run (it waits as it is: no timer of Rare's runs), for 6 s at most.
    // One waiting line is let go at a time (the others wait half a second for its real start), a warning or alert
    // line before the others.
    Said &w = *it;
    const float now = Clock(base);
    const Guard *sg = Find(w.c);
    const char *set = sg && !sg->dead && sg->st != St::Dead && LoadF(base, w.c + kHealth) > 0.0f ? VoiceSetName(sg->voice) : nullptr;
    if (set)
    {
      uint32_t who = 0;
      bool busy = SetBusy(base, set, w.c, now, &who);
      if (!busy)
      {
        const auto rel = g_set_release.find(set);
        if (rel != g_set_release.end() && rel->second.c != w.c && now >= rel->second.at && now - rel->second.at < 0.5f)
        {
          busy = true;
          who = rel->second.c;
        }
      }
      if (!busy && !w.strong)
      {
        for (const Said &o : g_said_lines)
        {
          const Guard *og = o.c != w.c && o.strong && o.stage == 1 && o.wait_from > -1e8f && !o.wait_over ? Find(o.c) : nullptr;
          if (og && VoiceSetName(og->voice) == set)
          {
            busy = true;
            who = o.c;
            break;
          }
        }
      }
      if (busy && (w.wait_from < -1e8f || (now >= w.wait_from && now - w.wait_from < 6.0f)))
      {
        if (w.wait_from < -1e8f)
        {
          w.wait_from = now;
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} {} WAITS: {} of his voice set ({}) is speaking: it starts when that line has ended",
                        Id(base, w.c), EventName(base, w.ev), Ptr(who) ? Id(base, who) : std::string("another speaker"), set);
          }
        }
        w.at = now; // (the trace's own 5 s limit counts from the end of the wait)
        return;
      }
      if (w.wait_from > -1e8f && !w.wait_over)
      {
        w.wait_over = true;
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} {} waited {:.2f} s for his voice set ({}): {}", Id(base, w.c), EventName(base, w.ev),
                      now - w.wait_from, set, busy ? "6 s are up: it is let go" : "the set is free: it starts now");
        }
      }
      SetRelease &r = g_set_release[set];
      r.at = now;
      r.c = w.c;
    }
  }
  const uint8_t flags_before = base[task + 88];
  t_task = task;
  t_task_arbiter = -1;
  t_task_start = -1;
  t_task_strong = it->strong;
  t_task_overridden = false;
  __imp__sub_821AEA48(ctx, base);
  t_task = 0;
  it = find();
  if (it == g_said_lines.end())
  {
    return;
  }
  Said &s = *it;
  const float now = Clock(base);
  if (t_task_arbiter >= 0)
  {
    s.arbiter = t_task_overridden ? 0 : t_task_arbiter;
    s.squad = t_task_squad;
    if (t_task_overridden && !s.overridden)
    {
      s.overridden = true;
      Said held = s;
      held.arbiter = t_task_arbiter;
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} {} is an ALERT line: it is not held back by {}", Id(base, s.c), EventName(base, s.ev),
                    ArbiterWhy(ctx, base, held));
      }
    }
  }
  if (t_task_start >= 0)
  {
    s.start = t_task_start;
  }
  const uint8_t flags_after = base[task + 88];
  if (t_task_start == 0)
  {
    s.stage = 2; // the sound is made: its real start is the play hook's
    s.sound = Load32(base, s.c + kVoice + 4);
    return;
  }
  if (t_task_arbiter > 0 || t_task_start > 0)
  {
    ++s.tries;
  }
  if (!(flags_before & 1) && (flags_after & 1))
  {
    const Said copy = s;
    g_said_lines.erase(it);
    NotPlayed(base, copy, now,
              fmt::format("Rare's speech task gave up after waiting ({} tries): {}", copy.tries, ArbiterWhy(ctx, base, copy)));
  }
  else if (Load32(base, task + 12) == 3)
  {
    const Said copy = s;
    g_said_lines.erase(it);
    NotPlayed(base, copy, now, "Rare's speech task ended at once: his character may not speak now (Rare's own test)");
  }
}

// Round 10b: the speaker's sound is stopped (a newer sound of his, his death): a line stopped before its real start.
REX_HOOK_RAW(sub_8262CCB8)
{
  const uint32_t speech = ctx.r3.u32;
  if (g_lab && REXCVAR_GET(pdz_lab_guard_stealth) && speech > kVoice)
  {
    const uint32_t sound = Load32(base, speech + 4);
    if (Ptr(sound))
    {
      std::lock_guard<std::recursive_mutex> hold(g_lock);
      const uint32_t c = speech - kVoice;
      const auto it = std::find_if(g_said_lines.begin(), g_said_lines.end(),
                                   [&](const Said &s) { return s.c == c && s.stage == 2 && s.sound == sound; });
      const uint32_t state = Load32(base, sound + 340);
      if (it != g_said_lines.end() && state != 4)
      {
        const Said copy = *it;
        g_said_lines.erase(it);
        NotPlayed(base, copy, Clock(base),
                  fmt::format("its sound was stopped before it started (sound state {}): a newer sound of his (a scream, a "
                              "scripted line) or his death",
                              state));
      }
    }
  }
  __imp__sub_8262CCB8(ctx, base);
}

namespace lab_stealth
{
  void PrimeBegin(const uint8_t *base, uint32_t sound)
  {
    t_prime_sound = 0;
    t_force_take = 0;
    t_picked = -1;
    t_forced = false;
    t_mismatch = 0;
    t_force_mask = 0;
    t_ordered = false;
    t_prime_cue.clear();
    if ((!g_lab && !(g_loud && g_lab_run)) || !REXCVAR_GET(pdz_lab_guard_stealth) || !Ptr(sound))
    {
      return; // (round 11k: after LOUD only an ordered take, the alarm shout's, is steered)
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t owner = Load32(base, sound + 12);
    if (!g_take_orders.empty())
    {
      // Round 11b: a line played on a speaker with its takes chosen (a scientist's warning, a lamp comment, the
      // lights-out talk for shot-out lamps).
      const std::string ordered = SoundName(base, sound);
      const float t = Clock(base);
      for (auto it = g_take_orders.begin(); it != g_take_orders.end();)
      {
        if (t < it->at || t - it->at > 6.0f)
        {
          it = g_take_orders.erase(it);
          continue;
        }
        if (it->cue == ordered)
        {
          t_prime_sound = sound;
          t_force_mask = it->takes;
          if (bool all_recent = false; g_lab && SpeechSetCue(ordered))
          {
            t_force_mask = NotRecent(ordered, it->takes, t, all_recent); // (round 11k: not a take the set said lately)
            t_prime_cue = ordered;
          }
          if (!g_mesa_done && REXCVAR_GET(pdz_lab_mesa_first) && ordered.find("SCI1_LIGHTS_OUT_NEXT_QUESTION") != std::string::npos)
          {
            g_mesa_done = true; // round 11l (item 2): the run's first lights-out question is the "Mesa" take
            t_force_mask = 0x4;
            REXLOG_INFO("pdz_lab_stealth: lights-out conversation, the first in this run: {} plays take 3 (\"Mesa\")", ordered);
          }
          t_ordered = true;
          g_take_orders.erase(it);
          return;
        }
        ++it;
      }
    }
    {
      // Round 8: the scientists' lights-out lines (restorations.cpp): the take is XACT's own random pick and is
      // logged (PrimeEnd); TESTING: pdz_test_lights_out_take forces one take of SCI1's question once.
      const std::string lights = SoundName(base, sound);
      if (lights.find("LIGHTS_OUT_NEXT_") != std::string::npos)
      {
        t_prime_sound = sound;
        static int32_t used = 0;
        const int32_t want = REXCVAR_GET(pdz_test_lights_out_take);
        if (want != used && lights.find("SCI1_LIGHTS_OUT_NEXT_QUESTION") != std::string::npos)
        {
          used = want;
          if (want >= 1 && want <= 5)
          {
            t_force_take = want;
            t_force_n = 5;
            REXLOG_INFO("pdz_lab_stealth: test: {} primed: take {} of 5 forced (pdz_test_lights_out_take)", lights, want);
          }
        }
        if (t_force_take == 0 && !g_mesa_done && REXCVAR_GET(pdz_lab_mesa_first) &&
            lights.find("SCI1_LIGHTS_OUT_NEXT_QUESTION") != std::string::npos)
        {
          g_mesa_done = true; // round 11l (item 2): the run's first lights-out question is the "Mesa" take
          t_force_take = 3;
          t_force_n = 5;
          REXLOG_INFO("pdz_lab_stealth: lights-out conversation, the first in this run: {} plays take 3 of 5 (\"Mesa\")", lights);
        }
        return;
      }
    }
    if (!g_lab)
    {
      return; // (after LOUD: only an ordered take and the lights-out question above)
    }
    Guard *g = Ptr(owner) ? Find(owner) : nullptr;
    if (!g)
    {
      // Round 5 (item 6): our tannoy line (no owner): the take the rules chose, of the cue's three.
      const float t = Clock(base);
      if (g_tannoy_takes && t >= g_tannoy_takes_at && t - g_tannoy_takes_at < 6.0f &&
          SoundName(base, sound) == "LV05_SPEECH_TANNOY_SECURITY12")
      {
        const int take = PickTake(g_tannoy_takes, 3, Rand01());
        g_tannoy_takes = 0;
        if (take > 0)
        {
          t_prime_sound = sound;
          t_force_take = take;
          t_force_n = 3;
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: tannoy TANNOY_SECURITY12 primed: take {} of 3 forced", take);
          }
        }
      }
      return;
    }
    t_prime_sound = sound;
    const float now = Clock(base);
    const std::string name = SoundName(base, sound);
    if (SpeechSetCue(name))
    {
      t_prime_cue = name; // (round 11l: the voice set's take memory is for speech cues only)
    }
    if (g->pend_take > 0 && now >= g->pend_at && now - g->pend_at < 5.0f && EndsWith(name, "_" + EventName(base, g->pend_ev)))
    {
      t_force_take = g->pend_take; // the cue asked for: the take chosen at the say-event
      t_force_n = g->pend_n;
      g->pend_take = 0;
      return;
    }
    // Another cue of his (Rare's _TEAM for a _SOLO event, or a line we never saw asked for): its own take table.
    if (Gangster(g->voice) && !g->scientist)
    {
      // Nightclub helper: GANGSTA1-3 have no take table; the takes chosen for his swapped warning line (the meter's
      // lines: PLAYER_CLOAKED_NEXT_SOLO 0x05 / 0x12) are forced as a mask (XACT's own pick if none of them exists).
      const size_t us = name.find('_');
      if (g->alt_cue && g->alt_takes && now >= g->alt_at && now - g->alt_at < 5.0f && us != std::string::npos &&
          name.compare(us + 1, std::string::npos, g->alt_cue) == 0)
      {
        t_force_mask = g->alt_takes;
        g->alt_cue = nullptr;
      }
      return;
    }
    if (g->voice < 17 || g->voice > 19 || g->scientist)
    {
      return;
    }
    const size_t us = name.find('_');
    const TakeSet *set = us == std::string::npos ? nullptr : TakesByCue(g->voice, name.substr(us + 1));
    if (!set)
    {
      return;
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    const auto lt = g->last_take.find(0x10000u + uint32_t(set - kTakes));
    uint32_t allowed = AllowedTakes(*set, g->seen_jo || AfterContact(*g), Ptr(jo) ? Dist(base, owner, jo) : 0.0f,
                                    lt != g->last_take.end() ? lt->second : 0);
    if (g->alt_cue && now >= g->alt_at && now - g->alt_at < 5.0f && std::strcmp(set->cue, g->alt_cue) == 0)
    {
      // Round 11f (item 1): Rare plays the _TEAM cue for his warning line: only the takes chosen for it.
      const uint32_t in_set = g->alt_takes & (set->n >= 32 ? 0xFFFFFFFFu : (1u << set->n) - 1);
      if (allowed & in_set)
      {
        allowed &= in_set;
      }
      else if (in_set)
      {
        allowed = in_set;
      }
      g->alt_cue = nullptr;
    }
    if (now >= g->nevermind_at && now - g->nevermind_at < 4.0f && std::strcmp(set->cue, "HEAR_NOISE_NOT_INVESTIGATE_TEAM") == 0)
    {
      // Round 4: a lamp investigator's own "never mind": only the takes that work said to nobody in particular
      // (TROOPER1 "Nevermind." / "Probably just the wind." / "Maybe a bird or something." / "Don't know, don't care.";
      // TROOPER2 "Never mind, you're just jumpy." / "...a bird, or a rat..." / "I don't know, and I care even less.";
      // TROOPER3 "Never mind." / "Could be anything, don't worry.").
      const uint32_t mask = g->voice == 17 ? 0x17u : g->voice == 18 ? 0x15u : 0x03u;
      if (allowed & mask)
      {
        allowed &= mask;
      }
      else if (set->n >= 1)
      {
        allowed = mask & ((1u << set->n) - 1);
      }
      g->nevermind_at = -1e9f;
    }
    if (now >= g->bodycall_at && now - g->bodycall_at < 4.0f && std::strcmp(set->cue, "SEE_BODY_TEAM") == 0 && set->n >= 5)
    {
      allowed = (allowed & 0x1Fu) ? (allowed & 0x1Fu) : 0x1Fu; // round 6: the squad call: takes 1-5
      g->bodycall_at = -1e9f;
    }
    if (now >= g->allclear_at && now - g->allclear_at < 4.0f && std::strcmp(set->cue, "SEARCH_ABANDON_TEAM") == 0 && set->n >= 2)
    {
      allowed = 0x2; // round 5: "All clear" (take 2 in every trooper voice)
      g->allclear_at = -1e9f;
    }
    {
      bool all_recent = false; // round 11k (item 3): not a take his voice set said lately (none left: the oldest)
      allowed = NotRecent(name, allowed, now, all_recent);
    }
    const int take = PickTake(allowed, set->n, Rand01());
    if (take > 0)
    {
      t_force_take = take;
      t_force_n = set->n;
      g->last_take[0x10000u + uint32_t(set - kTakes)] = uint8_t(take);
    }
    else if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} {}: no take he may say (XACT's pick plays: the line was let through for its "
                  "other version)",
                  Id(base, owner), name);
    }
  }

  void PrimeEnd(const uint8_t *base, uint32_t sound)
  {
    if (!t_prime_sound || t_prime_sound != sound)
    {
      t_prime_sound = 0;
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    Primed p;
    p.owner = Load32(base, sound + 12);
    p.cue = SoundName(base, sound);
    p.take = t_picked >= 0 ? t_picked + 1 : 0;
    p.n = t_pick_n;
    p.forced = t_forced;
    p.ordered = t_ordered;
    p.at = now;
    if (t_ordered && p.take > 0)
    {
      if (Guard *speaker = Find(p.owner); speaker && speaker->scientist)
      {
        speaker->sci_last[p.cue] = uint8_t(p.take - 1); // round 11j: his last take of this cue
      }
    }
    const Guard *g = Find(p.owner);
    if (g && EndsWith(p.cue, "_" + EventName(base, g->pend_ev)))
    {
      p.ev = g->pend_ev;
    }
    if (g_primed.size() > 64)
    {
      for (auto it = g_primed.begin(); it != g_primed.end();)
      {
        it = (now - it->second.at > 10.0f || now < it->second.at) ? g_primed.erase(it) : std::next(it);
      }
    }
    if (p.take > 0 && Ptr(p.owner) && Find(p.owner) && SpeechSetCue(p.cue))
    {
      NoteSetPrime(p.cue, p.take, p.owner, sound, now); // round 11k: the voice set's takes (11l: speech cues only)
    }
    if (p.n >= 1 && Ptr(p.owner) && SpeechSetCue(p.cue))
    {
      g_cue_n[p.cue] = p.n;
    }
    t_prime_cue.clear();
    g_primed[sound] = p;
    if (p.cue.find("LIGHTS_OUT_NEXT_") != std::string::npos)
    {
      REXLOG_INFO("pdz_lab_stealth: lights-out line {}: take {} of {} ({})", p.cue, p.take, p.n,
                  t_ordered && p.forced ? "chosen from the takes that suit a few dead lamps"
                  : p.forced            ? "forced by the test tool"
                                        : "XACT's own random pick");
    }
    else if (t_ordered && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: staff line {}: take {} of {} ({})", p.cue, p.take, p.n,
                  p.forced ? "from the takes chosen for it" : "XACT's own pick: none of the chosen takes exists in the cue");
    }
    if (t_mismatch && Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: take not steered for {} ({}): the cue has {} entries, the take table {} (XACT's pick "
                  "{})",
                  Ptr(p.owner) ? Id(base, p.owner) : std::string("?"), p.cue, t_mismatch, t_force_n, p.take);
    }
    t_prime_sound = 0;
    t_force_take = 0;
    t_force_mask = 0;
    t_ordered = false;
  }

  // Round 11b: the next Prime of this cue (name without XACT_SOUNDBANK_) within 3 s picks one of these takes.
  void OrderTake(const uint8_t *base, const std::string &cue, uint32_t takes)
  {
    if (!takes)
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (g_take_orders.size() > 16)
    {
      g_take_orders.erase(g_take_orders.begin());
    }
    TakeOrder o;
    o.cue = cue;
    o.takes = takes;
    o.at = Clock(base);
    g_take_orders.push_back(o);
  }
} // namespace lab_stealth

// XACT's variation pick: the armed take for a guard line being primed, else XACT's own pick.
REX_HOOK_RAW(sub_82C2BD80)
{
  if (!t_prime_sound || !Ptr(ctx.r3.u32))
  {
    __imp__sub_82C2BD80(ctx, base);
    return;
  }
  const uint32_t h = Load32(base, ctx.r3.u32 + 8);
  const uint32_t count = Ptr(h) ? (Load32(base, h) & 0xFFFF) : 0;
  if (t_force_mask && count > 0)
  {
    // Round 11b: one of the takes chosen for a line played on a speaker; not the cue's previous take when another
    // of them is left (XACT's own no-repeat rule).
    const uint32_t mask = t_force_mask;
    t_force_mask = 0;
    const uint32_t last = (uint32_t(base[h + 4]) << 8) | base[h + 5];
    int bits[32];
    int nb = 0;
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
      if (mask & (1u << i))
      {
        bits[nb++] = int(i);
      }
    }
    if (nb > 1)
    {
      for (int i = 0; i < nb; ++i)
      {
        if (uint32_t(bits[i]) == last)
        {
          bits[i] = bits[--nb];
          break;
        }
      }
    }
    if (nb > 0)
    {
      float r = 0.0f;
      {
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        r = Rand01();
      }
      const uint32_t index = uint32_t(bits[std::min(nb - 1, int(r * float(nb)))]);
      Store16(base, h + 4, uint16_t(index));
      ctx.r3.u64 = index;
      t_picked = int(index);
      t_pick_n = count;
      t_forced = true;
      t_force_take = 0;
      return;
    }
  }
  if (t_force_take > 0 && count == t_force_n && uint32_t(t_force_take) <= count)
  {
    const uint32_t index = uint32_t(t_force_take - 1);
    Store16(base, h + 4, uint16_t(index)); // XACT's pick writes the chosen index there too (its no-repeat memory)
    ctx.r3.u64 = index;
    t_picked = int(index);
    t_pick_n = count;
    t_forced = true;
    t_force_take = 0;
    return;
  }
  if (t_force_take > 0)
  {
    t_mismatch = count;
    t_force_take = 0;
  }
  __imp__sub_82C2BD80(ctx, base);
  t_picked = int(ctx.r3.s32);
  t_pick_n = count;
  if (!t_prime_cue.empty() && SpeechSetCue(t_prime_cue) && count > 1 && count <= 32 && t_picked >= 0 && uint32_t(t_picked) < count)
  {
    // Round 11k (item 3): XACT's own pick for a tracked speaker's cue without a take table: not a take his voice set
    // said within pdz_lab_stealth_voice_set_s, when another is left.
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const uint32_t all = count >= 32 ? 0xFFFFFFFFu : (1u << count) - 1;
    bool all_recent = false;
    const uint32_t fresh = NotRecent(t_prime_cue, all, Clock(base), all_recent);
    if (fresh && !((fresh >> uint32_t(t_picked)) & 1))
    {
      const int take = PickTake(fresh, count, Rand01());
      if (take > 0)
      {
        if (Logging())
        {
          REXLOG_INFO("pdz_voice_take: {}: XACT picked take {} of {}, which its voice set said within "
                      "pdz_lab_stealth_voice_set_s: REPLACED by take {}",
                      t_prime_cue, t_picked + 1, count, take);
        }
        Store16(base, h + 4, uint16_t(take - 1));
        ctx.r3.u64 = uint32_t(take - 1);
        t_picked = take - 1;
        t_forced = true;
      }
    }
  }
}

// Round 11d (item 3): a line played on a speaker by restorations.cpp (the blackout's lights-out conversation) is
// followed to its real start, whether the stealth code is on or not (after LOUD it is off): its own small list and
// lock, looked at only while a line is being watched.
namespace
{
  struct Watched
  {
    std::string cue; // without XACT_SOUNDBANK_
    std::chrono::steady_clock::time_point at;
  };
  std::mutex g_watch_lock;
  std::vector<Watched> g_watched;
  std::atomic<int> g_watch_n{0};

  void WatchPlay(const uint8_t *base, uint32_t sound, uint32_t before, uint32_t after)
  {
    const bool started = (before == 2 || before == 3) && after == 4;
    const bool failed = after == 6 || after == 7 || after == 8;
    if (!started && !failed)
    {
      return;
    }
    const std::string name = SoundName(base, sound);
    std::lock_guard<std::mutex> hold(g_watch_lock);
    for (auto it = g_watched.begin(); it != g_watched.end(); ++it)
    {
      if (it->cue != name)
      {
        continue;
      }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - it->at).count();
      if (started)
      {
        REXLOG_INFO("pdz_restore: lights-out line {} REALLY STARTED ({:.0f} ms after it was asked for)", name, ms);
      }
      else
      {
        REXLOG_INFO("pdz_restore: lights-out line {} FAILED to start (sound state {}: {}), {:.0f} ms after it was asked for",
                    name, after,
                    after == 6 ? "the sound system refused it or its cue could not be prepared: too far from the listener, a "
                                 "category limit, or no free cue"
                    : after == 7 ? "XACT could not play the cue"
                                 : "the cue name is not in the loaded banks",
                    ms);
      }
      g_watched.erase(it);
      g_watch_n = int(g_watched.size());
      return;
    }
  }
} // namespace

namespace lab_stealth
{
  void WatchLine(const std::string &cue)
  {
    std::lock_guard<std::mutex> hold(g_watch_lock);
    if (g_watched.size() >= 8)
    {
      g_watched.erase(g_watched.begin());
    }
    g_watched.push_back({cue, std::chrono::steady_clock::now()});
    g_watch_n = int(g_watched.size());
  }
  void WatchTick()
  {
    if (g_watch_n.load(std::memory_order_relaxed) <= 0)
    {
      return;
    }
    std::lock_guard<std::mutex> hold(g_watch_lock);
    const auto now = std::chrono::steady_clock::now();
    for (auto it = g_watched.begin(); it != g_watched.end();)
    {
      if (now - it->at > std::chrono::seconds(3))
      {
        REXLOG_INFO("pdz_restore: lights-out line {} DID NOT START within 3 s of being asked for: no sound was made for it "
                    "(the sound system refused it: too far from the listener, a category limit, no free cue; or the cue "
                    "is not in the loaded banks)",
                    it->cue);
        it = g_watched.erase(it);
      }
      else
      {
        ++it;
      }
    }
    g_watch_n = int(g_watched.size());
  }
} // namespace lab_stealth

// DarkAudioSound::Play: a guard line's real start (state 2 / 3 -> 4).
// Round 11l (item 1): THE ALARM SOUND AND THE SIX SIRENS, logged (pdz_lab_stealth_log), before and after LOUD. The
// level manager's alarm sound (manager +0x50, started by the alarm ON call sub_8214EBD0) and the six sirens (the
// Laboratory update, 8232789C-82327A6C, when its flag 194 toggles: STOCK_LV01_ALARM made at props 4, 5, 6, 7, 45, 46
// with sub_821FF428 and started with DarkAudioSound::Play, the handles kept at script +0x700..+0x714; flag 193 stops
// them) are the same cue. Every such sound is noted when it is made (or refused: no sound), its state (+340: 2
// prepared, 3 delayed, 4 playing) is followed every frame, a stop asked for it is logged with its caller, and every
// 10 s one line gives the state of each with its distance from Jo, the alarm byte and the six handles.
namespace
{
  struct AlarmSound
  {
    uint32_t sound = 0, state = 0xFFFFFFFFu;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    bool has_pos = false, gone = false;
    float at = 0.0f;
    std::string what;
  };
  std::mutex g_alarm_snd_lock;
  std::vector<AlarmSound> g_alarm_sounds;
  // The six sirens as the Laboratory update asked for them (the return address of its sub_821FF428 call gives the
  // slot): where each is, for the retry.
  float g_siren_pos[6][3] = {};
  std::atomic<uint32_t> g_siren_known{0};
  uint32_t g_siren_script = 0;
  uint32_t g_siren_mem = 0; // guest: a cue reference (1024 bytes) and a position
  float g_siren_try = -1e9f;
  int SirenSlot(uint32_t caller)
  {
    static const uint32_t kReturns[6] = {0x823278D8, 0x82327924, 0x82327970, 0x823279BC, 0x82327A08, 0x82327A54};
    for (int i = 0; i < 6; ++i)
    {
      if (kReturns[i] == caller)
      {
        return i;
      }
    }
    return -1;
  }
  std::atomic<int> g_alarm_snd_n{0};
  float g_alarm_snd_summary = -1e9f, g_alarm_snd_tick = -1e9f;

  bool AlarmCue(const uint8_t *base, uint32_t name_at)
  {
    return std::strncmp(reinterpret_cast<const char *>(base + name_at), "XACT_SOUNDBANK_STOCK_LV01_ALARM", 31) == 0;
  }
  std::string SoundStateText(uint32_t state)
  {
    return state == 4            ? std::string("4 PLAYING")
           : state == 3          ? std::string("3 delayed")
           : state == 2          ? std::string("2 prepared")
           : state == 0xFFFFFFFEu ? std::string("GONE (the sound object holds another sound now)")
           : state == 0xFFFFFFFFu ? std::string("just made")
                                  : fmt::format("{} (not playing)", state);
  }
  // The sound's XACT category (sound +312, low 6 bits) and Rare's level for it (category table 82E67C48, 32 bytes
  // each: +0x1C level 0..100, +0x1E enabled): a category at level 0 is silent whatever plays in it.
  std::string SoundCategoryText(const uint8_t *base, uint32_t sound)
  {
    const uint32_t cat = Load32(base, sound + 312) & 0x3F;
    const uint32_t entry = 0x82E67C48 + 32 * cat;
    return fmt::format("category {} at level {} ({})", cat, int(base[entry + 0x1C]), base[entry + 0x1E] ? "enabled" : "NOT enabled");
  }
  void AlarmSoundAdd(const uint8_t *base, uint32_t sound, const float *pos, const char *what, uint32_t caller)
  {
    const std::string where = pos ? fmt::format(" at ({:.0f} {:.0f} {:.0f})", pos[0], pos[1], pos[2]) : std::string();
    if (!Ptr(sound))
    {
      if (caller) // (not for our own retries: twice a second)
      {
        REXLOG_INFO("pdz_lab_stealth: ALARM SOUND NOT MADE: {}{}: the sound system gave no sound (caller {:08X})", what, where, caller);
      }
      return;
    }
    std::lock_guard<std::mutex> hold(g_alarm_snd_lock);
    for (const AlarmSound &a : g_alarm_sounds)
    {
      if (a.sound == sound && !a.gone)
      {
        return; // known
      }
    }
    if (g_alarm_sounds.size() >= 32)
    {
      g_alarm_sounds.erase(g_alarm_sounds.begin());
    }
    AlarmSound a;
    a.sound = sound;
    a.what = what + where;
    a.has_pos = pos != nullptr;
    if (pos)
    {
      std::memcpy(a.pos, pos, sizeof(a.pos));
    }
    a.at = Clock(base);
    g_alarm_sounds.push_back(a);
    g_alarm_snd_n = int(g_alarm_sounds.size());
    REXLOG_INFO("pdz_lab_stealth: ALARM SOUND made: {} (sound {:08X}, state {}, caller {:08X}), {}", a.what, sound,
                Load32(base, sound + 340), caller, SoundCategoryText(base, sound));
  }
  // Once per AI frame (the sight update).
  void AlarmSoundTick(const uint8_t *base)
  {
    const float now = Clock(base);
    if (now == g_alarm_snd_tick)
    {
      return;
    }
    g_alarm_snd_tick = now;
    std::lock_guard<std::mutex> hold(g_alarm_snd_lock);
    const uint32_t jo = Load32(base, kPlayerOne);
    const bool summary = now - g_alarm_snd_summary >= 10.0f || now < g_alarm_snd_summary;
    std::string line;
    for (AlarmSound &a : g_alarm_sounds)
    {
      if (a.gone)
      {
        continue;
      }
      const bool same = pdz::guestmem::Ok(base, a.sound, 344) && AlarmCue(base, a.sound + 52); // (Sun 22 Nov night (audit))
      const uint32_t state = same ? Load32(base, a.sound + 340) : 0xFFFFFFFEu;
      if (state != a.state)
      {
        REXLOG_INFO("pdz_lab_stealth: ALARM SOUND: {} (sound {:08X}): state {} -> {} ({:.1f} s after it was made)", a.what, a.sound,
                    SoundStateText(a.state), SoundStateText(state), now - a.at);
        a.state = state;
        a.gone = !same;
      }
      if (summary && !a.gone)
      {
        line += fmt::format(" [{}: {}{}]", a.what, SoundStateText(state),
                            a.has_pos && Ptr(jo) ? fmt::format(", {:.0f} cm from Jo",
                                                               std::sqrt((a.pos[0] - LoadF(base, jo + kPosition)) * (a.pos[0] - LoadF(base, jo + kPosition)) +
                                                                         (a.pos[1] - LoadF(base, jo + kPosition + 4)) * (a.pos[1] - LoadF(base, jo + kPosition + 4)) +
                                                                         (a.pos[2] - LoadF(base, jo + kPosition + 8)) * (a.pos[2] - LoadF(base, jo + kPosition + 8))))
                                                 : std::string());
      }
    }
    g_alarm_sounds.erase(std::remove_if(g_alarm_sounds.begin(), g_alarm_sounds.end(), [](const AlarmSound &a) { return a.gone; }),
                         g_alarm_sounds.end());
    g_alarm_snd_n = int(g_alarm_sounds.size());
    if (summary)
    {
      g_alarm_snd_summary = now;
      const uint32_t script = lab_restorations::CurrentScript();
      const uint32_t manager = AlarmManager(base);
      std::string handles;
      for (uint32_t i = 0; script && i < 6; ++i)
      {
        handles += fmt::format(" {:08X}", Load32(base, script + 0x700 + 4 * i));
      }
      const std::string cat = g_alarm_sounds.empty() ? std::string("-") : SoundCategoryText(base, g_alarm_sounds.front().sound);
      REXLOG_INFO("pdz_lab_stealth: ALARM SOUNDS now: alarm byte {}, the manager's alarm sound {:08X}, the six siren handles:{}, "
                  "{} |{}",
                  AlarmOn(base) ? "ON" : "off", Ptr(manager) ? Load32(base, manager + 0x50) : 0, handles, cat,
                  line.empty() ? std::string(" none is being followed") : line);
    }
  }
} // namespace

// Round 11l (item 1): THE FIX. The first 11l run (the alarm raised with Jo at the level's start): of the six sirens
// Rare's update asked for, the sound system made one (973 cm from Jo) and gave no sound for four (thousands of cm
// away); the update only asks in the frame the alarm goes on and keeps a handle of 0 for good. In hand test 023 the
// alarm was raised by a runner far from every siren, so none was made and the whole alarm was silent. While the alarm
// is on, every siren slot Rare asked for whose handle (script +0x700 + 4 x slot) is 0, or whose sound is no longer
// that cue or no longer prepared / playing, is made again with Rare's own calls (the cue reference of Rare's own name
// string 8209041C, sub_821FF428 at the siren's position, DarkAudioSound::Play with delay 0) twice a second, and the
// handle is stored where Rare's stop (flag 193) finds it.
namespace
{
  void SirenRetryTick(PPCContext &ctx, uint8_t *base)
  {
    const float now = Clock(base);
    if (now >= g_siren_try && now - g_siren_try < 0.5f)
    {
      return;
    }
    g_siren_try = now;
    const uint32_t script = lab_restorations::CurrentScript();
    constexpr uint32_t kSirenName = 0x8209041C;
    if (!script || script != g_siren_script || !AlarmOn(base) || !AlarmCue(base, kSirenName))
    {
      return;
    }
    if (!g_siren_mem)
    {
      g_siren_mem = REX_KERNEL_MEMORY()->SystemHeapAlloc(1024 + 16);
      if (!g_siren_mem)
      {
        return;
      }
    }
    const uint32_t known = g_siren_known.load(std::memory_order_relaxed);
    for (uint32_t slot = 0; slot < 6; ++slot)
    {
      if (!(known & (1u << slot)))
      {
        continue;
      }
      const uint32_t handle = Load32(base, script + 0x700 + 4 * slot);
      const uint32_t state = Ptr(handle) && pdz::guestmem::Ok(base, handle, 344) && AlarmCue(base, handle + 52) // (Sun 22 Nov night (audit))
                                 ? Load32(base, handle + 340)
                                 : 0;
      if (Ptr(handle) && state >= 1 && state <= 4)
      {
        continue; // made, and prepared or playing
      }
      float p[3];
      {
        std::lock_guard<std::mutex> hold(g_alarm_snd_lock);
        std::memcpy(p, g_siren_pos[slot], sizeof(p));
      }
      for (uint32_t i = 0; i < 3; ++i)
      {
        uint32_t bits;
        std::memcpy(&bits, &p[i], 4);
        Store32(base, g_siren_mem + 1024 + 4 * i, bits);
      }
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = g_siren_mem;
        frame.ctx.r4.u64 = kSirenName;
        sub_82202D20(frame.ctx, base);
      }
      uint32_t sound = 0;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.lr = 0; // (the hook above reads it: 0 = the retry)
        frame.ctx.r3.u64 = g_siren_mem;
        frame.ctx.r4.u64 = g_siren_mem + 1024;
        frame.ctx.r5.u64 = 0;
        frame.ctx.r6.u64 = 0;
        sub_821FF428(frame.ctx, base);
        sound = frame.ctx.r3.u32;
      }
      if (!Ptr(sound))
      {
        continue; // still out of hearing: tried again in half a second
      }
      Store32(base, script + 0x700 + 4 * slot, sound);
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = sound;
        frame.ctx.f1.f64 = 0.0;
        sub_822061C8(frame.ctx, base);
      }
      const uint32_t jo = Load32(base, kPlayerOne);
      float jp[3] = {0.0f, 0.0f, 0.0f};
      if (Ptr(jo))
      {
        PosOf(base, jo, jp);
      }
      REXLOG_INFO("pdz_lab_stealth: SIREN STARTED BY THE RETRY: siren {} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from Jo: sound "
                  "{:08X} (its handle was {}); Rare's script had no sound for it",
                  slot, p[0], p[1], p[2], Dist3(p, jp), sound,
                  Ptr(handle) ? fmt::format("{:08X}, a sound that had ended (state {})", handle, state) : std::string("0"));
    }
  }
} // namespace

namespace lab_stealth
{
  void SoundStopped(const uint8_t *base, uint32_t sound, uint32_t caller)
  {
    if (g_alarm_snd_n.load(std::memory_order_relaxed) <= 0 || !sound)
    {
      return;
    }
    std::lock_guard<std::mutex> hold(g_alarm_snd_lock);
    for (const AlarmSound &a : g_alarm_sounds)
    {
      if (a.sound == sound && !a.gone)
      {
        REXLOG_INFO("pdz_lab_stealth: ALARM SOUND STOP asked for: {} (sound {:08X}, state {}), caller {:08X}", a.what, sound,
                    SoundStateText(Load32(base, sound + 340)), caller);
      }
    }
  }
} // namespace lab_stealth

// A positioned sound made from a cue reference (r3 the reference: the cue's name at +0; r4 -> the position or 0):
// the sirens are made here.
REX_EXTERN(__imp__sub_821FF428);
namespace pdz::guardsteps // guard_footsteps_Mon16Nov.cpp (log only: pdz_snd_step_log)
{
  void OnStepSound(const uint8_t *base, uint32_t ref, uint32_t sound);
}
namespace pdz::prop_contacts // contact_log_Wed18Nov.cpp (log only)
{
  void OnContactSound(const uint8_t *base, uint32_t ref, uint32_t record, uint32_t sound);
}
namespace pdz::water // restored_sounds_r2_Tue17Nov.cpp (Sun 22 Nov: the bullet-into-water sound's trim)
{
  void OnSoundMade(uint8_t *base, uint32_t caller, uint32_t sound);
}
REX_HOOK_RAW(sub_821FF428)
{
  const uint32_t ref = ctx.r3.u32, pos = ctx.r4.u32, caller = uint32_t(ctx.lr);
  __imp__sub_821FF428(ctx, base);
  pdz::guardsteps::OnStepSound(base, ref, ctx.r3.u32); // (Wed 18 Nov night: a footstep's sound, log only)
  pdz::water::OnSoundMade(base, caller, ctx.r3.u32);    // (Sun 22 Nov: before Rare plays it)
  if (caller == 0x82512078u)
  {
    pdz::prop_contacts::OnContactSound(base, ref, pos, ctx.r3.u32); // (Wed 18 Nov: a prop's knock sound, log only)
  }
  if (Ptr(ref) && base[ref + 21] == 'L' && base[ref + 22] == 'V' && AlarmCue(base, ref) && lab_restorations::CurrentScript() != 0)
  {
    float p[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; Ptr(pos) && i < 3; ++i)
    {
      p[i] = LoadF(base, pos + 4 * uint32_t(i));
    }
    if (const int slot = SirenSlot(caller); slot >= 0 && Ptr(pos))
    {
      std::lock_guard<std::mutex> hold(g_alarm_snd_lock);
      const uint32_t script = lab_restorations::CurrentScript();
      if (script != g_siren_script)
      {
        g_siren_script = script;
        g_siren_known = 0;
      }
      std::memcpy(g_siren_pos[slot], p, sizeof(p));
      g_siren_known |= 1u << slot;
    }
    if (REXCVAR_GET(pdz_lab_stealth_log) && (caller != 0 || Ptr(ctx.r3.u32)))
    {
      AlarmSoundAdd(base, ctx.r3.u32, Ptr(pos) ? p : nullptr, caller ? "a siren" : "a siren (made by the retry)", caller);
    }
  }
}

REX_HOOK_RAW(sub_822061C8)
{
  const uint32_t sound = ctx.r3.u32;
  const bool watched = g_watch_n.load(std::memory_order_relaxed) > 0 && Ptr(sound);
  if (!g_lab || !REXCVAR_GET(pdz_lab_guard_stealth) || !Ptr(sound))
  {
    if (watched)
    {
      const uint32_t state = Load32(base, sound + 340);
      __imp__sub_822061C8(ctx, base);
      WatchPlay(base, sound, state, Load32(base, sound + 340));
      return;
    }
    __imp__sub_822061C8(ctx, base);
    return;
  }
  const uint32_t before = Load32(base, sound + 340);
  __imp__sub_822061C8(ctx, base);
  const uint32_t after = Load32(base, sound + 340);
  if (watched)
  {
    WatchPlay(base, sound, before, after);
  }
  if (!((before == 2 || before == 3) && after == 4))
  {
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  if (std::strncmp(reinterpret_cast<const char *>(base + sound + 52), "XACT_SOUNDBANK_LV05_SPEECH_", 27) == 0)
  {
    // Round 5 (item 7): one of Rare's scripted Laboratory lines started (Jo, Chandra, the scripted scientists, the
    // tannoys): the guards' idle lines and our tannoys wait while it plays.
    g_story_sound = sound;
    g_story_at = Clock(base);
    const char *text = reinterpret_cast<const char *>(base + sound + 52);
    g_story_name.assign(text, strnlen(text, 250));
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: story speech {} started: guards who are not fighting hold their lines while it plays",
                  g_story_name.substr(15));
    }
  }
  if (RadioRow())
  {
    // Mansion helper: the radio line really started (DarkAudioSound::Play, state 2 / 3 -> 4): the announced call heard.
    const std::string name = SoundName(base, sound);
    if (name.find("_LVL4_PLAYER_SPOTTED_RADIO_IN") != std::string::npos ||
        name.find("_LVL4_TRY_RADIO_NETWORK_DOWN") != std::string::npos)
    {
      const uint32_t owner = Load32(base, sound + 12);
      const Guard *rg = Ptr(owner) ? Find(owner) : nullptr;
      if (StealthLog())
      {
        REXLOG_INFO("pdz_stealth_radio: {} ANNOUNCED CALL line STARTED: {} ({}){}", Ptr(owner) ? Id(base, owner) : std::string("?"),
                    name, rg ? "a guard under the layer" : "not a guard under the layer",
                    rg && rg->radio_at > -1e8f ? fmt::format(", {:.0f} ms after Rare asked for it", 1000.0f * (Clock(base) - rg->radio_at))
                                               : std::string());
      }
    }
  }
  const auto it = g_primed.find(sound);
  if (it == g_primed.end())
  {
    return;
  }
  const Primed p = it->second;
  g_primed.erase(it);
  Guard *g = Find(p.owner);
  if (g)
  {
    // Round 11k (item 3): the voice set is speaking from now on (another speaker of the set waits for its end).
    float set_len = 3.0f;
    const size_t cut = p.cue.find('_');
    if (g->voice >= 17 && g->voice <= 19 && cut != std::string::npos)
    {
      if (const TakeSet *ts = TakesByCue(g->voice, p.cue.substr(cut + 1)); ts && p.take >= 1 && uint32_t(p.take) <= ts->n && p.take <= 20)
      {
        set_len = ts->secs[p.take - 1];
      }
    }
    else if (g->scientist && g->sci_len > 0.5f)
    {
      set_len = g->sci_len;
    }
    NoteSetPlay(sound, p.owner, Clock(base), set_len);
  }
  if (p.ordered && !std::any_of(g_said_lines.begin(), g_said_lines.end(),
                                [&](const Said &sd) { return sd.c == p.owner && sd.stage == 2 && sd.sound == sound; }))
  {
    // Round 11b: a line of ours played on the speaker (a scientist's warning, a lamp comment, the lights-out talk).
    if (Logging())
    {
      REXLOG_INFO("pdz_voice_play: {} {} take {} of {} (a staff line played on the speaker: outside Rare's speech rules){}",
                  Ptr(p.owner) ? Id(base, p.owner) : std::string("?"), p.cue, p.take, p.n,
                  g ? fmt::format(" in state {}", StName(g->st)) : std::string());
    }
    return;
  }
  // Round 10b: the line's trace ends here: it really started.
  bool traced = false;
  for (auto s = g_said_lines.begin(); s != g_said_lines.end(); ++s)
  {
    if (s->c == p.owner && s->stage == 2 && s->sound == sound)
    {
      g_said_lines.erase(s);
      traced = true;
      break;
    }
  }
  if (!g || g->scientist || g->voice < 17 || g->voice > 19)
  {
    if (g && traced)
    {
      // A scientist's line our say hook let through (outside the guards' line count, but in the one-voice rule).
      for (auto r = g_lines.rbegin(); r != g_lines.rend(); ++r)
      {
        if (r->c == p.owner && !r->played)
        {
          r->played = true;
          break;
        }
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_voice_play: {} {} take {} of {} ({}; staff line through Rare's speech, outside the guard line count) "
                    "in state {}",
                    Id(base, p.owner), p.cue, p.take, p.n, p.forced ? "steered" : "XACT's pick", StName(g->st));
      }
    }
    return; // guard lines only (the scientists' scripted talk is Rare's)
  }
  const float now = Clock(base);
  float len = 2.0f;
  const size_t us = p.cue.find('_');
  if (const TakeSet *set = us == std::string::npos ? nullptr : TakesByCue(g->voice, p.cue.substr(us + 1)))
  {
    if (p.take >= 1 && uint32_t(p.take) <= set->n && p.n == set->n)
    {
      len = set->secs[p.take - 1];
    }
  }
  g_plays.erase(std::remove_if(g_plays.begin(), g_plays.end(),
                               [now](const Play &l) { return l.end < now - 5.0f || l.start > now + 1.0f; }),
                g_plays.end());
  const int playing = int(std::count_if(g_plays.begin(), g_plays.end(), [now](const Play &l) { return l.end > now; }));
  const bool fight = Fighting();
  ++g_play_count;
  if (playing > 0 && !fight)
  {
    ++g_play_overlaps;
  }
  Play pl;
  pl.start = now;
  pl.end = now + len;
  pl.owner = p.owner;
  g_plays.push_back(pl);
  g_last_play_end = std::max(g_last_play_end, pl.end);
  g_voice_last = now;
  if (p.owner == g_mutter_wait)
  {
    g_mutter_wait = 0; // the mutter really started
    g_mutter_skip.erase(p.owner);
  }
  if (g->prio && (p.cue.find("SEE_BODY") != std::string::npos || (g->prio == 2 && p.cue.find("WARN_TEAM") != std::string::npos)))
  {
    // Round 6 (item 2): the body line really started.
    REXLOG_INFO("pdz_lab_stealth: {} BODY LINE PLAYED: {} take {} of {} ({}), {:.1f} s after it was due ({} other guard "
                "line(s) playing)",
                Id(base, p.owner), p.cue, p.take, p.n, g->prio == 1 ? "he noticed a body" : "his squad call", now - g->prio_from,
                playing);
    g->prio = 0;
  }
  // The budget's line for it starts now (its say-event was a frame or more earlier).
  for (auto r = g_lines.rbegin(); r != g_lines.rend(); ++r)
  {
    if (r->c == p.owner && (!p.ev || r->ev == p.ev) && now - r->start < 5.0f)
    {
      r->start = now;
      r->end = now + len + 0.15f;
      r->played = true;
      g_last_line_end = std::max(g_last_line_end, r->end);
      break;
    }
  }
  if (Logging())
  {
    REXLOG_INFO("pdz_voice_play: {} {} take {} of {} ({}, {:.2f} s) in state {}{} | {} other guard line(s) playing{} | "
                "plays {}, overlaps outside a fight {}",
                Id(base, p.owner), p.cue, p.take, p.n, p.forced ? "steered" : "XACT's pick", len, StName(g->st),
                fight ? " (fight)" : "", playing, playing > (fight ? 1 : 0) ? " (!)" : "", g_play_count, g_play_overlaps);
  }
}

// pdz_test_stealth_no_alarm_run (testing): the alarm-run builder answers "no task" in the Laboratory. Its callers (the
// COMBAT update 821CC358, the DEFAULT role dispatch 821D09FC, the Laboratory scientist task 8231A3DC / 8231A6D4,
// 822BEBFC) already handle 0, which Rare returns himself when his gate 821DF340 refuses.
REX_HOOK_RAW(sub_821DF4A0)
{
  if (g_loud && g_lab_run && !g_alarm_run_ours && Ptr(ctx.r3.u32) && REXCVAR_GET(pdz_lab_guard_stealth) &&
      REXCVAR_GET(pdz_lab_stealth_loud_alarm_announce) && REXCVAR_GET(pdz_lab_stealth_alarm_announce) &&
      !REXCVAR_GET(pdz_test_stealth_no_alarm_run) && lab_restorations::CurrentScript() != 0)
  {
    // Round 11k (item 6): after LOUD the alarm run keeps its warning.
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (LoudAlarmAsk(ctx, base, ctx.r3.u32))
    {
      return;
    }
  }
  if (g_lab && REXCVAR_GET(pdz_lab_guard_stealth) &&
      (lab_restorations::CurrentScript() != 0 || (ZoneLevel() && g_level->alarm_announce && InLayer(base, ctx.r3.u32))))
  {
    // (Stealth rollout: on a zone level with alarm_announce, for a guard inside the zone: the announcement below.)
    // Round 4: a guard leaving a target-less combat because the alarm ended by itself runs for no alarm while his
    // COMBAT script ends (its update builds the alarm run before it ends, 821CC358).
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const Guard *g = Find(ctx.r3.u32);
    const float now = Clock(base);
    // Round 4 (test r4_i: the hub scientist s2s2, who never saw Jo, ran for the alarm the moment it ended and raised it
    // again 5 s later; log 025 shows the same after Jo's switch-off): after the first alarm has ended, an alarm run is
    // only for someone who has seen Jo since (Rare's valid-target bit, or his own sight test) or a body finder's
    // escalation; the rest are runs left over from the alarm itself. Rare's callers handle "no task" (his own gate's).
    const uint32_t who = ctx.r3.u32;
    // Round 5 (item 4, log 027 07:43:39: s1m3 ran for the alarm with meter 0 and no target, from gunfire 45 m away):
    // Rare's role dispatch sends an ALARM_USER for the alarm at ANY stimulus. Only someone who has seen Jo (a real
    // sighting, a fight, hurt by her) runs; a body finder's run is ours (AlarmRun, after his search).
    if (REXCVAR_GET(pdz_lab_stealth_noise_rules) && !RarePlus() && !g_alarm_run_ours && g && !g->dead && g->st != St::Dead)
    {
      Guard *gm = Find(who);
      const bool seen = g->seen_jo || g->spotted || g->fought || g->st == St::Combat || g->st == St::Lost ||
                        (g->confirm_kinds & (kCfSighting | kCfCombat | kCfHurt)) != 0 ||
                        (Load32(base, who + kSight) & kValidTarget) != 0;
      // The gunfire rule (item 4 as changed): a trooper who heard an unsilenced shot or an explosion in the last 5 s
      // runs as Rare has it. (Scientists still need a sighting.)
      const bool loud = REXCVAR_GET(pdz_lab_stealth_gunfire_rules) && !g->scientist && now >= g->loud_at && now - g->loud_at < 5.0f;
      if (loud && !seen && Logging() && gm && (now - gm->alarm_refused_log >= 10.0f || now < gm->alarm_refused_log))
      {
        gm->alarm_refused_log = now;
        REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN allowed: he heard an unsilenced shot or an explosion {:.1f} s ago (Rare's "
                    "alarm user reaction)",
                    Id(base, who), now - g->loud_at);
      }
      if (!seen && !loud)
      {
        if (Logging() && gm && (now - gm->alarm_refused_log >= 10.0f || now < gm->alarm_refused_log))
        {
          gm->alarm_refused_log = now;
          REXLOG_INFO("pdz_lab_stealth: {} (voice {}, state {}) alarm run refused: he has not seen Jo (Rare sends an alarm "
                      "user for the alarm at any stimulus; +6260 {:08X})",
                      Id(base, who), int32_t(Load32(base, who + kVoiceIndex)), StName(g->st), Load32(base, who + kStimuli));
        }
        ctx.r3.u64 = 0;
        return;
      }
    }
    const bool tiers = false; // round 11: no alarm tiers
    // (Also in the frame the alarm byte went off, before the alarm tick has booked the end: Jo's switch.)
    const bool just_ended = g_alarm_on && !AlarmOn(base);
    const int ended = g_alarms_ended + (just_ended ? 1 : 0);
    if (REXCVAR_GET(pdz_lab_stealth_alarm_end) && !RarePlus() && ended > 0 && (!tiers || ended == 1) && !AlarmOn(base) &&
        !g_alarm_run_ours && Ptr(who))
    {
      const float since = just_ended ? now : g_alarm_ended_at;
      // Round 6 (item 4, log 062 17:08:47: the hub scientist s2s1 started an alarm run on a glimpse, meter 1): "seen
      // since" is a full sighting (the meter filled: spotted_at), not Rare's visibility test alone (last_visible).
      const bool seen = (Load32(base, who + kSight) & kValidTarget) != 0 || (g && g->spotted_at >= since);
      if (!seen)
      {
        static std::unordered_map<uint32_t, float> stale_said;
        const auto it = stale_said.find(who);
        if (Logging() && (it == stale_said.end() || now - it->second >= 10.0f || now < it->second))
        {
          stale_said[who] = now;
          REXLOG_INFO("pdz_lab_stealth: {} (voice {}) alarm run refused: left over from alarm {} (he has not seen Jo since it "
                      "ended {:.0f} s ago)",
                      Id(base, who), int32_t(Load32(base, who + kVoiceIndex)), g_alarm_no, now - since);
        }
        ctx.r3.u64 = 0;
        return;
      }
    }
    if (REXCVAR_GET(pdz_lab_stealth_alarm_announce) && !g_alarm_run_ours)
    {
      // Round 11f (item 4): Rare's own alarm run (his COMBAT update, the role dispatch, the scientists' task) is
      // announced first as well: no task now; it starts after his shout (AlarmRunTick), and Rare's callers ask again.
      Guard *a = Find(who);
      if (a && a->run_committed)
      {
        ctx.r3.u64 = 0; // round 11h: he is committed: his alarm task is ours to give (RunKeepTick)
        return;
      }
      // (Round 11g: while his run is pending, shout or hesitation, Rare's request is never let through: in log 013
      // 20:52:14.3 Rare's own request started the run 18 ms after the shout began.)
      if (a && !a->dead && a->st != St::Dead && LoadF(base, who + kHealth) > 0.0f &&
          (a->run_pending || (!(now >= a->announced_at && now - a->announced_at < 10.0f) && now >= a->run_go_until)))
      {
        if (!a->run_pending)
        {
          // Round 11i (item 2; log 017 21:41:29 and 21:42:52: two "Sound the alarm!" shouts in the generator room,
          // which has no alarm in reach, and nothing followed): the check comes first. Rare's own builder is run now:
          // only when it makes the task (an alarm in reach, a route) does he shout, hesitate and run, and that very
          // task is the one handed to him after his hesitation. No task: no shout: he fights as Rare made him.
          if (now < a->run_block_until)
          {
            ctx.r3.u64 = 0;
            return;
          }
          __imp__sub_821DF4A0(ctx, base);
          const uint32_t ready = ctx.r3.u32;
          if (!Ptr(ready))
          {
            if (now - a->run_check_log >= 10.0f || now < a->run_check_log)
            {
              a->run_check_log = now;
              REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN CHECK (Rare asked for his alarm run, state {}): Rare's builder makes "
                          "no alarm task (no alarm in reach, or no route to one): NO SHOUT, no run: he fights as Rare made him",
                          Id(base, who), StName(a->st));
            }
            ctx.r3.u64 = 0;
            return;
          }
          a->run_ready = ready;
          REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN CHECK (Rare asked for his alarm run, state {}): Rare's builder made the "
                      "alarm task {:08X}: an alarm is in reach: he shouts, hesitates, then gets that task",
                      Id(base, who), StName(a->st), ready);
          StartAnnounce(base, who, *a, now, "Rare's own alarm run");
        }
        ctx.r3.u64 = 0;
        return;
      }
    }
    if (g && g->st == St::Lost && g->calm_after_combat && now >= g->since && now - g->since < 5.0f)
    {
      static std::unordered_map<uint32_t, float> said;
      const auto it = said.find(ctx.r3.u32);
      if (Logging() && (it == said.end() || now - it->second >= 5.0f || now < it->second))
      {
        said[ctx.r3.u32] = now;
        REXLOG_INFO("pdz_lab_stealth: {} alarm run refused: he is leaving his combat after the alarm ended by itself",
                    Id(base, ctx.r3.u32));
      }
      ctx.r3.u64 = 0;
      return;
    }
  }
  if (!REXCVAR_GET(pdz_test_stealth_no_alarm_run) || lab_restorations::CurrentScript() == 0)
  {
    __imp__sub_821DF4A0(ctx, base);
    return;
  }
  const uint32_t c = ctx.r3.u32;
  static std::unordered_map<uint32_t, float> logged_at; // per character: at most one line per 5 s
  const float now = Clock(base);
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  auto it = logged_at.find(c);
  if (Logging() && (it == logged_at.end() || now - it->second >= 5.0f || now < it->second))
  {
    logged_at[c] = now;
    REXLOG_INFO("pdz_lab_stealth: alarm run blocked (test): {} (voice {})", Ptr(c) ? Id(base, c) : std::string("?"),
                Ptr(c) ? int32_t(Load32(base, c + kVoiceIndex)) : -1);
  }
  ctx.r3.u64 = 0;
}

// Confirmed contact from Jo's hits (header point 7): hurt by her, or a squad mate killed by her.
namespace lab_stealth
{
  // Follow-up Sun 15 Nov (item 16): the hit being handled is an explosion's (no tracked character and not Jo as its
  // attacker, and an explosion on record reaches the victim): HitAfter then only marks him hurt (meter full).
  thread_local bool t_hit_blast = false;
  float HitBefore(const uint8_t *base, uint32_t victim, uint32_t attacker)
  {
    t_hit_blast = false;
    if (!g_lab || !REXCVAR_GET(pdz_lab_guard_stealth) || !Ptr(victim) || victim == attacker)
    {
      return -1.0f;
    }
    if (!Ptr(attacker) || attacker != Load32(base, kPlayerOne))
    {
      if (!REXCVAR_GET(pdz_stealth_hurt_full))
      {
        return -1.0f;
      }
      std::lock_guard<std::recursive_mutex> hold(g_lock);
      float pos[3];
      PosOf(base, victim, pos);
      if (g_blasts.empty() || !Find(victim) || (Ptr(attacker) && Find(attacker)) || !BlastNear(pos, Clock(base)))
      {
        return -1.0f;
      }
      t_hit_blast = true;
    }
    const float health = LoadF(base, victim + kHealth);
    return std::isfinite(health) ? std::max(0.0f, health) : -1.0f;
  }

  void HitAfter(const uint8_t *base, uint32_t victim, uint32_t attacker, float before)
  {
    if (before < 0.0f || !g_lab || !Ptr(victim))
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    const float after = LoadF(base, victim + kHealth);
    Guard *g = Find(victim);
    if (g && after < before)
    {
      g->hurt_at = now;
      if (!t_hit_blast)
      {
        R3cBJoHit(base, victim, before, after, now); // round 3c (B, fix 3): was there a shot of hers before it?
      }
      Confirm(base, victim, *g, kCfHurt,
              fmt::format("hurt by {} (health {:.2f} -> {:.2f})", t_hit_blast ? "an explosion" : "Jo", before, after));
      HurtFull(base, victim, *g, now, // (follow-up Sun 15 Nov, item 16)
               fmt::format("{}, health {:.0f} -> {:.0f}", t_hit_blast ? "an explosion" : "her hit", before, after));
    }
    if (t_hit_blast)
    {
      return; // (an explosion with no character as its attacker: not counted as Jo's kill here, as before)
    }
    if (!(before > 0.0f && after <= 0.0f) ||
        std::any_of(g_kills.begin(), g_kills.end(), [victim](const Kill &k) { return k.victim == victim; }))
    {
      return; // not a kill, or one already counted (Rare's Kill can run the hit handler itself)
    }
    Kill k;
    k.victim = victim;
    k.at = now;
    for (int i = 0; i < 3; ++i)
    {
      k.pos[i] = LoadF(base, victim + kPosition + 4 * i);
    }
    k.who = Id(base, victim);
    R4AKillLog(base, k, now); // (stealth4, fix 1: always on, one line per kill)
    const uint32_t squad = g ? g->squad : 0;
    if (g)
    {
      g->dead = true;
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: Jo killed {} at ({:.0f} {:.0f} {:.0f}){}", k.who, k.pos[0], k.pos[1], k.pos[2],
                  squad ? fmt::format(", squad {:08X}", squad) : std::string());
    }
    if (g_kills.size() < 64)
    {
      g_kills.push_back(k);
    }
    // Round 3 (hand test 004 07:02:13: one kill confirmed the whole squad, scientists included, though none saw it): a
    // death confirms only a guard who SAW it: one whose own sight test saw Jo within the last 1.5 s (shown to Rare or
    // still hidden by the meter: he saw her at the moment she fired) within 30 m, or who is fighting her (Rare's
    // valid-target bit). Everyone else may still find the body later (Rare's SEE_BODY: point 7) or come in on Rare's
    // own reactions to the noise, as glimpse-only searchers.
    const uint32_t jo = Load32(base, kPlayerOne);
    for (auto &[other, o] : g_guards)
    {
      if (other == victim || o.dead || o.st == St::Dead || !Ptr(jo))
      {
        continue;
      }
      const float d_jo = Dist(base, other, jo);
      const bool saw_jo = now >= o.last_visible && now - o.last_visible < 1.5f && d_jo <= 3000.0f;
      const bool fighting = (Load32(base, other + kSight) & kValidTarget) != 0;
      if (saw_jo || fighting)
      {
        // The flag only: he is told nothing new of where she is.
        Confirm(base, other, o, kCfMateKilled,
                fmt::format("saw Jo kill {}{} ({}, {:.0f} cm from her)", k.who, (squad && o.squad == squad) ? " (squad mate)" : "",
                            fighting ? "he is fighting her" : "his sight test saw her", d_jo));
      }
      else if (squad && o.squad == squad && Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} not confirmed by the death of squad mate {}: he did not see it (no sight of Jo "
                    "{}, {:.0f} cm from her); the body counts if he finds it",
                    Id(base, other), k.who,
                    o.last_visible < -1e8f ? std::string("in this run") : fmt::format("for {:.1f} s", now - o.last_visible),
                    d_jo);
      }
    }
  }
  // Round 5, Mon 16 Nov (fix 6; lab_stealth_r5_A_Mon16Nov.inc): around Rare's melee routine (the cut_scenarios.cpp hook).
  void MeleeBefore(const uint8_t *base, uint32_t attacker)
  {
    R5AMeleeBefore(base, attacker);
  }
  void MeleeAfter(const uint8_t *base, uint32_t attacker)
  {
    R5AMeleeAfter(base, attacker);
  }
} // namespace lab_stealth

#if !PDZ_COUNTEROP
// The character hit handler (r3 victim, r4 hit, r5 attacker). counter_op.cpp hooks it when PDZ_COUNTEROP builds it
// and calls the two functions above itself.
REX_EXTERN(__imp__sub_82549040);
REX_HOOK_RAW(sub_82549040)
{
  const uint32_t victim = ctx.r3.u32, attacker = ctx.r5.u32;
  const float before = lab_stealth::HitBefore(base, victim, attacker);
  __imp__sub_82549040(ctx, base);
  lab_stealth::HitAfter(base, victim, attacker, before);
}
#endif

// --- Dark patches (lab_dark_patches.cpp) ---

// The player's concealment (vtable 820C7DE0 +0x68; Rare: 0.5 crouched and not seen before, else 1): Rare's sight test
// shows Jo only within range x sqrt(concealment). With dark patches the range also shrinks with the light at Jo.
REX_EXTERN(__imp__sub_825A93C0);
REX_HOOK_RAW(sub_825A93C0)
{
  const uint32_t target = ctx.r3.u32;
  const float r2 = t_sight_range2;
  __imp__sub_825A93C0(ctx, base);
  if (r2 > 0.0f && lab_dark::On() && !g_loud && target == Load32(base, kPlayerOne) && // (round 11: not once loud)
      R8DarkRangeAllowed()) // (Tue 17 Nov night, fix2: not for a guard the layer holds as having seen her)
  {
    const float scale = lab_dark::RangeScale(std::sqrt(r2));
    ctx.f1.f64 *= double(scale) * double(scale);
    R8DarkRangeUsed(scale);
  }
}

namespace
{
  // A lamp going out near calm guards (Part 2 item 3).
  struct Incident
  {
    float pos[3] = {};
    float at = -1e9f;
    float next = 0.0f;
    uint32_t investigator = 0;
    std::vector<uint32_t> others;
    size_t said = 0;
  };
  Incident g_incident;
  std::unordered_map<std::string, uint32_t> g_event_index;

  uint32_t EventIndex(const uint8_t *base, const std::string &name)
  {
    const auto it = g_event_index.find(name);
    if (it != g_event_index.end())
    {
      return it->second;
    }
    uint32_t found = 0;
    for (uint32_t ev = 1; ev < 170; ++ev)
    {
      if (EventName(base, ev) == name)
      {
        found = ev;
        break;
      }
    }
    g_event_index[name] = found;
    return found;
  }

  bool Calm(const Guard &g)
  {
    return LayerGuard(g) && !g.dead && !g.run_committed && (g.st == St::Unaware || g.st == St::Wary) && // (the Lab: 17-19)
           !(RarePlus() && g.rare_busy); // (rareplus: only a guard Rare has calm: never in place of a Rare reaction)
  }

  // The lamp-out comments. "Never mind" is Rare's HEAR_NOISE_NOT_INVESTIGATE_TEAM, recorded for every trooper but not
  // in the speech enum (docs SP_INVENTORY "Unused speech"); it is asked for as HEAR_UNKNOWN_TEAM. Rare's speech picks
  // another cue for that event when the guard is unaware (test dk_incident2: BEEN_SHOT_MISS_UNAWARE_SOLO), so the
  // next cue name of that voice resolved in sub_82202D20 (cut_scenarios.cpp) within 4 s is swapped for the comment's
  // own: HEAR_NOISE_NOT_INVESTIGATE_TEAM, then HEAR_UNKNOWN_TEAM.
  // Round 5 (log 025 07:07:04-07: the swap armed for s3g5's comment, which Rare's speech never queued, replaced the
  // next TROOPER2 cue looked up within 4 s: the dying s3g2's TROOPER2_HUP). The say-event only stores the event; the
  // cue name is built frames later by the speaker's speech task (sub_8262AF60 -> sub_82204718 -> sub_82202D20). So the
  // swap now belongs to one speaker: it replaces only a name looked up inside HIS speech task's build, and only the
  // two names the event we asked for can become (HEAR_UNKNOWN_TEAM, or BEEN_SHOT_MISS_UNAWARE_SOLO: Rare adds 1 to
  // an event below 87 when the speaker's squad has more than one member).
  std::atomic<bool> g_swap_armed{false};
  std::string g_swap_from;
  std::string g_swap_only; // round 11c: the one cue (without the voice) that may be swapped; empty: round 5's pair
  std::string g_swap_only2; // round 11k: ... or this one
  uint32_t g_swap_to = 0;
  uint32_t g_swap_c = 0;
  float g_swap_at = -1e9f;
  std::unordered_map<std::string, uint32_t> g_swap_guest;

  void ArmSwap(uint8_t *base, uint32_t c, uint32_t voice, const char *cue, float now)
  {
    static const char *const kVoice[5] = {"SCI1", "SCI2", "TROOPER1", "TROOPER2", "TROOPER3"};
    // (Nightclub helper: on a zone level also the row's other guard voices, GANGSTA1-3: their own set's cue. The
    // Laboratory: 15-19 only, as before.)
    const char *set = voice >= 15 && voice <= 19 ? kVoice[voice - 15]
                      : ZoneLevel() && GuardVoice(voice) ? VoiceTableName(voice)
                                                         : nullptr;
    if (!set)
    {
      return;
    }
    g_swap_c = c;
    const std::string bank = std::string("XACT_SOUNDBANK_") + set + "_";
    const std::string to = bank + cue;
    uint32_t &guest = g_swap_guest[to];
    if (!guest)
    {
      guest = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(to.size() + 1));
      if (guest)
      {
        std::memcpy(base + guest, to.c_str(), to.size() + 1);
      }
    }
    if (!guest)
    {
      return;
    }
    g_swap_from = bank; // any cue of this voice
    g_swap_only.clear();
    g_swap_only2.clear();
    g_swap_to = guest;
    g_swap_at = now;
    g_swap_armed = true;
  }
  // Round 11c: the speaker's next cue `only` (of his own voice), built in his own speech task within 4 s, plays as
  // `target` (a full cue name without XACT_SOUNDBANK_, of any voice set: SCI1_LIGHTS_OUT_NEXT_QUESTION on a
  // scientist whose voice number is SCI2's).
  void ArmSwapName(uint8_t *base, uint32_t c, uint32_t voice, const std::string &target, const char *only, float now,
                   const char *only2)
  {
    static const char *const kVoice[5] = {"SCI1", "SCI2", "TROOPER1", "TROOPER2", "TROOPER3"};
    // (Nightclub helper: on a zone level also the row's other guard voices, GANGSTA1-3: their own set's cue. The
    // Laboratory: 15-19 only, as before.)
    const char *set = voice >= 15 && voice <= 19 ? kVoice[voice - 15]
                      : ZoneLevel() && GuardVoice(voice) ? VoiceTableName(voice)
                                                         : nullptr;
    if (!set)
    {
      return;
    }
    const std::string to = "XACT_SOUNDBANK_" + target;
    uint32_t &guest = g_swap_guest[to];
    if (!guest)
    {
      guest = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(to.size() + 1));
      if (guest)
      {
        std::memcpy(base + guest, to.c_str(), to.size() + 1);
      }
    }
    if (!guest)
    {
      return;
    }
    g_swap_c = c;
    g_swap_from = std::string("XACT_SOUNDBANK_") + set + "_";
    g_swap_only = only;
    g_swap_only2 = only2 ? only2 : ""; // (round 11k: or this cue: Rare's _TEAM for a _SOLO event)
    g_swap_to = guest;
    g_swap_at = now;
    g_swap_armed = true;
  }
  void DisarmSwap()
  {
    g_swap_armed = false;
  }

  // Round 5 (5f): the scientists who saw the lamp go out say Rare's unused LIGHT_SHOT_OUT, one after another. (Round
  // 4's trooper comments are gone: nobody says "never mind" when a lamp goes out, only at the end of a real search.)
  // Round 11 (item 1): once, right after Jo went loud: what the stealth code had put on the guards is taken off (its
  // soft-alarm flags, as the alarm's calm-down does; the test pin), its waiting lines and the cue swap are dropped
  // and the music duck is let go. Their running tasks are left alone: a SEARCH of ours is a SEARCH of Rare's, which
  // reacts to the alarm, to gunfire and to a sighting by Rare's own rules and ends by them.
  void LoudRelease(PPCContext &ctx, uint8_t *base)
  {
    g_loud_released = true;
    bool rare_soft = false;
    for (const auto &[c, g] : g_guards)
    {
      rare_soft = rare_soft || (!g.soft_by_us && g.st != St::Dead && (Load32(base, c + kConfig) & kSoftAlarm));
    }
    int live = 0, soft = 0;
    std::string busy;
    for (auto &[c, g] : g_guards)
    {
      if (g.st == St::Dead || g.dead || Load32(base, c + kData) != g.data)
      {
        continue;
      }
      ++live;
      if (g.soft_by_us && !rare_soft)
      {
        Store32(base, c + kConfig, Load32(base, c + kConfig) & ~kSoftAlarm);
        ++soft;
      }
      g.soft_by_us = false;
      g.pinned = false;
      g.metered = false;
      g.hold_ev = 0;
      g.warn_ev = 0;
      if (g.run_committed)
      {
        REXLOG_INFO("pdz_lab_stealth: {} ALARM RUN ended: the alarm was raised ({:.1f} s after his shout started; the task was "
                    "handed to him {} time(s))",
                    Id(base, c), Clock(base) - g.run_commit_at, g.run_gives);
        g.run_committed = false;
      }
      g.run_pending = false;
      g_alarm_deferred = 0;
      g.sci_cue.clear();
      g.sci_hold_until = -1e9f;
      if (g.st != St::Unaware)
      {
        busy += fmt::format(" {} ({}{})", Id(base, c), StName(g.st),
                            g.st == St::Investigating ? fmt::format(": {}", InvName(g.inv)) : std::string());
      }
    }
    g_holds = 0;
    g_said_lines.clear();
    g_lines.clear();
    g_tannoy_want = -1.0f;
    DisarmSwap();
    if (g_duck_db > 0.0f || voice_volume::MusicDuck() < 0.999f)
    {
      voice_volume::SetMusicDuck(ctx, base, 1.0f);
    }
    g_duck_db = 0.0f;
    REXLOG_INFO("pdz_lab_stealth: LOUD: released: {} live tracked guard(s) and scientist(s) are Rare's from now on ({} "
                "soft-alarm flag(s) of ours cleared); not calm at that moment:{}",
                live, soft, busy.empty() ? std::string(" nobody") : busy);
  }

  // ------------------------------------------------------------------------------------------------------------
  // Round 11b. The rule above everything: IF A GUARD OR SCIENTIST HAS NOTICED SOMETHING, THE PLAYER MUST HEAR IT.
  // ------------------------------------------------------------------------------------------------------------
  // The one voice is taken for `seconds` from now (a line played on a speaker, the lights-out talk).
  void HoldVoice(uint32_t c, float now, float seconds)
  {
    Line l;
    l.start = now;
    l.end = now + seconds;
    l.c = c;
    l.played = true;
    g_lines.push_back(l);
    if (l.end > g_last_line_end || g_last_line_end > now + 30.0f)
    {
      g_last_line_end = l.end;
    }
  }
  bool VoiceBusy(float now)
  {
    return std::any_of(g_lines.begin(), g_lines.end(), [now](const Line &l) {
      return l.end > now || (!l.played && now >= l.start && now - l.start < 2.5f);
    });
  }

  // A scientist's own line (his warnings, his lamp comments, the lights-out talk): the set (SCI1 / SCI2), the cue,
  // the takes it may be, its longest take in seconds.
  // Round 11c (item 1; log 005: all 14 scientist lines of round 11b failed, "the cue is not in the loaded banks": that
  // test of the cue reference was wrong, and the lights-out lines, which went the same way without the test, were
  // made and started by XACT but were not heard). They go through Rare's speech again, as a WARNING line like the
  // guards' (WarnTick): Rare's LIGHT_SHOT_OUT event, which a scientist's speech plays, with the cue name swapped to
  // the one wanted in his own speech task; it waits for the gap, skips Rare's squad throttle and our limits and is
  // asked for again until DarkAudioSound::Play started it.
  void SciQueue(const uint8_t *base, Guard &g, const std::string &bank, const std::string &cue, uint32_t takes, float len,
                float now, const char *what, bool sighting)
  {
    g.warn_ev = EventIndex(base, "LIGHT_SHOT_OUT");
    g.warn_from = now;
    g.warn_until = now + std::max(1.0f, float(REXCVAR_GET(pdz_lab_stealth_warn_wait_s)));
    g.warn_try = g.warn_queued = -1e9f;
    g.warn_tries = 0;
    g.warn_takes = 0;
    g.warn_swap = nullptr;
    g.warn_what = what;
    g.sci_bank = bank;
    g.sci_cue = cue;
    g.sci_mask = takes;
    g.sci_len = len;
    g.sci_from = now;
    g.sci_sighting = sighting;
  }
  void SciSay(const uint8_t *base, Guard &g, const char *cue, uint32_t takes, float len, float now, const char *what,
              bool sighting = false)
  {
    SciQueue(base, g, g.sci_set1 || g.voice == 15 ? "SCI1" : "SCI2", cue, takes, len, now, what, sighting);
  }

  // Item 3: the scientist turns to Jo (step false) or takes a step or two towards her and looks at her (step true):
  // Rare's own task parts, as his SEE_BODY reaction builds a guard's walk to a body (WalkToBody above): a composite
  // of a go-to-object part and a look-at-object part with Jo as the object (Rare's object position sub_82151210
  // reads any game object: its +228 holder, else +48), queued on top of his running task. The go-to part stops
  // pdz_lab_scientist_step_cm nearer to her than he stands (the turn: where he stands already, so it ends at once).
  bool SciReact(PPCContext &ctx, uint8_t *base, uint32_t c, uint32_t jo, bool step)
  {
    if (!REXCVAR_GET(pdz_lab_scientist_warn_moves) || !Ptr(jo))
    {
      return false;
    }
    bool top_is_search = false;
    const int depth = StackDepth(base, c), waiting = StackWaiting(base, c);
    if (depth < 1 || depth >= kGiveDepth || waiting != 0 || StackSearches(base, c, top_is_search) > 0 ||
        (Load32(base, c + kConfig) & (kSearching | kDormant)) != 0)
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} does not {} now (his task stack is busy): {}", Id(base, c),
                    step ? "step towards Jo" : "turn to Jo", StackText(base, c));
      }
      return false;
    }
    const float d = Dist(base, c, jo);
    const float stop = step ? std::max(150.0f, d - std::max(50.0f, float(REXCVAR_GET(pdz_lab_scientist_step_cm)))) : d + 200.0f;
    const uint32_t composite = PoolTask(ctx, base, 96);
    if (!composite)
    {
      return false;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      sub_821CA7E0(frame.ctx, base);
      Store32(base, composite, 0x820922A4);
    }
    uint32_t look = 0, go = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 80;
      sub_821784F0(frame.ctx, base);
      look = Ptr(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
    }
    if (look)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = look;
      frame.ctx.r4.u64 = jo;
      frame.ctx.r5.u64 = 1;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      sub_821A1538(frame.ctx, base);
      look = frame.ctx.r3.u32;
    }
    go = PoolTask(ctx, base, 144);
    if (go)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = go;
      frame.ctx.r4.u64 = jo;
      frame.ctx.r5.u64 = 1;
      frame.ctx.r6.u64 = 1;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.r9.u64 = 0;
      frame.ctx.r10.u64 = 0;
      frame.ctx.f1.f64 = double(stop);
      sub_821D5678(frame.ctx, base);
      Store32(base, go, 0x82092E88);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = go;
      frame.ctx.r5.u64 = look;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.r9.u64 = 0;
      frame.ctx.r10.u64 = 0;
      sub_821CA8C0(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c + kScheduler;
      frame.ctx.r4.u64 = composite;
      sub_82171210(frame.ctx, base);
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} {} (Rare's go-to-object / look-at-object parts, task {:08X}; he is {:.0f} cm from her, "
                  "the walk stops at {:.0f} cm): {}",
                  Id(base, c), step ? "STEPS TOWARDS JO and looks at her" : "TURNS TO JO", composite, d, stop, StackText(base, c));
    }
    return true;
  }

  // Item 3: his meter is full. The first time in this climb: his sighting line is asked for and he hesitates
  // (true = still hidden from Rare's sight). The hesitation is pdz_lab_scientist_hesitate_s from the line's real
  // start (SciTick), 3 s more at most while the line waits for the gap.
  bool SciHold(const uint8_t *base, uint32_t c, Guard &g, float now)
  {
    if (!g.scientist || (g.voice != 15 && g.voice != 16) || !REXCVAR_GET(pdz_lab_scientist_warn) ||
        !(g.st == St::Unaware || g.st == St::Wary) || g.combat_script != 0 || g.dead ||
        HurtKnown(g)) // (follow-up Sun 15 Nov, item 16: hurt by Jo: no hesitation, Rare's sight at once)
    {
      return false;
    }
    if (g.sci_stage < 3)
    {
      // Round 11c (item 2; log 005 19:20:52.7-53.3: s5l2 at 8 m went from warning 1 to his sighting in 0.6 s): the
      // stages come one after another however fast his meter fills: warning 2 at least
      // pdz_lab_scientist_warn_gap_s after warning 1 really started, his sighting line as long after warning 2.
      // Until then he stays hidden from Rare's sight (8 s at most after his meter was first full).
      if (g.sci_full_at < -1e8f || now < g.sci_full_at)
      {
        g.sci_full_at = now;
      }
      const float gap = std::max(0.0f, float(REXCVAR_GET(pdz_lab_scientist_warn_gap_s)));
      const bool ready = g.sci_stage == 2 && !g.warn_ev && now - g.sci_line_at >= gap;
      if (!ready && now - g.sci_full_at < 8.0f)
      {
        return true; // (warnings 1 and 2 come first: SciTick)
      }
      const float hes = std::max(0.0f, float(REXCVAR_GET(pdz_lab_scientist_hesitate_s)));
      g.sci_stage = 3;
      g.sci_hold_until = hes > 0.0f ? now + hes + 3.0f : now;
      SciSay(base, g, "WARN_TEAM", 0xF, 2.4f, now, "scientist warning 3: his sighting line", true);
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} SCIENTIST WARNING 3 (meter {:.0f}, full for {:.1f} s): he sees Jo: his sighting line "
                    "WARN_TEAM (\"Someone call security!\" kind), then {:.1f} s of hesitation before Rare's sight (his alarm "
                    "run) takes over",
                    Id(base, c), g.meter, now - g.sci_full_at, hes);
      }
      return hes > 0.0f;
    }
    return now < g.sci_hold_until;
  }

  void SciTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now)
  {
    if (!g.scientist)
    {
      return;
    }
    if (!REXCVAR_GET(pdz_lab_scientist_warn) || (g.voice != 15 && g.voice != 16) || g.dead || g.st == St::Dead ||
        !(g.st == St::Unaware || g.st == St::Wary) || g.spotted || g.combat_script != 0 ||
        HurtKnown(g)) // (follow-up Sun 15 Nov, item 16: hurt by Jo: no warnings)
    {
      return;
    }
    if (g.sci_stage > 0 && g.meter < std::max(20.0f, Floor(g) + 1.0f) && now >= g.sci_hold_until)
    {
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} lost sight of her (meter {:.0f}): his warnings may come again", Id(base, c), g.meter);
      }
      g.sci_stage = 0;
      g.sci_full_at = -1e9f;
    }
    // Round 11c (item 2): the next warning waits for the one before to have really started (or been given up) and
    // pdz_lab_scientist_warn_gap_s more.
    const bool gap_over = !g.warn_ev && now - g.sci_line_at >= std::max(0.0f, float(REXCVAR_GET(pdz_lab_scientist_warn_gap_s)));
    const float w1 = float(REXCVAR_GET(pdz_lab_scientist_warn_1)), w2 = std::max(w1, float(REXCVAR_GET(pdz_lab_scientist_warn_2)));
    if (g.sci_stage == 1 && g.meter >= w2 && gap_over)
    {
      // "Is someone creeping about?" (HEAR_UNKNOWN_TEAM take 3 in both voices), and a step or two towards her.
      g.sci_stage = 2;
      SciSay(base, g, "HEAR_UNKNOWN_TEAM", 0x4, 1.3f, now, "scientist warning 2: more alarmed");
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} SCIENTIST WARNING 2 (meter {:.0f} of {:.0f}, {:.0f} cm from Jo): \"Is someone creeping "
                    "about?\" and a step towards her",
                    Id(base, c), g.meter, Threshold(g), Ptr(jo) ? Dist(base, c, jo) : 0.0f);
      }
      SciReact(ctx, base, c, jo, true);
    }
    else if (g.sci_stage < 1 && g.meter >= w1)
    {
      // "Who's there?" / "What was that?" (HEAR_UNKNOWN_SOLO takes 1 and 2), and he turns to where he glimpsed her.
      g.sci_stage = 1;
      SciSay(base, g, "HEAR_UNKNOWN_SOLO", 0x3, 1.8f, now, "scientist warning 1: who's there?");
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: {} SCIENTIST WARNING 1 (meter {:.0f} of {:.0f}, {:.0f} cm from Jo): \"Who's there?\" and he "
                    "turns to her",
                    Id(base, c), g.meter, Threshold(g), Ptr(jo) ? Dist(base, c, jo) : 0.0f);
      }
      SciReact(ctx, base, c, jo, false);
    }
  }

  // The lamp is in front of him: on the ground plan within 75 degrees of his facing (Rare's facing vector from his
  // last sight test, else his yaw), as LightIncident has it.
  bool LampInFront(const uint8_t *base, uint32_t c, const Guard &g, float now, const float eye[3], const float lamp[3])
  {
    float fx = g.dir[0], fz = g.dir[2];
    if (!(now >= g.eye_at && now - g.eye_at < 3.0f) || fx * fx + fz * fz < 1e-6f)
    {
      const float yaw = LoadF(base, c + 0x1670);
      fx = std::sin(yaw);
      fz = std::cos(yaw);
    }
    const float lx = lamp[0] - eye[0], lz = lamp[2] - eye[2];
    const float ll = std::sqrt(lx * lx + lz * lz), fl = std::sqrt(fx * fx + fz * fz);
    return !(ll > 100.0f && fl > 1e-4f && (lx * fx + lz * fz) / (ll * fl) < 0.26f);
  }

  // His spot under a lamp (LightIncident (e)): a pad not above it, within 7 m of it on the ground plan, in his
  // reach, with a clear line to the lamp. 0: none.
  uint32_t LampSpot(PPCContext &ctx, uint8_t *base, uint32_t c, const float lamp[3], const LampView &view)
  {
    uint32_t spot = 0;
    float best = 1e9f;
    for (const auto &[pad, hop] : PadsAround(base, Load32(base, c + kPad), 60, 1200))
    {
      float p[3];
      if (!PadPos(base, pad, p) || p[1] > lamp[1] + 30.0f)
      {
        continue;
      }
      const float dx = p[0] - lamp[0], dz = p[2] - lamp[2];
      const float flat = std::sqrt(dx * dx + dz * dz);
      const float score = flat + 0.5f * (lamp[1] - p[1]);
      if (flat > 700.0f || score >= best)
      {
        continue;
      }
      const float head[3] = {p[0], p[1] + 150.0f, p[2]};
      if (!SightClear(ctx, base, lamp, head) && !LampSeen(ctx, base, view, head))
      {
        continue;
      }
      best = score;
      spot = pad;
    }
    return spot;
  }

  // Item 7: a calm guard gets a lamp Jo shot out in view without having noticed it go out: he says so (a warning
  // line: it always plays), walks over for a look of 5-10 s under it and goes back to what he was doing, calm or
  // wary as before. Once per lamp: a lamp somebody went to look at is never checked again. A scientist who sees one
  // only comments (it counts for his dark area, LampTalkTick). Looked for every 0.7 s per guard.
  void DarkLampTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool busy)
  {
    if (!REXCVAR_GET(pdz_lab_dark_lamp_notice) || g_dead_lamps.empty() || busy || g.dead || g.st == St::Dead ||
        (now >= g.dark_check_at && now - g.dark_check_at < 0.7f) ||
        R8BodyCheck(c, g)) // (Tue 17 Nov night, fix2, item 3: not while he checks a body; the lamp waits)
    {
      return;
    }
    g.dark_check_at = now;
    const bool trooper = Calm(g);
    const bool scientist = g.scientist && (g.st == St::Unaware || g.st == St::Wary) && !g.spotted && !g.seen_jo &&
                           REXCVAR_GET(pdz_lab_stealth_scientist_lamps);
    if ((!trooper && !scientist) || (Load32(base, c + kConfig) & kDormant) != 0 || !(LoadF(base, c + kHealth) > 0.0f) ||
        (trooper && (Looking(g, now) || g.body_pending || g.warn_ev || NoWalk(g.who)))) // (Nightclub helper: no_look;
                                                                                       // Mansion: no_walk)
    {
      return;
    }
    float eye[3];
    EyeOf(base, c, g, now, eye);
    const float range = float(trooper ? REXCVAR_GET(pdz_lab_lamp_notice_cm) : REXCVAR_GET(pdz_lab_lamp_notice_scientist_cm));
    for (DeadLamp &l : g_dead_lamps)
    {
      if ((trooper && l.checked) || now < l.at || now - l.at < 3.0f ||
          std::find(l.noticed.begin(), l.noticed.end(), c) != l.noticed.end())
      {
        continue;
      }
      const float lamp[3] = {l.pos[0], l.pos[1] - 40.0f, l.pos[2]};
      const float d = Dist3(eye, lamp);
      if (d > range || !LampInFront(base, c, g, now, eye, lamp))
      {
        continue;
      }
      LampView view;
      std::memcpy(view.below, l.below, sizeof(view.below));
      std::memcpy(view.floor, l.floor, sizeof(view.floor));
      view.has_floor = l.has_floor;
      if (!SightClear(ctx, base, lamp, eye) && !LampSeen(ctx, base, view, eye))
      {
        continue;
      }
      l.noticed.push_back(c);
      if (scientist)
      {
        // (round 11j: not when a scientist within 15 m of him has commented on this lamp)
        const bool again = (now >= g.lamp_line_at && now - g.lamp_line_at < float(REXCVAR_GET(pdz_lab_lamp_comment_s))) ||
                           std::any_of(l.commented.begin(), l.commented.end(),
                                       [&](uint32_t o) { return o != c && Dist(base, o, c) <= 1500.0f; });
        if (!again && !g.warn_ev && (g.voice == 15 || g.voice == 16))
        {
          SciSay(base, g, "LIGHT_SHOT_OUT", 0x7, 1.0f, now, "sees a lamp that is out");
          g.lamp_line_at = now;
          l.commented.push_back(c);
        }
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} (scientist) sees a lamp that is out at ({:.0f} {:.0f} {:.0f}) from {:.0f} cm (it went "
                      "out {:.0f} s ago){}: it counts for his dark area",
                      Id(base, c), l.pos[0], l.pos[1], l.pos[2], d, now - l.at,
                      again ? ", no comment: his last one was less than pdz_lab_lamp_comment_s ago" : ": he comments");
        }
        return;
      }
      const bool was_wary = g.st == St::Wary;
      const bool may_go = REXCVAR_GET(pdz_lab_stealth_investigation) &&
                          !(Load32(base, c + kConfig) & (kSearching | kNeverInvestigates));
      const uint32_t spot = may_go ? LampSpot(ctx, base, c, lamp, view) : 0;
      if (Ptr(spot))
      {
        g.investigator_lamp = true;
        g.squad = SquadOf(ctx, base, c);
        g_next_brief = true;
        StartInvestigation(ctx, base, c, g, Inv::Lamp, l.pos, -1, now,
                           fmt::format("SEES A LAMP THAT IS OUT at ({:.0f} {:.0f} {:.0f}) from {:.0f} cm (it went out {:.0f} s "
                                       "ago, nobody has checked it): he goes for a short look under it",
                                       l.pos[0], l.pos[1], l.pos[2], d, now - l.at),
                           spot);
        g_next_brief = false;
        if (g.st == St::Investigating)
        {
          g.inv_from_wary = was_wary;
          l.checked = true;
        }
      }
      else
      {
        Alert(ctx, base, c, g, now,
              fmt::format("sees a lamp that is out at ({:.0f} {:.0f} {:.0f}) from {:.0f} cm; does not go: {}", l.pos[0], l.pos[1],
                          l.pos[2], d, may_go ? "no spot under it in his reach with a view of it" : "Rare's never-investigates flag or a search of Rare's"));
      }
      // Round 11c (item 3): the calm takes of LIGHT_SHOT_OUT only: 3 and 4 in every trooper voice ("That bulb blew." /
      // "Where'd the light go?", "Bulb popped!" / "Who turned out the light?", "That light blow?" / "Who switched off
      // the lights?"); takes 1 and 2 ("What was that?" / "Who did that?") are for a lamp he saw or heard go.
      Warn(ctx, base, c, g, EventIndex(base, "LIGHT_SHOT_OUT"), now, "finds a lamp dark (calm takes)", 0xC);
      return;
    }
  }

  // Item 6: the scientists' lights-out conversation for lamps Jo shot out (restorations.cpp plays it: LampTalk). A
  // calm scientist who has not seen Jo and whose area is properly dark: he knows of pdz_lab_lamp_talk_lamps dead
  // lamps (shots that put lamps out; he saw or heard them go, or saw them dark) within pdz_lab_lamp_talk_cm of him,
  // or he stands where one of them shone and knows of one more within 15 m. He has the conversation with the nearest
  // second calm scientist within 15 m (one asks, the other answers 4 s later), or says his solo line. Once per
  // scientist. Never after LOUD (this code is off then; the generator blackout's own trigger is restorations.cpp's).
  float g_talk_check = -1e9f;
  void LampTalkTick(PPCContext &ctx, uint8_t *base, float now)
  {
    if (!REXCVAR_GET(pdz_lab_lamp_talk) || g_dead_lamps.empty() || (now >= g_talk_check && now - g_talk_check < 0.5f))
    {
      return;
    }
    g_talk_check = now;
    if (lab_dark::RareBlackout())
    {
      return;
    }
    const float near_cm = float(REXCVAR_GET(pdz_lab_lamp_talk_cm));
    const int need = std::max(1, int(REXCVAR_GET(pdz_lab_lamp_talk_lamps)));
    const auto calm = [&](uint32_t c, const Guard &g) {
      return g.scientist && (g.voice == 15 || g.voice == 16) && !g.dead && (g.st == St::Unaware || g.st == St::Wary) &&
             !g.spotted && !g.seen_jo && !g.confirmed && g.sci_stage == 0 && Load32(base, c + kData) == g.data &&
             LoadF(base, c + kHealth) > 0.0f && (Load32(base, c + kConfig) & kDormant) == 0;
    };
    for (auto &[c, g] : g_guards)
    {
      if (g.lamp_talked || !calm(c, g))
      {
        continue;
      }
      float me[3];
      PosOf(base, c, me);
      int near = 0, known = 0;
      bool in_light = false;
      for (const DeadLamp &l : g_dead_lamps)
      {
        if (now < l.at || now - l.at < 2.5f || std::find(l.noticed.begin(), l.noticed.end(), c) == l.noticed.end())
        {
          continue; // (his comment on the lamp comes first)
        }
        const float d = Dist3(me, l.pos);
        known += d <= 1500.0f ? 1 : 0;
        near += d <= near_cm ? 1 : 0;
        in_light = in_light || (l.reach > 0.0f && d <= l.reach);
      }
      if (!(near >= need || (in_light && known >= 2)))
      {
        continue;
      }
      if (StoryPlaying(base, now) || VoiceBusy(now) || g.warn_ev)
      {
        return; // it waits for the gap
      }
      uint32_t partner = 0;
      float partner_d = 1500.0f;
      for (const auto &[oc, og] : g_guards)
      {
        if (oc == c || og.lamp_talked || og.warn_ev || !calm(oc, og))
        {
          continue;
        }
        const float d = Dist(base, c, oc);
        if (d <= partner_d)
        {
          partner = oc;
          partner_d = d;
        }
      }
      if (!restorations::LampTalk(ctx, base, c, partner))
      {
        return; // (another line of the talk is pending, or the switch is off: looked at again in half a second)
      }
      g.lamp_talked = true;
      if (partner)
      {
        const auto it = g_guards.find(partner);
        if (it != g_guards.end())
        {
          it->second.lamp_talked = true;
        }
      }
      REXLOG_INFO("pdz_lab_stealth: LIGHTS-OUT TALK for shot-out lamps: {} at ({:.0f} {:.0f} {:.0f}) knows of {} dead lamp(s) "
                  "within {:.0f} cm{}: {}",
                  Id(base, c), me[0], me[1], me[2], near, near_cm,
                  in_light ? fmt::format(" (he stands where one shone; {} within 15 m)", known) : std::string(),
                  partner ? fmt::format("the conversation with {} ({:.0f} cm from him)", Id(base, partner), partner_d)
                          : std::string("no other calm scientist within 15 m: his solo line"));
      return;
    }
  }

  void IncidentTick(PPCContext &ctx, uint8_t *base, float now)
  {
    if (g_incident.said >= g_incident.others.size() || now < g_incident.next)
    {
      return;
    }
    if (now - g_incident.at > 12.0f || now < g_incident.at)
    {
      g_incident.others.clear();
      return;
    }
    const uint32_t c = g_incident.others[g_incident.said];
    Guard *g = Find(c);
    const uint32_t ev = EventIndex(base, "LIGHT_SHOT_OUT");
    if (!g || !g->scientist || g->dead || g->st == St::Dead || g->spotted || !ev)
    {
      ++g_incident.said;
      return;
    }
    if (REXCVAR_GET(pdz_lab_stealth_warn_lines) && (g->voice == 15 || g->voice == 16))
    {
      // Round 11b (items 4 / 5): his comment is played on him (it waits for the gap, then always plays).
      if (!g->warn_ev)
      {
        SciSay(base, *g, "LIGHT_SHOT_OUT", 0x7, 1.0f, now, "lamp out (scientist)");
        g->lamp_line_at = now;
      }
      ++g_incident.said;
      g_incident.next = now + 2.0f;
    }
    else if (Say(ctx, base, c, ev, false, "lamp out (scientist)"))
    {
      g->lamp_line_at = now;
      ++g_incident.said;
      g_incident.next = now + 2.0f;
    }
    else
    {
      g_incident.next = now + 0.7f;
    }
  }
} // namespace

#include "lab_stealth_r3c_A2_Sun15Nov.inc" // (round 3c, fix 1: the level start test and the clear itself: after every record is defined)

namespace lab_stealth
{
  // Round 11c (item 6): a scientist's line through Rare's speech, as a warning line: `cue` with its set
  // (SCI1_LIGHTS_OUT_NEXT_QUESTION), the takes it may be, its longest take. False: the stealth code does not track
  // him as a scientist (or is off): the caller plays it its own way.
  bool StaffLine(const uint8_t *base, uint32_t character, const std::string &cue, uint32_t takes, float len, const char *what)
  {
    if (!LabActive(base) || !REXCVAR_GET(pdz_lab_stealth_warn_lines))
    {
      return false;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    Guard *g = Find(character);
    const size_t us = cue.find('_');
    if (!g || !g->scientist || g->dead || g->st == St::Dead || (g->voice != 15 && g->voice != 16) || us == std::string::npos)
    {
      return false;
    }
    SciQueue(base, *g, cue.substr(0, us), cue.substr(us + 1), takes, len, Clock(base), what, false);
    return g->warn_ev != 0;
  }

  uint32_t CueName(uint8_t *base, uint32_t name)
  {
    R4CCue(base, name); // (round 4 C: Killian's escape line; one atomic read unless armed)
    if (!g_swap_armed.load(std::memory_order_relaxed) || !name)
    {
      return name;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g_swap_armed)
    {
      return name;
    }
    const float now = Clock(base);
    if (now < g_swap_at || now - g_swap_at > 4.0f)
    {
      g_swap_armed = false;
      return name;
    }
    const char *text = reinterpret_cast<const char *>(base + name);
    if (std::strncmp(text, g_swap_from.c_str(), g_swap_from.size()) != 0)
    {
      return name;
    }
    if (t_name_c == 0 || t_name_c != g_swap_c)
    {
      return name; // not the commenter's own speech task: another speaker's cue
    }
    const char *cue = text + g_swap_from.size();
    if (!g_swap_only.empty())
    {
      // Round 11c: a staff line: only the cue asked for is swapped.
      if (std::strcmp(cue, g_swap_only.c_str()) != 0 && (g_swap_only2.empty() || std::strcmp(cue, g_swap_only2.c_str()) != 0))
      {
        return name;
      }
      g_swap_armed = false;
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: line swap: Rare's speech asked for {}, played as {}",
                    std::string(text + 15, strnlen(text + 15, 96)), reinterpret_cast<const char *>(base + g_swap_to) + 15);
      }
      return g_swap_to;
    }
    const bool body = std::strncmp(reinterpret_cast<const char *>(base + g_swap_to) + g_swap_from.size(), "SEE_BODY_", 9) == 0;
    if (body ? (std::strcmp(cue, "SEE_BODY_SOLO") != 0 && std::strcmp(cue, "SEE_BODY_TEAM") != 0)
             : (std::strcmp(cue, "HEAR_UNKNOWN_TEAM") != 0 && std::strcmp(cue, "BEEN_SHOT_MISS_UNAWARE_SOLO") != 0))
    {
      return name; // another line of his (pain, death, combat): never swapped
    }
    g_swap_armed = false;
    if (body)
    {
      // Round 6: a body line: Rare made _TEAM of the _SOLO event (he has squad mates); the one asked for plays.
      return g_swap_to;
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: lamp-out comment: Rare's speech asked for {}, played as {}",
                  std::string(text + 15, strnlen(text + 15, 96)), reinterpret_cast<const char *>(base + g_swap_to) + 15);
    }
    return g_swap_to;
  }

  // Round 5 (item 5): a lamp went out.
  // (a) Only those who could see it react: Rare's sight ray between the lamp (40 cm under its fitting) and the eye is
  //     clear, within 30 m; or, without sight, within pdz_lab_stealth_lamp_hear_cm with an open walking route to it of
  //     at most 1.5 times that (an open doorway: he hears the pop). Never through closed doors or floors.
  // (b) Of the calm troopers who noticed it, the nearest two (three when five or more did) walk over, each on his own
  //     spots (round 4's investigation); the others are alert (they stop, look round, the meter fills faster).
  //     Nobody says "never mind" at the start.
  // (c) One investigation per light: no squad claim.
  // (d) A third lamp within 60 s and 20 m of two others is a spree: the area's calm troopers search it together.
  // (e) The first stop is a pad under the lamp: not above it, within 7 m of it on the ground plan, in his reach, with a
  //     clear line to the lamp; none: he does not go.
  // (f) Scientists who could see it say Rare's unused LIGHT_SHOT_OUT and are alert; they never go.
  // Round 3c (B, fix 2): Rare's light-surface routine has run for a hit (lab_light_shot_log.cpp): the light it picked
  // and that light's real state before and after Rare's call. Nothing counts here: the light is watched
  // (R3cBLampTick) and becomes a LightIncident only once it is really out.
  bool LampShot(PPCContext &ctx, uint8_t *base, uint32_t light, const float hit[3], uint32_t prop, uint32_t caller, uint32_t before,
                uint32_t after, int lamps)
  {
    (void)ctx;
    if (!LabActive(base) || !Ptr(light))
    {
      return false;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    return R3cBLampShot(base, light, hit, prop, caller, before, after, lamps);
  }

  void LightIncident(PPCContext &ctx, uint8_t *base, const float pos[3], int lamps)
  {
    if (!LabActive(base))
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    if (RadioRow() && StealthLog())
    {
      // Mansion helper: the grounds have no lamp props; a scene light that is really out (round 3c: never a candle).
      REXLOG_INFO("pdz_stealth_light: {}: a scene light went out at ({:.0f} {:.0f} {:.0f}) (really out: round 3c): "
                  "lamp lure for the guards of the grounds",
                  g_level->name, pos[0], pos[1], pos[2]);
    }
    const bool by_sight = REXCVAR_GET(pdz_lab_stealth_lamp_sight);
    const float hear = float(REXCVAR_GET(pdz_lab_stealth_lamp_hear_cm));
    const float lamp[3] = {pos[0], pos[1] - 40.0f, pos[2]};
    const LampView view = by_sight ? LampViewOf(ctx, base, pos) : LampView();
    // (c) A guard who stands in the light the lamp gave (within its reach; Rare's lights shine through decks and
    // walls, so the floor a storey below a walkway lamp is lit by it) notices his own surroundings dim: counted when
    // an open pad route no longer than 1.5 x the reach leads from his pad to the pad of his floor under the lamp (a
    // closed door between them breaks it).
    const float reach = by_sight && lab_dark::On() ? lab_dark::LampReach(base, pos) : 0.0f;
    g_lamps_out.erase(std::remove_if(g_lamps_out.begin(), g_lamps_out.end(),
                                     [now](const LampOut &l) { return now - l.at > 60.0f || now < l.at; }),
                      g_lamps_out.end());
    int near_before = 0;
    for (const LampOut &l : g_lamps_out)
    {
      near_before += Dist3(l.pos, pos) <= 2000.0f ? 1 : 0;
    }
    LampOut out;
    std::memcpy(out.pos, pos, sizeof(out.pos));
    out.at = now;
    g_lamps_out.push_back(out);
    {
      // Round 11b (items 6 / 7): the lamp is remembered for the rest of the run.
      if (g_dead_lamps.size() >= 64)
      {
        g_dead_lamps.erase(g_dead_lamps.begin());
      }
      const LampView dv = by_sight ? view : LampViewOf(ctx, base, pos);
      DeadLamp dl;
      std::memcpy(dl.pos, pos, sizeof(dl.pos));
      dl.at = now;
      dl.reach = lab_dark::On() ? lab_dark::LampReach(base, pos) : 0.0f;
      std::memcpy(dl.below, dv.below, sizeof(dl.below));
      std::memcpy(dl.floor, dv.floor, sizeof(dl.floor));
      dl.has_floor = dv.has_floor;
      g_dead_lamps.push_back(dl);
    }
    const size_t dead_i = g_dead_lamps.size() - 1;
    struct Reactor
    {
      float d;
      uint32_t c;
      bool saw;
    };
    std::vector<Reactor> troopers, scientists;
    std::vector<uint32_t> spots; // the spots under the lamp already given out (one guard per spot)
    std::string blocked, seen_how, heard_how;
    for (auto &[c, g] : g_guards)
    {
      // (A guard already looking at a lamp or a sound still notices the next lamp: the spree rule needs him.)
      const bool looking = !g.scientist && !g.dead && g.st == St::Investigating && (g.inv == Inv::Lamp || g.inv == Inv::Noise) &&
                           LayerGuard(g);
      const bool trooper = Calm(g) || looking;
      const bool scientist = g.scientist && !g.dead && g.st != St::Dead && g.st != St::Combat && !g.spotted &&
                             LoadF(base, c + kHealth) > 0.0f && REXCVAR_GET(pdz_lab_stealth_scientist_lamps);
      if ((!trooper && !scientist) || (Load32(base, c + kConfig) & kDormant))
      {
        continue;
      }
      float eye[3];
      EyeOf(base, c, g, now, eye);
      const float d = Dist3(eye, lamp);
      if (d > 3000.0f)
      {
        continue;
      }
      bool saw = true, heard = false;
      const char *why_not = "";
      int r10_walls = -1;                 // (Thu 19 Nov: the cause line)
      const char *r10_seen = "it was in his view";
      if (by_sight)
      {
        // Round 10 (hand test 033: lamps noticed from 17.9 and 22.4 m, the hub scientists commenting on lamps 15 to
        // 25 m away): (a) he stands in the lamp's light: always (below); (b) else only within pdz_lab_lamp_notice_cm
        // (scientists: pdz_lab_lamp_notice_scientist_cm), inside his view cone (75 degrees off his facing, Rare's own
        // facing vector from his last sight test; unknown: only within 4 m) and with a line of sight to the fitting,
        // the point under it or the floor it lit; (c) else not noticed. No hearing.
        const float notice = float(scientist && !trooper ? REXCVAR_GET(pdz_lab_lamp_notice_scientist_cm)
                                                         : REXCVAR_GET(pdz_lab_lamp_notice_cm));
        bool in_view = d <= notice;
        why_not = in_view ? "no line of sight" : "too far";
        if (in_view)
        {
          // "In front of him": on the ground plane (the lamps hang high), 75 degrees to either side of his facing:
          // Rare's facing vector from his last sight test, else his yaw (+0x1670).
          float fx = g.dir[0], fz = g.dir[2];
          if (!(now >= g.eye_at && now - g.eye_at < 3.0f) || fx * fx + fz * fz < 1e-6f)
          {
            const float yaw = LoadF(base, c + 0x1670);
            fx = std::sin(yaw);
            fz = std::cos(yaw);
          }
          const float lx = lamp[0] - eye[0], lz = lamp[2] - eye[2];
          const float ll = std::sqrt(lx * lx + lz * lz), fl = std::sqrt(fx * fx + fz * fz);
          if (ll > 100.0f && fl > 1e-4f && (lx * fx + lz * fz) / (ll * fl) < 0.26f)
          {
            in_view = false;
            why_not = "behind him or to his side";
          }
        }
        const int how = !in_view ? 0 : SightClear(ctx, base, lamp, eye) ? 3 : LampSeen(ctx, base, view, eye);
        saw = how != 0;
        if (in_view && !saw)
        {
          why_not = "no line of sight";
        }
        if (how == 1 || how == 2)
        {
          seen_how += fmt::format(" {} ({})", Id(base, c), how == 1 ? "the point 1 m under the lamp" : "the lit floor under it");
          r10_seen = how == 1 ? "in his view: the point 1 m under the lamp" : "in his view: the lit floor under it";
        }
        if (!saw && reach > 0.0f && d <= reach)
        {
          float feet[3];
          PosOf(base, c, feet);
          const float under[3] = {pos[0], feet[1] - 81.0f, pos[2]}; // his floor under the lamp (pads are at feet height)
          float pd = 0.0f;
          const uint32_t from = Load32(base, c + kPad);
          const uint32_t to = NearestPadTo(base, from, under, pd);
          if (Ptr(to) && pd <= reach && OpenPathWithin(base, from, to, reach * 1.5f))
          {
            saw = true;
            r10_seen = "he stands in the light it gave: his surroundings went dark";
            seen_how += fmt::format(" {} (he stands in the light it gave: {:.0f} cm from it, its reach {:.0f} cm)", Id(base, c), d, reach);
          }
        }
        if (!saw && hear > 0.0f && d <= hear && !R10LampHearFresh(why_not))
        {
          // (Thu 19 Nov: the glass broke long before the light went dark: not heard now.)
        }
        else if (!saw && hear > 0.0f && d <= hear)
        {
          // Round 11 (item 4; log 010 16:59:43 / 16:59:49: lamps shot out 4.2 m and 3.7 m behind the two hub
          // scientists, nobody reacted): a breaking lamp is HEARD within pdz_lab_stealth_lamp_hear_cm whatever way he
          // faces; walls and floors block it (Rare's block-AI-sound geometry on the line from the lamp to his ears).
          // (follow-up Sun 15 Nov, pdz_stealth_wall_muffle: each wall or floor halves the distance instead)
          const bool muffle = REXCVAR_GET(pdz_stealth_wall_muffle);
          const int lamp_walls = muffle ? SoundWalls(ctx, base, lamp, eye) : 0;
          r10_walls = muffle ? lamp_walls : -1;
          if (muffle && Logging() && lamp_walls)
          {
            REXLOG_INFO("pdz_lab_stealth: {} and the breaking lamp, {:.0f} cm away: {} wall(s) or floor(s) between them, heard "
                        "within {:.0f} cm (of {:.0f} cm) (pdz_stealth_wall_muffle)",
                        Id(base, c), d, lamp_walls, Muffled(hear, lamp_walls), hear);
          }
          if (muffle ? d <= Muffled(hear, lamp_walls)
                     : (SegmentClear(ctx, base, lamp, eye, 0x40) && SegmentClear(ctx, base, eye, lamp, 0x40)))
          {
            heard = true;
            heard_how += fmt::format(" {} ({:.0f} cm{})", Id(base, c), d, in_view ? std::string() : fmt::format(", {}", why_not));
          }
          else
          {
            why_not = "not seen, and a wall or a floor blocks the sound";
          }
        }
      }
      if (!saw && !heard)
      {
        blocked += fmt::format(" {} ({:.0f} cm, {})", Id(base, c), d, by_sight ? why_not : "");
        continue;
      }
      R10LampCause(base, c, lamp, d, saw, heard, r10_walls, r10_seen); // (Thu 19 Nov: every lamp reaction logs its cause)
      (trooper ? troopers : scientists).push_back({d, c, saw});
      g_dead_lamps[dead_i].noticed.push_back(c); // (round 11b: he knows it is out)
    }
    const auto nearer = [](const Reactor &x, const Reactor &y) { return x.d < y.d; };
    std::sort(troopers.begin(), troopers.end(), nearer);
    std::sort(scientists.begin(), scientists.end(), nearer);
    g_incident = Incident();
    std::memcpy(g_incident.pos, pos, sizeof(g_incident.pos));
    g_incident.at = now;
    g_incident.next = now + 1.2f;
    if (Logging() && !seen_how.empty())
    {
      REXLOG_INFO("pdz_lab_stealth: lamp out at ({:.0f} {:.0f} {:.0f}): noticed without a view of the fitting (floor under "
                  "it at height {:.0f}{}, the lamp's reach {:.0f} cm), by:{}",
                  pos[0], pos[1], pos[2], view.floor[1] - 30.0f, view.has_floor ? "" : ": not found", reach, seen_how);
    }
    if (Logging() && !heard_how.empty())
    {
      REXLOG_INFO("pdz_lab_stealth: lamp out at ({:.0f} {:.0f} {:.0f}): HEARD (not seen; within pdz_lab_stealth_lamp_hear_cm "
                  "{:.0f} cm, no wall or floor between) by:{}",
                  pos[0], pos[1], pos[2], hear, heard_how);
    }
    if (Logging() && !blocked.empty())
    {
      REXLOG_INFO("pdz_lab_stealth: lamp out at ({:.0f} {:.0f} {:.0f}): NOT noticed (not heard: beyond {:.0f} cm or a wall or "
                  "floor between; not in its light, reach {:.0f} cm; not seen: beyond {:.0f} cm (scientists {:.0f}), or "
                  "behind him, or no line of sight to it, the point under it or the floor it lit, height {:.0f}{}) by:{}",
                  pos[0], pos[1], pos[2], hear, reach, float(REXCVAR_GET(pdz_lab_lamp_notice_cm)),
                  float(REXCVAR_GET(pdz_lab_lamp_notice_scientist_cm)), view.floor[1] - 30.0f, view.has_floor ? "" : ": not found",
                  blocked);
    }
    // (f) the scientists.
    std::string sci;
    for (const Reactor &r : scientists)
    {
      Guard &g = g_guards[r.c];
      const bool second = now - g.lamp_seen_at < 30.0f && now >= g.lamp_seen_at;
      g.lamp_seen_at = now;
      g.alert_until = now + float(REXCVAR_GET(pdz_lab_stealth_alert_s));
      // Round 8 (log 048: the two hub scientists said LIGHT_SHOT_OUT ten times in three minutes, for lamps up to
      // 24.5 m away): a scientist comments only on a lamp he sees go out (the guards' sight rule; not one he only
      // hears) and not again within pdz_lab_stealth_repeat_s of his last lamp comment.
      const float repeat = float(REXCVAR_GET(pdz_lab_lamp_comment_s)); // round 10: one lamp comment in 30 s
      const bool again = now >= g.lamp_line_at && now - g.lamp_line_at < repeat;
      // Round 11j (item 4): of scientists within 15 m of each other only the nearest to the lamp comments.
      const bool paired = std::any_of(g_incident.others.begin(), g_incident.others.end(),
                                      [&](uint32_t o) { return Dist(base, o, r.c) <= 1500.0f; });
      if (!again && !paired)
      {
        g_incident.others.push_back(r.c); // (round 11: a scientist who only heard it comments too)
        g_dead_lamps[dead_i].commented.push_back(r.c);
      }
      sci += fmt::format(" {} ({:.0f} cm{}{})", Id(base, r.c), r.d, r.saw ? "" : ", heard it",
                         paired ? std::string(", no comment: a nearer scientist within 15 m of him comments")
                         : again ? fmt::format(", no comment: his last one was {:.0f} s ago", now - g.lamp_line_at)
                               : std::string());
      if (second && REXCVAR_GET(pdz_lab_stealth_scientist_spook) && !AlarmOn(base))
      {
        std::string refusal;
        const bool runs = AlarmRun(ctx, base, r.c, refusal);
        if (Logging())
        {
          REXLOG_INFO("pdz_lab_stealth: {} saw a second lamp go out within 30 s (pdz_lab_stealth_scientist_spook): {}",
                      Id(base, r.c), runs ? "RUNS FOR THE ALARM (Rare's alarm run)" : "no alarm run: " + refusal);
        }
      }
    }
    if (troopers.empty())
    {
      g_lamps_out.pop_back(); // round 10: a lamp no guard noticed does not count towards a spree
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: lamp out at ({:.0f} {:.0f} {:.0f}) ({} lamp(s)): no calm guard could see or hear it; "
                    "scientists:{}",
                    pos[0], pos[1], pos[2], lamps, sci.empty() ? std::string(" none") : sci);
      }
      return;
    }
    // (d) a spree. Round 7 (item 3): also a lamp going out where a body was found or an investigator was lost
    // (tier 2: nobody walks up alone).
    std::string tier;
    const bool tier2 = AreaTier2(pos, 0, now, true, tier);
    const std::string spree_why = tier2 ? "ESCALATION TIER 2: " + tier : std::string("the third lamp out within 60 s and 20 m (a spree)");
    if (!RarePlus() && ((near_before >= 2 && REXCVAR_GET(pdz_lab_stealth_lamp_spree)) || tier2)) // (rareplus: no squad search)
    {
      std::vector<uint32_t> squad;
      std::string list;
      for (const Reactor &r : troopers)
      {
        squad.push_back(r.c);
      }
      const float radius = float(REXCVAR_GET(pdz_lab_stealth_call_radius_cm));
      for (auto &[c, g] : g_guards)
      {
        float p[3];
        PosOf(base, c, p);
        if (Calm(g) && !(Load32(base, c + kConfig) & kDormant) && Dist3(p, pos) <= radius &&
            std::find(squad.begin(), squad.end(), c) == squad.end() &&
            std::any_of(troopers.begin(), troopers.end(), [&](const Reactor &r) {
              const Guard &o = g_guards[r.c];
              return o.squad && o.squad == g.squad;
            }))
        {
          squad.push_back(c); // a squad mate of someone who noticed it, within the call radius
        }
      }
      for (const uint32_t c : squad)
      {
        Guard &g = g_guards[c];
        if (g.st == St::Investigating)
        {
          // already looking (a leg's SEARCH runs: no new one can be handed over): his look becomes the area search,
          // from his present leg on
          g.inv = Inv::Squad;
          std::memcpy(g.inv_centre, pos, sizeof(g.inv_centre));
          g.inv_start = now;
          g.inv_until = now + std::max(5.0f, InvSeconds(Inv::Squad));
          g.inv_body = -1;
          if (Logging())
          {
            REXLOG_INFO("pdz_lab_stealth: {} {}: his look becomes the area search, {:.0f} s from now", Id(base, c), spree_why,
                        g.inv_until - now);
          }
        }
        else
        {
          StartInvestigation(ctx, base, c, g, Inv::Squad, pos, -1, now,
                             spree_why + ": the area is searched like a body");
        }
        if (g.st == St::Investigating)
        {
          list += " " + Id(base, c);
        }
      }
      g_lamps_out.clear();
      if (!squad.empty())
      {
        const uint32_t ev = EventIndex(base, "LIGHT_SHOT_OUT");
        if (ev)
        {
          Warn(ctx, base, squad.front(), g_guards[squad.front()], ev, now, "lamp out"); // (round 11b: it always plays)
        }
      }
      if (!list.empty())
      {
        g_dead_lamps[dead_i].checked = true;
      }
      if (Logging())
      {
        REXLOG_INFO("pdz_lab_stealth: lamp out at ({:.0f} {:.0f} {:.0f}) ({} lamp(s)): {}: searching the area for {:.0f} s, "
                    "then wary:{}; scientists:{}",
                    pos[0], pos[1], pos[2], lamps, tier2 ? spree_why : std::string("A SPREE (the third within 60 s and 20 m)"),
                    float(REXCVAR_GET(pdz_lab_stealth_squad_search_s)),
                    list.empty() ? std::string(" nobody could start") : list, sci.empty() ? std::string(" none") : sci);
      }
      return;
    }
    // (b) who goes, who stays.
    // (Nightclub helper: a zone row's lamp_lookers, e.g. 1 on the docks so that a lamp splits a group up.)
    const int most = std::max(1, ZoneLevel() && g_level->lamp_lookers > 0 ? g_level->lamp_lookers
                                                                           : int(REXCVAR_GET(pdz_lab_stealth_lamp_investigators)));
    const int want = std::min(most, int(troopers.size())); // round 10: one guard goes to look per lamp (the default)
    int going = 0;
    std::string went, stayed;
    // Round 11b (item 5; log 003 18:32:45-18:33:17): every guard who notices the lamp, by sight or by sound, goes to
    // look (pdz_lab_stealth_lamp_investigators 8, each on his own spot), and it is said out loud: the nearest speaks
    // (LIGHT_SHOT_OUT, a warning line: it always plays); a guard within 10 m of one who speaks follows without a
    // line of his own, so there is one clear reaction per group, not two lines on top of each other.
    const uint32_t lamp_ev = EventIndex(base, "LIGHT_SHOT_OUT");
    std::vector<std::array<float, 3>> speakers;
    // Follow-up (Sun 15 Nov): pdz_stealth_lamp_escalation (off by default). A guard was killed while he looked at a
    // lamp within pdz_lab_stealth_tier_area_cm of this one in the last pdz_lab_stealth_tier_s: nobody walks up alone
    // to this lamp. They stay, say their lamp line and are alert (the "stays" branch below, as for no_look).
    std::string lamp_esc;
    if (REXCVAR_GET(pdz_stealth_lamp_escalation) && REXCVAR_GET(pdz_lab_stealth_tiers))
    {
      const float area = float(REXCVAR_GET(pdz_lab_stealth_tier_area_cm));
      const float keep = float(REXCVAR_GET(pdz_lab_stealth_tier_s));
      for (const AreaIncident &a : g_area)
      {
        if (a.lost && now >= a.at && now - a.at <= keep && Dist3(a.pos, pos) <= area)
        {
          lamp_esc = fmt::format("pdz_stealth_lamp_escalation: {} went to look at a {} {:.0f} cm from here {:.0f} s ago and was "
                                 "killed: nobody walks up alone: he stays, his line only, alert",
                                 a.name, a.kind, Dist3(a.pos, pos), now - a.at);
        }
      }
    }
    for (const Reactor &r : troopers)
    {
      Guard &g = g_guards[r.c];
      bool goes = false;
      std::string no;
      if (g.st == St::Investigating)
      {
        // already looking at a lamp or a sound near here: he carries on (one investigation, not a new one per lamp)
        ++going;
        went += fmt::format(" {} ({:.0f} cm, already looking: he carries on)", Id(base, r.c), r.d);
        continue;
      }
      if (going < want)
      {
        if (!lamp_esc.empty())
        {
          no = lamp_esc;
        }
        else if (NoLook(g.who))
        {
          no = "his row's no_look (his conversation is the objective, or Rare's WARNER): he stays, his line only";
        }
        else if (NoWalk(g.who))
        {
          no = "his row's no_walk (a tower sniper: he never leaves his tower): he stays, his line only";
        }
        else if (Load32(base, r.c + kConfig) & (kSearching | kNeverInvestigates))
        {
          no = "Rare's never-investigates flag or a search of Rare's";
        }
        else if (REXCVAR_GET(pdz_lab_stealth_investigation))
        {
          // (e) his spot under the lamp.
          uint32_t spot = 0;
          float best = 1e9f;
          for (const auto &[pad, hop] : PadsAround(base, Load32(base, r.c + kPad), 60, 1200))
          {
            float p[3];
            if (!PadPos(base, pad, p) || p[1] > lamp[1] + 30.0f || std::find(spots.begin(), spots.end(), pad) != spots.end())
            {
              continue;
            }
            const float dx = p[0] - lamp[0], dz = p[2] - lamp[2];
            const float flat = std::sqrt(dx * dx + dz * dz);
            const float score = flat + 0.5f * (lamp[1] - p[1]);
            if (flat > 700.0f || score >= best)
            {
              continue;
            }
            const float head[3] = {p[0], p[1] + 150.0f, p[2]};
            if (by_sight && !SightClear(ctx, base, lamp, head) && !LampSeen(ctx, base, view, head))
            {
              continue;
            }
            best = score;
            spot = pad;
          }
          if (!Ptr(spot) && !r.saw && REXCVAR_GET(pdz_stealth_lamp_heard_walk))
          {
            // Wed 18 Nov night (rule 5c): he HEARD it break and no spot with a view of it is in his reach (round a
            // corner, behind a door): the pad nearest the lamp on his pad graph, whatever it can see.
            float nd = 0.0f;
            const uint32_t near_pad = NearestPadTo(base, Load32(base, r.c + kPad), lamp, nd);
            if (Ptr(near_pad) && nd <= 900.0f && std::find(spots.begin(), spots.end(), near_pad) == spots.end())
            {
              spot = near_pad;
              REXLOG_INFO("pdz_stealth_r9: LAMP HEARD: {} heard a lamp break {:.0f} cm away and has no spot with a view of it in "
                          "his reach: he walks to the pad nearest it ({:08X}, {:.0f} cm from the lamp; "
                          "pdz_stealth_lamp_heard_walk)",
                          Id(base, r.c), r.d, near_pad, nd);
            }
          }
          if (!Ptr(spot))
          {
            no = "no spot under the lamp in his reach with a view of it";
          }
          else
          {
            g.investigator_lamp = true;
            g.squad = SquadOf(ctx, base, r.c);
            StartInvestigation(ctx, base, r.c, g, Inv::Lamp, pos, -1, now,
                               fmt::format("a lamp went out at ({:.0f} {:.0f} {:.0f}), {} lamp(s), he {} it from {:.0f} cm: "
                                           "he goes to look",
                                           pos[0], pos[1], pos[2], lamps, r.saw ? "saw" : "heard", r.d),
                               spot);
            goes = g.st == St::Investigating;
            no = goes ? "" : "no search could start";
            if (goes)
            {
              spots.push_back(spot);
            }
          }
        }
        else
        {
          // Round 3: one Rare SEARCH at the pad beside the lamp.
          float d = 0.0f;
          const uint32_t pad = NearestPadTo(base, Load32(base, r.c + kPad), pos, d);
          g.was_wary = g.st == St::Wary;
          g.need_line = false;
          if (Ptr(pad) && GiveSearch(ctx, base, r.c, pad))
          {
            g.search_pad = pad;
            goes = true;
            Set(base, r.c, g, St::Suspicious, now,
                fmt::format("a lamp went out at ({:.0f} {:.0f} {:.0f}): he goes to look (SEARCH at pad {:08X}; "
                            "pdz_lab_stealth_investigation off)",
                            pos[0], pos[1], pos[2], pad));
          }
        }
      }
      float rp[3];
      PosOf(base, r.c, rp);
      const bool speaks = lamp_ev && !std::any_of(speakers.begin(), speakers.end(), [&](const std::array<float, 3> &s) {
        const float q[3] = {s[0], s[1], s[2]};
        return Dist3(q, rp) <= 1000.0f;
      });
      if (speaks)
      {
        speakers.push_back({rp[0], rp[1], rp[2]});
      }
      if (goes)
      {
        if (going == 0)
        {
          g_incident.investigator = r.c;
        }
        g_dead_lamps[dead_i].checked = true; // (item 7: somebody looks under it: nobody checks it again)
        ++going;
        went += fmt::format(" {} ({:.0f} cm{})", Id(base, r.c), r.d, speaks ? ", says so" : ", follows");
      }
      else
      {
        Alert(ctx, base, r.c, g, now,
              fmt::format("{} a lamp go out at ({:.0f} {:.0f} {:.0f}) from {:.0f} cm{}", r.saw ? "saw" : "heard", pos[0], pos[1],
                          pos[2], r.d, no.empty() ? std::string() : "; does not go: " + no));
        stayed += fmt::format(" {} ({:.0f} cm{})", Id(base, r.c), r.d, speaks ? ", says so" : "");
      }
      if (speaks)
      {
        Warn(ctx, base, r.c, g, lamp_ev, now, r.saw ? "saw a lamp go out" : "heard a lamp go out");
      }
    }
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: lamp out at ({:.0f} {:.0f} {:.0f}) ({} lamp(s)): go to look:{}; stay, alert:{}; "
                  "scientists:{}",
                  pos[0], pos[1], pos[2], lamps, went.empty() ? std::string(" nobody") : went,
                  stayed.empty() ? std::string(" nobody") : stayed, sci.empty() ? std::string(" none") : sci);
    }
  }

  // Round 8: the staff's scripted lines (the scientists' lights-out conversation) keep to the same speech rules.
  bool SpeechFree(const uint8_t *base)
  {
    if (!LabActive(base))
    {
      return true;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    if (StoryPlaying(base, now))
    {
      return false;
    }
    const float gap = float(REXCVAR_GET(pdz_lab_stealth_voice_gap_s));
    if (now >= g_last_line_end - 30.0f && now < g_last_line_end + gap)
    {
      return false;
    }
    return !std::any_of(g_lines.begin(), g_lines.end(), [now](const Line &l) {
      return l.end > now || (!l.played && now >= l.start && now - l.start < 2.5f);
    });
  }
  bool CalmStaff(const uint8_t *base, uint32_t character)
  {
    if (!LabActive(base))
    {
      return true;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const Guard *g = Find(character);
    return !g || (!g->dead && !g->spotted && !g->seen_jo && !g->confirmed && (g->st == St::Unaware || g->st == St::Wary));
  }
  void SpeechBusy(const uint8_t *base, float seconds)
  {
    if (!LabActive(base))
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    Line l;
    l.start = now;
    l.end = now + seconds;
    l.played = true;
    g_lines.push_back(l);
    if (l.end > g_last_line_end || g_last_line_end > now + 30.0f)
    {
      g_last_line_end = l.end;
    }
  }

  // Round 4 (12 (e)): the s10 spawn message during the first alarm was dropped. Round 11: never (it always answers
  // false); the level events are still logged while the stealth code is on.
  bool HoldLevelMessage(const uint8_t *base, uint32_t message)
  {
    if (Ptr(message) && base[message + 1] == 7 && base[message + 2] == 10 && REXCVAR_GET(pdz_lab_guard_stealth) &&
        REXCVAR_GET(pdz_lab_stealth_loud) && lab_restorations::CurrentScript() != 0)
    {
      // Rare asks for a reinforcement wave: the alarm is on (the alarm byte is written a moment later in this frame).
      std::lock_guard<std::recursive_mutex> hold(g_lock);
      LabActive(base);
      GoLoud(base, "the alarm was raised (Rare's first reinforcement wave request)");
    }
    if (Ptr(message) && base[message + 1] == 8 && g_lab && Logging() && REXCVAR_GET(pdz_lab_guard_stealth))
    {
      // Diagnostics (round 4): the Laboratory's level events (type 8: event b2, argument b3), at most once a second each.
      static std::unordered_map<uint32_t, float> said;
      std::lock_guard<std::recursive_mutex> hold(g_lock);
      const float now = Clock(base);
      const uint32_t key = (uint32_t(base[message + 2]) << 8) | base[message + 3];
      const auto it = said.find(key);
      if (it == said.end() || now - it->second >= 1.0f || now < it->second)
      {
        said[key] = now;
        REXLOG_INFO("pdz_lab_stealth: level event {} (argument {}, trigger {:02X}){}", int(base[message + 2]),
                    int(base[message + 3]), int(base[message]), g_alarm_on ? " during the alarm" : "");
      }
    }
    if (!Ptr(message) || base[message + 1] != 7 || base[message + 2] != 10 || !REXCVAR_GET(pdz_lab_guard_stealth) ||
        !g_lab || lab_restorations::CurrentScript() == 0)
    {
      return false;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    const bool held = false; // round 11 (item 1): Rare's reinforcement waves are never held back
    uint32_t &n = held ? g_waves_held : g_waves_passed;
    ++n;
    if (Logging() && (n == 1 || now - g_waves_log >= 10.0f || now < g_waves_log))
    {
      g_waves_log = now;
      REXLOG_INFO("pdz_lab_stealth: alarm {}: Rare's s10 reinforcement spawn request {} ({} so far in this alarm){}",
                  g_alarm_on ? g_alarm_no : g_alarm_no + 1, // (raised this frame: the alarm tick has not counted it yet)
                  held ? "HELD BACK (the first alarm: the placed guards only)" : "let through", n,
                  held ? "" : ": Rare's own (never held back)");
    }
    return held;
  }
} // namespace lab_stealth

// Round 11g (item 4): Rare's level alarm ON (every route to the alarm ends here: the panel's level message, the
// scripts). Before Jo goes loud the alarm cannot be raised until pdz_lab_stealth_alarm_min_s after the runner's shout
// really started: a call that comes sooner is held and made again when the time is up, if the runner is still alive
// (the sight update's tick). The reinforcement wave request and the alarm byte both come from this call.
REX_HOOK_RAW(sub_8214EBD0)
{
  if ((g_lab || (g_loud && g_lab_run)) && REXCVAR_GET(pdz_lab_guard_stealth) &&
      (lab_restorations::CurrentScript() != 0 || (ZoneLevel() && g_level->alarm_announce)) &&
      Ptr(ctx.r3.u32)) // (round 11k: also after LOUD, for an alarm run announced then; rollout: zone levels' hold)
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    if (now < g_alarm_not_before && g_alarm_not_before - now < 30.0f)
    {
      if (!g_alarm_deferred)
      {
        REXLOG_INFO("pdz_lab_stealth: ALARM HELD: the alarm was switched on {:.1f} s before the {:.1f} s after {}'s shout are up: "
                    "it sounds then, if he is still alive",
                    g_alarm_not_before - now, float(REXCVAR_GET(pdz_lab_stealth_alarm_min_s)),
                    Ptr(g_alarm_runner) ? Id(base, g_alarm_runner) : std::string("the runner"));
      }
      g_alarm_deferred = ctx.r3.u32;
      return;
    }
  }
  const uint32_t alarm_manager = ctx.r3.u32, alarm_caller = uint32_t(ctx.lr);
  const uint32_t alarm_before = Ptr(alarm_manager) ? Load32(base, alarm_manager + 0x50) : 0;
  __imp__sub_8214EBD0(ctx, base);
  if (Ptr(alarm_manager) && REXCVAR_GET(pdz_lab_stealth_log) && lab_restorations::CurrentScript() != 0)
  {
    // Round 11l (item 1): the level alarm ON call and the manager's alarm sound it starts.
    const uint32_t sound = Load32(base, alarm_manager + 0x50);
    REXLOG_INFO("pdz_lab_stealth: ALARM ON call (sub_8214EBD0, caller {:08X}): alarm byte {}, the manager's alarm sound {:08X} "
                "-> {:08X}",
                alarm_caller, int(base[alarm_manager + 0x54]), alarm_before, sound);
    if (Ptr(sound) && AlarmCue(base, sound + 52))
    {
      AlarmSoundAdd(base, sound, nullptr, "the level alarm sound (manager +0x50)", alarm_caller);
    }
  }
}

// Rare's spawn by setup name: the s10 reinforcements that come in are logged (round 4, alarm tiers).
REX_HOOK_RAW(sub_82142AD8)
{
  const uint32_t name = ctx.r3.u32;
  std::string text;
  if (g_lab_run && Ptr(name) && Logging()) // (round 11: logged after Jo went loud too: Rare's waves come in)
  {
    for (int i = 0; i < 40 && base[name + i] >= 0x20 && base[name + i] < 0x7F; ++i)
    {
      text += char(base[name + i]);
    }
  }
  __imp__sub_82142AD8(ctx, base);
  if (!text.empty() && text.rfind("s10", 0) == 0)
  {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    ++g_waves_spawned;
    REXLOG_INFO("pdz_lab_stealth: Rare spawned reinforcement {} ({:08X}), {} so far{}", text, ctx.r3.u32, g_waves_spawned,
                g_loud ? " (Jo went loud: Rare's own waves)" : "");
  }
}

// Overnight Mon 16 / Tue 17 Nov (items 6b / 6c; lab_stealth_r6_Tue17Nov.inc). The real security cameras (class
// DP_SecurityCamera, vtable 820C9028; NOT 820BE9A8, which is the sentry gun). Both of their events reach the layer
// from camera_sounds_Tue17Nov.cpp, which hooks the camera's break routine sub_825BFCA0 (an event, not a per-frame
// routine):
//   - the explosion (the fifth hit, a blast): the destroyed listener, the LOUD event;
//   - a shot to the lens that puts the camera out: the lens-hit listener, the QUIET event.
// This file hooks nothing of the camera's. With the layer not running: one test per event, nothing else.
namespace pdz::camsound
{
  using Destroyed = void (*)(PPCContext &ctx, uint8_t *base, uint32_t prop, const float pos[3]);
  void SetDestroyedListener(Destroyed fn); // camera_sounds_Tue17Nov.cpp (the game thread, once per exploded camera)
  using LensHit = void (*)(PPCContext &ctx, uint8_t *base, uint32_t prop, const float pos[3]);
  void SetLensHitListener(LensHit fn); // (the game thread, once per camera put out by a shot to its lens)
} // namespace pdz::camsound

static void R6OnCameraDestroyed(PPCContext &ctx, uint8_t *base, uint32_t prop, const float pos[3])
{
  (void)ctx;
  if (!Ptr(prop) || !REXCVAR_GET(pdz_lab_guard_stealth) || !LabActive(base))
  {
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  R6CameraDown(base, prop, pos);
}

static void R6OnCameraLens(PPCContext &ctx, uint8_t *base, uint32_t prop, const float pos[3])
{
  (void)ctx;
  if (!Ptr(prop) || !REXCVAR_GET(pdz_lab_guard_stealth) || !LabActive(base))
  {
    return;
  }
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  R6CameraLens(base, prop, pos);
}

// Registered when the program starts (two pointer stores into constant-initialised atomics; the listeners themselves
// do nothing unless the layer runs).
static const bool g_r6_camera_listeners =
    (pdz::camsound::SetDestroyedListener(&R6OnCameraDestroyed), pdz::camsound::SetLensHitListener(&R6OnCameraLens), true);
namespace lab_stealth
{
  bool LampLureOn(const uint8_t *base)
  {
    return LabActive(base) && ZoneLevel() && g_level->lamps;
  }
  bool LampPropLureOn(const uint8_t *base)
  {
    return LampLureOn(base) && g_level->sw == LevelSwitch::kSubway;
  }
  // Round 5, Mon 16 Nov (fix 5, lab_stealth_r5_C_Mon16Nov.inc): prop bullet hits are looked at (the layer runs).
  bool PropHitsWatched(const uint8_t *base)
  {
    // (pdz_stealth_camera_noise off: no health read round Rare's handler at all, Rare's path as before round 5)
    return LabActive(base) && RarePlus() && REXCVAR_GET(pdz_stealth_camera_noise);
  }
  void PropBulletHit(const uint8_t *base, uint32_t prop, uint32_t vtable, const char *name, const char *model, const float pos[3],
                     float before, float after, bool gone)
  {
    if (!LabActive(base))
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    R5CPropHit(base, prop, vtable, name, model, pos, before, after, gone);
  }
  // (overnight Mon 16 / Tue 17 Nov) A prop's bounded text: its name (prop +136) and its model name (setup +84 -> +140).
  static void PropText(const uint8_t *base, uint32_t at, char out[96])
  {
    out[0] = 0;
    if (!Ptr(at) || at >= 0xC0000000u)
    {
      return;
    }
    const char *text = reinterpret_cast<const char *>(base + at);
    int i = 0;
    for (; i < 95 && text[i] >= 32 && text[i] < 127; ++i)
    {
      out[i] = text[i];
    }
    out[i] = 0;
  }
  bool IsCameraProp(const uint8_t *base, uint32_t prop)
  {
    if (!Ptr(prop) || prop >= 0xC0000000u)
    {
      return false;
    }
    char name[96], model[96];
    PropText(base, Load32(base, prop + 136), name);
    const uint32_t setup = Load32(base, prop + 84);
    PropText(base, Ptr(setup) && setup < 0xC0000000u ? Load32(base, setup + 140) : 0, model);
    return Load32(base, prop) == kR6CameraVtable || R6CameraWords(name, model); // the class, or the name / model words
  }
  // (g_lab_run is kept by LabActive, which every sight update calls: the answer is at most one update old.)
  bool CounterOpGameNow()
  {
    return g_counter_op_game.load(std::memory_order_relaxed);
  }
  bool LayerRuns()
  {
    return REXCVAR_GET(pdz_lab_guard_stealth) && g_lab_run;
  }
} // namespace lab_stealth
