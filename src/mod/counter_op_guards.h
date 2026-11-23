// Counter-Operative round 2, the guard side (research notes: round2\a\FINDINGS.md): which guards player 2
// may take (the level's ORIGINAL enemies, never reinforcement waves), forcing the original guards a level keeps
// dormant into play at the start, Rare's own "can the agent see this point" test, and the first-spawn / respawn
// pick. counter_op.cpp keeps its id lists, the take, the kill and everything else; it calls in at the points
// marked "CALL-IN" below (see the patch notes in FINDINGS.md, section 7).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct PPCContext;

namespace counter_op_guards
{
  // Where a guard came from (decided once, the first time the scan sees it).
  enum class Origin : uint8_t
  {
    kNotGuard = 0,      // not an SP guard, or an ally / third party / non-combatant (the caller's predicate said no)
    kOpeningLive,       // a numbered setup, live when the opening was settled: the level's opening enemies
    kDormantOriginal,   // a numbered setup the level script keeps dormant (Rare wakes it later)
    kForced,            // a dormant original woken by us at the start (pdz_counterop_force_originals)
    kWokenByScript,     // a dormant original Rare's level script woke during play
    kOpeningClone,      // a clone of a spawn template created before the opening was settled (scripted opening)
    kReinforcement,     // a clone created after that: alarm / spawner waves; never eligible
  };

  const char *OriginName(Origin o);

  // One candidate for a pick: built by counter_op.cpp from its spots (live guards only, eligible by its id lists).
  struct Candidate
  {
    uint32_t character = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float distance = 0.0f; // cm from the agent
    bool calm = true;      // counter_op.cpp's calm test (not alerted, not fighting, never shot)
    // Filled by Pick*: Rare's view cone of the agent + a clear line of sight to the spot (1.6 m up).
    bool seen = false;
    Origin origin = Origin::kNotGuard;
    bool rider = false; // round 13 c: in a jetpack seat, his seat can be handed over (counter_op_jetpack: AirPending)
  };

  // CALL-IN 1 (counter_op.cpp Start(), after the id lists are parsed): the mission (SpLevelName) and the caller's
  // eligibility test for a guard character (team 0, combat type, the mission's eligible id list / not denied: the
  // same test Spots() applies, minus the alive / taken checks). Takes the first snapshot.
  void LevelStart(PPCContext &ctx, uint8_t *base, const std::string &mission,
                  std::function<bool(uint32_t character)> eligible);

  // CALL-IN 2 (counter_op.cpp Tick(), every frame after Start, before the hide / pick code): rescans at most twice a
  // second, settles the opening once the first cutscene is over (or 3 s after Start without one) and then forces the
  // dormant originals into play (once), tracks script wake-ups and clone waves. Cheap otherwise.
  void Tick(PPCContext &ctx, uint8_t *base, uint32_t agent, bool in_cutscene);

  // CALL-IN 3 (counter_op.cpp Reset()).
  void Reset();

  // CALL-IN 8 (counter_op.cpp Start(), once, any time; kept across LevelStart): counter_op.cpp's taken test
  // (g.taken.count(c) != 0). Datacore's forced spawner guards that player 2 has taken are never put back into the
  // world by us (Rare's own area stash / activate may still touch them: see round2\d\FINDINGS.txt, Datacore risks).
  void SetTakenTest(std::function<bool(uint32_t character)> taken);

  // CALL-IN 9 (counter_op.cpp, before it touches a guard it keeps a pointer to: the taken guard on restore / kill,
  // its replace list): false = a Datacore spawner guard Rare has DELETED (its spawner's slot no longer names it,
  // e.g. the IndoorGuard spawners at the third stage flag, 82302ADC). The pointer is freed memory: drop it, never
  // restore / kill it; player 2 respawns as on a death. True for every other character.
  bool StillExists(const uint8_t *base, uint32_t character);

  // True once the opening is settled (the first pick should wait for it: CALL-IN 4 in TryPick()).
  bool OpeningSettled();

