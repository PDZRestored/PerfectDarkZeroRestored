// Laboratory dark patches (pdz_lab_dark_patches, Gameplay, hot-reloadable; Laboratory only).
// Follow-up (Sun 15 Nov): ON by default as part of the stealth layer, and only while that layer runs
// (lab_stealth::LayerRuns: the master switch pdz_lab_guard_stealth, not Counter-Operative); pdz_lab_dark_shot_cm 0 and
// pdz_lab_dark_visual_strength 0 by default (Rare's own one lamp per shot, the screen as Rare draws it). The stealth
// zones of other levels use only the light measure (MeasureSwitch / Fraction), for the meter.
//
// 1. DARKNESS WHERE JO STANDS. About once a second the light reaching Jo is measured with Rare's own light falloff
//    (lab_light_level.cpp: linear from the inner to the outer radius, spot cones, colour luma, no occlusion, as the
//    light pre-pass draws it) at three heights (feet + 20, + 100, + 170 cm) over every scene light in Rare's list
//    (0x82E19AE0, count 0x82E3C5F4; lights a prop owns, +0x338, left out) with each light's current intensity
//    (+0x598), plus Rare's ambient term (luma of the ambient colour 0x82E09F60 + 0x10, 0.206 at level start, 0 after
//    the blackout). The fraction F = that / the same with every light on and the level-start ambient. F feeds Rare's
//    sight through the player's concealment (sub_825A93C0, called by the sight test sub_82189F18 at 8218A01C: Jo is
//    visible only within range x sqrt(concealment)): the range becomes near + (range - near) x F, near =
//    pdz_lab_dark_near_cm (1000: the blackout's 10 m), multiplied with Rare's own crouch 0.5. The stealth meter's
//    light factor (lab_stealth.cpp Exposure) becomes 0.3 + 0.7 F instead of the blackout's dark bit. Full light = the
//    meter of round 3; near-dark = guards see her only within about 10 m, and slowly.
// 2. SHOT LIGHTS. When a shot hits a light surface of the level (Rare's sub_825F3978 with no prop), Rare puts out the
//    one light its pick chooses; here every other eligible light within pdz_lab_dark_shot_cm (300) of the hit goes out
//    the same way (Rare's eligibility: type 0 / 2, +0x32C 0 the shoot-out flag, a scene light, state 1 / 2; Rare's
//    sparks sub_82612330 effect 13 and fade sub_82602648(light, 0, 0.01)): the Lab One door's lamp puts out 385 and 91.
// 3. A light going out near calm guards is an incident (lab_stealth.cpp LightIncident): one guard investigates.
// 4. LOCAL DARKNESS (pdz_lab_dark_visual, round 6). A lamp that was on and is now out (shot, the test tools, Rare's
//    blackout) takes most of the ambient light away within its reach: full effect inside its outer radius, fading out
//    to 1.5 x the radius; lamps that overlap darken together (the factors multiply, never below
//    1 - pdz_lab_dark_visual_strength, the part of the ambient left). ROUND 7: OFF BY DEFAULT (strength 0: the screen
//    is drawn as Rare draws it, and the light measure counts Rare's own lights and ambient only); the hand test's
//    verdict was that it looked like a fan addition (a black spot in a lit room, doors not dimmed, the darkening
//    snapping in as the 32 nearest lamps changed). The code and the switch stay for testing (0.9 = round 6).
//    DRAWN by the native renderer only
//    (render/native/native_labdark.cpp): Rare's ambient is one full-screen draw of the light pre-pass (the sun /
//    ambient pass sub_826043E0, pixel shader a95c4b61); right after it one more full-screen draw multiplies the light
//    buffer by the factor at each pixel's position (rebuilt from the depth texture that pass reads, with its own
//    constant c0). The hook on sub_826043E0 below hands over each view's lamps in Rare's view space (its world-to-view
//    matrix, r7), keyed by the shadow ring sequences of the draws the pass records, so the record thread draws a
//    view's lamps with that view's camera. Emissive surfaces and everything drawn forward (glass, particles, the HUD)
//    are untouched; the emulated path is unchanged.
//    COUNTED by the light measure: the ambient part of the light at Jo (and at a body) is scaled by the same factor.
// 5. THE ABSOLUTE MEASURE (pdz_lab_dark_absolute, round 6). F = the light at Jo / a fixed bright reference
//    (pdz_lab_dark_reference: the level's median over its lit AI pads, three samples) instead of / the light at the
//    same spot with every lamp on: the dim gaps between the lamp pools count as dim (about 0.24), a pool whose lamps
//    are out as dark. pdz_lab_dark_absolute off, pdz_lab_dark_visual off and pdz_lab_dark_near_cm 1000 = round 5.
// 6. RARE'S BLACKOUT (round 7). While Rare's generator blackout is on (its ambient is black: 0 of the level-start
//    value) nothing of ours is laid on top: the range scale is 1 and the stealth meter uses Rare's own dark bit again
//    (the guards Rare marks "affected by darkness": 10 m sight, slower aim). Log 048 07:01: Rare's darkness and this
//    reading stacked, and the light at Jo read 0.00 everywhere.
// Research: the light notes A1-A4 (light data, falloff, light at Jo, the Lab One door) and the shot-lights note.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "lab_dark_patches.h"
#include "lab_light_level.h"
#include "lab_restorations.h"
#include "lab_stealth.h"
#include "render/native/native.h" // pdz::native::Enabled (the native renderer draws)
#include "render/shadow_record.h" // the shadow ring's head: which draws an ambient pass recorded

