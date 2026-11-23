// Counter-Operative round 4 (item 1): damage between the two humans. Built only with PDZ_COUNTEROP.
//
// What Rare does:
//  - A bullet hit runs the character hit handler 82549040, which asks the damage calculator 82236730 (a leaf) for the
//    health damage and the armour damage. 82236730 reads Rare's damage category table (gun.xml: 15 rows of 9 numbers;
//    the gun's row is its record byte +0x98) and then scales by GAME TYPE: in solo / co-op a human victim's health
//    damage is x 0.1 on Agent, x 0.3 on Secret Agent (armour damage x 0.5); in multiplayer (game type 2) a human
//    victim's health damage is x 0.5 unless it is a precise head shot, with no difficulty factor, and only the head /
//    body columns are used. This is the "1 damage" of the hand test (P99 body shot on an armoured human, Agent:
//    10 x 0.1 = 1 health, 12.5 x 0.5 = 6 armour).
//  - The shared damage routine 82546BC8 then applies the co-op "mercy" rule to a human victim (a hit that would take
//    her to <= 30 / 20 / 10 health is halved, up to three times, and every hit in the 5 s after it does nothing):
//    this is the "floor at 15" of log 054. Its stack byte (entry r1 + 0x6F) skips the rule.
//  - The player damage handler 825AAF50 never takes a human below human +7828 (0 on most levels, 1.0 on Datacore).
//
// What this file does, for a hit between the agent and player 2 of a running Counter-Op game only:
//  - pdz_counterop_pvp_damage = "mp" (default): the two numbers 82236730 hands back are replaced by the ones its own
//    multiplayer branch would have produced from the same inputs (same table, same row, same distance fall-off).
//  - "scale": Rare's co-op numbers x pdz_counterop_pvp_scale. "off": Rare's co-op numbers.
//  - "mp" / "scale": no mercy rule for the agent when player 2 hits her, her health floor is lifted for that hit, and
//    Rare's low-difficulty damage boost (82546A98) is kept out of the hit.
//
// Call-ins: cut_scenarios.cpp's 82546BC8 hook (PdzCounterOpPvpBegin / End) and counter_op.cpp ApplyHealth
// (PdzCounterOpPvpOn: 100 health, no armour). This file hooks only 82236730.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"

bool PdzCounterOpRunning();     // counter_op.cpp
uint32_t PdzCounterOpAgent();   // the agent's character while a counter-op game runs, else 0
uint32_t PdzCounterOpPlayer2(); // player 2's character while a counter-op game runs, else 0

REXCVAR_DEFINE_STRING(pdz_counterop_pvp_damage, "mp", "Gameplay",
                      "Counter-Operative: bullet damage between the agent and player 2. mp (default) = Rare's "
                      "multiplayer numbers for the gun (no difficulty scaling, no co-op mercy rule, no health floor "
                      "for the agent); scale = Rare's co-op numbers x pdz_counterop_pvp_scale (also no mercy rule and "
                      "no floor); off = Rare's co-op rules unchanged")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_pvp_scale, 2.0, "Gameplay",
                      "Counter-Operative: with pdz_counterop_pvp_damage = scale, what Rare's co-op health and armour "
                      "damage between the two humans is multiplied by")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_pvp_log, true, "Testing",
                    "Counter-Operative: log the first 60 hits between the two humans (Rare's numbers and ours)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_kill, true, "Gameplay",
                    "Counter-Operative (round 15, default on): the pounce of the spiderbot player 2 drives may land the "
                    "killing blow on the agent: it counts as his hit, so the game's co-op mercy rule (a hit that takes "
                    "her to 30 / 20 / 10 is halved, then 5 s of no damage) and Datacore's 1-health floor are not used "
                    "for it, as for his bullets. Off = the game's rules (her health stopped at 5 in a hand test). "
                    "Single player is not changed")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
uint32_t PdzCounterOpSpiderDrivenBot(); // counter_op_spider.cpp (round 15): the bot player 2 drives now, 0 = none

