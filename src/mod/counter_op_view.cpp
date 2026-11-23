// Counter-Operative, round 2: how player 2 is presented (built only with PDZ_COUNTEROP; every part is behind the
// mode, so with Counter-Op off nothing here runs). Evidence and addresses: research notes: round2\b\FINDINGS.md.
//
// Design items:
//  4  While player 2 is out of the world (counter_op.cpp's HideP2: Rare's hide steps, the action lock), his half of
//     the split screen is dark with one short centred line: "Choosing a guard..." while a guard is being found,
//     "No guards left" once he is out for the rest of the mission. Drawn as a native overlay (ImGui, plain font) over
//     his viewport: Rare's two-player split is top / bottom, player 2 the bottom half (round 1 frames); the line
//     between the halves is the middle of the window whether the game image is letter- or pillar-boxed, so the bottom
//     half of the window covers his view exactly (the bars are black anyway). The overlay shows only while
//     counter_op.cpp refreshes the state every level tick (so it vanishes by itself when the level stops ticking:
//     menus, loading, the end), and never in a cutscene (Rare plays those full screen).
//     ShowP2's missing step: HideP2 runs Rare's 82189B08, which frees the character's perception / noise slot
//     (+0x354 -> -1; table 82E5D590, 170 slots of 0x50 bytes, player_limit.cpp's MB5 note); Rare's restore
//     (825818E0) takes a new one with 82189918, ShowP2 did not.
//     PdzCounterOpViewRestorePerception() takes it back with the human constructor's own arguments (825A6918).
//  7  Nothing here: player 2 is drawn for the agent once ShowP2 has run (round 1's Rooftops frame shows him as the
//     henchman in the agent's view). In the hand test he was never shown while the agent could see him (Datacore: no
//     clone was ever eligible, so he stayed out of the world the whole mission; Subway: out until his take 116 m away).
//  9  The co-op MISSION SUMMARY (aid_darkxui_frontend_coop_local_start) shows each player's loadout in Rare's boxes
//     Inventory1 (DarkXUIPlayer1Box) and Inventory2 (DarkXUIPlayer2Box). Their slots latch Rare's "current menu user"
//     byte 82E2F5EC at their init; Player1Box's init (823D0960, message 0x13) sets it to player 1's user, Player2Box's
//     handler (823D0AE0) is an empty stub, so player 2's box always shows player 1's (the agent's) items. For a
//     counter-op game the box is hidden and the PLAYER 2 INVENTORY button above it reads Rare's COUNTER OPERATIVE
//     (frontend text 4 + 5), with no press path (so it doesn't open Rare's buy screen for him).
// 10  First-person arms: a guard body has no player hands (+16 "_noname_"); Rare's own fallback for that is the
//     trooper hands (82414070's second name), which only Trinity Escape ships. Every SP level ships these player
//     hands: chandra, jack, jo_adventure, jo_bountyhunter, jo_carrington, johnathan, killian, maihem_hands_burnt,
//     skeleton_xray; plus CI_swat_hands in Jungle, Temple, Outpost and Bridge, trooper_hands in Trinity Escape, and
//     jo_blackleather in Datacore (package scan, FINDINGS). PdzCounterOpViewHands() picks by the body's animation set
//     (Rare's "henchman" set = a male body, "jo" set = a female body) the first of a list Rare's lookup finds.
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/imgui_dialog.h>
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DEFINE_BOOL(pdz_counterop_dark_view, true, "Gameplay",
                    "Counter-Operative (default on): while player 2 waits for a guard (out of the world) his half of "
                    "the split screen is dark with a short line ('Choosing a guard...', 'No guards left'). Off = his "
                    "view follows the agent as in round 1")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 4 (item 7): set while Rare's own HUD shows player 2's dark panel and line (below): the overlay then draws
// nothing over his half.
std::atomic<bool> g_rare_dark_drawn{false};
REXCVAR_DEFINE_INT32(pdz_counterop_view_half, 0, "Gameplay",
                     "Counter-Operative: where player 2's view is on screen for the dark overlay. 0 = the bottom half "
                     "(Rare's two-player split), 1 = the right half")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_hands_male, "trooper_hands,CI_swat_hands,johnathan_hands,jack_hands,killian_hands",
                      "Gameplay",
                      "Counter-Operative: player 2's first-person hands on a male guard body (Rare's 'henchman' "
                      "animation set), the first of these the level ships (aid_darkcharacter_xenonhands_<name>)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_hands_female, "jo_carrington_hands,maihem_hands_burnt,chandra_hands", "Gameplay",
                      "Counter-Operative: player 2's first-person hands on a female guard body (Rare's 'jo' animation "
                      "set), the first of these the level ships")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_plain_overlay, false, "Testing",
                    "Counter-Operative (testing, default off): when Rare's HUD can't show player 2's waiting line, "
                    "draw a black half with the line in the plain overlay font (rounds 2-5). Off = draw nothing then")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(sub_827909A0); // XUI: give an element the focus for a user (element, user or 0xFF = all, 0)

REXCVAR_DEFINE_BOOL(pdz_counterop_summary_tree, false, "Testing",
                    "TESTING TOOL (counter-op): log every element (Id, Text) of the co-op MISSION SUMMARY scene")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_summary_panel, true, "Gameplay",
                    "Counter-Operative (default on): on the co-op MISSION SUMMARY, player 2's loadout box is hidden "
                    "and his inventory button reads COUNTER OPERATIVE (Rare's box shows the agent's items for him)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82413FD8); // an asset by name: (name, fallback name or 0) -> asset or 0
REX_EXTERN(sub_82189918); // take a perception / noise slot (character, ...; stack +0x5C / +0x64); 825A6918 for a human
REX_EXTERN(sub_8239BCD8); // a string by id ((table << 16) | index) -> UTF-16 pointer or 0
// XUI (as counter_op_menu.cpp / mp_music.cpp use them)
REX_EXTERN(sub_8278A4C8); // first child (h, &out)
REX_EXTERN(sub_8278A5B8); // next sibling (h, &out)
REX_EXTERN(sub_8277B7F8); // property id by name (h, name, &id)
REX_EXTERN(sub_8277F928); // get property (h, id, 0, &val)
REX_EXTERN(sub_8277FA28); // set property (h, id, 0, &val)
REX_EXTERN(sub_82794EC0); // XUIElementPropVal clear
REX_EXTERN(sub_827950E0); // XUIElementPropVal set string

namespace
{
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

  std::string GuestString(const uint8_t *base, uint32_t p, size_t max = 63)
  {
    std::string out;
    // Sun 22 Nov night (audit): each byte read only where mapped
    for (size_t i = 0; Ptr(p) && i < max && pdz::guestmem::Ok(base, p + uint32_t(i), 1) && base[p + i]; ++i)
    {
      out += char(base[p + i]);
    }
    return out;
  }

  // ---- Item 4: the dark view ------------------------------------------------------------------------------------

