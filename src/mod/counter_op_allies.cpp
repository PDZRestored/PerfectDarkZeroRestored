// Counter-Operative round 13 (G1): Jo's AI friends ignore player 2 completely. Built only with PDZ_COUNTEROP.
//
// The owner's rule: the agent's allies (Jack, Jonathan, the CI soldiers, the pilot, Carrington ...) act as if player 2
// is not there, and he cannot hurt them. What Rare does, and where each route is closed (TU3 code):
//   - HIS HITS ON THEM. Every damage handler of a character first asks the victim 'does this hit count' (vtable
//     +0x2E0: a human's 82545698, a guard's 8252E2D0 which jumps to it). The answer 0 skips the damage, the hurt
//     reaction, the 'I was damaged' stimulus and death. counter_op_pvp.cpp's hook on it asks PdzCounterOpAllyHitRefused
//     below: a hit of his (or of a thing of his: weapon, grenade, mine, blast, a turret he mans) on an ally is
//     answered 0. Round 8 did the same for his own side.
//   - HIS BLOWS AND DISARM. counter_op_only_jo.cpp [A]: the melee target finder 82157F98 answers 'nobody' and the
//     disarm's take 82554BC8 is refused when the target is an ally (its OwnSide test asks PdzCounterOpIsAgentAlly).
//   - THEIR EYES. Round 10's sight test hook (82189F18, counter_op_unheard.cpp [5]) let two kinds of viewer see him:
//     his own side and the agent's side ('he is their enemy, as any guard is'). With this rule on only his own side
//     sees him; every pair record an ally would keep of him is never made, so their target pick (the seen list
//     8218B498), their 'enemy seen' lines and their fire never start. As a second net the seen-list iterator steps
//     over him for an ally viewer (a record made before the rule was switched on).
//   - THEIR EARS. Round 10 already mutes his perception slot for every listener (8218AAC8), his near misses
//     (8217A1C8) and his sound events (8218AD40): nothing to add.
//   - THEIR FIRE ON HIM. A stray bullet, grenade or turret shot of an ally that reaches him is answered 0 by the same
//     'does this hit count' hook (victim = player 2, the source's owner = an ally).
// Round 13b p (the walk-through rule default off: not proven; the gadgets rule default on; each also needs the
// rule above on):
//   - THEIR FEET (pdz_counterop_allies_walk_through_p2). Characters are solid to each other in one place only: a
//     moving character's wall check (8269C8F0 -> 8269EE40) sweeps a sphere against every object of its neighbour
//     list (825DCA28: holder [character+0x1C4], its own object holder+0x24), and each object answers through its
//     virtual +0xA0, for every character class 82544E08 ('sweep a sphere against me'). The AI's 'is my way clear'
//     rays (825DC030 mask 4 from 82190E9C / 821935A4 / 82195CAC, 825DC428 mask 0x14 from 821907CC / 82192ED4 /
//     821955DC) walk the same list and ask virtual +0x9C = 82544D88 ('ray against me'). A character has no Havok
//     body of its own. HERE: while a sweep or ray of an ally's runs, player 2 answers 'no hit'; while a sweep of
//     player 2's own runs, an ally answers 'no hit' (so neither can pin the other: he walks through them as they
//     walk through him). His own rays are not changed: his bullets and blows still stop at an ally (and do nothing).
//   - HIS FLASHBANG AND PSYCHOSIS (pdz_counterop_allies_no_p2_gadgets). Neither is a hit, so neither asks
//     82545698. Flash: the blast 825B7190(&pos, tuning, owner) runs 825B7D20(character, blast; blast+0x14 = the
//     owner) for every character in its radius; in co-op (game +0x28 != 0, 825B8274) even the owner's friends are
//     blinded (8252E0A0: +0x3120 = 10 s; +0x1874 |= 0x10; animation 0xCD; line 0x77). Psychosis: the dart's impact
//     822513D8 calls the victim's virtual +0x30C(victim, shooter) with no friend test: a guard's 82581FE8 (zeroes
//     its allies / enemies masks, line 0x79) then the base 8254B738 (timer +0x1608 += 30 s, max 40; while it runs
//     the mask virtual 8252B050 swaps friends and enemies). HERE: all three return at once when the owner / shooter
//     is player 2 (or a thing of his) and the character is an ally.
#include "counter_op_rules13.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();
bool PdzCounterOpIsHis(const uint8_t *base, uint32_t object); // counter_op_unheard.cpp: player 2 or a thing of his
void PdzCounterOpVehiclesTestTick(PPCContext &ctx, uint8_t *base, bool playing); // counter_op_vehicles.cpp
uint32_t PdzCounterOpRules13OwnerOf(const uint8_t *base, uint32_t object);       // below (round 13b p: used earlier)