REX_EXTERN(__imp__sub_82236730); // the bullet damage calculator (leaf; see Calc below)
float PdzCounterOpOutpostGunScale(); // counter_op_outpost.cpp (round 14): 1 unless the hit is his gun's at the agent

#include "counter_op_pvp.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

namespace
{
  enum class Mode
  {
    Off,
    Mp,
    Scale
  };
  Mode CurrentMode()
  {
    const std::string m = REXCVAR_GET(pdz_counterop_pvp_damage);
    if (m == "mp")
    {
      return Mode::Mp;
    }
    if (m == "scale")
    {
      return Mode::Scale;
    }
    return Mode::Off; // "off" and anything unknown: Rare's rules
  }

  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
  }
  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t v = Load32(base, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  void StoreFloat(uint8_t *base, uint32_t a, float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    Store32(base, a, v);
  }

  constexpr uint32_t kGame = 0x82D6BFD8;        // game state pointer (+4 difficulty, +40 game type)
  constexpr uint32_t kDamageOwner = 0x82D6C008; // [this] + 0x1C = Rare's damage category table (82236730: 8223679C)
  constexpr uint32_t kHumanVtable = 0x820C7DE0;
  constexpr uint32_t kClassHuman = 2;       // character +0xC of a human (0x21 a guard, 0x2A a bot)
  constexpr uint32_t kParticipantOf = 6560; // human +0x19A0
  int g_last_part_class = -1;
  constexpr uint32_t kHealthFloor = 7828;   // human +0x1E94 (825AAF50: 825AAF94)
  constexpr uint32_t kPartDamageTotal = 0xA10; // participant: damage total (825AB0F0), read by the boost 82546A98
  constexpr uint32_t kOwner = 92;           // a weapon / kit: its owner (as cut_scenarios.cpp's CharacterOf)

  // Rare's constants in 82236730 (read from the TU3 image).
  constexpr float kFallOff = 0.74f;       // 820EB26C: both numbers, when the shot is past the gun's range class
  constexpr float kFarClass2 = 1000.0f;   // 8208C2F4: range class 2 (gun vtable +0x1B0 = record byte +0xB1): 10 m
  constexpr float kFarClass1 = 3500.0f;   // 820EB270: range class 1: 35 m
  constexpr float kMpHumanHealth = 0.5f;  // 820758EC: multiplayer, human victim, not a precise head shot
  constexpr float kMaxHealthDamage = 200.0f; // 8208BCF8
  constexpr float kMaxArmourDamage = 100.0f; // 820DFCEC

  // What 82236730's multiplayer branch (game type 2) returns for these inputs. Line for line from the disassembly
  // (82236738-82236A54) with the game type tests taken as "2"; the attacker is a human here, so the "a guard never
  // gets the head column" rule (82236778) does not apply.
  //   armour_class: r4 (3 for a human victim: all of him; 1 = head only, 2 = body only: guards' armour types)
  //   row: r5, the gun's damage category (gun vtable +0x1AC = record byte +0x98)
  //   range_class: r6; part: r7 (0 head, 1 body, 2+ limbs); precise: stack byte +0x57 (a human's shot fired with
  //   little spread, 82549620-82549678); armour: f1 (the victim's armour now); distance: f2 (cm)
  void CalcMp(const uint8_t *base, uint32_t armour_class, uint32_t row, uint32_t range_class, uint32_t part,
              bool precise, float armour, float distance, float &health_out, float &armour_out)
  {
    const uint32_t owner = Load32(base, kDamageOwner);
    const uint32_t table = Ptr(owner) ? Load32(base, owner + 0x1C) : 0;
    const uint32_t r = table + (row & 0xFFu) * 0x28u; // Rare's own addressing: the row's numbers are at r - 0x24 .. r - 4
    if (!Ptr(table) || !pdz::guestmem::Ok(base, r - 0x24, 0x24)) // Sun 22 Nov night (audit): no table: Rare's numbers
    {
      return;
    }
    const bool head = part == 0 && precise;
    const int col = head ? 0 : 1; // multiplayer never uses the limb column (8223681C / 8223688C)
    armour_out = armour;          // 82236798
    const bool covered = armour != 0.0f && !(armour_class == 2 && part != 1) && !(armour_class == 1 && part != 0);
    if (covered)
    {
      health_out = LoadFloat(base, r - 0x18 + uint32_t(col) * 4); // health damage through armour
      armour_out = LoadFloat(base, r - 0x0C + uint32_t(col) * 4); // armour damage
    }
    else
    {
      health_out = LoadFloat(base, r - 0x24 + uint32_t(col) * 4); // health damage, no armour
    }
    // Distance fall-off (822368A8-82236920): not for a precise head shot unless the gun is range class 2.
    const bool fall_off_applies = !(part == 0 && precise && range_class != 2);
    if (fall_off_applies &&
        ((distance > kFarClass2 && range_class == 2) || (distance > kFarClass1 && range_class == 1)))
    {
      health_out *= kFallOff;
      armour_out *= kFallOff;
    }
    // Multiplayer, human (or bot) victim (822369DC-82236A08): half the health damage, except the precise head shot.
    if (!head)
    {
      health_out *= kMpHumanHealth;
    }
    health_out = std::fmin(std::fmax(health_out, 0.0f), kMaxHealthDamage);
    armour_out = std::fmin(std::fmax(armour_out, 0.0f), kMaxArmourDamage);
  }

  int g_log_lines = 0;
} // namespace