  // The rule (CALL-IN 5 in Spots(): skip a guard for which Takeable() is false; skip dormant guards altogether).
  Origin OriginOf(uint32_t character);
  bool Takeable(uint32_t character); // an original (opening live, dormant original woken, forced, opening clone)

  // A clone of a spawn template (its setup's +44 name is not "_noname_": Rare's loader 82279B90 keeps such records
  // as templates and deletes their own object, so only clones carry them).
  bool IsTemplateClone(const uint8_t *base, uint32_t character);

  // Rare's own visibility pieces (82153AB8 without its loop over every human): the agent's view cone (82153890:
  // his eye = position + (0, +0x1678, 0), his FOV through vtable +0x1C) and a clear line of sight (82259B80 with
  // mask 0x28 and the "any hit" visitor 820906BC) to (x, y + 160, z): 160 cm up, as Rare's spawners test a pad.
  bool AgentSees(PPCContext &ctx, uint8_t *base, uint32_t agent, float x, float y, float z);
  // Sat 21 Nov: the same line test without the view cone (y as given).
  bool LineClear(PPCContext &ctx, uint8_t *base, uint32_t agent, float x, float y, float z);

  // CALL-IN 6 (counter_op.cpp Pick()): returns the index of the chosen candidate or -1, and the rule step's name.
  // first = the first spawn of the mission (opening enemies, in or near the opening action, not in the agent's
  // face); otherwise a respawn (any original, out of the agent's view first).
  // Round 3: patient = the first spawn's first seconds, when only the opening enemies of the agent's area are tried
  // (out of her view, or at the edge of it); why is set on a failure too (what was there).
  int Pick(PPCContext &ctx, uint8_t *base, uint32_t agent, std::vector<Candidate> &candidates, bool first,
           bool patient, std::string &why);

  // Originals still alive and not taken (live + dormant): 0 = player 2 is out for the rest of the mission.
  // taken: counter_op.cpp's g.taken (replaced guards).
  uint32_t OriginalsLeft(PPCContext &ctx, uint8_t *base, const std::function<bool(uint32_t)> &taken);
} // namespace counter_op_guards

// Round 4 (item 2), counter_op_wake.cpp: the level's wake groups (which sleeping guards the level wakes together, and
// from which trigger volumes).
namespace counter_op_guards
{
  // Round 5 (item 4), Datacore only (kNone / false on every other level).
  enum class DatacorePhase : uint8_t
  {
    kNone,        // not Datacore
    kFirstHalf,   // before the mid-level cutscene: the IndoorGuard01s exist
    kMidCutscene, // the third cutscene is flagged / playing; Rare deletes the IndoorGuard01s when it ends
    kEndArea,     // after it (stage bit 0x100): only guards hostile to the agent by Rare's masks (the squads)
  };
  DatacorePhase Phase(const uint8_t *base);
  const char *PhaseName(DatacorePhase p);
  uint32_t DatacoreStageWord(const uint8_t *base); // [script +0x3E0] +0x20, 0 when not Datacore (for the log)
  // One of the IndoorGuard01 spawners' guards (script +0x5A4 / +0x5D8 / +0x60C / +0x640), still in its slot.
  bool IsFirstHalfGuard(const uint8_t *base, uint32_t character);
  bool IsDatacoreJetpackRider(const uint8_t *base, uint32_t character); // round 7: a slot of spawner +0x7AC
  // Round 12 (item 6): the jetpack (vehicle prop) Rare's on-spawn callback sends that rider to; 0 = none.
  uint32_t DatacoreRiderJetpack(const uint8_t *base, uint32_t character);
  // Round 12 (item 6): how long player 2 has been without a body (set before Pick), and the relax step the last
  // successful Pick used (0 = the area rule held, 1 = the nearest area ahead, 2 = any eligible original).
  void SetPickWait(double seconds);
  double PickWait(); // round 15: the last value given to SetPickWait (seconds)
  int LastPickRelaxed();
  // The spawner (guest address) is one of those four of the running Datacore script.
  bool IsFirstHalfSpawner(const uint8_t *base, uint32_t spawner);
  void DatacoreTestStage(uint8_t *base); // TESTING TOOL: pdz_counterop_test_datacore_stage
  int DatacoreAgentArea(const uint8_t *base);                    // the script's area word: the area she is in
  int DatacoreGuardArea(const uint8_t *base, uint32_t character); // a first-half guard's area (2..5), -1 = none
  bool DatacoreAreaAllows(const uint8_t *base, uint32_t character, std::string *note);
  // Round 6 (item 3): one more server-room elite for player 2 when both placed ones are used up (0 = none made).
  uint32_t DatacoreExtraElite(PPCContext &ctx, uint8_t *base, uint32_t agent);
  // Round 12 (item 6): the last resort on the platform top (only seated riders left): one guard of the squads'
  // kind made for player 2 (0 = none made). waited_s = how long he has been without a body.
  uint32_t DatacoreLastResort(PPCContext &ctx, uint8_t *base, uint32_t agent, double waited_s);
  // Round 14 (Tue 17 Nov): Datacore's end, pdz_counterop_datacore_out_after_riders.
  bool DatacoreRidersRule(const uint8_t *base); // the rule is on and this is Datacore
  void DatacoreOutUpdate(PPCContext &ctx, uint8_t *base); // after a pick that found nothing
  bool DatacoreOut(); // no rider and no other guard is left: he is out until the level ends
  bool IsDatacoreOpeningElite(const uint8_t *base, uint32_t character); // tut0 / tut1
  bool IsDatacoreExtraElite(uint32_t character);                        // made by DatacoreExtraElite (testing)
} // namespace counter_op_guards

