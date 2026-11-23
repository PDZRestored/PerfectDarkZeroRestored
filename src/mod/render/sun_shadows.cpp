// Polish 6: longer sun shadows (pdz_shadow_distance_scale; BUG_AUDIT VR4-VR7). Enhancement, off by default (1).
//
// The shadow setup (TU3 sub_8260C0D8 = launch sub_825FC758 + 0xF980, per light and view; the light's vtable entry at
// 82108618) loads three constants and stores them into the light (float index = view + n, view = [82E1BA20]):
//   8260C3A4  lfs f13,-20096(r10)  1600.0 (820EB180)  -> 8260C3CC stfsx f13 = light[424 + view]: the fade start
//   8260C3C4  lfs f27,-32416(r9)   300.0  (82088160)  -> 8260C408 stfsx f27 = light[430 + view]: the near cascade's
//                                                         end (the split)
//   8260C3D4  lfs f26,30612(r8)    2000.0 (820A7794)  -> 8260C420 stfsx f26 = light[432 + view]: the far cascade's end
//                                                         = the shadow cut-off = the fade end
// (light[434 + view] = 0.0017 (820EB184) is stored next to them and not touched.) In the cascade loop each cascade's
// box is the view frustum from the eye to its end (8260C46C reads light[430 / 432 + view] back), fitted by the
// ortho fit sub_8260D518 with the fixed light depth range 0..20000 (820DFCA0; the eye 10000 back): VR6 puts the limit
// of that range at about k = 4. Every receiver reads the light back (sub_82607470 at 82607AD8, sub_82607E08 at
// 826080F4, sub_8260D3C0 at 8260D4C0) and builds its pixel-shader fade constant from it: c.x = 1 / (end - start),
// c.y = start * c.x + 1 (the mask shader then does saturate(z * c.x + c.y)), c.w = the split. So scaling the two
// values in the setup moves the cut-off, the fade and the far box together; nothing else needs a hook.
//
// The constants themselves are shared (2000.0 has 33 readers, some in AI code; 1600.0 at 820EB180 is used elsewhere
// too), so they are never written: two mid-function hooks (midasm_hooks_tu3.toml) scale the registers after the
// loads, before the stores:
//   PdzShadowFadeStart before 8260C3CC (the store of f13): f13 *= k
//   PdzShadowFarEnd    before 8260C3D8 (right after the load of f26): f26 *= k (stored for cascade 1 at 8260C420)
// The near split (f27, 300) is left alone: the near cascade's box, so its texel density, stays as Rare made it. The
// far cascade's box grows k times, so its texels do too (VR6: about 2 cm per texel at 20 m, 2x / 3x coarser at
// k = 2 / 3); the native renderer's pdz_shadow_map_scale 2 renders the shadow maps at twice the size to make up for
// it (native_cvars.cpp). With k = 1 the hooks return at once and touch no register: the game runs as before.
//
// Read on the game thread, once per light and view per frame (two calls a frame in a single-view level): one cvar
// read each, no syscalls; one log line when the value in effect changes.
//
// Polish 6b: the far cascade's bias and soft edges (pdz_shadow_bias_scale, pdz_shadow_far_pcf; PDZRenderResearch
// native\stage2\polish\shadowbias.md).
// The bias is not a render state: the shadow pass (sub_8260C8C0) sets no DepthBias / SlopeScaleDepthBias, the casters
// write plain depth. It is in the receiver, the sun shadow mask shader 34d98195 that sub_82607470 binds (SetPixelShader
// at 826079A8): per pixel it rebuilds the view position from the scene depth, picks the cascade (far = beyond the split
// c8.w: matrix c4-c6, maps t4 / t2; near: c0-c2, t6 / t1), point-samples the map and compares in the cascade's light
// depth d / 20000 (OrthoOffCenter zn 0, zf 20000 = 820DFCA0, in the ortho fit sub_8260D518):
//   z' = z + NdotL * c13.x + c13.y        c13 = {0.0013333, -0.0015334} (a literal: D3D copies it at SetPixelShader)
//   lit = (z' - map <= 0), soft = saturate(1 - 200 * (z' - map))   (200 = the literal c10.w)
// i.e. the receiver is pulled toward the sun by 20000 * (0.0015334 - 0.0013333 NdotL) = 4 cm (facing the sun) to 31 cm
// (edge-on): a slope bias in world units, the same for both cascades. A cascade k times longer has texels k times
// wider (k / m with an m times larger map); the depth error of a sloped receiver inside one texel grows with the texel,
// so the same 4-31 cm loses against it sooner: acne stripes. The fix scales the far cascade's bias only, by
//   t = clamp(pdz_shadow_distance_scale / m * pdz_shadow_bias_scale, 1, 8)
// (m = the native shadow maps' extra resolution, ShadowMapRelativeScale, when the native renderer is the only output;
// else 1: the emulated maps have none), without touching a constant: PdzShadowFarDepthRange stretches the far
// cascade's light depth range from 0..20000 to 0..20000 t (divides the z column of its ortho matrix by t in the setup,
// before the matrix is copied into the cascade table 82FBD6C0 that the caster pass, the mask shader and the forward
// receivers all read). The normalized bias then means t times the centimetres; the casters write and the receivers
// compare in the same stretched units, so nothing else moves (the soft-compare widths, 100 cm for c10.w, grow by t
// too). The near cascade (cascade 0) keeps Rare's 0..20000. At t = 1 the hook returns before touching anything. 24-bit
// depth over 8 x 20000 cm still resolves 0.01 cm.
// PCF: the mask shader point-samples (a filter override in the fetch instruction), so softer far edges need another
// shader: pdz_shadow_far_pcf is native only (native_hlslfix.h PatchShadowFarPcf, applied in native_coissue.cpp). The
// patched shader reads its radius from c13.z, a component of the bias literal the original never reads (0 in the
// literal), which PdzShadowMaskConstants writes after the mask's SetPixelShader while the cvar is above 0 and the
// native renderer is on. The emulated path never reads c13.z.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "../../pdz_version.h"

#include "cutscene_dump.h" // pdz_cutscene_frame_dump: the frame's camera matrix
#include "frame_timing.h"  // pdz::timing::Enabled (the caster counter's switch)
#include "native/native.h" // Enabled, ShadowMapRelativeScale; gpu_sync.h NullBackend; pdz::shadow guest helpers
#include "../guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DECLARE(int32_t, pdz_shadow_map_scale); // native/native_cvars.cpp

