// Counter-Operative round 3: what each player's HUD shows of the other (items 6-8): the co-op partner ring and radar
// blip, the revive prompt, player 2's gadget prompts. Built only with PDZ_COUNTEROP.
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov (round 18): reads of followed pointers tested
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"

bool PdzCounterOpRunning();     // counter_op.cpp
uint32_t PdzCounterOpAgent();   // the agent's character while a counter-op game runs, else 0
uint32_t PdzCounterOpPlayer2(); // player 2's character while a counter-op game runs, else 0
bool PdzCounterOpVehiclePromptDenied(const uint8_t *base, uint32_t prop); // round 13 a, counter_op_vehicles.cpp
void PdzCounterOpHudRestoreLater(const char *why);                        // round 16 (item 2), below

REXCVAR_DEFINE_BOOL(pdz_counterop_no_gadget_prompts, true, "Gameplay",
                    "Counter-Operative (default on): player 2 gets no gadget prompts or actions (Data Thief, "
                    "Locktopus, revive kit, demo kit use points, terminal use); doors, vehicles and mounted guns work "
                    "as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_hide_partner_radar, true, "Gameplay",
                    "Counter-Operative (default on): neither player's radar shows the other human (alive, down or "
                    "riding a vehicle)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_no_revive_prompt, true, "Gameplay",
                    "Counter-Operative (default on): the agent gets no revive-kit prompt at player 2's downed body")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_no_down_prompt, false, "Gameplay",
                    "Counter-Operative (testing, off): player 2 gets no prompt from Rare's co-op death states "
                    "(8222F740's kind-5 prompt, text 0x14F; what it says is not known)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_825AAB48); // offer a context prompt (character, prop, kind, text id, priority, action) -> bool
REX_EXTERN(__imp__sub_8226D140); // radar: add a blip (map, &position, colour index, shape) (leaf)

namespace
{
  bool Running() { return PdzCounterOpRunning(); }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  // Sun 22 Nov (round 18): readable now (the Sat 21 Nov cached check), for pointers followed out of guest memory.
  bool Rd(const uint8_t *base, uint32_t a, uint32_t n) { return pdz::guestmem::Ok(base, a, n); }
  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }

  // Return addresses of 825AAB48's gadget callers (the instruction after each bl; FINDINGS 6.3).
  constexpr uint32_t kPromptHack = 0x8258AC88;          // hackable: Data Thief hack (kind 3, text 0x76)
  constexpr uint32_t kPromptTerminalUse = 0x8258ACDC;   // hackable: use a terminal / HackAndUse (kind 1, text 0x76)
  constexpr uint32_t kPromptDoorUnlock = 0x82562640;    // door: Locktopus while locked (kind 3, door +0x450)
  constexpr uint32_t kPromptDoorLock = 0x825626E4;      // door: Locktopus to lock it (kind 3, door +0x44C)
  constexpr uint32_t kPromptReviveKit = 0x82545EC8;     // downed character: revive kit 0x3B (kind 3, text 0x92)
  constexpr uint32_t kPromptUsePoint = 0x825F0AC0;      // Sabotage-type use point (kind 3, text 0x7F)
  constexpr uint32_t kPromptLockProp = 0x825A0920;      // class at vtable 820C6DC0 next to lockbot data (UNSURE)
  constexpr uint32_t kPromptDownState = 0x8222FC44;     // co-op death states' own prompt (kind 5, text 0x14F)
  constexpr uint32_t kPromptKindGadget = 3;             // 825AAB48's "hold" kind: needs ability bit 1 << 32
  constexpr uint32_t kHackableVtable = 0x820C5180;      // hackable prop (vt +0x88 8258A5D8, +0x8C 8258ABB0)
  // Round 4 (item 6): the door class (vt +0x8C = 82562528, the door's prompt function). A locked door offers, besides
  // the two Locktopus prompts above, kind-0 notices through its shared tail call (return 82562794): text door +0x454
  // (locked, lockable, the character has no Locktopus) and +0x458 (locked, not lockable); its kind-1 prompts (+0x45C,
  // +0x460: a hand-operated door) come from 825626B4 and the same tail.
  constexpr uint32_t kDoorVtable = 0x820C27C8;
  constexpr uint32_t kPromptDoorTail = 0x82562794;

  uint32_t g_prompts_denied = 0;
  // Round 4 (item 5): one line per level for each of the ring and the radar, and the counts when the level ends.
  uint32_t g_ring_skipped = 0, g_radar_skipped = 0, g_radar_agent = 0; // _agent: of those, on the agent's radar
  uint32_t g_radar_human_asked = 0, g_radar_human_other_site = 0, g_radar_viewer_unknown = 0;
  // Testing (pdz_counterop_test_prompt_tour): player 2's prompt offers at one watched prop.
  uint32_t g_watch_prop = 0, g_watch_asked = 0, g_watch_denied = 0, g_watch_offered = 0, g_watch_kind = 0,
           g_watch_text = 0;

  // The other human for a radar's viewer (map +0xA04 -> participant, +0x3C4 -> character).
  uint32_t OtherHuman(const uint8_t *base, uint32_t map)
  {
    const uint32_t viewer_part = Rd(base, map + 0xA04, 4) ? Load32(base, map + 0xA04) : 0;
    const uint32_t viewer = Rd(base, viewer_part + 0x3C4, 4) ? Load32(base, viewer_part + 0x3C4) : 0;
    if (!Ptr(viewer))
    {
      return 0;
    }
    if (viewer == PdzCounterOpAgent())
    {
      return PdzCounterOpPlayer2();
    }
    if (viewer == PdzCounterOpPlayer2())
    {
      return PdzCounterOpAgent();
    }
    return 0;
  }
} // namespace

