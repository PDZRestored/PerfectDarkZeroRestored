// Counter-Operative, round 6 (item 7): player 2's half no longer shows the agent's objective lines; he can get one
// line of his own in the SAME HUD element, through Rare's own function (pdz_counterop_objective_text: Rare's string
// ids; empty until a wording is chosen). Built only with PDZ_COUNTEROP.
//
// Rare's chain (TU3, all CONFIRMED in the disassembly):
//   level script (flag handler / Setup)                     objective record = [script + 0x3F0] + n * 0x34:
//     -> 8264EC88(mgr [0x82D6BFD8 + 0x50], &text, kind)        +0 state (0 off, 1 active, 2 completed, 3 failed),
//        "new objective": title = general string 0x119 /        +4 string object (the objective's text), +0x10 kind,
//        0x11A / 0x11B for kind 0 / 1 / 2                       +0x14 pending-condition bits, +0x1C.. message states
//     -> 8264EDC8(mgr, &text, completed)   (from 82151520 Complete and 82151630 Fail)
//        title = general 0x40 "OBJECTIVE COMPLETE - " / 0x41 "OBJECTIVE FAILED - "
//   both walk the human participants (824AA9F0 / 824AAB10 over 0x82E34224) and call, per participant,
//     823B1E88(character [participant + 0x3C4], &title, &text, state)      state 1 new, 2 completed, 3 failed
//       character must be type 2 ([c + 0xC] == 2); participant = c->vt[0x298](c); HUD = [0x82E2F594 + pad * 4]
//       -> 823B2978(hud, 0x14 textObjectiveTitle, &title, 0.0 = the kind's default 10 s, r7 colour 0 / 1 green / 2 red)
//       -> 823B2978(hud, 0x15 textObjectiveInfo,  &text,  0.0, 0)
// So 823B1E88 is the ONE per-player gate for new / completed / failed objective lines (its only callers are 8264ED74
// and 8264EEA8). No hook sits on it (grep of src\ for REX_HOOK_RAW(sub_823B1E88): none).
//
// A second, smaller thing goes to every HUD at once: 821518B0(objective, message index) writes general string 0xEF
// to kind 0x16 (textObjectiveMessages) on all four HUDs through 823B1560(kind, &string, seconds) and plays
// XACT_SOUNDBANK_STOCK_LIFECARD_UPDATED. The text of 0xEF is not known to the helper (UNSURE): the hook below logs
// it once and can skip player 2's HUD (pdz_counterop_objective_drop_messages, off until the text is read).
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

bool PdzCounterOpOn();          // counter_op.cpp: a Counter-Op game is chosen (true before Start too)
bool PdzCounterOpRunning();     // counter_op.cpp
uint32_t PdzCounterOpPlayer2(); // player 2's character while a counter-op game runs, else 0

REXCVAR_DEFINE_BOOL(pdz_counterop_own_objective, true, "Gameplay",
                    "Counter-Operative (round 6, default on): player 2's half does not show the agent's objective "
                    "lines (new / completed / failed); he gets one objective line of his own in the same place")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_objective_text, "ELIMINATE JOANNA DARK", "Gameplay",
                      "Counter-Operative: player 2's own objective line, shown on his half under Rare's 'NEW PRIMARY "
                      "OBJECTIVE - ' title through Rare's objective display. The default is this project's own "
                      "wording (no Rare string says it), the same English line in every language. A value starting "
                      "with # is read as Rare's string ids instead (hex, (table << 16) | index, joined with +). "
                      "Empty = no own line (the agent's lines are still not shown to him)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_objective_each_life, false, "Gameplay",
                    "Counter-Operative: show player 2's objective line again each time he takes a guard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_objective_drop_messages, false, "Gameplay",
                    "Counter-Operative (off until the text is known): the objective-message notice (general string "
                    "0xEF, HUD kind 0x16) is not shown on player 2's half")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_823B1E88); // (character, &title, &text, state 1 new / 2 completed / 3 failed): objective lines of one player