  using Clock = std::chrono::steady_clock;
  std::atomic<int> g_view{0};             // PdzCounterOpViewSet's last state
  std::atomic<int64_t> g_view_at_ms{0};   // when it was set (the overlay needs a fresh state)
  constexpr int64_t kStaleMs = 250;       // no refresh for this long = no overlay (the level stopped ticking)

  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  }

  const char *ViewText(int view)
  {
    switch (view)
    {
    case 1:
      return "DEPLOYING..."; // round 8 (the owner's wording; rounds 2-7: "Choosing a guard...")
    case 2:
      return "No guards left";
    case 3:
      return "AWAITING DEPLOYMENT"; // round 8 (rounds 3-7: "No enemy guards yet..."): no guard he may take is in play
    case 4:
      return "NO MORE DEPLOYMENTS"; // round 14: Datacore's end, the jetpack riders are gone: out until the level ends
    default:
      return nullptr;
    }
  }

  class DarkViewDialog final : public rex::ui::ImGuiDialog
  {
  public:
    explicit DarkViewDialog(rex::ui::ImGuiDrawer *drawer) : ImGuiDialog(drawer) {}

  protected:
    void OnDraw(ImGuiIO &io) override
    {
      const int view = g_view.load(std::memory_order_relaxed);
      const char *text = ViewText(view);
      if (!text || !REXCVAR_GET(pdz_counterop_dark_view) ||
          NowMs() - g_view_at_ms.load(std::memory_order_relaxed) > kStaleMs)
      {
        return;
      }
      const float w = io.DisplaySize.x, h = io.DisplaySize.y;
      if (w < 2.0f || h < 2.0f)
      {
        return;
      }
      const bool right = REXCVAR_GET(pdz_counterop_view_half) == 1;
      const ImVec2 p0 = right ? ImVec2(w * 0.5f, 0.0f) : ImVec2(0.0f, h * 0.5f);
      const ImVec2 p1(w, h);
      ImDrawList *draw = ImGui::GetForegroundDrawList();
      if (g_rare_dark_drawn.load(std::memory_order_relaxed) || !REXCVAR_GET(pdz_counterop_plain_overlay))
      {
        // Round 4 (item 7): Rare's own HUD shows the line. Round 6 (item 8): when it can't (no HUD yet: a loading
        // screen), nothing is drawn at all: never a line in a font that isn't the game's.
        return;
      }
      draw->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));
      ImFont *font = ImGui::GetFont();
      const float size = std::max(ImGui::GetFontSize(), (p1.y - p0.y) * 0.06f);
      const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
      const ImVec2 at((p0.x + p1.x - extent.x) * 0.5f, (p0.y + p1.y - extent.y) * 0.5f);
      draw->AddText(font, size, at, IM_COL32(200, 200, 200, 255), text);
    }
  };
  std::unique_ptr<DarkViewDialog> g_dialog;

  // ---- Item 9: the MISSION SUMMARY ------------------------------------------------------------------------------

  constexpr uint32_t kPropValSize = 20, kPropBool = 1, kPropString = 5;
  constexpr uint32_t kFrontendUsers = 0x82E74070; // Rare's frontend block: +0x14 player 1's pad, +0x15 the partner's, +0x16 the loadout user
  constexpr uint32_t kTextCounter = 0x20004, kTextOperative = 0x20005; // frontend 4 / 5: COUNTER / OPERATIVE

  uint32_t PushWide(PPCContext &frame, uint8_t *base, const std::u16string &text)
  {
    std::string bytes;
    for (char16_t c : text)
    {
      bytes += char(c >> 8);
      bytes += char(c & 0xFF);
    }
    bytes.append(2, '\0');
    return rex::ppc::stack_push(frame, base, bytes.data(), uint32_t(bytes.size()));
  }
  std::u16string ReadWide(const uint8_t *base, uint32_t address, size_t max = 256)
  {
    std::u16string text;
    for (size_t i = 0; Ptr(address) && i < max; ++i)
    {
      const char16_t c = char16_t(base[address + i * 2] << 8 | base[address + i * 2 + 1]);
      if (!c)
      {
        break;
      }
      text += c;
    }
    return text;
  }
  std::u16string Wide(const std::string &s) { return std::u16string(s.begin(), s.end()); }
  std::string Ascii(const std::u16string &s)
  {
    std::string out;
    for (char16_t c : s)
    {
      out += c < 0x80 ? char(c) : '?';
    }
    return out;
  }

  // The few XUI calls the summary needs, on one frame (counter_op_menu.cpp's Xui, trimmed).
  struct Xui
  {
    rex::CallFrame frame;
    uint8_t *base;
    uint32_t val, out;
    Xui(PPCContext &ctx, uint8_t *b) : frame(ctx), base(b)
    {
      val = rex::ppc::stack_push(frame.ctx, base, std::string(kPropValSize, '\0').data(), kPropValSize);
      out = rex::ppc::stack_push<uint32_t>(frame.ctx, base, 0);
    }
    uint32_t PropId(uint32_t h, const std::string &name)
    {
      const uint32_t saved = frame.ctx.r1.u32;
      Store32(base, out, 0xFFFFFFFFu);
      frame.ctx.r3.u64 = h;
      frame.ctx.r4.u64 = PushWide(frame.ctx, base, Wide(name));
      frame.ctx.r5.u64 = out;
      sub_8277B7F8(frame.ctx, base);
      frame.ctx.r1.u32 = saved;
      return int32_t(frame.ctx.r3.u32) < 0 ? 0xFFFFFFFFu : Load32(base, out);
    }
    bool Call(PPCFunc *fn, uint32_t h, const std::string &name)
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
    void Clear()
    {
      frame.ctx.r3.u64 = val;
      sub_82794EC0(frame.ctx, base);
    }
    std::u16string GetText(uint32_t h, const std::string &name)
    {
      std::memset(base + val, 0, kPropValSize);
      std::u16string text;
      if (Call(sub_8277F928, h, name) && Load32(base, val) == kPropString)
      {
        text = ReadWide(base, Load32(base, val + 4));
      }
      Clear();
      return text;
    }
    bool SetText(uint32_t h, const std::string &name, const std::u16string &text)
    {
      const uint32_t saved = frame.ctx.r1.u32;
      frame.ctx.r3.u64 = val;
      frame.ctx.r4.u64 = PushWide(frame.ctx, base, text);
      sub_827950E0(frame.ctx, base);
      frame.ctx.r1.u32 = saved;
      const bool ok = Call(sub_8277FA28, h, name);
      Clear();
      return ok;
    }
    bool SetBool(uint32_t h, const std::string &name, bool value)
    {
      std::memset(base + val, 0, kPropValSize);
      Store32(base, val, kPropBool);
      Store32(base, val + 4, value ? 1u : 0u);
      return Call(sub_8277FA28, h, name);
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
    uint32_t Find(uint32_t h, const std::string &id, int depth = 0)
    {
      if (depth > 12)
      {
        return 0;
      }
      int n = 0;
      for (uint32_t c = Child(h, true); c && n < 256; c = Child(c, false), ++n)
      {
        if (Ascii(GetText(c, "Id")) == id)
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
  };

  std::u16string RareText(PPCContext &ctx, uint8_t *base, uint32_t id)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = id;
    sub_8239BCD8(frame.ctx, base);
    return ReadWide(base, frame.ctx.r3.u32);
  }

  // ---- Item 10: the hands --------------------------------------------------------------------------------------

  constexpr uint32_t kBodyAnimName = 12; // a body list entry's animation set name (822788D8 resolves it into +64)
  constexpr const char *kHandsPrefix = "aid_darkcharacter_xenonhands_";
  uint32_t g_name_scratch = 0;
  constexpr uint32_t kNameScratchSize = 96;

  std::vector<std::string> SplitList(const std::string &list)
  {
    std::vector<std::string> out;
    std::string item;
    for (const char ch : list + ",")
    {
      if (ch == ',')
      {
        while (!item.empty() && item.back() == ' ')
        {
          item.pop_back();
        }
        if (!item.empty())
        {
          out.push_back(item);
        }
        item.clear();
      }
      else if (ch != ' ' || !item.empty())
      {
        item += ch;
      }
    }
    return out;
  }

  // ---- Item 4: the perception / noise slot ---------------------------------------------------------------------

  constexpr uint32_t kPerceptionSlot = 0x354; // a character's perception / noise slot (82189B08 frees it: -1)
} // namespace

// ==== Call-in points ================================================================================================

namespace pdz::counterop_view
{
  // perfectdarkzerorecomp_app.h, OnCreateDialogs (with PDZ_COUNTEROP): the overlay dialog.
  void Init(rex::ui::ImGuiDrawer *drawer) { g_dialog = std::make_unique<DarkViewDialog>(drawer); }
} // namespace pdz::counterop_view

// counter_op.cpp, every level tick (the 8222F740 hook, after Tick): 0 = nothing, 1 = "Choosing a guard...", 2 = "No
// guards left". The caller passes 0 unless Counter-Op runs in this level, player 2 is out of the world and no
// cutscene plays (see FINDINGS for the exact expression).
void PdzCounterOpViewSet(int view)
{
  if (view == 0 && g_view.load(std::memory_order_relaxed) == 0)
  {
    return; // the usual case (every level tick of every co-op game): nothing to draw, no clock read
  }
  const int before = g_view.exchange(view, std::memory_order_relaxed);
  g_view_at_ms.store(NowMs(), std::memory_order_relaxed);
  if (before != view)
  {
    REXLOG_INFO("pdz_counterop: view: player 2's half of the screen {}",
                view ? fmt::format("dark: '{}'{}", ViewText(view),
                                   REXCVAR_GET(pdz_counterop_dark_view) ? "" : " (pdz_counterop_dark_view off: not drawn)")
                     : std::string("shown (his own view)"));
  }
}

// counter_op.cpp, Start: with the dark view player 2 goes out of the world at once (no wait for his camera).
bool PdzCounterOpViewDark() { return REXCVAR_GET(pdz_counterop_dark_view); }

// counter_op.cpp, ShowP2 (after Rare's restore steps): HideP2's 82189B08 freed his perception / noise slot (+0x354 =
// -1) and nothing took it back (Rare's own restore, 825818E0, calls 82189918 for that). Taken again with the arguments
// the human constructor uses (825A64A8 at 825A68E4..825A6918: r4 1, r5 0, r6 0, r9 1, r10 0, f1 0, f2 0, f3 1.0, stack
// +0x5C -1, +0x64 -1 = find a free slot). Without it, 825BE5D0 reads slot -1's +0x24 (the word before Rare's table).
void PdzCounterOpViewRestorePerception(PPCContext &ctx, uint8_t *base, uint32_t character)
{
  if (!Ptr(character) || int32_t(Load32(base, character + kPerceptionSlot)) != -1)
  {
    return;
  }
  rex::CallFrame frame(ctx);
  frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400u) & ~0xFu; // our own outgoing frame: the callee reads its stack arguments
  Store32(base, frame.ctx.r1.u32 + 0x5C, 0xFFFFFFFFu);
  Store32(base, frame.ctx.r1.u32 + 0x64, 0xFFFFFFFFu);
  frame.ctx.r3.u64 = character;
  frame.ctx.r4.u64 = 1;
  frame.ctx.r5.u64 = 0;
  frame.ctx.r6.u64 = 0;
  frame.ctx.r7.u64 = 0;
  frame.ctx.r8.u64 = 0;
  frame.ctx.r9.u64 = 1;
  frame.ctx.r10.u64 = 0;
  frame.ctx.f1.f64 = 0.0;
  frame.ctx.f2.f64 = 0.0;
  frame.ctx.f3.f64 = 1.0;
  sub_82189918(frame.ctx, base);
  REXLOG_INFO("pdz_counterop: view: player 2's perception / noise slot taken again: +0x354 = {}",
              int32_t(Load32(base, character + kPerceptionSlot)));
}

// counter_op.cpp, BodyUsable: player hands for a guard body that has none of its own (its +16 is "_noname_" and
// Rare's fallback, the trooper hands, is not in the level): the first name of the male or female list that Rare's
// lookup (82413FD8, null when missing) finds. 0 = none (the caller keeps the co-op body's hands).
uint32_t PdzCounterOpViewHands(PPCContext &ctx, uint8_t *base, uint32_t body_entry, std::string &why)
{
  if (!Ptr(body_entry))
  {
    why = "no body entry";
    return 0;
  }
  const std::string anim = GuestString(base, Load32(base, body_entry + kBodyAnimName));
  const bool female = anim.find("joanims") != std::string::npos;
  const std::string list = female ? std::string(REXCVAR_GET(pdz_counterop_hands_female))
                                  : std::string(REXCVAR_GET(pdz_counterop_hands_male));
  if (!g_name_scratch)
  {
    g_name_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(kNameScratchSize);
  }
  if (!g_name_scratch)
  {
    why = "no guest memory for the name";
    return 0;
  }
  std::string tried;
  for (const std::string &name : SplitList(list))
  {
    const std::string full = kHandsPrefix + name;
    if (full.size() + 1 > kNameScratchSize)
    {
      continue;
    }
    std::memcpy(base + g_name_scratch, full.c_str(), full.size() + 1);
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = g_name_scratch;
    frame.ctx.r4.u64 = 0;
    sub_82413FD8(frame.ctx, base);
    const uint32_t asset = frame.ctx.r3.u32;
    if (Ptr(asset))
    {
      why = fmt::format("{} body (animation '{}'): '{}' ({:08X}){}", female ? "a female" : "a male", anim, name, asset,
                        tried.empty() ? "" : "; not in this level: " + tried);
      return asset;
    }
    tried += (tried.empty() ? "" : ", ") + name;
  }
  why = fmt::format("{} body (animation '{}'): none of '{}' is in this level", female ? "a female" : "a male", anim,
                    list);
  return 0;
}

namespace
{
  // pdz_counterop_summary_tree: one line per element (depth, Id, Text, shown or not).
  void DumpTree(Xui &x, uint32_t h, int depth, std::string &out)
  {
    int n = 0;
    for (uint32_t c = depth > 10 ? 0 : x.Child(h, true); c && n < 256; c = x.Child(c, false), ++n)
    {
      out += fmt::format("\n  {}{} '{}'", std::string(size_t(depth) * 2, ' '), Ascii(x.GetText(c, "Id")),
                         Ascii(x.GetText(c, "Text")));
      DumpTree(x, c, depth + 1, out);
    }
  }
} // namespace