REXCVAR_DEFINE_BOOL(pdz_counterop_allies_ignore_p2, true, "Gameplay",
                    "Counter-Operative (round 13, G1, default on): the agent's AI friends (Jack, Jonathan, the CI soldiers, the "
                    "pilot, Carrington) ignore player 2 completely: they never see, target or hurt him, and his "
                    "shots, blasts, blows and disarm do nothing to them (no damage, no reaction, no alert). Off = "
                    "round 12 (he can kill them and they shoot him)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_allies_walk_through_p2, true, "Gameplay",
                    "Counter-Operative (round 13b; on by default since round 14; needs pdz_counterop_allies_ignore_p2): the "
                    "agent's AI friends are not blocked by player 2's body and do not steer round it (their movement "
                    "sweep and their 'is my way clear' rays leave him out), and his own movement is not blocked by "
                    "theirs: they walk through each other. Off = round 13 (Rare's ordinary character collision)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_allies_walk_through_callback, false, "Gameplay",
                    "Counter-Operative (round 16, OFF: not working yet; needs pdz_counterop_allies_walk_through_p2): "
                    "the walk-through for the movement sweep that reaches the characters through Rare's per-object "
                    "callback 82261B18 (the one that still stops player 2 at a CI soldier). The mover is not yet "
                    "found in that query (its +12 is a move vector, not a place): on, it only logs its checks")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_allies_no_p2_gadgets, true, "Gameplay",
                    "Counter-Operative (round 13b, default on; needs pdz_counterop_allies_ignore_p2): "
                    "player 2's flashbang does not blind the agent's AI friends and his Psychosis dart does not turn "
                    "them (no blind state, no provoked stimulus, no line, no team change). Off = round 13 (Rare's "
                    "co-op rule: a flash blinds everyone, a dart turns anyone)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_p2_ally_gadget, 0, "Testing",
                     "TESTING TOOL (counter-op, round 13b): each time this goes up by one, Rare's own routines are "
                     "asked for the agent's nearest live ally with player 2 as the owner, and what changed is logged: "
                     "the flash's per-character callback (825B7D20, a flash at player 2's place), the ally's 'become "
                     "psychotic' virtual (+0x30C, as the dart's impact 822513D8 calls it) and, only while "
                     "pdz_counterop_allies_ignore_p2 is on, the disarm's take (82554BC8)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_p2_ally, 0, "Testing",
                     "TESTING TOOL (counter-op, round 13): 1 = player 2's view is turned at the agent's nearest live "
                     "ally within 60 m every frame (a script's 'p2' trigger presses then shoot the ally); 2 = the "
                     "same, and every 6 s he is put up to 4 m from that ally, towards the agent. Every 2 s one line: the ally, its masks, "
                     "health and alert words, and what Rare's 'does this hit count' answers both ways; 3 (round 13b) = "
                     "the walk test: every 12 s he is put up to 4 m from that ally, his view is kept on it (a script's "
                     "'p2 stick' then walks him into it) and every 0.5 s one line gives both places, the distance, the "
                     "smallest distance since the put and how many sweeps / rays left the other out")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_825AD3B0);        // set a human's view (character, f1 pitch, positive down, f2 yaw)
REX_EXTERN(sub_825AD148);        // move a character (character, &position, 0)
REX_EXTERN(sub_82545698);        // 'does this hit count' (victim, attacker) -> byte (hooked in counter_op_pvp.cpp)
REX_EXTERN(__imp__sub_8218B498); // the seen-list iterator (hooked in counter_op_unheard.cpp)
// Round 13b p:
REX_EXTERN(__imp__sub_825DCA28); // sphere sweep against the neighbour list (holder r3, mask r4, ...)
REX_EXTERN(__imp__sub_825DC030); // ray against the neighbour list (holder r3, mask r4, ...)
REX_EXTERN(__imp__sub_825DC428); // ray against the neighbour list, second form (holder r3, mask r4, ...)
REX_EXTERN(__imp__sub_82544E08); // a character's 'sweep a sphere against me' (character, &start, &delta, f1, r7 collector)
REX_EXTERN(__imp__sub_82544D88); // a character's 'ray against me' (character, r4, r5, r6 collector)
REX_EXTERN(__imp__sub_825B7D20); // the flash's per-character callback (character, blast; blast+0x14 = the owner)
REX_EXTERN(__imp__sub_82581FE8); // a guard's 'become psychotic' (guard, causer)
REX_EXTERN(__imp__sub_8254B738); // the base 'become psychotic' (character, causer): timer +0x1608
REX_EXTERN(sub_82554BC8);        // the disarm's take (victim, taker) (hooked in counter_op_only_jo.cpp)