// sub_82236730 (leaf, no frame): the bullet damage calculator, called only by the character hit handler 82549040
// (82549878). In: r4 armour class, r5 damage category row, r6 the gun's range class, r7 body part, r9 the victim's
// class id (+0xC), r10 the attacker's class id (0 = none), f1 the victim's armour, f2 the distance; stack (entry r1):
// byte +0x57 "precise shot", +0x5C -> health damage out, +0x64 -> armour damage out, +0x6C -> kind out (0 / 1 / 2 by
// difficulty, 7 above, 6 in multiplayer; 825AAF50 uses it for the lasting part of the damage).
// Counter-op: a human hit by a human is always one of the two players (there is no third human; a guard is 0x21).
REX_HOOK_RAW(sub_82236730)
{
  const bool pair = ctx.r9.u32 == kClassHuman && ctx.r10.u32 == kClassHuman && PdzCounterOpRunning();
  if (!pair)
  {
    __imp__sub_82236730(ctx, base);
    return;
  }
  const Mode mode = CurrentMode();
  // The inputs, before Rare's code changes the registers.
  const uint32_t sp = ctx.r1.u32;
  const uint32_t armour_class = ctx.r4.u32, row = ctx.r5.u32 & 0xFFu, range_class = ctx.r6.u32, part = ctx.r7.u32;
  g_last_part_class = int(part); // round 6 (item 6): the death animation of the guard player 2 was follows the hit
  const bool precise = base[sp + 0x57] != 0;
  const float armour = float(ctx.f1.f64), distance = float(ctx.f2.f64);
  const uint32_t health_ptr = Load32(base, sp + 0x5C), armour_ptr = Load32(base, sp + 0x64);
  __imp__sub_82236730(ctx, base);
  if (mode == Mode::Off || !Ptr(health_ptr) || !Ptr(armour_ptr))
  {
    return;
  }
  const float rare_health = LoadFloat(base, health_ptr), rare_armour = LoadFloat(base, armour_ptr);
  float health = rare_health, armour_damage = rare_armour;
  if (mode == Mode::Mp)
  {
    CalcMp(base, armour_class, row, range_class, part, precise, armour, distance, health, armour_damage);
  }
  else
  {
    const float scale = float(REXCVAR_GET(pdz_counterop_pvp_scale));
    health = std::fmin(std::fmax(rare_health * scale, 0.0f), kMaxHealthDamage);
    armour_damage = std::fmin(std::fmax(rare_armour * scale, 0.0f), kMaxArmourDamage);
  }
  const float gun_scale = PdzCounterOpOutpostGunScale(); // round 14: Outpost, his gun at the agent
  health *= gun_scale;
  armour_damage *= gun_scale;
  if (!std::isfinite(health) || !std::isfinite(armour_damage))
  {
    return; // keep Rare's numbers
  }
  StoreFloat(base, health_ptr, health);
  StoreFloat(base, armour_ptr, armour_damage);
  // The kind word (+0x6C) is left as Rare set it (the co-op difficulty's): 6 would make half of every hit lasting
  // damage, as in multiplayer (825AB090: table 82D2A3E0[6] = 0.5); not wanted between the two players here.
  if (REXCVAR_GET(pdz_counterop_pvp_log) && g_log_lines < 60)
  {
    ++g_log_lines;
    const uint32_t so = Load32(base, kGame);
    REXLOG_INFO("pdz_counterop: pvp hit {}: damage row {}, part {} ({}), {}, range class {}, {:.1f} m, the victim's "
                "armour {:.1f}; Rare's co-op numbers (difficulty {}): health {:.2f} armour {:.2f}; used ({}): health "
                "{:.2f} armour {:.2f}",
                g_log_lines, row, part, part == 0 ? "head" : part == 1 ? "body" : "limb",
                precise ? "precise shot" : "not a precise shot", range_class, distance / 100.0f, armour,
                Ptr(so) ? int32_t(Load32(base, so + 4)) : -1, rare_health, rare_armour,
                mode == Mode::Mp ? "multiplayer rules" : "scaled", health, armour_damage);
  }
}

