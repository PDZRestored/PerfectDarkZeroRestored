// Counter-Operative round 12 (items 8, 10, 12b): what player 2's hands may do. Built only with PDZ_COUNTEROP.
//
// [A] HIS OWN SIDE IS OUT OF HIS REACH (item 8). What Rare does (TU3 code):
//   - Every blow on a character starts in the melee routine 82158590(striker, &target out, r5 hit, r6 friends too,
//     f1 / f2 damage, r10, a callback on the stack). Its callers: a gun's melee animation event (8223AFD4: the pistol
//     whip / the butt of every gun), the unarmed moves (824FB538 / 824FB560 in 824FB4B8), NPC melee (821EE110,
//     82258A54). It asks 82157F98(striker, &direction) for the target; with one, the victim's virtual +0xD4 (a
//     character's 82547068) is called, which itself asks 'does this hit count' (vtable +0x2E0 = 82545698: round 8's
//     friendly-fire answer covers it). With no target the blow only meets the level (a pane of glass, a surface).
//   - THE DISARM is not a hit: the unarmed move with its byte +0x308 set (824FA73C: the action whose ability bit is
//     0x01000000) takes its target from the same 82157F98 (through 82158590 with r5 = r6 = 0), calls the victim's
//     +0xD4 (refused for a friend), then sets the victim's +0x187C bit 0x400 and TAKES HIS WEAPON: 82554BC8(victim,
//     taker) (824FB834), whatever side he is on. Rare's reaction 824FB848 leaves a friend alone, but the weapon is
//     gone: an unarmed guard leaves his post. Nothing of this passes 82545698, so nothing was logged.
//   Here: for player 2 the target finder 82157F98 answers 'nobody' when it found a character of his own side (never
//   the agent, never a prop), so no blow, whip, butt, punch or disarm of his reaches a friend: no damage, no reaction,
//   no weapon taken, no alert. 82554BC8 is refused for him on a friend as a second net (the network copy of the
//   disarm, 824BEAE4, comes the same way).
//   Round 13 (G1, pdz_counterop_allies_ignore_p2): 'his own side' in both hooks now also means an AI ally of the
//   agent (Jack, Jonathan, the CI soldiers ...): he cannot strike or disarm them either. The log lines below still
//   say 'of his own side' for both kinds; the masks tell them apart.
//
// [B] MISSION ITEMS (item 10). Rare's give 8254C378(character, item type, ...) is refused for player 2 when the type
//   is not a gun, grenade or mine (the GunType enum 82D21F00: 1-39 are the weapons; 40-43 flags, 44 Explosives, 45-49
//   Armour / Other). His first gives are logged with Rare's caller, so a hand test names the route of anything else.
//   Nightclub's keys are an evidence prop: see [B2] at the end of this file (the real block). Weapon pick-ups stay
//   blocked by counter_op.cpp's 82586238 hook as before.
//
// [C] HIS SPIDERBOT (item 12b). Every damage to a spiderbot ends in 825C8E68(bot, attacker, f1): from its bullet
//   handler (vtable +0xC0 = 825C8860, attacker r4), its blast handler (825C88E0) and its melee handler (825C8C78).
//   While player 2 drives a bot, damage whose attacker is a real object that is neither the agent nor a thing of
//   hers is dropped. No attacker (Rare's own kills) passes.
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"

uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();
uint32_t PdzCounterOpSpiderBot(); // counter_op_spider.cpp: the bot player 2 drives, or 0
bool PdzCounterOpIsOf(const uint8_t *base, uint32_t object, uint32_t who); // counter_op_unheard.cpp (round 12)
bool PdzCounterOpAlliesIgnoreP2();                                         // counter_op_allies.cpp (round 13, G1)
bool PdzCounterOpIsAgentAlly(const uint8_t *base, uint32_t character);     // counter_op_rules13.h

REXCVAR_DEFINE_BOOL(pdz_counterop_p2_own_side_untouchable, true, "Gameplay",
                    "Counter-Operative (round 12, default on): player 2's melee blows (pistol whip, gun butt, punch) "
                    "and his disarm find no target among his own side, and the friendly-fire rule also knows the "
                    "things he owns by Rare's owner links. Off = round 11 (only his hits were refused; a disarm took "
                    "a guard's weapon)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_p2_no_mission_items, true, "Gameplay",
                    "Counter-Operative (round 12, default on): Rare's give refuses player 2 every item that is not a "
                    "gun, grenade or mine (flags, explosives charges, the 'other' item types). Off = round 11")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_only_agent_hurts, true, "Gameplay",
                    "Counter-Operative (round 12, default on): while player 2 drives a spiderbot only the agent's "
                    "shots, blows and blasts damage it; stray fire of anyone else does nothing. Off = round 11")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t L32(const uint8_t *b, uint32_t a)
  {
    const uint8_t *p = b + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  constexpr uint32_t kClass = 0x0C;                   // object +0xC: 8 / 0x2B = props (82157F98: 821581C0-821581D4)
  constexpr uint32_t kFirstNotWeapon = 40;            // GunType enum 82D21F00: 40 = BlueFlag
} // namespace

