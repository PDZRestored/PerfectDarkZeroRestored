// Transparency vs water (0.9.2, Rare's bug; pdz_fix_transparency_water, on by default, F1 toggles it).
//
// Rare's transparent stage sub_826216D0 (traced in Nightclub's harbour, overnight 092's follow-up) runs, in order:
//   1. sub_82622510 (returns to 82621ABC): a bucket of meshes, the water's first layer (PS ccf49f44) among them;
//   2. sub_82603AC0 (returns to 82621AE0): the additive glows (82608510; the street-lamp halos, PS d5ce1e70);
//   3. sub_826F21D0: a resolve of the scene colour (with the glows in it) to 1D700000;
//   4. sub_82622510 (returns to 82621B68): the same water meshes again, the second layer (PS a2917857, the
//      refraction fog), which composites that copy over the water area.
// The glows write no depth, so step 4 paints over every glow in front of the water: looking at the sea through a
// street-lamp halo, the water covers the halo (play testing, 7 Nov). On a real Xbox 360 too (the emulator path draws the
// same calls in the same order).
// The fix: the glow call of step 2 is held back (its eight arguments, all from sub_826216D0's callee-saved
// registers, are kept) and made right after step 4 returns, before the next resolve. The glows are then drawn over
// the finished water, depth-tested against the same scene depth; nothing else moves. Should step 4 not run in a
// frame, the held call is made when sub_826216D0 returns, so no glow is ever lost.
//
// Diagnostic stage: pdz_transparency_trace_frame = N logs, for the one game frame whose counter (82D6BFD8 + 136,
// the Swap count) reaches N, every entry of Rare's mesh drawer sub_82710CC0 (the water layers, the alpha meshes)
// and of the glow / particle pass sub_826216D0 (-> 82603AC0 -> 82608510, the additive street-light glows), each
// with its guest call chain, so the frame's own order can be read.
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

#include <fmt/format.h>

#include "../game_cvars.h"

REX_EXTERN(__imp__sub_82710CC0);
REX_EXTERN(__imp__sub_826216D0);
REX_EXTERN(__imp__sub_82603AC0);
REX_EXTERN(__imp__sub_82622510);
REX_EXTERN(__imp__sub_82628888); // the per-view object visibility pass (skyline objects, below)

namespace
{
  constexpr uint32_t kFrameCounter = 0x82D6BFD8 + 136; // the game's frame counter (one per Swap; test_mp_setup.cpp)