// Call-in [A], before Rare's 82546BC8 (victim r3, attacker r4: the shooter's character for a bullet, 82549CA4; a
// weapon / kit for other damage, whose owner is +92).
PdzCounterOpPvpScope PdzCounterOpPvpBegin(uint8_t *base, uint32_t victim, uint32_t attacker)
{
  PdzCounterOpPvpScope scope;
  if (!PdzCounterOpRunning() || !Ptr(victim) || !Ptr(attacker) || CurrentMode() == Mode::Off)
  {
    return scope;
  }
  const uint32_t agent = PdzCounterOpAgent(), p2 = PdzCounterOpPlayer2();
  if (!Ptr(agent) || !Ptr(p2) || (victim != agent && victim != p2))
  {
    return scope;
  }
  uint32_t who = attacker;
  if (who != agent && who != p2)
  {
    const uint32_t owner = Load32(base, attacker + kOwner);
    who = owner == agent || owner == p2 ? owner : 0;
    // Round 15 (Thu 19 Nov night, play log 006 21:41: pounces took 91, 71, 51, 31, 11, then 5 and nothing more; she
    // refilled to 91). Rare's pounce hit 821B18F0 calls the victim's damage routine with the bot as the attacker and
    // 20 damage; with one mercy left the hit to 11 - 20 was halved (5.5, rounded 6: 5.0 left) and every pounce in the 5
    // s after it did nothing. Not a spiderbot rule: the co-op mercy rule (82546C0C-82546D18). The bot he drives
    // counts as him here, so its pounce gets what his bullets get (no mercy, no 1-health floor on Datacore, no
    // low-difficulty boost).
    if (!who && victim == agent && REXCVAR_GET(pdz_counterop_spiderbot_kill) && attacker == PdzCounterOpSpiderDrivenBot())
    {
      who = p2;
      static int lines = 0;
      if (lines < 20)
      {
        ++lines;
        REXLOG_INFO("pdz_counterop: spider: his spiderbot {:08X}'s pounce on the agent counts as his hit (her health {:.1f}): no "
                    "co-op mercy rule, no health floor (pdz_counterop_spiderbot_kill)",
                    attacker, LoadFloat(base, victim + 120));
      }
    }
  }
  if (!who || who == victim)
  {
    return scope; // a guard's hit, or his own grenade: Rare's rules
  }
  // The co-op mercy rule (82546C0C-82546D18) is skipped. Player 2 already gets this from the existing hook.
  scope.no_mercy = true;
  // The agent's health floor (0 on most levels; Datacore gives its humans 1.0): lifted for this hit. Player 2's own
  // floor is the counter-op "own death" mechanism (counter_op.cpp, kOwnFloor) and stays.
  if (victim == agent && Load32(base, victim) == kHumanVtable && LoadFloat(base, victim + kHealthFloor) != 0.0f)
  {
    scope.floor_at = victim + kHealthFloor;
    scope.floor_bits = Load32(base, scope.floor_at);
    StoreFloat(base, scope.floor_at, 0.0f);
  }
  // Rare's low-difficulty boost (82546A98, the routine's first call): on Agent / Secret Agent a hit of <= 20 on a
  // human whose damage total per entry of his participant's +0xE8C list is under 10 is raised by up to x 3. With the
  // total at 0 the routine leaves the hit alone (82546B7C). 825AAF50 adds this hit to the total inside the call;
  // PdzCounterOpPvpEnd puts the old total back plus that. (What the +0xE8C list counts is not known.)
  const uint32_t participant = Load32(base, victim) == kHumanVtable ? Load32(base, victim + kParticipantOf) : 0;
  if (Ptr(participant))
  {
    scope.total_at = participant + kPartDamageTotal;
    scope.total_bits = Load32(base, scope.total_at);
    StoreFloat(base, scope.total_at, 0.0f);
  }
  return scope;
}

