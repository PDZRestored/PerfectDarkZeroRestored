// Counter-Operative round 2, the guard side: see counter_op_guards.h and research notes: round2\a\FINDINGS.md.
//
// How a single-player level gets its enemies (TU3; FINDINGS sections 1-3):
// - Level load: 82279E30 walks the level's setup records and 82279B90 builds each one through the character
//   factory 82522438 (after Rare's setup filter 82279928). A record whose name (setup +44, a string pointer) is not
//   "_noname_" is a SPAWN TEMPLATE: 82279B90 files {class, setup, ...} in the template table 82D6C728 (count
//   [82E2F498], at most 20) and deletes the object it just built. Every other record is a numbered original.
// - The level script's setup (vtable +8) puts groups of originals to sleep with SetDormant(1) (vtable +0x340 =
//   825818E0(c, 1): out of the world, perception and physics; +6272 bit 0x10) and its tick / message handler
//   wakes them (825818E0(c, 0)) when the agent enters trigger volumes. Subway: 64 of its 71 table ids (all but the
//   five bikers and 16-3) at 82361E8C; Nightclub 90 at 822AE18C; Trinity Infiltration 95 at 822C321C; Trinity
//   Escape 106 at 822D5B44.
// - Waves are clones: 82142AD8(name, pos, pad, vec) / 822771B0(spawner, class, name, pad) -> 82277158 -> template
//   lookup 82276FF0 -> 822772B8 -> 82522438 with the TEMPLATE's setup; the clone's setup (+84) is the template.
// - The cap: SP guards come from a pool of 150 blocks of 0x3300 bytes (821420F8 -> 82638A50(game +0x1C +0x2DC,
//   0x3300, 150)); 825801C8 refuses a new guard when the pool or the 150-node list at 82E454A0 is full, and
//   82142AD8 returns 0 (the wave guard is simply not made). Dormant guards keep their block; waking one allocates
//   nothing, so forcing the originals awake never reaches the cap.
//
// Datacore (no numbered guard; research notes: round2\d\FINDINGS.txt): every enemy comes from an "area
// spawner" member of the level script (script class vtable 820A1610, ctor 822F8D58). Spawner layout (base ctor
// 821536B8, kind A ctor 82303D18 / vtable 820A182C; kind B ctor 823046C8 / vtable 820A1854 = the spiderbots):
//   +4 slot count, +8 {name, class} template list, +0xC template count, +0x10 pad-number list (u32), +0x14 pad
//   count, +0x18 / +0x1C template / pad cursors, +0x20 byte "spawn even if a player sees the pad", +0x28 on-spawn
//   callback (a member delegate of the script), +0x2C slots (8 bytes each, +4 = the character, a weak handle),
//   +0x30 one byte per slot: 1 = in the world ("active"), 0 = stashed.
// Kind A vtable: [1] 82303EE0 Fill (an empty slot gets a clone at the next pad that is free and unseen: 82153C00,
// pad flags 0x90 or seen by any human = blocked; spawn 82305F60 -> 822771B0; marks the pad occupied, pad +0x34
// |= 0x10), [2] 82304048 activate every stashed slot no human sees, [3] 823040D8 stash every active one no human
// sees, [4] 82304188 delete, [5] 82304238 activate slot i, [6] 823043C8 stash slot i, [7] 823044F0 delete slot i.
// The script's FIRST tick (822FB04C-822FB1F4, once: flag word +0x1C bit 0x40000000 of [script +0x3E0]) fills
// every spawner but the alarm wave and the 10-slot spiderbot one, then stashes everything nobody sees. Entering an
// area volume (message type 7 -> 822F0E88) activates that area's spawners; leaving it (822F1090) stashes them. The
// guard spawners are never filled again (their only Fill calls are in that first tick), so each guard there is
// the first and only guard of its pad: the level's own originals. Forcing them = Rare's own activate [5] on each
// stashed slot (no spawn, no pool block: the guard already exists).
#include "counter_op_guards.h"
#include "counter_op_levels13.h" // round 13 i: Bridge's cloaked end waves count as takeable

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

#include "objective_bots.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop_force_originals, false, "Gameplay",
                    "Counter-Operative: once the opening is over, wake the level's original guards that its script "
                    "keeps dormant until the agent gets near (Rare's own wake, 825818E0), so player 2 can take any of "
                    "them from the start. Only missions whose wake-ups were checked (Subway); guards a script sends in "
                    "on cue (Subway's locked-door trio) still arrive on time; reinforcement waves are never touched. "
                    "Round 3: off by default. Woken far from the players, the guards ran as live AI from the start and "
                    "had left their areas (or died in the opening fight) by the time the players arrived, so areas "
                    "were empty; the level now wakes its guards on its own cues again, and player 2 can still take a "
                    "sleeping original (pdz_counterop_take_dormant), woken only if he dies as it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_force_all_missions, false, "Testing",
                    "TESTING TOOL (counter-op): apply pdz_counterop_force_originals to every mission, also those whose "
                    "wake-ups were not checked (Nightclub wakes some guards only on Agent, Trinity's not traced)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_opening_grace, 5.0, "Gameplay",
                      "Counter-Operative: seconds after the opening cutscene (or after the start without one) during "
                      "which clones a level script makes still count as its opening enemies (eligible); later clones "
                      "are reinforcement waves and are never taken")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(bool, pdz_counterop_datacore_first_half); // defined at the end of this file (round 5)
bool PdzCounterOpRooftopsDropGone(const uint8_t *base, uint32_t character); // round 13 e, counter_op_rooftops.cpp: a guard Rare has deleted
int PdzCounterOpRooftopsDropVerdict(uint32_t character); // round 13 e: -1 not a guard Killian's ship dropped, 0 not yet, 1 may be taken
REXCVAR_DEFINE_BOOL(pdz_counterop_force_spawner_firsts, false, "Gameplay",
                    "Counter-Operative on Datacore (no numbered guards): once the opening is over, put each guard "
                    "the level made at its area spawn spot into the world (Rare's own spawner activate, 82304238), "
                    "out of every player's view, so player 2 can take any of them from the start. Refills and the "
                    "alarm wave still come only as the level sends them and are never taken. Round 3: off by default "
                    "(the guards it places, the IndoorGuard01s, are the agent's friendlies and are never taken)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_take_dormant, true, "Gameplay",
                    "Counter-Operative (round 3, default on): player 2 may take an original guard the level still keeps "
                    "asleep (he appears at its post; the guard stays hidden and dies through Rare's kill when he does). "
                    "The first spawn still takes the opening enemies first, and respawns the guards near the agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_dormant_next_only, true, "Gameplay",
                    "Counter-Operative (default on, round 4): a sleeping original guard is taken only when it belongs "
                    "to the area the agent is heading into next (the nearest wake trigger not yet fired, within "
                    "pdz_counterop_next_area_m of her), and only after the awake guards near her; never a sleeping "
                    "guard far ahead. Off = round 3 (any sleeping original)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_force_squads, false, "Testing",
                    "TESTING TOOL (counter-op): on Datacore also bring in the six Guard01-04 squads of the last area "
                    "at the start. Rare gives them the COMBAT script and the agent (or his partner) as target when "
                    "they are made (822F26A8), so once in the world they hunt him across the level")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(pdz_counterop_area_relax_s, 10.0, "Gameplay",
                      "Counter-Operative (round 12): when no guard has fitted the area rule for this many seconds "
                      "after player 2 lost his body, the rule is relaxed: first the nearest guard of the nearest area "
                      "ahead of the agent, and after twice this time any eligible original, always out of her view "
                      "(never a sleeping guard the script wakes on a cue; a guard taken this way wakes nobody else). "
                      "0 = off: he waits, as in round 11")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_riders, true, "Gameplay",
                    "Counter-Operative on Datacore (round 12): the platform's jetpack guards (the five Guard01-04 "
                    "clones of the script's spawner +0x7AC) count as originals and may be taken like any other enemy "
                    "of the agent while they are on foot; when his own jetpack is still free player 2 is seated on it "
                    "through Rare's mount and flies it with the player's jetpack controls. One already flying stays "
                    "Rare's. Off = they are never taken (round 7 to 11)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_scene_release, true, "Gameplay",
                    "Counter-Operative (round 16, default on): a guard the level script has just woken is not player "
                    "2's body for pdz_counterop_scene_release_s while he is still calm (his group's scripted scene may "
                    "be playing); once he is alerted or fighting, or the time is up, he may be taken. Off = taken at once")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_scene_release_s, 15.0, "Gameplay",
                      "Counter-Operative: with pdz_counterop_scene_release, seconds after the level's own wake during "
                      "which a calm guard is left to his scene")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Fri 20 Nov night (play log 031, Trinity Escape: 's01c' 1.4 m from the agent at 22:01:24, 2.5 m in her view at
// 21:52:54, 's01a' 4.4 m in the middle of her view at 21:52:50): the pick's later steps have no distance floor.
REXCVAR_DEFINE_BOOL(pdz_counterop_spawn_keep_away, true, "Gameplay",
                    "Counter-Operative (Fri 20 Nov night, default on): player 2 is never given a body closer to the "
                    "agent than pdz_counterop_spawn_min_m, nor one she can see near the middle of her view "
                    "(pdz_counterop_spawn_view_deg) within pdz_counterop_spawn_view_m, whatever the pick's step. Only "
                    "when nothing else is left, and after pdz_counterop_spawn_near_wait_s, is such a body taken "
                    "(logged). Off = the pick's own steps (a body may be next to her)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spawn_min_m, 8.0, "Gameplay",
                      "Counter-Operative, pdz_counterop_spawn_keep_away: no body within this many metres of the agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spawn_view_m, 20.0, "Gameplay",
                      "Counter-Operative, pdz_counterop_spawn_keep_away: no body she can see near the middle of her "
                      "view within this many metres")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spawn_view_deg, 30.0, "Gameplay",
                      "Counter-Operative, pdz_counterop_spawn_keep_away: 'the middle of her view' = this many degrees "
                      "or less off her view direction")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_spawn_near_wait_s, 4.0, "Gameplay",
                      "Counter-Operative, pdz_counterop_spawn_keep_away: when only bodies near the agent are left, "
                      "player 2 waits this many seconds before one of them is taken")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_825818E0); // SetDormant (character, 1 sleep / 0 wake): guard vtable +0x340
REX_EXTERN(sub_82304238); // area spawner (vtable 820A182C [5]): activate slot (spawner, slot) -> 1
REX_EXTERN(sub_82153AB8); // does any human see this point? (&point) -> byte, 1 = seen (spawners' own test)
REX_EXTERN(sub_82153890); // in the viewer's view cone? (&point, viewer character) -> byte
REX_EXTERN(sub_82259B80); // physics ray (mask, &from, &to, &visitor, 1) -> byte: hit (hooked in cut_scenarios.cpp)

// counter_op_quick_Sat21Nov.cpp (Sat 21 Nov)
bool PdzCounterOpTakeSpawned();
bool PdzCounterOpPickLockedOut(const uint8_t *base, uint32_t guard, float x, float z, std::string &why); // counter_op.cpp (round 18, item 4)
bool PdzCounterOpPickPreferNext(); // counter_op.cpp (round 18, item 4)
bool PdzCounterOpRelaxAwakeOnly();
bool PdzCounterOpUntabledByPosition();
float PdzCounterOpUntabledReachCm(int relax);
namespace counter_op_guards
{
  namespace
  {
    constexpr uint32_t kGame = 0x82D6BFD8;
    constexpr uint32_t kRegistry = 28, kRegistryBase = 20, kGroupFirst = 676, kGroupEnd = 680, kGroupNext = 36;
    constexpr uint32_t kGroupListFirst = 4 + 16, kGroupListEnd = 4 + 20, kNodeObject = 0, kNodeNext = 8;
    constexpr uint32_t kGuardVtable = 0x820C45A0;
    constexpr uint32_t kPosition = 48, kSetup = 84, kHealth = 120, kHidden = 6272;
    constexpr uint32_t kSetupName = 44, kSetupId = 52;
    constexpr uint32_t kNoName = 0x82090914; // "_noname_" (82279B90 compares the setup name with it)
    constexpr uint32_t kEyeHeight = 0x1678;  // 82153890 / 82153AB8: eye = position + (0, this, 0)
    constexpr uint32_t kAnyHitVisitor = 0x820906BC;
    constexpr uint32_t kSightMask = 0x28;    // 82153AB8's ray mask
    constexpr float kSpotHeight = 160.0f;    // 82153C00: Rare's spawners test a point 160 cm above the pad
    // The SP guard pool (821420F8): game +0x1C -> +0x2DC, +4 free list head, +0x10 blocks in use; 150 blocks.
    constexpr uint32_t kGuardPool = 0x1C, kGuardPoolOff = 0x2DC, kGuardPoolUsed = 0x10;
    constexpr uint32_t kGuardPoolBlocks = 150;
    constexpr uint32_t kForcePerFrame = 4;
    constexpr double kScanSeconds = 0.5, kSettleNoCutsceneSeconds = 3.0;
    constexpr float kRidingSpeed = 600.0f; // cm/s between two scans: faster = riding / falling, never taken

    // Missions whose wake-ups were traced (FINDINGS section 3) and the ids their scripts bring in on cue (woken and
    // then ordered somewhere, or cutscene actors), which are never forced.
    struct ForceRule
    {
      const char *mission;
      const char *on_time; // comma list
    };
    constexpr ForceRule kForce[] = {
        // 823639FC: s20a-c woken by the tick and set on the agent (8252D8D8); jack / zieg: intro cutscene actors.
        {"subway", "s20a,s20b,s20c,jack,zieg"},
        // Lead (10 Nov, the design's item 3: force the originals on the test levels too; their wake-ups are not all
        // traced, so the ids known to come on cue or by difficulty stay Rare's): 822B64C0..822B6524: s06a woken by an
        // event, s03a only on Agent (difficulty byte 82E4C0C7 == 0); 822B646C: s04a put back to sleep on Secret
        // Agent and up.
        {"nightclub", "s03a,s04a,s06a,rin1,rin2,rin3,r2bu,chf1"},
        {"trinity1", "clay,virg,drca,radr,rada,radd"},
        {"trinity2", "s30a,s30b,s30p"},
    };
    // Missions not traced: forced only with pdz_counterop_force_all_missions.
    constexpr ForceRule kForceUntraced[] = {
        {"", ""},
    };