// counter_op_menu.cpp, PdzCounterOpSceneCreated (while armed, a "frontend_coop_" scene): the MISSION SUMMARY's
// player 2 column. true = the scene was the summary.
bool PdzCounterOpViewSceneCreated(PPCContext &caller, uint8_t *base, const std::string &path, uint32_t scene)
{
  if (path.find("frontend_coop_local_start") == std::string::npos || !Ptr(scene) ||
      !REXCVAR_GET(pdz_counterop_summary_panel))
  {
    return false;
  }
  PPCContext ctx = caller;
  Xui x(ctx, base);
  if (REXCVAR_GET(pdz_counterop_summary_tree))
  {
    std::string tree;
    DumpTree(x, scene, 0, tree);
    REXLOG_INFO("pdz_counterop: view: summary tree:{}", tree);
  }
  // Round 3 (item 9). In real play his column showed an empty box with WEAPONS / GADGETS headings (they are the
  // skin of the box button 'SelectWeapons2' itself; its Text is not drawn), and a press on it could leave the menu
  // without a usable focus (Rare's press handler 823D0650 makes player 2 the menu user for a loadout screen that the
  // cleared press path never opened). Now: the column's title 'PLAYER 2' reads Rare's COUNTER OPERATIVE; the box
  // button and the item box under it are hidden AND disabled (nothing in his column can take focus or a press); the
  // other controls' navigation links to it are cut; Rare's "whose loadout" byte is cleared and every pad's focus is
  // put on START MISSION, so A starts the mission and B backs out from either pad.
  const uint32_t box = x.Find(scene, "Inventory2");
  const uint32_t button = x.Find(scene, "SelectWeapons2");
  const uint32_t title = x.Find(scene, "PLAYER 2");
  const uint32_t start = x.Find(scene, "StartGame");
  const uint32_t p1button = x.Find(scene, "SelectWeapons");
  std::u16string label = RareText(x.frame.ctx, base, kTextCounter);
  const std::u16string second = RareText(x.frame.ctx, base, kTextOperative);
  label = label.empty() || second.empty() ? std::u16string(u"COUNTER OPERATIVE") : label + u' ' + second;
  const bool titled = title && x.SetText(title, "Text", label);
  const bool box_hidden = box && x.SetBool(box, "Show", false);
  if (box)
  {
    x.SetBool(box, "Enabled", false);
  }
  const bool button_hidden = button && x.SetBool(button, "Show", false);
  const bool button_off = button && x.SetBool(button, "Enabled", false);
  int relinked = 0;
  std::string links;
  for (const char *id : {"SelectWeapons", "StartGame", "BackButton1"})
  {
    const uint32_t e = x.Find(scene, id);
    for (const char *nav : {"NavLeft", "NavRight", "NavUp", "NavDown"})
    {
      const std::string to = e ? Ascii(x.GetText(e, nav)) : std::string();
      if (!to.empty())
      {
        links += fmt::format(" {}.{}={}", id, nav, to);
      }
      if (to == "SelectWeapons2")
      {
        const bool vertical = nav[3] == 'U' || nav[3] == 'D';
        relinked += x.SetText(e, nav, vertical && std::string(id) != "SelectWeapons" ? std::u16string(u"SelectWeapons")
                                                                                       : std::u16string())
                        ? 1
                        : 0;
      }
    }
  }
  base[kFrontendUsers + 0x16] = 0xFF; // Rare's "whose loadout is being edited" (823D07FC writes the same)
  bool focused = false;
  if (const uint32_t target = start ? start : p1button)
  {
    x.frame.ctx.r3.u64 = target;
    x.frame.ctx.r4.u64 = 0xFF; // every pad (Rare's own call at 823D53D0)
    x.frame.ctx.r5.u64 = 0;
    sub_827909A0(x.frame.ctx, base);
    focused = int32_t(x.frame.ctx.r3.u32) >= 0;
  }
  REXLOG_INFO("pdz_counterop: view: MISSION SUMMARY: column title 'PLAYER 2' -> '{}' {}; box button 'SelectWeapons2' {} "
              "and {}, item box 'Inventory2' {}; {} navigation link(s) to it cut (links:{}); every pad's focus on {} {}",
              Ascii(label), titled ? "set" : "NOT set", button_hidden ? "hidden" : "NOT hidden",
              button_off ? "disabled" : "NOT disabled", box_hidden ? "hidden" : "NOT hidden", relinked, links,
              start ? "START MISSION" : "PLAYER 1 INVENTORY", focused ? "set" : "NOT set");  return true;
}

// ---- Round 4 (item 7): player 2's waiting line in Rare's own HUD text, his half dark from inside the game -------
// His HUD is an XUI scene ([0x82E2F594 + pad * 4] -> HUD object, +0 = the scene handle; the HUD message kinds are
// its labels, table 0x82D28140). The dark is the figure of the scene's own 'textMatchOver' group stretched over the
// scene; the line is that group's XuiText or a HUD message of a kind drawn after it.
REXCVAR_DEFINE_BOOL(pdz_counterop_rare_text, true, "Gameplay",
                    "Counter-Operative (default on, round 4): player 2's waiting lines ('Choosing a guard...', 'No "
                    "enemy guards yet...', 'No guards left') are drawn by the game's own HUD in Rare's text (its "
                    "notification line), with the rest of his HUD hidden. Off = the dark overlay with plain text")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_rare_text_kind, -1, "Gameplay",
                     "Counter-Operative: -1 = automatic (the text of Rare's MATCH OVER panel when his HUD has one, "
                     "else the notification line 0x10); 0x04 (textHint), 0x10 (textNotification), 0x11 (textInfo), "
                     "0x02 (textAction) = sent as that HUD message "
                     "(only the kinds drawn after the panel show on it)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_hud_tree, false, "Testing",
                    "Counter-Operative (testing): log player 2's HUD scene (element Ids, sizes, shown) when the dark "
                    "panel is first put up")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_823B2978); // HUD message: (hud, kind, &string, f1 seconds / 0 default / -1 stays, r7 0)
REX_EXTERN(sub_823B2B50); // HUD messages of a kind off: (hud, kind, r5 0 = let them end now, 1 = drop at once)

namespace
{
  constexpr uint32_t kHudTable = 0x82E2F594;        // [pad * 4] = the pad's HUD object; +0 = its XUI scene handle
  constexpr uint32_t kPropFloat = 4, kPropVector = 7; // XUI_DATATYPE_* (bool 1 and string 5 are the ones in use)

  struct RareDark
  {
    uint32_t hud = 0, scene = 0, group = 0, figure = 0, text = 0;
    bool on = false, no_panel = false;
    int view = 0;
    int32_t kind = -2; // the HUD message kind the line was last sent as
    int sent_view = 0;
    // what Rare's scene had, to put back
    float fig_w = 0, fig_h = 0, fig_x = 0, fig_y = 0;
    std::u16string old_text;
    std::vector<uint32_t> hidden; // HUD elements we hid (they were shown)
    std::vector<float> hidden_opacity; // their opacity (no-panel path), same order; empty on the panel path
    int64_t sent_ms = 0;               // when the line was last sent as a HUD message
  };
  RareDark g_rd;

  void StoreFloat(uint8_t *base, uint32_t a, float f)
  {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    Store32(base, a, u);
  }
  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t u = Load32(base, a);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  }
  bool SetFloat(Xui &x, uint32_t h, const char *name, float v)
  {
    std::memset(x.base + x.val, 0, kPropValSize);
    Store32(x.base, x.val, kPropFloat);
    StoreFloat(x.base, x.val + 4, v);
    return x.Call(sub_8277FA28, h, name);
  }
  bool GetFloat(Xui &x, uint32_t h, const char *name, float &v)
  {
    std::memset(x.base + x.val, 0, kPropValSize);
    const bool ok = x.Call(sub_8277F928, h, name) && Load32(x.base, x.val) == kPropFloat;
    if (ok)
    {
      v = LoadFloat(x.base, x.val + 4);
    }
    return ok;
  }
  bool SetVec(Xui &x, uint32_t h, const char *name, float vx, float vy)
  {
    std::memset(x.base + x.val, 0, kPropValSize);
    Store32(x.base, x.val, kPropVector);
    StoreFloat(x.base, x.val + 4, vx);
    StoreFloat(x.base, x.val + 8, vy);
    return x.Call(sub_8277FA28, h, name);
  }
  bool GetVec(Xui &x, uint32_t h, const char *name, float &vx, float &vy)
  {
    std::memset(x.base + x.val, 0, kPropValSize);
    const bool ok = x.Call(sub_8277F928, h, name) && Load32(x.base, x.val) == kPropVector;
    if (ok)
    {
      vx = LoadFloat(x.base, x.val + 4);
      vy = LoadFloat(x.base, x.val + 8);
    }
    return ok;
  }

  // The HUD elements that are drawn AFTER textMatchOver in Rare's scene (they would show on the dark panel).
  constexpr const char *kAfterPanel[] = {"textCommsRing", "textContext", "textObjectiveTitle", "textObjectiveInfo",
                                         "AmmoBarL", "AmmoMetersL", "AmmoBarR", "AmmoMetersR", "barArmour",
                                         "barMaxHealth", "barHealth", "barAction", "textObjectiveMessages",
                                         "imageObjectiveMessages", "PickUpDropScene", "SpeakerIcon"};

  // The HUD message kind the line is sent as: the cvar's, or, when his scene has no panel, textNotification (0x10:
  // Rare's centred notification line, the larger blue text; tried on Datacore with 0x04 / 0x11 / 0x02, which are the
  // small white lines, and 0x01, which the HUD's own prompt update clears at once).
  int32_t RareKind()
  {
    const int32_t kind = REXCVAR_GET(pdz_counterop_rare_text_kind);
    return kind >= 0 && kind <= 0x1A ? kind : g_rd.no_panel ? 0x10 : -1;
  }

  void HudTree(Xui &x, uint32_t h, int depth)
  {
    int n = 0;
    for (uint32_t c = x.Child(h, true); c && n < 80 && depth < 3; c = x.Child(c, false), ++n)
    {
      float w = -1, hh = -1, px = 0, py = 0, op = -1;
      GetFloat(x, c, "Width", w);
      GetFloat(x, c, "Height", hh);
      GetVec(x, c, "Position", px, py);
      GetFloat(x, c, "Opacity", op);
      REXLOG_INFO("pdz_counterop: hud tree: {}{:08X} '{}' {:.0f} x {:.0f} at {:.0f},{:.0f} opacity {:.2f} text '{}'",
                  std::string(size_t(depth) * 2, ' '), c, Ascii(x.GetText(c, "Id")), w, hh, px, py, op,
                  Ascii(x.GetText(c, "Text")).substr(0, 40));
      HudTree(x, c, depth + 1);
    }
  }

  void DarkOff(PPCContext &ctx, uint8_t *base)
  {
    if (!g_rd.on)
    {
      return;
    }
    // The HUD object may be gone (level end): only touch it if the table still holds the same object and scene.
    bool live = false;
    for (uint32_t pad = 0; pad < 4; ++pad)
    {
      live = live || (Load32(base, kHudTable + pad * 4) == g_rd.hud && Ptr(g_rd.hud) &&
                      Load32(base, g_rd.hud) == g_rd.scene);
    }
    if (live)
    {
      PPCContext copy = ctx;
      Xui x(copy, base);
      if (g_rd.group)
      {
        x.SetBool(g_rd.group, "Show", false);
      }
      if (g_rd.figure)
      {
        SetFloat(x, g_rd.figure, "Width", g_rd.fig_w);
        SetFloat(x, g_rd.figure, "Height", g_rd.fig_h);
        SetVec(x, g_rd.figure, "Position", g_rd.fig_x, g_rd.fig_y);
      }
      if (g_rd.text)
      {
        x.SetText(g_rd.text, "Text", g_rd.old_text);
      }
      for (size_t i = 0; i < g_rd.hidden.size(); ++i)
      {
        x.SetBool(g_rd.hidden[i], "Show", true);
        if (i < g_rd.hidden_opacity.size())
        {
          SetFloat(x, g_rd.hidden[i], "Opacity", g_rd.hidden_opacity[i]);
        }
      }
      const int32_t kind = RareKind();
      if (kind >= 0 && kind <= 0x1A)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = g_rd.hud;
        frame.ctx.r4.u64 = uint32_t(kind);
        frame.ctx.r5.u64 = 1; // round 6 (item 8): dropped at once (0 let it fade out over a starting cutscene)
        sub_823B2B50(frame.ctx, base);
      }
    }
    REXLOG_INFO("pdz_counterop: rare text: dark panel off ({})", live ? "Rare's HUD restored" : "the HUD was gone");
    g_rd = RareDark{};
  }
} // namespace