  bool IsGuestPointer(uint32_t a) { return a >= 0x00010000u && a < 0xFFFF0000u; }

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    return _byteswap_ulong(*reinterpret_cast<const uint32_t *>(base + a));
  }

  uint32_t Frame(const uint8_t *base)
  {
    return Load32(base, kFrameCounter);
  }

  // Return addresses on the guest stack (back chain at +0, saved LR at -8 of the caller's frame).
  std::string Callers(const PPCContext &ctx, const uint8_t *base, int depth = 10)
  {
    std::string chain = fmt::format("{:08X}", uint32_t(ctx.lr));
    uint32_t sp = ctx.r1.u32;
    for (int i = 0; i < depth && IsGuestPointer(sp); ++i)
    {
      const uint32_t next = Load32(base, sp);
      if (!IsGuestPointer(next) || next <= sp || next - sp > 0x8000 || (next & 0xFFFF) == 0)
      {
        break;
      }
      chain += fmt::format(" < {:08X}", Load32(base, next - 8));
      sp = next;
    }
    return chain;
  }

  int g_seq = 0;
  uint32_t g_frame = 0;

  constexpr uint32_t kGlowCallReturn = 0x82621AE0;    // sub_826216D0's call of the glow pass (step 2)
  constexpr uint32_t kWaterLayer2Return = 0x82621B68; // its bucket call that draws the water's second layer (step 4)

  uint32_t Frame(const uint8_t *base); // below

  // pdz_transparency_test_alternate (testing): the fix on odd game frames only, so two consecutive captures with a
  // still camera show the fix's own change.
  bool FixOn(const uint8_t *base)
  {
    if (!REXCVAR_GET(pdz_game_fixes) || !REXCVAR_GET(pdz_fix_transparency_water))
    {
      return false;
    }
    return !REXCVAR_GET(pdz_transparency_test_alternate) || (Frame(base) & 1u);
  }

  struct HeldGlows
  {
    bool pending = false;
    uint64_t r[8] = {}; // r3..r10
  } g_held;
  uint64_t g_moved = 0, g_flushed = 0;

  void RunHeldGlows(PPCContext &ctx, uint8_t *base, const char *where)
  {
    if (!g_held.pending)
    {
      return;
    }
    g_held.pending = false;
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x800) & ~0xFu; // a frame of its own below the caller's
    uint64_t *regs[8] = {&frame.ctx.r3.u64, &frame.ctx.r4.u64, &frame.ctx.r5.u64, &frame.ctx.r6.u64,
                         &frame.ctx.r7.u64, &frame.ctx.r8.u64, &frame.ctx.r9.u64, &frame.ctx.r10.u64};
    for (int i = 0; i < 8; ++i)
    {
      *regs[i] = g_held.r[i];
    }
    __imp__sub_82603AC0(frame.ctx, base);
    ++g_moved;
    if ((g_moved & (g_moved - 1)) == 0) // 1, 2, 4, 8, ...
    {
      REXLOG_INFO("pdz_fix_transparency_water: the glows drawn after the water's second layer ({}; {} frames so far, "
                  "{} by the end-of-stage flush)",
                  where, g_moved, g_flushed);
    }
  }

  uint32_t g_target = 0;       // one-shot frames from pdz::transwater::TraceFrame (pdz_trace_on_mark), 0: none
  uint32_t g_target_count = 1; // ... this many game frames from g_target
  uint32_t g_last_logged = 0;

  bool Tracing(const uint8_t *base)
  {
    const int32_t want = REXCVAR_GET(pdz_transparency_trace_frame);
    const uint32_t target = g_target ? g_target : want > 0 ? uint32_t(want) : 0;
    if (!target)
    {
      return false;
    }
    const uint32_t count = g_target ? g_target_count : 1u;
    const uint32_t f = Frame(base);
    if (g_frame == 0 && f >= target)
    {
      g_frame = f;
      g_seq = 0;
      g_last_logged = f;
      REXLOG_INFO("pdz_transwater: trace of frame {} begins ({} frame(s))", f, count);
    }
    if (g_target && g_frame && f >= g_frame + count)
    {
      REXLOG_INFO("pdz_transwater: trace of frame {} ends", g_frame + count - 1);
      g_target = 0; // one-shot done; the cvar's own frame stays traced once
      g_frame = 0;
      return false;
    }
    const bool on = g_frame != 0 && f >= g_frame && f < g_frame + count;
    if (on && f != g_last_logged)
    {
      g_last_logged = f;
      REXLOG_INFO("pdz_transwater: trace of frame {} begins (frame {} of {})", f, f - g_frame + 1, count);
    }
    return on;
  }
} // namespace