// Call-in [A], after Rare's 82546BC8.
void PdzCounterOpPvpEnd(uint8_t *base, const PdzCounterOpPvpScope &scope)
{
  if (scope.floor_at)
  {
    Store32(base, scope.floor_at, scope.floor_bits);
  }
  if (scope.total_at)
  {
    float before;
    std::memcpy(&before, &scope.total_bits, 4);
    const float added = LoadFloat(base, scope.total_at); // this hit's damage (825AB0F0)
    StoreFloat(base, scope.total_at, std::isfinite(before) && std::isfinite(added) ? before + added : added);
  }
}

// counter_op.cpp ApplyHealth: the two humans fight on multiplayer (or scaled) numbers, so player 2 gets a full
// player's health.
// Round 6 (item 6): the damage class of the last hit between the two players (0 head, 1 body, 2 limb; -1 none).
int PdzCounterOpPvpLastPart() { return g_last_part_class; }

bool PdzCounterOpPvpOn()
{
  return CurrentMode() != Mode::Off;
}

// ==== Round 8 (item 6): friendly fire off for player 2 =================================================================
// Rare: every damage handler first asks the victim (vtable +0x2E0 = 82545698(victim, attacker); the guard's own
// 8252E2D0 jumps to it) whether the hit counts; the answer 0 skips the damage, the hurt reaction, the guard's "I was
// damaged" stimulus (which alerts him and sends him looking for the agent) and death. In multiplayer this is where a
// team mate's hit is dropped; in solo / co-op a human's hit on a friendly character always counts. For a running
// Counter-Op game a hit by player 2 (or by something he owns: its +92) on a character who counts him as a friend
// (Rare's 8251EBD0) is answered with 0. The agent and her allies are not his friends: they are hit as before.
// Round 13 (G1, pdz_counterop_allies_ignore_p2): her AI allies are no longer hit by him, nor he by them: see
// counter_op_allies.cpp (PdzCounterOpAllyHitRefused, asked first below). The agent herself is hit as before.
#include <rex/ppc/stack.h>

REXCVAR_DEFINE_BOOL(pdz_counterop_friendly_fire_off, true, "Gameplay",
                    "Counter-Operative (default on, round 8): player 2's shots, blows and explosives never hurt or "
                    "provoke a character of his own side (Rare's own 'does this hit count' answer, as multiplayer "
                    "uses it for team mates). Off = rounds 1-7: he could wound and alert his own side")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();