REX_EXTERN(__imp__sub_823B1560); // (kind, &string, f1 seconds): one HUD text on all four HUDs
REX_EXTERN(sub_823B2978);        // (hud, kind, &string, f1 seconds / 0 default, r7 style)
REX_EXTERN(sub_8239BCD8);        // a string by id -> UTF-16 pointer or 0
REX_EXTERN(__imp__sub_823DD198); // DarkXuiObjectiveList        OnMessage(this r3, XUIMessage* r4)
REX_EXTERN(__imp__sub_823DDC00); // DarkXuiObjectiveMessageList OnMessage(this r3, XUIMessage* r4)
REX_EXTERN(__imp__sub_823DDF20); // DarkXuiObjectiveMessageBox  OnMessage(this r3, XUIMessage* r4)
REX_EXTERN(sub_823A2A00);        // (frontend block 0x82E74070 r3, XUI handle r4) -> r3 = the scene's pad (0..3, else 0xFF)
REX_EXTERN(sub_8278A450);        // (XUI handle r3, out handle* r4): the element's parent

REXCVAR_DEFINE_BOOL(pdz_counterop_objective_pause_list, true, "Gameplay",
                    "Counter-Operative (default on, round 7): player 2's pause menu lists only his own objective, "
                    "not the agent's. Off = round 6 (it lists the agent's objectives)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kPlayerOne = 0x82E3C3E4;   // player 1's character
  constexpr uint32_t kHumanVtable = 0x820C7DE0;
  constexpr uint32_t kParticipantOf = 6560;     // human +0x19A0 -> participant
  constexpr uint32_t kPad = 0x690;              // participant: pad
  constexpr uint32_t kHudTable = 0x82E2F594;    // [pad * 4] = the pad's HUD object
  constexpr uint32_t kGameState = 0x82D6BFD8;   // [+0] -> +40 game type (1 = co-op)
  constexpr uint32_t kKindObjectiveMessages = 0x16;
  constexpr size_t kChars = 160;

  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
  }

  struct Own
  {
    uint32_t text_object = 0;  // guest: {+0 pointer, +4 length, +8 -1 static}, text at +16
    uint32_t title_object = 0; // the same, for the each-life call (Rare's "new objective" title 0x119)
    uint32_t shown_hud = 0;    // the HUD the own line went to (a new level has a new HUD object, or call Reset)
    uint32_t dropped = 0;
    bool probed = false;
  };
  Own g_own;

  std::u16string Rare(PPCContext &ctx, uint8_t *base, uint32_t id)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = id;
    sub_8239BCD8(frame.ctx, base);
    std::u16string out;
    const uint32_t chars = frame.ctx.r3.u32;
    for (uint32_t i = 0; Ptr(chars) && i < kChars; ++i)
    {
      const char16_t c = char16_t((base[chars + i * 2] << 8) | base[chars + i * 2 + 1]);
      if (!c)
      {
        break;
      }
      out += c;
    }
    return out;
  }
  std::string Ascii(const std::u16string &text)
  {
    std::string out;
    for (char16_t c : text)
    {
      out += (c >= 0x20 && c < 0x7F) ? char(c) : '?';
    }
    return out;
  }
  // The line: plain text as given (round 7: this project's own wording), or, after a leading '#', Rare's string ids
  // joined with + (their texts joined with a space).
  std::u16string Spec(PPCContext &ctx, uint8_t *base, const std::string &value)
  {
    std::u16string out;
    if (value.empty() || value[0] != '#')
    {
      for (unsigned char c : value)
      {
        out += char16_t(c);
      }
      return out;
    }
    const std::string spec = value.substr(1);
    size_t at = 0;
    while (at < spec.size())
    {
      const size_t end = spec.find('+', at);
      const std::string part = spec.substr(at, end == std::string::npos ? std::string::npos : end - at);
      at = end == std::string::npos ? spec.size() : end + 1;
      const std::u16string text = part.empty() ? std::u16string() : Rare(ctx, base, uint32_t(std::strtoul(part.c_str(), nullptr, 16)));
      if (!text.empty())
      {
        out += out.empty() ? u"" : u" ";
        out += text;
      }
    }
    return out;
  }
  bool Fill(uint8_t *base, uint32_t &object, std::u16string text)
  {
    if (!object)
    {
      object = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(16 + (kChars + 1) * 2));
    }
    if (!object || text.empty())
    {
      return false;
    }
    text.resize(std::min(text.size(), kChars));
    std::memset(base + object, 0, 16 + (kChars + 1) * 2);
    for (size_t i = 0; i < text.size(); ++i)
    {
      base[object + 16 + i * 2] = uint8_t(text[i] >> 8);
      base[object + 16 + i * 2 + 1] = uint8_t(text[i]);
    }
    Store32(base, object, object + 16);
    Store32(base, object + 4, uint32_t(text.size()));
    Store32(base, object + 8, 0xFFFFFFFFu); // static: Rare never frees it (823B2978 copies the text, 8262EE40)
    return true;
  }

  // Player 2's human, also BEFORE counter_op.cpp's Start (the first objective can be announced earlier): in a co-op
  // game with Counter-Op chosen, the human that is not player 1.
  bool IsPlayer2(const uint8_t *base, uint32_t character)
  {
    if (!Ptr(character) || !PdzCounterOpOn())
    {
      return false;
    }
    if (const uint32_t p2 = PdzCounterOpPlayer2())
    {
      return character == p2;
    }
    const uint32_t state = Load32(base, kGameState);
    return Ptr(state) && Load32(base, state + 40) == 1 && Load32(base, character) == kHumanVtable &&
           character != Load32(base, kPlayerOne);
  }
  uint32_t HudOf(const uint8_t *base, uint32_t character)
  {
    // Sun 22 Nov night (audit): the character and his participant read only where readable.
    const uint32_t part = pdz::guestmem::Ok(base, character, kParticipantOf + 4) ? Load32(base, character + kParticipantOf) : 0;
    const uint32_t pad = pdz::guestmem::Ok(base, part, kPad + 4) ? (Load32(base, part + kPad) & 0xFF) : 4;
    return pad < 4 ? Load32(base, kHudTable + pad * 4) : 0;
  }

  // His own line through Rare's function (same labels, colour, queue and 10 s as a new objective of the agent).
  bool ShowOwn(PPCContext &ctx, uint8_t *base, uint32_t character, uint32_t title_object)
  {
    const std::string spec = REXCVAR_GET(pdz_counterop_objective_text);
    if (spec.empty() || !Fill(base, g_own.text_object, Spec(ctx, base, spec)))
    {
      return false;
    }
    if (!title_object)
    {
      if (!Fill(base, g_own.title_object, Rare(ctx, base, 0x119)))
      {
        return false;
      }
      title_object = g_own.title_object;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = character;
    frame.ctx.r4.u64 = title_object;
    frame.ctx.r5.u64 = g_own.text_object;
    frame.ctx.r6.u64 = 1; // "new": the plain colour
    __imp__sub_823B1E88(frame.ctx, base);
    g_own.shown_hud = HudOf(base, character);
    REXLOG_INFO("pdz_counterop: objectives: player 2's own objective shown through Rare's objective display (823B1E88, "
                "state 1 = new): title Rare's string 119 '{}', text '{}' ({}), HUD {:08X}",
                Ascii(Rare(ctx, base, 0x119)), Ascii(Spec(ctx, base, spec)),
                !spec.empty() && spec[0] == '#' ? "Rare's strings" : "this project's own wording", g_own.shown_hud);
    return true;
  }
} // namespace