// Water reflection visibility (Mon 16 Nov; pdz_fix_reflection_visibility, on by default). Rare's reflection pass
// sub_82621BE8 draws the scene again with a mirrored camera, but runs no visibility query of its own: the mesh drawer
// below skips every world part whose bit is clear in the MAIN camera's per-frame bit array (pointer at 82E22568, set
// by sub_82714FF8). A tall building that has left the screen therefore vanishes from the water's reflection. While
// the pass's "in reflection" byte (82E3C69F) is set, the drawer reads a private all-ones array instead: every world
// part counts as visible in the reflection. Only the pointer is swapped (and put back); Rare's array is not written.
REXCVAR_DEFINE_BOOL(pdz_fix_reflection_visibility, true, "Render",
                    "Water reflections: the reflection pass draws every world part, not only the parts the main "
                    "camera sees, so tall buildings stay in the reflection when they leave the screen. Off: as on "
                    "the Xbox 360")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Skyline visibility (Mon 16 Nov, release candidate; pdz_fix_skyline_visibility, OFF by default since the hand test of
// that evening: with it active the Nightclub skyline was still flat head-on, and the effect was then found to be the
// level's fog, which is never changed. The code stays for a later look; nothing here runs while the switch is off). On the Nightclub
// harbour a block of far buildings across the water shows only as the flat grey backdrop from some spots and as the
// detailed lit towers from others. Read in the code: the world is split into rooms; the mesh drawer below skips every
// room whose bit is clear in the frame's bit array (the same array the reflection fix above replaces), and the object
// pass sub_82628888 hides the objects of every room whose bit is clear in the view's visible-room bitfield. Nothing
// else was found that swaps far geometry (no distance or angle test in these paths). INFERRED, not seen in a run: the
// towers' room is missing from the visible set at those spots.
// The fix, on the levels of pdz_fix_skyline_levels only ("nightclub"): in the main scene passes (renderer modes 0, 1,
// 7 and 8 at 82E22554; not the shadow passes) the drawer reads the all-ones array, so every room's static geometry is
// submitted, and the per-room screen rectangles are not applied for that call (82E2254C = 0: a room that Rare did not
// mark visible has no rectangle of its own and would be cut by the last one set). Hidden rooms end up behind the
// walls in the depth buffer. Cost: all of the level's static geometry is submitted in each main pass, every frame,
// instead of the visible rooms only. Rare's arrays are not written; the pointer and the flag are put back.
// pdz_fix_skyline_objects (off; to try by hand if the towers turn out to be objects): the object pass runs with every
// room marked visible (the view's bitfield is filled for the call and restored), so each object is only tested
// against the view frustum.
REXCVAR_DEFINE_BOOL(pdz_fix_skyline_visibility, false, "Render",
                    "Far skyline: on the levels of pdz_fix_skyline_levels the main picture draws every part of the "
                    "level, not only the rooms the game marks visible, so far buildings no longer drop out and leave "
                    "the flat backdrop. Costs draw time on those levels. Off by default (hand test, Mon 16 Nov: it did not "
                    "change the Nightclub skyline, which is a fog effect and is left as the game draws it). Off: as on "
                    "the Xbox 360")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_fix_skyline_levels, "nightclub", "Render",
                      "The single player levels pdz_fix_skyline_visibility acts on (names as pdz_sp_60_levels takes "
                      "them, separated by commas). Empty: none")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_fix_skyline_objects, false, "Render",
                    "With pdz_fix_skyline_visibility, on the same levels: objects in rooms the game marks hidden are "
                    "drawn too when they are inside the view (props and other objects, not only the level's own "
                    "geometry). Costs more. Off by default: untested")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::pace
{
  std::string SpLevelName(const uint8_t *base, std::string *raw); // render/game_pace.cpp
}

namespace
{
  constexpr uint32_t kInReflection = 0x82E3C69F;  // byte, raised by sub_82621BE8 around its scene draw
  constexpr uint32_t kVisibleBits = 0x82E22568;   // pointer to the frame's world-part visibility bits
  constexpr uint32_t kUseRects = 0x82E2254C;      // word: the drawer applies the per-room screen rectangles
  constexpr uint32_t kRenderMode = 0x82E22554;    // word: the scene pass's mode (2 shadow casters, 3 reflection)
  constexpr uint32_t kAllVisibleSize = 0x10000;   // 64 KB of ones: part ids up to 524287
  uint32_t g_all_visible = 0;

  std::atomic<bool> g_skyline_level{false};
  std::atomic<uint32_t> g_skyline_checked{~0u}; // the game frame of the last level check
  std::atomic<bool> g_skyline_logged{false};

