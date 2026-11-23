// pdz_far_lighting_beams_no_fade (Mon 16 Nov 2026): the far streetlight beams keep their full size.
//
// What fades them: not fog and not a distance in the shader, but a RANK rule in Rare's glow pass sub_82603AC0. It
// walks the view's light list (nearest / strongest first) and counts the beams (glow cones) it has drawn (r23):
//   beams  0-7   size factors 1, 1                                  detail 1
//   beams  8-15  second factor 1 - (n - 8) / 8   (1 down to 0.125)  detail 1
//   beams 16-23  second factor 1 - (n - 16) / 8                     detail 2
//   beams 24-31  both factors  1 - (n - 24) / 8                     detail 4
//   beam  32     the loop ends: no more beams in this view
// (constants 8.0 / 16.0 / 24.0 / 0.125 at 0x8208C118 / 0x8208B2B0 / 0x820EAEA0 / 0x820EB414; the count test is
// 0x82603F90 cmpwi r23,32). The factors go to sub_82608510 as f3 / f4 and scale the cone's extents; the detail is the
// caller's stack word [r1 + 292]. The count only goes up when sub_82608510 returns non-zero in r3's low byte. So on a
// street with many lamps the 9th beam onwards shrinks and the 33rd is never drawn, while the lamps' pools of light
// (the light draws, another loop) stay.
//
// With the switch on, the hook of sub_82608510 (distant_lights.cpp) calls Before / After here: every beam is drawn
// with Rare's first-eight values (factors 1, detail 1) and reports "not counted", so Rare's counter stays at 0 and
// neither the shrink nor the cap of 32 is reached. Nothing in guest memory is written except the caller's own
// argument word. Off = Rare's rule exactly. Cost: every listed beam light in view is drawn at full size and detail
// (Nightclub has 141 beam lights), so it is off by default.
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(pdz_far_lighting_beams_no_fade, false, "Graphics",
                    "Light beams (street lamps, spot lights with a glow cone) keep their full size however many are "
                    "in view. Rare draws the nearest 8 at full size, shrinks the next 24 step by step and draws no "
                    "more than 32 per view, so far beams along a street fade out while their pools of light stay. "
                    "Costs some performance where many beams are in view. Off = Rare's rule exactly")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::beam_fade
{
  // Entry of sub_82608510 (r1 is still the caller's frame): Rare's values for the nearest beams. Returns whether
  // After has to run.
  bool Before(PPCContext &ctx, uint8_t *base)
  {
    if (!REXCVAR_GET(pdz_far_lighting_beams_no_fade))
    {
      return false;
    }
    ctx.f3.f64 = 1.0;
    ctx.f4.f64 = 1.0;
    const uint32_t one_be = 0x01000000u; // big-endian 1: the detail word Rare passes for beams 0-15
    std::memcpy(base + ctx.r1.u32 + 292, &one_be, 4);
    return true;
  }

  // After sub_82608510: "not counted", so the caller's beam counter (r23) stays where it is.
  void After(PPCContext &ctx) { ctx.r3.u64 = 0; }
} // namespace pdz::beam_fade