    // Datacore's area spawners (members of its level script; see the comment at the top of the file).
    // The script: [[[0x82D6BFD8 +0x1C] +0x2D8] +0x74] (82277988 stores the level's message target L at +0x2D8,
    // 8214DAC0 puts the script made by the factory 8208AB50 at L +0x74), checked by its vtable.
    constexpr uint32_t kLevelTarget = 0x2D8, kLevelTargetScript = 0x74;
    constexpr uint32_t kDatacoreScriptVtable = 0x820A1610;
    constexpr uint32_t kAreaSpawnerVtable = 0x820A182C; // kind A (Datacore's guards, scientists; Arena's Zhang Li)
    constexpr uint32_t kScriptFlags = 0x3E0;             // -> the script's flag words
    constexpr uint32_t kFirstTickWord = 0x1C, kFirstTickBit = 0x40000000u; // set by the first tick (822FB01C)
    constexpr uint32_t kSpawnerSlotCount = 4, kSpawnerSlots = 0x2C, kSpawnerActive = 0x30, kSlotCharacter = 4;
    // Rare's two 150-node character lists (82156EF0 adds an activated character to both; 82157008 does not check
    // for room, so we keep headroom for the level's own spawns / activations; +0x718 = nodes in use).
    constexpr uint32_t kCharListA = 0x82E454A0, kCharListB = 0x82E44D80, kCharListUsed = 0x718;
    constexpr uint32_t kCharListNodes = 150, kCharListHeadroom = 24;

    enum class SpawnerGroup : uint8_t
    {
      kAreaGuards, // IndoorGuard01: placed at their pads, idle until they see the agent (on-spawn 822F2610)
      kSquads,     // Guard01-04: COMBAT script + the agent as target at spawn (822F26A8): hunters, kept on time
      kWave,       // the alarm wave: filled again and again by the tick (82300758 / 82300B30): reinforcements
      kOther,      // scientists (L02S / Al01 denied): never forced
    };
    struct AreaSpawner
    {
      uint32_t offset; // in the script
      const char *what;
      SpawnerGroup group;
    };
    // Script ctor 822F8D58 (822F8E48-822F8FBC); areas from 822F0E88 (enter) / 822F1090 (leave).
    constexpr AreaSpawner kDatacoreSpawners[] = {
        {0x570, "TankScientist / Scientist01-03, 4 slots at pads 4253,387,409,1403 (area 0)", SpawnerGroup::kOther},
        {0x5A4, "IndoorGuard01, 1 slot at pad 3830 (area 2)", SpawnerGroup::kAreaGuards},
        {0x5D8, "IndoorGuard01, 7 slots, pads 109,192,82,193,83,204,86,194 (area 3)", SpawnerGroup::kAreaGuards},
        {0x60C, "IndoorGuard01, 8 slots, the same 8 pads (area 4)", SpawnerGroup::kAreaGuards},
        {0x640, "IndoorGuard01, 7 slots, the same 8 pads (area 5)", SpawnerGroup::kAreaGuards},
        {0x674, "Guard01-04 squad, 2 slots, 20 shared pads (area 7)", SpawnerGroup::kSquads},
        {0x6A8, "Guard01-04 squad, 2 slots, 20 shared pads (area 7)", SpawnerGroup::kSquads},
        {0x6DC, "Guard01-04 squad, 3 slots, 20 shared pads (area 7)", SpawnerGroup::kSquads},
        {0x710, "Guard01-04 squad, 4 slots, 20 shared pads (area 7)", SpawnerGroup::kSquads},
        {0x744, "Guard01-04 squad, 2 slots, 20 shared pads (area 7)", SpawnerGroup::kSquads},
        {0x778, "Guard01-04 squad, 3 slots, 20 shared pads (area 7)", SpawnerGroup::kSquads},
        {0x7AC, "Guard01-04 alarm wave, 5 slots, 20 shared pads", SpawnerGroup::kWave},
    };

    struct SpawnerSlot
    {
      uint32_t spawner = 0; // guest address
      uint32_t slot = 0;
      SpawnerGroup group = SpawnerGroup::kOther;
    };

    struct Known
    {
      Origin origin = Origin::kNotGuard;
      bool eligible = false;
      bool was_hidden = false;
      bool on_time = false; // round 3: an id the level brings in on cue or by difficulty: never taken while dormant
      float last[3] = {};
      std::chrono::steady_clock::time_point last_at{};
      float speed = 0.0f; // cm/s between the last two scans
      // Round 16 (item 6b): when the level script woke him (not us), for pdz_counterop_scene_release.
      std::chrono::steady_clock::time_point woken_at{};
      bool woken_by_level = false, scene_logged = false;
    };

    struct State
    {
      bool started = false, settled = false, forced = false, saw_cutscene = false, cutscene_over = false;
      std::unordered_set<uint32_t> extra_elites; // round 6: Datacore guards made for player 2 (DatacoreExtraElite)
      std::string mission;
      std::function<bool(uint32_t)> eligible;
      std::unordered_map<uint32_t, Known> known;
      std::unordered_set<uint32_t> on_time;
      std::chrono::steady_clock::time_point start_at{}, settle_at{}, next_scan{};
      uint32_t scratch = 0; // guest: 0 eye, 16 point, 32 visitor
      uint32_t woken_by_script = 0, reinforcements = 0, opening_clones = 0;
      // A clone-only mission (no numbered original at all: Datacore, Arena): its area spawners place the level's
      // guards as the agent goes on. The first clone at a spot is that spot's guard (an original); a later clone at a
      // spot that already had one is a refill (a reinforcement, never taken).
      bool clone_mission = false;
      std::vector<uint32_t> force_queue; // dormant originals still to wake (ForceStep)
      uint32_t force_woken = 0, force_failed = 0;
      std::string force_ids;
      std::vector<std::array<float, 3>> clone_spots;
      // Datacore's area spawners: every character in one of their slots, by character (refreshed each scan).
      uint32_t spawner_script = 0;
      bool spawner_force = false, spawner_first_tick_logged = false, spawner_done_logged = false;
      std::unordered_map<uint32_t, SpawnerSlot> spawner_of;
      std::unordered_map<uint32_t, SpawnerSlot> spawner_home; // every spawner guard ever seen in a slot (StillExists)
      uint32_t spawner_placed = 0, spawner_waiting_seen = 0, spawner_waiting_room = 0;
      std::chrono::steady_clock::time_point next_spawner_step{};
      std::function<bool(uint32_t)> taken; // counter_op.cpp's g.taken (SetTakenTest)
    } g;

    std::mt19937 g_rng{0xC0DE0Au};
    double g_pick_wait_s = 0.0;  // round 12: how long player 2 has been without a body (SetPickWait)
    int g_last_pick_relaxed = 0; // round 12: the relax step of the last successful Pick (0 = the area rule held)
    // Fri 20 Nov night (pdz_counterop_spawn_keep_away): since when only bodies near the agent have been left (0 = not).
    std::chrono::steady_clock::time_point g_near_since{};

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