// counter_op.cpp, every level tick, where PdzCounterOpViewSet(view) is called (game thread, inside a guest call).
// True when Rare's HUD shows the dark panel and the line (false = the overlay draws its rectangle and text).
bool PdzCounterOpRareDark(PPCContext &ctx, uint8_t *base, int view, int32_t pad)
{
  if (view == 0 || !REXCVAR_GET(pdz_counterop_rare_text) || !REXCVAR_GET(pdz_counterop_dark_view) || pad < 0 || pad > 3)
  {
    DarkOff(ctx, base);
    g_rare_dark_drawn.store(false, std::memory_order_relaxed);
    return false;
  }
  const uint32_t hud = Load32(base, kHudTable + uint32_t(pad) * 4);
  const uint32_t scene = Ptr(hud) ? Load32(base, hud) : 0; // 823B2EE0: the HUD object's +0 is its scene handle
  if (!scene)
  {
    g_rare_dark_drawn.store(false, std::memory_order_relaxed);
    return false;
  }
  if (g_rd.on && (g_rd.hud != hud || g_rd.scene != scene))
  {
    g_rd = RareDark{}; // a new HUD (new level): the old handles are dead, nothing to restore
  }
  // (a max-1 kind's repeated message replaces the entry without a new fade: sent again every 2 s, so a notification
  // of Rare's own that took the label's place doesn't leave his half blank)
  if (g_rd.on && g_rd.view == view && g_rd.kind == RareKind() && (g_rd.kind < 0 || NowMs() - g_rd.sent_ms < 2000))
  {
    return true; // nothing per frame: XUI keeps the state
  }
  PPCContext copy = ctx;
  Xui x(copy, base);
  if (!g_rd.on)
  {
    if (REXCVAR_GET(pdz_counterop_hud_tree))
    {
      HudTree(x, scene, 0);
    }
    g_rd.hud = hud;
    g_rd.scene = scene;
    g_rd.group = x.Find(scene, "textMatchOver");
    if (!g_rd.group)
    {
      // His scene (the 2-player co-op HUD) has no such panel (seen: t5_text_datacore). The line is then a HUD message
      // (textNotification); the HUD's other elements are hidden while it is up.
      static constexpr const char *kHide[] = {"weapon swap", "weapon inventory", "textCommsRing", "barArmour",
                                              "barHealth", "barAction", "Radar", "AmmoBarL", "AmmoMetersL", "AmmoBarR",
                                              "AmmoMetersR", "SpeakerIcon", "LevelRegionLabel", "textObjectiveMessages",
                                              "imageObjectiveMessages", "XuiText1"};
      for (const char *id : kHide)
      {
        const uint32_t h = x.Find(scene, id);
        if (!h)
        {
          continue;
        }
        // Show off AND opacity 0: Rare's HUD update shows some of them again every frame (the fire-mode box stayed
        // in t6_text_datacore), but it doesn't touch their opacity.
        float opacity = 1.0f;
        GetFloat(x, h, "Opacity", opacity);
        const bool shown = x.SetBool(h, "Show", false);
        const bool faded = SetFloat(x, h, "Opacity", 0.0f);
        if (shown || faded)
        {
          g_rd.hidden.push_back(h);
          g_rd.hidden_opacity.push_back(opacity);
        }
      }
      if (const uint32_t h = x.Find(scene, "barMaxHealth"); h && x.SetBool(h, "Show", false))
      {
        float opacity = 1.0f;
        GetFloat(x, h, "Opacity", opacity);
        SetFloat(x, h, "Opacity", 0.0f);
        g_rd.hidden.push_back(h);
        g_rd.hidden_opacity.push_back(opacity);
      }
      g_rd.on = true;
      g_rd.no_panel = true;
      REXLOG_INFO("pdz_counterop: rare text: player 2's HUD scene {:08X} has no 'textMatchOver' panel: the line is a HUD "
                  "message (kind {:#x}); {} HUD elements hidden. His view is not darkened by us: black before his "
                  "first guard (Rare has made him no camera yet), where he died after a death",
                  scene, RareKind(), g_rd.hidden.size());
    }
    else
    {
    // Its children: one XuiFigure and one XuiText (Ids as the scene names them: logged, UNSURE).
    std::string kids;
    int n = 0;
    for (uint32_t c = x.Child(g_rd.group, true); c && n < 8; c = x.Child(c, false), ++n)
    {
      const std::string id = Ascii(x.GetText(c, "Id"));
      kids += fmt::format(" '{}' text '{}'", id, Ascii(x.GetText(c, "Text")));
      if (id.find("Figure") != std::string::npos && !g_rd.figure)
      {
        g_rd.figure = c;
      }
      else if (id.find("Text") != std::string::npos && !g_rd.text)
      {
        g_rd.text = c;
      }
    }
    float sw = 0, sh = 0, gx = 0, gy = 0;
    GetFloat(x, scene, "Width", sw);
    GetFloat(x, scene, "Height", sh);
    GetVec(x, g_rd.group, "Position", gx, gy);
    if (g_rd.figure)
    {
      GetFloat(x, g_rd.figure, "Width", g_rd.fig_w);
      GetFloat(x, g_rd.figure, "Height", g_rd.fig_h);
      GetVec(x, g_rd.figure, "Position", g_rd.fig_x, g_rd.fig_y);
      if (sw > 1 && sh > 1)
      {
        // Over the whole HUD scene (= his view): the figure's place is relative to its group.
        SetVec(x, g_rd.figure, "Position", -gx, -gy);
        SetFloat(x, g_rd.figure, "Width", sw);
        SetFloat(x, g_rd.figure, "Height", sh);
        SetFloat(x, g_rd.figure, "Opacity", 1.0f);
      }
    }
    if (g_rd.text)
    {
      g_rd.old_text = x.GetText(g_rd.text, "Text");
    }
    for (const char *id : kAfterPanel)
    {
      const uint32_t h = x.Find(scene, id);
      if (h && x.SetBool(h, "Show", false)) // UNSURE: Rare's HUD update may show some again every frame (then hide
      {                                     // them every tick, or accept them on the panel)
        g_rd.hidden.push_back(h);
      }
    }
    x.SetBool(g_rd.group, "Show", true);
    SetFloat(x, g_rd.group, "Opacity", 1.0f);
    g_rd.on = true;
    REXLOG_INFO("pdz_counterop: rare text: dark panel on: HUD {:08X} scene {:08X} ({:.0f} x {:.0f}), 'textMatchOver' "
                "{:08X} at {:.0f},{:.0f}, children:{}; figure {:08X} was {:.0f} x {:.0f} at {:.0f},{:.0f}; {} HUD "
                "elements hidden",
                hud, scene, sw, sh, g_rd.group, gx, gy, kids, g_rd.figure, g_rd.fig_w, g_rd.fig_h, g_rd.fig_x,
                g_rd.fig_y, g_rd.hidden.size());
    }
  }
  g_rd.view = view;
  const std::u16string line = Wide(ViewText(view));
  const int32_t kind = RareKind();
  if (g_rd.kind >= 0 && g_rd.kind != kind)
  {
    rex::CallFrame frame(ctx); // the kind changed (a test): the old kind's line goes
    frame.ctx.r3.u64 = hud;
    frame.ctx.r4.u64 = uint32_t(g_rd.kind);
    frame.ctx.r5.u64 = 1;
    sub_823B2B50(frame.ctx, base);
  }
  g_rd.kind = kind;
  if (kind >= 0 && kind <= 0x1A)
  {
    if (g_rd.text)
    {
      x.SetText(g_rd.text, "Text", std::u16string());
    }
    // A static string object (+8 = -1: Rare's 8262F330 then keeps our pointer and frees nothing), one per line, in
    // guest memory that lives for the whole run: the line is sent again every 2 s, so nothing may pile up.
    static uint32_t objects[5] = {0, 0, 0, 0, 0}; // (round 14: view 4)
    if (view >= 1 && view <= 4 && !objects[view])
    {
      const uint32_t bytes = 16 + uint32_t(line.size() + 1) * 2;
      const uint32_t at = REX_KERNEL_MEMORY()->SystemHeapAlloc(bytes);
      if (at)
      {
        std::memset(base + at, 0, bytes);
        for (size_t i = 0; i < line.size(); ++i)
        {
          base[at + 16 + i * 2] = uint8_t(line[i] >> 8);
          base[at + 16 + i * 2 + 1] = uint8_t(line[i]);
        }
        Store32(base, at, at + 16);
        Store32(base, at + 4, uint32_t(line.size()));
        Store32(base, at + 8, 0xFFFFFFFFu);
        objects[view] = at;
      }
    }
    const uint32_t object = view >= 1 && view <= 4 ? objects[view] : 0;
    if (!object)
    {
      g_rare_dark_drawn.store(false, std::memory_order_relaxed);
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = hud;
    frame.ctx.r4.u64 = uint32_t(kind);
    frame.ctx.r5.u64 = 1;
    if (g_rd.sent_view != view)
    {
      sub_823B2B50(frame.ctx, base); // another line: the old one of this kind goes at once
    }
    g_rd.sent_view = view;
    g_rd.sent_ms = NowMs();
    frame.ctx.r3.u64 = hud;
    frame.ctx.r4.u64 = uint32_t(kind);
    frame.ctx.r5.u64 = object;
    frame.ctx.r7.u64 = 0;
    frame.ctx.f1.f64 = -1.0; // stays until 823B2B50
    sub_823B2978(frame.ctx, base);
  }
  else if (g_rd.text)
  {
    x.SetText(g_rd.text, "Text", line);
  }
  g_rare_dark_drawn.store(true, std::memory_order_relaxed);
  return true;
}

// Round 11: while player 2 drives a spiderbot his HUD shows no weapon: the weapon row ('weapon inventory' with
// WeaponSelect1-4: the P9P icon and the Y switch row), the pick-up / drop box ('weapon swap') and the ammo bars. The
// same way the waiting line hides them: Show off AND opacity 0 (Rare's HUD update shows some again every frame but
// does not write their opacity; both are written again every tick here in case it does for the ammo bars).
namespace
{
  struct SpiderHud
  {
    bool on = false;
    uint32_t hud = 0, scene = 0;
    std::vector<uint32_t> hidden;
    std::vector<float> opacity;
  };
  SpiderHud g_sh;
} // namespace
void PdzCounterOpSpiderHud(PPCContext &ctx, uint8_t *base, bool driving, int32_t pad)
{
  const uint32_t hud = pad >= 0 && pad <= 3 ? Load32(base, kHudTable + uint32_t(pad) * 4) : 0;
  const uint32_t scene = Ptr(hud) ? Load32(base, hud) : 0;
  if (g_sh.on && (g_sh.hud != hud || g_sh.scene != scene))
  {
    g_sh = SpiderHud{}; // a new HUD (new level): the old handles are dead, nothing to put back
  }
  if (!driving)
  {
    if (g_sh.on)
    {
      PPCContext copy = ctx;
      Xui x(copy, base);
      for (size_t i = 0; i < g_sh.hidden.size(); ++i)
      {
        x.SetBool(g_sh.hidden[i], "Show", true);
        SetFloat(x, g_sh.hidden[i], "Opacity", g_sh.opacity[i]);
      }
      REXLOG_INFO("pdz_counterop: spider: HUD: {} weapon element(s) of player 2's HUD shown again", g_sh.hidden.size());
      g_sh = SpiderHud{};
    }
    return;
  }
  if (!scene)
  {
    return;
  }
  PPCContext copy = ctx;
  Xui x(copy, base);
  if (!g_sh.on)
  {
    static constexpr const char *kHide[] = {"weapon swap", "weapon inventory", "AmmoBarL", "AmmoMetersL", "AmmoBarR",
                                            "AmmoMetersR"};
    g_sh.hud = hud;
    g_sh.scene = scene;
    for (const char *id : kHide)
    {
      const uint32_t h = x.Find(scene, id);
      if (!h)
      {
        continue;
      }
      float opacity = 1.0f;
      GetFloat(x, h, "Opacity", opacity);
      g_sh.hidden.push_back(h);
      g_sh.opacity.push_back(opacity);
    }
    g_sh.on = true;
    REXLOG_INFO("pdz_counterop: spider: HUD: {} weapon element(s) of player 2's HUD hidden while he drives a bot or waits",
                g_sh.hidden.size());
  }
  for (const uint32_t h : g_sh.hidden)
  {
    x.SetBool(h, "Show", false);
    SetFloat(x, h, "Opacity", 0.0f);
  }
}

// Round 5 (item 2): who won, on player 2's own half, in Rare's text and Rare's HUD label.
// Rare's in-game "mission failed" is only the per-player context prompt SHOW DEBRIEFING (general 0x14F, HUD kind 0,
// with the Back button picture; 8222FC40 -> 825AAB48 -> 823B1BE0 -> 823B2978): there is no MISSION FAILED / COMPLETED
// label in the in-game HUD to take away from his half. His half keeps Rare's prompt (he can open the debriefing too)
// and gets one line more in his HUD's centred notification label (kind 0x10), made of Rare's own strings fetched
// through Rare's lookup 8239BCD8 (Rare's wording, in the player's language):
//   the agent is down  (level state bit 0x2): pdz_counterop_win_text_won  (frontend 148 "COUNTER-OP" + the general
//                                             table's 0x43 "MISSION COMPLETED")
//   the agent finished (level state bit 0x1): pdz_counterop_win_text_lost ("COUNTER-OP" + the general table's 0x42
//                                             "MISSION FAILED")
// Rare's multiplayer winner lines ("%s WINS!") live in the multiplayer text table, which is not loaded in a mission.
REXCVAR_DEFINE_BOOL(pdz_counterop_win_screens, true, "Gameplay",
                    "Counter-Operative (round 5, default on): when the mission ends, player 2's half says how it "
                    "ended for HIM in the game's own text (the agent down: the counter-op's mission is completed; the "
                    "agent finished: the counter-op's mission failed). The agent's half and the debriefing stay as "
                    "they are")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_win_first, true, "Gameplay",
                    "Counter-Operative (default on): when the mission fails for the agent, Rare's MISSION FAILED "
                    "notification on player 2's half is replaced by his own line in the same frame (he has won). Off "
                    "= his half shows Rare's MISSION FAILED first, his line up to 2 s later (round 5)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_win_text_won, "20094+43", "Gameplay",
                      "Counter-Operative: Rare's string ids (hex, (table << 16) | index, joined with +) player 2 sees "
                      "when the agent is down: default frontend 148 (COUNTER-OP) and the general table's 0x43 (MISSION "
                      "COMPLETED)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_win_text_lost, "20094+42", "Gameplay",
                      "Counter-Operative: Rare's string ids player 2 sees when the agent completes the mission: "
                      "default frontend 148 (COUNTER-OP) and the general table's 0x42 (MISSION FAILED)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_win_kind, 0x10, "Gameplay",
                     "Counter-Operative: the HUD message kind of that line (0x10 the centred notification)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_win_probe, false, "Testing",
                    "Counter-Operative (testing): at the first level tick log Rare's text of candidate string ids")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kWinLevelState = 0x82E426F8; // byte: 0x1 completed, 0x2 failed

  struct WinLine
  {
    int result = 0;    // 0 none, 1 player 2 won (agent down), 2 player 2 lost (agent finished)
    uint32_t hud = 0;  // the HUD the line was sent to
    int32_t kind = -1; // as which kind
    int64_t sent_ms = 0;
    bool probed = false, logged = false;
    bool state_seen = false; // the level state's end bit was up while the line was (round 10)
  };
  WinLine g_win;
  // Round 10 (item 2): the round before is over (a restart, a checkpoint reload, a quit, the next level, a new game):
  // our line is taken off the HUD it is on at the next tick, whatever the level state says.
  bool g_win_drop = false;
  int g_win_force = 0; // round 6 (item 4): Rare's own MISSION FAILED notification was just sent: 1 = he won
  uint32_t g_win_object = 0; // guest memory for the run: {+0 text pointer, +4 length, +8 -1 static}, text at +16

  std::string WinAscii(const std::u16string &text)
  {
    std::string out;
    for (char16_t c : text)
    {
      out += (c >= 0x20 && c < 0x7F) ? char(c) : (c == u'\r' || c == u'\n') ? '|' : '?';
    }
    return out;
  }

  std::u16string WinString(PPCContext &ctx, uint8_t *base, uint32_t id)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = id;
    sub_8239BCD8(frame.ctx, base);
    const uint32_t chars = frame.ctx.r3.u32;
    return Ptr(chars) ? ReadWide(base, chars) : std::u16string();
  }

  // "20094+43" -> Rare's strings joined with ": "; ids = how many were found.
  std::u16string WinText(PPCContext &ctx, uint8_t *base, const std::string &spec, int &found, int &asked)
  {
    std::u16string out;
    found = asked = 0;
    size_t at = 0;
    while (at <= spec.size())
    {
      const size_t end = spec.find('+', at);
      const std::string part = spec.substr(at, end == std::string::npos ? std::string::npos : end - at);
      at = end == std::string::npos ? spec.size() + 1 : end + 1;
      if (part.empty())
      {
        continue;
      }
      ++asked;
      const uint32_t id = uint32_t(std::strtoul(part.c_str(), nullptr, 16));
      const std::u16string text = WinString(ctx, base, id);
      if (text.empty())
      {
        continue;
      }
      ++found;
      if (!out.empty())
      {
        out += u": ";
      }
      out += text;
    }
    return out;
  }
} // namespace

