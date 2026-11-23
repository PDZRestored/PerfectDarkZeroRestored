// Sounds chosen by a prop's model (Wed 18 Nov 2026, job bymodel): metal doors, swivel chairs, the fuse box hum
// (bymodel_sounds_Wed18Nov.cpp).
#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace bymodel_sounds
{
  // Called from the cue-name hook (sub_82202D20) with the name asked for; returns the name to look up.
  uint32_t CueName(PPCContext &ctx, uint8_t *base, uint32_t name);

  // A prop's body was made (the prop body setup hook, sub_825E0EF0): the level's fuse boxes are remembered.
  void OnPropBody(uint8_t *base, uint32_t prop);

  // Once a frame (the physics update hook, sub_824E8BC8): the fuse boxes' hum is started and stopped.
  void Tick(PPCContext &ctx, uint8_t *base);

  // A sound is released (sub_82205BD0, any thread): a hum the game released itself is forgotten.
  void OnRelease(uint32_t sound);

  // A level start (first load, mission restart, reload), from the stealth layer's clear: the fuse boxes remembered
  // from the attempt before are forgotten at the next Tick. Any thread.
  void LevelReset();

  // Thu 19 Nov: the object is a door whose model is door_rusty_door (a wooden-looking door Rare's data gives a metal
  // collision surface). Cached per door setup. The game thread.
  bool IsRustyDoor(uint8_t *base, uint32_t object);
} // namespace bymodel_sounds