namespace counter_op_wake
{
  void Reset();
  // The level manager passed this message on to the level script (type 7 = wake an area).
  void OnLevelMessage(const uint8_t *base, uint8_t type, uint8_t arg);
  // The group the agent is heading into next (index, -1 = none within pdz_counterop_next_area_m).
  int NextGroup(const uint8_t *base, uint32_t agent, float *distance_m);
  // May this sleeping guard be taken now (his group is the agent's next one)?
  bool InNextArea(const uint8_t *base, uint32_t guard, uint32_t agent);
  // Player 2 took this sleeping guard: the others of his group are woken (queued; Step wakes a few a frame).
  void WakeGroupOf(const uint8_t *base, uint32_t guard);
  void Step(PPCContext &ctx, uint8_t *base, uint32_t taken);
  // Round 5 (item 1): areas from Rare's wake groups and squad numbers.
  void AreasOfJo(const uint8_t *base, uint32_t agent, int *current, int *next, float *trigger_m);
  std::string AreaLine(const uint8_t *base, uint32_t agent); // "Jo in area 0 'garage' (next: 1 ...)"
  // May this guard be taken now (the agent's current area, or her next one in reach)? note = the log text.
  bool AreaAllows(const uint8_t *base, uint32_t guard, uint32_t agent, bool asleep, bool next_only, std::string &note);
  // Round 12 (item 6), for the relaxed pick: the guard's area by the level's table (-1 = none, or no table);
  // liftable = the area rule's refusal of him may be lifted (never a sleeping guard the script wakes on a cue).
  int AreaOfGuard(const uint8_t *base, uint32_t guard, bool asleep, bool *liftable);
  // Player 2 took this guard (awake or asleep): every sleeping guard of his area is woken with him.
  void WakeAreaOf(const uint8_t *base, uint32_t guard);
  // Round 16: true = Step() (our wake, not the level script's) woke this guard in this level.
  bool WokeIt(uint32_t guard);
  // Sat 21 Nov: true = this level has an area table and the area rule is on (else round 4's rule applies).
  bool HasAreaTable(const uint8_t *base);
  // Sun 22 Nov (item 9): the area of the guard post nearest p (-1 = none within pdz_counterop_area_post_m, or no
  // table); post_m = that post's distance. AreaName: the table's name ("" = none).
  int AreaAtPosition(const uint8_t *base, const float p[3], float *post_m);
  const char *AreaName(const uint8_t *base, int area);
} // namespace counter_op_wake