bool PdzCounterOpSpiderIsHis(uint32_t object); // counter_op_spider.cpp (round 11)
bool PdzCounterOpIsHis(const uint8_t *base, uint32_t object); // counter_op_unheard.cpp: player 2 or a thing of his
REXCVAR_DECLARE(bool, pdz_counterop_p2_own_side_untouchable); // counter_op_only_jo.cpp (round 12, item 8)
bool PdzCounterOpAllyHitRefused(const uint8_t *base, uint32_t victim, uint32_t source, uint32_t from); // round 13 (G1)
uint32_t PdzCounterOpRules13OwnerOf(const uint8_t *base, uint32_t object); // counter_op_allies.cpp (round 13)
bool PdzCounterOpOutpostHitRefused(const uint8_t *base, uint32_t victim, uint32_t source); // round 13 f, counter_op_outpost.cpp
REX_EXTERN(sub_8251EBD0);        // Rare's "friend?" (object, other)
REX_EXTERN(__imp__sub_82545698);
REX_HOOK_RAW(sub_82545698)
{
  const uint32_t p2c = PdzCounterOpPlayer2();
  if (PdzCounterOpSpiderIsHis(ctx.r4.u32) && ctx.r3.u32 != PdzCounterOpAgent())
  {
    static uint64_t refused = 0;
    if (++refused <= 20 || refused % 100 == 0)
    {
      REXLOG_INFO("pdz_counterop: spider: a hit of player 2's bot (or its blast) on {:08X} does not count (#{}; Rare's "
                  "hit-counts answer 82545698 = 0: no damage, no reaction, no alert): only the agent can be hurt by it",
                  ctx.r3.u32, refused);
    }
    ctx.r3.u64 = 0;
    return;
  }
  // Round 13 (G1, counter_op_allies.cpp): his hit on an ally of the agent, or an ally's hit on him, does not count.
  if (p2c && PdzCounterOpAllyHitRefused(base, ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr)))
  {
    ctx.r3.u64 = 0;
    return;
  }
  if (p2c && PdzCounterOpOutpostHitRefused(base, ctx.r3.u32, ctx.r4.u32)) { ctx.r3.u64 = 0; return; } // round 13 f: Outpost, at the hovercraft's gun only he and the agent hurt each other
  { bool PdzCounterOpTurretHitRefused(const uint8_t *, uint32_t, uint32_t, uint32_t); if (p2c && PdzCounterOpTurretHitRefused(base, ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr))) { ctx.r3.u64 = 0; return; } } // round 13b q: at a stationary turret his shots count on the agent only (counter_op_turrets.cpp, cvar)
  if (p2c && REXCVAR_GET(pdz_counterop_friendly_fire_off))
  {
    const uint32_t victim = ctx.r3.u32, source = ctx.r4.u32;
    if (Ptr(victim) && Ptr(source) && victim != p2c && victim != PdzCounterOpAgent() &&
        (source == p2c || Load32(base, source + 92) == p2c ||
         // Round 12 (item 8): also a thing of his by Rare's other owner links (a thrown or placed explosive, a blast).
         // Round 13 (G2): and the gun of a stationary turret he mans (a weapon whose owner is the turret).
         (REXCVAR_GET(pdz_counterop_p2_own_side_untouchable) &&
          (PdzCounterOpIsHis(base, source) || PdzCounterOpRules13OwnerOf(base, source) == p2c))))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = victim;
      frame.ctx.r4.u64 = p2c;
      sub_8251EBD0(frame.ctx, base);
      if (frame.ctx.r3.u32 & 0xFF)
      {
        static uint64_t dropped = 0;
        if (++dropped <= 20 || dropped % 100 == 0)
        {
          REXLOG_INFO("pdz_counterop: friendly fire: player 2's hit on {:08X} (vtable {:08X}) of his own side does "
                      "not count (#{}; Rare's hit-counts answer 82545698 = 0: no damage, no reaction, no alert)",
                      victim, Load32(base, victim), dropped);
        }
        ctx.r3.u64 = 0;
        return;
      }
    }
  }
  __imp__sub_82545698(ctx, base);
}