// Every level tick, from counter_op.cpp's 8222F740 hook after Tick. on = a Counter-Op game runs; pad = player 2's
// pad or -1. Returns true while his line is up (the caller keeps the waiting panel off meanwhile).
bool PdzCounterOpWinScreens(PPCContext &ctx, uint8_t *base, bool on, int32_t pad)
{
  if (on && REXCVAR_GET(pdz_counterop_win_probe) && !g_win.probed)
  {
    g_win.probed = true;
    for (uint32_t id : {0x40040u, 0x40041u, 0x40042u, 0x40043u, 0x40044u, 0x40045u, 0x40046u, 0x14Fu, 0x20004u,
                        0x20005u, 0x20092u, 0x20093u, 0x20094u, 0x40272u, 0x40273u, 0x4027Eu, 0x4027Fu, 0x40281u})
    {
      const std::u16string text = WinString(ctx, base, id);
      REXLOG_INFO("pdz_counterop: win screens: probe: Rare's string {:X} = {}", id,
                  text.empty() ? std::string("(none)") : "'" + WinAscii(text) + "'");
    }
    // Every loaded table (slot << 16 | index): the strings with a win / lose / mission word.
    static constexpr const char *kWords[] = {"WIN", "WON", "LOSE", "LOST", "VICTOR", "DEFEAT", "MISSION", "FAIL",
                                             "COMPLETE", "SUCCESS", "KILLED", "DEAD", "DIED", "ELIMINAT", "OVER",
                                             "COUNTER", "ABORT", "WELL DONE", "CONGRAT", "KILL", "ASSASSIN", "INTRUD",
                                             "TARGET", "JOANNA", "DARK", "STOP", "HUNT", "RESPAWN", "WAIT", "SPECTAT",
                                             "CHOOS", "OBJECTIVE", "ENEMY", "ENEMIES", "GUARD", "DESTROY", "TERMINAT",
                                             "NEUTRALI", "AGENT", "SELECT", "READY"};
    int lines = 0;
    for (uint32_t slot = 0; slot < 10 && lines < 900; ++slot)
    {
      for (uint32_t index = 0; index < 3000 && lines < 900; ++index)
      {
        const std::u16string text = WinString(ctx, base, (slot << 16) | index);
        if (text.empty() || text.size() > 60)
        {
          continue;
        }
        std::string up = WinAscii(text);
        for (char &c : up)
        {
          c = char(std::toupper(static_cast<unsigned char>(c)));
        }
        bool hit = false;
        for (const char *w : kWords)
        {
          hit = hit || up.find(w) != std::string::npos;
        }
        if (hit)
        {
          ++lines;
          REXLOG_INFO("pdz_counterop: win screens: scan: Rare's string {:X} = '{}'", (slot << 16) | index,
                      WinAscii(text));
        }
      }
    }
  }
  const uint8_t state = base[kWinLevelState];
  if (g_win_drop)
  {
    // Round 10 (item 2). Hand test of round 9: after 'COUNTER-OP: MISSION COMPLETED' the level was restarted and the
    // line stayed: g_win_force (set by Rare's MISSION FAILED notification) was only cleared at a level boundary, a
    // restart is not one, and the line is sent to stay (-1 s) and again every 2 s.
    const uint32_t old_hud = (pad >= 0 && pad < 4) ? Load32(base, kHudTable + uint32_t(pad) * 4) : 0;
    const int32_t old_kind = g_win.kind >= 0 ? g_win.kind : std::clamp<int32_t>(REXCVAR_GET(pdz_counterop_win_kind), 0, 0x1A);
    const bool had = g_win.result != 0 || g_win_force != 0;
    if (Ptr(old_hud) && had)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = old_hud;
      frame.ctx.r4.u64 = uint32_t(old_kind);
      frame.ctx.r5.u64 = 1; // at once
      sub_823B2B50(frame.ctx, base);
    }
    if (had)
    {
      REXLOG_INFO("pdz_counterop: win screens: the round is over (restart, reload, quit or a new level): player 2's "
                  "win / lose line is cleared{} (it was {}; level state {:02X})",
                  Ptr(old_hud) ? fmt::format(" from his HUD {:08X} (kind {:X}, at once)", old_hud, old_kind)
                               : std::string(" (his HUD is gone with the level)"),
                  (g_win_force ? g_win_force : g_win.result) == 1 ? "'mission completed'" : "'mission failed'", state);
    }
    const bool probed = g_win.probed;
    g_win = WinLine{};
    g_win.probed = probed;
    g_win_force = 0;
    g_win_drop = false;
  }
  if (g_win_force && (state & 0x3))
  {
    g_win.state_seen = true;
  }
  else if (g_win_force && g_win.state_seen)
  {
    g_win_force = 0; // the level state went back to 'running' under a forced line: a restart from the checkpoint
  }
  int result = g_win.result; // the first of the two bits decides
  if (!on || !REXCVAR_GET(pdz_counterop_win_screens))
  {
    result = 0;
    g_win_force = 0;
  }
  else if (g_win_force)
  {
    result = g_win_force; // Rare's failure notification came before the level state bit (or without it)
  }
  else if (!(state & 0x3))
  {
    result = 0;
  }
  else if (!result)
  {
    result = (state & 0x1) ? 2 : 1;
  }
  const uint32_t hud = (pad >= 0 && pad < 4) ? Load32(base, kHudTable + uint32_t(pad) * 4) : 0;
  if (!result || !Ptr(hud))
  {
    if (g_win.result && g_win.kind >= 0 && on && Ptr(hud) && hud == g_win.hud)
    {
      rex::CallFrame frame(ctx); // the level state went back (a restart from the checkpoint): the line goes
      frame.ctx.r3.u64 = g_win.hud;
      frame.ctx.r4.u64 = uint32_t(g_win.kind);
      frame.ctx.r5.u64 = 0;
      sub_823B2B50(frame.ctx, base);
    }
    const bool probed = g_win.probed && on;
    const bool seen = g_win.state_seen && g_win_force != 0;
    g_win = WinLine{};
    g_win.probed = probed;
    g_win.state_seen = seen;
    return false;
  }
  const int32_t kind = std::clamp<int32_t>(REXCVAR_GET(pdz_counterop_win_kind), 0, 0x1A);
  // Sent again every 2 s: the kind keeps one entry, and a notification of Rare's that took the label is replaced.
  if (g_win.result == result && g_win.hud == hud && g_win.kind == kind && NowMs() - g_win.sent_ms < 2000)
  {
    return true;
  }
  const std::string spec = result == 1 ? REXCVAR_GET(pdz_counterop_win_text_won) : REXCVAR_GET(pdz_counterop_win_text_lost);
  int found = 0, asked = 0;
  std::u16string text = WinText(ctx, base, spec, found, asked);
  if (text.size() > 120)
  {
    text.resize(120);
  }
  if (!g_win_object)
  {
    g_win_object = REX_KERNEL_MEMORY()->SystemHeapAlloc(16 + 128 * 2);
  }
  if (text.empty() || !g_win_object)
  {
    if (!g_win.logged)
    {
      g_win.logged = true;
      REXLOG_WARN("pdz_counterop: win screens: none of Rare's strings '{}' is loaded in this level: player 2's half "
                  "keeps Rare's prompt only",
                  spec);
    }
    g_win.result = result;
    return false;
  }
  std::memset(base + g_win_object, 0, 16 + 128 * 2);
  for (size_t i = 0; i < text.size(); ++i)
  {
    base[g_win_object + 16 + i * 2] = uint8_t(text[i] >> 8);
    base[g_win_object + 16 + i * 2 + 1] = uint8_t(text[i]);
  }
  Store32(base, g_win_object, g_win_object + 16);
  Store32(base, g_win_object + 4, uint32_t(text.size()));
  Store32(base, g_win_object + 8, 0xFFFFFFFFu);
  rex::CallFrame frame(ctx);
  if (g_win.kind >= 0 && g_win.kind != kind && Ptr(g_win.hud))
  {
    frame.ctx.r3.u64 = g_win.hud;
    frame.ctx.r4.u64 = uint32_t(g_win.kind);
    frame.ctx.r5.u64 = 1;
    sub_823B2B50(frame.ctx, base);
  }
  frame.ctx.r3.u64 = hud;
  frame.ctx.r4.u64 = uint32_t(kind);
  frame.ctx.r5.u64 = g_win_object;
  frame.ctx.r7.u64 = 0;
  frame.ctx.f1.f64 = -1.0; // stays until the HUD goes with the level
  sub_823B2978(frame.ctx, base);
  if (!g_win.logged)
  {
    g_win.logged = true;
    REXLOG_INFO("pdz_counterop: win screens: level state {:02X}: player 2 {}: his HUD {:08X} (pad {}) kind {:X} shows "
                "Rare's strings {} ({} of {} found): '{}'; the agent's half is Rare's own",
                state, result == 1 ? "WON (the agent is down)" : "LOST (the agent completed the mission)", hud, pad,
                kind, spec, found, asked, WinAscii(text));
  }
  g_win.result = result;
  g_win.hud = hud;
  g_win.kind = kind;
  g_win.sent_ms = NowMs();
  return true;
}

