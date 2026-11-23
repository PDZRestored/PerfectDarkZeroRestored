// Guards' aim at Jo (Wed 18 Nov 2026, job aim120). See aim120_Wed18Nov.cpp.
#pragma once
#include <cstdint>

struct PPCContext;

namespace pdz::aim120
{
  // From the sub_82539698 hook (cut_scenarios.cpp): one NPC shot, before and after Rare's routine runs. `sp` is r1 at
  // the call (the routine's frame, 0xD0 below it, still holds its origin and aim point afterwards).
  void NpcShotBefore(uint8_t *base, uint32_t npc, uint32_t weapon);
  void NpcShotAfter(uint8_t *base, uint32_t npc, uint32_t weapon, uint32_t sp);
  // From Rare's noise writer for a hand-held gun's shot sub_825882E8 (lab_stealth.cpp): r3 the weapon (+0x5C owner).
  void GunShot(uint8_t *base, uint32_t weapon);
  // From the player damage hook sub_825AAF50 (minigame_log.cpp), before the god mode zeroes the damage.
  void PlayerDamage(uint8_t *base, uint32_t victim, uint32_t attacker, double damage);
  // From Rare's per-character target step sub_82580508 (lab_stealth.cpp), every character, every update.
  void CharacterTick(uint8_t *base, uint32_t character);
}