namespace
{
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t L32(const uint8_t *b, uint32_t a)
  {
    const uint8_t *p = b + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint16_t L16(const uint8_t *b, uint32_t a) { return uint16_t((uint16_t(b[a]) << 8) | b[a + 1]); }
  float LF(const uint8_t *b, uint32_t a)
  {
    const uint32_t v = L32(b, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  void SF(uint8_t *b, uint32_t a, float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    b[a] = uint8_t(v >> 24);
    b[a + 1] = uint8_t(v >> 16);
    b[a + 2] = uint8_t(v >> 8);
    b[a + 3] = uint8_t(v);
  }
  constexpr uint32_t kClass = 0x0C;                                  // 2 human, 0x21 guard / NPC, 8 / 0x2B props
  constexpr uint32_t kTeamBits = 128, kAllies = 130, kEnemies = 132; // Rare's friend test 8251EBD0
  constexpr uint32_t kParent = 0x5C;                                 // a weapon / kit: its owner; a rider: his vehicle
  constexpr uint32_t kTurretGunner = 0x5C4;                          // class 0x2E (hardpoint turret): the gunner
  constexpr uint32_t kPosition = 48, kHealth = 120, kHidden = 6272;
  constexpr uint32_t kWorld = 0x82D6BFD8 + 24, kWorldFirst = 43212, kWorldEnd = 43216; // nodes: +4 object, +12 next
  constexpr uint32_t kSlots = 0x82E5D590, kSlotSize = 0x50, kSlotCount = 170, kSlotOwner = 0x04; // perception slots

  bool On() { return REXCVAR_GET(pdz_counterop_allies_ignore_p2); }

  // Rare's owner link (the friend test 8251EBD0, 8251EBF8-8251EC38): object +0x68 bit 0x20, a list at +0x70 of
  // {type, value, next}; type 5 = the owner. (As counter_op_unheard.cpp's LinkOwner.)
  uint32_t LinkOwner(const uint8_t *b, uint32_t o)
  {
    if (!(L32(b, o + 0x68) & 0x20))
    {
      return 0;
    }
    int guard = 0;
    for (uint32_t n = L32(b, o + 0x70); Ptr(n) && guard < 32; n = L32(b, n + 8), ++guard)
    {
      if (L32(b, n) == 5)
      {
        return L32(b, n + 4);
      }
    }
    return 0;
  }
  bool IsCharacterClass(uint32_t cls) { return cls == 2 || cls == 0x21 || cls == 0x2A; }

  uint64_t g_hits_refused = 0, g_fire_refused = 0, g_seen_skipped = 0;
  // Round 13b p.
  constexpr uint32_t kHolderOwner = 0x24; // neighbour holder [character+0x1C4]: its own object
  constexpr uint32_t kBlastOwner = 0x14;  // the flash's blast record: the owner
  constexpr uint32_t kBlindTimer = 0x3120, kPsychosisTimer = 0x1608, kStimulus = 0x1874;
  uint64_t g_sweeps_ally = 0, g_sweeps_his = 0, g_rays_ally = 0, g_flash_refused = 0, g_psychosis_refused = 0;
  // Whose neighbour query runs on this thread right now: 0 nobody we care about, 1 an ally's, 2 player 2's own sweep.
  thread_local int t_query = 0;
  thread_local uint32_t t_query_owner = 0;
  bool WalkOn() { return REXCVAR_GET(pdz_counterop_allies_walk_through_p2) && On(); }
  bool GadgetsOn() { return REXCVAR_GET(pdz_counterop_allies_no_p2_gadgets) && On(); }
  bool IsAllyCharacter(const uint8_t *b, uint32_t o)
  {
    if (!Ptr(o))
    {
      return false;
    }
    const uint32_t cls = L32(b, o + kClass);
    return (cls == 0x21 || cls == 0x2A) && PdzCounterOpIsAgentAlly(b, o);
  }
  bool HisThing(const uint8_t *b, uint32_t o, uint32_t p2)
  {
    return Ptr(o) && (o == p2 || PdzCounterOpIsHis(b, o) || PdzCounterOpRules13OwnerOf(b, o) == p2);
  }
  // Run 1 (09:44): holder+0x24 did not identify the mover (no sweep was ever marked). The holders are now known
  // by the characters themselves: [character+0x1C4], player 2's read live, the allies' kept in a small table that
  // the level tick refreshes twice a second.
  constexpr uint32_t kHolder = 0x1C4;
  constexpr int kAllyMax = 24;
  std::atomic<uint32_t> g_ally_holder[kAllyMax] = {}, g_ally_of_holder[kAllyMax] = {};
  uint32_t g_diag_lines = 0;
  // The kind of a neighbour query by its holder (see t_query). `sweeps` = player 2's own queries count too.
  int QueryKind(const uint8_t *b, uint32_t holder, uint32_t p2, bool sweeps, uint32_t &owner, uint32_t from)
  {
    owner = 0;
    if (!Ptr(holder))
    {
      return 0;
    }
    int kind = 0;
    if (pdz::guestmem::Ok(b, p2, kHolder + 4) && holder == L32(b, p2 + kHolder)) // Sun 22 Nov night (audit): Ok
    {
      owner = p2;
      kind = sweeps ? 2 : 0;
    }
    else
    {
      for (int i = 0; i < kAllyMax; ++i)
      {
        if (g_ally_holder[i].load(std::memory_order_relaxed) == holder)
        {
          owner = g_ally_of_holder[i].load(std::memory_order_relaxed);
          kind = 1;
          break;
        }
      }
    }
    if (owner && g_diag_lines < 6)
    {
      ++g_diag_lines;
      REXLOG_INFO("pdz_counterop: allies (diagnostic): a neighbour query of {:08X} ({}): holder {:08X}, its +0x24 "
                  "{:08X}, asked from {:08X}, kind {}",
                  owner, owner == p2 ? "player 2" : "an ally", holder, L32(b, holder + kHolderOwner), from, kind);
    }
    return kind;
  }
  struct QueryScope
  {
    int kind;
    uint32_t owner;
    QueryScope(int k, uint32_t o) : kind(t_query), owner(t_query_owner)
    {
      t_query = k;
      t_query_owner = o;
    }
    ~QueryScope()
    {
      t_query = kind;
      t_query_owner = owner;
    }
  };
} // namespace

// Sun 22 Nov night (audit): counter_op_vehicles.cpp's PdzCounterOpRules13Reset (every Counter-Op reset): the allies'
// neighbour holders of a level are never matched in the next.
void PdzCounterOpAlliesReset()
{
  for (int i = 0; i < kAllyMax; ++i)
  {
    g_ally_holder[i].store(0, std::memory_order_relaxed);
    g_ally_of_holder[i].store(0, std::memory_order_relaxed);
  }
}

bool PdzCounterOpIsAgentAlly(const uint8_t *base, uint32_t character)
{
  const uint32_t agent = PdzCounterOpAgent();
  if (!agent || !Ptr(character) || character == agent || character == PdzCounterOpPlayer2())
  {
    return false;
  }
  // Sun 22 Nov night (audit): both read only where readable.
  if (!pdz::guestmem::Ok(base, agent, kEnemies + 2) || !pdz::guestmem::Ok(base, character, kEnemies + 2))
  {
    return false;
  }
  const uint32_t cls = L32(base, character + kClass);
  if (cls == 2 || cls == 8 || cls == 0x2B || cls == 0x2E || cls == 4)
  {
    return false; // a human (there are only the two), a prop, a turret, a weapon
  }
  const uint16_t agent_team = L16(base, agent + kTeamBits);
  return (agent_team & (L16(base, character + kTeamBits) | L16(base, character + kAllies))) != 0 &&
         (agent_team & L16(base, character + kEnemies)) == 0;
}

// The character behind an object, as Rare's near miss resolves its source (8252CA0C-8252CA74): a character is
// himself; class 0x2E (a hardpoint turret): its gunner; class 0x2C: [+0xB80] +0x5C; class 4 (a weapon): its owner
// +0x5C, and when that owner is a turret, the turret's gunner; else the owner link. 0 = nobody.
uint32_t PdzCounterOpRules13OwnerOf(const uint8_t *base, uint32_t object)
{
  if (!Ptr(object))
  {
    return 0;
  }
  const uint32_t cls = L32(base, object + kClass);
  if (IsCharacterClass(cls))
  {
    return object;
  }
  if (cls == 0x2E)
  {
    return L32(base, object + kTurretGunner);
  }
  if (cls == 0x2C)
  {
    const uint32_t w = L32(base, object + 0xB80);
    return Ptr(w) ? L32(base, w + kParent) : 0;
  }
  if (cls == 4)
  {
    // Only a weapon's +0x5C is followed (Rare follows it too); any other class's +0x5C is not known to be an object.
    const uint32_t owner = L32(base, object + kParent);
    if (Ptr(owner) && L32(base, owner + kClass) == 0x2E)
    {
      return L32(base, owner + kTurretGunner); // a turret's own gun
    }
    return Ptr(owner) ? owner : 0;
  }
  return LinkOwner(base, object);
}

bool PdzCounterOpAlliesIgnoreP2() { return On() && PdzCounterOpPlayer2() != 0; }

// counter_op_pvp.cpp's 82545698 hook (victim, attacker / source). True = the hit does not count (already logged).
bool PdzCounterOpAllyHitRefused(const uint8_t *base, uint32_t victim, uint32_t source, uint32_t from)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || !On() || !Ptr(victim) || !Ptr(source))
  {
    return false;
  }
  if (victim == p2)
  {
    // An ally's bullet, grenade, blast or turret shot on him.
    const uint32_t owner = source == p2 ? 0 : PdzCounterOpRules13OwnerOf(base, source);
    if (!owner || owner == p2 || !PdzCounterOpIsAgentAlly(base, owner))
    {
      return false;
    }
    if (++g_fire_refused <= 8 || g_fire_refused % 200 == 0)
    {
      REXLOG_INFO("pdz_counterop: allies: a hit on player 2 from {:08X} (vtable {:08X}; its owner {:08X}, an ally of "
                  "the agent, masks {:04X} / {:04X} / {:04X}) does not count (#{}; Rare's hit-counts answer 82545698 "
                  "= 0, asked from {:08X}): the agent's friends cannot hurt him",
                  source, L32(base, source), owner, L16(base, owner + kTeamBits), L16(base, owner + kAllies),
                  L16(base, owner + kEnemies), g_fire_refused, from);
    }
    return true;
  }
  if (!PdzCounterOpIsAgentAlly(base, victim))
  {
    return false;
  }
  if (source != p2 && !PdzCounterOpIsHis(base, source) && PdzCounterOpRules13OwnerOf(base, source) != p2)
  {
    return false;
  }
  if (++g_hits_refused <= 12 || g_hits_refused % 200 == 0)
  {
    REXLOG_INFO("pdz_counterop: allies: player 2's hit on {:08X} (vtable {:08X}, class {}, masks {:04X} / {:04X} / "
                "{:04X}, health {:.1f}), an ally of the agent, does not count (#{}; the source {:08X}; Rare's "
                "hit-counts answer 82545698 = 0, asked from {:08X}): no damage, no reaction, no alert",
                victim, L32(base, victim), L32(base, victim + kClass), L16(base, victim + kTeamBits),
                L16(base, victim + kAllies), L16(base, victim + kEnemies), LF(base, victim + kHealth), g_hits_refused,
                source, from);
  }
  return true;
}

// counter_op_unheard.cpp's seen-list iterator hook, after Rare's call: while the answer is player 2 and the viewer
// (the iterator's slot owner) is an ally of the agent, the iterator is asked again (its cursor is already past him).
void PdzCounterOpAllySeenSkip(PPCContext &ctx, uint8_t *base, uint32_t it, uint32_t out)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || ctx.r3.u32 != p2 || !On() || !Ptr(it))
  {
    return;
  }
  const uint32_t slot = L32(base, it + 4);
  const uint32_t viewer = slot < kSlotCount ? L32(base, kSlots + slot * kSlotSize + kSlotOwner) : 0;
  if (!PdzCounterOpIsAgentAlly(base, viewer))
  {
    return;
  }
  for (int i = 0; i < 4 && ctx.r3.u32 == p2; ++i)
  {
    if (++g_seen_skipped == 1)
    {
      REXLOG_INFO("pdz_counterop: allies: player 2 is left out of the seen list of {:08X}, an ally of the agent "
                  "(Rare's iterator 8218B498; the first this session)",
                  viewer);
    }
    ctx.r3.u64 = it;
    ctx.r4.u64 = out;
    __imp__sub_8218B498(ctx, base);
  }
}

