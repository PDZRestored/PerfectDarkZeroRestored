// Counter-Operative, round 4 (item 3a): player 2 re-dressed as the guard he takes, at every spawn, the way Rare's own
// multiplayer re-skin does it (825B45D0, called by the participant's respawn 82456C90 at 82457304). 825B45D0 itself
// can't be called: it returns at once unless the game type ([[82D6BFD8]] +0x28) is 2, and it takes the ids from the
// participant's MP character choice. This file runs the same steps, in Rare's order, with the ids passed in.
// Built only with PDZ_COUNTEROP. Call-in: counter_op.cpp SpawnAt(), before ShowP2 (he is out of the world), after an
// inventory clear.
// hands_asset: the asset record (what 82413FD8 returns) for the first-person hands to use with this body; 0 = keep
// the hands player 2 has now (they are copied to the new body entry's +20 when that is empty). voice: 0..25 (Rare's
// table 82D2B098), -1 = keep his voice index (counter_op_voice.cpp sets the guard's afterwards).
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DEFINE_BOOL(pdz_counterop_dress, true, "Gameplay",
                    "Counter-Operative (default on, round 4): at every spawn player 2's body, head and first-person "
                    "hands are rebuilt as the taken guard's, with the steps of Rare's multiplayer re-skin (825B45D0). "
                    "Off = the look chosen at the level load for the whole mission")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_821E86B0); // (character): the animator off the body instance (instance +4), 8251FBB8
REX_EXTERN(sub_827736E0); // (&slot state, -2): frees a model-instance pool slot
REX_EXTERN(sub_825B4540); // (human): first-person hands down (+0x1E74.. six instances, the x-ray hands asset +0x1E8C)
REX_EXTERN(sub_825463B0); // (character): the x-ray overlay down (+0x1498 instance, +0x149C asset)
REX_EXTERN(sub_82410A60); // (model data, 0, 10) -> a new body instance
REX_EXTERN(sub_82411048); // (model data, parent instance, 10) -> a new instance on the parent's skeleton (head, overlay)
REX_EXTERN(sub_82546340); // (character): the x-ray overlay up, from the body entry's +0x2C name, on +0x48
REX_EXTERN(sub_825B43E0); // (human): first-person hands up, from the body entry's +0x14 (and +0x1C, always 0)
REX_EXTERN(sub_825464D0); // (character): the animator on +0x48 from the body entry's +0x40 (821E82A0)
REX_EXTERN(sub_82413688); // (model, bone name, &index out, 0): the hair bone index (the constructor's 825A6D38..)
REX_EXTERN(sub_8262CD78); // (human +0x1E9C, head id, voice 0..25): the speech / face set re-made when either changed