  // Is the level being played on pdz_fix_skyline_levels? Asked again every 64 game frames (both threads call this).
  bool SkylineLevel(const uint8_t *base)
  {
    const uint32_t frame = Frame(base);
    const uint32_t last = g_skyline_checked.load(std::memory_order_relaxed);
    if (last == ~0u || frame - last >= 64u)
    {
      g_skyline_checked.store(frame, std::memory_order_relaxed);
      bool on = false;
      const std::string list = REXCVAR_GET(pdz_fix_skyline_levels);
      const std::string level = list.empty() ? std::string() : pdz::pace::SpLevelName(base, nullptr);
      for (size_t at = 0; !level.empty() && at <= list.size();)
      {
        size_t end = list.find_first_of(", ;", at);
        if (end == std::string::npos)
        {
          end = list.size();
        }
        if (end > at && list.compare(at, end - at, level) == 0)
        {
          on = true;
          break;
        }
        at = end + 1;
      }
      if (g_skyline_level.exchange(on, std::memory_order_relaxed) != on)
      {
        REXLOG_INFO("pdz_fix_skyline_visibility: this level is {} pdz_fix_skyline_levels ('{}')", on ? "on" : "not on",
                    level);
      }
    }
    return g_skyline_level.load(std::memory_order_relaxed);
  }

  // What a call of the mesh drawer has to put back: the bits pointer (0: nothing swapped) and the rectangle flag.
  struct PartsSwap
  {
    uint32_t bits = 0;
    bool rects = false;
    uint32_t rects_value = 0; // as stored (guest byte order)
  };

  // All world parts visible for this call of the drawer? The reflection pass (pdz_fix_reflection_visibility) and, on
  // the skyline levels, the main scene passes (pdz_fix_skyline_visibility).
  PartsSwap SeeAllParts(uint8_t *base)
  {
    PartsSwap swap;
    if (!REXCVAR_GET(pdz_game_fixes))
    {
      return swap;
    }
    const bool reflection = base[kInReflection] != 0;
    bool main_pass = false;
    if (reflection)
    {
      if (!REXCVAR_GET(pdz_fix_reflection_visibility))
      {
        return swap;
      }
    }
    else
    {
      if (!REXCVAR_GET(pdz_fix_skyline_visibility))
      {
        return swap;
      }
      const uint32_t mode = Load32(base, kRenderMode);
      if ((mode != 0 && mode != 1 && mode != 7 && mode != 8) || !SkylineLevel(base))
      {
        return swap;
      }
      main_pass = true;
    }
    if (!g_all_visible)
    {
      g_all_visible = REX_KERNEL_MEMORY()->SystemHeapAlloc(kAllVisibleSize);
      if (!g_all_visible)
      {
        return swap;
      }
      std::memset(base + g_all_visible, 0xFF, kAllVisibleSize);
      REXLOG_INFO("pdz_fix_reflection_visibility: {} now draws every world part (all-visible bits at {:08X})",
                  reflection ? "the water reflection pass" : "a main scene pass (pdz_fix_skyline_visibility)",
                  g_all_visible);
    }
    const uint32_t old = Load32(base, kVisibleBits);
    if (old == g_all_visible || !IsGuestPointer(old))
    {
      return swap;
    }
    *reinterpret_cast<uint32_t *>(base + kVisibleBits) = _byteswap_ulong(g_all_visible);
    swap.bits = old;
    if (main_pass)
    {
      uint32_t *rects = reinterpret_cast<uint32_t *>(base + kUseRects);
      if (*rects != 0)
      {
        swap.rects = true;
        swap.rects_value = *rects;
        *rects = 0;
      }
      if (!g_skyline_logged.exchange(true, std::memory_order_relaxed))
      {
        REXLOG_INFO("pdz_fix_skyline_visibility: the main scene passes on this level now draw every world part "
                    "(renderer mode {}, the per-room rectangles {})",
                    Load32(base, kRenderMode), swap.rects ? "held off for these calls" : "were not in use");
      }
    }
    return swap;
  }
} // namespace

