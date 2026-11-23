// Counter-Operative round 2: player 2 takes over a jetpack guard and flies his jetpack through Rare's own player
// jetpack mode (the mount the Use action makes on a parked jetpack). Draft, not built. Evidence and the reasons for
// the limits: research notes: round2\e\FINDINGS.txt.
//
// Rule: a mission's jetpack guards (Trinity Escape: s21a, s21c) stay on counter_op.cpp's denied list, so no other
// path ever takes them; they are eligible ONLY through Takeable() below, and only while the guard is on foot next to
// his parked, free jetpack. A guard already flying stays Rare's AI (Rare has no way to free a jetpack's seat in the
// air: its exit test 826854F8 needs the jetpack landed). Call-in points for counter_op.cpp are marked "CALL-IN J".
#pragma once

#include <cstdint>
#include <string>

struct PPCContext;

namespace counter_op_jetpack
{
  // CALL-IN J1 (counter_op.cpp Start(), next to counter_op_guards::LevelStart): the mission (SpLevelName).
  void LevelStart(const std::string &mission);

  // CALL-IN J2 (counter_op.cpp Reset()).
  void Reset();

  // True for this mission's jetpack guard ids (packed as counter_op.cpp's SetupIdKey: first char in the top byte).
  bool Handles(uint32_t id_key);

  // CALL-IN J3 (counter_op.cpp Spots(), BEFORE the "if (id && g.denied.count(id))" test): for an id Handles() accepts,
  // the answer replaces the denied / list / Takeable tests: true = a spot (the caller fills it as a live guard with
  // by_list = true); false = skip it (++d.denied). why = one short reason for the log.
  bool Takeable(PPCContext &ctx, uint8_t *base, uint32_t guard, std::string &why);

  // Round 12 (item 6), Datacore's jetpack guards (spawner +0x7AC; counter_op.cpp Spots(), for each such guard on
  // foot that may be taken): prop = the jetpack Rare's on-spawn callback sends him to
  // (counter_op_guards::DatacoreRiderJetpack). True = that jetpack is still free: AfterSpawn() will seat player 2 on
  // it as for Trinity Escape. False = he is taken as a guard on foot (no free jetpack). why = one line for the log.
  bool DatacoreRiderTake(const uint8_t *base, uint32_t guard, uint32_t prop, std::string &why);

  // Round 13 (helper c), THE FLYING TAKEOVER (pdz_counterop_jetpack_air; JETPACK_AIR_STUDY_Sun15Nov.md).
  // AirRider: this guard sits in a jetpack seat on a level where the flying takeover is on. counter_op.cpp: the
  // Datacore side rule lets him through, and Spots() sends him to Takeable() whatever his id (the answer replaces the
  // denied / list / "gun in hand" tests: a rider's gun is put away). Takeable() then says whether his seat can be
  // handed over now: settled in flight (seat state 3, rider state 2 / 3, vehicle state 2) and solid ground within
  // pdz_counterop_jetpack_air_max_height below (the jetpack's own ground ray).
  bool AirRider(const uint8_t *base, uint32_t guard);
  // True for a guard Takeable() accepted in his seat (the spot is a seat hand-over).
  bool AirPending(uint32_t guard);
  // True on Datacore: a flying rider is picked only when no guard on foot can be (counter_op.cpp Pick()).
  bool AirLast();
  // Testing tool (pdz_counterop_test_jet_take_first): the pick takes a flying rider before any other guard.
  bool AirTestFirst();
  // counter_op.cpp SpawnAt(), take mode, after ShowP2 and just before RemoveGuard: a guard picked in his seat leaves
  // it through Rare's "rider removed at once" steps (82685800, 82685D00, seat state 0, handle cleared, 82680BB0);
  // AfterSpawn() then seats player 2 in the same tick. Does nothing for any other guard. Never to be called on its own.
  void Unseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);

  // CALL-IN J4 (counter_op.cpp SpawnAt(), at the end of step (e), take mode): when the guard was taken through
  // Takeable(), player 2 is put by the jetpack and mounted through Rare's mount (8266A8E8, seat 0, flag 0: what the
  // Use action 825D2E90 does on a parked jetpack). Returns the log line ("" = not a jetpack take).
  std::string AfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);

  // CALL-IN J5 (counter_op.cpp Tick(), the "player 2 down" branch, before __imp__sub_824573A8 runs): true = hold
  // Rare's revive this tick, because player 2 is still in a jetpack seat (Rare's exit / occupant handling still
  // running). Asks Rare's dismount once; capped at 3 s, then false (logged) so the revive is never lost.
  bool HoldRevive(PPCContext &ctx, uint8_t *base, uint32_t p2);

  // CALL-IN J6 (counter_op.cpp Tick(), every frame while started): twice a second while player 2 rides, one line
  // with the seat state, the jetpack's state and position (diagnostic for the first hand test). The ride is followed
  // by his parent link, so a ride he starts himself with Rare's Use (getting back on after Rare's exit) is logged too
  // ("gets on" / "gets off" lines). It also runs the remount check below.
  void Tick(PPCContext &ctx, uint8_t *base, uint32_t p2);

  // CALL-IN J8 (counter_op_triggers.cpp's sub_825178B0 hook, before a trigger is dropped for player 2): every press
  // of Use on a vehicle ends in Rare's Interact trigger (825D2E90 always calls 825199F0 -> 825178B0 kind 3 at
  // 825D32B8, mounted or not), so this is "player 2 pressed Use on this prop". Tick() looks 0.3 s later: seated =
  // Rare's own mount worked (logged once per ride); not seated = one line with every field Rare's CanEnter reads, and
  // with pdz_counterop_jetpack_remount (default off) the same mount Rare's Use makes, once.
  void OnInteract(const uint8_t *base, uint32_t prop, uint32_t activator, uint32_t kind);

  // The vehicle (prop) a character is attached to as a rider (character +0x5C, Rare's parent link), 0 = on foot.
  uint32_t VehicleOf(const uint8_t *base, uint32_t character);

  // Sun 22 Nov (round 17): the testing tools pdz_counterop_test_jet_destroy and pdz_counterop_test_jet_watch, every
  // frame in a level (single player too). p1 / p2 = the players' characters (p2 0 when there is none).
  void TestTools(PPCContext &ctx, uint8_t *base, uint32_t p1, uint32_t p2);

  // True while player 2 is seated in (or getting into / out of) a vehicle: counter_op.cpp must not move him with
  // 825AD148 or run HideP2 / ShowP2 then (CALL-IN J7: guard those calls with !counter_op_jetpack::Riding(base, p2)).
  bool Riding(const uint8_t *base, uint32_t p2);
} // namespace counter_op_jetpack