    uint32_t IdKey(const char *s)
    {
      uint32_t key = 0;
      for (int i = 0; i < 4 && s[i]; ++i)
      {
        key |= uint32_t(uint8_t(s[i])) << (24 - 8 * i);
      }
      return key;
    }
    // The setup id as counter_op.cpp packs it (first character in the top byte); 0 = none.
    uint32_t SetupIdKey(const uint8_t *base, uint32_t character)
    {
      // (Sat 21 Nov evening: a known guard's memory can be stale: every step tested as mapped memory)
      const uint32_t setup = pdz::guestmem::Ok(base, character, kSetup + 4) ? Load32(base, character + kSetup) : 0;
      const uint32_t p = pdz::guestmem::Ok(base, setup, kSetupId + 4) ? Load32(base, setup + kSetupId) : 0;
      if (!pdz::guestmem::Ok(base, p, 4))
      {
        return 0;
      }
      char s[5] = {};
      for (int i = 0; i < 4; ++i)
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
        s[i] = char(ch);
      }
      return IdKey(s);
    }
    std::string IdText(uint32_t key)
    {
      std::string out;
      for (int i = 24; i >= 0 && ((key >> i) & 0xFF); i -= 8)
      {
        out += char((key >> i) & 0xFF);
      }
      return out.empty() ? std::string("?") : out;
    }
    void ParseList(const char *list, std::unordered_set<uint32_t> &out)
    {
      std::string s = list;
      size_t at = 0;
      while (at < s.size())
      {
        const size_t comma = s.find(',', at);
        const std::string id = s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!id.empty())
        {
          out.insert(IdKey(id.c_str()));
        }
        if (comma == std::string::npos)
        {
          break;
        }
        at = comma + 1;
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
    bool Hidden(const uint8_t *base, uint32_t c) { return (Load32(base, c + kHidden) & 0x10) != 0; }

    // Every SP guard in Rare's object registry (hidden ones included; counter_op.cpp's CollectObjects walk).
    void Guards(const uint8_t *base, std::vector<uint32_t> &out)
    {
      std::unordered_set<uint32_t> seen;
      // Rare's character list (0x82E454A0 +0x710 .. +0x714, node +0 character, +8 next): every guard the level built,
      // dormant ones included (the registry walk below gives nothing in TU3: round 2's first Subway run).
      {
        constexpr uint32_t kCharList = 0x82E454A0;
        uint32_t node = Load32(base, kCharList + 0x710);
        const uint32_t end = Load32(base, kCharList + 0x714);
        for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
        {
          const uint32_t o = Load32(base, node);
          node = Load32(base, node + 8);
          if (Ptr(o) && Load32(base, o) == kGuardVtable && seen.insert(o).second)
          {
            out.push_back(o);
          }
        }
      }
      const uint32_t so = Load32(base, kGame);
      const uint32_t registry = Ptr(so) ? Load32(base, so + kRegistry) : 0;
      if (!Ptr(registry))
      {
        return;
      }
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
          if (Ptr(o) && Load32(base, o) == kGuardVtable && seen.insert(o).second)
          {
            out.push_back(o);
          }
        }
      }
    }

    std::string PoolText(const uint8_t *base)
    {
      const uint32_t owner = Load32(base, kGame + kGuardPool);
      if (!Ptr(owner))
      {
        return "guard pool unknown";
      }
      return fmt::format("guard pool {} of {} blocks in use", Load32(base, owner + kGuardPoolOff + kGuardPoolUsed),
                         kGuardPoolBlocks);
    }

    double Since(std::chrono::steady_clock::time_point t)
    {
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    }

    bool DatacoreAreaInReach(const uint8_t *base, uint32_t spawner_offset); // round 5, at the end of the file

    // Datacore's level script, or 0 in any other level.
    uint32_t DatacoreScript(const uint8_t *base)
    {
      const uint32_t level = Load32(base, kGame + kGuardPool); // [0x82D6BFF4]: the same owner as the guard pool
      const uint32_t target = pdz::guestmem::Ok(base, level, kLevelTarget + 4) ? Load32(base, level + kLevelTarget) : 0;
      const uint32_t script = pdz::guestmem::Ok(base, target, kLevelTargetScript + 4) ? Load32(base, target + kLevelTargetScript) : 0;
      return pdz::guestmem::Ok(base, script, 4) && Load32(base, script) == kDatacoreScriptVtable ? script : 0;
    }

    bool FirstTickDone(const uint8_t *base, uint32_t script)
    {
      const uint32_t flags = Load32(base, script + kScriptFlags);
      return Ptr(flags) && (Load32(base, flags + kFirstTickWord) & kFirstTickBit) != 0;
    }

    // Every character in a slot of Datacore's spawners (stashed or active), by character.
    void MapSpawners(const uint8_t *base)
    {
      g.spawner_of.clear();
      const uint32_t script = g.spawner_script;
      if (!script)
      {
        return;
      }
      for (const AreaSpawner &s : kDatacoreSpawners)
      {
        const uint32_t sp = script + s.offset;
        if (Load32(base, sp) != kAreaSpawnerVtable)
        {
          continue;
        }
        const uint32_t count = Load32(base, sp + kSpawnerSlotCount), slots = Load32(base, sp + kSpawnerSlots);
        for (uint32_t i = 0; Ptr(slots) && i < count && i < 64; ++i)
        {
          const uint32_t c = Load32(base, slots + 8 * i + kSlotCharacter);
          if (Ptr(c))
          {
            g.spawner_of[c] = SpawnerSlot{sp, i, s.group};
            g.spawner_home[c] = SpawnerSlot{sp, i, s.group};
          }
        }
      }
    }

    // Rare's own "can any human see this point" (82153AB8: every human's view cone + a clear ray), as the spawners
    // ask it before activating a stashed guard (82304098, the guard's position +0x30).
    bool AnyHumanSees(PPCContext &ctx, uint8_t *base, uint32_t c)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
      frame.ctx.r3.u64 = c + kPosition;
      sub_82153AB8(frame.ctx, base);
      return (frame.ctx.r3.u32 & 0xFF) != 0;
    }

    bool CharListRoom(const uint8_t *base)
    {
      const uint32_t limit = kCharListNodes - kCharListHeadroom;
      return Load32(base, kCharListA + kCharListUsed) < limit && Load32(base, kCharListB + kCharListUsed) < limit;
    }

    const char *GroupName(SpawnerGroup group)
    {
      switch (group)
      {
      case SpawnerGroup::kAreaGuards:
        return "area guards";
      case SpawnerGroup::kSquads:
        return "last-area squads";
      case SpawnerGroup::kWave:
        return "alarm wave";
      default:
        return "scientists";
      }
    }

    bool ForcedGroup(SpawnerGroup group)
    {
      // Round 6: the first-half IndoorGuards (the agent's friendlies) are only placed ahead of her with round 5's rule.
      return (group == SpawnerGroup::kAreaGuards && (REXCVAR_GET(pdz_counterop_datacore_first_half) ||
                                                     REXCVAR_GET(pdz_counterop_force_spawner_firsts))) ||
             (group == SpawnerGroup::kSquads && REXCVAR_GET(pdz_counterop_datacore_force_squads));
    }

    void Scan(PPCContext &ctx, uint8_t *base)
    {
      std::vector<uint32_t> guards;
      Guards(base, guards);
      if (!g.spawner_script)
      {
        g.spawner_script = DatacoreScript(base);
      }
      MapSpawners(base);
      const auto now = std::chrono::steady_clock::now();
      for (const uint32_t c : guards)
      {
        auto it = g.known.find(c);
        const bool hidden = Hidden(base, c);
        const float pos[3] = {LoadFloat(base, c + kPosition), LoadFloat(base, c + kPosition + 4),
                              LoadFloat(base, c + kPosition + 8)};
        if (it == g.known.end())
        {
          Known k;
          k.eligible = g.eligible && g.eligible(c);
          k.was_hidden = hidden;
          k.on_time = g.on_time.count(SetupIdKey(base, c)) != 0;
          const bool clone = IsTemplateClone(base, c);
          const auto spawner = g.spawner_of.find(c);
          if (!k.eligible)
          {
            k.origin = Origin::kNotGuard;
          }
          else if (spawner != g.spawner_of.end())
          {
            // Datacore: a guard of an area spawner. The area guards and the squads are made once, by the script's
            // first tick, one per pad: the level's own originals (whether the level or we put them in the world).
            // The alarm wave is filled again and again: reinforcements.
            // Round 12 (item 6): the "alarm wave" spawner +0x7AC is the platform's five jetpack guards (round 7).
            // They were reinforcements, so never counted and never taken (log 013, 23:00:33: "6 the agent's
            // friendlies" = jack and the five of them); with pdz_counterop_datacore_riders they are originals.
            const bool wave = spawner->second.group == SpawnerGroup::kWave && !REXCVAR_GET(pdz_counterop_datacore_riders);
            k.origin = wave ? Origin::kReinforcement : Origin::kOpeningClone;
            ++(wave ? g.reinforcements : g.opening_clones);
            REXLOG_INFO("pdz_counterop: guards: spawner guard {:08X} (script +0x{:X} slot {}, {}) in the world at "
                        "({:.0f}, {:.0f}, {:.0f}), team / allies / enemies masks {:04X} / {:04X} / {:04X}: {} ({:.1f} s "
                        "after the start; {})",
                        c, spawner->second.spawner - g.spawner_script, spawner->second.slot,
                        GroupName(spawner->second.group), pos[0], pos[1], pos[2], Load32(base, c + 128) >> 16,
                        Load32(base, c + 128) & 0xFFFF, Load32(base, c + 132) >> 16,
                        wave ? "a reinforcement (never taken)" : "an original of its spot", Since(g.start_at),
                        PoolText(base));
          }
          else if (clone)
          {
            // Clones the opening script makes (before the opening is settled, or within the grace after it) are
            // opening enemies; later ones are reinforcement waves.
            bool opening = !g.settled || Since(g.settle_at) < REXCVAR_GET(pdz_counterop_opening_grace);
            if (!opening && g.clone_mission)
            {
              bool seen_spot = false;
              for (const auto &sp : g.clone_spots)
              {
                const float dx = sp[0] - pos[0], dy = sp[1] - pos[1], dz = sp[2] - pos[2];
                seen_spot = seen_spot || dx * dx + dy * dy + dz * dz < 300.0f * 300.0f;
              }
              opening = !seen_spot; // the spot's first guard: one of the level's own
            }
            if (g.clone_mission)
            {
              g.clone_spots.push_back({pos[0], pos[1], pos[2]});
            }
            k.origin = opening ? Origin::kOpeningClone : Origin::kReinforcement;
            ++(opening ? g.opening_clones : g.reinforcements);
            REXLOG_INFO("pdz_counterop: guards: clone '{}' {:08X} (masks {:04X} / {:04X} / {:04X}) {} ({:.1f} s after "
                        "the start; {})",
                        IdText(SetupIdKey(base, c)), c, Load32(base, c + 128) >> 16, Load32(base, c + 128) & 0xFFFF,
                        Load32(base, c + 132) >> 16,
                        opening ? "counts as an opening enemy"
                        : PdzCounterOpTakeSpawned() && g.mission != "datacore" ? "is a reinforcement (a spawned copy: may be taken, pdz_counterop_take_spawned)"
                                                                               : "is a reinforcement (never taken)",
                        Since(g.start_at), PoolText(base));
          }
          else
          {
            k.origin = hidden ? Origin::kDormantOriginal : Origin::kOpeningLive;
          }
          std::memcpy(k.last, pos, sizeof(pos));
          k.last_at = now;
          it = g.known.emplace(c, k).first;
          continue;
        }
        Known &k = it->second;
        if (k.origin == Origin::kDormantOriginal && !hidden)
        {
          k.origin = Origin::kWokenByScript;
          ++g.woken_by_script;
          // Round 16 (item 6b): log 027 read our own wake of the Subway platform police (counter_op_wake, when player 2
          // took 'zsb2') as the level script's; and a replaced guard restored for Rare's kill shows up here too.
          const bool ours = counter_op_wake::WokeIt(c), restored = g.taken && g.taken(c);
          k.woken_at = now;
          k.woken_by_level = !ours && !restored;
          REXLOG_INFO("pdz_counterop: guards: '{}' {:08X} woken by {} at ({:.0f}, {:.0f}, {:.0f}), {:.1f} s after the "
                      "start",
                      IdText(SetupIdKey(base, c)), c,
                      restored ? "Counter-Op (player 2's replaced guard, restored for Rare's kill)"
                      : ours   ? "Counter-Op (counter_op_wake: player 2 took a guard of his area), not the level script"
                               : "the level script",
                      pos[0], pos[1], pos[2], Since(g.start_at));
        }
        const double dt = std::chrono::duration<double>(now - k.last_at).count();
        if (dt > 0.05)
        {
          const float dx = pos[0] - k.last[0], dy = pos[1] - k.last[1], dz = pos[2] - k.last[2];
          k.speed = hidden || k.was_hidden != hidden ? 0.0f : float(std::sqrt(dx * dx + dy * dy + dz * dz) / dt);
          std::memcpy(k.last, pos, sizeof(pos));
          k.last_at = now;
        }
        k.was_hidden = hidden;
      }
    }

    const ForceRule *FindRule(const ForceRule *rules, size_t n, const std::string &mission)
    {
      for (size_t i = 0; i < n; ++i)
      {
        if (mission == rules[i].mission)
        {
          return &rules[i];
        }
      }
      return nullptr;
    }

    void SpawnSpawnerFirsts(PPCContext &ctx, uint8_t *base); // below (Datacore)

    // Wake the dormant originals (Rare's own wake, as its scripts call it: vtable +0x340 with 0). A later wake by
    // the script is a no-op on an awake guard (825818E0 acts only when the dormant bit differs).
    void ForceOriginals(PPCContext &ctx, uint8_t *base)
    {
      g.forced = true;
      if (!REXCVAR_GET(pdz_counterop_force_originals))
      {
        if (DatacoreScript(base) || (g.clone_mission && REXCVAR_GET(pdz_counterop_datacore_first_half)))
        {
          SpawnSpawnerFirsts(ctx, base); // round 5 (item 4): Datacore's first-half guards, by the agent's area
          return;
        }
        REXLOG_INFO("pdz_counterop: guards: forcing off (pdz_counterop_force_originals)");
        return;
      }
      if (g.clone_mission)
      {
        SpawnSpawnerFirsts(ctx, base); // Datacore: the area spawners' guards (no dormant originals to wake)
        return;
      }
      const ForceRule *rule = FindRule(kForce, std::size(kForce), g.mission);
      if (!rule && REXCVAR_GET(pdz_counterop_force_all_missions))
      {
        rule = FindRule(kForceUntraced, std::size(kForceUntraced), g.mission);
      }
      if (!rule && !REXCVAR_GET(pdz_counterop_force_all_missions))
      {
        REXLOG_INFO("pdz_counterop: guards: {}: dormant originals are not forced (its wake-ups were not checked); "
                    "they become takeable as the level wakes them",
                    g.mission.empty() ? "this level" : g.mission);
        return;
      }
      g.on_time.clear();
      if (rule)
      {
        ParseList(rule->on_time, g.on_time);
      }
      g.force_queue.clear();
      g.force_woken = g.force_failed = 0;
      g.force_ids.clear();
      uint32_t kept = 0;
      std::string kept_ids;
      for (auto &[c, k] : g.known)
      {
        if (k.origin != Origin::kDormantOriginal || !pdz::guestmem::Ok(base, c, kHidden + 4) || // Sun 22 Nov night (audit)
            Load32(base, c) != kGuardVtable || !Alive(ctx, base, c) || !Hidden(base, c))
        {
          continue;
        }
        const uint32_t id = SetupIdKey(base, c);
        if (g.on_time.count(id))
        {
          ++kept;
          kept_ids += (kept_ids.empty() ? "" : ",") + IdText(id);
          continue;
        }
        g.force_queue.push_back(c);
      }
      REXLOG_INFO("pdz_counterop: guards: {}: {} dormant original(s) to wake at the start, {} a frame; {} left to "
                  "arrive on cue ({}); {}",
                  g.mission.empty() ? "this level" : g.mission, g.force_queue.size(), kForcePerFrame, kept,
                  kept_ids.empty() ? "-" : kept_ids, PoolText(base));
    }

    // The forced wake, a few guards a frame (all at once cost one ~600 ms frame on Nightclub / Trinity Escape).
    void ForceStep(PPCContext &ctx, uint8_t *base)
    {
      if (g.force_queue.empty())
      {
        return;
      }
      for (uint32_t n = 0; n < kForcePerFrame && !g.force_queue.empty(); ++n)
      {
        const uint32_t c = g.force_queue.back();
        g.force_queue.pop_back();
        auto it = g.known.find(c);
        if (it == g.known.end() || it->second.origin != Origin::kDormantOriginal ||
            !pdz::guestmem::Ok(base, c, kHidden + 4) || Load32(base, c) != kGuardVtable || // Sun 22 Nov night (audit)
            !Alive(ctx, base, c) || !Hidden(base, c))
        {
          continue;
        }
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = c;
          frame.ctx.r4.u64 = 0;
          sub_825818E0(frame.ctx, base);
        }
        if (Hidden(base, c))
        {
          ++g.force_failed;
          continue;
        }
        it->second.origin = Origin::kForced;
        it->second.was_hidden = false;
        ++g.force_woken;
        g.force_ids += (g.force_ids.empty() ? "" : ",") + IdText(SetupIdKey(base, c));
      }
      if (g.force_queue.empty())
      {
        REXLOG_INFO("pdz_counterop: guards: {}: {} dormant original(s) woken at the start ({}){}; {}",
                    g.mission.empty() ? "this level" : g.mission, g.force_woken,
                    g.force_ids.empty() ? "-" : g.force_ids,
                    g.force_failed ? fmt::format(", {} did not wake", g.force_failed) : std::string(),
                    PoolText(base));
      }
    }

    // Datacore (a clone-only level): plan the forced placing of each area spawner's guards (SpawnerStep does it).
    void SpawnSpawnerFirsts(PPCContext &ctx, uint8_t *base)
    {
      (void)ctx;
      if (!REXCVAR_GET(pdz_counterop_force_spawner_firsts) && !REXCVAR_GET(pdz_counterop_datacore_first_half) &&
          !DatacoreScript(base)) // round 6: on Datacore the plan is always made (the squads' test tool reads its cvar live)
      {
        REXLOG_INFO("pdz_counterop: guards: spawner guards not forced (pdz_counterop_force_spawner_firsts off): they "
                    "come into the world as the agent enters their areas");
        return;
      }
      if (!g.spawner_script)
      {
        g.spawner_script = DatacoreScript(base);
      }
      if (!g.spawner_script)
      {
        REXLOG_INFO("pdz_counterop: guards: {}: a clone-only level without Datacore's area spawners: nothing to "
                    "force (each spot's first clone still counts as an original)",
                    g.mission.empty() ? "this level" : g.mission);
        return;
      }
      g.spawner_force = true;
      g.next_spawner_step = {};
      MapSpawners(base);
      for (const AreaSpawner &s : kDatacoreSpawners)
      {
        const uint32_t sp = g.spawner_script + s.offset;
        uint32_t filled = 0, active = 0;
        const uint32_t count = Load32(base, sp + kSpawnerSlotCount), slots = Load32(base, sp + kSpawnerSlots);
        const uint32_t flags = Load32(base, sp + kSpawnerActive);
        for (uint32_t i = 0; Ptr(slots) && Ptr(flags) && i < count && i < 64; ++i)
        {
          filled += Ptr(Load32(base, slots + 8 * i + kSlotCharacter)) ? 1 : 0;
          active += base[flags + i] ? 1 : 0;
        }
        REXLOG_INFO("pdz_counterop: guards: datacore spawner +0x{:X} ({}; {}): {} of {} slots filled by the first "
                    "tick, {} in the world: {}",
                    s.offset, s.what, GroupName(s.group), filled, count, active,
                    ForcedGroup(s.group)       ? "forced in now"
                    : s.group == SpawnerGroup::kSquads ? "on time (pdz_counterop_datacore_force_squads off)"
                                                       : "never forced");
      }
    }

    // Put the stashed guards of the forced spawners into the world with Rare's activate (82304238), a few a frame,
    // each only while no human sees it (the spawners' own rule) and while Rare's character lists have room. Runs on
    // every scan, so a guard the level stashes again (leaving an area volume, 822F1090) comes back; empty slots are
    // never filled (refills are the level's own business) and a guard player 2 has taken is left alone.
    void SpawnerStep(PPCContext &ctx, uint8_t *base)
    {
      if (!g.spawner_force || !g.spawner_script)
      {
        return;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now < g.next_spawner_step)
      {
        return;
      }
      if (DatacoreScript(base) != g.spawner_script)
      {
        g.spawner_force = false; // the level went away
        return;
      }
      if (!FirstTickDone(base, g.spawner_script))
      {
        if (!g.spawner_first_tick_logged)
        {
          g.spawner_first_tick_logged = true;
          REXLOG_INFO("pdz_counterop: guards: datacore: the level script has not had its first tick yet (its "
                      "spawners are still empty); waiting");
        }
        g.next_spawner_step = now + std::chrono::milliseconds(100);
        return;
      }
      uint32_t done = 0, seen = 0, room = 0;
      bool more = false;
      std::string ids;
      for (const AreaSpawner &s : kDatacoreSpawners)
      {
        if (!ForcedGroup(s.group))
        {
          continue;
        }
        // Round 5 (item 4): a first-half spawner's guards are placed only in the first half, and only for the
        // agent's current area or the next one with guards (as the area rule of the other levels).
        if (s.group == SpawnerGroup::kAreaGuards && !REXCVAR_GET(pdz_counterop_force_spawner_firsts) &&
            (Phase(base) != DatacorePhase::kFirstHalf || !DatacoreAreaInReach(base, s.offset)))
        {
          continue;
        }
        const uint32_t sp = g.spawner_script + s.offset;
        if (Load32(base, sp) != kAreaSpawnerVtable)
        {
          continue;
        }
        const uint32_t count = Load32(base, sp + kSpawnerSlotCount), slots = Load32(base, sp + kSpawnerSlots);
        const uint32_t flags = Load32(base, sp + kSpawnerActive);
        for (uint32_t i = 0; Ptr(slots) && Ptr(flags) && i < count && i < 64; ++i)
        {
          const uint32_t c = Load32(base, slots + 8 * i + kSlotCharacter);
          if (!Ptr(c) || base[flags + i] != 0 || !Alive(ctx, base, c) || (g.taken && g.taken(c)))
          {
            continue; // empty (never filled, or deleted by the level), already in the world, dead, or player 2's
          }
          if (done >= kForcePerFrame)
          {
            more = true; // the rest next frame
            break;
          }
          if (!CharListRoom(base))
          {
            ++room;
            continue;
          }
          if (AnyHumanSees(ctx, base, c))
          {
            ++seen;
            continue; // Rare's rule: never pop a guard in where a player is looking
          }
          {
            rex::CallFrame frame(ctx);
            frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
            frame.ctx.r3.u64 = sp;
            frame.ctx.r4.u64 = i;
            sub_82304238(frame.ctx, base);
          }
          if (base[flags + i] == 0)
          {
            continue;
          }
          ++done;
          ++g.spawner_placed;
          ids += fmt::format("{}{:X}.{}", ids.empty() ? "" : ",", s.offset, i);
          // Known now (it enters the character list only now): forced at its spot, an original.
          Known k;
          k.eligible = g.eligible && g.eligible(c);
          k.origin = k.eligible ? Origin::kForced : Origin::kNotGuard;
          k.last[0] = LoadFloat(base, c + kPosition);
          k.last[1] = LoadFloat(base, c + kPosition + 4);
          k.last[2] = LoadFloat(base, c + kPosition + 8);
          k.last_at = now;
          g.known[c] = k;
          g.spawner_of[c] = SpawnerSlot{sp, i, s.group};
          g.spawner_home[c] = SpawnerSlot{sp, i, s.group};
          if (!k.eligible)
          {
            REXLOG_WARN("pdz_counterop: guards: datacore: spawner guard {:08X} placed but refused by the eligibility "
                        "test (Datacore's guard templates all carry the id Grd1: is it still in the denied list?)",
                        c);
          }
        }
        if (more)
        {
          break;
        }
      }
      if (done)
      {
        REXLOG_INFO("pdz_counterop: guards: datacore: {} spawner guard(s) put in the world at their spots (script "
                    "offset.slot {}), {} so far; {}",
                    done, ids, g.spawner_placed, PoolText(base));
      }
      g.next_spawner_step = more ? now : now + std::chrono::milliseconds(int(kScanSeconds * 1000));
      if ((seen != g.spawner_waiting_seen || room != g.spawner_waiting_room) && (seen || room))
      {
        REXLOG_INFO("pdz_counterop: guards: datacore: {} spawner guard(s) wait (a player sees the spot), {} wait for "
                    "room in Rare's character lists (in use {} / {} of {})",
                    seen, room, Load32(base, kCharListA + kCharListUsed), Load32(base, kCharListB + kCharListUsed),
                    kCharListNodes);
      }
      g.spawner_waiting_seen = seen;
      g.spawner_waiting_room = room;
    }

    void Settle(PPCContext &ctx, uint8_t *base, const char *why)
    {
      g.settled = true;
      g.settle_at = std::chrono::steady_clock::now();
      uint32_t live = 0, dormant = 0, clones = 0, other = 0;
      std::string opening;
      for (const auto &[c, k] : g.known)
      {
        switch (k.origin)
        {
        case Origin::kOpeningLive:
          ++live;
          opening += (opening.empty() ? "" : ",") + IdText(SetupIdKey(base, c));
          break;
        case Origin::kDormantOriginal:
          ++dormant;
          break;
        case Origin::kOpeningClone:
          ++clones;
          break;
        default:
          ++other;
          break;
        }
      }
      REXLOG_INFO("pdz_counterop: guards: opening settled ({}, {:.1f} s after the start): {} opening enemies live ({}), "
                  "{} opening clones, {} originals dormant, {} other characters not eligible; {}",
                  why, Since(g.start_at), live, opening.empty() ? "-" : opening, clones, dormant, other, PoolText(base));
      g.clone_mission = live == 0 && dormant == 0;
      if (g.clone_mission)
      {
        REXLOG_INFO("pdz_counterop: guards: {}: no numbered original at all: a clone-only level; each spot's first "
                    "guard from the level's own spawners counts as an original, a later guard at the same spot is a "
                    "refill (a reinforcement, never taken)",
                    g.mission.empty() ? "this level" : g.mission);
      }
    }
  } // namespace

  const char *OriginName(Origin o)
  {
    switch (o)
    {
    case Origin::kOpeningLive:
      return "opening enemy";
    case Origin::kDormantOriginal:
      return "original, dormant";
    case Origin::kForced:
      return "original, woken at the start";
    case Origin::kWokenByScript:
      return "original, woken by the script";
    case Origin::kOpeningClone:
      return "opening clone";
    case Origin::kReinforcement:
      return "reinforcement";
    default:
      return "not a guard";
    }
  }

  bool IsTemplateClone(const uint8_t *base, uint32_t character)
  {
    // Sun 22 Nov night (audit): the setup and the name text read only where readable.
    const uint32_t setup = pdz::guestmem::Ok(base, character, kSetup + 4) ? Load32(base, character + kSetup) : 0;
    const uint32_t name = pdz::guestmem::Ok(base, setup, kSetupName + 4) ? Load32(base, setup + kSetupName) : 0;
    if (!pdz::guestmem::Ok(base, name, 9) || name == kNoName)
    {
      return false;
    }
    static constexpr char kNoNameText[] = "_noname_";
    return std::memcmp(base + name, kNoNameText, sizeof(kNoNameText)) != 0;
  }

  void LevelStart(PPCContext &ctx, uint8_t *base, const std::string &mission,
                  std::function<bool(uint32_t character)> eligible)
  {
    const uint32_t scratch = g.scratch;
    auto taken = std::move(g.taken);
    g = State{};
    g.scratch = scratch;
    g.taken = std::move(taken);
    g.started = true;
    g.mission = mission;
    g.eligible = std::move(eligible);
    if (const ForceRule *rule = FindRule(kForce, std::size(kForce), mission))
    {
      ParseList(rule->on_time, g.on_time); // round 3: known before the first scan (dormant takes skip these ids)
    }
    g.start_at = std::chrono::steady_clock::now();
    Scan(ctx, base);
    uint32_t live = 0, dormant = 0, clones = 0;
    for (const auto &[c, k] : g.known)
    {
      live += k.origin == Origin::kOpeningLive;
      dormant += k.origin == Origin::kDormantOriginal;
      clones += k.origin == Origin::kOpeningClone;
    }
    REXLOG_INFO("pdz_counterop: guards: start snapshot: {} SP guards, {} eligible originals live, {} dormant, {} clones "
                "(the opening is settled after the first cutscene); {}",
                g.known.size(), live, dormant, clones, PoolText(base));
  }

  void Reset() { g.started = false; }

  bool OpeningSettled() { return g.settled; }

  void Tick(PPCContext &ctx, uint8_t *base, uint32_t agent, bool in_cutscene)
  {
    (void)agent;
    if (!g.started)
    {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (in_cutscene)
    {
      g.saw_cutscene = true;
    }
    else if (g.saw_cutscene)
    {
      g.cutscene_over = true;
    }
    if (now >= g.next_scan)
    {
      g.next_scan = now + std::chrono::milliseconds(int(kScanSeconds * 1000));
      Scan(ctx, base);
    }
    if (!g.settled && !in_cutscene &&
        (g.cutscene_over || (!g.saw_cutscene && Since(g.start_at) >= kSettleNoCutsceneSeconds)))
    {
      Scan(ctx, base);
      Settle(ctx, base, g.cutscene_over ? "the opening cutscene is over" : "no opening cutscene");
      ForceOriginals(ctx, base);
    }
    if (g.settled && !in_cutscene)
    {
      ForceStep(ctx, base);
      SpawnerStep(ctx, base);
    }
  }

  void SetTakenTest(std::function<bool(uint32_t character)> taken) { g.taken = std::move(taken); }

  bool StillExists(const uint8_t *base, uint32_t character)
  {
    if (PdzCounterOpRooftopsDropGone(base, character)) { return false; } // round 13 e: Rooftops, a dropped guard Rare has deleted
    const auto it = g.spawner_home.find(character);
    if (it == g.spawner_home.end() || !Ptr(it->second.spawner))
    {
      return true; // not a spawner guard: Rare never deletes those under us
    }
    // Rare's delete (82304188 -> [7] 823044F0, e.g. the IndoorGuard spawners at 82302ADC when flag +0x20 bit
    // 0x100 is set) frees the guard; the slot's weak handle no longer names it.
    // Sun 22 Nov night (audit): the spawner and its slot read only where readable (gone = not existing).
    if (!pdz::guestmem::Ok(base, it->second.spawner, kSpawnerSlots + 4))
    {
      return false;
    }
    const uint32_t slots = Load32(base, it->second.spawner + kSpawnerSlots);
    return pdz::guestmem::Ok(base, slots + 8 * it->second.slot + kSlotCharacter, 4) && Ptr(slots) &&
           Load32(base, slots + 8 * it->second.slot + kSlotCharacter) == character;
  }

  Origin OriginOf(uint32_t character)
  {
    const auto it = g.known.find(character);
    return it == g.known.end() ? Origin::kNotGuard : it->second.origin;
  }

  bool Takeable(uint32_t character)
  {
    if (counter_op_levels13::WaveBodyKnown(character)) { return true; } // round 13 i: a Bridge end-wave guard
    if (const int drop = PdzCounterOpRooftopsDropVerdict(character); drop >= 0) { return drop != 0; } // round 13 e: Rooftops, a guard Killian's ship dropped (taken only once landed)
    const auto it = g.known.find(character);
    if (it == g.known.end())
    {
      return false;
    }
    const Origin o = it->second.origin;
    if (o == Origin::kDormantOriginal)
    {
      // Round 3 (item 12): a sleeping original can be taken where it will stand (the level keeps its own wake-ups,
      // so no area is emptied early); never one the level brings in on cue or only on some difficulties.
      return REXCVAR_GET(pdz_counterop_take_dormant) && !it->second.on_time;
    }
    // Sat 21 Nov (pdz_counterop_take_spawned): a later spawned copy of a takeable id (Jungle's 's01a' wave guards) is
    // taken like an original (Datacore's spawner waves keep their own rules, counter_op_jetpack / datacore_riders).
    if (o == Origin::kReinforcement && PdzCounterOpTakeSpawned() && g.mission != "datacore")
    {
      return true;
    }
    return o == Origin::kOpeningLive || o == Origin::kForced || o == Origin::kWokenByScript ||
           o == Origin::kOpeningClone;
  }

  bool AgentSees(PPCContext &ctx, uint8_t *base, uint32_t agent, float x, float y, float z)
  {
    if (!Ptr(agent))
    {
      return false;
    }
    if (!g.scratch)
    {
      g.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
      if (!g.scratch)
      {
        return false;
      }
    }
    const uint32_t eye = g.scratch, point = g.scratch + 16, visitor = g.scratch + 32;
    StoreFloat(base, point, x);
    StoreFloat(base, point + 4, y + kSpotHeight);
    StoreFloat(base, point + 8, z);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = point;
      frame.ctx.r4.u64 = agent;
      sub_82153890(frame.ctx, base);
      if ((frame.ctx.r3.u32 & 0xFF) == 0)
      {
        return false; // outside his view cone
      }
    }
    StoreFloat(base, eye, LoadFloat(base, agent + kPosition));
    StoreFloat(base, eye + 4, LoadFloat(base, agent + kPosition + 4) + LoadFloat(base, agent + kEyeHeight));
    StoreFloat(base, eye + 8, LoadFloat(base, agent + kPosition + 8));
    Store32(base, visitor, kAnyHitVisitor);
    base[visitor + 4] = 0;
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
    frame.ctx.r3.u64 = kSightMask;
    frame.ctx.r4.u64 = eye;
    frame.ctx.r5.u64 = point;
    frame.ctx.r6.u64 = visitor;
    frame.ctx.r7.u64 = 1;
    sub_82259B80(frame.ctx, base);
    return (frame.ctx.r3.u32 & 0xFF) == 0; // nothing in the way: seen
  }

  // Sat 21 Nov (pdz_counterop_wait_view_upright): AgentSees' line test without its view cone: nothing solid between
  // the agent's eye and (x, y, z) (y as given, no spot height added).
  bool LineClear(PPCContext &ctx, uint8_t *base, uint32_t agent, float x, float y, float z)
  {
    if (!Ptr(agent))
    {
      return false;
    }
    if (!g.scratch)
    {
      g.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
      if (!g.scratch)
      {
        return false;
      }
    }
    const uint32_t eye = g.scratch, point = g.scratch + 16, visitor = g.scratch + 32;
    StoreFloat(base, point, x);
    StoreFloat(base, point + 4, y);
    StoreFloat(base, point + 8, z);
    StoreFloat(base, eye, LoadFloat(base, agent + kPosition));
    StoreFloat(base, eye + 4, LoadFloat(base, agent + kPosition + 4) + LoadFloat(base, agent + kEyeHeight));
    StoreFloat(base, eye + 8, LoadFloat(base, agent + kPosition + 8));
    Store32(base, visitor, kAnyHitVisitor);
    base[visitor + 4] = 0;
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
    frame.ctx.r3.u64 = kSightMask;
    frame.ctx.r4.u64 = eye;
    frame.ctx.r5.u64 = point;
    frame.ctx.r6.u64 = visitor;
    frame.ctx.r7.u64 = 1;
    sub_82259B80(frame.ctx, base);
    return (frame.ctx.r3.u32 & 0xFF) == 0;
  }

  int Pick(PPCContext &ctx, uint8_t *base, uint32_t agent, std::vector<Candidate> &candidates, bool first,
           bool patient, std::string &why)
  {
    // Round 3 (item 5). In real play the opening enemies are all in the agent's view, so round 2's "out of view
    // first" skipped them for a guard further into the level. The order now:
    //   first spawn: (a) an opening enemy in the agent's area (within kAreaM, about her floor) she can't see right
    //   now, however close; (b) else one at the edge of her view (kEdgeDeg or more off the middle of her screen and
    //   not within 6 m); (c) only after kPatience of that (the caller's `patient`), enemies further in.
    //   respawn: out of her view, her area first, then further; in view only far away.
    // Steps: {opening enemies only, unseen required, min m, max m (0 = any), her area only, min degrees off the
    // middle of her screen (seen guards only), patient = tried while the caller still waits for the area}.
    // Round 4 (item 2): sleep = 0 either, 1 awake guards only, 2 sleeping guards only. With
    // pdz_counterop_dormant_next_only a sleeping guard is a candidate at all only when its wake group is the one the
    // agent is heading into next (counter_op_wake), and the awake guards near her come first; a sleeping guard far
    // ahead is never taken (log 053: 'lop2', 91 m ahead in an area nothing had woken).
    struct Step
    {
      const char *name;
      bool opening_only, unseen;
      float min_m, max_m;
      bool area;
      float min_deg;
      bool while_patient;
      int sleep;
    };
    constexpr float kAreaM = 70.0f, kAreaUpM = 6.0f, kEdgeDeg = 18.0f;
    static constexpr Step kFirst[] = {
        {"an opening enemy in the agent's area, out of her view", true, true, 2.5f, kAreaM, true, 0.0f, true, 1},
        {"an opening enemy in the agent's area, at the edge of her view", true, false, 6.0f, kAreaM, true, kEdgeDeg, true, 1},
        {"an opening enemy in the agent's area, off the middle of her screen and 15 m or more away", true, false, 15.0f,
         kAreaM, true, 9.0f, true, 1},
        {"an opening enemy out of the agent's view (none usable in her area)", true, true, 0.0f, 0.0f, false, 0.0f, false, 1},
        {"an awake original out of the agent's view, within 60 m", false, true, 2.5f, 60.0f, false, 0.0f, false, 1},
        {"a sleeping original of the area the agent is heading into, out of her view", false, true, 2.5f, 0.0f, false,
         0.0f, false, 2},
        {"an awake original out of the agent's view", false, true, 0.0f, 0.0f, false, 0.0f, false, 1},
        {"an awake original at least 15 m away", false, false, 15.0f, 0.0f, false, 0.0f, false, 1},
        {"a sleeping original of the area the agent is heading into, at least 15 m away", false, false, 15.0f, 0.0f,
         false, 0.0f, false, 2},
        {"any awake original", false, false, 0.0f, 0.0f, false, 0.0f, false, 1},
    };
    static constexpr Step kRespawn[] = {
        {"an awake original in the agent's area, out of her view", false, true, 6.0f, 45.0f, true, 0.0f, true, 1},
        {"an awake original out of the agent's view, within 90 m", false, true, 6.0f, 90.0f, false, 0.0f, true, 1},
        {"a sleeping original of the area the agent is heading into, out of her view", false, true, 6.0f, 0.0f, false,
         0.0f, true, 2},
        {"an awake original in view, 30 to 90 m away", false, false, 30.0f, 90.0f, false, 0.0f, true, 1},
        {"an awake original out of the agent's view", false, true, 0.0f, 0.0f, false, 0.0f, true, 1},
        {"a sleeping original of the area the agent is heading into, at least 15 m away", false, false, 15.0f, 0.0f,
         false, 0.0f, true, 2},
        {"an awake original in view but at least 30 m away", false, false, 30.0f, 0.0f, false, 0.0f, true, 1},
        {"any awake original", false, false, 0.0f, 0.0f, false, 0.0f, true, 1},
    };
    const bool next_only = REXCVAR_GET(pdz_counterop_dormant_next_only);
    uint32_t asleep_far = 0;
    std::vector<uint8_t> asleep(candidates.size(), 0);
    std::vector<std::string> notes(candidates.size());
    static uint32_t g_area_refusal_lines = 0; // the first 40 refusals of a run are logged one by one
    // Rule facts per candidate; riders / fallers out (a guard moving faster than 6 m/s between scans).
    const float yaw = Ptr(agent) ? LoadFloat(base, agent + 0x1670) * 3.14159265f / 180.0f : 0.0f; // view yaw, degrees
    const float ax = Ptr(agent) ? LoadFloat(base, agent + kPosition) : 0.0f;
    const float ay = Ptr(agent) ? LoadFloat(base, agent + kPosition + 4) : 0.0f;
    const float az = Ptr(agent) ? LoadFloat(base, agent + kPosition + 8) : 0.0f;
    std::vector<size_t> usable;
    std::vector<size_t> area_refused;
    g_last_pick_relaxed = 0;
    std::vector<float> off_deg(candidates.size(), 180.0f);
    std::vector<uint8_t> in_area(candidates.size(), 0);
    // Sat 21 Nov: a level with no area table (or the rule off) uses real positions (pdz_counterop_untabled_by_position).
    const bool untabled = PdzCounterOpUntabledByPosition() && !counter_op_wake::HasAreaTable(base);
    const bool awake_only = PdzCounterOpRelaxAwakeOnly();
    uint32_t beyond_reach = 0;
    uint32_t locked_out = 0;           // round 18 (item 4)
    static uint32_t locked_lines = 0;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
      Candidate &e = candidates[i];
      e.origin = OriginOf(e.character);
      const auto it = g.known.find(e.character);
      // Round 13 c: a rider whose seat can be handed over (the flying takeover) is a candidate as he is: the "moving
      // fast = riding" rule and the originals rule are for guards on foot (the caller's side rule has passed him).
      if (!e.rider && (!Takeable(e.character) || (it != g.known.end() && it->second.speed > kRidingSpeed)))
      {
        continue;
      }
      // Round 16 (item 6b): just woken by the level and still calm: his group's scripted scene may be playing.
      if (!e.rider && it != g.known.end() && REXCVAR_GET(pdz_counterop_scene_release) && e.calm &&
          it->second.woken_by_level && Since(it->second.woken_at) < REXCVAR_GET(pdz_counterop_scene_release_s))
      {
        if (!it->second.scene_logged)
        {
          it->second.scene_logged = true;
          REXLOG_INFO("pdz_counterop: pick: guard '{}' is not taken yet: the level woke him {:.1f} s ago and he is "
                      "still calm (his group's scripted scene may be playing; free after {:.0f} s or once he is "
                      "alerted: pdz_counterop_scene_release)",
                      IdText(SetupIdKey(base, e.character)), Since(it->second.woken_at),
                      REXCVAR_GET(pdz_counterop_scene_release_s));
        }
        continue;
      }
      asleep[i] = !e.rider && e.origin == Origin::kDormantOriginal;
      // Round 18 (item 4, pdz_counterop_pick_locked_doors): never behind a door the agent has locked, nor a guard a
      // cut-off move has just handed back. Not even the relax steps below take him (he is not in area_refused).
      {
        std::string locked_why;
        if (PdzCounterOpPickLockedOut(base, e.character, e.x, e.z, locked_why))
        {
          ++locked_out;
          if (locked_lines < 40)
          {
            ++locked_lines;
            REXLOG_INFO("pdz_counterop: pick: guard '{}' ({:.1f} m from the agent) not taken: {}",
                        IdText(SetupIdKey(base, e.character)), e.distance / 100.0f, locked_why);
          }
          continue;
        }
      }
      // Round 5 (item 1): awake or asleep, only a guard of the agent's current area or of her next one in reach.
      if (!counter_op_wake::AreaAllows(base, e.character, agent, asleep[i] != 0, next_only, notes[i]))
      {
        ++asleep_far;
        area_refused.push_back(i); // round 12: the relaxed steps below may still take him
        if (g_area_refusal_lines < 40)
        {
          ++g_area_refusal_lines;
          REXLOG_INFO("pdz_counterop: pick: {}", notes[i]);
        }
        continue;
      }
      // Sat 21 Nov (pdz_counterop_untabled_by_position): a level with no area table: only within reach of the agent
      // (the relax steps below widen it). Sleeping guards were refused above (round 4's rule: no wake table).
      if (untabled && !e.rider && e.distance > PdzCounterOpUntabledReachCm(0))
      {
        ++beyond_reach;
        notes[i] = fmt::format("guard '{}': {:.1f} m from the agent, beyond the reach of {:.0f} m on a level with no area "
                               "table (pdz_counterop_untabled_by_position)",
                               IdText(SetupIdKey(base, e.character)), e.distance / 100.0f,
                               PdzCounterOpUntabledReachCm(0) / 100.0f);
        area_refused.push_back(i);
        continue;
      }
      e.seen = AgentSees(ctx, base, agent, e.x, e.y, e.z);
      const float dx = e.x - ax, dz = e.z - az, len = std::sqrt(dx * dx + dz * dz);
      if (len > 1.0f)
      {
        const float dot = std::clamp((dx * std::sin(yaw) + dz * std::cos(yaw)) / len, -1.0f, 1.0f);
        off_deg[i] = std::acos(dot) * 180.0f / 3.14159265f;
      }
      in_area[i] = e.distance <= kAreaM * 100.0f && std::fabs(e.y - ay) <= kAreaUpM * 100.0f;
      usable.push_back(i);
    }
    std::sort(usable.begin(), usable.end(),
              [&](size_t a, size_t b) { return candidates[a].distance < candidates[b].distance; });
    // Round 18 (item 4): a cut-off move's pick: the guards of the area she is heading into first (nearest first).
    if (PdzCounterOpPickPreferNext() && counter_op_wake::HasAreaTable(base))
    {
      int cur = -1, next = -1;
      float trigger_m = 0.0f;
      counter_op_wake::AreasOfJo(base, agent, &cur, &next, &trigger_m);
      if (next >= 0)
      {
        std::stable_partition(usable.begin(), usable.end(), [&](size_t i) {
          bool liftable = false;
          return counter_op_wake::AreaOfGuard(base, candidates[i].character, asleep[i] != 0, &liftable) == next;
        });
      }
    }
    if (untabled)
    {
      // Ahead of her (within 90 degrees of where she looks) before behind her, each nearest first.
      std::stable_sort(usable.begin(), usable.end(),
                       [&](size_t a, size_t b) { return (off_deg[a] <= 90.0f) > (off_deg[b] <= 90.0f); });
    }
    const Step *steps = first ? kFirst : kRespawn;
    const size_t count = first ? std::size(kFirst) : std::size(kRespawn);
    // Fri 20 Nov night (pdz_counterop_spawn_keep_away): every step refuses a body within min_m of the agent, or one
    // she sees within view_m near the middle of her view. Once only such bodies have been left for near_wait_s
    // (earlier tries of this pick, g_near_since), the rule is waived for one pick (logged).
    const bool keep_on = REXCVAR_GET(pdz_counterop_spawn_keep_away);
    const bool waive = keep_on && !patient && g_near_since != std::chrono::steady_clock::time_point{} &&
                       Since(g_near_since) >= REXCVAR_GET(pdz_counterop_spawn_near_wait_s);
    const bool keep = keep_on && !waive;
    const float keep_min_cm = float(REXCVAR_GET(pdz_counterop_spawn_min_m)) * 100.0f;
    const float keep_view_cm = float(REXCVAR_GET(pdz_counterop_spawn_view_m)) * 100.0f;
    const float keep_view_deg = float(REXCVAR_GET(pdz_counterop_spawn_view_deg));
    std::vector<uint8_t> near_refused(candidates.size(), 0);
    const auto too_near = [&](size_t i) {
      const Candidate &e = candidates[i];
      return e.distance < keep_min_cm || (e.seen && e.distance < keep_view_cm && off_deg[i] <= keep_view_deg);
    };
    const auto near_text = [&]() {
      uint32_t n = 0;
      std::string ids;
      for (size_t i = 0; i < candidates.size(); ++i)
      {
        if (near_refused[i])
        {
          ++n;
          if (n <= 6)
          {
            ids += fmt::format("{}'{}' {:.1f} m{}", n > 1 ? ", " : "", IdText(SetupIdKey(base, candidates[i].character)),
                               candidates[i].distance / 100.0f, candidates[i].seen ? " in her view" : "");
          }
        }
      }
      return std::make_pair(n, ids);
    };
    for (size_t si = 0; si < count; ++si)
    {
      const Step &s = steps[si];
      if (patient && !s.while_patient)
      {
        break; // the first spawn: for a few seconds only her area's opening enemies (one may turn the corner)
      }
      std::vector<size_t> list;
      for (const size_t i : usable)
      {
        const Candidate &e = candidates[i];
        const bool opening = e.origin == Origin::kOpeningLive || e.origin == Origin::kOpeningClone;
        if (next_only && s.sleep != 0 && (s.sleep == 2) != (asleep[i] != 0))
        {
          continue;
        }
        if ((s.opening_only && !opening) || (s.unseen && e.seen) || e.distance < s.min_m * 100.0f ||
            (s.max_m > 0.0f && e.distance > s.max_m * 100.0f) || (s.area && !in_area[i]) ||
            (e.seen && off_deg[i] < s.min_deg))
        {
          continue;
        }
        if (keep && too_near(i))
        {
          near_refused[i] = 1; // Fri 20 Nov night: pdz_counterop_spawn_keep_away
          continue;
        }
        list.push_back(i);
      }
      if (!list.empty())
      {
        // An edge-of-view step takes the guard furthest off the middle of her screen; the others one of the two
        // nearest at random (N64: not always the same guard).
        size_t pick = list[0];
        if (s.min_deg > 0.0f)
        {
          for (const size_t i : list)
          {
            pick = off_deg[i] > off_deg[pick] ? i : pick;
          }
        }
        else
        {
          const size_t n = std::min<size_t>(list.size(), 2);
          pick = list[std::uniform_int_distribution<size_t>(0, n - 1)(g_rng)];
        }
        const Candidate &e = candidates[pick];
        why = fmt::format("{} ('{}', {}, {:.1f} m, {}, {:.0f} deg off the middle of her view; step {} of {}; {} of {} "
                          "candidates usable, {} refused by the area rule); {}",
                          s.name, IdText(SetupIdKey(base, e.character)), OriginName(e.origin), e.distance / 100.0f,
                          e.seen ? "seen by the agent" : "out of her view", off_deg[pick], si + 1, count, usable.size(),
                          candidates.size(), asleep_far, notes[pick]);
        // Fri 20 Nov night (pdz_counterop_spawn_keep_away)
        if (waive)
        {
          REXLOG_INFO("pdz_counterop: pick: keep-away WAIVED: only bodies near the agent were left for {:.1f} s "
                      "(pdz_counterop_spawn_near_wait_s {:.1f}): '{}' taken at {:.1f} m ({}, {:.0f} deg off the middle "
                      "of her view)",
                      Since(g_near_since), REXCVAR_GET(pdz_counterop_spawn_near_wait_s),
                      IdText(SetupIdKey(base, e.character)), e.distance / 100.0f,
                      e.seen ? "seen by the agent" : "out of her view", off_deg[pick]);
          why += "; keep-away waived (nothing else left)";
        }
        else if (const auto [n, ids] = near_text(); n > 0)
        {
          why += fmt::format("; keep-away (pdz_counterop_spawn_keep_away): {} body/bodies near the agent passed over "
                             "({}{}; under {:.0f} m, or in her view within {:.0f} m and {:.0f} deg of its middle)",
                             n, ids, n > 6 ? ", ..." : "", keep_min_cm / 100.0f, keep_view_cm / 100.0f, keep_view_deg);
        }
        g_near_since = {};
        return int(pick);
      }
    }
    // Round 12 (item 6): he is never left without a body. Log 013, Nightclub from 23:14:59: 21 candidates, every one
    // refused by the area rule, try after try (Jo in area 1, nobody left of it, area 2's guards not in reach). When
    // no guard has fitted for pdz_counterop_area_relax_s the rule is relaxed step by step, always out of her view:
    //   step 1: the nearest guard of the nearest area ahead of her that has one;
    //   step 2 (after twice that time): any eligible original the area rule refused.
    // Never lifted: a sleeping guard the script wakes on a cue, a guard that is no takeable original, a rider.
    const double relax_s = REXCVAR_GET(pdz_counterop_area_relax_s);
    const int relax = relax_s > 0.0 && !patient ? (g_pick_wait_s >= 2.0 * relax_s ? 2 : g_pick_wait_s >= relax_s ? 1 : 0) : 0;
    std::string relax_text;
    if (relax > 0 && !area_refused.empty())
    {
      int cur = 0, next = -1;
      float trigger_m = 0.0f;
      counter_op_wake::AreasOfJo(base, agent, &cur, &next, &trigger_m);
      int best = -1, best_area = 1 << 30;
      uint32_t cue = 0, in_view = 0, behind = 0, lifted = 0;
      std::string in_view_ids, cue_ids;
      for (const size_t i : area_refused)
      {
        Candidate &e = candidates[i];
        bool liftable = false;
        const int area = counter_op_wake::AreaOfGuard(base, e.character, asleep[i] != 0, &liftable);
        const std::string id = IdText(SetupIdKey(base, e.character));
        // Sat 21 Nov (pdz_counterop_relax_awake_only): never a sleeping (dormant) or scene-held guard.
        if (!liftable || (awake_only && asleep[i]))
        {
          ++cue;
          cue_ids += (cue_ids.empty() ? "" : ",") + id;
          continue;
        }
        if (untabled)
        {
          // Sat 21 Nov (pdz_counterop_untabled_by_position): the reach widens (step 1 double, step 2 any distance).
          if (e.distance > PdzCounterOpUntabledReachCm(relax))
          {
            ++behind;
            continue;
          }
        }
        else if (relax == 1 && area <= cur)
        {
          ++behind; // step 1 only looks ahead of her
          continue;
        }
        e.seen = AgentSees(ctx, base, agent, e.x, e.y, e.z);
        if (e.seen)
        {
          ++in_view;
          in_view_ids += (in_view_ids.empty() ? "" : ",") + id;
          continue;
        }
        if (keep && e.distance < keep_min_cm)
        {
          near_refused[i] = 1; // Fri 20 Nov night: pdz_counterop_spawn_keep_away (out of her view: the distance only)
          continue;
        }
        ++lifted;
        // Step 1: the nearest area ahead first, then the nearest guard; step 2: the nearest guard. Sat 21 Nov, no area
        // table: ahead of her first (within 90 degrees of her look), then the nearest.
        float relax_off = 180.0f;
        if (untabled)
        {
          const float dx = e.x - ax, dz = e.z - az, len = std::sqrt(dx * dx + dz * dz);
          if (len > 1.0f)
          {
            relax_off = std::acos(std::clamp((dx * std::sin(yaw) + dz * std::cos(yaw)) / len, -1.0f, 1.0f)) * 57.29578f;
          }
        }
        const int key = untabled ? (relax_off <= 90.0f ? 0 : 1) : relax == 1 ? area : 0;
        if (best < 0 || key < best_area || (key == best_area && e.distance < candidates[size_t(best)].distance))
        {
          best = int(i);
          best_area = key;
        }
      }
      relax_text = fmt::format("; RELAXED step {} of 2 after {:.0f} s without a guard that fits the area rule "
                               "(pdz_counterop_area_relax_s {:.0f}): {} refused by the area rule looked at again: {} "
                               "usable out of her view, {} in her view ({}), {} never lifted: asleep until the script's "
                               "cue, or in no area ({}), {} not ahead of her{}",
                               relax, g_pick_wait_s, relax_s, area_refused.size(), lifted, in_view,
                               in_view_ids.empty() ? "-" : in_view_ids, cue, cue_ids.empty() ? "-" : cue_ids, behind,
                               untabled ? fmt::format(" (no area table: by position, reach {}; sleeping guards never "
                                                      "taken: pdz_counterop_untabled_by_position / relax_awake_only)",
                                                      relax >= 2 ? std::string("any distance")
                                                                 : fmt::format("{:.0f} m", PdzCounterOpUntabledReachCm(relax) / 100.0f))
                                        : std::string());
      if (best >= 0)
      {
        const Candidate &e = candidates[size_t(best)];
        g_last_pick_relaxed = relax;
        why = fmt::format("{} ('{}', {}, {}, {:.1f} m, out of her view){}; the guard was refused before by: {}",
                          untabled ? "an awake guard within the widened reach, ahead of the agent first, out of her view"
                          : relax == 1 ? "the nearest guard of the nearest area ahead of the agent, out of her view"
                                       : "any eligible original, out of her view",
                          IdText(SetupIdKey(base, e.character)), OriginName(e.origin),
                          asleep[size_t(best)] ? "asleep" : "awake", e.distance / 100.0f, relax_text, notes[size_t(best)]);
        g_near_since = {}; // Fri 20 Nov night: pdz_counterop_spawn_keep_away
        return best;
      }
    }
    uint32_t opening_n = 0, area_n = 0, seen_n = 0;
    for (const size_t i : usable)
    {
      const Candidate &e = candidates[i];
      opening_n += e.origin == Origin::kOpeningLive || e.origin == Origin::kOpeningClone;
      area_n += in_area[i];
      seen_n += e.seen;
    }
    why = fmt::format("no takeable original this try among {} candidates ({} usable: {} opening enemies, {} in her area, "
                      "{} seen; {} behind a door the agent locked or just handed back; {} refused by the area rule: not "
                      "of her current area, nor of her next area in reach{}{}); {}",
                      candidates.size(), usable.size(), opening_n, area_n, seen_n, locked_out, asleep_far,
                      untabled ? fmt::format("; {} beyond the reach of {:.0f} m (no area table: by position)", beyond_reach,
                                             PdzCounterOpUntabledReachCm(0) / 100.0f)
                               : std::string(),
                      patient ? "; still waiting for one of her area's opening enemies" : "",
                      counter_op_wake::AreaLine(base, agent));
    why += relax_text;
    // Fri 20 Nov night (pdz_counterop_spawn_keep_away): only bodies near the agent left: wait, then waive the rule.
    if (const auto [near_n, near_ids] = near_text(); keep && !patient && near_n > 0)
    {
      if (g_near_since == std::chrono::steady_clock::time_point{})
      {
        g_near_since = std::chrono::steady_clock::now();
        REXLOG_INFO("pdz_counterop: pick: keep-away: only bodies near the agent are left ({}{}; under {:.0f} m, or in "
                    "her view within {:.0f} m and {:.0f} deg of its middle): player 2 waits {:.1f} s before one of "
                    "them is taken (pdz_counterop_spawn_near_wait_s)",
                    near_ids, near_n > 6 ? ", ..." : "", keep_min_cm / 100.0f, keep_view_cm / 100.0f, keep_view_deg,
                    REXCVAR_GET(pdz_counterop_spawn_near_wait_s));
      }
      why += fmt::format("; keep-away: {} body/bodies near the agent refused, waiting ({:.1f} s so far)", near_n,
                         Since(g_near_since));
    }
    else if (!keep || near_n == 0)
    {
      g_near_since = {}; // nothing near was refused (or the rule is off / waived and still nobody): no wait running
    }
    // Round 12: each change of the relax step is logged even after the caller's 12 lines are used up.
    static int last_relax_logged = 0;
    if (relax != last_relax_logged)
    {
      last_relax_logged = relax;
      if (relax > 0)
      {
        REXLOG_INFO("pdz_counterop: pick: relax step {} found nobody: {}", relax, why);
      }
    }
    return -1;
  }

  uint32_t OriginalsLeft(PPCContext &ctx, uint8_t *base, const std::function<bool(uint32_t)> &taken)
  {
    uint32_t left = 0;
    // Sun 22 Nov night (audit): g.known keeps guards Rare has since deleted (Datacore's spawners, Outpost's last fight):
    // only a readable guard that is still one and still in its slot is asked (the taken test reads it too).
    const auto still_guard = [base](uint32_t c) {
      return pdz::guestmem::Ok(base, c, kHidden + 4) && Load32(base, c) == kGuardVtable && StillExists(base, c);
    };
    for (const auto &[c, k] : g.known)
    {
      const bool original = k.origin == Origin::kOpeningLive || k.origin == Origin::kDormantOriginal ||
                            k.origin == Origin::kForced || k.origin == Origin::kWokenByScript ||
                            k.origin == Origin::kOpeningClone;
      left += original && still_guard(c) && !(taken && taken(c)) && !PdzCounterOpRooftopsDropGone(base, c) && Alive(ctx, base, c); // round 13 e: not a guard Rare has deleted (Rooftops deletes its first 29 when Killian arrives)
    }
    // Datacore: forced spawner guards still stashed (placed as soon as no player sees their spot) count too.
    if (g.spawner_force)
    {
      for (const auto &[c, s] : g.spawner_of)
      {
        left += ForcedGroup(s.group) && !g.known.count(c) && still_guard(c) && !(taken && taken(c)) && Alive(ctx, base, c);
      }
    }
    return left;
  }
} // namespace counter_op_guards