REX_EXTERN(sub_8251EBD0); // Rare's "friend?" (object, other)

namespace
{
  bool Friend(PPCContext &ctx, uint8_t *base, uint32_t who, uint32_t other)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = who;
    frame.ctx.r4.u64 = other;
    sub_8251EBD0(frame.ctx, base);
    return (frame.ctx.r3.u32 & 0xFF) != 0;
  }
  // A character of player 2's own side: not the agent, not himself, not a prop, and Rare's friend test says yes.
  bool OwnSide(PPCContext &ctx, uint8_t *base, uint32_t who, uint32_t p2)
  {
    if (!Ptr(who) || who == p2 || who == PdzCounterOpAgent())
    {
      return false;
    }
    const uint32_t cls = L32(base, who + kClass);
    if (cls == 8 || cls == 0x2B)
    {
      return false;
    }
    // Round 13 (G1): an AI ally of the agent is out of his reach the same way (counter_op_allies.cpp).
    return Friend(ctx, base, who, p2) || (PdzCounterOpAlliesIgnoreP2() && PdzCounterOpIsAgentAlly(base, who));
  }
} // namespace

// [A1] The melee target finder (striker r3, r4 -> direction; returns the target or 0).
REX_EXTERN(__imp__sub_82157F98);
REX_HOOK_RAW(sub_82157F98)
{
  const uint32_t striker = ctx.r3.u32, from = uint32_t(ctx.lr);
  __imp__sub_82157F98(ctx, base);
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || striker != p2 || !ctx.r3.u32 || !REXCVAR_GET(pdz_counterop_p2_own_side_untouchable))
  {
    return;
  }
  const uint32_t target = ctx.r3.u32;
  if (!OwnSide(ctx, base, target, p2))
  {
    return;
  }
  // The finder also runs while he only stands near a friend (the disarm lock, 82554A90): one line per target change.
  static uint32_t last = 0;
  static uint64_t refused = 0;
  ++refused;
  if (target != last)
  {
    last = target;
    REXLOG_INFO("pdz_counterop: melee: player 2's blow / disarm finds no target in {:08X} (vtable {:08X}) of his own "
                "side (Rare's target finder 82157F98 answered 'nobody', asked from {:08X}; #{}): no damage, no "
                "reaction, no weapon taken, no alert",
                target, L32(base, target), from, refused);
  }
  ctx.r3.u64 = 0;
}

// [A2] The disarm's take (victim r3, taker r4; nothing returned).
REX_EXTERN(__imp__sub_82554BC8);
REX_HOOK_RAW(sub_82554BC8)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (p2 && ctx.r4.u32 == p2 && REXCVAR_GET(pdz_counterop_p2_own_side_untouchable) &&
      OwnSide(ctx, base, ctx.r3.u32, p2))
  {
    static uint64_t refused = 0;
    if (++refused <= 20 || refused % 100 == 0)
    {
      REXLOG_INFO("pdz_counterop: melee: player 2's disarm of {:08X} (vtable {:08X}) of his own side is refused "
                  "(Rare's take 82554BC8 not run, asked from {:08X}; #{}): the guard keeps his weapon",
                  ctx.r3.u32, L32(base, ctx.r3.u32), uint32_t(ctx.lr), refused);
    }
    return;
  }
  __imp__sub_82554BC8(ctx, base);
}

// [B] Rare's give (character r3, item type r4, r5-r8; returns the item or 0; its own 'type 0' path returns 0).
REX_EXTERN(__imp__sub_8254C378);
REX_HOOK_RAW(sub_8254C378)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (p2 && ctx.r3.u32 == p2)
  {
    const uint32_t type = ctx.r4.u32;
    static uint32_t lines = 0;
    if (type >= kFirstNotWeapon && REXCVAR_GET(pdz_counterop_p2_no_mission_items))
    {
      static uint64_t refused = 0;
      if (++refused <= 20 || refused % 100 == 0)
      {
        REXLOG_INFO("pdz_counterop: item: a give of item type {} to player 2 is refused (not a gun, grenade or mine: "
                    "it stays for the agent; Rare's give 8254C378 asked from {:08X}; #{})",
                    type, uint32_t(ctx.lr), refused);
      }
      ctx.r3.u64 = 0;
      return;
    }
    if (++lines <= 12)
    {
      REXLOG_INFO("pdz_counterop: item: give to player 2: item type {} (asked from {:08X}; the first 12 are logged)",
                  type, uint32_t(ctx.lr));
    }
  }
  __imp__sub_8254C378(ctx, base);
}