// Round 6 (item 4). Rare's "MISSION FAILED" is a HUD notification (kind 0x10, general string 0x42) that 8264EEF8 sends
// to EVERY human participant when the mission fails (level scripts call it; the co-op game over too), so player 2's
// half showed it before his own line took the label on its next 2 s refresh. counter_op.cpp's hook of 8264EEF8 calls
// this right after Rare's: his line replaces Rare's in the same label in the same frame (a kind with one entry: the
// newer message takes it), so his half never shows MISSION FAILED when he has won.
void PdzCounterOpWinNow(PPCContext &ctx, uint8_t *base, int32_t pad, int result)
{
  if (!REXCVAR_GET(pdz_counterop_win_screens) || !REXCVAR_GET(pdz_counterop_win_first))
  {
    return;
  }
  g_win_force = result;
  g_win.sent_ms = 0; // send now
  g_win.result = result;
  const bool sent = PdzCounterOpWinScreens(ctx, base, true, pad);
  REXLOG_INFO("pdz_counterop: win screens: Rare's MISSION FAILED notification (8264EEF8) went to every player: player "
              "2's label {} at once (pad {})",
              sent ? "replaced by his own line" : "could NOT be replaced (no HUD or no string)", pad);
}

// Round 6 (item 1): a level boundary (counter_op.cpp PdzCounterOpLevelBoundary). The HUD handles are those of a level
// that is gone: nothing is restored through them, they are only forgotten.
// Round 10 (item 2): the round is over without a level boundary (counter_op.cpp's Reset: a restart, a checkpoint
// reload, a player re-created) or with one. The line is taken off at the next tick (the HUD call needs a context).
void PdzCounterOpWinRoundOver()
{
  if (g_win.result != 0 || g_win_force != 0)
  {
    g_win_drop = true;
  }
}

void PdzCounterOpEnvReset(); // round 12 (item 5), below: the remembered sky / fog states are those of a level that is gone

void PdzCounterOpViewReset()
{
  PdzCounterOpEnvReset();
  g_rd = RareDark{};
  const bool probed = g_win.probed;
  g_win = WinLine{};
  g_win.probed = probed;
  g_win_force = 0;
  g_view.store(0, std::memory_order_relaxed);
  g_rare_dark_drawn.store(false, std::memory_order_relaxed);
  g_sh = SpiderHud{}; // Sun 22 Nov night (audit): the hidden HUD elements' handles are those of a level that is gone
}

// ==== Round 12 (item 5): sky, fog, rain / snow and lighting of player 2's view ======================================
//
// What Rare does (research notes: Sun15Nov_round12\d\FINDINGS.md has the addresses and the evidence):
// every player's view object (human +0x19A4, set at 825A9BB0) carries its own environment: the fog settings in use
// (+0x4D0 on, +0x4D4 the settings, +0x4D8 / +0x4DC the next ones and the trigger being crossed), rain / snow (+0x4E0
// on, +0x4E1 rain, +0x4E4 the trigger), the lighting set (+0x4EC the id, +0x4F0 / +0x4F8 the next one and its
// trigger, +0x4FC..+0x515 the two vectors in use) and the sky dome (+0x518). 8220E9F0 copies them into the view's
// render record every frame. Nothing in Rare's code works them out from a position: a view starts with the level's
// defaults (825A99D8 for a new human, 82277720 at the level start: 8220E3A8(view, 0, 0, default fog), 8220F6F8(view,
// default lighting), the level's rain / snow bytes) and is changed from then on only by the level's own trigger
// messages as that player walks through the boundary volumes (82517A50: PropMsg_Fog_ChangeSettings 0x40,
// PropMsg_SkyDome_Change 0x45, PropMsg_Rain / Snow 0x4C..0x4F, PropMsg_Lighting_ChangeSettings 0x58; each for the
// activator's own view only), and by cutscene camera tracks.
//
// Why player 2 kept Datacore's underwater look on the rig: (1) counter_op_triggers.cpp drops every presence trigger
// with him as the activator, the boundary volumes' PassRToG / ContactFace kinds among them, so his view never left
// the level's default; (2) a takeover moves him with Rare's move (825AD148), which crosses no boundary.
//
// What this does (pdz_counterop_env_refresh, default on; off = round 11):
//  a. PdzCounterOpEnvTrigger: a trigger of his that counter_op_triggers.cpp drops is looked through the way 825178B0
//     does, and its sky / fog / weather / lighting messages (only those) are sent on through Rare's 82517A50. They
//     change his own view and nothing else, so walking from one area into the next now looks as it does for Jo.
//  b. The agent's view is the one that is known to be right for where she is. Her settled state is remembered with
//     her position as she plays (a place every 3 m or at every change). PdzCounterOpEnvRefresh gives player 2's view
//     the state remembered nearest to where he is, through Rare's own immediate calls (8220E3A8(view, 0, 0,
//     settings), 8220F6F8(view, lighting id), the inline stores of 825A9C7C for the weather and 82517E64 for the
//     sky dome): at every spawn / takeover (counter_op.cpp's SpawnAt, after the move) and, from the tick, while his
//     hidden body is carried (a driven spiderbot, the parking with the agent).
//
// Round 16 (item 11, Fri 20 Nov; research notes: RESEARCH_env_Fri20Nov.md):
//  - Why (a) never acted: g_env.out is only written by the tick, which runs after the spawn, so every trigger of a
//    spawn (Rare's volume sweep 825D68B8 runs in the unhide and in the frame after the move) still saw him as out
//    and was dropped silently; afterwards Rare's per-player volume state says "inside", so the same volume never
//    fires again until he leaves it. Now SpawnAt's refresh marks him in the world, and the tick's second
//    "back in the world" refresh no longer puts the remembered state back over what his own triggers just did.
//  - Trinity Infiltration's underwater look is not a zone message at all: its data has only the two "second sky"
//    volumes at the start (PropMsg_SkyDome_Change 1). Rare's own rule is in the water camera's update 82217838
//    (camera mode 1, vtable 82095FF8): on levels 0 / 7 / 8 (game +0x19: Datacore, Trinity Infiltration, Trinity
//    Escape), not in game type 2, the view's sky dome = eye above y 0 ? 1 : 0 (level cs1: skydome 0 =
//    'skydomecs1underwater', 1 = 'skydomecs1') and its lighting set = above ? 2 : 1 (8220F6F8). The agent keeps the
//    state that rule (and the volumes) last gave her view; player 2 spawned under water never got it.
//    pdz_counterop_env_water gives his view that rule's result at a spawn whose remembered agent state is from the
//    other side of the water line, and whenever his eye crosses the line in the world (unless one of his own zone
//    triggers has just set his view).
#include <cmath>