// Round 5 (item 4): Datacore's phases and its first-half guards.
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_first_half, false, "Testing",
                    "TESTING (counter-op, Datacore; round 5, off since round 6): before the mid-level cutscene "
                    "player 2 may take a first-half IndoorGuard01 (the agent's friendly in Rare's data) with Rare's "
                    "turned-guard masks, and those guards are placed ahead of the agent. Off = he only ever takes "
                    "guards that are the agent's enemies by Rare's own masks: the two placed server-room elites "
                    "(tut0 / tut1) first, then the last area's squads")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_datacore_stage, 0, "Testing",
                     "TESTING TOOL (counter-op, Datacore): 1 = the script's stage word is set as when the mid-level "
                     "cutscene is flagged, 2 = as when it is over (Rare's own handler then deletes the first-half "
                     "guards). For scripted tests only: no script can play the level that far")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_calm, true, "Gameplay",
                    "Counter-Operative on Datacore's first half (default on): when player 2 dies as a first-half "
                    "guard, the replaced guard's death does not run Rare's Datacore rule that turns the guards within "
                    "25 m on the agent (her kills of real guards still do). Off = Rare's rule also for his deaths")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace counter_op_guards
{
  namespace
  {
    // Script flag word [script +0x3E0] +0x20 (822FFCC8-822FFD28, 822F5134-822F51A8): bit 0x20 = a cutscene is
    // flagged; each level message 0xB9 clears it and, if it was set, sets the next of 0x40 / 0x80 / 0x100. The flag
    // handler (82302ABC-82302B10) deletes the IndoorGuard01 spawners' guards when 0x100 is newly set (0x200 clear).
    // A restart at the mid-level checkpoint presets 0x40 | 0x80 | 0x20 (822FAC58-822FAC88).
    constexpr uint32_t kStageWord = 0x20;
    constexpr uint32_t kCutsceneFlag = 0x20, kStage1 = 0x40, kStage2 = 0x80, kStage3 = 0x100;
    constexpr uint32_t kFirstHalfSpawners[] = {0x5A4, 0x5D8, 0x60C, 0x640};
  } // namespace

  uint32_t DatacoreStageWord(const uint8_t *base)
  {
    const uint32_t script = DatacoreScript(base);
    const uint32_t flags = script ? Load32(base, script + kScriptFlags) : 0;
    return Ptr(flags) ? Load32(base, flags + kStageWord) : 0;
  }

  DatacorePhase Phase(const uint8_t *base)
  {
    const uint32_t script = DatacoreScript(base);
    const uint32_t flags = script ? Load32(base, script + kScriptFlags) : 0;
    if (!Ptr(flags))
    {
      return DatacorePhase::kNone;
    }
    const uint32_t w = Load32(base, flags + kStageWord);
    if (w & kStage3)
    {
      return DatacorePhase::kEndArea;
    }
    if ((w & (kStage1 | kStage2 | kCutsceneFlag)) == (kStage1 | kStage2 | kCutsceneFlag))
    {
      return DatacorePhase::kMidCutscene;
    }
    return DatacorePhase::kFirstHalf;
  }

  const char *PhaseName(DatacorePhase p)
  {
    switch (p)
    {
    case DatacorePhase::kFirstHalf:
      return "first half";
    case DatacorePhase::kMidCutscene:
      return "mid-level cutscene";
    case DatacorePhase::kEndArea:
      return "end area";
    default:
      return "not Datacore";
    }
  }

  bool IsFirstHalfSpawner(const uint8_t *base, uint32_t spawner)
  {
    const uint32_t script = DatacoreScript(base);
    if (!script)
    {
      return false;
    }
    for (const uint32_t off : kFirstHalfSpawners)
    {
      if (spawner == script + off)
      {
        return true;
      }
    }
    return false;
  }

  // Round 7: a guard of the script's spawner +0x7AC. Round 2 called it the "alarm wave"; it is the platform's
  // jetpack riders (five Guard01-04 clones, masks 0008 / FFFF / 0020; the on-spawn callback 822F2B78 orders each to
  // mount jetpack vehicle slot + 0xB4).
  bool IsDatacoreJetpackRider(const uint8_t *base, uint32_t character)
  {
    const uint32_t script = DatacoreScript(base);
    if (!script || !Ptr(character))
    {
      return false;
    }
    const uint32_t sp = script + 0x7AC;
    if (Load32(base, sp) != kAreaSpawnerVtable)
    {
      return false;
    }
    const uint32_t count = Load32(base, sp + kSpawnerSlotCount), slots = Load32(base, sp + kSpawnerSlots);
    for (uint32_t i = 0; Ptr(slots) && i < count && i < 64; ++i)
    {
      if (Load32(base, slots + 8 * i + kSlotCharacter) == character)
      {
        return true;
      }
    }
    return false;
  }

  // Round 12 (item 6): the jetpack Rare's on-spawn callback 822F2B78 sends a rider of spawner +0x7AC to: entry
  // slot + 0xB4 (1-based) of the level's object table ([[game +0x1C] +0x2D8]: +4 count, +8 -> 8-byte entries, the
  // object in the second word), read as 822F2C3C-822F2C68 reads it. 0 = not a rider, or no such object.
  uint32_t DatacoreRiderJetpack(const uint8_t *base, uint32_t character)
  {
    const uint32_t script = DatacoreScript(base);
    if (!script || !Ptr(character))
    {
      return 0;
    }
    const uint32_t sp = script + 0x7AC;
    if (Load32(base, sp) != kAreaSpawnerVtable)
    {
      return 0;
    }
    const uint32_t count = Load32(base, sp + kSpawnerSlotCount), slots = Load32(base, sp + kSpawnerSlots);
    for (uint32_t i = 0; Ptr(slots) && i < count && i < 64; ++i)
    {
      if (Load32(base, slots + 8 * i + kSlotCharacter) != character)
      {
        continue;
      }
      const uint32_t level = Load32(base, kGame + kGuardPool);
      const uint32_t table = pdz::guestmem::Ok(base, level, kLevelTarget + 4) ? Load32(base, level + kLevelTarget) : 0;
      const uint32_t index = i + 0xB4;
      if (!Ptr(table) || index > Load32(base, table + 4))
      {
        return 0;
      }
      const uint32_t entries = Load32(base, table + 8);
      const uint32_t prop = Ptr(entries) ? Load32(base, entries + 8 * index - 4) : 0;
      return Ptr(prop) ? prop : 0;
    }
    return 0;
  }

  // Round 12 (item 6): counter_op.cpp tells the pick how long player 2 has been without a body; the relax step of
  // the last successful Pick (0 = the area rule held).
  void SetPickWait(double seconds) { g_pick_wait_s = seconds; }
  double PickWait() { return g_pick_wait_s; }
  int LastPickRelaxed() { return g_last_pick_relaxed; }

  // Read from the slots themselves (not the 0.5 s map), so it is right inside Rare's delete too.
  bool IsFirstHalfGuard(const uint8_t *base, uint32_t character)
  {
    const uint32_t script = DatacoreScript(base);
    if (!script || !Ptr(character))
    {
      return false;
    }
    for (const uint32_t off : kFirstHalfSpawners)
    {
      const uint32_t sp = script + off;
      if (Load32(base, sp) != kAreaSpawnerVtable)
      {
        continue;
      }
      const uint32_t count = Load32(base, sp + kSpawnerSlotCount), slots = Load32(base, sp + kSpawnerSlots);
      for (uint32_t i = 0; Ptr(slots) && i < count && i < 64; ++i)
      {
        if (Load32(base, slots + 8 * i + kSlotCharacter) == character)
        {
          return true;
        }
      }
    }
    return false;
  }
} // namespace counter_op_guards