// ---- Round 7: the pause menu's objective list -----------------------------------------------------------------
// Rare's three XUI classes of the objectives page read the SAME six records for every player:
//   records = [[[0x82D6BFD8 + 0x1C] + 0x2D8] + 0x74] + 0x28C + n * 0x34   (n < 6; +0 state 0 off / 1 active /
//   2 completed / 3 failed, +4 the text's string object, +0x10 kind, +0x18 = -1, +0x1C.. message states)
// and know their player only as pad = 823A2A00(0x82E74070, their XUI handle). While one of those classes handles a
// message for PLAYER 2's scene, the six records are swapped for one record of his own (state 1 = active, his text,
// kind 0 = primary, no messages) and put back before the hook returns. The count, the label, the in-progress mark,
// the selection and the (empty) message panel are all Rare's code reading that one record.
namespace
{
  constexpr uint32_t kFrontend = 0x82E74070;  // +4 a mode word, +0x15 the co-op partner's pad (byte)
  constexpr uint32_t kLevelRoot = 0x82D6BFF4; // [0x82D6BFD8 + 0x1C] -> +0x2D8 -> +0x74 = the mission state block
  constexpr uint32_t kRecords = 0x28C, kRecord = 0x34, kRecordCount = 6, kRecordBytes = kRecord * kRecordCount;

  struct View
  {
    bool applied = false;
    uint32_t at = 0;
    uint8_t real[kRecordBytes];
    uint32_t scratch = 0;
    uint32_t logged = 0;
    uint32_t counted_list = 0; // the list object whose rows were recounted for player 2
    uint32_t message = 0;      // guest memory for that count message
  };
  View g_view;