REXCVAR_DEFINE_BOOL(pdz_counterop_env_refresh, true, "Gameplay",
                    "Counter-Operative (default on): player 2's view gets the sky, fog, rain / snow and lighting of "
                    "the place he is put in at every spawn, takeover and spiderbot hand-over (the state the agent's "
                    "view had nearest to that place), and the level's own sky / fog boundaries work for him when he "
                    "walks through them. Off = round 11 (his view keeps the state it had)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_env_triggers, true, "Gameplay",
                    "Counter-Operative (default on, needs pdz_counterop_env_refresh): of the level triggers that are "
                    "dropped for player 2, the sky / fog / rain / snow / lighting messages are still passed to Rare "
                    "(they change his own view only)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_env_water_datacore, false, "Gameplay",
                    "Counter-Operative (round 16, default off): pdz_counterop_env_water's above / under water rule on "
                    "Datacore too (off: Datacore keeps round 12's boundary volumes and the agent's remembered state; "
                    "the two Trinity levels use the rule)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_env_water, true, "Gameplay",
                    "Counter-Operative (default on, needs pdz_counterop_env_refresh): on Datacore and both Trinity "
                    "levels player 2's sky dome and lighting follow Rare's own water-line rule (the water camera's: "
                    "eye above y 0 = sky dome 1, lighting set 2; below = sky dome 0, lighting set 1) at a spawn whose "
                    "remembered agent state is from the other side of the water line, and when his eye crosses the "
                    "line. Off = the agent's remembered state only (round 12)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_8220E3A8); // a view's fog: (view, side, trigger or 0 = at once, settings or 0 = no fog)
REX_EXTERN(sub_8220F6F8); // a view's lighting set, at once: (view, id)
REX_EXTERN(sub_82517A50); // one trigger message of a prop: (prop, &message, activator)
bool PdzCounterOpIsPlayer2(uint32_t character); // counter_op.cpp: started, pdz_counterop on, this is player 2

namespace
{
  constexpr uint32_t kEnvGame = 0x82D6BFD8;       // +0xC -> the level's record: +0x74 fog table (0x1C each), +0x78 default
  constexpr uint32_t kEnvHumanVtable = 0x820C7DE0;
  constexpr uint32_t kEnvViewOf = 0x19A4;         // a human's view object
  constexpr uint32_t kEnvCentre = 48;             // a character's position (sphere centre)
  constexpr uint32_t kEnvFogOn = 0x4D0, kEnvFog = 0x4D4, kEnvFogNext = 0x4D8, kEnvFogTrigger = 0x4DC;
  constexpr uint32_t kEnvWeatherOn = 0x4E0, kEnvWeatherRain = 0x4E1, kEnvWeatherTrigger = 0x4E4;
  constexpr uint32_t kEnvWeatherSideA = 0x4E8, kEnvWeatherSideB = 0x4E9;
  constexpr uint32_t kEnvLight = 0x4EC, kEnvLightTrigger = 0x4F8, kEnvLightBlock = 0x4FC;
  constexpr uint32_t kEnvLightBlockSize = 0x1A;   // +0x4FC..+0x515: two vectors, a flag, the 'set' flag
  constexpr uint32_t kEnvSky = 0x518;
  constexpr float kEnvCrumbStep = 300.0f;         // a new remembered place every 3 m
  constexpr size_t kEnvCrumbMax = 6000;

  struct EnvState
  {
    bool fog_on = false;
    uint32_t fog = 0;   // the fog settings in use (a pointer into the level's table)
    uint32_t sky = 0;   // the sky dome id
    uint8_t weather = 0, rain = 0;
    uint32_t light = 0; // the lighting set id
    uint8_t light_block[kEnvLightBlockSize] = {};
  };
  struct EnvCrumb
  {
    float at[3] = {};
    EnvState state;
    int64_t ms = 0;
  };
  struct EnvMemory
  {
    std::vector<EnvCrumb> crumbs;
    bool out = false, was_out = false;
    bool cutscene = false; // a cutscene plays (or just ended): the agent's character is an actor somewhere else
    uint32_t p2c = 0;
    float last_at[3] = {};
    bool last_at_ok = false;
    int64_t last_tick_ms = 0, last_carried_ms = 0;
    uint32_t refreshes = 0, passed = 0;
    // Round 16 (item 11).
    bool spawn_refreshed = false; // SpawnAt refreshed his view since the last tick (the tick does not do it again)
    int water_side = -1;          // his eye's side of the water line at the last look (1 above, 0 below, -1 unknown)
    int64_t last_trigger_ms = 0;  // the last time one of his own zone triggers went on to Rare
    uint32_t water = 0, held_out = 0;
  };
  EnvMemory g_env;

  bool EnvSame(const EnvState &a, const EnvState &b)
  {
    return a.fog_on == b.fog_on && (!a.fog_on || a.fog == b.fog) && a.sky == b.sky && a.weather == b.weather &&
           (!a.weather || a.rain == b.rain) && a.light == b.light &&
           std::memcmp(a.light_block, b.light_block, kEnvLightBlockSize) == 0;
  }

  // A human's view object, or 0.
  uint32_t EnvViewOf(const uint8_t *base, uint32_t character)
  {
    if (!Ptr(character) || Load32(base, character) != kEnvHumanVtable)
    {
      return 0;
    }
    const uint32_t view = Load32(base, character + kEnvViewOf);
    return Ptr(view) ? view : 0;
  }

  EnvState EnvRead(const uint8_t *base, uint32_t view)
  {
    EnvState s;
    s.fog_on = base[view + kEnvFogOn] != 0;
    s.fog = Load32(base, view + kEnvFog);
    s.sky = Load32(base, view + kEnvSky);
    s.weather = base[view + kEnvWeatherOn];
    s.rain = base[view + kEnvWeatherRain];
    s.light = Load32(base, view + kEnvLight);
    std::memcpy(s.light_block, base + view + kEnvLightBlock, kEnvLightBlockSize);
    return s;
  }

  // No boundary is being crossed: nothing is blending towards a next state.
  bool EnvSettled(const uint8_t *base, uint32_t view)
  {
    return Load32(base, view + kEnvFogTrigger) == 0 && Load32(base, view + kEnvWeatherTrigger) == 0 &&
           Load32(base, view + kEnvLightTrigger) == 0;
  }

  // The fog settings' id in the level's table (1-based, as the trigger messages give it); 0 = not one of the table's.
  uint32_t EnvFogId(const uint8_t *base, uint32_t fog)
  {
    const uint32_t level = Load32(base, kEnvGame + 0xC);
    const uint32_t table = pdz::guestmem::Ok(base, level, 0x74 + 4) ? Load32(base, level + 0x74) : 0;
    if (!Ptr(table) || !Ptr(fog) || fog < table || (fog - table) % 0x1Cu != 0 || (fog - table) / 0x1Cu >= 255u)
    {
      return 0;
    }
    return (fog - table) / 0x1Cu + 1u;
  }

  std::string EnvText(const uint8_t *base, const EnvState &s)
  {
    const std::string fog = !s.fog_on ? std::string("off") : fmt::format("{} ({:08X})", EnvFogId(base, s.fog), s.fog);
    const char *weather = !s.weather ? "no rain or snow" : s.rain ? "rain" : "snow";
    return fmt::format("fog {}, sky dome {}, {}, lighting set {}", fog, s.sky, weather, s.light);
  }

  bool EnvCentre(const uint8_t *base, uint32_t character, float out[3])
  {
    for (int i = 0; i < 3; ++i)
    {
      out[i] = LoadFloat(base, character + kEnvCentre + 4 * uint32_t(i));
    }
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
  }

  float EnvDist2(const float a[3], const float b[3])
  {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
  }

  // The agent's settled state is remembered with her place: a new entry every 3 m, at every change, or (forced: a
  // refresh is about to use it) when she is more than half a metre from the last one. Never in a cutscene: Rare
  // moves her character about as an actor then (Datacore's opening shows her on the rig while her view still has
  // the level's first state), and its camera tracks write the view's sky themselves.
  void EnvNote(const uint8_t *base, uint32_t p1c, int64_t now, bool force)
  {
    const uint32_t view = EnvViewOf(base, p1c);
    float at[3] = {};
    if (g_env.cutscene || !view || !EnvSettled(base, view) || !EnvCentre(base, p1c, at))
    {
      return;
    }
    const EnvState s = EnvRead(base, view);
    if (s.fog_on && !EnvFogId(base, s.fog))
    {
      return; // not one of this level's fog settings: nothing to hand on
    }
    if (!g_env.crumbs.empty())
    {
      const EnvCrumb &last = g_env.crumbs.back();
      const float step = force ? 50.0f : kEnvCrumbStep;
      if (EnvSame(last.state, s) && EnvDist2(last.at, at) < step * step)
      {
        return;
      }
    }
    if (g_env.crumbs.size() >= kEnvCrumbMax)
    {
      g_env.crumbs.erase(g_env.crumbs.begin(), g_env.crumbs.begin() + int(kEnvCrumbMax / 4));
    }
    EnvCrumb c;
    std::memcpy(c.at, at, sizeof(c.at));
    c.state = s;
    c.ms = now;
    g_env.crumbs.push_back(c);
  }

  // The index of the remembered place nearest to 'at' (the newer one of two that are within 3 m of being equally
  // near), or -1.
  int EnvNearest(const float at[3], float &dist)
  {
    int best = -1;
    float best_d = 0.0f;
    for (size_t i = g_env.crumbs.size(); i-- > 0;)
    {
      const float d = std::sqrt(EnvDist2(g_env.crumbs[i].at, at));
      if (best < 0 || d < best_d - 300.0f)
      {
        best = int(i);
        best_d = d;
      }
    }
    dist = best_d;
    return best;
  }

  // Rare's own immediate calls and stores (see the head of this section).
  void EnvApply(PPCContext &ctx, uint8_t *base, uint32_t view, const EnvState &s)
  {
    if (!s.fog_on || EnvFogId(base, s.fog))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = view;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = 0; // no trigger: at once, nothing blended
      frame.ctx.r6.u64 = s.fog_on ? s.fog : 0;
      sub_8220E3A8(frame.ctx, base);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = view;
      frame.ctx.r4.u64 = s.light;
      sub_8220F6F8(frame.ctx, base);
    }
    std::memcpy(base + view + kEnvLightBlock, s.light_block, kEnvLightBlockSize);
    base[view + kEnvWeatherOn] = s.weather; // 825A9C7C..825A9C8C
    Store32(base, view + kEnvWeatherTrigger, 0);
    base[view + kEnvWeatherSideA] = 0;
    base[view + kEnvWeatherSideB] = 0;
    base[view + kEnvWeatherRain] = s.rain;
    Store32(base, view + kEnvSky, s.sky); // 82517E64
  }

  // Round 16 (item 11): Rare's water-line rule (82217838 at 8221809C..8221811C, the water camera's update).
  // Its level test: the game object [82D6BFD8] +0x19 (the level) is 0, 7 or 8 and +0x28 (the game type) is not 2.
  bool EnvWaterLevel(const uint8_t *base)
  {
    const uint32_t game = Load32(base, kEnvGame);
    if (!Ptr(game))
    {
      return false;
    }
    const int level = int(int8_t(base[game + 0x19]));
    // Datacore (level 0) only with pdz_counterop_env_water_datacore: log 027 shows the agent's own view under water
    // there with lighting set 0 and 1 by place (its boundary volumes), where the camera rule would always give 1.
    return ((level == 0 && REXCVAR_GET(pdz_counterop_env_water_datacore)) || level == 7 || level == 8) &&
           Load32(base, game + 0x28) != 2;
  }

  // His eye height: the top camera of his view's camera stack (view +0x444 points past the top entry, the stack
  // starts at view +0x14; the camera's position +4, the field Rare's rule reads), if it is within 3 m of his
  // character; else his character's centre.
  float EnvEyeY(const uint8_t *base, uint32_t view, uint32_t character)
  {
    const float centre = LoadFloat(base, character + kEnvCentre + 4);
    const uint32_t top = Load32(base, view + 0x444);
    if (Ptr(top) && top != view + 0x14)
    {
      const uint32_t camera = Load32(base, top - 4);
      if (Ptr(camera))
      {
        const float y = LoadFloat(base, camera + 4 + 4);
        if (std::isfinite(y) && std::fabs(y - centre) < 300.0f)
        {
          return y;
        }
      }
    }
    return centre;
  }

  // The rule's result for one side through Rare's own store and call (sky dome +0x518 as 82217838 stores it;
  // 8220F6F8(view, lighting set)). True = something changed.
  bool EnvWaterSet(PPCContext &ctx, uint8_t *base, uint32_t view, int side, float eye_y, const char *why)
  {
    const uint32_t sky = side ? 1u : 0u, light = side ? 2u : 1u;
    const uint32_t old_sky = Load32(base, view + kEnvSky), old_light = Load32(base, view + kEnvLight);
    if (old_sky == sky && old_light == light)
    {
      return false;
    }
    Store32(base, view + kEnvSky, sky);
    if (old_light != light)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = view;
      frame.ctx.r4.u64 = light;
      sub_8220F6F8(frame.ctx, base);
    }
    const uint32_t count = ++g_env.water;
    if (count <= 40 || count % 50 == 0)
    {
      REXLOG_INFO("pdz_counterop: env: water line #{} ({}): player 2's eye at y {:.0f}, {} the water line: sky dome "
                  "{} -> {}, lighting set {} -> {} (Rare's water-camera rule 82217838, pdz_counterop_env_water)",
                  count, why, eye_y, side ? "above" : "below", old_sky, sky, old_light, light);
    }
    return true;
  }

  // A refresh with him in the world (a spawn / takeover, back in the world): the remembered agent state stays when
  // it is from his side of the water line ('at' = that place, nullptr = none); else the rule decides.
  void EnvWaterRefresh(PPCContext &ctx, uint8_t *base, uint32_t view, uint32_t p2c, const float *at, const char *why)
  {
    if (!REXCVAR_GET(pdz_counterop_env_water) || !EnvWaterLevel(base))
    {
      g_env.water_side = -1;
      return;
    }
    const float y = EnvEyeY(base, view, p2c);
    const int side = y > 0.0f ? 1 : 0;
    g_env.water_side = side;
    if (at && (at[1] > 0.0f) == (side == 1))
    {
      return;
    }
    const std::string reason =
        fmt::format("{}: {}", why,
                    at ? "the agent's remembered state is from the other side of the water line"
                       : "no remembered agent state");
    EnvWaterSet(ctx, base, view, side, y, reason.c_str());
  }

  // One refresh. 'always' = an event (a spawn): one line whatever the outcome; otherwise a line only on a change.
  // Round 16 (item 11): 'in_world' = he is in the world at his own place (not a carried hidden body): the water-line
  // rule may follow the refresh.
  void EnvRefresh(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, const char *why, bool always,
                  bool in_world)
  {
    const uint32_t view = EnvViewOf(base, p2c);
    float at[3] = {};
    if (!view || !EnvCentre(base, p2c, at))
    {
      if (always)
      {
        REXLOG_INFO("pdz_counterop: env: refresh ({}): not done: player 2 has no view object or no usable position",
                    why);
      }
      return;
    }
    const int64_t now = NowMs();
    EnvNote(base, p1c, now, true);
    float dist = 0.0f;
    const int index = EnvNearest(at, dist);
    if (index < 0)
    {
      if (always)
      {
        REXLOG_INFO("pdz_counterop: env: refresh ({}): not done: no settled state of the agent's view is known yet",
                    why);
      }
      if (in_world)
      {
        EnvWaterRefresh(ctx, base, view, p2c, nullptr, why);
      }
      return;
    }
    const EnvCrumb crumb = g_env.crumbs[size_t(index)];
    const EnvState old = EnvRead(base, view);
    const bool settled = EnvSettled(base, view);
    const bool changed = !EnvSame(old, crumb.state) || !settled;
    if (changed)
    {
      EnvApply(ctx, base, view, crumb.state);
    }
    std::memcpy(g_env.last_at, at, sizeof(g_env.last_at));
    g_env.last_at_ok = true;
    if (always || changed)
    {
      ++g_env.refreshes;
      REXLOG_INFO("pdz_counterop: env: refresh #{} ({}): player 2 at ({:.0f}, {:.0f}, {:.0f}): {}{} -> {}{}; the state "
                  "of the agent's view {:.1f} m from there, {:.0f} s ago ({} places remembered); set through Rare's "
                  "8220E3A8 / 8220F6F8 and the view's weather and sky dome fields",
                  g_env.refreshes, why, at[0], at[1], at[2], EnvText(base, old),
                  settled ? "" : " (a boundary was half crossed)", EnvText(base, crumb.state),
                  changed ? "" : " (unchanged)", double(dist) / 100.0, double(now - crumb.ms) / 1000.0,
                  g_env.crumbs.size());
    }
    if (in_world)
    {
      EnvWaterRefresh(ctx, base, view, p2c, crumb.at, why);
    }
  }

  bool EnvMessage(uint8_t id)
  {
    return (id >= 0x40 && id <= 0x42) || (id >= 0x45 && id <= 0x49) || (id >= 0x4C && id <= 0x4F) ||
           (id >= 0x58 && id <= 0x5A);
  }
} // namespace