namespace counter_op_guards
{
  namespace
  {
    // The area a first-half spawner belongs to (the script activates it while the agent is in that area).
    int AreaOfSpawnerOffset(uint32_t offset)
    {
      switch (offset)
      {
      case 0x5A4:
        return 2;
      case 0x5D8:
        return 3;
      case 0x60C:
        return 4;
      case 0x640:
        return 5;
      default:
        return -1;
      }
    }
    bool DatacoreAreaInReach(const uint8_t *base, uint32_t spawner_offset)
    {
      const int area = AreaOfSpawnerOffset(spawner_offset);
      const int agent = DatacoreAgentArea(base);
      return area >= 0 && agent >= 0 && area <= std::max(agent, 1) + 1;
    }
  } // namespace

  // TESTING TOOL (pdz_counterop_test_datacore_stage): no script can play Datacore up to its mid-level cutscene, so the
  // script's own stage word is moved on: 1 = the mid-level cutscene is flagged (the bits a restart at the mid-level
  // checkpoint presets: 0x40 | 0x80 | 0x20), 2 = it is over (0x20 cleared, 0x100 set: Rare's flag handler then deletes
  // the first-half guards). Called from counter_op.cpp's tick.
  void DatacoreTestStage(uint8_t *base)
  {
    static int32_t done = 0;
    const int32_t want = REXCVAR_GET(pdz_counterop_test_datacore_stage);
    if (want == done)
    {
      return;
    }
    const uint32_t script = DatacoreScript(base);
    const uint32_t flags = script ? Load32(base, script + kScriptFlags) : 0;
    if (!Ptr(flags))
    {
      return;
    }
    done = want;
    const uint32_t before = Load32(base, flags + kStageWord);
    uint32_t w = before;
    if (want == 1)
    {
      w |= kStage1 | kStage2 | kCutsceneFlag;
    }
    else if (want == 2)
    {
      w = (w & ~kCutsceneFlag) | kStage1 | kStage2 | kStage3;
    }
    const uint32_t be = __builtin_bswap32(w);
    std::memcpy(base + flags + kStageWord, &be, 4);
    REXLOG_INFO("pdz_counterop: datacore: TEST TOOL: the script's stage word {:08X} -> {:08X} ({})", before, w,
                want == 1 ? "the mid-level cutscene flagged" : want == 2 ? "the mid-level cutscene over" : "unchanged");
  }