// The per-view object visibility pass (game thread; r3 = the view). pdz_fix_skyline_objects only: the view's
// visible-room bitfield is filled with ones for the call and restored after it, so the pass takes every room as
// visible and tests each object against the view frustum alone (its own loop, sub_82627DE8). The addresses are the
// pass's own: level rooms = [[[82D6BFD8 + 12] + 16] + 4] (count at + 68, the room pointers at [+ 64], a room's bit
// number at room + 76); the view's record = [[82D6BFD8 + 12] + 4 (buffer + 7)] + [rooms + 32] + view x [rooms + 36];
// the bitfield = [[rooms + 104] + record + 44].
REX_HOOK_RAW(sub_82628888)
{
  constexpr uint32_t kGame = 0x82D6BFD8, kBuffer = 0x82FBE3C0;
  constexpr uint32_t kMaxBytes = 4096;
  uint8_t saved[kMaxBytes];
  uint32_t bits = 0, bytes = 0;
  if (REXCVAR_GET(pdz_fix_skyline_objects) && REXCVAR_GET(pdz_fix_skyline_visibility) && REXCVAR_GET(pdz_game_fixes) &&
      SkylineLevel(base))
  {
    const uint32_t tables = Load32(base, kGame + 12);
    const uint32_t level = IsGuestPointer(tables) ? Load32(base, tables + 16) : 0;
    const uint32_t rooms = IsGuestPointer(level) ? Load32(base, level + 4) : 0;
    const uint32_t view_object = IsGuestPointer(ctx.r3.u32) ? Load32(base, ctx.r3.u32 + 20) : 0;
    const uint32_t buffer = Load32(base, kBuffer);
    if (IsGuestPointer(rooms) && IsGuestPointer(view_object) && buffer <= 1)
    {
      const uint32_t count = (uint32_t(base[rooms + 68]) << 8) | base[rooms + 69];
      const uint32_t list = Load32(base, rooms + 64);
      const uint32_t frame_record = Load32(base, tables + 4 * (buffer + 7));
      const uint32_t view = Load32(base, view_object + 64);
      if (count && count <= 8192 && IsGuestPointer(list) && IsGuestPointer(frame_record) && view <= 3)
      {
        const uint32_t record = frame_record + Load32(base, rooms + 32) + view * Load32(base, rooms + 36);
        const uint32_t holder = Load32(base, rooms + 104) + record;
        const uint32_t field = IsGuestPointer(holder) ? Load32(base, holder + 44) : 0;
        uint32_t highest = 0;
        bool sane = IsGuestPointer(field);
        for (uint32_t i = 0; sane && i < count; ++i)
        {
          const uint32_t room = Load32(base, list + 4 * i);
          if (!IsGuestPointer(room))
          {
            sane = false;
            break;
          }
          const uint32_t n = Load32(base, room + 76);
          if (n >= kMaxBytes * 8)
          {
            sane = false;
            break;
          }
          highest = n > highest ? n : highest;
        }
        if (sane)
        {
          bits = field;
          bytes = (highest >> 3) + 1;
          std::memcpy(saved, base + bits, bytes);
          std::memset(base + bits, 0xFF, bytes);
          static bool logged = false;
          if (!logged)
          {
            logged = true;
            REXLOG_INFO("pdz_fix_skyline_objects: the object pass takes all {} rooms of this level as visible "
                        "({} bytes of the view's room bits at {:08X} filled for the call)",
                        count, bytes, bits);
          }
        }
      }
    }
  }
  __imp__sub_82628888(ctx, base);
  if (bits)
  {
    std::memcpy(base + bits, saved, bytes);
  }
}

REX_HOOK_RAW(sub_82710CC0)
{
  const PartsSwap parts_swap = SeeAllParts(base);
  struct RestoreParts
  {
    uint8_t *base;
    PartsSwap swap;
    ~RestoreParts()
    {
      if (swap.bits)
      {
        *reinterpret_cast<uint32_t *>(base + kVisibleBits) = _byteswap_ulong(swap.bits);
      }
      if (swap.rects)
      {
        *reinterpret_cast<uint32_t *>(base + kUseRects) = swap.rects_value;
      }
    }
  } restore_parts{base, parts_swap};
  static bool first = true;
  if (first && REXCVAR_GET(pdz_transparency_trace_frame) > 0)
  {
    first = false;
    REXLOG_INFO("pdz_transwater: sub_82710CC0 hooked; frame counter now {}", Frame(base));
  }
  const bool t = Tracing(base);
  const int seq = t ? ++g_seq : 0;
  if (t)
  {
    // The native renderer draws inline with the record thread off (PDZ_PLAY_TRACE.bat), so the frame trace's draw
    // lines between this line and its "ends" line are this call's draws.
    REXLOG_INFO("pdz_transwater: #{} mesh sub_82710CC0 r3 {:08X} r4 {:08X} r5 {:08X} r6 {:08X} | {}", seq, ctx.r3.u32,
                ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, Callers(ctx, base));
  }
  __imp__sub_82710CC0(ctx, base);
  if (t)
  {
    REXLOG_INFO("pdz_transwater: #{} mesh ends", seq);
  }
}

