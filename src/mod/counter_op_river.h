// Counter-Operative round 13 (helper h): River's lakes. Player 2 takes over the DRIVER of an enemy hovercraft; the
// craft's AI gunner stays on its gun (counter_op_river.cpp). Built only with PDZ_COUNTEROP.
// Every function answers false / does nothing at once unless the level script is River's (vtable 8209B5E4) and a
// Counter-Op game runs; the takeover itself also needs pdz_counterop_river_hovercraft.
#pragma once

#include <cstdint>
#include <string>

struct PPCContext;

namespace counter_op_river
{
  // counter_op.cpp Reset(): every level boundary.
  void Reset();
  // counter_op.cpp Tick(), before the picks: the seat watch, the test tools, the log lines.
  void Tick(PPCContext &ctx, uint8_t *base, const std::string &mission, uint32_t p1c, uint32_t p2c, bool out);
  // counter_op.cpp Spots(), asked for a guard on the denied list: he is the seated driver of an enemy hovercraft
  // whose seat can be handed over now (the guard is then a spot of the id list, picked like any other).
  bool DriverTake(PPCContext &ctx, uint8_t *base, uint32_t guard);
  // counter_op.cpp SpawnAt(), where player 2 is moved to the guard: for a driver picked in his seat the spot is on
  // the craft (the seated guard's own origin is under the water line, and the lake kills a character on foot).
  void SeatSpot(const uint8_t *base, uint32_t guard, float at[3]);
  // counter_op.cpp SpawnAt(), take mode, just before the taken guard is hidden: a driver picked in his seat leaves
  // it through Rare's own removal, in the same tick as AfterSpawn's mount.
  void Unseat(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);
  // counter_op.cpp SpawnAt(), after the guard is hidden: Rare's mount puts player 2 in the driver's seat.
  void AfterSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2, uint32_t guard);
  // counter_op.cpp HideP2(), first: player 2 in a hovercraft seat leaves it through Rare's removal, so the hide is
  // that of a character on foot (his own death, a cutscene, the level's end).
  void LeaveSeat(PPCContext &ctx, uint8_t *base, uint32_t p2, const char *why);
  // Player 2 sits in (or is getting into) a hovercraft seat: his gun stays put away (no redraw).
  bool Driving(const uint8_t *base, uint32_t p2);
  // Round 13b (helper r): counter_op.cpp's Pick, asked per spot before the ordinary pick: true = this spot is a
  // hovercraft driver who may be taken now and the agent sits in a hovercraft (pdz_counterop_river_hovercraft_first).
  bool DriverFirst(const uint8_t *base, uint32_t guard);
  // Round 13b (helper r), TESTING TOOLS ONLY (both do nothing unless their test cvar is on):
  // counter_op_triggers.cpp's Setup hook: pdz_counterop_test_river_checkpoint (River from Rare's checkpoint).
  void PreSetup(uint8_t *base);
  // counter_op.cpp's hit handler hook (82549040): pdz_counterop_test_river_log's hit lines. One object at the top of
  // the hook; it reads the victim's health before and after Rare's call. Off: one bool test.
  struct HitScope
  {
    HitScope(const uint8_t *base, uint32_t victim, uint32_t attacker);
    ~HitScope();
    HitScope(const HitScope &) = delete;
    HitScope &operator=(const HitScope &) = delete;
    const uint8_t *base;
    uint32_t victim, attacker;
    float before;
    bool on;
  };
} // namespace counter_op_river