// ---- Round 13b p: their feet (pdz_counterop_allies_walk_through_p2) --------------------------------------------------
// [W1] The sphere sweep against the neighbour list (holder r3). All eight callers are movement (8269EEF4 the wall
// check, 8269ED28, 82215820, 82217274, 824FD588, 825025E8 / 8250276C / 825028B8).
REX_HOOK_RAW(sub_825DCA28)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || !WalkOn())
  {
    __imp__sub_825DCA28(ctx, base);
    return;
  }
  uint32_t owner = 0;
  const int kind = QueryKind(base, ctx.r3.u32, p2, true, owner, uint32_t(ctx.lr));
  const QueryScope scope(kind, owner);
  __imp__sub_825DCA28(ctx, base);
}

// [W2] / [W3] The two rays against the neighbour list (holder r3): only an ally's are marked.
REX_HOOK_RAW(sub_825DC030)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || !WalkOn())
  {
    __imp__sub_825DC030(ctx, base);
    return;
  }
  uint32_t owner = 0;
  const int kind = QueryKind(base, ctx.r3.u32, p2, false, owner, uint32_t(ctx.lr));
  const QueryScope scope(kind, owner);
  __imp__sub_825DC030(ctx, base);
}
REX_HOOK_RAW(sub_825DC428)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (!p2 || !WalkOn())
  {
    __imp__sub_825DC428(ctx, base);
    return;
  }
  uint32_t owner = 0;
  const int kind = QueryKind(base, ctx.r3.u32, p2, false, owner, uint32_t(ctx.lr));
  const QueryScope scope(kind, owner);
  __imp__sub_825DC428(ctx, base);
}