REX_HOOK_RAW(sub_826216D0)
{
  static bool first = true;
  if (first && REXCVAR_GET(pdz_transparency_trace_frame) > 0)
  {
    first = false;
    REXLOG_INFO("pdz_transwater: sub_826216D0 hooked; frame counter now {}", Frame(base));
  }
  const bool t = Tracing(base);
  if (t)
  {
    REXLOG_INFO("pdz_transwater: #{} GLOW PASS begins sub_826216D0 r3 {:08X} r4 {:08X} | {}", ++g_seq, ctx.r3.u32,
                ctx.r4.u32, Callers(ctx, base));
  }
  __imp__sub_826216D0(ctx, base);
  if (g_held.pending)
  {
    ++g_flushed;
    RunHeldGlows(ctx, base, "the stage's end: no second water layer this frame");
  }
  if (t)
  {
    REXLOG_INFO("pdz_transwater: #{} GLOW PASS ends", ++g_seq);
  }
}

// Step 4's bucket: the held glows right after it.
REX_HOOK_RAW(sub_82622510)
{
  const uint32_t lr = uint32_t(ctx.lr);
  const bool t = Tracing(base);
  if (t)
  {
    REXLOG_INFO("pdz_transwater: #{} BUCKET sub_82622510 from {:08X} begins", ++g_seq, lr);
  }
  __imp__sub_82622510(ctx, base);
  if (t)
  {
    REXLOG_INFO("pdz_transwater: #{} BUCKET from {:08X} ends", ++g_seq, lr);
  }
  if (lr == kWaterLayer2Return && g_held.pending)
  {
    RunHeldGlows(ctx, base, "after the second water layer");
  }
}

REX_HOOK_RAW(sub_82603AC0)
{
  if (uint32_t(ctx.lr) == kGlowCallReturn && FixOn(base) && !g_held.pending)
  {
    const uint64_t *regs[8] = {&ctx.r3.u64, &ctx.r4.u64, &ctx.r5.u64, &ctx.r6.u64,
                               &ctx.r7.u64, &ctx.r8.u64, &ctx.r9.u64, &ctx.r10.u64};
    for (int i = 0; i < 8; ++i)
    {
      g_held.r[i] = *regs[i];
    }
    g_held.pending = true;
    if (Tracing(base))
    {
      REXLOG_INFO("pdz_transwater: #{} GLOWS held for after the second water layer", ++g_seq);
    }
    return;
  }
  const bool t = Tracing(base);
  if (t)
  {
    REXLOG_INFO("pdz_transwater: #{} GLOWS begin sub_82603AC0 r3 {:08X} r4 {:08X} r5 {:08X} | {}", ++g_seq, ctx.r3.u32,
                ctx.r4.u32, ctx.r5.u32, Callers(ctx, base));
  }
  __imp__sub_82603AC0(ctx, base);
  if (t)
  {
    REXLOG_INFO("pdz_transwater: #{} GLOWS end", ++g_seq);
  }
}

// Water loop (0.9.2; pdz_fix_water_loop, on by default, Insert toggles it; the fix itself is in the native renderer).
// Rare's water animations are stacks of frames (Nightclub's sea: 16 ripple normal maps, DXN 128x128x16; the
// caustics: DXT1 256x256x32) played as loops: the layer coordinate (the sea: VS c35.w) moves with the clock and Rare
// wraps it by 1.0 every few seconds (the sea: 0.25 a second, every 4 s). The fetch constants say repeat on the layer
// axis, but the renderer clamped a stacked texture's layer to the first / last layer (as the SDK does), so at each
// wrap the animation froze for a moment and then jumped back: the snap seen on the sea, the pool, the caustics
// and running water (draw dump of the harbour, 7 Nov: the layer coordinate is the only constant that jumps). The fix
// (native_draw.cpp WaterLoopMask, native_hlslfix.h pdzTfetch3DS): those slots' layers wrap (the last blends into the
// first), so w and w - 1 read the same. Rare's data and the emulator are untouched. Earlier tries: Rare's wave phase
// scale 0.98 -> 1.0 (no visible change, dropped); a +64 mark on the sea's texture matrix only (replaced by the
// per-fetch rule, which covers every such stack).