REXCVAR_DEFINE_DOUBLE(pdz_shadow_distance_scale, 1.0, "Render",
                      "Enhancement (polish 6): how far the sun's shadows reach. The game ends them at 20 m and fades "
                      "them out from 16 m (BUG_AUDIT VR5); this scales both, and the far shadow cascade with them: 2 = "
                      "shadows to 40 m fading from 32 m, 4 = 80 m (the light's depth range holds about 4). The near "
                      "cascade (to 3 m) follows with pdz_shadow_split_follow. The far cascade's map keeps its size, so far shadows get "
                      "coarser by the same factor (with the native renderer, pdz_shadow_map_scale 2 doubles the shadow "
                      "maps' resolution). The shadow casters follow (polish 6d): the game draws into the shadow maps "
                      "only the objects within about 14 m of a point 10 m ahead of the camera, and that caster box "
                      "scales too (pdz_shadow_caster_scale: automatically up to x2, within 28 m of a point 20 m ahead), "
                      "so far props cast again; more casters cost CPU time in the shadow pass. Both GPU paths; changes apply on the next frame. 1 = original, "
                      "clamped 1-4")
    .range(1.0, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(pdz_shadow_bias_scale, 1.0, "Render",
                      "Enhancement (polish 6b / shadows 2, with pdz_shadow_distance_scale): extra factor on the far "
                      "sun-shadow cascade's automatic depth bias. The game's bias is fixed in world units (4 cm on "
                      "ground facing the sun to 31 cm edge-on, in the shadow mask shader), tuned for its 20 m cascade; "
                      "even then low-sun slopes show some acne, and the longer cascade shows it out to 60 m. The far "
                      "cascade's bias is x the texel size (pdz_shadow_distance_scale / the native shadow maps' extra "
                      "resolution, pdz_shadow_map_scale, when the native renderer is the only output; at least 1) x "
                      "the longer-shadow factor (1 at distance 1, rising to 4 from distance 2: calibrated on the "
                      "user's 2026-10-27 test, where bias 4 at distance 3 / map 3 removed the acne on the River "
                      "rocks) x this, kept within 1-8 (the near cascade is untouched). 1 = the automatic bias: x4 at "
                      "distance 3 / map 3, exactly the original at the stock settings (distance 1); 0.25-0.5 = less, "
                      "if far shadows look detached from their casters; 2 = more, if stripes remain. The far PCF does "
                      "not need more (shadows 2 sweep: the same picture against the bias with it on or off). Both GPU "
                      "paths; changes apply on the next frame. Range 0.25-4")
    .range(0.25, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(pdz_shadow_far_pcf, 0.0, "Render",
                      "Native renderer only, enhancement (polish 6b): soft edges in the far sun-shadow cascade. The game "
                      "point-samples its shadow maps (one texel decides lit or not), so the far cascade stretched by "
                      "pdz_shadow_distance_scale shows blocky texel edges. Above 0: the shadow mask shader (34d98195) is "
                      "recompiled at its first use with nine bilinear comparison taps (2 x 2 texels each, tent "
                      "weighted) spaced this many shadow-map texels apart in the far cascade: 1 = a smooth edge about "
                      "4 texels wide, 2 = softer. The near cascade keeps the game's single sample. 0 = off (the shader "
                      "is not touched). The shader is patched only if this is above 0 at its first use (set it at "
                      "start; a later change from 0 needs a restart); the radius is read every frame. The emulated GPU "
                      "path has no shader patch layer and draws as before. Range 0-4")
    .range(0.0, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_shadow_split_follow, true, "Render",
                    "Enhancement (polish 6c, part of pdz_shadow_distance_scale): the near sun-shadow cascade's end (3 m, "
                    "the split where the sharp near map hands over to the far one) scales with "
                    "pdz_shadow_distance_scale like the far end, so the two cascades keep the game's proportions (near "
                    "texels 6.7 x finer than far ones instead of 20 x at distance 3) and the visible ring at the split "
                    "moves out with the shadows (9 m at distance 3). The near map then covers k times more ground, "
                    "still finer than the game's own far cascade. Off: the split stays at 3 m whatever the distance. "
                    "Does nothing while the distance scale is 1. Both GPU paths; changes apply on the next frame")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(pdz_shadow_split_blend, 0.25, "Render",
                      "Native renderer only, enhancement (polish 6c, part of pdz_shadow_distance_scale): blends the near "
                      "sun-shadow cascade into the far one over the last part of the near cascade instead of switching "
                      "at one distance (the ring around the player). The width of that band as a fraction of the near "
                      "cascade's end: 0.25 = its last quarter (6.75-9 m at distance 3 with pdz_shadow_split_follow). "
                      "There the shadow mask shader (34d98195) also samples the far map (with pdz_shadow_far_pcf's "
                      "taps when that is on) and mixes the two results. Active only while pdz_shadow_distance_scale is "
                      "above 1; the shader is patched only if this is above 0 and the distance scale above 1 at its first "
                      "use (set both at start); the width is read every frame (0 live = the original switch). The "
                      "emulated GPU path has no shader patch layer and switches as before. 0 = off, range 0-0.75")
    .range(0.0, 0.75)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_shadow_far_object_fade_fix, true, "Render",
                    "Native renderer only, fix for the longer sun shadows (pdz_shadow_distance_scale above 1; nothing "
                    "changes at the stock distance): the game fades an object's shadow (palms, props, characters) out "
                    "with the distance behind the object. The far cascade's larger depth bias stretched that fade 4 "
                    "times, so a palm's shadow carried on behind it across walls and showed as a second, darker copy "
                    "beside its normal shadow (MP Desert). On: the game's own fade length in the far cascade. Off: "
                    "as before (to compare)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_shadow_stable, false, "Render",
                    "Enhancement (polish 6c): rotation-stable sun-shadow cascades. The game fits each cascade's shadow "
                    "map to the view and orients it by the camera, so every shadow edge crawls when the camera turns "
                    "or moves (original behaviour). On: the shadow maps keep one orientation in the world, each "
                    "cascade's square covers a sphere around its slice of the view (the same size whatever the "
                    "direction) and moves in whole shadow-map texels, so standing shadows stay still while looking "
                    "around. The squares are about 15-20% wider than the game's fitted boxes (slightly coarser shadows). "
                    "Both GPU paths; changes apply on the next frame. Off = original")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  // Set by the Long shadows preset (ApplyPreset, end of this file) while it is applied; 1 = no change (the preset off,
  // or the plain pdz_shadow_* settings used by hand).
  std::atomic<float> g_long_split_extra{1.0f};  // extra factor on the near cascade's end (pdz_long_shadows_split)
  std::atomic<float> g_long_object_fade{1.0f};  // far object shadow fade length, x the game's (pdz_long_shadows_object_fade)

  // The factor in effect: 1 for 1, anything below it or not a number (the original), at most 4.
  float DistanceScale()
  {
    const double v = REXCVAR_GET(pdz_shadow_distance_scale);
    if (!(v > 1.0))
    {
      return 1.0f;
    }
    return float(std::min(v, 4.0));
  }

  std::atomic<float> g_logged{1.0f};

  // One line per change of the value in effect (the first use included).
  void NoteScale(float k, float far_end)
  {
    if (g_logged.exchange(k, std::memory_order_relaxed) != k)
    {
      REXLOG_INFO("pdz_shadow: sun shadow distance x{:g} (pdz_shadow_distance_scale): cut-off {:g} cm, fade from "
                  "{:g} cm (original 2000 / 1600); the near cascade's split: see pdz_shadow_split_follow",
                  k, far_end, 1600.0f * k);
    }
  }

  // The native shadow maps' extra resolution the far bias can count on: ShadowMapRelativeScale() when the native
  // renderer is the only output (graphics_backend null), else 1 (the emulated maps are the game's size x the plugin's
  // resolution_scale like every other target; with both renderers on, the bias suits the coarser one).
  float MapRelativeScale()
  {
    if (!pdz::gpusync::NullBackend())
    {
      return 1.0f;
    }
    const float m = pdz::native::ShadowMapRelativeScale();
    return m > 1.0f ? m : 1.0f;
  }

  // The longer-shadow factor (shadows 2): 1 at distance 1 (the original), rising linearly to 4 at distance 2 and held
  // there. Rare's 4-31 cm already leave some acne on low-sun slopes inside 20 m; the longer far cascade shows it out to
  // 60 m. 4 = the user's 2026-10-27 calibration (pdz_shadow_bias_scale 4 at distance 3 / map 3, where the texel size
  // equals the stock 4K one, removed the River rock acne).
  float LongShadowFactor(float k)
  {
    return 1.0f + 3.0f * std::clamp(k - 1.0f, 0.0f, 1.0f);
  }

  // The far cascade's bias factor t: texel size (distance scale / map scale, at least 1) x the longer-shadow factor x
  // pdz_shadow_bias_scale, at least 1 (the original) and at most 8. Exactly 1 at the stock settings (distance 1, bias
  // scale 1); x4 at distance 3 / map 3 (the user's calibration); x8 (the cap) for the emulated path at distance 3.
  float FarBiasScale(float k, float m, float p)
  {
    double b = REXCVAR_GET(pdz_shadow_bias_scale);
    if (!(b > 0.0))
    {
      b = 1.0; // not a number
    }
    const double t = std::max(1.0, double(k) / double(m)) * double(p) * std::clamp(b, 0.25, 4.0);
    if (!(t > 1.0))
    {
      return 1.0f;
    }
    return float(std::min(t, 8.0));
  }

  std::atomic<float> g_bias_logged{1.0f};

  // One line per change of the far bias factor in effect (the first use included).
  void NoteBias(float t, float k, float m, float p)
  {
    if (g_bias_logged.exchange(t, std::memory_order_relaxed) != t)
    {
      REXLOG_INFO("pdz_shadow: far cascade bias x{:g} (texel size: distance x{:g} / shadow map x{:g}, x longer-shadow "
                  "factor x{:g} x "
                  "pdz_shadow_bias_scale {:g}): its light depth range 0..{:g} cm (original 0..20000), so its slope "
                  "bias {:g}-{:g} cm (original 4-31); near cascade unchanged",
                  t, k, m, p, REXCVAR_GET(pdz_shadow_bias_scale), 20000.0f * t, 4.0f * t, 30.7f * t);
    }
  }

  void StoreFloat(uint8_t *base, uint32_t address, float value)
  {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    bits = pdz::shadow::Swap32(bits);
    std::memcpy(const_cast<uint8_t *>(pdz::shadow::GuestPtr(base, address)), &bits, sizeof(bits));
  }

  void Store64(uint8_t *base, uint32_t address, uint64_t value)
  {
    const uint32_t hi = pdz::shadow::Swap32(uint32_t(value >> 32)), lo = pdz::shadow::Swap32(uint32_t(value));
    uint8_t *p = const_cast<uint8_t *>(pdz::shadow::GuestPtr(base, address));
    std::memcpy(p, &hi, sizeof(hi));
    std::memcpy(p + 4, &lo, sizeof(lo));
  }

  // Polish 6c: the split blend band in effect (pdz_shadow_split_blend, a fraction of the near cascade's end): 0 = off,
  // which it always is while the distance scale is 1 (the longer shadows off) or the value is not a number.
  float SplitBlendBand()
  {
    const double v = REXCVAR_GET(pdz_shadow_split_blend);
    if (!(v > 0.0) || DistanceScale() == 1.0f)
    {
      return 0.0f;
    }
    return float(std::min(v, 0.75));
  }
} // namespace

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C3CC (stfsx f13,r10,r31) in the shadow setup sub_8260C0D8:
// f13 = the fade start (1600.0, loaded at 8260C3A4), about to be stored into light[424 + view].
void PdzShadowFadeStart(PPCRegister &f13)
{
  const float k = DistanceScale();
  if (k == 1.0f)
  {
    return;
  }
  f13.f64 = double(float(f13.f64) * k);
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C3D8 (addi r25,r10,-10560) in the shadow setup
// sub_8260C0D8: f26 = the far cascade's end (2000.0, loaded at 8260C3D4), stored into light[432 + view] by the
// cascade loop (8260C420) and read back for the far box (8260C46C) and by every receiver.
namespace
{
  // Test trail (2026-10-28): every pdz_shadow_* setting is logged once at the session's first shadow setup, and
  // every later change of one (F4 overlay, console, the test script's cvar command, code) logs one line old -> new,
  // from the cvar registry's change callback, so a play test's log shows what was really switched and when. Logging
  // only: nothing reads these values.
  std::mutex g_cvar_log_mutex;
  std::unordered_map<std::string, std::string> g_cvar_last;
  std::once_flag g_cvar_log_once;

  void RegisterShadowCvarLog()
  {
    std::vector<std::string> names;
    for (std::string &name : rex::cvar::ListFlags())
    {
      if (name.rfind("pdz_shadow_", 0) == 0)
      {
        names.push_back(std::move(name));
      }
    }
    // The values are read before taking g_cvar_log_mutex: a change callback runs under the registry's lock and then
    // takes ours, so ours is never held while asking the registry (one lock order).
    std::vector<std::string> values;
    std::string all;
    for (const std::string &name : names)
    {
      values.push_back(rex::cvar::GetFlagByName(name));
      all += fmt::format("{}{} {}", all.empty() ? "" : ", ", name, values.back());
    }
    {
      std::lock_guard lock(g_cvar_log_mutex);
      for (size_t i = 0; i < names.size(); ++i)
      {
        g_cvar_last.emplace(names[i], values[i]); // a change logged meanwhile keeps its newer value
      }
    }
    for (const std::string &name : names)
    {
      rex::cvar::RegisterChangeCallback(name, [](std::string_view n, std::string_view value) {
        std::lock_guard lock(g_cvar_log_mutex);
        std::string &last = g_cvar_last[std::string(n)];
        if (last != value)
        {
          REXLOG_INFO("pdz_shadow: {} {} -> {} (live change)", n, last, value);
          last = std::string(value);
        }
      });
    }
    REXLOG_INFO("pdz_shadow: settings at the first shadow setup: {} (live changes are logged 'old -> new')", all);
  }
} // namespace

namespace pdz::sunshadow
{
  void ApplyPreset(bool game_thread = false); // the "Long shadows" preset (end of this file)
  void NoteMaskShader(bool long_patches);     // native_coissue.cpp: how the shadow mask shader was prepared
}

void PdzShadowFarEnd(PPCRegister &f26)
{
  pdz::sunshadow::ApplyPreset(true);
  std::call_once(g_cvar_log_once, RegisterShadowCvarLog);
  const float k = DistanceScale();
  if (k == 1.0f)
  {
    if (g_logged.load(std::memory_order_relaxed) != 1.0f)
    {
      NoteScale(1.0f, float(f26.f64)); // back to the original (a live change)
    }
    return;
  }
  f26.f64 = double(float(f26.f64) * k);
  NoteScale(k, float(f26.f64));
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C568 (add r11,r29,r28) in the shadow setup sub_8260C0D8,
// right after the ortho fit sub_8260D518 (8260C564) built cascade r28's light projection at r1 + 288 (OrthoOffCenter
// sub_827D7280, row-major: element 10 = 1 / (zn - zf), 14 = zn / (zn - zf); zn 0, zf 20000) and before the loop at
// 8260C5A8 copies it into the cascade table. For the far cascade (r28 = 1) the z column (elements 2, 6, 10, 14) is
// divided by t (FarBiasScale: texel size x the longer-shadow factor x pdz_shadow_bias_scale): the same projection with zf' = zn + t (zf - zn), so the shader's normalized bias is t
// times the centimetres. Cascade 0 (near) and t = 1 return without a write.
void PdzShadowFarDepthRange(PPCRegister &r28, PPCRegister &r1)
{
  if (r28.u32 != 1)
  {
    return;
  }
  const float k = DistanceScale(), m = MapRelativeScale(), p = LongShadowFactor(k);
  const float t = FarBiasScale(k, m, p);
  if (t == 1.0f)
  {
    if (g_bias_logged.load(std::memory_order_relaxed) != 1.0f)
    {
      NoteBias(1.0f, k, m, p); // back to the original (a live change)
    }
    return;
  }
  uint8_t *base = rex::Runtime::instance()->virtual_membase();
  const uint32_t matrix = r1.u32 + 288;
  for (uint32_t i = 2; i < 16; i += 4)
  {
    const uint32_t address = matrix + 4 * i;
    StoreFloat(base, address, pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address)) / t);
  }
  NoteBias(t, k, m, p);
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 826079AC in the sun shadow mask pass sub_82607470, right after
// its SetPixelShader (826079A8, the mask shader 34d98195; D3D has just copied the shader's literals into the device's
// PS constants, c13 = {0.0013333, -0.0015334, 0, 0}, and marked them dirty). With the native renderer on: c13.z = the
// pdz_shadow_far_pcf radius in texels while it is above 0, c13.w = the pdz_shadow_split_blend band while it is above 0
// and the distance scale above 1 (polish 6c, SplitBlendBand); the native renderer's patched shader reads them, the
// original shader never reads c13.z / w. The PS group c12-c15 is then marked dirty again (bit 63 - 13 / 4 of the PS mask
// at device + 24, as the game marks its own constant writes). Neither: nothing is written; every SetPixelShader restores
// the literal's 0 anyway.
void PdzShadowMaskConstants()
{
  const double radius = REXCVAR_GET(pdz_shadow_far_pcf);
  const float band = SplitBlendBand();
  // pdz_shadow_far_object_fade_fix: c17.z = the far cascade's depth-range factor t (PdzShadowFarDepthRange), read by
  // the patched shader to give the far cascade's object shadow fade its original length; 0 (the literal) = no change.
  float fade_t = 1.0f;
  if (REXCVAR_GET(pdz_shadow_far_object_fade_fix))
  {
    const float k = DistanceScale();
    fade_t = FarBiasScale(k, MapRelativeScale(), LongShadowFactor(k));
    // pdz_long_shadows_object_fade (the preset only): the fade that many times the game's length (the shader takes
    // max(c17.z, 1), so it never gets longer than the unfixed stretch).
    fade_t = std::max(1.0f, fade_t / g_long_object_fade.load(std::memory_order_relaxed));
  }
  if ((!(radius > 0.0) && band == 0.0f && fade_t == 1.0f) || !pdz::native::Enabled())
  {
    return;
  }
  namespace dev = pdz::shadow::dev;
  uint8_t *base = rex::Runtime::instance()->virtual_membase();
  const uint32_t device = pdz::shadow::Load32(base, dev::kDevicePointerGlobal);
  if (!pdz::shadow::IsGuestPointer(device))
  {
    return;
  }
  constexpr uint32_t kMaskBiasRegister = 13;
  if (radius > 0.0)
  {
    StoreFloat(base, device + dev::kPsAluConstants + 16 * kMaskBiasRegister + 8, float(std::min(radius, 4.0)));
  }
  if (band > 0.0f)
  {
    StoreFloat(base, device + dev::kPsAluConstants + 16 * kMaskBiasRegister + 12, band);
  }
  const uint32_t dirty = device + dev::kDirtyMasks + 8; // PS ALU constants: one bit per 4 registers, from bit 63
  uint64_t bits = uint64_t(1) << (63 - kMaskBiasRegister / 4);
  if (fade_t != 1.0f)
  {
    constexpr uint32_t kMaskFadeRegister = 17; // literal {0.7, 0.3, 0, 0}: z unused by the original shader
    StoreFloat(base, device + dev::kPsAluConstants + 16 * kMaskFadeRegister + 8, fade_t);
    bits |= uint64_t(1) << (63 - kMaskFadeRegister / 4);
    static std::atomic<float> logged{1.0f};
    if (logged.exchange(fade_t, std::memory_order_relaxed) != fade_t)
    {
      REXLOG_INFO("pdz_shadow: far object shadow fade fix (pdz_shadow_far_object_fade_fix): the far cascade's object "
                  "shadow fade x{:g} shorter again (= the game's length)",
                  fade_t);
    }
  }
  Store64(base, dirty, pdz::shadow::Load64(base, dirty) | bits);
}

// Polish 6c: the cascade layout (PDZRenderResearch native\stage2\polish\shadows2_cascade.md).
// The setup's cascade loop (8260C3E4-8260C5C4, r28 = cascade) builds, per cascade, the 8 corners of the view frustum
// from the eye's near plane (light[426 / 428 + view] = f25) to the cascade's end (light[430 + view] = f27 = 300 for the
// near cascade, light[432 + view] = f26 for the far one) in view space at r1 + 160 (4 far corners, x y z) and r1 + 208
// (4 near ones), then the ortho fit sub_8260D518 transforms them by the light view at 82FBDAC0 (r26, built once per
// call at 8260C37C by the LookAt sub_827D6470 from the view-space light direction light[1136 + 12 view], eye 10000
// back), takes their 2D hull (sub_8260D5F8), its bounding rectangle, and OrthoOffCenterRH (sub_827D7280) at r1 + 288:
// [0] = 2 / (r - l), [5] = 2 / (t - b), [12] = -(l + r) / (r - l), [13] = -(t + b) / (t - b), the z column = the
// 0..20000 depth range. 8260C588 multiplies the world->view matrix (r23, the setup's argument r5) by the light view
// into the table 82FBD2C0 (world->light), the copy loop puts the projection into 82FBD6C0; the caster pass, the mask
// shader (c0-c7 = view->world x both) and the forward receivers read only those tables. Matrices are row-major for row
// vectors (p' = p M), element [4 row + column].
//
// pdz_shadow_split_follow: PdzShadowSplit scales f27 (the split, 300) by the distance scale before the loop stores it
// (8260C408); every receiver reads the split back from light[430 + view] (c8.w), so nothing else moves.
//
// pdz_shadow_stable: the light view is built in view space (its up vector from the camera's axes) and each box is the
// view slice's bounding rectangle, so the shadow map's texel grid turns and changes size with the camera, and slides
// with it (no snapping): every shadow edge crawls when the camera turns (the original, kept by default). With the
// switch on:
//   PdzShadowStableBasis (before 8260C380, right after the light view is built): turns the light view about its own z
//     axis so that a fixed world axis (Y, else X, else Z: the first one more than ~25 degrees from the sun) projects
//     onto light +y. The light's x / y axes are then fixed in the world; z (the sun) and the depth range do not change.
//   PdzShadowStableBox (before 8260C56C, after the fit and after PdzShadowFarDepthRange at 8260C568): replaces the
//     box with a square of side 2R around the bounding sphere of the cascade's view slice (from the corners at
//     r1 + 160 / 208: the same for any camera direction; R rounded up to 1/16 octave so a changing field of view does
//     not resize it every frame), its corner a whole number of texels of the guest's 1344-texel grid from the world
//     origin's light-space position (the native maps are an integer multiple of 1344, so their texels line up too).
//     Only elements 0, 5, 12 and 13 are written; the z column stays as the fit and the bias hook left it.
// Off: both return before reading anything.
namespace
{
  constexpr double kGuestShadowTexels = 1344.0; // the shadow pass's viewport (8260C93C)

  struct Matrix4
  {
    double m[16]; // row-major, row vectors: m[4 * row + column]
  };

  bool LoadMatrix4(const uint8_t *base, uint32_t address, Matrix4 &out)
  {
    if (!pdz::shadow::IsGuestPointer(address))
    {
      return false;
    }
    for (uint32_t i = 0; i < 16; ++i)
    {
      out.m[i] = double(pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address + 4 * i)));
      if (!std::isfinite(out.m[i]))
      {
        return false;
      }
    }
    return true;
  }

  double LoadDouble(const uint8_t *base, uint32_t address)
  {
    return double(pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address)));
  }

  // The near cascade's end factor in effect: the distance scale with pdz_shadow_split_follow on, else 1.
  float SplitFactor()
  {
    if (!REXCVAR_GET(pdz_shadow_split_follow))
    {
      return 1.0f;
    }
    const float k = DistanceScale();
    return k == 1.0f ? 1.0f : k * g_long_split_extra.load(std::memory_order_relaxed);
  }

  std::atomic<float> g_split_logged{1.0f};

  // One line per change of the split factor in effect (the first use included).
  void NoteSplit(float f, float split)
  {
    if (g_split_logged.exchange(f, std::memory_order_relaxed) != f)
    {
      REXLOG_INFO("pdz_shadow: near cascade split x{:g} (pdz_shadow_split_follow, distance x{:g}): {:g} cm (original "
                  "300); split blend band {:g} (pdz_shadow_split_blend, native renderer only)",
                  f, DistanceScale(), split, SplitBlendBand());
    }
  }

  std::atomic<bool> g_stable_logged{false};
  std::atomic<uint32_t> g_stable_box_logs{0};
  std::atomic<float> g_stable_radius[2] = {0.0f, 0.0f};

  // One line per change of pdz_shadow_stable in effect.
  void NoteStable(bool on)
  {
    if (g_stable_logged.exchange(on, std::memory_order_relaxed) != on)
    {
      REXLOG_INFO("pdz_shadow: rotation-stable cascades {} (pdz_shadow_stable){}", on ? "on" : "off",
                  on ? ": world-fixed light axes, square boxes around each view slice's bounding sphere, snapped to "
                       "whole shadow-map texels"
                     : ": the game's fit (turns and slides with the camera)");
      g_stable_radius[0].store(0.0f, std::memory_order_relaxed);
      g_stable_radius[1].store(0.0f, std::memory_order_relaxed);
    }
  }
} // namespace

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C3D0 (lis r10,-32004) in the shadow setup sub_8260C0D8:
// f27 = the near cascade's end (300.0, loaded at 8260C3C4), stored into light[430 + view] by the cascade loop
// (8260C408) and read back for the near box (8260C46C) and by every receiver (the split, c8.w). With
// pdz_shadow_split_follow on and the distance scale k above 1: f27 *= k. Otherwise it returns without touching it.
void PdzShadowSplit(PPCRegister &f27)
{
  const float f = SplitFactor();
  if (f == 1.0f)
  {
    if (g_split_logged.load(std::memory_order_relaxed) != 1.0f)
    {
      NoteSplit(1.0f, float(f27.f64)); // back to the original (a live change)
    }
    return;
  }
  f27.f64 = double(float(f27.f64) * f);
  NoteSplit(f, float(f27.f64));
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C380 (lis r10,-32241) in the shadow setup sub_8260C0D8,
// right after the LookAt sub_827D6470 (8260C37C) built the light view at r26 (82FBDAC0) in view space; r23 = the
// world->view matrix. pdz_shadow_stable only: the light view is turned about its z axis (V' = V Rz: its x / y columns,
// every row) so that a world-fixed axis points along light +y. Off: no read, no write.
void PdzShadowStableBasis(PPCRegister &r23, PPCRegister &r26)
{
  if (!REXCVAR_GET(pdz_shadow_stable))
  {
    if (g_stable_logged.load(std::memory_order_relaxed))
    {
      NoteStable(false);
    }
    return;
  }
  uint8_t *base = rex::Runtime::instance()->virtual_membase();
  Matrix4 view, light;
  if (!LoadMatrix4(base, r23.u32, view) || !LoadMatrix4(base, r26.u32, light))
  {
    return;
  }
  // The sun's direction in the world: light z in view space is the light view's column 2; view->world is the
  // transpose of the world->view rotation, so dir[i] = sum_j view[i][j] * column2[j].
  double dir[3];
  for (int i = 0; i < 3; ++i)
  {
    dir[i] = view.m[4 * i] * light.m[2] + view.m[4 * i + 1] * light.m[6] + view.m[4 * i + 2] * light.m[10];
  }
  const double len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
  if (!(len > 1e-6))
  {
    return;
  }
  // World Y, else X, else Z: the first axis more than ~25 degrees away from the sun (fixed for a fixed sun).
  int axis = 1;
  if (std::fabs(dir[1]) > 0.9 * len)
  {
    axis = std::fabs(dir[0]) > 0.9 * len ? 2 : 0;
  }
  // That axis in view space is row `axis` of the world->view rotation; its light x / y, through the light view.
  double u[2];
  for (int j = 0; j < 2; ++j)
  {
    u[j] = view.m[4 * axis] * light.m[j] + view.m[4 * axis + 1] * light.m[4 + j] + view.m[4 * axis + 2] * light.m[8 + j];
  }
  const double ulen = std::sqrt(u[0] * u[0] + u[1] * u[1]);
  if (!(ulen > 1e-6))
  {
    return;
  }
  const double a = u[0] / ulen, b = u[1] / ulen; // new x = (b, -a), new y = (a, b) in the old light x / y
  for (uint32_t row = 0; row < 4; ++row)
  {
    const double x = light.m[4 * row], y = light.m[4 * row + 1];
    StoreFloat(base, r26.u32 + 16 * row, float(x * b - y * a));
    StoreFloat(base, r26.u32 + 16 * row + 4, float(x * a + y * b));
  }
  NoteStable(true);
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C56C (mr r5,r26) in the shadow setup sub_8260C0D8, after the
// ortho fit (8260C564) wrote cascade r28's projection at r1 + 288 and PdzShadowFarDepthRange (8260C568) its z column;
// r1 + 160 / + 208 = the cascade's far / near frustum corners in view space, r26 = the light view (turned by
// PdzShadowStableBasis), r23 = world->view. pdz_shadow_stable only: x / y of the projection = a square of side 2R
// around the bounding sphere of the view slice, its corner on the world-fixed grid of 2R / 1344 texels. Off: no read,
// no write.
void PdzShadowStableBox(PPCRegister &r28, PPCRegister &r1, PPCRegister &r23, PPCRegister &r26)
{
  if (!REXCVAR_GET(pdz_shadow_stable) || r28.u32 > 1)
  {
    return;
  }
  uint8_t *base = rex::Runtime::instance()->virtual_membase();
  Matrix4 view, light;
  if (!LoadMatrix4(base, r23.u32, view) || !LoadMatrix4(base, r26.u32, light))
  {
    return;
  }
  // The slice: far corners at depth L, radius rf from the view axis; near corners at depth n, radius rn.
  double far_depth = 0.0, far_radius = 0.0, near_depth = 1e30, near_radius = 0.0;
  for (uint32_t c = 0; c < 4; ++c)
  {
    const uint32_t f = r1.u32 + 160 + 12 * c, n = r1.u32 + 208 + 12 * c;
    const double fx = LoadDouble(base, f), fy = LoadDouble(base, f + 4), fz = LoadDouble(base, f + 8);
    const double nx = LoadDouble(base, n), ny = LoadDouble(base, n + 4), nz = LoadDouble(base, n + 8);
    far_depth = std::max(far_depth, -fz);
    far_radius = std::max(far_radius, std::sqrt(fx * fx + fy * fy));
    near_depth = std::min(near_depth, -nz);
    near_radius = std::max(near_radius, std::sqrt(nx * nx + ny * ny));
  }
  if (!std::isfinite(far_depth + far_radius + near_depth + near_radius) || !(near_depth >= 0.0) ||
      !(far_depth > near_depth) || !(far_radius > 0.0))
  {
    return;
  }
  // The sphere through both rings, its centre on the view axis at depth zc (kept inside the slice).
  double zc = (far_depth * far_depth + far_radius * far_radius - near_depth * near_depth - near_radius * near_radius) /
              (2.0 * (far_depth - near_depth));
  zc = std::clamp(zc, near_depth, far_depth);
  double radius = std::max(std::hypot(far_depth - zc, far_radius), std::hypot(zc - near_depth, near_radius));
  radius = std::exp2(std::ceil(std::log2(radius) * 16.0) / 16.0);
  // Its centre (0, 0, -zc) and the world origin (world->view row 3) in light space (x, y).
  const double cx = -zc * light.m[8] + light.m[12], cy = -zc * light.m[9] + light.m[13];
  double ox = light.m[12], oy = light.m[13];
  for (int i = 0; i < 3; ++i)
  {
    ox += view.m[12 + i] * light.m[4 * i];
    oy += view.m[12 + i] * light.m[4 * i + 1];
  }
  const double texel = 2.0 * radius / kGuestShadowTexels;
  const double left = ox + std::round((cx - radius - ox) / texel) * texel; // u = (x - left) / 2R
  const double top = oy + std::round((cy + radius - oy) / texel) * texel;  // v = (top - y) / 2R
  const uint32_t matrix = r1.u32 + 288;
  StoreFloat(base, matrix + 4 * 0, float(1.0 / radius));
  StoreFloat(base, matrix + 4 * 5, float(1.0 / radius));
  StoreFloat(base, matrix + 4 * 12, float(-(left + radius) / radius));
  StoreFloat(base, matrix + 4 * 13, float(-(top - radius) / radius));
  NoteStable(true);
  const uint32_t cascade = r28.u32;
  if (g_stable_radius[cascade].exchange(float(radius), std::memory_order_relaxed) != float(radius))
  {
    const uint32_t n = g_stable_box_logs.fetch_add(1, std::memory_order_relaxed);
    if (n < 16)
    {
      REXLOG_INFO("pdz_shadow: stable {} cascade: view slice {:g}-{:g} cm, box {:g} cm square, guest texel {:g} cm{}",
                  cascade == 0 ? "near" : "far", near_depth, far_depth, 2.0 * radius, texel,
                  n == 15 ? " (further size changes not logged)" : "");
    }
  }
}

// Polish 6d: the shadow casters follow the distance (pdz_shadow_distance_scale; PDZRenderResearch
// native\stage2\polish\shadows2_casters.md).
// The caster passes (sub_8260C8C0 -> sub_8261D728 -> the callback sub_8261CA68 -> sub_824119A0 in renderer mode 2,
// flags 0x4001) draw only the objects whose shadow bit for the view is set; the visibility pass sets it per view in
// sub_82628888: it builds a caster box with sub_82629B78, and its room / object loops (sub_82627DE8, sub_826282A0,
// sub_82628568) set an object's shadow bit (object + 38, copied to the render table 82D73900 bits 4-7) only if the
// object's bounding sphere (object + 48, radius + 60) touches that box (sub_82727B50, sphere against an oriented box).
// The box is three values the shadow setup sub_8260C0D8 stores in the light (r31; idx = 2 view + [82E1BA20], M = r28,
// the camera matrix the setup transforms with):
//   8260C1BC  P0 = M (0, 0, -1000)             -> light + 1472 + 12 idx: the box's centre, 10 m ahead (820EB18C)
//   8260C1DC  P1 = M (10000 L + (0, 0, -1000)) -> light + 1568 + 12 idx: P0 + 100 m along the light (the 1000 is
//                                                 8208C2F4, L = light + 1136)
//   8260C200  R = 1414.0 (820EB188)            -> light[416 + idx]: the half width across the light (1000 x sqrt 2)
// sub_82629B78 turns them into a box around P0: axis P0 - P1 (half length 2 |P0 - P1| = 200 m, no limit in practice),
// +-R across it. So only objects within about 14 m (+ their radius) of a point 10 m ahead of the camera cast, whatever
// the cut-off: with the far cascade stretched to 60 m, props from about 25 m out lost their shadows. Characters take
// another path (sub_825A8688, no box) and kept theirs. Only sub_82629B78 reads the three values (the engine has no
// other reader of light + 1472 / 1568 / [416 + idx]), so PdzShadowCasterBox rewrites them right after the setup
// stored them: the centre k x 10 m ahead (recomputed with M as the setup does), P1 moved with it (the light axis and
// its length are kept) and R x k: the box keeps Rare's proportions to the k times longer cascade. At k = 1 it returns
// before any read or write. No camera / LOD distance cut-off was found for props (their cull state, vtable + 128 =
// sub_825173C8, only checks visibility flags), so normal rendering is untouched.
// Cost: the casters grow with the box's cross-section (up to k^2 x the props in it; the level's static geometry and
// the characters are not in this box), and every caster is drawn into both cascades (the near one clips it away on
// the GPU, but its draw is paid on the CPU). With pdz_frame_timing on, one 'pdz_shadow: casters' line every
// kCasterLogPasses shadow passes (any k): caster objects per pass (sub_824119A0's selection count in the caster
// passes, both cascades), the shadow pass's own time on the game thread (sub_8260C8C0 entry to exit: the guest's
// caster draws plus our draw hooks inside them) and PdzShadowCasterBox's time. Off: one cvar load per hook call.
namespace
{
  constexpr uint32_t kCasterBoxAhead = 0x820EB18C; // -1000.0: the box centre's view z (read, never written)
  constexpr float kCasterBoxHalfWidth = 1414.0f;   // 820EB188, for the log lines
  constexpr uint32_t kCasterLogPasses = 600;

  std::atomic<float> g_box_logged{1.0f};

  // One line per change of the caster box scale in effect (the first use included).
  void NoteCasterBox(float k)
  {
    if (g_box_logged.exchange(k, std::memory_order_relaxed) != k)
    {
      REXLOG_INFO("pdz_shadow: sun shadow casters x{:g} (pdz_shadow_caster_scale): caster box centre {:g} cm ahead "
                  "of the camera, half width {:g} cm across the light (original 1000 / 1414)",
                  k, 1000.0f * k, kCasterBoxHalfWidth * k);
    }
  }

  // The pdz_frame_timing counter (game thread: the setup, the shadow pass and the object renderer all run there).
  struct CasterStats
  {
    bool in_pass = false;
    std::chrono::steady_clock::time_point pass_start;
    uint64_t pass_objects = 0; // this pass, both cascades
    uint32_t passes = 0;
    uint64_t objects = 0, objects_max = 0;
    double pass_ms = 0.0, pass_ms_max = 0.0;
    uint64_t box_calls = 0;
    double box_us = 0.0;
  };
  CasterStats g_casters;

  double MsSince(std::chrono::steady_clock::time_point t0)
  {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
} // namespace

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C204 (addi r11,r10,95) in the shadow setup sub_8260C0D8,
// right after the caster box's last value was stored (8260C200 stfsx f0,r11,r31: R = 1414.0 at light + r11):
// r31 = the light, r10 = idx (P0 at light + 1472 + 12 idx, P1 at light + 1568 + 12 idx), r28 = the camera matrix the
// setup transformed P0 / P1 with (sub_827D15A8: row vectors, v M divided by w). With k > 1: P0 = M (0, 0, -1000 k),
// P1 += the same move, R *= k. k = 1: returns at once.
REXCVAR_DEFINE_DOUBLE(pdz_shadow_caster_scale, 0.0, "Render",
                      "Enhancement (shadows 2, with pdz_shadow_distance_scale): how far the sun-shadow caster box "
                      "grows with the longer shadows (the game draws into the shadow maps only the objects within about "
                      "14 m of a point 10 m ahead of the camera; this scales both). Every caster is drawn into both "
                      "cascades on the game thread: at distance 3 the full x3 box drew 230-340 objects per pass in the "
                      "31-bot Urban match (stock 36-55) and cost about 2 ms of game thread and 1.5 ms of GPU, too much "
                      "for 4K60. 0 = automatic: the distance scale, at most 2 (props cast to about 48 m, where the "
                      "longer shadows start to fade at distance 3); 1-4 = this factor, at most the distance scale (3 "
                      "at distance 3 = every prop to 60 m). Both GPU paths; changes apply on the next frame")
    .range(0.0, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  // The caster box factor in effect: 1 at distance 1 (the original, no write), else pdz_shadow_caster_scale (0 =
  // automatic: the distance scale, at most kAutoCasterScale), never above the distance scale.
  constexpr float kAutoCasterScale = 2.0f;
  float CasterScale()
  {
    const float k = DistanceScale();
    if (k == 1.0f)
    {
      return 1.0f;
    }
    const double v = REXCVAR_GET(pdz_shadow_caster_scale);
    const float c = v >= 1.0 ? float(std::min(v, 4.0)) : kAutoCasterScale;
    return std::clamp(c, 1.0f, k);
  }
} // namespace

// Far-map caster limit (Mon 16 Nov night, performance job step 2a; pdz_long_shadows_far_caster_limit, off).
// The shadow pass sub_8260C8C0 loops over the two cascades (index at 82E09F08: 0 near, 1 far; 8260CCDC-8260CF3C) and
// calls the caster draw sub_8261D728 twice per cascade; the object renderer sub_824119A0 (mode 2, flags 0x4001) then
// selects every object with the view's shadow bit into the list at 82D7CF00 (pointers to the 512-byte render table
// entries; r20 = count) and draws them all, whatever the cascade. With the limit on, in the FAR cascade's calls only,
// PdzShadowCasterCount takes out of that list the objects that are both small (bounding radius, object + 60, under
// pdz_long_shadows_far_caster_radius_cm) and far (centre, object + 48, further from the camera than
// pdz_long_shadows_far_caster_from_cm + the radius). The object is entry[10 + slot] (slot = 1 - [82FBE3C0], as
// 82411AD4 / 82411D48 read it). The camera position is the one PdzShadowCasterBox saw last (the setup's camera matrix,
// row 3). The level's own geometry and the characters are not in this list; the near cascade's calls are not touched;
// with the caster box at Rare's size (long shadows off) nothing is done. Off: one cvar read per shadow pass.
REXCVAR_DEFINE_BOOL(pdz_long_shadows_far_caster_limit, false, "Render",
                    "Long shadows, performance (Mon 16 Nov): small far objects (plants, small props) are not drawn "
                    "into the far sun-shadow map; large objects, the level's own geometry and characters still are, "
                    "and the near map is unchanged. Small = bounding radius under "
                    "pdz_long_shadows_far_caster_radius_cm; far = further from the camera than "
                    "pdz_long_shadows_far_caster_from_cm. Saves game-thread draw work in the shadow pass; those "
                    "objects lose their shadow beyond the near map. Only acts while the longer shadows are on. With "
                    "pdz_frame_timing a 'pdz_shadow: far caster limit' line every 600 shadow passes. Off = every "
                    "caster in both maps")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_long_shadows_far_caster_radius_cm, 200.0, "Render",
                      "pdz_long_shadows_far_caster_limit: an object with a bounding radius under this many cm counts "
                      "as small")
    .range(0.0, 100000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_long_shadows_far_caster_from_cm, 2400.0, "Render",
                      "pdz_long_shadows_far_caster_limit: small objects further from the camera than this many cm "
                      "(plus their radius) are left out of the far map. 2400 = about the reach of the game's own "
                      "caster box, so the longer shadows' extra reach is kept for large objects only")
    .range(0.0, 100000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Near-map caster cull (Mon 16 Nov night, step 2a part two; pdz_long_shadows_near_caster_cull, on since Tue 17 Nov): picture-identical.
// Per cascade the pass builds the caster matrix C = A x V[i] x P[i] (8260CD18-8260CD60, sub_827D4E08 twice; row
// vectors): A = the pass's r6 (kept in r20), V = the light view table 82FBD2C0, P = the light projection table
// 82FBD6C0 (64 bytes each), i = 2 (2 view + cascade) + [82E0A0DC] (view = the pass's r8, kept in r24), and sets user
// clip planes from C's side planes, so nothing outside the map's rectangle is ever written to it. The projection is
// orthographic along the sun, so a caster whose bounding sphere lies wholly outside the rectangle in light x or y
// (clip |x| or |y| - radius > 1) cannot write a texel whatever its depth. In the NEAR cascade's calls those casters
// are taken out of the selection list (the far cascade draws them as before). Kept whenever in doubt: C not affine
// (w not 1), a bad pointer, a NaN. pdz_long_shadows_near_caster_margin_cm widens the rectangle. PdzShadowPassBegin
// takes r20 / r24 for this (midasm_hooks_tu3.toml: registers = ["r20", "r24"]).
REXCVAR_DEFINE_BOOL(pdz_long_shadows_near_caster_cull, true, "Render",
                    "Long shadows, performance (Mon 16 Nov): objects that cannot reach the near sun-shadow map (their "
                    "bounding sphere is wholly outside the map's rectangle as the sun sees it) are not drawn into it. "
                    "The game draws every caster into both maps and lets the GPU clip the near map's away; this "
                    "skips that draw work on the game thread. No change to the picture is intended. Only acts while "
                    "the longer shadows are on. Off = every caster drawn into both maps")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_long_shadows_near_caster_margin_cm, 50.0, "Render",
                      "pdz_long_shadows_near_caster_cull: safety margin in cm added to every object's bounding radius "
                      "(raise it if a near shadow is ever missing)")
    .range(0.0, 100000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(pdz_long_shadows_far_caster_radius_offset, -1, "Render",
                     "pdz_long_shadows_far_caster_limit / pdz_long_shadows_near_caster_cull: where the object's "
                     "bounding radius is read (the offset of a float in the object; -1 = the model's value, "
                     "[[entry] + 4] + 56). The 'pdz_shadow: caster fields' log line shows the candidates' ranges")
    .range(-1, 508)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kCascadeLightView = 0x82FBD2C0; // V[i], 64 bytes each
  constexpr uint32_t kCascadeLightProj = 0x82FBD6C0; // P[i]
  constexpr uint32_t kCascadeTableFlag = 0x82E0A0DC; // [82E09F3C + 416]
  constexpr uint32_t kCascadeTableSize = 16;         // entries in each table (1024 bytes between the two)
  constexpr uint32_t kShadowCascadeIndex = 0x82E09F08; // sub_8260C8C0's cascade loop counter (0 near, 1 far)
  constexpr uint32_t kCasterList = 0x82D7CF00;         // sub_824119A0's selection: r20 render table entry pointers
  constexpr uint32_t kCasterSlotFlag = 0x82FBE3C0;     // slot = 1 - this (82411AD0)
  constexpr uint32_t kCasterListMax = 2500;            // the selection loop's table size

  constexpr int kNearCands = 5;
  const char *const kNearCandNames[kNearCands] = {"A*V*P", "V*P", "(P*V*A)t", "(P*V)t", "inv(A)*V*P"};
  constexpr int kDiagFields = 5;
  constexpr int kDiagOffsets[kDiagFields] = {44, 60, 64, 76, -1}; // -1: the model's [[entry] + 4] + 56
  struct NearCand
  {
    bool affine = false;
    float x[4] = {}, y[4] = {};
    float sx = 0.0f, sy = 0.0f;
    float cam[2] = {}, ahead[2] = {}; // clip x, y of the camera and of the point 150 cm ahead of it
    bool inside = false;              // affine and both points inside the map (|x|, |y| <= 1.02)
    uint32_t close_in = 0;            // sampled pass: casters within 500 cm of the camera whose centre is inside
  };
  struct FarCasterLimit
  {
    bool pass = false;   // inside a shadow pass with the limit in effect
    bool camera = false; // cam is valid
    float cam[3] = {};
    uint32_t passes = 0;
    uint64_t dropped = 0, kept = 0, near_drawn = 0;
    uint64_t far_small = 0, far_large = 0; // of the far cascade's list: under / over the radius (any distance)
    // pdz_long_shadows_near_caster_cull
    bool far_on = false, near_on = false; // the two switches as read at the pass's start
    uint32_t matrix_a = 0, view = 0;      // the pass's r20 / r24
    bool near_ready = false, near_ok = false; // the near cascade's matrix was built this pass / is usable
    float near_x[4] = {}, near_y[4] = {};     // clip x / y = dot((c, 1), these): the candidate picked
    float near_sx = 0.0f, near_sy = 0.0f;     // clip units per cm of radius
    uint64_t near_dropped = 0;
    // The self-check: the candidate conventions for the near cascade's caster matrix (kNearCands), the camera's
    // forward axis, and one sampled pass per log period (the first) for the diagnostic lines.
    bool fwd_ok = false;
    float fwd[3] = {};
    NearCand cand[kNearCands];
    int pick = -1;
    bool sampled = false;
    uint32_t close_total = 0;         // casters within 500 cm of the camera in the sampled near call
    std::vector<float> fields[kDiagFields];
  };
  FarCasterLimit g_far_limit;

  // The object's bounding radius for the two switches (pdz_long_shadows_far_caster_radius_offset).
  float CasterRadius(const uint8_t *base, uint32_t entry, uint32_t object, int offset)
  {
    // Sun 22 Nov night (audit): each hop only while it is there (NaN = the caster is kept, as before).
    using pdz::guestmem::Ok;
    if (offset >= 0)
    {
      return Ok(base, object + uint32_t(offset), 4)
                 ? pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, object + uint32_t(offset)))
                 : std::numeric_limits<float>::quiet_NaN();
    }
    const uint32_t a = Ok(base, entry, 4) ? pdz::shadow::Load32(base, entry) : 0;
    if (!pdz::shadow::IsGuestPointer(a) || !Ok(base, a + 4, 4))
    {
      return std::numeric_limits<float>::quiet_NaN();
    }
    const uint32_t model = pdz::shadow::Load32(base, a + 4);
    return pdz::shadow::IsGuestPointer(model) && Ok(base, model + 56, 4)
               ? pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, model + 56))
               : std::numeric_limits<float>::quiet_NaN();
  }

  struct M4
  {
    float m[4][4];
  };
  M4 Mul(const M4 &a, const M4 &b)
  {
    M4 out;
    for (uint32_t r = 0; r < 4; ++r)
    {
      for (uint32_t c = 0; c < 4; ++c)
      {
        out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c] + a.m[r][3] * b.m[3][c];
      }
    }
    return out;
  }
  M4 Transposed(const M4 &a)
  {
    M4 out;
    for (uint32_t r = 0; r < 4; ++r)
    {
      for (uint32_t c = 0; c < 4; ++c)
      {
        out.m[r][c] = a.m[c][r];
      }
    }
    return out;
  }
  // The inverse of a rigid row-vector transform (v R + t): rotation transposed, translation -t Rt.
  M4 RigidInverse(const M4 &a)
  {
    M4 out{};
    for (uint32_t r = 0; r < 3; ++r)
    {
      for (uint32_t c = 0; c < 3; ++c)
      {
        out.m[r][c] = a.m[c][r];
      }
    }
    for (uint32_t c = 0; c < 3; ++c)
    {
      out.m[3][c] = -(a.m[3][0] * out.m[0][c] + a.m[3][1] * out.m[1][c] + a.m[3][2] * out.m[2][c]);
    }
    out.m[3][3] = 1.0f;
    return out;
  }

  // The near cascade's caster matrix of this pass in every plausible convention; the one picked is the only one that
  // puts the camera and the point 150 cm ahead of it inside the near map (several that agree count as one).
  void BuildNearMatrix(FarCasterLimit &f, const uint8_t *base)
  {
    f.near_ready = true;
    f.near_ok = false;
    f.pick = -1;
    const uint32_t i = 2 * (2 * f.view) + pdz::shadow::Load32(base, kCascadeTableFlag); // cascade 0
    if (i >= kCascadeTableSize || !pdz::shadow::IsGuestPointer(f.matrix_a) || !f.camera || !f.fwd_ok)
    {
      for (NearCand &c : f.cand)
      {
        c = NearCand{};
      }
      return;
    }
    auto read = [base](uint32_t address) {
      M4 m;
      for (uint32_t k = 0; k < 16; ++k)
      {
        m.m[k / 4][k % 4] = pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address + 4 * k));
      }
      return m;
    };
    const M4 a = read(f.matrix_a), v = read(kCascadeLightView + 64 * i), p = read(kCascadeLightProj + 64 * i);
    const M4 vp = Mul(v, p), pv = Mul(p, v);
    const M4 mats[kNearCands] = {Mul(a, vp), vp, Transposed(Mul(pv, a)), Transposed(pv), Mul(RigidInverse(a), vp)};
    const float ahead[3] = {f.cam[0] + 150.0f * f.fwd[0], f.cam[1] + 150.0f * f.fwd[1], f.cam[2] + 150.0f * f.fwd[2]};
    int inside = 0;
    for (int k = 0; k < kNearCands; ++k)
    {
      const M4 &c = mats[k];
      NearCand &n = f.cand[k];
      const uint32_t close_in = n.close_in;
      n = NearCand{};
      n.close_in = close_in;
      for (uint32_t r = 0; r < 4; ++r)
      {
        n.x[r] = c.m[r][0];
        n.y[r] = c.m[r][1];
      }
      n.sx = std::sqrt(c.m[0][0] * c.m[0][0] + c.m[1][0] * c.m[1][0] + c.m[2][0] * c.m[2][0]);
      n.sy = std::sqrt(c.m[0][1] * c.m[0][1] + c.m[1][1] * c.m[1][1] + c.m[2][1] * c.m[2][1]);
      // Usable only if affine (orthographic: w = 1 everywhere) and with a real scale; a NaN fails every test here.
      n.affine = std::fabs(c.m[0][3]) < 1e-6f && std::fabs(c.m[1][3]) < 1e-6f && std::fabs(c.m[2][3]) < 1e-6f &&
                 std::fabs(c.m[3][3] - 1.0f) < 1e-4f && n.sx > 1e-9f && n.sy > 1e-9f && n.sx < 1.0f && n.sy < 1.0f;
      n.cam[0] = f.cam[0] * n.x[0] + f.cam[1] * n.x[1] + f.cam[2] * n.x[2] + n.x[3];
      n.cam[1] = f.cam[0] * n.y[0] + f.cam[1] * n.y[1] + f.cam[2] * n.y[2] + n.y[3];
      n.ahead[0] = ahead[0] * n.x[0] + ahead[1] * n.x[1] + ahead[2] * n.x[2] + n.x[3];
      n.ahead[1] = ahead[0] * n.y[0] + ahead[1] * n.y[1] + ahead[2] * n.y[2] + n.y[3];
      n.inside = n.affine && std::fabs(n.cam[0]) <= 1.02f && std::fabs(n.cam[1]) <= 1.02f &&
                 std::fabs(n.ahead[0]) <= 1.02f && std::fabs(n.ahead[1]) <= 1.02f;
      if (!n.inside)
      {
        continue;
      }
      ++inside;
      if (f.pick < 0)
      {
        f.pick = k;
      }
      else
      {
        // A second candidate inside: it must be the same projection as the first, or nothing is usable.
        const NearCand &q = f.cand[f.pick];
        float diff = 0.0f;
        for (uint32_t r = 0; r < 4; ++r)
        {
          diff = std::max({diff, std::fabs(q.x[r] - n.x[r]) / (r < 3 ? q.sx : 1.0f),
                           std::fabs(q.y[r] - n.y[r]) / (r < 3 ? q.sy : 1.0f)});
        }
        if (!(diff < 1e-3f))
        {
          f.pick = -2;
          break;
        }
      }
    }
    if (f.pick >= 0)
    {
      const NearCand &q = f.cand[f.pick];
      std::copy(q.x, q.x + 4, f.near_x);
      std::copy(q.y, q.y + 4, f.near_y);
      f.near_sx = q.sx;
      f.near_sy = q.sy;
      f.near_ok = true;
    }
  }

  // The sampled pass (the first of each log period): per candidate, how many of the casters within 500 cm of the
  // camera have their centre inside the near map; and the candidate radius fields of every caster in the list.
  void SampleCasters(FarCasterLimit &f, const uint8_t *base, uint32_t n, uint32_t slot, bool near_call)
  {
    auto loadf = [base](uint32_t address) { return pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address)); };
    for (uint32_t i = 0; i < n; ++i)
    {
      const uint32_t entry = pdz::shadow::Load32(base, kCasterList + 4 * i);
      if (!pdz::shadow::IsGuestPointer(entry))
      {
        continue;
      }
      const uint32_t object = pdz::shadow::Load32(base, entry + 4 * (10 + slot));
      if (!pdz::shadow::IsGuestPointer(object))
      {
        continue;
      }
      if (!near_call)
      {
        for (int k = 0; k < kDiagFields; ++k)
        {
          if (f.fields[k].size() < 4096)
          {
            f.fields[k].push_back(CasterRadius(base, entry, object, kDiagOffsets[k]));
          }
        }
        continue;
      }
      const float cx = loadf(object + 48), cy = loadf(object + 52), cz = loadf(object + 56);
      const float dx = cx - f.cam[0], dy = cy - f.cam[1], dz = cz - f.cam[2];
      if (!(dx * dx + dy * dy + dz * dz < 500.0f * 500.0f))
      {
        continue;
      }
      ++f.close_total;
      for (NearCand &c : f.cand)
      {
        const float x = cx * c.x[0] + cy * c.x[1] + cz * c.x[2] + c.x[3];
        const float y = cx * c.y[0] + cy * c.y[1] + cz * c.y[2] + c.y[3];
        c.close_in += std::fabs(x) <= 1.0f && std::fabs(y) <= 1.0f;
      }
    }
  }

  // The near cascade's selection list without the casters wholly outside the map's rectangle; r20 = the new count.
  void CullNearCasters(FarCasterLimit &f, uint8_t *base, PPCRegister &r20)
  {
    const uint32_t n = r20.u32;
    if (!f.near_ready)
    {
      BuildNearMatrix(f, base);
    }
    const uint32_t slot = 1u - pdz::shadow::Load32(base, kCasterSlotFlag);
    if (n == 0 || n > kCasterListMax || slot > 1)
    {
      f.near_drawn += n;
      return;
    }
    if (f.passes == 0 && f.camera)
    {
      SampleCasters(f, base, n, slot, true);
    }
    if (!f.near_ok)
    {
      f.near_drawn += n;
      return;
    }
    const float margin = float(REXCVAR_GET(pdz_long_shadows_near_caster_margin_cm));
    const int offset = REXCVAR_GET(pdz_long_shadows_far_caster_radius_offset);
    auto loadf = [base](uint32_t address) { return pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address)); };
    uint32_t out = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
      const uint32_t entry = pdz::shadow::Load32(base, kCasterList + 4 * i);
      bool drop = false;
      if (pdz::shadow::IsGuestPointer(entry))
      {
        const uint32_t object = pdz::shadow::Load32(base, entry + 4 * (10 + slot));
        if (pdz::shadow::IsGuestPointer(object))
        {
          const float cx = loadf(object + 48), cy = loadf(object + 52), cz = loadf(object + 56);
          const float reach = CasterRadius(base, entry, object, offset) + margin;
          const float x = cx * f.near_x[0] + cy * f.near_x[1] + cz * f.near_x[2] + f.near_x[3];
          const float y = cx * f.near_y[0] + cy * f.near_y[1] + cz * f.near_y[2] + f.near_y[3];
          // Every comparison is false for a NaN: the caster is kept. A negative or huge radius keeps it too.
          drop = reach >= margin && reach < 1.0e6f &&
                 (std::fabs(x) - reach * f.near_sx > 1.0f || std::fabs(y) - reach * f.near_sy > 1.0f);
        }
      }
      if (drop)
      {
        continue;
      }
      if (out != i)
      {
        uint32_t bits = pdz::shadow::Swap32(entry);
        std::memcpy(const_cast<uint8_t *>(pdz::shadow::GuestPtr(base, kCasterList + 4 * out)), &bits, sizeof(bits));
      }
      ++out;
    }
    f.near_dropped += n - out;
    f.near_drawn += out;
    r20.u64 = out;
  }

  // The far cascade's selection list, compacted in place; r20 = the new count.
  void LimitFarCasters(PPCRegister &r20)
  {
    FarCasterLimit &f = g_far_limit;
    uint8_t *base = rex::Runtime::instance()->virtual_membase();
    const uint32_t n = r20.u32;
    const uint32_t cascade = pdz::shadow::Load32(base, kShadowCascadeIndex);
    if (cascade == 0 && f.near_on)
    {
      CullNearCasters(f, base, r20);
      return;
    }
    if (cascade == 1 && f.passes == 0 && n <= kCasterListMax)
    {
      const uint32_t sample_slot = 1u - pdz::shadow::Load32(base, kCasterSlotFlag);
      if (sample_slot <= 1)
      {
        SampleCasters(f, base, n, sample_slot, false);
      }
    }
    if (cascade != 1 || !f.far_on)
    {
      (cascade == 1 ? f.kept : f.near_drawn) += n;
      return;
    }
    const uint32_t slot = 1u - pdz::shadow::Load32(base, kCasterSlotFlag);
    if (!f.camera || n == 0 || n > kCasterListMax || slot > 1)
    {
      f.kept += n;
      return;
    }
    const float small_cm = float(REXCVAR_GET(pdz_long_shadows_far_caster_radius_cm));
    const int radius_offset = REXCVAR_GET(pdz_long_shadows_far_caster_radius_offset);
    const float from = float(REXCVAR_GET(pdz_long_shadows_far_caster_from_cm));
    auto loadf = [base](uint32_t address) { return pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address)); };
    uint32_t out = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
      const uint32_t entry = pdz::shadow::Load32(base, kCasterList + 4 * i);
      bool drop = false;
      if (pdz::shadow::IsGuestPointer(entry))
      {
        const uint32_t object = pdz::shadow::Load32(base, entry + 4 * (10 + slot));
        if (pdz::shadow::IsGuestPointer(object))
        {
          const float radius = CasterRadius(base, entry, object, radius_offset);
          if (radius >= 0.0f && radius < small_cm)
          {
            ++f.far_small;
            const float dx = loadf(object + 48) - f.cam[0], dy = loadf(object + 52) - f.cam[1],
                        dz = loadf(object + 56) - f.cam[2];
            const float reach = from + radius;
            drop = dx * dx + dy * dy + dz * dz > reach * reach; // false for a NaN
          }
          else
          {
            ++f.far_large;
          }
        }
      }
      if (drop)
      {
        continue;
      }
      if (out != i)
      {
        uint32_t bits = pdz::shadow::Swap32(entry);
        std::memcpy(const_cast<uint8_t *>(pdz::shadow::GuestPtr(base, kCasterList + 4 * out)), &bits, sizeof(bits));
      }
      ++out;
    }
    f.dropped += n - out;
    f.kept += out;
    r20.u64 = out;
  }
} // namespace