REXCVAR_DEFINE_BOOL(pdz_lab_dark_patches, true, "Gameplay",
                    "Part of the stealth layer (it only acts while the layer runs: pdz_lab_guard_stealth on, not a "
                    "Counter-Operative game). Darkness hides Jo: the light reaching where she stands (Rare's own light "
                    "falloff, every lamp's current brightness and the ambient light) feeds the layer's meter on its "
                    "four levels; on the Laboratory it also shortens the guards' sight of her (full light as before, "
                    "near-dark only within pdz_lab_dark_near_cm; crouching halves it again), and a lamp going out near "
                    "calm guards makes one of them come and look. Nothing is drawn differently: the screen is Rare's "
                    "(pdz_lab_dark_visual_strength 0). Off = the meter without its light term, Rare's sight range")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_dark_log, false, "Diagnostics",
                    "pdz_lab_dark_patches: log the light at Jo once a second, the lamps a shot puts out, the incidents")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_dark_near_cm, 600.0, "Gameplay",
                      "pdz_lab_dark_patches: in complete darkness guards see Jo only within this distance (cm; 1000 = "
                      "the blackout's 10 m, the value before round 6, which left the 10 m hall guards' range "
                      "unchanged); in between it scales with the light")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_dark_visual, true, "Gameplay",
                    "pdz_lab_dark_patches, native renderer only: a lamp that is out (shot, or switched off after it "
                    "was on) also takes most of the ambient light away within its reach (its radius, fading out to "
                    "1.5 x the radius), on screen and in the light measured at Jo; lamps that overlap darken together. "
                    "Emissive surfaces and the emulated renderer are unchanged. Off = only the lamp's own light goes")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_dark_visual_strength, 0.0, "Gameplay",
                      "pdz_lab_dark_visual (for testing): how much of the ambient light a lamp that is out takes away "
                      "within its reach. 0 = nothing: the screen is drawn as Rare draws it and the guards' light "
                      "reading counts Rare's own lights only (the default). 0.9 = round 6's look; 1 = all of it")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_lab_dark_absolute, true, "Gameplay",
                    "pdz_lab_dark_patches: the light at Jo is measured against a fixed bright reference "
                    "(pdz_lab_dark_reference) instead of against the same spot with every lamp on, so the dim gaps "
                    "between the lamp pools count as dim and a pool whose lamps are out as dark. Off = the relative "
                    "measure (before round 6)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_dark_reference, 2.6, "Gameplay",
                      "pdz_lab_dark_absolute: the light that counts as fully lit (the sum of the three samples at "
                      "Jo; 2.6 = the median over the Laboratory's lit AI pads; the ambient alone is 0.62)")
    .range(0.1, 100.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_dark_shot_cm, 0.0, "Gameplay",
                      "pdz_lab_dark_patches: a shot lamp also puts out every other lamp within this distance (cm) of "
                      "the hit. 0 (the default) = only the lamp Rare's own pick puts out; 300 = rounds 5 to 11")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_lab_dark_out, "", "Testing",
                      "TESTING TOOL: with pdz_lab_dark_patches, \"x,y,z,r\": every eligible lamp within r cm of the "
                      "point goes out as a shot would put it out (Rare's sparks and fade), and the guards' incident "
                      "follows. Once per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_test_lab_dark_map, "", "Testing",
                      "TESTING TOOL: with pdz_lab_dark_patches in the Laboratory, a new value logs a light map (a "
                      "report only): every lamp, then every AI pad linked to Jo's pad with its light with every lamp "
                      "on, the part no shot can put out, and the shootable lamps that reach it (pdz_lab_dark_map lines)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82612330); // effect at a point (r3 &pos, r4 1, r5 effect 13 electrical, r6 0, r7 &up 0x82073C60)
REX_EXTERN(sub_82602648); // light on / off (r3 light, r4 0 off / 1 on, f1 fade seconds)
REX_EXTERN(__imp__sub_826043E0); // the light pre-pass's sun / ambient pass (r5 light, r7 the world-to-view matrix)

