// Counter-Operative round 13 (helper d): Trinity Infiltration's twins. Player 2 plays one of the Brothers in the
// boss fight the level opens on (counter_op_twins.cpp). Built only with PDZ_COUNTEROP.
// Every function answers false / does nothing at once unless pdz_counterop_trinity_twins is on AND the level script
// is Trinity Infiltration's (vtable 8209EB00) AND counter_op.cpp's tick has called Tick() this frame.
#pragma once

#include <cstdint>
#include <vector>

struct PPCContext;

namespace counter_op_twins
{
  // counter_op.cpp Reset(): every level boundary (start, end, restart, reload).
  void Reset();
  // counter_op.cpp Tick(), before the picks: the fight's state for this frame (and the test tool, the log lines).
  void Tick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, uint32_t current_guard, bool out, bool cutscene);
  // `character` is Brother Clay or Brother Virgil of this level (class 820BD880, the plain NPC class): the class
  // allow-ins of the dress, the keep-hidden pass, the kill step; no area wake; the health.
  bool IsTwin(uint32_t character);
  // The twin player 2 takes now: the fight is on, he has not had his twin yet, and this is the chosen one, alive.
  bool Pickable(uint32_t character);
  // Nobody but Pickable() is picked: the fight is on, or it has just ended and the post-fight guards are not up yet.
  bool FightHolds();
  // counter_op.cpp ApplyHealth(): a twin's own pools (health + the second pool) instead of the guard rule's share.
  float SpawnHealth(const uint8_t *base, uint32_t character, float hp);
  // counter_op.cpp SpawnAt(): what else the twin carried (his other gun, flashbangs), besides `gun` and `given`.
  std::vector<uint32_t> ExtraWeaponTypes(PPCContext &ctx, uint8_t *base, uint32_t character, uint32_t gun,
                                         const std::vector<uint32_t> &given);
} // namespace counter_op_twins
