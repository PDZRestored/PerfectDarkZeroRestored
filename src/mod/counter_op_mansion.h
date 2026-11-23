// Counter-Operative round 13 (helper g): Mansion's last fight. Player 2 plays Mai Hem in the arena
// (counter_op_mansion.cpp). Built only with PDZ_COUNTEROP.
// Every function answers false / does nothing at once unless pdz_counterop_mansion_maihem is on AND the level script
// is Mansion's (vtable 820A3BD0) AND the fight in the arena is on.
#pragma once

#include <cstdint>

struct PPCContext;

namespace counter_op_mansion
{
  // counter_op.cpp Reset(): every level boundary (start, end, restart, reload).
  void Reset();
  // `character` is Mai Hem of this level's fight (or, with pdz_counterop_mansion_terracotta, one of the eight
  // terracotta warriors): the class allow-ins of the dress, the keep-hidden pass and the kill step; no area wake.
  bool IsBoss(uint32_t character);
  // Mai Hem can be taken now: the fight is on, she lives, and she is "in" (her AI script is not vanishing, gone,
  // coming back or waiting for the level script). With pdz_counterop_mansion_terracotta: or a terracotta warrior Rare
  // has woken, while she is gone.
  bool Pickable(uint32_t character);
  // counter_op.cpp SpawnAt(): the weapon type he is given for this body: a terracotta warrior is unarmed (Rare's
  // unarmed item, type 15); anybody else: `gun` unchanged.
  uint32_t GunFor(uint32_t character, uint32_t gun);
  // Nobody but Pickable() is picked: the fight in the arena is on.
  bool FightHolds();
  // counter_op.cpp ApplyHealth(): his health as Mai Hem (her share of her start health x the setting).
  float SpawnHealth(const uint8_t *base, uint32_t character, float hp);
  // counter_op.cpp's tick: true while the body player 2 has now must be handed back to Rare's AI without a death (the
  // fight starts while he is a guard elsewhere; Mai Hem reaches one of Rare's phase marks). The give-back itself is
  // counter_op.cpp's; AfterGiveBack() then puts Mai Hem where he stood with his share of her health.
  bool GiveBackNow();
  // Round 16 (item 8, pdz_counterop_mansion_instant): the give-back is for Mai Hem (he is another guard when the
  // fight begins): the next pick runs at once. Over(): Mai Hem is dead: no other body for the rest of the level.
  bool GiveBackForBoss();
  bool Over();
  void AfterGiveBack(PPCContext &ctx, uint8_t *base);
} // namespace counter_op_mansion