namespace
{
  constexpr uint32_t kLightArray = 0x82E19AE0, kLightCount = 0x82E3C5F4, kMaxLights = 1024;
  constexpr uint32_t kStateIndex = 0x82E1BA20; // 0 / 1: which of the two state words is current
  constexpr uint32_t kAmbient = 0x82E09F60;    // +0x10: the ambient colour (vec4)
  constexpr uint32_t kAiClock = 0x82E2F334;
  constexpr uint32_t kUp = 0x82073C60;         // (0, 1, 0, 0), Rare's own argument for the sparks
  constexpr uint32_t kPosition = 48;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a)
  {
    const uint32_t v = Load32(base, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  bool Ptr(uint32_t a)
  {
    return a >= 0x10000u && a < 0xFFFF0000u;
  }
  bool Logging()
  {
    return REXCVAR_GET(pdz_lab_dark_log);
  }

  std::mutex g_lock;
  uint32_t g_script = 0;
  float g_sample_at = -1e9f;
  float g_fraction = 1.0f;
  float g_lit = 0.0f, g_full = 0.0f, g_amb_now = 0.0f, g_amb_full = -1.0f;
  float g_logged_fraction = -1.0f;
  float g_logged_at = -1e9f;
  std::string g_test_done;

  bool Lab()
  {
    // Follow-up (Sun 15 Nov): on by default now, so only while the stealth layer runs (its master switch off, or a
    // Counter-Operative game: nothing of this file acts and the Laboratory is Rare's).
    return REXCVAR_GET(pdz_lab_dark_patches) && lab_restorations::CurrentScript() != 0 && lab_stealth::LayerRuns();
  }

  // Shoot-out eligibility (Rare's pick sub_825F3CD8 and its on test 825F39A8): a scene light of type 0 / 2 with the
  // shoot-out flag (+0x32C 0), on or fading in.
  bool Eligible(const uint8_t *base, uint32_t light, uint32_t index)
  {
    const uint32_t type = Load32(base, light + 0x2A8);
    if ((type != 0 && type != 2) || Load32(base, light + 0x32C) != 0 || Load32(base, light + 0x338) != 0)
    {
      return false;
    }
    const uint32_t state = Load32(base, light + 0x590 + 4 * (index & 1));
    return state == 1 || state == 2;
  }

  // ----- local darkness (pdz_lab_dark_visual) ---------------------------------------------------------------------
  // A lamp counts as out when it was seen on in this run of the level (a scene light of type 0 / 2 no prop owns,
  // state 1 / 2 with more than half its brightness) and its state is now neither on nor fading in; its weight is
  // 1 - its brightness (+0x598), so the darkness comes with Rare's fade. Lamps that were never on leave the level as
  // Rare lit it. The bytes are written by the game thread's sample and the render thread's ambient hook (the same
  // value from either; a lost write is made again a frame later).
  uint8_t g_seen_on[kMaxLights] = {};
  std::atomic<uint32_t> g_seen_script{0};
  struct OutLamp
  {
    float pos[3];
    float radius;
    float weight;
  };

  // The part of the ambient left inside a lamp that is out: 1 - pdz_lab_dark_visual_strength (round 7: the switch
  // says how strong the darkening is; 0 = off).
  std::atomic<bool> g_blackout{false}; // Rare's blackout is on (the ambient is black): nothing of ours on top
  float VisualStrengthNow()
  {
    return 1.0f - std::clamp(float(REXCVAR_GET(pdz_lab_dark_visual_strength)), 0.0f, 1.0f);
  }

  // The Laboratory's lights are the ones loaded: its script ticks (lab_restorations::CurrentScript), or it ticked
  // before and the game stands still since (a pause: the script's tick stops, the scene is still drawn): the same
  // light list (its first light) and the AI clock within a second of the last tick's. Another light list forgets
  // which lamps were seen on.
  std::atomic<uint32_t> g_live_count{0}, g_live_first{0}, g_live_clock{0};
  bool LevelLive(const uint8_t *base)
  {
    const uint32_t script = lab_restorations::CurrentScript();
    const uint32_t count = Load32(base, kLightCount), first = Load32(base, kLightArray);
    if (script != 0)
    {
      // (Not the script pointer, and not the count: effects add and remove lights of their own at the list's end,
      // e.g. the sparks of a lamp going out.)
      if (first != g_live_first.load(std::memory_order_relaxed))
      {
        g_seen_script.store(script, std::memory_order_relaxed);
        g_live_count.store(count, std::memory_order_relaxed);
        g_live_first.store(first, std::memory_order_relaxed);
        std::memset(g_seen_on, 0, sizeof(g_seen_on));
      }
      g_live_clock.store(Load32(base, kAiClock), std::memory_order_relaxed);
      return true;
    }
    if (g_seen_script.load(std::memory_order_relaxed) == 0 || count == 0 ||
        first != g_live_first.load(std::memory_order_relaxed))
    {
      return false;
    }
    const uint32_t bits = g_live_clock.load(std::memory_order_relaxed);
    float then;
    std::memcpy(&then, &bits, 4);
    return std::fabs(LoadF(base, kAiClock) - then) < 1.0f;
  }

  bool VisualWanted(const uint8_t *base)
  {
    return REXCVAR_GET(pdz_lab_dark_patches) && REXCVAR_GET(pdz_lab_dark_visual) &&
           REXCVAR_GET(pdz_lab_dark_visual_strength) > 0.0 && pdz::native::Enabled() && LevelLive(base);
  }

  // Every lamp that is out, in world space (at most `max`). Any thread; after VisualWanted (LevelLive) said yes.
  int CollectOut(const uint8_t *base, OutLamp *out, int max)
  {
    const uint32_t index = Load32(base, kStateIndex) & 1;
    const uint32_t count = std::min(Load32(base, kLightCount), kMaxLights);
    int n = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t a = Load32(base, kLightArray + 4 * i);
      if (!Ptr(a))
      {
        continue;
      }
      const uint32_t state = Load32(base, a + 0x590 + 4 * index);
      if (state == 1 || state == 2)
      {
        if (!g_seen_on[i])
        {
          const uint32_t type = Load32(base, a + 0x2A8);
          if ((type == 0 || type == 2) && Load32(base, a + 0x338) == 0 && LoadF(base, a + 0x598) > 0.5f)
          {
            g_seen_on[i] = 1;
          }
        }
        continue;
      }
      if (!g_seen_on[i] || n >= max)
      {
        continue;
      }
      const float weight = 1.0f - std::clamp(LoadF(base, a + 0x598), 0.0f, 1.0f);
      const float radius = LoadF(base, a + 0x2AC);
      if (!(weight > 0.004f) || !std::isfinite(radius) || !(radius > 1.0f))
      {
        continue;
      }
      OutLamp &o = out[n++];
      o.pos[0] = LoadF(base, a + 0x2D0);
      o.pos[1] = LoadF(base, a + 0x2D4);
      o.pos[2] = LoadF(base, a + 0x2D8);
      o.radius = radius;
      o.weight = weight;
    }
    return n;
  }

  // The ambient kept at p: the product over the lamps that are out of lerp(1, strength, weight x reach), reach 1
  // inside the lamp's radius and smoothstep down to 0 at kVisualFalloff x the radius; never below strength. The
  // native pass's pixel shader computes the same (native_labdark.cpp).
  float KeepAt(const OutLamp *lamps, int n, float strength, const float p[3])
  {
    float keep = 1.0f;
    for (int i = 0; i < n; ++i)
    {
      const OutLamp &o = lamps[i];
      const float dx = p[0] - o.pos[0], dy = p[1] - o.pos[1], dz = p[2] - o.pos[2];
      const float d2 = dx * dx + dy * dy + dz * dz;
      const float end = o.radius * lab_dark::kVisualFalloff;
      if (d2 >= end * end)
      {
        continue;
      }
      const float t = std::clamp((std::sqrt(d2) - o.radius) / (end - o.radius), 0.0f, 1.0f);
      const float reach = 1.0f - t * t * (3.0f - 2.0f * t);
      keep *= 1.0f + (strength - 1.0f) * (o.weight * reach);
    }
    return std::max(keep, strength);
  }

  // The views' lamps handed to the native renderer: one entry per call of Rare's ambient pass, valid for the shadow
  // ring sequences [first, end) its draws got. Written by the render thread's hook, read by the record thread at the
  // ambient pass's draw (once a view): an uncontended lock on both sides.
  struct FrameSlot
  {
    uint32_t first = 0, end = 0;
    lab_dark::VisualFrame frame;
  };
  constexpr uint32_t kFrameSlots = 8;
  std::mutex g_frame_lock;
  FrameSlot g_frames[kFrameSlots];
  uint32_t g_frame_next = 0;
  std::atomic<bool> g_visual_armed{false};
  int64_t g_visual_logged_ms = 0;

  uint32_t g_zone_first = 0; // (Nightclub helper) the light list a zone level's measure ran on
  // Mansion helper (Sun 15 Nov): a zone level's extra light at Jo (the moon where a ray to it is clear: the sum over
  // the three samples) and its own reference (0 = pdz_lab_dark_reference). Set by the stealth layer before Fraction;
  // never on the Laboratory (its script ticks).
  float g_zone_extra = 0.0f, g_zone_reference = 0.0f;
  float g_zone_extra_used = 0.0f, g_zone_reference_used = 0.0f;
  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start (lab_dark::LevelReset, from the stealth layer's clear):
  // the measure's memory of the attempt before (the level-start ambient, which lamps were seen on, the blackout
  // flag, the last sample) is dropped at the next sample. A mission restart can keep the script pointer and the
  // light list where they were, so neither test below sees it. The measure only: nothing here draws.
  std::atomic<bool> g_level_reset{false};
  void Sample(const uint8_t *base, uint32_t jo, float now)
  {
    if (g_level_reset.exchange(false, std::memory_order_relaxed))
    {
      g_amb_full = -1.0f;
      g_sample_at = -1e9f;
      g_logged_fraction = -1.0f;
      g_logged_at = -1e9f;
      g_blackout.store(false, std::memory_order_relaxed);
      std::memset(g_seen_on, 0, sizeof(g_seen_on)); // (as LevelLive does for another light list)
    }
    const uint32_t script = lab_restorations::CurrentScript();
    if (script != g_script)
    {
      g_script = script;
      g_amb_full = -1.0f;
      g_sample_at = -1e9f;
    }
    if (script == 0)
    {
      // Nightclub helper: a zone level (no Laboratory script) measures for the meter only; another light list (another
      // level, or a restart) forgets the level-start ambient, so one level's ambient never counts on the next.
      const uint32_t first = Load32(base, kLightArray);
      if (first != g_zone_first)
      {
        g_zone_first = first;
        g_amb_full = -1.0f;
      }
    }
    const float amb[3] = {LoadF(base, kAmbient + 0x10), LoadF(base, kAmbient + 0x14), LoadF(base, kAmbient + 0x18)};
    g_amb_now = std::max(0.0f, lab_light_level::Luma(amb));
    if (g_amb_full < 0.0f || g_amb_now > g_amb_full)
    {
      g_amb_full = g_amb_now; // the level-start ambient (the blackout only ever lowers it)
    }
    const float x = LoadF(base, jo + kPosition), y = LoadF(base, jo + kPosition + 4), z = LoadF(base, jo + kPosition + 8);
    const float pts[3][3] = {{x, y + 20.0f, z}, {x, y + 100.0f, z}, {x, y + 170.0f, z}};
    float lit = 3.0f * g_amb_now, full = 3.0f * g_amb_full;
    // Local darkness: the ambient part follows what the native renderer draws at each sample.
    float keep_mean = 1.0f;
    int out_lamps = 0;
    if (VisualWanted(base))
    {
      static OutLamp out[kMaxLights]; // under g_lock
      out_lamps = CollectOut(base, out, int(kMaxLights));
      if (out_lamps > 0)
      {
        const float strength = VisualStrengthNow();
        float sum = 0.0f;
        for (const auto &p : pts)
        {
          sum += KeepAt(out, out_lamps, strength, p);
        }
        keep_mean = sum / 3.0f;
        lit = g_amb_now * sum;
      }
    }
    const uint32_t count = std::min(Load32(base, kLightCount), kMaxLights);
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t a = Load32(base, kLightArray + 4 * i);
      if (!Ptr(a) || Load32(base, a + 0x338) != 0)
      {
        continue;
      }
      lab_light_level::Light L;
      const uint32_t type = Load32(base, a + 0x2A8);
      if (type != 0 && type != 2)
      {
        continue;
      }
      L.type = uint8_t(type);
      L.outer = LoadF(base, a + 0x2AC);
      L.inner = LoadF(base, a + 0x2B0);
      L.cos_inner = LoadF(base, a + 0x2B4);
      L.cos_outer = LoadF(base, a + 0x2B8);
      for (int k = 0; k < 3; ++k)
      {
        L.colour[k] = LoadF(base, a + 0x2C0 + 4 * k);
        L.pos[k] = LoadF(base, a + 0x2D0 + 4 * k);
        L.dir[k] = -LoadF(base, a + 0x2E4 + 4 * k);
      }
      if (!std::isfinite(L.outer) || L.outer <= 0.0f)
      {
        continue;
      }
      const float intensity = std::clamp(LoadF(base, a + 0x598), 0.0f, 1.0f);
      for (const auto &p : pts)
      {
        const float c = lab_light_level::Contribution(L, p);
        if (c > 0.0f && std::isfinite(c))
        {
          full += c;
          lit += c * intensity;
        }
      }
    }
    const bool zone_terms = script == 0; // (Mansion helper: a zone level's moon and reference; never the Laboratory)
    if (zone_terms && g_zone_extra > 0.0f && std::isfinite(g_zone_extra))
    {
      lit += g_zone_extra;
      full += g_zone_extra;
    }
    g_zone_extra_used = zone_terms ? g_zone_extra : 0.0f;
    g_lit = lit;
    g_full = full;
    const bool absolute = REXCVAR_GET(pdz_lab_dark_absolute);
    if (absolute)
    {
      // Against the fixed bright reference (three samples): the same light reads the same wherever Jo stands.
      const float reference = std::max(0.1f, zone_terms && g_zone_reference > 0.0f
                                                 ? g_zone_reference
                                                 : float(REXCVAR_GET(pdz_lab_dark_reference)));
      g_zone_reference_used = reference;
      g_fraction = std::clamp(lit / reference, 0.0f, 1.0f);
    }
    else
    {
      g_fraction = full > 1e-6f ? std::clamp(lit / full, 0.0f, 1.0f) : 1.0f;
    }
    g_sample_at = now;
    // Round 7: Rare's blackout (its ambient black): Rare's own darkness only.
    const bool blackout = g_amb_full > 0.01f && g_amb_now < 0.02f * g_amb_full;
    if (blackout != g_blackout.load(std::memory_order_relaxed))
    {
      g_blackout.store(blackout, std::memory_order_relaxed);
      REXLOG_INFO("pdz_lab_dark: Rare's blackout {} (ambient {:.3f} of {:.3f}): {}", blackout ? "on" : "over", g_amb_now,
                  g_amb_full,
                  blackout ? "Rare's own darkness only from now on (its darkened guards: shorter sight, slower aim); "
                             "the light reading is not laid on top"
                           : "the light reading applies again");
    }
    if (Logging() && (std::fabs(g_fraction - g_logged_fraction) >= 0.05f || now - g_logged_at >= 5.0f || now < g_logged_at))
    {
      g_logged_fraction = g_fraction;
      g_logged_at = now;
      if (blackout)
      {
        REXLOG_INFO("pdz_lab_dark: Jo at ({:.0f} {:.0f} {:.0f}): Rare's blackout: light reading {:.2f} NOT applied (range "
                    "scale 1: Rare's darkness only)",
                    x, y, z, g_fraction);
      }
      REXLOG_INFO("pdz_lab_dark: Jo at ({:.0f} {:.0f} {:.0f}): light {:.2f} (lit {:.2f} / full {:.2f}, ambient {:.3f} of "
                  "{:.3f}): guards see her within {:.0f}% of their range; {} measure, ambient kept {:.2f} ({} lamp(s) "
                  "out), 40 m guard {:.1f} m, 10 m guard {:.1f} m",
                  x, y, z, g_fraction, lit, full, g_amb_now, g_amb_full, 100.0f * lab_dark::RangeScale(4000.0f),
                  absolute ? "absolute" : "relative", keep_mean, out_lamps, 40.0f * lab_dark::RangeScale(4000.0f),
                  10.0f * lab_dark::RangeScale(1000.0f));
      int seen = 0;
      for (const uint8_t b : g_seen_on)
      {
        seen += b;
      }
      REXLOG_INFO("pdz_lab_dark: local darkness {}: {} lamp(s) seen on, {} out now, light list {} at {:08X}",
                  VisualWanted(base) ? "on" : "off", seen, out_lamps, Load32(base, kLightCount), Load32(base, kLightArray));
    }
  }

  // Every eligible lamp within r of p goes out as Rare's shot path puts its pick out. Returns how many.
  int PutOut(PPCContext &ctx, uint8_t *base, const float p[3], float r, std::string &list)
  {
    const uint32_t index = Load32(base, kStateIndex);
    const uint32_t count = std::min(Load32(base, kLightCount), kMaxLights);
    int n = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t a = Load32(base, kLightArray + 4 * i);
      if (!Ptr(a) || !Eligible(base, a, index))
      {
        continue;
      }
      const float dx = LoadF(base, a + 0x2D0) - p[0], dy = LoadF(base, a + 0x2D4) - p[1], dz = LoadF(base, a + 0x2D8) - p[2];
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (!(d <= r))
      {
        continue;
      }
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = a + 0x2D0;
        frame.ctx.r4.u64 = 1;
        frame.ctx.r5.u64 = 13;
        frame.ctx.r6.u64 = 0;
        frame.ctx.r7.u64 = kUp;
        sub_82612330(frame.ctx, base);
      }
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = a;
        frame.ctx.r4.u64 = 0;
        frame.ctx.f1.f64 = 0.01;
        sub_82602648(frame.ctx, base);
      }
      ++n;
      list += fmt::format(" #{} ({:.0f} cm)", i, d);
    }
    return n;
  }

  // pdz_test_lab_dark_map (testing, round 4 darkness report): the light map over the AI pads linked to Jo's pad.
  // Per pad (feet = the pad's position): the three samples' full light (every lamp on, the level-start ambient), the
  // part no shot can put out (ambient + lamps that are not shootable), and every shootable lamp reaching it (index:
  // contribution). The lamps' own lines come first (index, position, shootable now).
  std::string g_map_done;
  void DarkMap(const uint8_t *base, uint32_t jo, const std::string &tag)
  {
    struct Lamp
    {
      lab_light_level::Light L;
      uint32_t index = 0;
      bool shootable = false;
    };
    std::vector<Lamp> lamps;
    const uint32_t state = Load32(base, kStateIndex);
    const uint32_t count = std::min(Load32(base, kLightCount), kMaxLights);
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t a = Load32(base, kLightArray + 4 * i);
      if (!Ptr(a) || Load32(base, a + 0x338) != 0)
      {
        continue;
      }
      const uint32_t type = Load32(base, a + 0x2A8);
      if (type != 0 && type != 2)
      {
        continue;
      }
      Lamp m;
      m.index = i;
      m.L.type = uint8_t(type);
      m.L.outer = LoadF(base, a + 0x2AC);
      m.L.inner = LoadF(base, a + 0x2B0);
      m.L.cos_inner = LoadF(base, a + 0x2B4);
      m.L.cos_outer = LoadF(base, a + 0x2B8);
      for (int k = 0; k < 3; ++k)
      {
        m.L.colour[k] = LoadF(base, a + 0x2C0 + 4 * k);
        m.L.pos[k] = LoadF(base, a + 0x2D0 + 4 * k);
        m.L.dir[k] = -LoadF(base, a + 0x2E4 + 4 * k);
      }
      if (!std::isfinite(m.L.outer) || m.L.outer <= 0.0f)
      {
        continue;
      }
      m.shootable = Eligible(base, a, state);
      lamps.push_back(m);
      REXLOG_INFO("pdz_lab_dark_map[{}]: lamp #{} ({:.0f} {:.0f} {:.0f}) outer {:.0f} type {} shootable {}", tag, i, m.L.pos[0],
                  m.L.pos[1], m.L.pos[2], m.L.outer, type, m.shootable ? 1 : 0);
    }
    const uint32_t start = Load32(base, jo + 0xB4);
    std::vector<uint32_t> pads;
    if (Ptr(start))
    {
      pads.push_back(start);
    }
    for (size_t i = 0; i < pads.size() && pads.size() < 6000; ++i)
    {
      uint32_t link = Load32(base, pads[i] + 8);
      for (int n = 0; Ptr(link) && n < 32; ++n, link = Load32(base, link + 4))
      {
        const uint32_t to = Load32(base, link);
        if (Ptr(to) && std::find(pads.begin(), pads.end(), to) == pads.end())
        {
          pads.push_back(to);
        }
      }
    }
    const float amb = std::max(0.0f, g_amb_full);
    for (const uint32_t pad : pads)
    {
      const float x = LoadF(base, pad + 16), y = LoadF(base, pad + 20), z = LoadF(base, pad + 24);
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::fabs(x) > 1e6f)
      {
        continue;
      }
      const float pts[3][3] = {{x, y + 20.0f, z}, {x, y + 100.0f, z}, {x, y + 170.0f, z}};
      float full = 3.0f * amb, fixed = 3.0f * amb;
      std::vector<std::pair<float, uint32_t>> parts;
      for (const Lamp &m : lamps)
      {
        float c = 0.0f;
        for (const auto &p : pts)
        {
          const float v = lab_light_level::Contribution(m.L, p);
          c += (v > 0.0f && std::isfinite(v)) ? v : 0.0f;
        }
        if (c <= 0.0f)
        {
          continue;
        }
        full += c;
        if (m.shootable)
        {
          parts.push_back({c, m.index});
        }
        else
        {
          fixed += c;
        }
      }
      std::sort(parts.rbegin(), parts.rend());
      std::string list;
      for (size_t k = 0; k < parts.size() && k < 16; ++k)
      {
        list += fmt::format(" {}:{:.3f}", parts[k].second, parts[k].first);
      }
      REXLOG_INFO("pdz_lab_dark_map[{}]: pad {:08X} ({:.0f} {:.0f} {:.0f}) flags {:08X} full {:.3f} fixed {:.3f} floor {:.2f} "
                  "lamps{}",
                  tag, pad, x, y, z, Load32(base, pad + 0x34), full, fixed, full > 1e-6f ? fixed / full : 1.0f,
                  list.empty() ? std::string(" none") : list);
    }
    REXLOG_INFO("pdz_lab_dark_map[{}]: {} lamps, {} pads linked to Jo's pad {:08X}, ambient {:.3f} per sample", tag,
                lamps.size(), pads.size(), start, amb);
    REXLOG_INFO("pdz_lab_dark_map[{}]: measure: absolute {} reference {:.3f} near {:.0f} cm; visual {} strength {:.2f} "
                "falloff {:.2f} x the radius",
                tag, REXCVAR_GET(pdz_lab_dark_absolute) ? 1 : 0, float(REXCVAR_GET(pdz_lab_dark_reference)),
                float(REXCVAR_GET(pdz_lab_dark_near_cm)), VisualWanted(base) ? 1 : 0, VisualStrengthNow(),
                lab_dark::kVisualFalloff);
  }
} // namespace