  uint32_t Records(const uint8_t *base)
  {
    const uint32_t root = Load32(base, kLevelRoot);
    const uint32_t level = pdz::guestmem::Ok(base, root, 0x2D8 + 4) ? Load32(base, root + 0x2D8) : 0;
    const uint32_t block = pdz::guestmem::Ok(base, level, 0x74 + 4) ? Load32(base, level + 0x74) : 0;
    return Ptr(block) ? block + kRecords : 0;
  }
  void Apply(uint8_t *base, uint32_t records)
  {
    std::memcpy(g_view.real, base + records, kRecordBytes);
    std::memset(base + records, 0, kRecordBytes);
    for (uint32_t n = 0; n < kRecordCount; ++n)
    {
      Store32(base, records + n * kRecord + 0x18, 0xFFFFFFFFu); // as Rare's constructor 82151348
    }
    Store32(base, records + 0x00, 1);                                   // active
    Store32(base, records + 0x04, g_own.text_object + 16);              // the string object: pointer,
    Store32(base, records + 0x08, Load32(base, g_own.text_object + 4)); // length,
    Store32(base, records + 0x0C, 0xFFFFFFFFu);                         // static
    Store32(base, records + 0x10, 0);                                   // kind 0 = primary
    g_view.applied = true;
    g_view.at = records;
  }
  void Remove(uint8_t *base)
  {
    std::memcpy(base + g_view.at, g_view.real, kRecordBytes);
    g_view.applied = false;
  }
  template <class Call> void WithView(PPCContext &ctx, uint8_t *base, bool own, Call call)
  {
    const uint32_t records = own ? Records(base) : 0;
    const std::string spec = REXCVAR_GET(pdz_counterop_objective_text);
    if (own && (!records || spec.empty() || !Fill(base, g_own.text_object, Spec(ctx, base, spec))))
    {
      own = false;
    }
    if (own == g_view.applied)
    {
      call(); // also a nested message for the same player's scene
      return;
    }
    if (own)
    {
      Apply(base, records);
      call();
      Remove(base);
    }
    else
    { // a nested message for the agent's scene while player 2's is being handled: the real records for it
      const uint32_t at = g_view.at;
      Remove(base);
      call();
      Apply(base, at);
    }
  }