// [W4] A character's 'sweep a sphere against me' (character r3; returns hit 0 / 1; its only other effect is
// collector +4 = the character).
REX_HOOK_RAW(sub_82544E08)
{
  if (REXCVAR_GET(pdz_counterop_test_p2_ally) == 3)
  {
    // Diagnostic (the walk test only): the first sweeps that ask player 2 or an ally at all, with the caller.
    static uint32_t lines = 0;
    const uint32_t p2 = PdzCounterOpPlayer2(), me = ctx.r3.u32;
    if (p2 && lines < 10 && (me == p2 || IsAllyCharacter(base, me)))
    {
      ++lines;
      REXLOG_INFO("pdz_counterop: allies (diagnostic): 'sweep against me' 82544E08 asked of {:08X} ({}), from "
                  "{:08X}; the query marked on this thread: kind {}, owner {:08X}",
                  me, me == p2 ? "player 2" : "an ally", uint32_t(ctx.lr), t_query, t_query_owner);
    }
  }
  // Round 16 (item 14, pdz_counterop_allies_walk_through_callback): the Trinity Escape walk test (log t14) showed
  // player 2 stopping 0.55 m from a CI soldier with the rule on AND off: the sweep that blocks him reaches 82544E08
  // from 82261B88 (inside 82261B18, a per-object callback of a movement query, r4 = that query, r5 = query +12),
  // never through the neighbour-list walk 825DCA28 that marks t_query. The query holds no pointer to the mover (a
  // first try looked for one; another read through its words and crashed), so the mover is told by place: the
  // query's vector at +12 (taken as the sweep's start) against player 2's centre (+48) and the allies Rare has asked
  // about. Nothing is read through a query word. Log lines give the distances, for checking the reading.
  if (!t_query && uint32_t(ctx.lr) == 0x82261B88u && REXCVAR_GET(pdz_counterop_allies_walk_through_callback) &&
      WalkOn() && Ptr(ctx.r4.u32))
  {
    const uint32_t p2 = PdzCounterOpPlayer2(), me = ctx.r3.u32, query = ctx.r4.u32;
    const bool me_p2 = p2 && me == p2, me_ally = p2 && !me_p2 && IsAllyCharacter(base, me);
    static std::unordered_set<uint32_t> s_allies;
    static uint32_t s_allies_p2 = 0;
    static uint32_t s_lines = 0, s_hits = 0;
    if (s_allies_p2 != p2)
    {
      s_allies.clear();
      s_allies_p2 = p2;
      s_lines = 0;
    }
    if (me_ally && s_allies.size() < 512)
    {
      s_allies.insert(me);
    }
    if (me_p2 || me_ally)
    {
      const float qx = LF(base, query + 12), qy = LF(base, query + 16), qz = LF(base, query + 20);
      const auto dist = [&](uint32_t c)
      {
        const float dx = LF(base, c + kPosition) - qx, dy = LF(base, c + kPosition + 4) - qy,
                    dz = LF(base, c + kPosition + 8) - qz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
      };
      uint32_t mover = 0;
      float best = 1e30f;
      if (std::isfinite(qx) && std::isfinite(qy) && std::isfinite(qz))
      {
        if (me_ally)
        {
          best = dist(p2);
          mover = best < 100.0f ? p2 : 0;
        }
        else
        {
          for (uint32_t a : s_allies)
          {
            if (!pdz::guestmem::Ok(base, a, kPosition + 12)) // Sun 22 Nov night (audit): a kept ally may be gone
            {
              continue;
            }
            const float d = dist(a);
            if (d < best)
            {
              best = d;
              mover = d < 100.0f ? a : 0;
            }
          }
        }
      }
      if (s_lines < 16)
      {
        ++s_lines;
        REXLOG_INFO("pdz_counterop: allies: walk-through check: 82261B18 asks {:08X} ({}); query {:08X} +12 = ({:.0f}, "
                    "{:.0f}, {:.0f}); nearest {} {:.0f} cm: {}",
                    me, me_p2 ? "player 2" : "an ally", query, qx, qy, qz, me_p2 ? "ally" : "player 2", best,
                    mover ? "taken as the mover: no hit" : "not the mover: Rare's answer");
      }
      if (mover)
      {
        if (++s_hits <= 3 || s_hits % 5000 == 0)
        {
          REXLOG_INFO("pdz_counterop: allies: a movement sweep through 82261B18 by {:08X} leaves {:08X} out (#{}, "
                      "pdz_counterop_allies_walk_through_callback): they walk through each other",
                      mover, me, s_hits);
        }
        ctx.r3.u64 = 0;
        return;
      }
    }
  }
  if (t_query)
  {
    const uint32_t p2 = PdzCounterOpPlayer2(), me = ctx.r3.u32;
    if (p2 && t_query == 1 && me == p2)
    {
      if (++g_sweeps_ally <= 3 || g_sweeps_ally % 5000 == 0)
      {
        REXLOG_INFO("pdz_counterop: allies: the movement sweep of {:08X}, an ally of the agent, leaves player 2's body "
                    "out (#{}; Rare's 'sweep against me' 82544E08 answered no hit, asked from {:08X}): it is not "
                    "blocked by him",
                    t_query_owner, g_sweeps_ally, uint32_t(ctx.lr));
      }
      ctx.r3.u64 = 0;
      return;
    }
    if (p2 && t_query == 2 && IsAllyCharacter(base, me))
    {
      if (++g_sweeps_his <= 3 || g_sweeps_his % 5000 == 0)
      {
        REXLOG_INFO("pdz_counterop: allies: player 2's own movement sweep leaves {:08X}, an ally of the agent, out "
                    "(#{}; Rare's 'sweep against me' 82544E08 answered no hit, asked from {:08X}): he is not blocked "
                    "by it",
                    me, g_sweeps_his, uint32_t(ctx.lr));
      }
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_82544E08(ctx, base);
}

// [W5] A character's 'ray against me' (character r3; returns hit 0 / 1).
REX_HOOK_RAW(sub_82544D88)
{
  if (t_query == 1 && ctx.r3.u32 && ctx.r3.u32 == PdzCounterOpPlayer2())
  {
    if (++g_rays_ally <= 3 || g_rays_ally % 5000 == 0)
    {
      REXLOG_INFO("pdz_counterop: allies: a ray of {:08X}, an ally of the agent, leaves player 2's body out (#{}; "
                  "Rare's 'ray against me' 82544D88 answered no hit, asked from {:08X}): its way is clear of him",
                  t_query_owner, g_rays_ally, uint32_t(ctx.lr));
    }
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82544D88(ctx, base);
}

// ---- Round 13b p: his flashbang and Psychosis (pdz_counterop_allies_no_p2_gadgets) -----------------------------------
// [F] The flash's per-character callback (character r3, blast r4; returns 0).
REX_HOOK_RAW(sub_825B7D20)
{
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (p2 && Ptr(ctx.r4.u32) && GadgetsOn())
  {
    const uint32_t victim = ctx.r3.u32, owner = L32(base, ctx.r4.u32 + kBlastOwner);
    if (HisThing(base, owner, p2) && IsAllyCharacter(base, victim))
    {
      if (++g_flash_refused <= 12 || g_flash_refused % 100 == 0)
      {
        REXLOG_INFO("pdz_counterop: allies: player 2's flash does nothing to {:08X} (vtable {:08X}, masks {:04X} / "
                    "{:04X} / {:04X}), an ally of the agent (#{}; the owner {:08X}; Rare's flash callback 825B7D20 not "
                    "run, asked from {:08X}): no blind state, no stimulus, no line",
                    victim, L32(base, victim), L16(base, victim + kTeamBits), L16(base, victim + kAllies),
                    L16(base, victim + kEnemies), g_flash_refused, owner, uint32_t(ctx.lr));
      }
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_825B7D20(ctx, base);
}

namespace
{
  bool PsychosisRefused(const uint8_t *base, uint32_t victim, uint32_t causer, uint32_t from, const char *where)
  {
    const uint32_t p2 = PdzCounterOpPlayer2();
    if (!p2 || !GadgetsOn() || !HisThing(base, causer, p2) || !IsAllyCharacter(base, victim))
    {
      return false;
    }
    if (++g_psychosis_refused <= 12 || g_psychosis_refused % 100 == 0)
    {
      REXLOG_INFO("pdz_counterop: allies: player 2's Psychosis does nothing to {:08X} (vtable {:08X}, masks {:04X} / "
                  "{:04X} / {:04X}), an ally of the agent (#{}; the causer {:08X}; {} not run, asked from {:08X}): no "
                  "team change, no timer, no line",
                  victim, L32(base, victim), L16(base, victim + kTeamBits), L16(base, victim + kAllies),
                  L16(base, victim + kEnemies), g_psychosis_refused, causer, where, from);
    }
    return true;
  }
} // namespace

// [P1] A guard's 'become psychotic' (guard r3, causer r4): virtual +0x30C of vtable 820C45A0.
REX_HOOK_RAW(sub_82581FE8)
{
  if (PsychosisRefused(base, ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr), "Rare's guard routine 82581FE8"))
  {
    return;
  }
  __imp__sub_82581FE8(ctx, base);
}

// [P2] The base 'become psychotic' (character r3, causer r4): the other character classes' virtual +0x30C.
REX_HOOK_RAW(sub_8254B738)
{
  if (PsychosisRefused(base, ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr), "Rare's base routine 8254B738"))
  {
    return;
  }
  __imp__sub_8254B738(ctx, base);
}

// ---- TESTING TOOL (pdz_counterop_test_p2_ally) and the tick ---------------------------------------------------------
namespace
{
  uint32_t Ask(PPCContext &ctx, uint8_t *base, uint32_t victim, uint32_t attacker)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = victim;
    frame.ctx.r4.u64 = attacker;
    frame.ctx.lr = 0;
    sub_82545698(frame.ctx, base);
    return frame.ctx.r3.u32 & 0xFF;
  }

  void AllyTestTick(PPCContext &ctx, uint8_t *base, int mode)
  {
    const uint32_t p2 = PdzCounterOpPlayer2();
    if (!p2)
    {
      return;
    }
    const float px = LF(base, p2 + kPosition), py = LF(base, p2 + kPosition + 4), pz = LF(base, p2 + kPosition + 8);
    uint32_t best = 0;
    float best_d = 6000.0f;
    int allies = 0;
    const uint32_t world = L32(base, kWorld);
    const uint32_t end = Ptr(world) ? L32(base, world + kWorldEnd) : 0;
    uint32_t node = Ptr(world) ? L32(base, world + kWorldFirst) : 0;
    for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = L32(base, node + 12))
    {
      const uint32_t o = L32(base, node + 4);
      if (!Ptr(o) || L32(base, o + kClass) != 0x21 || (L32(base, o + kHidden) & 0x10) ||
          !(LF(base, o + kHealth) > 0.0f) || !PdzCounterOpIsAgentAlly(base, o))
      {
        continue;
      }
      ++allies;
      const float dx = LF(base, o + kPosition) - px, dy = LF(base, o + kPosition + 4) - py,
                  dz = LF(base, o + kPosition + 8) - pz;
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d < best_d && (d > 80.0f || mode >= 2))
      {
        best_d = d;
        best = o;
      }
    }
    static std::chrono::steady_clock::time_point told{}, moved{};
    const auto now = std::chrono::steady_clock::now();
    // Mode 3 (round 13b, the walk test): the ally chosen at a put is kept until the next put.
    static uint32_t walk_ally = 0;
    static float walk_min = 1e9f;
    if (mode == 3 && walk_ally && now - moved < std::chrono::seconds(12))
    {
      if (L32(base, walk_ally + kClass) == 0x21 && LF(base, walk_ally + kHealth) > 0.0f &&
          !(L32(base, walk_ally + kHidden) & 0x10))
      {
        best = walk_ally;
      }
    }
    if (!best)
    {
      if (now - told >= std::chrono::seconds(4))
      {
        told = now;
        REXLOG_INFO("pdz_counterop: TEST TOOL (allies): no live ally of the agent within 60 m of player 2 ({} in the "
                    "world list)",
                    allies);
      }
      return;
    }
    const float ax = LF(base, best + kPosition), ay = LF(base, best + kPosition + 4), az = LF(base, best + kPosition + 8);
    if (mode >= 2 && now - moved >= std::chrono::seconds(mode == 3 ? 12 : 6))
    {
      moved = now;
      walk_ally = best;
      walk_min = 1e9f;
      // Up to 4 m from the ally on the line towards the agent, never past the midpoint (run 1: a spot on her far
      // side of the ally was over an edge and he fell to his death each time; the ground between two characters
      // is the safest guess).
      const uint32_t agent = PdzCounterOpAgent();
      float ox = 0.0f, oz = 0.0f;
      if (Ptr(agent))
      {
        const float gx = LF(base, agent + kPosition) - ax, gz = LF(base, agent + kPosition + 8) - az;
        const float g = std::sqrt(gx * gx + gz * gz);
        if (g > 50.0f)
        {
          const float step = g * 0.5f < 400.0f ? g * 0.5f : 400.0f;
          ox = gx / g * step;
          oz = gz / g * step;
        }
      }
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u32 -= 0x100; // our own scratch between Rare's frame and the callee's
      const uint32_t pos = frame.ctx.r1.u32 + 0xC0;
      SF(base, pos, ax + ox);
      SF(base, pos + 4, ay + 5.0f);
      SF(base, pos + 8, az + oz);
      frame.ctx.r3.u64 = p2;
      frame.ctx.r4.u64 = pos;
      frame.ctx.r5.u64 = 0;
      sub_825AD148(frame.ctx, base);
      REXLOG_INFO("pdz_counterop: TEST TOOL (allies): player 2 put 4 m from the ally {:08X} at ({:.0f}, {:.0f}, {:.0f}); "
                  "he is now at ({:.0f}, {:.0f}, {:.0f})",
                  best, ax, ay, az, LF(base, p2 + kPosition), LF(base, p2 + kPosition + 4), LF(base, p2 + kPosition + 8));
      return; // aim from the new place on the next frame
    }
    if (mode == 3)
    {
      // The walk test: his view level on the ally (kept as it is inside 0.3 m, so he walks on through), and a line
      // every 0.5 s.
      const float dx = ax - px, dz = az - pz;
      const float flat = std::sqrt(dx * dx + dz * dz);
      if (std::isfinite(flat) && flat > 30.0f)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = p2;
        frame.ctx.f1.f64 = 0.0;
        frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
        sub_825AD3B0(frame.ctx, base);
      }
      if (flat < walk_min)
      {
        walk_min = flat;
      }
      if (now - told >= std::chrono::milliseconds(500))
      {
        told = now;
        REXLOG_INFO("pdz_counterop: TEST TOOL (allies, walk test): ally {:08X} at ({:.0f}, {:.0f}, {:.0f}), player 2 "
                    "at ({:.0f}, {:.0f}, {:.0f}): {:.2f} m apart on the flat, smallest since the put {:.2f} m; "
                    "pdz_counterop_allies_walk_through_p2 is {}; left out so far: player 2 from {} sweep(s) and {} "
                    "ray(s) of allies, allies from {} sweep(s) of his",
                    best, ax, ay, az, px, py, pz, flat / 100.0f, walk_min / 100.0f, WalkOn() ? "on" : "off",
                    g_sweeps_ally, g_rays_ally, g_sweeps_his);
      }
      return;
    }
    {
      const float dx = ax - px, dz = az - pz, dy = ay + 115.0f - (py + 160.0f);
      const float flat = std::sqrt(dx * dx + dz * dz);
      if (std::isfinite(flat) && flat > 30.0f)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = p2;
        frame.ctx.f1.f64 = -std::atan2(double(dy), double(flat)); // 825AD3B0's pitch is positive down
        frame.ctx.f2.f64 = std::atan2(double(dx), double(dz));
        sub_825AD3B0(frame.ctx, base);
      }
    }
    if (now - told >= std::chrono::seconds(2))
    {
      told = now;
      const uint32_t hit = Ask(ctx, base, best, p2), back = Ask(ctx, base, p2, best);
      REXLOG_INFO("pdz_counterop: TEST TOOL (allies): player 2's view is on the agent's ally {:08X} (vtable {:08X}, "
                  "masks {:04X} / {:04X} / {:04X}) {:.1f} m away, {} allies in the world: its health {:.1f}, alert "
                  "words {:08X} / {:08X}, stimulus word (+0x1874) {:08X}; player 2's masks {:04X} / {:04X} / {:04X}; "
                  "'does his hit on it count' = {}, 'does its hit on him count' = {}; so far {} hit(s) of his on "
                  "allies refused, {} of theirs on him, {} seen-list skip(s)",
                  best, L32(base, best), L16(base, best + kTeamBits), L16(base, best + kAllies),
                  L16(base, best + kEnemies), best_d / 100.0f, allies, LF(base, best + kHealth),
                  L32(base, best + 6268), L32(base, best + 6252), L32(base, best + 0x1874), L16(base, p2 + kTeamBits),
                  L16(base, p2 + kAllies), L16(base, p2 + kEnemies), hit, back, g_hits_refused, g_fire_refused,
                  g_seen_skipped);
    }
  }

  // pdz_counterop_test_p2_ally_gadget (round 13b): Rare's flash callback, 'become psychotic' and the disarm's take,
  // asked for the nearest live ally with player 2 as the owner.
  void AllyGadgetTestTick(PPCContext &ctx, uint8_t *base)
  {
    static int32_t done = 0;
    const int32_t want = REXCVAR_GET(pdz_counterop_test_p2_ally_gadget);
    const uint32_t p2 = PdzCounterOpPlayer2();
    if (want <= done || !p2)
    {
      return;
    }
    ++done;
    const float px = LF(base, p2 + kPosition), py = LF(base, p2 + kPosition + 4), pz = LF(base, p2 + kPosition + 8);
    uint32_t ally = 0;
    float ally_d = 1e30f;
    const uint32_t world = L32(base, kWorld);
    const uint32_t end = Ptr(world) ? L32(base, world + kWorldEnd) : 0;
    uint32_t node = Ptr(world) ? L32(base, world + kWorldFirst) : 0;
    for (int i = 0; Ptr(node) && node != end && i < 16384; ++i, node = L32(base, node + 12))
    {
      const uint32_t o = L32(base, node + 4);
      if (!Ptr(o) || L32(base, o + kClass) != 0x21 || (L32(base, o + kHidden) & 0x10) ||
          !(LF(base, o + kHealth) > 0.0f) || !PdzCounterOpIsAgentAlly(base, o))
      {
        continue;
      }
      const float dx = LF(base, o + kPosition) - px, dy = LF(base, o + kPosition + 4) - py,
                  dz = LF(base, o + kPosition + 8) - pz;
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d < ally_d)
      {
        ally_d = d;
        ally = o;
      }
    }
    if (!ally)
    {
      REXLOG_INFO("pdz_counterop: TEST TOOL (allies, gadgets) #{}: no live ally of the agent in the world list", done);
      return;
    }
    const auto state = [&](const char *when) {
      REXLOG_INFO("pdz_counterop: TEST TOOL (allies, gadgets) #{}: {}: ally {:08X} (vtable {:08X}) {:.1f} m from "
                  "player 2: blind timer (+0x3120) {:.1f} s, stimulus word (+0x1874) {:08X}, Psychosis timer (+0x1608) "
                  "{:.1f} s, masks {:04X} / {:04X} / {:04X}, flag words (+0x187C) {:08X} / (+0x1884) {:08X}; "
                  "pdz_counterop_allies_no_p2_gadgets is {}; refused so far: {} flash, {} Psychosis",
                  done, when, ally, L32(base, ally), ally_d / 100.0f, LF(base, ally + kBlindTimer),
                  L32(base, ally + kStimulus), LF(base, ally + kPsychosisTimer), L16(base, ally + kTeamBits),
                  L16(base, ally + kAllies), L16(base, ally + kEnemies), L32(base, ally + 0x187C),
                  L32(base, ally + 0x1884), GadgetsOn() ? "on" : "off", g_flash_refused, g_psychosis_refused);
    };
    state("before");
    {
      // The flash: a blast record as 825B7190 builds it (position, two floats, +0x14 the owner, +0x18 its pad = none),
      // at player 2's place, 1.2 m up.
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u32 -= 0x100; // our own scratch between Rare's frame and the callee's
      const uint32_t blast = frame.ctx.r1.u32 + 0xC0;
      std::memset(base + blast, 0, 0x20);
      SF(base, blast, px);
      SF(base, blast + 4, py + 120.0f);
      SF(base, blast + 8, pz);
      SF(base, blast + 0xC, 2000.0f);
      base[blast + kBlastOwner] = uint8_t(p2 >> 24);
      base[blast + kBlastOwner + 1] = uint8_t(p2 >> 16);
      base[blast + kBlastOwner + 2] = uint8_t(p2 >> 8);
      base[blast + kBlastOwner + 3] = uint8_t(p2);
      frame.ctx.r3.u64 = ally;
      frame.ctx.r4.u64 = blast;
      frame.ctx.lr = 0;
      sub_825B7D20(frame.ctx, base);
    }
    state("after the flash callback 825B7D20(ally, blast owned by player 2)");
    {
      const uint32_t fn = L32(base, L32(base, ally) + 0x30C);
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = ally;
      frame.ctx.r4.u64 = p2;
      frame.ctx.lr = 0;
      if (fn == 0x82581FE8)
      {
        sub_82581FE8(frame.ctx, base);
      }
      else if (fn == 0x8254B738)
      {
        sub_8254B738(frame.ctx, base);
      }
      REXLOG_INFO("pdz_counterop: TEST TOOL (allies, gadgets) #{}: the ally's 'become psychotic' virtual +0x30C is "
                  "{:08X}{}",
                  done, fn,
                  fn == 0x82581FE8 || fn == 0x8254B738 ? ": asked with player 2 as the causer"
                                                       : ": NOT asked (unknown routine)");
    }
    state("after 'become psychotic'(ally, player 2)");
    if (On())
    {
      // The disarm's take, only with the allies rule on (counter_op_only_jo.cpp refuses it; with the rule off it
      // would take the ally's weapon).
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = ally;
      frame.ctx.r4.u64 = p2;
      frame.ctx.lr = 0;
      sub_82554BC8(frame.ctx, base);
      REXLOG_INFO("pdz_counterop: TEST TOOL (allies, gadgets) #{}: the disarm's take 82554BC8(ally {:08X}, player 2) "
                  "was asked (the line above this one says what the melee rule did with it)",
                  done, ally);
    }
  }
} // namespace