// PdzCounterOpViewReset (a level boundary): the remembered states point into a level that is gone.
void PdzCounterOpEnvReset() { g_env = EnvMemory{}; }

// counter_op.cpp, SpawnAt (after his move and facing): every spawn and takeover. One line per call.
void PdzCounterOpEnvRefresh(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, const char *why)
{
  if (!REXCVAR_GET(pdz_counterop_env_refresh))
  {
    return;
  }
  // Round 16 (item 11): from here on he is in the world at his new place; the tick (which writes g_env.out) runs only
  // after this, and until then every trigger of his new place was dropped as one of an out-of-the-world body. The
  // triggers fired inside the unhide, before the move (his hidden body's place), have been dropped already: right.
  if (REXCVAR_GET(pdz_counterop_env_triggers))
  {
    g_env.out = false;
    g_env.spawn_refreshed = true;
  }
  EnvRefresh(ctx, base, p1c, p2c, why, true, true);
}

// counter_op.cpp, Tick (a started game, every level tick). Five times a second: the agent's state is remembered;
// while player 2 is out of the world his view follows where his hidden body is (carried by a driven spiderbot, parked
// with the agent); a line only when the state changes.
void PdzCounterOpEnvTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, bool out, bool cutscene)
{
  if (!REXCVAR_GET(pdz_counterop_env_refresh))
  {
    return;
  }
  g_env.out = out;
  g_env.cutscene = cutscene;
  if (cutscene)
  {
    return; // nothing is remembered or refreshed while Rare's cutscene owns the views
  }
  const int64_t now = NowMs();
  if (now - g_env.last_tick_ms < 200)
  {
    return;
  }
  g_env.last_tick_ms = now;
  EnvNote(base, p1c, now, false);
  const bool new_body = p2c != g_env.p2c;
  const bool came_back = !out && g_env.was_out;
  g_env.p2c = p2c;
  g_env.was_out = out;
  // Round 16 (item 11): SpawnAt refreshed his view since the last tick (set only with pdz_counterop_env_triggers).
  const bool spawned = g_env.spawn_refreshed;
  if (!out)
  {
    g_env.spawn_refreshed = false;
  }
  if (new_body)
  {
    g_env.last_at_ok = false;
  }
  if (out)
  {
    g_env.water_side = -1;
    float at[3] = {};
    if (EnvCentre(base, p2c, at) && (!g_env.last_at_ok || EnvDist2(at, g_env.last_at) > 300.0f * 300.0f ||
                                     now - g_env.last_carried_ms > 2000))
    {
      g_env.last_carried_ms = now;
      EnvRefresh(ctx, base, p1c, p2c, "his hidden body is somewhere else now: a spiderbot's camera, or parked", false,
                 false);
    }
  }
  else if (came_back || new_body)
  {
    // Round 16 (item 11): after SpawnAt's refresh the triggers of his new place have reached his view (they run
    // after it); a second remembered-state refresh here would put the agent's state back over them. Round 12 did it
    // again here (only a line on a change); now only when SpawnAt did not.
    if (!spawned)
    {
      EnvRefresh(ctx, base, p1c, p2c, "back in the world", false, true);
    }
  }
  else if (REXCVAR_GET(pdz_counterop_env_water))
  {
    // Round 16 (item 11): his eye crosses the water line in the world: Rare's water-camera rule, unless one of his
    // own zone triggers has just set his view (Datacore's boundary volumes at the water's edge).
    const uint32_t view = EnvViewOf(base, p2c);
    if (view && EnvWaterLevel(base))
    {
      const float y = EnvEyeY(base, view, p2c);
      if (std::isfinite(y))
      {
        const int side = y > 0.0f ? 1 : 0;
        if (g_env.water_side >= 0 && side != g_env.water_side)
        {
          if (now - g_env.last_trigger_ms < 3000)
          {
            if (++g_env.water <= 40)
            {
              REXLOG_INFO("pdz_counterop: env: water line: player 2's eye crossed it (y {:.0f}, now {}), his view "
                          "left as his own zone trigger set it {:.1f} s ago",
                          y, side ? "above" : "below", double(now - g_env.last_trigger_ms) / 1000.0);
            }
          }
          else
          {
            EnvWaterSet(ctx, base, view, side, y, "his eye crossed the water line");
          }
        }
        g_env.water_side = side;
      }
    }
  }
  else
  {
    g_env.water_side = -1;
  }
}

// counter_op_triggers.cpp, the 825178B0 hook, where a trigger with player 2 as the activator is dropped: its sky /
// fog / weather / lighting messages are sent on (the same walk over the prop's messages as 825178B0: the setup's two,
// the registered list, the template's four).
void PdzCounterOpEnvTrigger(PPCContext &ctx, uint8_t *base, uint32_t prop, uint32_t activator, uint32_t kind)
{
  if (!REXCVAR_GET(pdz_counterop_env_refresh) || !REXCVAR_GET(pdz_counterop_env_triggers) || !Ptr(prop) ||
      !PdzCounterOpIsPlayer2(activator) || !EnvViewOf(base, activator))
  {
    return;
  }
  uint32_t found[16] = {};
  uint32_t n = 0;
  const auto take = [&](uint32_t message)
  {
    if (Ptr(message) && uint32_t(base[message]) == kind && EnvMessage(base[message + 1]) && n < 16)
    {
      found[n++] = message;
    }
  };
  if (const uint32_t setup = Load32(base, prop + 0x54); Ptr(setup))
  {
    take(setup + 0x1E);
    take(setup + 0x22);
  }
  if ((Load32(base, prop + 0x68) >> 4) & 1u)
  {
    uint32_t message = 0, node = Load32(base, prop + 0x70);
    for (int i = 0; i < 64 && Ptr(node); ++i)
    {
      if (Load32(base, node) == 4)
      {
        message = Load32(base, node + 4);
        break;
      }
      node = Load32(base, node + 8);
    }
    for (int i = 0; i < 64 && Ptr(message); ++i)
    {
      take(message);
      message = Load32(base, message + 4);
    }
  }
  if (const uint32_t entry = Load32(base, prop + 0x64); Ptr(entry))
  {
    if (const uint32_t tmpl = Load32(base, entry + 4); Ptr(tmpl))
    {
      take(tmpl + 0xCC);
      take(tmpl + 0xD0);
      take(tmpl + 0xD4);
      take(tmpl + 0xD8);
    }
  }
  if (n == 0)
  {
    return;
  }
  // (The out test was the first one above until round 16, silent: the walk is done first now so the drop is seen.)
  if (g_env.out)
  {
    const uint32_t held = ++g_env.held_out;
    if (held <= 10 || held % 100 == 0)
    {
      REXLOG_INFO("pdz_counterop: env: trigger kind {} on prop {:08X} with sky / fog / weather / lighting message(s) "
                  "not passed (#{}): player 2 is out of the world (his hidden body's place)",
                  kind, prop, held);
    }
    return;
  }
  g_env.last_trigger_ms = NowMs();
  const uint32_t view = EnvViewOf(base, activator);
  const EnvState before = EnvRead(base, view);
  std::string ids;
  for (uint32_t i = 0; i < n; ++i)
  {
    ids += fmt::format("{}{:02X}/{}", i ? ", " : "", uint32_t(base[found[i] + 1]), uint32_t(base[found[i] + 2]));
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = prop;
    frame.ctx.r4.u64 = found[i];
    frame.ctx.r5.u64 = activator;
    sub_82517A50(frame.ctx, base);
  }
  const uint32_t count = ++g_env.passed;
  if (count <= 40 || count % 50 == 0)
  {
    REXLOG_INFO("pdz_counterop: env: boundary #{}: player 2 fired trigger kind {} on prop {:08X}: its sky / fog / "
                "weather / lighting message(s) (id/value: {}) went on to Rare (82517A50), for his view only; his view "
                "was: {}; now: {}, next fog {:08X} through trigger {:08X}",
                count, kind, prop, ids, EnvText(base, before), EnvText(base, EnvRead(base, view)),
                Load32(base, view + kEnvFogNext), Load32(base, view + kEnvFogTrigger));
  }
}
