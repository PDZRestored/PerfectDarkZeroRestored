// Vent grates (every level that has them): the fall sound Rare recorded for them (lab_grate_sound.cpp).
#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace lab_grate_sound
{
  // Called from the cue-name hook (sub_82202D20) with the name asked for; returns the name to look up.
  uint32_t CueName(PPCContext &ctx, uint8_t *base, uint32_t name);

  // A prop's body was made (the prop body setup hook, sub_825E0EF0): the level's vent grates are remembered.
  void OnPropBody(uint8_t *base, uint32_t prop);

  // Once a frame (the physics update hook, sub_824E8BC8): a remembered grate that broke gets its fall sound.
  void Tick(PPCContext &ctx, uint8_t *base);

  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start (first load, mission restart, reload): the grates
  // remembered from the attempt before are forgotten at the next Tick, without a sound. Any thread.
  void LevelReset();
} // namespace lab_grate_sound