namespace lab_dark
{
  bool On()
  {
    return Lab();
  }

  bool MeasureSwitch()
  {
    return REXCVAR_GET(pdz_lab_dark_patches);
  }

  void SetZoneLight(float extra, float reference)
  {
    std::lock_guard<std::mutex> hold(g_lock);
    g_zone_extra = extra;
    g_zone_reference = reference;
  }

  bool LastReading(float &lit, float &ambient, float &extra, float &reference, float &fraction)
  {
    std::lock_guard<std::mutex> hold(g_lock);
    lit = g_lit;
    ambient = g_amb_now;
    extra = g_zone_extra_used;
    reference = g_zone_reference_used;
    fraction = g_fraction;
    return g_sample_at > -1e8f;
  }

  float Fraction(const uint8_t *base, float now)
  {
    std::lock_guard<std::mutex> hold(g_lock);
    const uint32_t jo = Load32(base, 0x82E3C3E4);
    if (Ptr(jo) && (now - g_sample_at >= 1.0f || now < g_sample_at ||
                    g_level_reset.load(std::memory_order_relaxed))) // (stealth round 3c: a level start: at once)
    {
      Sample(base, jo, now);
    }
    return g_fraction;
  }

  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start. A flag only (any thread, no lock): the next sample
  // drops the measure's memory of the attempt before and measures at once.
  void LevelReset()
  {
    g_level_reset.store(true, std::memory_order_relaxed);
  }