// [C] A spiderbot takes damage (bot r3, attacker r4, f1; nothing returned).
REX_EXTERN(__imp__sub_825C8E68);
REX_HOOK_RAW(sub_825C8E68)
{
  const uint32_t bot = PdzCounterOpSpiderBot();
  if (bot && ctx.r3.u32 == bot && REXCVAR_GET(pdz_counterop_spiderbot_only_agent_hurts))
  {
    const uint32_t attacker = ctx.r4.u32, agent = PdzCounterOpAgent();
    if (Ptr(attacker) && Ptr(agent) && attacker != bot && !PdzCounterOpIsOf(base, attacker, agent))
    {
      static uint64_t refused = 0;
      if (++refused <= 20 || refused % 100 == 0)
      {
        REXLOG_INFO("pdz_counterop: spider: damage {:.1f} to player 2's bot {:08X} from {:08X} (vtable {:08X}) is "
                    "dropped: not the agent's (Rare's 825C8E68 not run, asked from {:08X}; #{})",
                    float(ctx.f1.f64), bot, attacker, L32(base, attacker), uint32_t(ctx.lr), refused);
      }
      return;
    }
  }
  __imp__sub_825C8E68(ctx, base);
}

// [B2] Round 12, second try at item 10: Rare's EVIDENCE PROP (class vtable 820C3470: Nightclub's office keys
// 'static\officekey\evidenceprop2', model niknak_keys_on_ring, at (2999, 511, 4400); Subway's briefcase is the same
// class). Its touch handler (vtable +0x94 = 82575A70: prop r3, toucher r4, humans only: +0xC == 2) collects it:
// through the pick-up object +0x420 when there is one (82498D00, which ends in 82483724), else directly; both end in
// 82575C78(prop, collector), which takes the prop out of the world and into the collector's inventory (82575DB0:
// 824F5830(collector +0x142C, prop)), shows the pick-up text, plays PICKUP_EVIDENCE_PROP and then fires the prop's
// Interact trigger (825199F0 at 82575D38) for the level script. For player 2 that trigger is dropped
// (counter_op_triggers.cpp), so the keys were gone and the script never heard of them. Both functions return at once
// for him: the prop stays where it is for the agent. Nothing changes for any other collector.
namespace
{
  bool EvidenceRefused(const uint8_t *base, uint32_t prop, uint32_t collector, uint32_t from, const char *where)
  {
    const uint32_t p2 = PdzCounterOpPlayer2();
    if (!p2 || collector != p2 || !REXCVAR_GET(pdz_counterop_p2_no_mission_items))
    {
      return false;
    }
    // The touch is asked every frame he stands on it: one line per prop, then one in 300.
    static uint32_t last_prop = 0;
    static uint64_t refused = 0;
    ++refused;
    if (prop != last_prop || refused % 300 == 0)
    {
      last_prop = prop;
      const uint32_t setup = Ptr(prop) ? L32(base, prop + 0x54) : 0;
      REXLOG_INFO("pdz_counterop: item: player 2 does not pick up the mission item {:08X} (Rare's evidence prop, setup "
                  "messages {:08X} {:08X}; {} not run, asked from {:08X}; #{}): it stays where it is for the agent",
                  prop, Ptr(setup) ? L32(base, setup + 0x1E) : 0, Ptr(setup) ? L32(base, setup + 0x22) : 0, where, from,
                  refused);
    }
    return true;
  }
} // namespace

// The evidence prop's touch handler (prop r3, toucher r4).
REX_EXTERN(__imp__sub_82575A70);
REX_HOOK_RAW(sub_82575A70)
{
  if (EvidenceRefused(base, ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr), "its touch handler 82575A70"))
  {
    return;
  }
  __imp__sub_82575A70(ctx, base);
}

// The evidence prop's collect (prop r3, collector r4, r5).
REX_EXTERN(__imp__sub_82575C78);
REX_HOOK_RAW(sub_82575C78)
{
  if (EvidenceRefused(base, ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr), "its collect 82575C78"))
  {
    return;
  }
  __imp__sub_82575C78(ctx, base);
}