  uint32_t Player2Pad(const uint8_t *base)
  {
    if (!PdzCounterOpOn())
    {
      return 0xFF;
    }
    if (const uint32_t p2 = PdzCounterOpPlayer2())
    {
      // Sun 22 Nov night (audit): a readable human, a readable participant (else Rare's list as it is).
      if (!pdz::guestmem::Ok(base, p2, kParticipantOf + 4) || Load32(base, p2) != kHumanVtable)
      {
        return 0xFF;
      }
      const uint32_t part = Load32(base, p2 + kParticipantOf);
      return pdz::guestmem::Ok(base, part, kPad + 4) ? (Load32(base, part + kPad) & 0xFF) : 0xFF;
    }
    return 0xFF; // no Counter-Op game runs: Rare's list as it is
  }
  uint32_t PadOfScene(PPCContext &ctx, uint8_t *base, uint32_t handle)
  {
    if (!handle)
    {
      return 0xFF;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = kFrontend;
    frame.ctx.r4.u64 = handle;
    sub_823A2A00(frame.ctx, base);
    return frame.ctx.r3.u32 & 0xFF;
  }
  // Is this message one of those that read the records (no pad lookup for the others)?
  bool Reads(const uint8_t *base, uint32_t message, std::initializer_list<uint32_t> ids)
  {
    if (!Ptr(message) || !REXCVAR_GET(pdz_counterop_own_objective) || !REXCVAR_GET(pdz_counterop_objective_pause_list))
    {
      return false;
    }
    const uint32_t id = Load32(base, message + 4);
    return std::find(ids.begin(), ids.end(), id) != ids.end();
  }
  void LogView(const uint8_t *base, const char *which, uint32_t message, uint32_t scene_pad, uint32_t p2_pad, bool own)
  {
    if (g_view.logged < 12 && p2_pad < 4)
    {
      ++g_view.logged;
      REXLOG_INFO("pdz_counterop: objectives: pause menu: {} message {:#x} for the scene of pad {} (player 2 is pad "
                  "{}; frontend mode word {}): {}",
                  which, Load32(base, message + 4), scene_pad, p2_pad, Load32(base, kFrontend + 4),
                  own ? "his own objective is listed (the agent's records are swapped out for this call)"
                      : "the agent's list, as Rare's");
    }
  }
} // namespace

// sub_823DD198(this r3, XUIMessage* r4): DarkXuiObjectiveList. this +4 = its XUI handle. Messages (message +4):
// 0x0E selection changed, 0 state visuals, 0x7DC item count, 0x7DF item text, 0x7E1 item image.
REX_HOOK_RAW(sub_823DD198)
{
  const uint32_t self = ctx.r3.u32, message = ctx.r4.u32;
  bool own = false;
  if (Ptr(message) && Load32(base, message + 4) == 0x13 && self == g_view.counted_list)
  {
    g_view.counted_list = 0; // the list is set up again (the menu was opened again at the same address)
  }
  if (Ptr(self) && Reads(base, message, {0x0u, 0x0Eu, 0x7DCu, 0x7DFu, 0x7E1u}))
  {
    const uint32_t pad = Player2Pad(base);
    if (pad < 4)
    {
      PPCContext saved = ctx;
      const uint32_t scene = PadOfScene(saved, base, Load32(base, self + 4));
      own = scene == pad;
      LogView(base, "list", message, scene, pad, own);
    }
  }
  // The list asks for its item count (0x7DC) while it is being built, before its scene belongs to a player (823A2A00
  // then answers with player 1's pad), so his list had as many rows as the agent has objectives. The first time a
  // message of this list resolves to player 2, the count is asked once more through Rare's own handler (823E14E8
  // adds or removes rows to match), with his one record in view.
  if (own && self != g_view.counted_list)
  {
    g_view.counted_list = self;
    if (!g_view.message)
    {
      g_view.message = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x60);
    }
    if (g_view.message)
    {
      std::memset(base + g_view.message, 0, 0x60);
      Store32(base, g_view.message + 4, 0x7DC);
      Store32(base, g_view.message + 0x10, g_view.message + 0x30); // data: [0] = 0 (all items), [+4] = the count out
      PPCContext recount = ctx;
      WithView(recount, base, true, [&] {
        rex::CallFrame frame(recount);
        frame.ctx.r3.u64 = self;
        frame.ctx.r4.u64 = g_view.message;
        __imp__sub_823DD198(frame.ctx, base);
      });
      REXLOG_INFO("pdz_counterop: objectives: pause menu: player 2's list {:08X} counted again with his own record: "
                  "{} row(s)",
                  self, Load32(base, g_view.message + 0x34));
    }
  }
  WithView(ctx, base, own, [&] { __imp__sub_823DD198(ctx, base); });
}

// sub_823DDC00(this r3, XUIMessage* r4): DarkXuiObjectiveMessageList (the selected objective's d-PAL messages).
REX_HOOK_RAW(sub_823DDC00)
{
  const uint32_t self = ctx.r3.u32, message = ctx.r4.u32;
  bool own = false;
  if (Ptr(self) && Reads(base, message, {0x0u, 0x7DCu}))
  {
    const uint32_t pad = Player2Pad(base);
    if (pad < 4)
    {
      PPCContext saved = ctx;
      own = PadOfScene(saved, base, Load32(base, self + 4)) == pad;
    }
  }
  WithView(ctx, base, own, [&] { __imp__sub_823DDC00(ctx, base); });
}

// sub_823DDF20(this r3, XUIMessage* r4): DarkXuiObjectiveMessageBox. Its pad is cached in the byte this +0x14 (0xFF =
// not yet: Rare then takes 823A2A00(frontend, the PARENT of this +4)).
REX_HOOK_RAW(sub_823DDF20)
{
  const uint32_t self = ctx.r3.u32, message = ctx.r4.u32;
  bool own = false;
  if (Ptr(self) && Reads(base, message, {0x0Au, 0x7DFu, 0x7E1u}))
  {
    const uint32_t pad = Player2Pad(base);
    uint32_t scene = base[self + 0x14];
    if (pad < 4 && scene == 0xFF)
    {
      if (!g_view.scratch)
      {
        g_view.scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(8);
      }
      if (g_view.scratch)
      {
        Store32(base, g_view.scratch, 0);
        PPCContext saved = ctx;
        {
          rex::CallFrame frame(saved);
          frame.ctx.r3.u64 = Load32(base, self + 4);
          frame.ctx.r4.u64 = g_view.scratch;
          sub_8278A450(frame.ctx, base);
        }
        scene = PadOfScene(saved, base, Load32(base, g_view.scratch));
      }
    }
    own = pad < 4 && scene == pad;
  }
  WithView(ctx, base, own, [&] { __imp__sub_823DDF20(ctx, base); });
}