  // Round 7 (item 4): the reach (outer radius, cm) of the scene lamp at `pos` (the largest within 350 cm of it; 0: none).
  float LampReach(const uint8_t *base, const float pos[3])
  {
    float reach = 0.0f;
    const uint32_t count = std::min(Load32(base, kLightCount), kMaxLights);
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t a = Load32(base, kLightArray + 4 * i);
      if (!Ptr(a) || Load32(base, a + 0x338) != 0)
      {
        continue;
      }
      const uint32_t type = Load32(base, a + 0x2A8);
      const float outer = LoadF(base, a + 0x2AC);
      if ((type != 0 && type != 2) || !std::isfinite(outer) || outer <= 0.0f)
      {
        continue;
      }
      const float dx = LoadF(base, a + 0x2D0) - pos[0], dy = LoadF(base, a + 0x2D4) - pos[1], dz = LoadF(base, a + 0x2D8) - pos[2];
      if (dx * dx + dy * dy + dz * dz <= 350.0f * 350.0f)
      {
        reach = std::max(reach, outer);
      }
    }
    return reach;
  }

  // Round 8: the lights changed all at once (the power return, a second blackout): measure again at the next call.
  void Resample()
  {
    std::lock_guard<std::mutex> hold(g_lock);
    g_sample_at = -1e9f;
  }

  bool RareBlackout()
  {
    return g_blackout.load(std::memory_order_relaxed);
  }

  float RangeScale(float range)
  {
    const float near_cm = std::max(0.0f, float(REXCVAR_GET(pdz_lab_dark_near_cm)));
    if (!(range > near_cm) || !(range > 0.0f) || g_blackout.load(std::memory_order_relaxed))
    {
      return 1.0f;
    }
    return (near_cm + (range - near_cm) * g_fraction) / range;
  }

  float MeterFactor()
  {
    return 0.3f + 0.7f * g_fraction;
  }

  // Round 5: the same measure at any point (a body on the floor): the light there now / with every lamp on, and the
  // range scale it gives (as RangeScale does for Jo).
  float RangeScaleAt(const uint8_t *base, const float pos[3], float range)
  {
    std::lock_guard<std::mutex> hold(g_lock);
    const float near_cm = std::max(0.0f, float(REXCVAR_GET(pdz_lab_dark_near_cm)));
    if (!(range > near_cm) || !(range > 0.0f) || g_amb_full < 0.0f || g_blackout.load(std::memory_order_relaxed))
    {
      return 1.0f;
    }
    const float pts[2][3] = {{pos[0], pos[1] + 20.0f, pos[2]}, {pos[0], pos[1] + 100.0f, pos[2]}};
    float lit = 2.0f * g_amb_now, full = 2.0f * g_amb_full;
    if (VisualWanted(base))
    {
      static OutLamp out[kMaxLights]; // under g_lock
      const int out_lamps = CollectOut(base, out, int(kMaxLights));
      if (out_lamps > 0)
      {
        const float strength = VisualStrengthNow();
        lit = g_amb_now * (KeepAt(out, out_lamps, strength, pts[0]) + KeepAt(out, out_lamps, strength, pts[1]));
      }
    }
    const uint32_t count = std::min(Load32(base, kLightCount), kMaxLights);
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t a = Load32(base, kLightArray + 4 * i);
      if (!Ptr(a) || Load32(base, a + 0x338) != 0)
      {
        continue;
      }
      lab_light_level::Light L;
      const uint32_t type = Load32(base, a + 0x2A8);
      if (type != 0 && type != 2)
      {
        continue;
      }
      L.type = uint8_t(type);
      L.outer = LoadF(base, a + 0x2AC);
      L.inner = LoadF(base, a + 0x2B0);
      L.cos_inner = LoadF(base, a + 0x2B4);
      L.cos_outer = LoadF(base, a + 0x2B8);
      for (int k = 0; k < 3; ++k)
      {
        L.colour[k] = LoadF(base, a + 0x2C0 + 4 * k);
        L.pos[k] = LoadF(base, a + 0x2D0 + 4 * k);
        L.dir[k] = -LoadF(base, a + 0x2E4 + 4 * k);
      }
      if (!std::isfinite(L.outer) || L.outer <= 0.0f)
      {
        continue;
      }
      const float intensity = std::clamp(LoadF(base, a + 0x598), 0.0f, 1.0f);
      for (const auto &p : pts)
      {
        const float c = lab_light_level::Contribution(L, p);
        if (c > 0.0f && std::isfinite(c))
        {
          full += c;
          lit += c * intensity;
        }
      }
    }
    float fraction = full > 1e-6f ? std::clamp(lit / full, 0.0f, 1.0f) : 1.0f;
    if (REXCVAR_GET(pdz_lab_dark_absolute))
    {
      // Two samples here against the reference's three.
      const float reference = std::max(0.1f, float(REXCVAR_GET(pdz_lab_dark_reference))) * (2.0f / 3.0f);
      fraction = std::clamp(lit / reference, 0.0f, 1.0f);
    }
    return (near_cm + (range - near_cm) * fraction) / range;
  }

  void Tick(PPCContext &ctx, uint8_t *base, float now)
  {
    if (!Lab())
    {
      return;
    }
    Fraction(base, now);
    {
      const std::string map = REXCVAR_GET(pdz_test_lab_dark_map);
      if (map != g_map_done)
      {
        g_map_done = map;
        const uint32_t jo = Load32(base, 0x82E3C3E4);
        if (!map.empty() && Ptr(jo))
        {
          std::lock_guard<std::mutex> hold(g_lock);
          DarkMap(base, jo, map);
        }
      }
    }
    const std::string want = REXCVAR_GET(pdz_test_lab_dark_out);
    if (want == g_test_done)
    {
      return;
    }
    g_test_done = want;
    float p[4] = {};
    if (std::sscanf(want.c_str(), "%f,%f,%f,%f", &p[0], &p[1], &p[2], &p[3]) != 4)
    {
      return;
    }
    std::string list;
    const int n = PutOut(ctx, base, p, p[3], list);
    REXLOG_INFO("pdz_lab_dark: test: {} lamp(s) within {:.0f} cm of ({:.0f} {:.0f} {:.0f}) put out:{}", n, p[3], p[0], p[1],
                p[2], list.empty() ? std::string(" none") : list);
    if (n > 0)
    {
      lab_stealth::LightIncident(ctx, base, p, n);
    }
    g_sample_at = -1e9f; // measure again now
  }

  void AfterShot(PPCContext &ctx, uint8_t *base, const float hit[3], uint32_t prop, bool rare_put_one_out)
  {
    if (!Lab() || prop != 0)
    {
      return;
    }
    std::string list;
    const int n = PutOut(ctx, base, hit, float(REXCVAR_GET(pdz_lab_dark_shot_cm)), list);
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_dark: shot lamp at ({:.0f} {:.0f} {:.0f}): Rare's pick {}, {} lamp(s) on within {:.0f} cm put "
                  "out:{}",
                  hit[0], hit[1], hit[2], rare_put_one_out ? "put a lamp out" : "put none out", n,
                  float(REXCVAR_GET(pdz_lab_dark_shot_cm)), list.empty() ? std::string(" none") : list);
    }
    if (n + (rare_put_one_out ? 1 : 0) > 0)
    {
      lab_stealth::LightIncident(ctx, base, hit, n + (rare_put_one_out ? 1 : 0));
    }
    g_sample_at = -1e9f;
  }

  bool VisualOn(const uint8_t *base)
  {
    return VisualWanted(base);
  }

  float VisualStrength()
  {
    return VisualStrengthNow();
  }

  float AmbientKeep(const uint8_t *base, const float pos[3])
  {
    if (!VisualWanted(base))
    {
      return 1.0f;
    }
    std::lock_guard<std::mutex> hold(g_lock);
    static OutLamp out[kMaxLights]; // under g_lock
    const int n = CollectOut(base, out, int(kMaxLights));
    return n > 0 ? KeepAt(out, n, VisualStrengthNow(), pos) : 1.0f;
  }

  bool VisualArmed()
  {
    return g_visual_armed.load(std::memory_order_relaxed);
  }

  bool VisualFrameFor(uint32_t sequence, VisualFrame &out)
  {
    std::lock_guard<std::mutex> hold(g_frame_lock);
    for (const FrameSlot &s : g_frames)
    {
      // Wrap-safe: first <= sequence < end.
      if (s.frame.count != 0 && uint32_t(sequence - s.first) < uint32_t(s.end - s.first))
      {
        out = s.frame;
        return true;
      }
    }
    return false;
  }
} // namespace lab_dark