  // The area the agent is in: the highest bit set in the script's area word ([script +0x3E0] +0: bit N = she is in
  // area N; 822FDEF4-822FDF14); 0 before any; -1 = not Datacore.
  int DatacoreAgentArea(const uint8_t *base)
  {
    const uint32_t script = DatacoreScript(base);
    const uint32_t flags = script ? Load32(base, script + kScriptFlags) : 0;
    if (!Ptr(flags))
    {
      return -1;
    }
    const uint32_t word = Load32(base, flags) & 0xFF;
    int area = 0;
    for (int n = 0; n < 8; ++n)
    {
      if (word & (1u << n))
      {
        area = n;
      }
    }
    return area;
  }

  // The area of a first-half guard (2..5, by its spawner), or -1.
  int DatacoreGuardArea(const uint8_t *base, uint32_t character)
  {
    const uint32_t script = DatacoreScript(base);
    if (!script || !Ptr(character))
    {
      return -1;
    }
    for (const uint32_t off : kFirstHalfSpawners)
    {
      const uint32_t sp = script + off;
      if (Load32(base, sp) != kAreaSpawnerVtable)
      {
        continue;
      }
      const uint32_t count = Load32(base, sp + kSpawnerSlotCount), slots = Load32(base, sp + kSpawnerSlots);
      for (uint32_t i = 0; Ptr(slots) && i < count && i < 64; ++i)
      {
        if (Load32(base, slots + 8 * i + kSlotCharacter) == character)
        {
          return AreaOfSpawnerOffset(off);
        }
      }
    }
    return -1;
  }