// counter_op.cpp: at a level's Start (where PdzCounterOpGreetReset() is called, ~2354).
void PdzCounterOpObjectiveReset()
{
  g_view.counted_list = 0;
  g_view.logged = 0;
  g_own.shown_hud = 0;
  g_own.dropped = 0;
}

// counter_op.cpp: when player 2 has taken a guard (after SpawnAt succeeded), game thread.
void PdzCounterOpObjectiveLife(PPCContext &ctx, uint8_t *base, uint32_t p2c)
{
  if (REXCVAR_GET(pdz_counterop_own_objective) && Ptr(p2c) &&
      (REXCVAR_GET(pdz_counterop_objective_each_life) || g_own.shown_hud != HudOf(base, p2c)))
  {
    ShowOwn(ctx, base, p2c, 0);
  }
}

// sub_823B1E88(character r3, &title r4, &text r5, state r6): one player's objective lines (HUD kinds 0x14 + 0x15).
// Counter-Op: the agent's lines are not sent to player 2. At the agent's first NEW objective of the level he gets
// his own line instead, with Rare's title of that very call (so both halves change in the same frame).
REX_HOOK_RAW(sub_823B1E88)
{
  const uint32_t character = ctx.r3.u32, title = ctx.r4.u32, state = ctx.r6.u32;
  if (!REXCVAR_GET(pdz_counterop_own_objective) || !IsPlayer2(base, character))
  {
    __imp__sub_823B1E88(ctx, base);
    return;
  }
  if (!g_own.probed)
  {
    g_own.probed = true; // what Rare's titles say (the helper could not read the tables)
    for (uint32_t id : {0x119u, 0x11Au, 0x11Bu, 0x40u, 0x41u, 0xEFu})
    {
      REXLOG_INFO("pdz_counterop: objectives: Rare's string {:X} = '{}'", id, Ascii(Rare(ctx, base, id)));
    }
  }
  if (++g_own.dropped <= 6 || g_own.dropped % 50 == 0)
  {
    REXLOG_INFO("pdz_counterop: objectives: the agent's objective line (state {}, from {:08X}) not shown to player 2 "
                "(#{})",
                state, uint32_t(ctx.lr), g_own.dropped);
  }
  // Round 7: his own line is not sent here (the agent's first objective can come during the opening cutscene, where
  // a 10 s HUD line would pass unseen): it is sent when he takes his first guard of the level (PdzCounterOpObjectiveLife),
  // always under Rare's "NEW PRIMARY OBJECTIVE - " title.
  (void)title;
}

// sub_823B1560(kind r3, &string r4, f1 seconds): a HUD text on every pad's HUD (loop of 823B2978 over 0x82E2F594).
// Counter-Op: kind 0x16 (the objective-message notice of 821518B0) skips player 2's HUD.
REX_HOOK_RAW(sub_823B1560)
{
  const uint32_t kind = ctx.r3.u32, text = ctx.r4.u32;
  const uint32_t p2 = PdzCounterOpPlayer2();
  if (kind != kKindObjectiveMessages || !p2 || !REXCVAR_GET(pdz_counterop_own_objective) ||
      !REXCVAR_GET(pdz_counterop_objective_drop_messages))
  {
    __imp__sub_823B1560(ctx, base);
    return;
  }
  const uint32_t skip = HudOf(base, p2);
  const double seconds = ctx.f1.f64;
  for (uint32_t pad = 0; pad < 4; ++pad)
  {
    const uint32_t hud = Load32(base, kHudTable + pad * 4);
    if (!Ptr(hud) || hud == skip)
    {
      continue;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = hud;
    frame.ctx.r4.u64 = kind;
    frame.ctx.r5.u64 = text;
    frame.ctx.r7.u64 = 0;
    frame.ctx.f1.f64 = seconds;
    sub_823B2978(frame.ctx, base);
  }
}
