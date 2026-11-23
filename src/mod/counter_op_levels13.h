// Counter-Operative round 13 (helper i): per-level pick rules and the plasma rifle's cloak. Built only with
// PDZ_COUNTEROP. Every function returns at once when its cvar is off or the level is not its own.
#pragma once

#include <cstdint>
#include <string>

struct PPCContext;

namespace counter_op_levels13
{
  // counter_op.cpp Start(): the mission (SpLevelName). Takes the snapshot of the guards the level data placed (every
  // guard Rare's character list holds at that moment: a guard made later is a spawn).
  void LevelStart(const uint8_t *base, const std::string &mission);

  // counter_op.cpp Spots() (take mode): true = this guard is never player 2's body on this level (the Laboratory:
  // only guards placed by the level data; never a spawner's clone, never an `s10*` spawn). id = the setup id key.
  bool RefuseBody(const uint8_t *base, uint32_t guard, uint32_t id);

  // counter_op.cpp Spots(), at the denied-id test: true = this guard is one of a level's cloaked wave guards the
  // owner wants playable (the Bridge's end waves, template `Endbridgespawner`: plasma rifle, cloak): its denied
  // template id does not refuse it. The guard is remembered for WaveBodyKnown.
  bool WaveBody(const uint8_t *base, uint32_t guard);
  // counter_op_guards.cpp Takeable(): a guard WaveBody accepted counts as takeable although it is a reinforcement.
  bool WaveBodyKnown(uint32_t guard);
  // counter_op.cpp Tick(), at the "no eligible guard left" test: true = more wave bodies may still come on this
  // level, so player 2 is not put out for the rest of the mission.
  bool KeepPicking();

  // counter_op.cpp SpawnAt(), take mode, before the guard is hidden: the cloak hand-over (the guard's own cloak bit is
  // cleared so the body Rare's kill leaves later is visible; a cloak left over from player 2's last life is ended).
  void OnTake(PPCContext &ctx, uint8_t *base, uint32_t p2c, uint32_t guard);

  // counter_op.cpp Tick(), every frame of a running game: one log line per change of player 2's cloak; ends a cloak
  // he still wears while he is out of the world. out = he has no body now.
  void Tick(PPCContext &ctx, uint8_t *base, uint32_t p2c, bool out);
} // namespace counter_op_levels13