  // The first-half area rule: her current area or the next one with guards.
  bool DatacoreAreaAllows(const uint8_t *base, uint32_t character, std::string *note)
  {
    const int area = DatacoreGuardArea(base, character), agent = DatacoreAgentArea(base);
    const bool ok = area >= 0 && agent >= 0 && area <= std::max(agent, 1) + 1;
    if (note)
    {
      *note = fmt::format("datacore area rule: Jo in area {}; the first-half guard is of area {} (its spawner): {}",
                          agent, area,
                          ok ? (area <= agent ? "allowed (her current area or one she has been through)"
                                              : "allowed (the next area with guards)")
                             : "refused (an area further ahead)");
    }
    return ok;
  }
} // namespace counter_op_guards

// Round 6 (item 3): Datacore's server-room elites. The level has exactly two placed enemies of the agent before its
// last area: the numbered originals tut0 / tut1 (body 84, heads 71 / 72; the script's Setup 822FA54C-822FA5C0 gives
// them the squads' masks 0008 / FFFF / 0010). Player 2 takes those first. When both are used up and no enemy of the
// agent is in the world, one more guard of their kind is made at one of their two posts from Rare's own template
// (Guard04 / Guard01: the same body, the same heads, the squads' guns), through the call Rare's spawners make
// (822771B0, as 82305F60 does), with the masks Setup gives tut0 / tut1. He is made only for player 2 to take.
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_extra_elite, false, "Testing",
                    "TESTING (counter-op, Datacore; default OFF: the level gives the agent only two enemies before "
                    "the hack, and a hack can't be done under fire). Before the mid-level cutscene, when both "
                    "server-room elites are used up and no enemy of the agent is in the world, one more guard of "
                    "their kind (Rare's Guard04 / Guard01 template: the same body, heads and masks) is made at one "
                    "of their two posts, out of every player's view and while the agent is within "
                    "pdz_counterop_datacore_extra_elite_m of it, for player 2 to take. Off = he waits for the last "
                    "area's squads")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_datacore_extra_elite_m, 80.0, "Gameplay",
                      "Counter-Operative on Datacore: the extra server-room elite is only made while the agent is "
                      "within this many metres of the post")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_last_resort, true, "Gameplay",
                    "Counter-Operative on Datacore's platform top (round 12): when player 2 has had no body for "
                    "pdz_counterop_datacore_last_resort_s and the only originals left are jetpack riders in their "
                    "seats, one guard of the squads' kind is made for him from Rare's template at a squad pad, out "
                    "of every player's view. Off = he waits")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_datacore_last_resort_s, 20.0, "Gameplay",
                      "Counter-Operative on Datacore: seconds without a body before the last resort is used")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_datacore_out_after_riders, true, "Gameplay",
                    "Counter-Operative on Datacore's platform top (round 14): the jetpack riders are player 2's "
                    "last bodies. No last-resort guard on foot is made for him (pdz_counterop_datacore_last_resort "
                    "is not used), a rider may be his next body right after a death in a jetpack seat, and once "
                    "no rider and no other guard is left he is out until the level ends (his half says NO MORE "
                    "DEPLOYMENTS). Off = round 13b (a guard on foot is made after "
                    "pdz_counterop_datacore_last_resort_s)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(sub_822771B0); // (spawn manager [0x82D6BFE8], class, template name, pad number) -> character or 0
REX_EXTERN(sub_82187D18); // (character): re-file it in the team lists after a team change (Setup 822FA564)
REX_EXTERN(sub_8216A5D8); // (pad number) -> pad object (+0x10 position, +0x1C rotation, +0x34 flags) or 0

namespace counter_op_guards
{
  // The level's two placed elites of the server room (numbered originals tut0 / tut1).
  bool IsDatacoreOpeningElite(const uint8_t *base, uint32_t character)
  {
    if (!DatacoreScript(base) || !Ptr(character))
    {
      return false;
    }
    const uint32_t id = SetupIdKey(base, character);
    return id == IdKey("tut0") || id == IdKey("tut1");
  }
  bool IsDatacoreExtraElite(uint32_t character) { return g.extra_elites.count(character) != 0; }