void PdzCounterOpHandsLend(PPCContext &ctx, uint8_t *base, uint32_t entry, uint32_t hands); // counter_op_hands.cpp

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;                // +8 -> the lists: +0 bodies, +0xC heads ({+0 entries, +8 count})
  constexpr uint32_t kHumanVtable = 0x820C7DE0;
  constexpr uint32_t kInstancePool = 0x82E75160;        // 0x9C4 instances of 0x200 bytes
  constexpr uint32_t kInstanceState = 0x82D70E80;       // one word per slot (4 = free)
  constexpr uint32_t kInstanceCount = 0x9C4;
  constexpr uint32_t kHairBone = 0x820C0008;            // "|_PART_hair"
  constexpr uint32_t kHelmetHairBone = 0x820C0014;      // "|_PART_helmet_hair"
  // The character.
  constexpr uint32_t kBodyInstance = 0x48, kChildFirst = 0x58, kChildNext = 0x60, kHeadInstance = 0x1008,
                     kHairIndex = 0x12F4, kHelmetHairIndex = 0x12F8, kBodyEntryOf = 0x1490, kHeadEntryOf = 0x1494,
                     kBodyId = 0x14A0, kHeadId = 0x14A4, kParticipantOf = 0x19A0, kHands = 0x1E74, kSpeech = 0x1E9C;
  // A body list entry / a head list entry.
  constexpr uint32_t kBodyHands = 0x14, kBodyAnimSet = 0x40, kBodyModel = 0x44, kHeadModel = 0xC;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24);
    base[a + 1] = uint8_t(v >> 16);
    base[a + 2] = uint8_t(v >> 8);
    base[a + 3] = uint8_t(v);
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  uint32_t Call(PPCContext &ctx, uint8_t *base, PPCFunc *fn, uint32_t r3, uint64_t r4 = 0, uint32_t r5 = 0,
                uint32_t r6 = 0)
  {
    rex::CallFrame frame(ctx); // copies r1 / r13 only: every argument register is set here
    frame.ctx.r3.u64 = r3;
    frame.ctx.r4.u64 = r4;
    frame.ctx.r5.u64 = r5;
    frame.ctx.r6.u64 = r6;
    fn(frame.ctx, base);
    return frame.ctx.r3.u32;
  }

  // An entry by id in list 0 (bodies) or 0xC (heads). Rare's own lookups (82278788 / 822787E0) fall back to id
  // 0x56 / 0x79 when the id is missing; this one returns 0 instead.
  uint32_t Entry(const uint8_t *base, uint32_t list_offset, uint32_t id)
  {
    const uint32_t lists = Load32(base, kGame + 8);
    const uint32_t list = Ptr(lists) ? Load32(base, lists + list_offset) : 0;
    if (!Ptr(list))
    {
      return 0;
    }
    const uint32_t entries = Load32(base, list);
    const int32_t count = int32_t(Load32(base, list + 8));
    for (int32_t i = 0; Ptr(entries) && i < count && i < 1024; ++i)
    {
      const uint32_t e = Load32(base, entries + uint32_t(i) * 4);
      if (Ptr(e) && Load32(base, e) == id)
      {
        return e;
      }
    }
    return 0;
  }

  bool PoolInstance(uint32_t inst)
  {
    return inst >= kInstancePool && inst < kInstancePool + kInstanceCount * 0x200u && ((inst - kInstancePool) & 0x1FF) == 0;
  }
  // 825B4794..825B47A8: the slot's state word, freed with -2 (Rare frees the body / head instance this way; the
  // asset reference stays with the list entry).
  void FreeInstance(PPCContext &ctx, uint8_t *base, uint32_t inst)
  {
    Call(ctx, base, sub_827736E0, kInstanceState + ((inst - kInstancePool) >> 9) * 4, uint64_t(int64_t(-2)));
  }
} // namespace

// Player 2 (a live human, out of the world) re-dressed as body_id / head_id of the level's lists. true = done (or
// nothing to do); false = refused before anything was touched (why says exactly why). Game thread only (the
// counter-op tick).
// Mon 23 Nov night (1.0.1). Rare's armour pieces of a character (helmet, vest ...): children of type 0x19 with +0x3C8
// set (Rare's re-skin 825B4860 deletes exactly these). Counted; with destroy they are deleted the way Rare's re-skin
// does (each one's own destructor), so none is left to be shed and fall. Every child is read only where readable and
// destroyed only through a vtable in the game image (as the dress code below).
uint32_t PdzCounterOpArmourPieces(PPCContext &ctx, uint8_t *base, uint32_t character, bool destroy)
{
  uint32_t pieces = 0;
  if (!pdz::guestmem::Ok(base, character, kChildFirst + 4))
  {
    return 0;
  }
  uint32_t child = Load32(base, character + kChildFirst);
  for (int n = 0; pdz::guestmem::Ok(base, child, 0x3CC) && n < 64; ++n)
  {
    const uint32_t next = Load32(base, child + kChildNext); // read first: the child may be destroyed below
    const uint32_t child_vt = Load32(base, child);
    if (Load32(base, child + 0xC) == 0x19 && Load32(base, child + 0x3C8) != 0 && child_vt >= 0x82000000u &&
        child_vt < 0x83000000u)
    {
      if (!destroy)
      {
        ++pieces;
      }
      else if (PPCFunc *fn = pdz::GuestFunction(Load32(base, child_vt), "counter-op: armour piece"))
      {
        Call(ctx, base, fn, child, 1);
        ++pieces;
      }
    }
    child = next;
  }
  return pieces;
}