// Rare's sun / ambient pass of the light pre-pass (called once per view by the light dispatcher sub_826042F0 for the
// level's type 1 light; r5 the light, r7 the view's world-to-view matrix, whose second column it uploads as the
// hemisphere ambient's up axis c4). With local darkness on, the lamps that are out go to the native renderer in this
// view's space for the draws this call records. Render thread; no logging unless pdz_lab_dark_log (at most every 5 s).
REX_HOOK_RAW(sub_826043E0)
{
  if (!REXCVAR_GET(pdz_lab_dark_patches) || !REXCVAR_GET(pdz_lab_dark_visual) ||
      !(REXCVAR_GET(pdz_lab_dark_visual_strength) > 0.0) || !VisualWanted(base))
  {
    if (g_visual_armed.load(std::memory_order_relaxed))
    {
      g_visual_armed.store(false, std::memory_order_relaxed);
    }
    __imp__sub_826043E0(ctx, base);
    return;
  }
  g_visual_armed.store(true, std::memory_order_relaxed);
  const uint32_t matrix = ctx.r7.u32;
  static OutLamp out[kMaxLights]; // this hook only (one render thread at a time draws the light pass)
  const int n = Ptr(matrix) ? CollectOut(base, out, int(kMaxLights)) : 0;
  if (n <= 0)
  {
    __imp__sub_826043E0(ctx, base);
    return;
  }
  // World to view. Rare's c4 = (m[1], m[5], m[9]) is the world's up axis in view space, so row i of the rotation is
  // m[4i..4i+2] either way; the translation is the fourth column (a world-to-view matrix) or, when that is empty and
  // the fourth row is not, the matrix is the camera's own (camera-to-world): view = R (p - row 3).
  float m[16];
  for (int i = 0; i < 16; ++i)
  {
    m[i] = LoadF(base, matrix + 4 * uint32_t(i));
  }
  const bool camera_form = std::fabs(m[3]) + std::fabs(m[7]) + std::fabs(m[11]) < 1e-4f &&
                           std::fabs(m[12]) + std::fabs(m[13]) + std::fabs(m[14]) > 1e-4f;
  const auto to_view = [&](const float p[3], float v[3]) {
    const float d[3] = {camera_form ? p[0] - m[12] : p[0], camera_form ? p[1] - m[13] : p[1],
                        camera_form ? p[2] - m[14] : p[2]};
    for (int i = 0; i < 3; ++i)
    {
      v[i] = m[4 * i] * d[0] + m[4 * i + 1] * d[1] + m[4 * i + 2] * d[2] + (camera_form ? 0.0f : m[4 * i + 3]);
    }
  };
  // The nearest kVisualMaxLamps to the camera (by the distance to the edge of each lamp's darkening).
  struct Near
  {
    float d;
    int i;
    float v[3];
  };
  static Near order[kMaxLights];
  int kept = 0;
  for (int i = 0; i < n; ++i)
  {
    Near &e = order[kept];
    to_view(out[i].pos, e.v);
    const float d =
        std::sqrt(e.v[0] * e.v[0] + e.v[1] * e.v[1] + e.v[2] * e.v[2]) - out[i].radius * lab_dark::kVisualFalloff;
    if (!std::isfinite(d) || d > 20000.0f) // beyond 200 m of the camera: no view of the Laboratory reaches it
    {
      continue;
    }
    e.d = d;
    e.i = i;
    ++kept;
  }
  if (kept > lab_dark::kVisualMaxLamps)
  {
    std::partial_sort(order, order + lab_dark::kVisualMaxLamps, order + kept,
                      [](const Near &a, const Near &b) { return a.d < b.d; });
    kept = lab_dark::kVisualMaxLamps;
  }
  if (kept == 0)
  {
    __imp__sub_826043E0(ctx, base);
    return;
  }
  const uint32_t first = pdz::shadow::Get().head;
  uint32_t slot;
  {
    std::lock_guard<std::mutex> hold(g_frame_lock);
    slot = g_frame_next++ % kFrameSlots;
    FrameSlot &s = g_frames[slot];
    s.first = first;
    s.end = first + pdz::shadow::RingSlots(); // open until the pass returns (the draws it records come next)
    s.frame.count = uint32_t(kept);
    s.frame.strength = VisualStrengthNow();
    for (int k = 0; k < kept; ++k)
    {
      const OutLamp &o = out[order[k].i];
      s.frame.lamp[k][0] = order[k].v[0];
      s.frame.lamp[k][1] = order[k].v[1];
      s.frame.lamp[k][2] = order[k].v[2];
      s.frame.lamp[k][3] = o.radius;
      s.frame.weight[k] = o.weight;
    }
  }
  __imp__sub_826043E0(ctx, base);
  {
    std::lock_guard<std::mutex> hold(g_frame_lock);
    FrameSlot &s = g_frames[slot];
    if (s.first == first)
    {
      s.end = pdz::shadow::Get().head;
    }
  }
  if (Logging())
  {
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - g_visual_logged_ms >= 5000)
    {
      g_visual_logged_ms = now;
      float jo_view[3] = {};
      const uint32_t jo = Load32(base, 0x82E3C3E4);
      if (Ptr(jo))
      {
        const float p[3] = {LoadF(base, jo + kPosition), LoadF(base, jo + kPosition + 4), LoadF(base, jo + kPosition + 8)};
        to_view(p, jo_view);
      }
      REXLOG_INFO("pdz_lab_dark_visual: ambient pass: {} lamp(s) out, {} drawn (nearest view ({:.0f} {:.0f} {:.0f}) "
                  "radius {:.0f} weight {:.2f}), strength {:.2f}, {} matrix, draws {}..{}, Jo in view space ({:.0f} "
                  "{:.0f} {:.0f})",
                  n, kept, order[0].v[0], order[0].v[1], order[0].v[2], out[order[0].i].radius, out[order[0].i].weight,
                  VisualStrengthNow(), camera_form ? "camera-to-world" : "world-to-view", first, pdz::shadow::Get().head,
                  jo_view[0], jo_view[1], jo_view[2]);
    }
  }
}