  uint32_t DatacoreExtraElite(PPCContext &ctx, uint8_t *base, uint32_t agent)
  {
    constexpr uint32_t kSpawnManager = 0x82D6BFE8, kClassGuard = 0x21;
    constexpr uint32_t kGuard04 = 0x820A0BF0, kGuard01 = 0x820A0C08; // template names (table 8208A1B4)
    constexpr uint32_t kPadPosition = 0x10, kPadFlags = 0x34;
    struct Post
    {
      uint32_t pad, name;
    };
    static constexpr Post kPosts[] = {{2433, kGuard04}, {2379, kGuard01}}; // 0.5 m / 0.6 m from tut0's / tut1's post
    static uint32_t next = 0;
    static uint32_t made = 0, last_script = 0;
    static std::chrono::steady_clock::time_point last_try{};
    static std::string last_why;
    const uint32_t script = DatacoreScript(base);
    if (!REXCVAR_GET(pdz_counterop_datacore_extra_elite) || !script || Phase(base) != DatacorePhase::kFirstHalf ||
        !Ptr(agent))
    {
      return 0;
    }
    if (script != last_script)
    {
      last_script = script;
      made = 0;
      last_why.clear();
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_try < std::chrono::seconds(3))
    {
      return 0;
    }
    last_try = now;
    std::string why;
    uint32_t result = 0;
    // The two placed elites (or an earlier extra one) still alive and not player 2's: nothing to add.
    std::vector<uint32_t> all;
    Guards(base, all);
    for (const uint32_t c : all)
    {
      const uint32_t id = SetupIdKey(base, c);
      const bool elite = id == IdKey("tut0") || id == IdKey("tut1") || g.extra_elites.count(c) != 0;
      if (elite && Alive(ctx, base, c) && !(g.taken && g.taken(c)))
      {
        why = "a server-room elite is still alive";
      }
    }
    const uint32_t owner = Load32(base, kGame + kGuardPool);
    if (why.empty() && (!CharListRoom(base) || !Ptr(owner) ||
                        Load32(base, owner + kGuardPoolOff + kGuardPoolUsed) + 8 > kGuardPoolBlocks))
    {
      why = "no room in Rare's character lists / guard pool";
    }
    for (uint32_t n = 0; why.empty() && !result && n < std::size(kPosts); ++n)
    {
      const Post &p = kPosts[(next + n) % std::size(kPosts)];
      uint32_t pad = 0;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
        frame.ctx.r3.u64 = p.pad;
        sub_8216A5D8(frame.ctx, base);
        pad = frame.ctx.r3.u32;
      }
      if (!Ptr(pad))
      {
        why = fmt::format("pad {} not found", p.pad);
        continue;
      }
      const float x = LoadFloat(base, pad + kPadPosition), y = LoadFloat(base, pad + kPadPosition + 4),
                  z = LoadFloat(base, pad + kPadPosition + 8);
      const float dx = x - LoadFloat(base, agent + kPosition), dz = z - LoadFloat(base, agent + kPosition + 8);
      const float range = float(REXCVAR_GET(pdz_counterop_datacore_extra_elite_m)) * 100.0f;
      bool seen = false;
      {
        rex::CallFrame frame(ctx); // Rare's spawners' own test: does any human see this point?
        frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
        frame.ctx.r3.u64 = pad + kPadPosition;
        sub_82153AB8(frame.ctx, base);
        seen = (frame.ctx.r3.u32 & 0xFF) != 0;
      }
      if ((Load32(base, pad + kPadFlags) & 0x90) != 0 || dx * dx + dz * dz > range * range || seen)
      {
        why = fmt::format("post {} is {} ({:.0f} m from the agent)", p.pad,
                          seen ? "in a player's view" : (Load32(base, pad + kPadFlags) & 0x90) ? "occupied" : "too far",
                          std::sqrt(dx * dx + dz * dz) / 100.0f);
        continue;
      }
      uint32_t c = 0;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
        frame.ctx.r3.u64 = Load32(base, kSpawnManager);
        frame.ctx.r4.u64 = kClassGuard;
        frame.ctx.r5.u64 = p.name;
        frame.ctx.r6.u64 = p.pad;
        sub_822771B0(frame.ctx, base);
        c = frame.ctx.r3.u32;
      }
      if (!Ptr(c) || Load32(base, c) != kGuardVtable)
      {
        why = fmt::format("Rare's template spawn gave nothing at pad {} ({:08X})", p.pad, c);
        continue;
      }
      // The masks Rare's Setup gives tut0 / tut1 (822FA550-822FA588), in its order.
      base[c + 0x80] = 0x00, base[c + 0x81] = 0x08;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
        frame.ctx.r3.u64 = c;
        sub_82187D18(frame.ctx, base);
      }
      base[c + 0x82] = 0xFF, base[c + 0x83] = 0xFF;
      base[c + 0x84] = 0x00, base[c + 0x85] = 0x10;
      Known k;
      k.eligible = g.eligible && g.eligible(c);
      k.origin = k.eligible ? Origin::kOpeningClone : Origin::kNotGuard;
      k.last[0] = x, k.last[1] = y, k.last[2] = z;
      k.last_at = now;
      g.known[c] = k;
      g.extra_elites.insert(c);
      next = (next + n + 1) % uint32_t(std::size(kPosts));
      ++made;
      REXLOG_INFO("pdz_counterop: guards: datacore: both placed server-room elites are used up: one more elite "
                  "{:08X} made from Rare's template ({}) at pad {} ({:.0f}, {:.0f}, {:.0f}), body {} / head {}, masks "
                  "0008 / FFFF / 0010, {} (#{} this level); {}",
                  c, p.name == kGuard04 ? "Guard04" : "Guard01", p.pad, x, y, z,
                  Ptr(Load32(base, c + 84)) ? Load32(base, Load32(base, c + 84) + 244) : 0,
                  Ptr(Load32(base, c + 84)) ? Load32(base, Load32(base, c + 84) + 248) : 0, k.eligible ? "eligible" : "NOT eligible", made, PoolText(base));
      result = c;
    }
    if (!result && why != last_why)
    {
      last_why = why;
      REXLOG_INFO("pdz_counterop: guards: datacore: no extra server-room elite yet: {}", why);
    }
    return result;
  }

  // Round 12 (item 6), the last resort on Datacore's platform top: every squad guard is used up and the only
  // originals left are jetpack riders already in their seats (never taken: Rare has no hand-over of a seat in the
  // air). After pdz_counterop_datacore_last_resort_s without a body one guard of the squads' kind is made for him
  // by round 6's route (Rare's template spawn 822771B0 and the squads' masks 0008 / FFFF / 0010, as
  // DatacoreExtraElite), at one of the squads' own pads (spawner +0x674's pad list, its first template): the pad
  // nearest the agent that is 8 m or more from her, not occupied, and seen by no human (82153AB8, the spawners'
  // own test). He is in no spawner slot, so the script's counts do not see him. Never during the spiderbot battle
  // (a seated rider must be left, and riders only exist on the rig), never while another original can still be
  // taken.
  // Round 14 (Tue 17 Nov): the owner's rule for Datacore's end: the riders are his last bodies. Asked on every pick
  // that found nothing: true once the riders have been made and no living rider and no other original that could be
  // taken is left: he is out until the level ends. (A rider who still flies but cannot be taken at this moment keeps
  // him waiting, not out.)
  bool g_datacore_out = false;
  bool DatacoreRidersRule(const uint8_t *base)
  {
    return REXCVAR_GET(pdz_counterop_datacore_out_after_riders) && DatacoreScript(base) != 0;
  }
  bool DatacoreOut() { return g_datacore_out; }
  void DatacoreOutUpdate(PPCContext &ctx, uint8_t *base)
  {
    static uint32_t last_script = 0, riders_seen = 0;
    static bool said = false;
    const uint32_t script = DatacoreScript(base);
    if (script != last_script)
    {
      last_script = script;
      riders_seen = 0;
      said = false;
      g_datacore_out = false;
    }
    if (!script || !REXCVAR_GET(pdz_counterop_datacore_out_after_riders) || Phase(base) != DatacorePhase::kEndArea)
    {
      g_datacore_out = false;
      return;
    }
    uint32_t riders = 0, riders_alive = 0, others = 0;
    for (const auto &[c, k] : g.known)
    {
      if (!StillExists(base, c) || Load32(base, c) != kGuardVtable)
      {
        continue;
      }
      const bool rider = IsDatacoreJetpackRider(base, c);
      riders += rider ? 1 : 0;
      const bool original = k.origin == Origin::kOpeningLive || k.origin == Origin::kDormantOriginal ||
                            k.origin == Origin::kForced || k.origin == Origin::kWokenByScript ||
                            k.origin == Origin::kOpeningClone;
      if (!original || (g.taken && g.taken(c)) || !Alive(ctx, base, c) || IsFirstHalfGuard(base, c) ||
          IsDatacoreOpeningElite(base, c))
      {
        continue;
      }
      ++(rider ? riders_alive : others);
    }
    riders_seen = std::max(riders_seen, riders);
    g_datacore_out = riders_seen > 0 && riders_alive == 0 && others == 0;
    if (g_datacore_out && !said)
    {
      said = true;
      REXLOG_INFO("pdz_counterop: guards: datacore: NO MORE DEPLOYMENTS: {} jetpack rider(s) were made in this level, "
                  "none is left alive and untaken and no other guard can be taken: player 2 is out until the level "
                  "ends (pdz_counterop_datacore_out_after_riders; no last-resort guard)",
                  riders_seen);
    }
  }

  uint32_t DatacoreLastResort(PPCContext &ctx, uint8_t *base, uint32_t agent, double waited_s)
  {
    if (REXCVAR_GET(pdz_counterop_datacore_out_after_riders))
    {
      return 0; // round 14: no guard on foot is made at Datacore's end
    }
    constexpr uint32_t kSpawnManager = 0x82D6BFE8, kClassGuard = 0x21, kVehicleVtable = 0x820CA9E0;
    constexpr uint32_t kPadPosition = 0x10, kPadFlags = 0x34, kParent = 0x5C;
    static std::chrono::steady_clock::time_point last_try{};
    static std::string last_why;
    static uint32_t made = 0, last_script = 0;
    const uint32_t script = DatacoreScript(base);
    if (!REXCVAR_GET(pdz_counterop_datacore_last_resort) || !script || !Ptr(agent) ||
        Phase(base) != DatacorePhase::kEndArea || waited_s < REXCVAR_GET(pdz_counterop_datacore_last_resort_s))
    {
      return 0;
    }
    if (script != last_script)
    {
      last_script = script;
      made = 0;
      last_why.clear();
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_try < std::chrono::seconds(3))
    {
      return 0;
    }
    last_try = now;
    std::string why;
    // (No area test: the riders are only made when the agent boards an outer lift of the platform top, long after
    // the spiderbot battle, so "a seated rider is left" below already means the rig.)
    // Who is left: seated riders, and any other original that could still be taken.
    uint32_t seated = 0, others = 0;
    for (const auto &[c, k] : g.known)
    {
      const bool original = k.origin == Origin::kOpeningLive || k.origin == Origin::kDormantOriginal ||
                            k.origin == Origin::kForced || k.origin == Origin::kWokenByScript ||
                            k.origin == Origin::kOpeningClone;
      if (!original || !StillExists(base, c) || Load32(base, c) != kGuardVtable || (g.taken && g.taken(c)) ||
          !Alive(ctx, base, c) || IsFirstHalfGuard(base, c) || IsDatacoreOpeningElite(base, c))
      {
        continue;
      }
      const uint32_t parent = Load32(base, c + kParent);
      const bool rider_seated =
          IsDatacoreJetpackRider(base, c) && Ptr(parent) && Load32(base, parent) == kVehicleVtable;
      ++(rider_seated ? seated : others);
    }
    if (why.empty() && others)
    {
      why = fmt::format("{} other original(s) can still be taken", others);
    }
    if (why.empty() && !seated)
    {
      why = "no seated jetpack rider is left either (nothing was refused for flying)";
    }
    const uint32_t owner = Load32(base, kGame + kGuardPool);
    if (why.empty() && (!CharListRoom(base) || !Ptr(owner) ||
                        Load32(base, owner + kGuardPoolOff + kGuardPoolUsed) + 8 > kGuardPoolBlocks))
    {
      why = "no room in Rare's character lists / guard pool";
    }
    const uint32_t sp = script + 0x674;
    const uint32_t pads = Load32(base, sp + 0x10), pad_count = Load32(base, sp + 0x14);
    const uint32_t templates = Load32(base, sp + 0x8);
    const uint32_t name = Ptr(templates) ? Load32(base, templates) : 0;
    if (why.empty() && (Load32(base, sp) != kAreaSpawnerVtable || !Ptr(pads) || !Ptr(name) ||
                        Load32(base, sp + 0xC) == 0 || Load32(base, templates + 4) != kClassGuard))
    {
      why = "the squads' spawner +0x674 has no usable pad list / guard template";
    }
    uint32_t result = 0;
    if (why.empty())
    {
      uint32_t best_pad = 0, best_number = 0, n_seen = 0, n_near = 0, n_busy = 0;
      float best_d = 1e30f;
      for (uint32_t p = 0; p < pad_count && p < 64; ++p)
      {
        const uint32_t number = Load32(base, pads + 4 * p);
        uint32_t pad = 0;
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
          frame.ctx.r3.u64 = number;
          sub_8216A5D8(frame.ctx, base);
          pad = frame.ctx.r3.u32;
        }
        if (!Ptr(pad))
        {
          continue;
        }
        const float dx = LoadFloat(base, pad + kPadPosition) - LoadFloat(base, agent + kPosition),
                    dy = LoadFloat(base, pad + kPadPosition + 4) - LoadFloat(base, agent + kPosition + 4),
                    dz = LoadFloat(base, pad + kPadPosition + 8) - LoadFloat(base, agent + kPosition + 8);
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (Load32(base, pad + kPadFlags) & 0x90)
        {
          ++n_busy;
          continue;
        }
        if (d < 800.0f)
        {
          ++n_near;
          continue;
        }
        if (d >= best_d)
        {
          continue;
        }
        bool seen = false;
        {
          rex::CallFrame frame(ctx); // Rare's spawners' own test: does any human see this point?
          frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
          frame.ctx.r3.u64 = pad + kPadPosition;
          sub_82153AB8(frame.ctx, base);
          seen = (frame.ctx.r3.u32 & 0xFF) != 0;
        }
        if (seen)
        {
          ++n_seen;
          continue;
        }
        best_d = d;
        best_pad = pad;
        best_number = number;
      }
      if (!best_pad)
      {
        why = fmt::format("no squad pad is free: of {} pads {} in a player's view, {} nearer than 8 m to the agent, {} "
                          "occupied",
                          pad_count, n_seen, n_near, n_busy);
      }
      else
      {
        uint32_t c = 0;
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
          frame.ctx.r3.u64 = Load32(base, kSpawnManager);
          frame.ctx.r4.u64 = kClassGuard;
          frame.ctx.r5.u64 = name;
          frame.ctx.r6.u64 = best_number;
          sub_822771B0(frame.ctx, base);
          c = frame.ctx.r3.u32;
        }
        if (!Ptr(c) || Load32(base, c) != kGuardVtable)
        {
          why = fmt::format("Rare's template spawn gave nothing at pad {} ({:08X})", best_number, c);
        }
        else
        {
          // The squads' masks (822F26A8; the same Rare's Setup gives tut0 / tut1), in round 6's order.
          base[c + 0x80] = 0x00, base[c + 0x81] = 0x08;
          {
            rex::CallFrame frame(ctx);
            frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu;
            frame.ctx.r3.u64 = c;
            sub_82187D18(frame.ctx, base);
          }
          base[c + 0x82] = 0xFF, base[c + 0x83] = 0xFF;
          base[c + 0x84] = 0x00, base[c + 0x85] = 0x10;
          Known k;
          k.eligible = g.eligible && g.eligible(c);
          k.origin = k.eligible ? Origin::kOpeningClone : Origin::kNotGuard;
          k.last[0] = LoadFloat(base, best_pad + kPadPosition);
          k.last[1] = LoadFloat(base, best_pad + kPadPosition + 4);
          k.last[2] = LoadFloat(base, best_pad + kPadPosition + 8);
          k.last_at = now;
          g.known[c] = k;
          g.extra_elites.insert(c);
          ++made;
          last_why.clear();
          REXLOG_INFO("pdz_counterop: guards: datacore: LAST RESORT #{}: player 2 has had no body for {:.0f} s and the "
                      "only originals left are {} jetpack rider(s) in their seats (never taken in the air): one "
                      "guard {:08X} of the squads' kind made from Rare's template at squad pad {} ({:.0f}, {:.0f}, "
                      "{:.0f}), {:.1f} m from the agent, seen by no player, masks 0008 / FFFF / 0010, {}; the next "
                      "pick takes him; {}",
                      made, waited_s, seated, c, best_number, k.last[0], k.last[1], k.last[2], best_d / 100.0f,
                      k.eligible ? "eligible" : "NOT eligible", PoolText(base));
          result = c;
        }
      }
    }
    if (!result && why != last_why)
    {
      last_why = why;
      REXLOG_INFO("pdz_counterop: guards: datacore: last resort not used ({:.0f} s without a body, {} seated "
                  "rider(s)): {}",
                  waited_s, seated, why);
    }
    return result;
  }
} // namespace counter_op_guards

REX_EXTERN(__imp__sub_82303588); // Datacore script vtable +0x34: (script, attacker, victim, killed byte)
REX_EXTERN(__imp__sub_82304188); // area spawner vtable [4]: delete every slot's guard (spawner, force)
bool PdzCounterOpRunning();
bool PdzCounterOpDatacoreCalm(uint32_t victim);
void PdzCounterOpDatacoreBeforeDelete(PPCContext &ctx, uint8_t *base, uint32_t spawner);

// Rare's Datacore rule: a human killed a first-half guard: the guards within 25 m turn on that human.
REX_HOOK_RAW(sub_82303588)
{
  if ((ctx.r6.u32 & 0xFF) != 0 && REXCVAR_GET(pdz_counterop_datacore_calm) && PdzCounterOpRunning() &&
      PdzCounterOpDatacoreCalm(ctx.r5.u32))
  {
    REXLOG_INFO("pdz_counterop: datacore: the replaced guard {:08X} died for player 2's death: Rare's turn-on-the-"
                "killer rule (82303588) not run for it, so the other first-half guards stay the agent's friends",
                ctx.r5.u32);
    return;
  }
  __imp__sub_82303588(ctx, base);
}

// Rare's delete of a spawner's guards (the first-half ones when the mid-level cutscene ends).
REX_HOOK_RAW(sub_82304188)
{
  if (PdzCounterOpRunning())
  {
    const uint32_t spawner = ctx.r3.u32, force = ctx.r4.u32; // the call-in runs guest code: keep the arguments
    PdzCounterOpDatacoreBeforeDelete(ctx, base, spawner);
    ctx.r3.u64 = spawner;
    ctx.r4.u64 = force;
  }
  __imp__sub_82304188(ctx, base);
}