// Player 2's pad: buttons (and triggers) already down when he takes a guard are ignored until each is released once,
// so a press held over from the briefing, a cutscene skip or a menu can't act in play (run g: an A from the start of
// the level got him off the jetpack). Masked where the game reads the pad (XInputGetState, test_harness.cpp's hook),
// so every reader (Rare's held / pressed / exit action bits at participant +0x718 / +0x720 / +0x728, the jetpack's
// controls, the controller object at participant +0x690) sees the button up until it really is up. Counter-Op only:
// nothing is masked until counter_op.cpp arms it for player 2's pad.
namespace counter_op_pad
{
  // CALL-IN P1 (counter_op.cpp SpawnAt(), after step (e) / AfterSpawn; and optionally where Tick() logs "cutscene
  // ends"): mask what player 2's pad holds now (his pad = participant +0x690, the XInputGetState user).
  void Arm(const uint8_t *base, uint32_t p2, const char *why);

  // Nothing masked (CALL-IN P2: already made by counter_op_jetpack::Reset(), CALL-IN J2).
  void Reset();

  // CALL-IN P3 (test_harness.cpp, the sub_82AD4A18 XInputGetState hook, right after ApplyPadScript and before
  // PdzAutopilotPad / mnk_layout / zoom, under #if PDZ_COUNTEROP): drops the masked buttons from the state the game
  // gets; a masked button leaves the mask once the pad (or the input script) reports it up.
  void OnPadState(uint8_t *base, uint32_t user, uint32_t state, uint32_t result);

  // Round 4 (item 4): milliseconds since player 2's pad last had D-pad Up down (Rare's "put the weapon away" button
  // for a human), or -1 when never / no pad armed.
  int64_t MsSinceUp();

  // Round 4 (item 4): while on, D-pad Up (Rare's put-away for a human) and D-pad Down (Rare's drop-the-weapon) are
  // dropped from player 2's pad: a guard always holds his weapon. counter_op.cpp turns it on while he is in the world
  // as a guard and off while he is out, in a cutscene or the game isn't Counter-Op.
  void BlockDpad(bool on);
  void BlockY(int32_t user); // round 11
  // Round 7: a pad's last polled buttons (| 0x10000 left trigger, | 0x20000 right trigger) and left stick (-1..1).
  uint32_t Polled(uint32_t user, float &lx, float &ly);
} // namespace counter_op_pad