bool PdzCounterOpDress(PPCContext &ctx, uint8_t *base, uint32_t human, uint32_t body_id, uint32_t head_id,
                       uint32_t hands_asset, int32_t voice, std::string &why)
{
  if (!REXCVAR_GET(pdz_counterop_dress))
  {
    why = "pdz_counterop_dress is off: the level-load look is kept";
    return false;
  }
  if (!Ptr(human) || Load32(base, human) != kHumanVtable)
  {
    why = fmt::format("{:08X} is not a human (vtable {:08X})", human, Ptr(human) ? Load32(base, human) : 0);
    return false;
  }
  const uint32_t old_body = Load32(base, human + kBodyId), old_head = Load32(base, human + kHeadId);
  const uint32_t old_entry = Load32(base, human + kBodyEntryOf);
  const uint32_t body = Entry(base, 0, body_id), head = Entry(base, 0xC, head_id);
  // ---- Every check before the first write (Rare's steps below read all of these unchecked) ----
  if (!Ptr(body) || !Ptr(head))
  {
    why = fmt::format("body {} / head {} not in the level's lists (entries {:08X} / {:08X})", body_id, head_id, body, head);
    return false;
  }
  const uint32_t model = Load32(base, body + kBodyModel), head_model = Load32(base, head + kHeadModel);
  // A guard of this level wears them, so Rare's loaders (82414070 / 82414618) have filled both; if not, refuse: in a
  // running level their fallbacks (placeholder body / head) are not resident and the null is read unchecked.
  if (!Ptr(model) || !Ptr(Load32(base, model + 8)) || !Ptr(head_model) || !Ptr(Load32(base, head_model + 8)))
  {
    why = fmt::format("body {} model {:08X} / head {} model {:08X} not loaded in this level", body_id, model, head_id,
                      head_model);
    return false;
  }
  if (!Ptr(Load32(base, body + kBodyAnimSet)))
  {
    why = fmt::format("body {} has no animation set (+0x40)", body_id);
    return false;
  }
  const uint32_t old_inst = Load32(base, human + kBodyInstance), old_head_inst = Load32(base, human + kHeadInstance);
  if (!PoolInstance(old_inst) || (old_head_inst && !PoolInstance(old_head_inst)))
  {
    why = fmt::format("his instances {:08X} / {:08X} are not pool instances", old_inst, old_head_inst);
    return false;
  }
  // First-person hands: the new body's +0x14 must be an asset before 825B43E0 runs (log 312: null hands crash).
  uint32_t hands = Ptr(hands_asset) ? hands_asset : Load32(base, body + kBodyHands);
  if (!Ptr(hands) && Ptr(old_entry))
  {
    hands = Load32(base, old_entry + kBodyHands); // the hands he has now
  }
  if (!Ptr(hands) || !Ptr(Load32(base, hands + 8)))
  {
    why = fmt::format("no first-person hands for body {} (asked {:08X}, entry +0x14 {:08X}, his own {:08X})", body_id,
                      hands_asset, Load32(base, body + kBodyHands), Ptr(old_entry) ? Load32(base, old_entry + kBodyHands) : 0);
    return false;
  }
  const bool same_look = old_body == body_id && old_head == head_id;
  const bool same_hands = Load32(base, body + kBodyHands) == hands;
  const uint32_t speech = human + kSpeech;
  const uint32_t voice_now = Load32(base, speech + 0x10);
  const uint32_t voice_new = voice >= 0 && voice < 26 ? uint32_t(voice) : voice_now;
  if (same_look && same_hands && voice_new == voice_now)
  {
    why = fmt::format("already body {} / head {} with hands {:08X}: nothing to do", body_id, head_id, hands);
    return true;
  }
  PdzCounterOpHandsLend(ctx, base, body, hands); // round 8: a loan (only 825B43E0 reads +0x14; guards never do)
  const uint32_t participant = Load32(base, human + kParticipantOf);
  const uint32_t pinfo = Ptr(participant) ? Load32(base, participant + 0xC) : 0;
  const bool local_view = Ptr(pinfo) && base[pinfo + 0xC] != 0; // 825B4834..825B484C: hands only for such a player

  if (same_look)
  {
    // Hands (and / or voice) only: 825B4540 + 825B43E0, as the re-skin does around its body swap.
    if (!same_hands)
    {
      Call(ctx, base, sub_825B4540, human);
      if (local_view)
      {
        Call(ctx, base, sub_825B43E0, human);
      }
    }
  }
  else
  {
    // ---- Rare's 825B4720..825B485C, in order ----
    Store32(base, human + kBodyId, body_id);
    Store32(base, human + kHeadId, head_id);
    Store32(base, human + kBodyEntryOf, body);
    Store32(base, human + kHeadEntryOf, head);
    Call(ctx, base, sub_821E86B0, human);             // the animator off the old body instance
    FreeInstance(ctx, base, old_inst);                // the old body instance (+0x48 keeps the stale pointer, as Rare)
    if (old_head_inst)
    {
      FreeInstance(ctx, base, old_head_inst);
      Store32(base, human + kHeadInstance, 0);
    }
    Call(ctx, base, sub_825B4540, human);             // first-person hands down
    Call(ctx, base, sub_825463B0, human);             // x-ray overlay down
    const uint32_t inst = Call(ctx, base, sub_82410A60, Load32(base, model + 8), 0, 10);
    Store32(base, human + kBodyInstance, inst);
    if (!Ptr(inst))
    {
      // The pool (2500 slots) had no free slot: Rare doesn't check. Two slots were just freed, so this can't happen
      // in practice; if it does the character has no body and the next frame crashes.
      REXLOG_ERROR("pdz_counterop: look: dress: NO body instance for body {} (pool full?)", body_id);
    }
    else
    {
      Store32(base, human + kHeadInstance, Call(ctx, base, sub_82411048, Load32(base, head_model + 8), inst, 10));
      Call(ctx, base, sub_82546340, human);           // x-ray overlay up (entry +0x2C: in every level package)
      if (local_view)
      {
        Call(ctx, base, sub_825B43E0, human);         // first-person hands up from the new entry's +0x14
      }
      Call(ctx, base, sub_825464D0, human);           // the animator on the new instance, from the new entry's +0x40
    }
    // Not in Rare's re-skin (the constructor's 825A6D38..825A6D98 only): the hair bone indices of the new head.
    // Rare's MP re-skin leaves the old head's; recomputed here so they can't point past the new head's bones.
    const uint32_t head_inst = Load32(base, human + kHeadInstance);
    const uint32_t head_rec = Ptr(head_inst) ? Load32(base, head_inst) : 0;
    const uint32_t head_data = Ptr(head_rec) ? Load32(base, head_rec + 4) : 0;
    if (Ptr(head_data))
    {
      Call(ctx, base, sub_82413688, head_data, kHairBone, human + kHairIndex, 0);
      Call(ctx, base, sub_82413688, head_data, kHelmetHairBone, human + kHelmetHairIndex, 0);
    }
    else
    {
      Store32(base, human + kHairIndex, 0xFFFFFFFFu);
      Store32(base, human + kHelmetHairIndex, 0xFFFFFFFFu);
    }
    // Rare's 825B4860..825B4A94: armour pieces (children of type 0x19 with +0x3C8 set) are destroyed and re-made
    // from the new entries' slots (body +0xB4 x10, head +0x6C x5, 82598210). Player 2 has no armour in counter-op
    // (armour stays 0), so none should exist; any found is destroyed the way Rare does and NOT re-made (logged).
    uint32_t pieces = 0;
    uint32_t child = Load32(base, human + kChildFirst);
    // Sun 22 Nov night (audit): each child read only where readable; its destroy only through a vtable in the game image.
    for (int n = 0; pdz::guestmem::Ok(base, child, 0x3CC) && n < 64; ++n)
    {
      const uint32_t next = Load32(base, child + kChildNext); // read first: the child is destroyed below
      const uint32_t child_vt = Load32(base, child);
      if (Load32(base, child + 0xC) == 0x19 && Load32(base, child + 0x3C8) != 0 && child_vt >= 0x82000000u &&
          child_vt < 0x83000000u)
      {
        if (PPCFunc *destroy = pdz::GuestFunction(Load32(base, child_vt), "counter-op dress: armour piece"))
        {
          Call(ctx, base, destroy, child, 1);
          ++pieces;
        }
      }
      child = next;
    }
    if (pieces)
    {
      REXLOG_WARN("pdz_counterop: look: dress: {} armour piece(s) of the old body destroyed, not re-made", pieces);
    }
  }
  // 825B4A98..825B4AA4: the speech / face set follows the head (and the voice index, table 82D2B098).
  Call(ctx, base, sub_8262CD78, speech, head_id, voice_new);
  why = fmt::format("player 2 re-dressed: body {} -> {}, head {} -> {}, voice {} -> {}; body instance {:08X} -> {:08X}, "
                    "head instance {:08X} -> {:08X}, hair bones {} / {}; hands asset {:08X} ({}), first-person hands "
                    "{:08X} / {:08X}{}",
                    old_body, body_id, old_head, head_id, voice_now, voice_new, old_inst,
                    Load32(base, human + kBodyInstance), old_head_inst, Load32(base, human + kHeadInstance),
                    int32_t(Load32(base, human + kHairIndex)), int32_t(Load32(base, human + kHelmetHairIndex)), hands,
                    same_hands ? "as before" : "new", Load32(base, human + kHands), Load32(base, human + kHands + 4),
                    local_view ? "" : " (not a local view: no first-person hands, as Rare)");
  return true;
}