// sub_825AAB48(character, prop, kind, text id, priority, action): Rare's one "offer a context prompt" (32 callers).
// Kind 3: needs ability 1 << 32 (8254DAB8), HUD 823B1CB0(character, text, 3). Other kinds: if priority >= character
// +0x1A28, HUD 823B1BE0(character, text, kind), character +0x1A30 = prop, +0x1A28 = priority, +0x1A2C = action.
// Returns 1 when offered. Every gadget caller raises the gadget / sets its action bit ONLY on a 1 (82545ED0,
// 8258AC90, 8256264C, 825626EC), so returning 0 removes both the prompt and the action.
REX_HOOK_RAW(sub_825AAB48)
{
  const uint32_t who = ctx.r3.u32, prop = ctx.r4.u32, kind = ctx.r5.u32, lr = uint32_t(ctx.lr);
  if (Running() && Ptr(who))
  {
    bool deny = false;
    const char *why = "";
    if (who == PdzCounterOpPlayer2() && REXCVAR_GET(pdz_counterop_no_gadget_prompts))
    {
      if (kind == kPromptKindGadget || lr == kPromptTerminalUse || lr == kPromptLockProp ||
          (Ptr(prop) && Load32(base, prop) == kHackableVtable))
      {
        deny = true;
        why = "gadget / terminal prompt for player 2";
      }
      else if (kind == 0 && lr == kPromptDoorTail && Ptr(prop) && Load32(base, prop) == kDoorVtable)
      {
        deny = true; // round 4: a locked door's notice (it names the gadget or the lock; he has neither)
        why = "locked-door notice for player 2";
      }
      else if (lr == kPromptDownState && REXCVAR_GET(pdz_counterop_no_down_prompt))
      {
        deny = true;
        why = "co-op death-state prompt for player 2";
      }
    }
    if (who == PdzCounterOpPlayer2() && PdzCounterOpVehiclePromptDenied(base, prop)) // round 13 a (G2)
    {
      deny = true;
      why = "a closed vehicle's (or vehicle gun's) prompt for player 2";
    }
    // The agent at player 2's downed body: Rare's revive-kit prompt (and so the revive) never offered.
    if (who == PdzCounterOpAgent() && lr == kPromptReviveKit && prop == PdzCounterOpPlayer2() && REXCVAR_GET(pdz_counterop_no_revive_prompt))
    {
      deny = true;
      why = "revive-kit prompt for the agent at player 2";
    }
    if (who == PdzCounterOpPlayer2() && prop == g_watch_prop && g_watch_prop)
    {
      ++g_watch_asked;
      g_watch_denied += deny;
      g_watch_kind = kind;
      g_watch_text = ctx.r6.u32;
      if (!deny)
      {
        __imp__sub_825AAB48(ctx, base);
        g_watch_offered += (ctx.r3.u32 & 0xFF) != 0;
        return;
      }
    }
    if (deny)
    {
      if (++g_prompts_denied <= 6 || g_prompts_denied % 2000 == 0)
      {
        REXLOG_INFO("pdz_counterop: prompt #{} denied: {} (kind {}, text {:X}, prop {:08X}, from {:08X})",
                    g_prompts_denied, why, kind, ctx.r6.u32, prop, lr);
      }
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_825AAB48(ctx, base);
}

// sub_8226D140(map, &position, colour index, shape): the radar's blip list (map +0, 12 bytes x 128; +0x600 colour;
// +0x800 shape; count +0xA00). Called twice, both from the radar draw 8226D280: 8226E468 (characters: the
// caller's r28 = the character, or 0 for the prop / vehicle path where r31 = the object) and 8226E530 (an object
// list, kind 5). The leaf keeps the caller's r28 / r31 untouched, so ctx.r28 / ctx.r31 are the caller's here.
REX_HOOK_RAW(sub_8226D140)
{
  if (Running())
  {
    // Round 4 (diagnostic, counted for the level's HUD line): every blip asked for a human, whatever the call site,
    // and how often the radar's viewer could not be read.
    const uint32_t who = ctx.r28.u32;
    if (Ptr(who) && (who == PdzCounterOpAgent() || who == PdzCounterOpPlayer2()))
    {
      ++g_radar_human_asked;
      g_radar_human_other_site += uint32_t(ctx.lr) != 0x8226E46C;
      g_radar_viewer_unknown += OtherHuman(base, ctx.r3.u32) == 0;
    }
  }
  if (Running() && REXCVAR_GET(pdz_counterop_hide_partner_radar) && uint32_t(ctx.lr) == 0x8226E46C)
  {
    const uint32_t other = OtherHuman(base, ctx.r3.u32);
    if (Ptr(other))
    {
      const uint32_t character = ctx.r28.u32;
      if (character == other)
      {
        g_radar_agent += other == PdzCounterOpPlayer2();
        if (++g_radar_skipped == 1)
        {
          REXLOG_INFO("pdz_counterop: radar: the other human's blip left out of {} radar (colour {}, shape {}); logged "
                      "once a level, the count at its end",
                      other == PdzCounterOpPlayer2() ? "the agent's" : "player 2's", ctx.r5.u32, ctx.r6.u32);
        }
        return; // the other human: alive (0,0 / 1,0) or down (1,1: the down-partner marker, viewer has a revive kit)
      }
      // Vehicle path (object +0xC == 0x2C: occupant from vt +0xE0, blip at the vehicle's +0x30): the other human
      // riding it. Rare's own vt +0xE0 call; ctx.r31 = the vehicle.
      const uint32_t object = ctx.r31.u32;
      // Sun 22 Nov night (audit): a readable object whose vtable is in the game image before its slot is called.
      const uint32_t object_vt = pdz::guestmem::Ok(base, object, 0x34) ? Load32(base, object) : 0;
      if (character == 0 && object_vt >= 0x82000000u && object_vt < 0x83000000u &&
          pdz::guestmem::Ok(base, object_vt + 0xE0, 4) && Load32(base, object + 0x0C) == 0x2C &&
          ctx.r4.u32 == object + 0x30)
      {
        PPCFunc *occupant_fn = pdz::GuestFunction(Load32(base, object_vt + 0xE0), "counter-op radar");
        if (occupant_fn)
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = object;
          occupant_fn(frame.ctx, base);
          if (frame.ctx.r3.u32 == other)
          {
            return;
          }
        }
      }
    }
  }
  __imp__sub_8226D140(ctx, base);
}


// Round 3 (item 7): the co-op partner ring. Rare's waypoint markers (a per-human manager at character +0x19C8, six
// slots; the co-op level start 824720E8 registers each human participant as a target, and the per-view draw 82447380
// calls this for every slot) draw a flat spinning target quad on the floor under each human in the other's view: the
// yellow ring. sub_824458B0(slot r3, viewer position r4, arrow texture r5, target texture r6): slot +0x18 is the
// target id, below 0 = a human participant, above 0 = an objective pad. While Counter-Op runs a slot whose target is a
// human is not drawn (no ring, no route arrows to him) in either view; objective waypoints and plain co-op are
// unchanged.
REXCVAR_DEFINE_BOOL(pdz_counterop_hide_partner_ring, true, "Gameplay",
                    "Counter-Operative (default on): the co-op partner ring (Rare's waypoint marker on the floor under "
                    "the other human) is not drawn for either player")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(__imp__sub_824458B0);
REX_HOOK_RAW(sub_824458B0)
{
  const uint32_t slot = ctx.r3.u32;
  if (Running() && REXCVAR_GET(pdz_counterop_hide_partner_ring) && Ptr(slot) && int32_t(Load32(base, slot + 0x18)) < 0)
  {
    if (++g_ring_skipped == 1)
    {

      REXLOG_INFO("pdz_counterop: ring: a waypoint marker aimed at a human (slot {:08X}, target id {}) not drawn; logged "
                  "once a level, the count at its end",
                  slot, int32_t(Load32(base, slot + 0x18)));
    }
    return;
  }
  __imp__sub_824458B0(ctx, base);
}
// Round 4 (item 5): counter_op.cpp's Reset() (the level ended, was reloaded, or Counter-Op was switched off): the
// level's counts in one line, and the once-a-level lines armed again.
void PdzCounterOpHudLevelEnd()
{
  if (g_ring_skipped || g_radar_skipped || g_prompts_denied || g_radar_human_asked)
  {
    REXLOG_INFO("pdz_counterop: HUD: this level: {} partner ring markers not drawn, {} radar blips of the other human "
                "left out ({} of player 2 on the agent's radar, {} of the agent on player 2's; Rare asked for a "
                "human's blip {} times in all, {} from another call site, {} with a viewer that could not be read), {} "
                "gadget / revive prompts denied",
                g_ring_skipped, g_radar_skipped, g_radar_agent, g_radar_skipped - g_radar_agent, g_radar_human_asked,
                g_radar_human_other_site, g_radar_viewer_unknown, g_prompts_denied);
  }
  g_ring_skipped = g_radar_skipped = g_radar_agent = g_prompts_denied = 0;
  g_radar_human_asked = g_radar_human_other_site = g_radar_viewer_unknown = 0;
  PdzCounterOpHudRestoreLater("level end, reload or checkpoint restart"); // round 16 (item 2)
}

// Testing (pdz_counterop_test_prompt_tour, counter_op.cpp): watch player 2's prompt offers at one prop; read = the
// counts since the watch was set (asked by Rare, denied by us, offered = Rare's call said yes) and the last kind / text.
void PdzCounterOpHudWatch(uint32_t prop)
{
  g_watch_prop = prop;
  g_watch_asked = g_watch_denied = g_watch_offered = g_watch_kind = g_watch_text = 0;
}
void PdzCounterOpHudWatchRead(uint32_t out[5])
{
  out[0] = g_watch_asked, out[1] = g_watch_denied, out[2] = g_watch_offered, out[3] = g_watch_kind, out[4] = g_watch_text;
}

// ==== Round 16 (item 2): player 2's weapon HUD after a spiderbot ====================================================
// While he drives a spiderbot, counter_op_view.cpp's PdzCounterOpSpiderHud hides six elements of his HUD ('weapon
// swap', 'weapon inventory', AmmoBarL / AmmoMetersL / AmmoBarR / AmmoMetersR: Show off AND Opacity 0, because Rare's
// HUD update shows some again every frame but never writes their opacity) and puts them back on the first tick he
// no longer drives. That put-back is dropped without a word when his HUD handle or scene reads differently on that
// tick (his pad -1 while his player is re-created, a reload: play log 027, hide at 15:14:00, checkpoint restart at
// 15:14:46, no 'shown again'). An element left at opacity 0 stays invisible for good: the waiting panel
// (PdzCounterOpRareDark) even saves that 0 as the value it puts back at his next body. The repair: every way out of a
// bot (counter_op_spider.cpp) checks the six at once; every drive, level end / reload / checkpoint restart and new
// body (PdzCounterOpHudOnNewBody) arms a check that runs on the second tick he neither drives nor stands behind the
// waiting panel (so the panel's own put-back has run first). Only an element still at opacity ~0 is touched: Show on,
// Opacity Rare's (1; 0.7 for AmmoBarR / AmmoMetersR). The crosshair is NOT one of them (it is not in Rare's XUI HUD
// scene). The check walks his HUD scene (a few hundred XUI calls): only at those moments, never every frame.
REXCVAR_DEFINE_BOOL(pdz_counterop_spiderbot_hud_restore, true, "Gameplay",
                    "Counter-Operative (round 16, default on): player 2's weapon HUD (weapon row, pick-up box, ammo "
                    "bars), hidden while he drives a spiderbot, is checked and shown again on every way out of the "
                    "bot and at his next body, whatever ended the drive. Off = only the drive's own put-back (round 11)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

extern std::atomic<bool> g_rare_dark_drawn; // counter_op_view.cpp: Rare's HUD shows his waiting panel
bool PdzCounterOpSpiderDriving();          // counter_op_spider.cpp
bool PdzCounterOpWaitHudHidden();          // counter_op.cpp (Sun 22 Nov, item 7): his weapon HUD is hidden while he waits

REX_EXTERN(sub_8278A4C8); // XUI: first child (h, &out)
REX_EXTERN(sub_8278A5B8); // XUI: next sibling (h, &out)
REX_EXTERN(sub_8277B7F8); // XUI: property id by name (h, name, &id)
REX_EXTERN(sub_8277F928); // XUI: get property (h, id, 0, &val)
REX_EXTERN(sub_8277FA28); // XUI: set property (h, id, 0, &val)
REX_EXTERN(sub_82794EC0); // XUIElementPropVal clear

namespace
{
  constexpr uint32_t kHudTable = 0x82E2F594; // [pad * 4] = the pad's HUD object; +0 = its XUI scene handle
  constexpr uint32_t kPropValSize = 20, kPropBool = 1, kPropFloat = 4, kPropString = 5;
  constexpr const char *kWeaponHud[] = {"weapon swap", "weapon inventory", "AmmoBarL", "AmmoMetersL", "AmmoBarR",
                                        "AmmoMetersR"}; // the six counter_op_view.cpp hides while he drives

  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
  }

  // counter_op_view.cpp's Xui, trimmed to what the check needs (find an element by Id, read / write its opacity,
  // switch it on).
  struct HudXui
  {
    rex::CallFrame frame;
    uint8_t *base;
    uint32_t val, out;
    HudXui(PPCContext &ctx, uint8_t *b) : frame(ctx), base(b)
    {
      val = rex::ppc::stack_push(frame.ctx, base, std::string(kPropValSize, '\0').data(), kPropValSize);
      out = rex::ppc::stack_push<uint32_t>(frame.ctx, base, 0);
    }
    uint32_t PropId(uint32_t h, const char *name)
    {
      const uint32_t saved = frame.ctx.r1.u32;
      Store32(base, out, 0xFFFFFFFFu);
      std::string bytes; // the name as big-endian UTF-16
      for (const char *p = name; *p; ++p)
      {
        bytes += '\0';
        bytes += *p;
      }
      bytes.append(2, '\0');
      frame.ctx.r3.u64 = h;
      frame.ctx.r4.u64 = rex::ppc::stack_push(frame.ctx, base, bytes.data(), uint32_t(bytes.size()));
      frame.ctx.r5.u64 = out;
      sub_8277B7F8(frame.ctx, base);
      frame.ctx.r1.u32 = saved;
      return int32_t(frame.ctx.r3.u32) < 0 ? 0xFFFFFFFFu : Load32(base, out);
    }
    bool Call(PPCFunc *fn, uint32_t h, const char *name)
    {
      const uint32_t id = PropId(h, name);
      if (id == 0xFFFFFFFFu)
      {
        return false;
      }
      frame.ctx.r3.u64 = h;
      frame.ctx.r4.u64 = id;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = val;
      fn(frame.ctx, base);
      return int32_t(frame.ctx.r3.u32) >= 0;
    }
    bool IdIs(uint32_t h, const char *id)
    {
      std::memset(base + val, 0, kPropValSize);
      bool same = false;
      if (Call(sub_8277F928, h, "Id") && Load32(base, val) == kPropString)
      {
        const uint32_t text = Load32(base, val + 4);
        size_t i = 0;
        same = Ptr(text);
        for (; same && i < 64; ++i)
        {
          const char16_t c = char16_t(base[text + i * 2] << 8 | base[text + i * 2 + 1]);
          if (c != char16_t(uint8_t(id[i])))
          {
            same = false;
          }
          else if (!c)
          {
            break;
          }
        }
      }
      frame.ctx.r3.u64 = val;
      sub_82794EC0(frame.ctx, base); // the string value is freed
      return same;
    }
    uint32_t Child(uint32_t h, bool first)
    {
      Store32(base, out, 0);
      frame.ctx.r3.u64 = h;
      frame.ctx.r4.u64 = out;
      if (first)
      {
        sub_8278A4C8(frame.ctx, base);
      }
      else
      {
        sub_8278A5B8(frame.ctx, base);
      }
      return int32_t(frame.ctx.r3.u32) < 0 ? 0 : Load32(base, out);
    }
    uint32_t Find(uint32_t h, const char *id, int depth = 0)
    {
      if (depth > 12)
      {
        return 0;
      }
      int n = 0;
      for (uint32_t c = Child(h, true); c && n < 256; c = Child(c, false), ++n)
      {
        if (IdIs(c, id))
        {
          return c;
        }
        if (const uint32_t found = Find(c, id, depth + 1))
        {
          return found;
        }
      }
      return 0;
    }
    bool GetOpacity(uint32_t h, float &v)
    {
      std::memset(base + val, 0, kPropValSize);
      const bool ok = Call(sub_8277F928, h, "Opacity") && Load32(base, val) == kPropFloat;
      if (ok)
      {
        const uint32_t u = Load32(base, val + 4);
        std::memcpy(&v, &u, 4);
      }
      return ok;
    }
    bool SetOpacity(uint32_t h, float v)
    {
      std::memset(base + val, 0, kPropValSize);
      Store32(base, val, kPropFloat);
      uint32_t u;
      std::memcpy(&u, &v, 4);
      Store32(base, val + 4, u);
      return Call(sub_8277FA28, h, "Opacity");
    }
    bool Show(uint32_t h)
    {
      std::memset(base + val, 0, kPropValSize);
      Store32(base, val, kPropBool);
      Store32(base, val + 4, 1u);
      return Call(sub_8277FA28, h, "Show");
    }
  };

  struct HudRestore
  {
    int wait = 0;          // ticks (free of a drive and of the waiting panel) before an armed check runs; 0 = none
    std::string why;       // what armed it
    bool was_driving = false;
    int32_t pad = -1;      // his XInput user at the last level tick (counter_op_spider.cpp passes it)
    uint32_t lines = 0;
  };
  HudRestore g_hr;

  // The six elements in his HUD scene: each one still at opacity ~0 is shown again (Show on, Opacity 1).
  void HudRestoreNow(PPCContext &ctx, uint8_t *base, int32_t pad, const std::string &why)
  {
    if (!REXCVAR_GET(pdz_counterop_spiderbot_hud_restore))
    {
      return;
    }
    const uint32_t hud = pad >= 0 && pad <= 3 ? Load32(base, kHudTable + uint32_t(pad) * 4) : 0;
    const uint32_t scene = Rd(base, hud, 4) ? Load32(base, hud) : 0;
    if (!scene)
    {
      if (g_hr.lines++ < 400)
      {
        REXLOG_INFO("pdz_counterop: HUD: weapon element check ({}): player 2's HUD is not there (pad {}, HUD {:08X}): "
                    "nothing to check",
                    why, pad, hud);
      }
      return;
    }
    PPCContext copy = ctx;
    HudXui x(copy, base);
    int found = 0, fixed = 0;
    std::string names;
    for (const char *id : kWeaponHud)
    {
      const uint32_t h = x.Find(scene, id);
      if (!h)
      {
        continue;
      }
      ++found;
      float opacity = 1.0f;
      if (x.GetOpacity(h, opacity) && opacity < 0.05f)
      {
        x.Show(h);
        // Rare's scene has the right-hand ammo bar at 0.7 (HUD tree dump, round 4 t5_text_datacore), the rest at 1
        x.SetOpacity(h, id[std::strlen(id) - 1] == 'R' ? 0.7f : 1.0f);
        ++fixed;
        names += names.empty() ? id : std::string(", ") + id;
      }
    }
    if (g_hr.lines++ < 400)
    {
      if (fixed)
      {
        REXLOG_INFO("pdz_counterop: HUD: weapon element check ({}): {} of the {} weapon element(s) of player 2's HUD "
                    "(scene {:08X}) were still hidden ({}): shown again (pdz_counterop_spiderbot_hud_restore)",
                    why, fixed, found, scene, names);
      }
      else
      {
        REXLOG_INFO("pdz_counterop: HUD: weapon element check ({}): none of the {} weapon element(s) of player 2's HUD "
                    "(scene {:08X}) is hidden",
                    why, found, scene);
      }
    }
  }
} // namespace

// Arms the check: it runs on the second level tick he neither drives a bot nor stands behind the waiting panel.
void PdzCounterOpHudRestoreLater(const char *why)
{
  g_hr.wait = 2;
  g_hr.why = why ? why : "";
}

// counter_op.cpp, at every new body of player 2 (a takeover or a spawn): the check runs once his own view is up.
void PdzCounterOpHudOnNewBody(uint8_t *)
{
  PdzCounterOpHudRestoreLater("a new body (takeover or spawn)");
}

// counter_op_spider.cpp, on a way out of a bot: at once (before the waiting panel saves the elements' opacity this
// tick), and once more at his next body. pad < 0: the last one seen.
void PdzCounterOpHudRestoreWeapons(PPCContext &ctx, uint8_t *base, int32_t pad, const char *why)
{
  if (pad >= 0 && pad <= 3)
  {
    g_hr.pad = pad;
  }
  HudRestoreNow(ctx, base, g_hr.pad, std::string("the bot is over: ") + (why ? why : ""));
  PdzCounterOpHudRestoreLater(why);
}

// counter_op_spider.cpp, at the top of PdzCounterOpSpiderDrive (every level tick while Counter-Op runs in a level).
void PdzCounterOpHudRestoreTick(PPCContext &ctx, uint8_t *base, int32_t pad)
{
  if (pad >= 0 && pad <= 3)
  {
    g_hr.pad = pad;
  }
  if (PdzCounterOpWaitHudHidden())
  {
    // Sun 22 Nov (item 7): while he waits the six are hidden on purpose; the check waits for his next body.
    g_hr.wait = std::max(g_hr.wait, 2);
    return;
  }
  if (PdzCounterOpSpiderDriving())
  {
    if (!g_hr.was_driving)
    {
      PdzCounterOpHudRestoreLater("after a spiderbot drive");
    }
    g_hr.was_driving = true;
    g_hr.wait = std::max(g_hr.wait, 2);
    return;
  }
  g_hr.was_driving = false;
  if (g_hr.wait <= 0 || g_rare_dark_drawn.load(std::memory_order_relaxed) ||
      !REXCVAR_GET(pdz_counterop_spiderbot_hud_restore))
  {
    return;
  }
  if (--g_hr.wait > 0)
  {
    return;
  }
  HudRestoreNow(ctx, base, g_hr.pad, g_hr.why);
}