void PdzShadowCasterBox(PPCRegister &r31, PPCRegister &r10, PPCRegister &r11, PPCRegister &r28)
{
  // cutscene_dump.h: the frame's camera matrix, read only (pdz_cutscene_frame_dump; one load while off).
  pdz::csdump::OnShadowCamera(r28.u32);
  const float k = CasterScale();
  if (k == 1.0f)
  {
    if (g_box_logged.load(std::memory_order_relaxed) != 1.0f)
    {
      NoteCasterBox(1.0f); // back to the original (a live change)
    }
    return;
  }
  const bool timed = pdz::timing::Enabled();
  const auto t0 = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  const uint32_t light = r31.u32, matrix = r28.u32, idx = r10.u32;
  if (!pdz::shadow::IsGuestPointer(light) || !pdz::shadow::IsGuestPointer(matrix) || idx >= 16)
  {
    return;
  }
  uint8_t *base = rex::Runtime::instance()->virtual_membase();
  auto load = [base](uint32_t address) { return pdz::shadow::BitsToFloat(pdz::shadow::Load32(base, address)); };
  const float z = load(kCasterBoxAhead) * k; // -1000 k
  float row2[4], row3[4];
  for (uint32_t i = 0; i < 4; ++i)
  {
    row2[i] = load(matrix + 32 + 4 * i);
    row3[i] = load(matrix + 48 + 4 * i);
  }
  const float w = z * row2[3] + row3[3];
  if (!(std::fabs(w) > 1e-6f))
  {
    return;
  }
  if (REXCVAR_GET(pdz_long_shadows_far_caster_limit) || REXCVAR_GET(pdz_long_shadows_near_caster_cull))
  {
    // The camera looks along -z of its matrix (the caster box centre is M (0, 0, -1000)): row 2 negated.
    const float len = std::sqrt(row2[0] * row2[0] + row2[1] * row2[1] + row2[2] * row2[2]);
    g_far_limit.fwd_ok = len > 1e-6f;
    for (uint32_t i = 0; i < 3 && g_far_limit.fwd_ok; ++i)
    {
      g_far_limit.fwd[i] = -row2[i] / len;
    }
    // The camera position (row 3 of the camera matrix, row vectors): for the far-map caster limit.
    g_far_limit.camera = std::fabs(row3[3]) > 1e-6f;
    for (uint32_t i = 0; i < 3 && g_far_limit.camera; ++i)
    {
      g_far_limit.cam[i] = row3[i] / row3[3];
    }
  }
  const uint32_t p0 = light + 1472 + 12 * idx, p1 = light + 1568 + 12 * idx, radius = light + r11.u32;
  for (uint32_t i = 0; i < 3; ++i)
  {
    const float centre = (z * row2[i] + row3[i]) / w;
    const float move = centre - load(p0 + 4 * i);
    StoreFloat(base, p0 + 4 * i, centre);
    StoreFloat(base, p1 + 4 * i, load(p1 + 4 * i) + move);
  }
  StoreFloat(base, radius, load(radius) * k);
  NoteCasterBox(k);
  if (timed)
  {
    g_casters.box_us += 1000.0 * MsSince(t0);
    ++g_casters.box_calls;
  }
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260C8F4 (lwz r31,8(r16)) in the shadow pass sub_8260C8C0,
// after its prologue: the pass's start (pdz_frame_timing only).
void PdzShadowPassBegin(PPCRegister &r20, PPCRegister &r24)
{
  {
    FarCasterLimit &f = g_far_limit;
    f.far_on = REXCVAR_GET(pdz_long_shadows_far_caster_limit);
    f.near_on = REXCVAR_GET(pdz_long_shadows_near_caster_cull);
    f.pass = (f.far_on || f.near_on) && CasterScale() > 1.0f;
    if (f.pass)
    {
      f.matrix_a = r20.u32;
      f.view = r24.u32;
      f.near_ready = false;
    }
  }
  if (!pdz::timing::Enabled())
  {
    g_casters.in_pass = false;
    return;
  }
  g_casters.in_pass = true;
  g_casters.pass_objects = 0;
  g_casters.pass_start = std::chrono::steady_clock::now();
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 82411BEC (li r11,1) in the object renderer sub_824119A0, after
// its selection loop: r20 = the objects it is about to draw, r16 = the renderer mode (2 in the caster passes; set by
// sub_8261D310), r15 = the pass flags (0x4001 there). Adds them to the shadow pass in progress (pdz_frame_timing only).
void PdzShadowCasterCount(PPCRegister &r15, PPCRegister &r16, PPCRegister &r20)
{
  if (g_far_limit.pass && r16.u32 == 2 && (r15.u32 & 0xFFFFu) == 0x4001u)
  {
    LimitFarCasters(r20); // pdz_long_shadows_far_caster_limit: the far cascade's list without its small far objects
  }
  if (!g_casters.in_pass || r16.u32 != 2 || (r15.u32 & 0xFFFFu) != 0x4001u)
  {
    return;
  }
  g_casters.pass_objects += r20.u32;
}

// Mid-function hook (midasm_hooks_tu3.toml), before T 8260CF5C (addi r1,r1,640) in sub_8260C8C0, the epilogue both of
// its paths reach: the pass's end; every kCasterLogPasses passes one 'pdz_shadow: casters' line.
void PdzShadowPassEnd()
{
  if (g_far_limit.pass)
  {
    FarCasterLimit &f = g_far_limit;
    f.pass = false;
    if (++f.passes >= kCasterLogPasses)
    {
      if (pdz::timing::Enabled())
      {
        const double n = double(f.passes);
        PDZ_LOG_DEFERRED("pdz_shadow: far caster limit (pdz_long_shadows_far_caster_limit, last {} shadow passes) | "
                         "far map per pass: dropped {:.1f}, kept {:.1f} (objects under {:g} cm radius {:.1f}, others "
                         "{:.1f}; dropped = small and over {:g} cm from the camera) | near map per pass: {:.1f} drawn, "
                         "{:.1f} dropped (pdz_long_shadows_near_caster_cull {}, matrix {})",
                         f.passes, double(f.dropped) / n, double(f.kept) / n,
                         REXCVAR_GET(pdz_long_shadows_far_caster_radius_cm), double(f.far_small) / n,
                         double(f.far_large) / n, REXCVAR_GET(pdz_long_shadows_far_caster_from_cm),
                         double(f.near_drawn) / n, double(f.near_dropped) / n, f.near_on ? "on" : "off",
                         !f.near_on ? "n/a" : f.near_ok ? kNearCandNames[f.pick] : "NOT usable: nothing culled");
        if (f.near_on)
        {
          std::string line;
          for (int k = 0; k < kNearCands; ++k)
          {
            const NearCand &c = f.cand[k];
            line += fmt::format(" | {}: {}camera {:.2f},{:.2f} ahead {:.2f},{:.2f} close casters inside {}/{}{}",
                                kNearCandNames[k], c.affine ? "" : "not affine, ", c.cam[0], c.cam[1], c.ahead[0],
                                c.ahead[1], c.close_in, f.close_total, k == f.pick ? " PICKED" : "");
          }
          PDZ_LOG_DEFERRED("pdz_shadow: near cull candidates (clip x,y of the camera {:.0f},{:.0f},{:.0f} and of the "
                           "point 150 cm ahead; a usable one has all four within 1; casters within 500 cm of the "
                           "camera, one sampled pass){}{}",
                           f.cam[0], f.cam[1], f.cam[2], line,
                           f.pick == -2 ? " | two candidates inside disagree" : "");
        }
        std::string fields;
        for (int k = 0; k < kDiagFields; ++k)
        {
          std::vector<float> &v = f.fields[k];
          std::sort(v.begin(), v.end(), [](float a, float b) { return (std::isnan(a) ? -1e30f : a) < (std::isnan(b) ? -1e30f : b); });
          const std::string name = kDiagOffsets[k] < 0 ? std::string("model+56 (offset -1)") : fmt::format("object+{}", kDiagOffsets[k]);
          fields += v.empty() ? fmt::format(" | {}: none", name)
                              : fmt::format(" | {}: min {:g} median {:g} max {:g}", name, v.front(), v[v.size() / 2], v.back());
        }
        PDZ_LOG_DEFERRED("pdz_shadow: caster fields (far map list, one sampled pass, {} objects; radius read at "
                         "pdz_long_shadows_far_caster_radius_offset {}){}",
                         f.fields[0].size(), REXCVAR_GET(pdz_long_shadows_far_caster_radius_offset), fields);
      }
      const bool camera = f.camera;
      const float cam[3] = {f.cam[0], f.cam[1], f.cam[2]};
      const bool fwd_ok = f.fwd_ok;
      const float fwd[3] = {f.fwd[0], f.fwd[1], f.fwd[2]};
      f = FarCasterLimit{};
      f.camera = camera;
      std::copy(cam, cam + 3, f.cam);
      f.fwd_ok = fwd_ok;
      std::copy(fwd, fwd + 3, f.fwd);
    }
  }
  CasterStats &s = g_casters;
  if (!s.in_pass)
  {
    return;
  }
  s.in_pass = false;
  const double ms = MsSince(s.pass_start);
  s.pass_ms += ms;
  s.pass_ms_max = std::max(s.pass_ms_max, ms);
  s.objects += s.pass_objects;
  s.objects_max = std::max(s.objects_max, s.pass_objects);
  if (++s.passes < kCasterLogPasses)
  {
    return;
  }
  const float k = CasterScale();
  PDZ_LOG_DEFERRED("pdz_shadow: casters (last {} shadow passes) | caster box x{:g} (pdz_shadow_caster_scale): caster box {:g} cm ahead, half width "
              "{:g} cm | caster objects per pass avg {:.1f} max {} (drawn into both cascades) | shadow pass ms avg "
              "{:.3f} max {:.3f} (game thread, sub_8260C8C0) | box hook us avg {:.2f} ({} calls)",
              s.passes, k, 1000.0f * k, kCasterBoxHalfWidth * k, double(s.objects) / s.passes, s.objects_max,
              s.pass_ms / s.passes, s.pass_ms_max, s.box_calls ? s.box_us / double(s.box_calls) : 0.0, s.box_calls);
  s = CasterStats{};
}

// "Long shadows" preset (Mon 16 Nov): one switch for the recommended combination of the settings above, for the
// Graphics tab later. pdz_long_shadows on sets pdz_shadow_distance_scale / pdz_shadow_far_pcf / pdz_shadow_bias_scale to
// the preset's values; off sets them back to the game's own (1 / 0 / 1). Applied when the switch changes (and once
// before the first shader is prepared when it is on from the start); with the switch never on, nothing is written, so
// the stock settings and hand-set values are untouched. The shadow map size stays the stock one (pdz_shadow_map_scale
// needs a restart and costs memory and four large depth resolves a frame; the preset does not need it). F7
// (bind_long_shadows) switches it while playing, to compare with the game's own shadows.
REXCVAR_DEFINE_BOOL(pdz_long_shadows, false, "Render",
                    "Enhancement, native renderer: longer sun shadows, as one switch. The game's shadows end at 20 m; "
                    "on: they reach 60 m with soft far edges (pdz_shadow_distance_scale = pdz_long_shadows_distance, "
                    "3; pdz_shadow_far_pcf 1, the far bias x4, the stock shadow map size, "
                    "pdz_shadow_far_object_fade_fix on). Off: "
                    "the game's own. F7 switches it while playing")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 2 (Mon 16 Nov evening): how far the preset reaches. Beyond the cut-off the game draws NO sun / moon shadow at
// all (the mask fades every cascade result to "lit" between the fade start and the far cascade's end), so ground under
// a far cliff is lit until the player is within the cut-off (Mansion grounds: the snow under the big rock; Jungle: the
// foliage that turns dark on approach). The level's big pieces are in both cascades' maps and not in the caster box,
// so only the cut-off decides. The first preset stopped at 40 m; 3 = 60 m is the longest the shadow study toured
// (River, Jungle: no new fault). The far bias is kept at the study's x4 for any distance (bias scale = 1 / distance,
// since the bias grows with the texel size by itself); the caster box for plants and props follows the distance too
// (pdz_shadow_caster_scale = the distance while the preset is on).
REXCVAR_DEFINE_DOUBLE(pdz_long_shadows_distance, 3.0, "Render",
                      "With pdz_long_shadows on: how far the shadows reach, as a multiple of the game's 20 m. 2 = 40 m "
                      "(fading from 32 m), 3 = 60 m (fading from 48 m), 4 = 80 m: the game draws no shadow beyond that "
                      "distance, so ground under a far cliff or tree looks lit until you are that close. Further costs "
                      "more and makes far shadows coarser. Plants and props cast their shadows over the same "
                      "distance, which is the larger part of the cost where there are many of them. Range 2-4")
    .range(2.0, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Last job of Mon 16 Nov (plants' shading still appearing on approach at 60 m): leaf-on-leaf shadows are smaller than
// the far cascade's bias and texels, so they only showed inside the near cascade (9 m at distance 3). With the preset
// on: the shadow maps at twice the size (the far bias follows the texel size by itself: x2 instead of x4) and the near
// cascade's end twice as far again (18 m at distance 3; its texels are then the size the shadow study toured at
// distance 3 with the stock maps).
// Overnight Mon 16 / Tue 17 Nov: both are chosen by the graphics card's dedicated video memory when left at 0
// (automatic): map scale 2 / split 2 (18 m at 60 m), or map scale 3 / split 3 (27 m) on a card with 12 GB or more
// (hand test at 4K: split 3 alone looked blockier, with map scale 3 much better; the shadow maps took about 3.4 GB at
// x3 against 1.5 GB at x2, 7.3 GB in all). A value of 1 or more is the player's own and is kept.
REXCVAR_DEFINE_INT32(pdz_long_shadows_map_scale, 0, "Render",
                     "With pdz_long_shadows on: the shadow maps' size, as pdz_shadow_map_scale (1 = the game's, 2 = "
                     "twice the side: finer far shadows and less bias, so leaves shade each other further away). "
                     "0 = automatic: 2, or 3 on a graphics card with 12 GB or more of video memory. Costs video "
                     "memory (the log's 'shadow map scale' line gives the figure) and some GPU time. Read once, when "
                     "the game starts. Range 0-4")
    .range(0, 4);
REXCVAR_DEFINE_DOUBLE(pdz_long_shadows_split, 0.0, "Render",
                      "With pdz_long_shadows on: how much further the sharp near shadow map reaches, on top of the "
                      "long shadows distance (1 = 3 m x the distance, 2 = twice that: 18 m at 60 m, 3 = 27 m). "
                      "Further = fine self-shadowing on plants from further away, but a coarser near map unless the "
                      "shadow maps are larger too. 0 = automatic: 2, or 3 on a graphics card with 12 GB or more of "
                      "video memory. Range 0-4")
    .range(0.0, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_long_shadows_object_fade, 1.0, "Render",
                      "With pdz_long_shadows on: the length of an object's own shadow fade in the far shadow map, as a "
                      "multiple of the game's (plants, props, characters: the shade right behind and under them). 1 = "
                      "the game's length, the same as in the near map. More makes far plants darker, but brings "
                      "back object shadows drawn inside a wall's baked shade (MP Desert) as it nears 4. Range 1-4")
    .range(1.0, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_long_shadows_skip_levels, "outpost", "Render",
                      "With pdz_long_shadows on: the single player levels that keep the game's own shadows (names as "
                      "pdz_sp_60_levels takes them, separated by commas). Outpost: its start area is over the 120 fps "
                      "frame time with the game's own shadows already, and the longer shadows draw about 30% more "
                      "there. Empty: long shadows on every level")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::pace
{
  std::string SpLevelName(const uint8_t *base, std::string *raw); // game_pace.cpp
}

namespace pdz::transwater
{
  void ShowStatus(const std::string &text, int seconds); // the on-screen label (transparency_water.cpp)
}

namespace pdz::sunshadow
{
  namespace
  {
    constexpr double kPresetFarPcf = 1.0; // texels
    // pdz_long_shadows_distance within 2-4 (3 when it is not a number).
    double PresetDistance()
    {
      const double v = REXCVAR_GET(pdz_long_shadows_distance);
      return v >= 2.0 && v <= 4.0 ? v : v > 4.0 ? 4.0 : v < 2.0 ? 2.0 : 3.0;
    }
    // The far bias factor stays x4 (16-123 cm) at any distance: texel size (x distance) x 4 x (1 / distance).
    double PresetBias(double distance) { return 1.0 / distance; }
    std::atomic<bool> g_preset_applied{false};
    std::atomic<double> g_preset_distance{0.0}; // the distance last applied (0: the preset is off)
    std::atomic<bool> g_preset_level_skipped{false};
    uint32_t g_preset_level_tick = 0; // game thread
    // The shadow mask shader is patched (far PCF, the object fade fix) only at its first use. 0: not prepared yet,
    // 1: prepared with the long shadows' patches, 2: prepared without them (the preset was off then): switching the
    // preset on now would bring back the object shadows inside baked shade, so it stays off until a restart.
    std::atomic<int> g_mask_shader_state{0};
    std::atomic<bool> g_preset_blocked_logged{false};

    // Is the level being played on pdz_long_shadows_skip_levels? Asked on the game thread, about twice a second.
    bool LevelSkipped()
    {
      const std::string list = REXCVAR_GET(pdz_long_shadows_skip_levels);
      if (list.empty())
      {
        return false;
      }
      const std::string level = pdz::pace::SpLevelName(rex::Runtime::instance()->virtual_membase(), nullptr);
      if (level.empty())
      {
        return false;
      }
      size_t at = 0;
      while (at <= list.size())
      {
        size_t end = list.find_first_of(", ;", at);
        if (end == std::string::npos)
        {
          end = list.size();
        }
        if (end > at && list.compare(at, end - at, level) == 0)
        {
          return true;
        }
        at = end + 1;
      }
      return false;
    }
    rex::ui::Window *g_window = nullptr;
  } // namespace

  // Called before the first shader patch decision (native_coissue.cpp) and at every shadow setup: cheap when nothing
  // changed.
  void ApplyPreset(bool game_thread)
  {
    const bool wanted = REXCVAR_GET(pdz_long_shadows);
    if (game_thread && wanted && (g_preset_level_tick++ % 64u) == 0)
    {
      const bool skip = LevelSkipped();
      if (g_preset_level_skipped.exchange(skip, std::memory_order_relaxed) != skip)
      {
        REXLOG_INFO("pdz_shadow: long shadows: this level is {} pdz_long_shadows_skip_levels: {}",
                    skip ? "on" : "not on", skip ? "the game's own shadows here" : "long shadows");
      }
    }
    const bool blocked = wanted && g_mask_shader_state.load(std::memory_order_relaxed) == 2;
    if (blocked && !g_preset_blocked_logged.exchange(true, std::memory_order_relaxed))
    {
      REXLOG_WARN("pdz_shadow: long shadows (pdz_long_shadows) switched on after the shadow shader was prepared "
                  "without them: not applied; start the game with pdz_long_shadows = true");
    }
    const bool on = wanted && !blocked && !g_preset_level_skipped.load(std::memory_order_relaxed);
    const double distance = on ? PresetDistance() : 0.0;
    const bool same_state = g_preset_applied.exchange(on, std::memory_order_relaxed) == on;
    if (g_preset_distance.exchange(distance, std::memory_order_relaxed) == distance && same_state)
    {
      return; // nothing changed (also a live change of pdz_long_shadows_distance is applied here)
    }
    REXCVAR_SET(pdz_shadow_distance_scale, on ? distance : 1.0);
    REXCVAR_SET(pdz_shadow_far_pcf, on ? kPresetFarPcf : 0.0);
    REXCVAR_SET(pdz_shadow_bias_scale, on ? PresetBias(distance) : 1.0);
    // The caster box follows the whole distance (rc item 4, Mon 16 Nov evening): plants and props are drawn into the
    // shadow maps only inside that box, and the automatic box stops at x2 (about 48 m, less to the sides), so at 60 m
    // a plant's own shadow and the shade right under it only appeared as the player walked closer. 0 = automatic
    // again when the preset is off (nothing reads it at the stock distance).
    REXCVAR_SET(pdz_shadow_caster_scale, on ? distance : 0.0);
    // The detail level: the player's own values where set (1 or more), else by the card's dedicated video memory.
    // 11.5 GiB and up counts as a 12 GB card (they report a little under 12 GiB); unknown (0) counts as small.
    const uint64_t vram = pdz::native::DedicatedVideoMemoryBytes();
    const bool large_card = vram >= (uint64_t(23) << 29); // 11.5 GiB
    const int32_t own_maps = REXCVAR_GET(pdz_long_shadows_map_scale);
    const double own_split = REXCVAR_GET(pdz_long_shadows_split), fade = REXCVAR_GET(pdz_long_shadows_object_fade);
    const int32_t maps = own_maps >= 1 ? std::min(own_maps, 4) : large_card ? 3 : 2;
    const double split = own_split >= 1.0 ? std::min(own_split, 4.0) : large_card ? 3.0 : 2.0;
    g_long_split_extra.store(on ? float(split) : 1.0f, std::memory_order_relaxed);
    g_long_object_fade.store(on && fade >= 1.0 ? float(std::min(fade, 4.0)) : 1.0f, std::memory_order_relaxed);
    // The shadow maps' size is latched by the first shadow surface, after this first call (made before the first
    // shader is prepared): only ever raised, and only while the preset is on at the start.
    if (on)
    {
      if (REXCVAR_GET(pdz_shadow_map_scale) < maps)
      {
        REXCVAR_SET(pdz_shadow_map_scale, maps);
      }
      REXLOG_INFO("pdz_shadow: long shadows detail: shadow map scale {} ({}), near split x{:g} = {:.0f} m ({}); "
                  "graphics card video memory {} (12 GB or more picks 3 / 3, otherwise 2 / 2)",
                  maps, own_maps >= 1 ? "set by hand: pdz_long_shadows_map_scale" : "automatic", split,
                  3.0 * distance * split, own_split >= 1.0 ? "set by hand: pdz_long_shadows_split" : "automatic",
                  vram ? fmt::format("{:.1f} GB", double(vram) / 1073741824.0) : std::string("unknown, taken as under 12 GB"));
    }
    REXLOG_INFO("pdz_shadow: long shadows {} (pdz_long_shadows): distance x{:g}, far PCF {:g}, bias scale {:g}",
                on ? "ON" : "OFF (the game's own)", REXCVAR_GET(pdz_shadow_distance_scale),
                REXCVAR_GET(pdz_shadow_far_pcf), REXCVAR_GET(pdz_shadow_bias_scale));
  }

  void NoteMaskShader(bool long_patches) { g_mask_shader_state.store(long_patches ? 1 : 2, std::memory_order_relaxed); }

  // F7 (bind_long_shadows): the preset on / off while playing, with the on-screen label and the window title.
  void Init(rex::ui::Window *window)
  {
    g_window = window;
    rex::ui::RegisterBind("bind_long_shadows", "F7", "Long shadows on / off (pdz_long_shadows), to compare", [] {
      static auto last = std::chrono::steady_clock::time_point{};
      const auto now = std::chrono::steady_clock::now();
      if (now - last < std::chrono::milliseconds(300))
      {
        return; // a held key repeats
      }
      last = now;
      bool on = !REXCVAR_GET(pdz_long_shadows);
      const bool blocked = on && g_mask_shader_state.load(std::memory_order_relaxed) == 2;
      if (blocked)
      {
        on = false;
      }
      REXCVAR_SET(pdz_long_shadows, on);
      ApplyPreset();
      const std::string text = blocked ? "Long shadows: start the game with them on to use F7"
                               : !on   ? "Long shadows: OFF (Rare's 20 m)"
                               : g_preset_level_skipped.load(std::memory_order_relaxed)
                                     ? "Long shadows: ON (not on this level: Rare's 20 m)"
                                     : fmt::format("Long shadows: ON ({:.0f} m)", 20.0 * PresetDistance());
      REXLOG_INFO("pdz_shadow: KEY PRESS: {}", text);
      pdz::transwater::ShowStatus(text, 3);
      if (g_window)
      {
        rex::ui::Window *w = g_window;
        const std::string title = std::string("PDZRestored ") + PDZ_RESTORED_VERSION + " | " + text;
        w->app_context().CallInUIThread([w, title] { w->SetTitle(title); });
      }
    });
  }
} // namespace pdz::sunshadow