// counter_op.cpp's level tick (round 13 a). `playing` = player 2 is alive in the world as a guard, no cutscene.
void PdzCounterOpRules13Tick(PPCContext &ctx, uint8_t *base, bool playing)
{
  if (!PdzCounterOpPlayer2())
  {
    return;
  }
  // Round 13b p: the allies' neighbour holders, refreshed twice a second while the walk-through rule is on.
  if (static uint32_t n = 0; WalkOn() && (n++ % 30) == 0)
  {
    int count = 0;
    const uint32_t world = L32(base, kWorld);
    const uint32_t end = Ptr(world) ? L32(base, world + kWorldEnd) : 0;
    uint32_t node = Ptr(world) ? L32(base, world + kWorldFirst) : 0;
    for (int i = 0; Ptr(node) && node != end && i < 16384 && count < kAllyMax; ++i, node = L32(base, node + 12))
    {
      const uint32_t o = L32(base, node + 4);
      if (Ptr(o) && L32(base, o + kClass) == 0x21 && !(L32(base, o + kHidden) & 0x10) &&
          LF(base, o + kHealth) > 0.0f && PdzCounterOpIsAgentAlly(base, o) && Ptr(L32(base, o + kHolder)))
      {
        g_ally_of_holder[count].store(o, std::memory_order_relaxed);
        g_ally_holder[count].store(L32(base, o + kHolder), std::memory_order_relaxed);
        ++count;
      }
    }
    for (int i = count; i < kAllyMax; ++i)
    {
      g_ally_holder[i].store(0, std::memory_order_relaxed);
    }
  }
  if (const int mode = REXCVAR_GET(pdz_counterop_test_p2_ally); mode > 0 && playing)
  {
    AllyTestTick(ctx, base, mode);
  }
  if (playing)
  {
    AllyGadgetTestTick(ctx, base); // round 13b p
  }
  PdzCounterOpVehiclesTestTick(ctx, base, playing);
}