namespace pdz::transwater
{
  // pdz_trace_on_mark (native_present.cpp): trace these game frames once.
  void TraceFrame(uint32_t frame, uint32_t count)
  {
    g_target_count = count ? count : 1u;
    g_target = frame;
    g_frame = 0;
  }

  uint32_t CurrentFrame(const uint8_t *base) { return Frame(base); }
} // namespace pdz::transwater

// F1 (bind_transparency_water_ab) and its on-screen label. The TU3 build doesn't compile live_toggles.cpp (its A/B
// keys aren't ported), so the key and the label live here; OnCreateDialogs calls Init (perfectdarkzerorecomp_app.h).
namespace
{
  using LabelClock = std::chrono::steady_clock;
  std::mutex g_label_mutex;
  std::string g_label;
  LabelClock::time_point g_label_until;

  class LabelDialog final : public rex::ui::ImGuiDialog
  {
  public:
    explicit LabelDialog(rex::ui::ImGuiDrawer *drawer) : ImGuiDialog(drawer) {}

  protected:
    void OnDraw(ImGuiIO &io) override
    {
      std::string text;
      {
        std::lock_guard<std::mutex> lock(g_label_mutex);
        if (LabelClock::now() >= g_label_until)
        {
          return;
        }
        text = g_label;
      }
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.20f), ImGuiCond_Always,
                              ImVec2(0.5f, 0.0f));
      ImGui::SetNextWindowBgAlpha(0.75f);
      constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;
      if (ImGui::Begin("##pdz_transwater_label", nullptr, kFlags))
      {
        ImGui::TextUnformatted(text.c_str());
      }
      ImGui::End();
    }
  };
  std::unique_ptr<LabelDialog> g_dialog;
} // namespace

namespace pdz::transwater
{
  void ShowStatus(const std::string &text, int seconds)
  {
    REXLOG_INFO("pdz_transwater: label: {}", text);
    std::lock_guard<std::mutex> lock(g_label_mutex);
    g_label = text;
    g_label_until = LabelClock::now() + std::chrono::seconds(seconds);
  }

  void Init(rex::ui::ImGuiDrawer *drawer)
  {
    g_dialog = std::make_unique<LabelDialog>(drawer);
    rex::ui::RegisterBind("bind_transparency_water_ab", "F1",
                          "Switch the transparency vs water fix (pdz_fix_transparency_water) on/off (A/B)", []
                          {
      static auto last = LabelClock::time_point{};
      const auto now = LabelClock::now();
      if (now - last < std::chrono::milliseconds(300))
      {
        return; // a held key repeats
      }
      last = now;
      const bool on = !REXCVAR_GET(pdz_fix_transparency_water);
      REXCVAR_SET(pdz_fix_transparency_water, on);
      ShowStatus(on ? std::string("Transparency vs water fix ON (glows in front of the water)")
                    : std::string("Transparency vs water fix OFF (Rare's original order)"),
                 3); });
    rex::ui::RegisterBind("bind_water_loop_ab", "Insert",
                          "Switch the water animation loop fix (pdz_fix_water_loop) on/off (A/B)", []
                          {
      static auto last = LabelClock::time_point{};
      const auto now = LabelClock::now();
      if (now - last < std::chrono::milliseconds(300))
      {
        return; // a held key repeats
      }
      last = now;
      const bool on = !REXCVAR_GET(pdz_fix_water_loop);
      REXCVAR_SET(pdz_fix_water_loop, on);
      ShowStatus(on ? std::string("Water loop fix ON (water animations loop smoothly)")
                    : std::string("Water loop fix OFF (water animations jump back every few seconds)"),
                 3); });
  }
} // namespace pdz::transwater
