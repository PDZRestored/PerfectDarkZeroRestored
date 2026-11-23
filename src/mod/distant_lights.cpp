// Distant window lights (Rare's bug, seen on a real Xbox 360 too; pdz_fix_distant_lights, on by default).
//
// What players see: in Subway's opening streets the lit windows of the houses in front of the blue towers go dark
// when looked at head-on and light up at the edge of the view; Nightclub's distant housing lights change with distance
// and with turning.
//
// What the lights are: the lit windows are Rare's deferred point lights (warm 1.46/1.5/0.87 and cyan 0.51/1.5/1.47,
// radius 300-700), drawn per light by sub_826048F0 (stencil sphere 826051E0 / 82605324, additive light 82605450),
// and the lamp halos (sub_82603AC0 -> 82608510) come from the same list.
//
// Rare's rule: each frame the list of lights to draw is built by sub_82606268 (called from sub_82602C88 with a cap of
// 200 lights): for every light in the level it asks sub_826064D0 for a score; a light with a score above 0 is drawn,
// the list is sorted by score (highest first; the top ones get the specular term, then it fades out by rank) and cut
// at the cap. For a point or spot light the score is
//     score = (z + radius) / range + 1        (clamped)
// where z is the light's position transformed by the camera (sub_827D15A8 into light +1232 + 12 * (2 * pass + buffer),
// so z = minus the depth along the view direction) and range = the caller's f2. So a light is drawn when its depth
// along the view direction, minus its radius, is under the range. Depth along the view direction is the distance
// times the cosine of the angle off the view centre: a light straight ahead counts at its full distance, the same light
// at the edge of the view counts nearer (at a 37 degree half angle, 0.8 of its distance). A light just past the range
// is therefore dropped when looked at and drawn when it is at the edge of the view, which is exactly the switching.
//
// The fix: the same score with the straight distance |v| from the camera instead of the depth (the range, the radius,
// the clamp, the cap and the sort are Rare's). A light's state then depends only on how far away it is, not on where
// it is in the view. pdz_distant_lights_reach scales the distance: 1 = Rare's range measured straight; below 1 the
// lights reach further (0.8 keeps every light that Rare drew at the edge of a 37 degree half angle view lit head-on).
//
// Diagnostics: pdz_distant_lights_log = N logs the next N list builds (one per pass and frame): every light the score
// function sees, with its world position, view vector, radius, gates, Rare's score and the fixed score.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "../game_cvars.h" // pdz_game_fixes
#include "../pdz_version.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): the room chain read only where mapped

REXCVAR_DEFINE_BOOL(pdz_fix_distant_lights, false, "Gameplay",
                    "Game fix (Rare's bug, on a real Xbox 360 too): lights (lit windows, lamps) are kept or dropped by "
                    "their straight distance from the camera instead of their depth along the view direction, so "
                    "distant window lights no longer switch off when looked at head-on and on at the edge of the view "
                    "(Subway's houses, Nightclub's housing). Off by default in 0.9.4 (the overnight runs showed no change at the "
                    "scripted spots: unproven). Needs pdz_game_fixes")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_distant_lights_reach, 1.0, "Gameplay",
                      "pdz_fix_distant_lights: the light's straight distance is multiplied by this before Rare's range "
                      "test. 1 = Rare's range; 0.8 = lights reach as far head-on as Rare's reached at the view's edge "
                      "(more lights drawn)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_distant_lights_log, 0, "Debug",
                     "Logs the next N light list builds (sub_82606268): every light's position, view vector, radius, "
                     "gates and score, Rare's and fixed. 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Light draw distance (pdz_light_draw_distance, 0 = Rare's by default; F6 cycles the steps while playing).
//
// Rare's range for the lights is not a light setting: it is the view's far plane, which is the fog end where the
// area has fog (sub_8220D988 puts the fog end into camera + 80 for the frame). Two cuts use it:
//   1. the per-view visibility mask (sub_82628F10(view, camera)): a light whose sphere is outside the view's six
//      frustum planes (the far one built from camera + 80) or whose rooms are not visible has its bit cleared;
//   2. the score above (sub_826064D0), which only looks at lights whose mask bit is set.
// A light beyond the range is therefore refused twice; changing the score alone can only drop lights.
//
// The option's steps: 0 Rare, 1 = x1.5, 2 = x2, 3 = x4, 4 = All.
//   - The mask is built with the far plane moved out by the step (camera + 80 for that one call, then put back and
//     the projection rebuilt, so everything else sees Rare's camera). Rooms and the side planes stay Rare's.
//   - The score uses the straight distance against Rare's range times the step (All: no range limit; the score
//     still falls with distance, so Rare's sort keeps the nearest at its cap of 200, and its specular rule by rank).
//   - No pop-in: a light inside the last pdz_light_draw_distance_fade (12.5 %) of the new range is drawn dimmer
//     (its colour for that one draw: the render buffer's copy at light + 828 + 16 * buffer is scaled around Rare's
//     draw call and put back) and its halo smaller (the two size factors of sub_82608510). At Rare's cap the last
//     listed light is dimmed by Rare's own cap-edge factor (light + 1408 + 4 * view, which Rare writes but never
//     reads), pdz_light_draw_distance_cap_fade.
//   - Nothing permanent is written to a light: not its base fields, its on / off state, or the level's light array
//     (the stealth layer's light reading uses those, never the drawn list).
// Far lighting, as shipped (round 3): pdz_far_lighting (on) is the master switch. With it on, Rare's cap of 200 lights
// per view is lifted (pdz_light_cap 2000), every light in view gets Rare's full light shader (pdz_light_detail 3), and
// with pdz_far_lighting_beams the lights that have a beam (Rare's glow cone: byte +821 set and size +756 above 0) are
// drawn even when Rare's room visibility hides their room. With it off every hook here passes straight through:
// Rare's list, cap, shader choice and room rule exactly.
REXCVAR_DEFINE_BOOL(pdz_far_lighting, true, "Graphics",
                    "Far lighting: every light in view is drawn (Rare stops at the nearest 200) with Rare's full light "
                    "shader (Rare gives it to the nearest 25 only), so distant lighting no longer appears as the player "
                    "gets closer. Off = Rare's lighting exactly. The finer switches below it: pdz_light_cap, "
                    "pdz_light_detail, pdz_far_lighting_beams, pdz_light_draw_distance")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_far_lighting_beams, true, "Graphics",
                    "Far lighting: lights that have a beam (street lamps, spot lights with a glow cone) are drawn even "
                    "when Rare's room visibility hides their room, so a distant beam does not go missing. Plain lights "
                    "in hidden rooms stay cut as Rare cuts them. On by default: its cost at the Laboratory's tunnel "
                    "view was within measurement noise (about 0.03 to 0.05 ms). Needs pdz_far_lighting")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_far_lighting_key, false, "Debug",
                    "Testing: F6 (bind_light_draw_distance) cycles the far lighting steps: Rare / no cap / no cap + full "
                    "lighting / + hidden beams / + all hidden rooms, with the step in the window title, a label and the "
                    "log")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_light_draw_distance, 0, "Graphics",
                     "Light draw distance: how far away Rare's lights (pools of light, lit windows, lamp halos) are "
                     "drawn. 0 = Rare's (the fog distance, measured along the view), 1 = x1.5, 2 = x2, 3 = x4 (Rare's "
                     "range times that, measured as straight distance so turning no longer switches lights), 4 = All "
                     "(no range limit; Rare's cap of 200 lights, sort and specular rule kept), 5 = All + hidden rooms "
                     "(also lights whose rooms Rare's room visibility hides; they have no shadows, so some may "
                     "shine through walls). F6 cycles pdz_light_preset, which sets this")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_light_draw_distance_fade, 0.125, "Graphics",
                      "pdz_light_draw_distance: a light fades in over this last part of the new range (its "
                      "brightness and its halo's size) instead of switching on. 0 = no fade")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_light_draw_distance_cap_fade, true, "Graphics",
                    "pdz_light_draw_distance: when more lights are in range than Rare's cap of 200, the last listed "
                    "light is dimmed by Rare's own cap-edge factor instead of switching on")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Light detail (pdz_light_detail, 0 = Rare's by default; F6 cycles its steps while playing).
//
// What makes the pools of light on the rock appear as the player walks closer (the Laboratory's first cave, seen in
// captures 5 m apart): Rare draws every listed light, but only the first 25 drawn in a view get the full light
// shader (the one with the specular term): in both draw functions (sub_826048F0 point, sub_82605488 spot, at
// 82604BA4 / the same code in the spot one) the count of full lights so far ([0x82E3C630]) must be under 25 and the
// light's byte +752 clear; the amount is full for a list rank under 15 and fades with the count from 15 to 25; every
// later light gets the cheaper shader with the amount 0. The list is sorted nearest first, so a lamp's pool on the
// rock switches on when the lamp becomes one of the nearest 25 lights in view.
//
// The option scales both numbers by k (x2: 30 / 50, x4: 60 / 100, All: every light): for the one call the rank
// passed in r5 is divided by k and the counter Rare reads is the true count divided by k; afterwards the counter is
// put back to the true count (plus one when Rare took the full shader). Rare's own fade then runs from 15 k to 25 k
// lights, so nothing pops at the new edge. Rare's shaders, the list and the lights are untouched.
REXCVAR_DEFINE_INT32(pdz_light_detail, 3, "Graphics",
                     "Light detail (needs pdz_far_lighting): how many of the nearest lights in view get Rare's full light "
                     "shader (the pools of light on rock and walls). 0 = Rare's (15 at full strength, fading out by the "
                     "25th), 1 = x2 (30 / "
                     "50), 2 = x4 (60 / 100), 3 = All. F6 cycles pdz_light_preset, which sets this")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Rare's exposure (the tone map, pixel shader EDCCE21F): the picture is multiplied by clamp(La, lo, hi) / La, La being
// the scene's adapted luminance and (lo, hi) = (0.2, 2.0) two floats of static data at 0x82D25858 (set as the
// shader's c0). Between the two the picture is untouched; a scene brighter than hi is darkened as a whole, one
// darker than lo brightened. pdz_exposure_hi / pdz_exposure_lo replace the two numbers (0 = Rare's).
REXCVAR_DEFINE_DOUBLE(pdz_exposure_hi, 0.0, "Graphics",
                      "Rare's exposure: the adapted scene brightness above which the whole picture is darkened (Rare's "
                      "2.0). A larger number keeps a bright place (a lit tunnel) from darkening the view beyond it. 0 = "
                      "Rare's")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_exposure_lo, 0.0, "Graphics",
                      "Rare's exposure: the adapted scene brightness below which the whole picture is brightened "
                      "(Rare's 0.2). 0 = Rare's")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Rare's cap on the light list: sub_82602C88 asks sub_82606268 for at most 200 lights per view (r3). The list's own
// storage holds 2000 pointers per view and buffer and Rare fills it with every level light each frame (the drawn ones
// first, sorted); the wrapper's use-count loop, the draw loop (sub_82603560) and the halo loop walk it by the stored
// count, with no 200-sized array. pdz_light_cap replaces the 200 (never the 1 of Rare's one-light mode).
REXCVAR_DEFINE_INT32(pdz_light_cap, 2000, "Graphics",
                     "The most lights drawn in a view (needs pdz_far_lighting). 200 or less = Rare's 200; up to 2000 "
                     "(the list itself holds 2000). Where more lights are in view than this, the farthest are dropped")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// The presets F6 cycles (pdz_light_preset). What makes far lighting appear as the player walks out of a place with
// many lights around (the Laboratory's yellow tunnel, looking out at the cave's rock face): more than 200 lights are
// in view there, Rare keeps the nearest 200 and drops the farthest, and they come back as the near ones fall behind
// the player. So the first step lifts the cap; the next two add the other two rules found on the way.
//   0 Rare.  1 No cap (pdz_light_cap 2000).  2 + full lighting for every light (pdz_light_detail 3).
//   3 + lights in hidden rooms (pdz_light_draw_distance 5).
REXCVAR_DEFINE_INT32(pdz_light_preset, -1, "Debug",
                     "Testing: sets the far lighting switches to one of the steps F6 cycles: 0 = Rare's; 1 = no cap; 2 "
                     "= no cap + full lighting; 3 = + hidden beams; 4 = + all hidden rooms. -1 = leave the switches "
                     "alone")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_light_reach_report, false, "Debug",
                    "Logs one far lighting report every 5 s (lights wanted, listed, full-shader lights, hidden beams "
                    "added) and a census of each level's lights (how many have a beam)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_test_light_draw_distance_key, 0, "Debug",
                     "Testing tool: set to 1 to act as one press of the light draw distance key (F6); it resets to 0")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_826064D0); // a light's score: (r3 light, r10 pass, f2 range) -> f1
REX_EXTERN(__imp__sub_82606268); // the light list build: (r3 cap, r10 pass, f1..f4)
REX_EXTERN(__imp__sub_82628F10); // the per-view light visibility mask: (r3 view, r4 camera)
REX_EXTERN(__imp__sub_826048F0); // draw a point light: (.., r6 light, r10 view)
REX_EXTERN(__imp__sub_82605488); // draw a spot light: (.., r6 light, r10 view)
REX_EXTERN(__imp__sub_82608510); // draw a lamp halo: (.., r7 light, f3 / f4 size factors; view at [r1 + 260])
REX_EXTERN(sub_826536F8);        // the camera's projection, rebuilt when its dirty byte (+84) is set: (r3 camera)

namespace pdz::transwater
{
  void ShowStatus(const std::string &text, int seconds); // the on-screen label (transparency_water.cpp)
}

namespace
{
  constexpr uint32_t kBuffer = 0x82E1BA20;     // render buffer index (0 / 1)
  constexpr uint32_t kListCount = 0x82E0A0BC;  // + 4 * (2 * pass + buffer): lights in the list
  constexpr uint32_t kLevelLights = 0x82E3C5F4; // lights in the level
  // Rare's float constants (the clamp of the score).
  constexpr uint32_t kOne = 0x820DFD34, kZero = 0x820DFCA4;
  constexpr uint32_t kMax = 0x820EB1A0, kOverMax = 0x820EB198, kMin = 0x820EAC94, kUnderMin = 0x820EB190;
  // The light (offsets).
  constexpr uint32_t kType = 680, kRadius = 684, kWorldPos = 720, kViewPos = 1232, kRgb = 828, kActive = 1448;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    return _byteswap_ulong(*reinterpret_cast<const uint32_t *>(base + a));
  }
  float LoadF(const uint8_t *base, uint32_t a)
  {
    const uint32_t u = Load32(base, a);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  }
  double LoadD(const uint8_t *base, uint32_t a)
  {
    const uint64_t u = _byteswap_uint64(*reinterpret_cast<const uint64_t *>(base + a));
    double d;
    std::memcpy(&d, &u, 8);
    return d;
  }

  // Rare's clamp at the end of sub_826064D0 (two fsel: over the max -> kOverMax, under the min -> kUnderMin).
  double Clamp(const uint8_t *base, double s)
  {
    const double max = LoadF(base, kMax), min = LoadF(base, kMin);
    const double f0 = (max - s) >= 0.0 ? s : LoadD(base, kOverMax);
    return (s - min) >= 0.0 ? f0 : LoadD(base, kUnderMin);
  }

  void StoreRaw32(uint8_t *base, uint32_t a, uint32_t raw) { std::memcpy(base + a, &raw, 4); }
  uint32_t LoadRaw32(const uint8_t *base, uint32_t a)
  {
    uint32_t raw;
    std::memcpy(&raw, base + a, 4);
    return raw;
  }
  void StoreF(uint8_t *base, uint32_t a, float f)
  {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    StoreRaw32(base, a, _byteswap_ulong(u));
  }

  // ---- pdz_light_draw_distance ----
  constexpr uint32_t kRenderBuffer = 0x82E0A0DC; // the render side's buffer index (the draws and halos read this one)
  constexpr uint32_t kLists = 0x82E0A0E0;        // + 8000 * (2 * view + buffer): 2000 light pointers
  constexpr uint32_t kCapEdge = 1408;            // light + 1408 + 4 * view: Rare's cap-edge factor (1 = not at the edge)
  constexpr uint32_t kCamFar = 80, kCamDirty = 84;
  constexpr double kAllRange = 4.0e6;            // "All": the score's range (cm); every light in a level scores above 0
  constexpr float kAllFar = 1.0e6f;              // "All": the far plane the visibility mask is built with (cm)
  constexpr int kSteps = 6, kAll = 4, kHidden = 5; // kAll and above: no range limit
  constexpr const char *kStepNames[kSteps] = {"Rare", "x1.5", "x2", "x4", "All", "All + hidden rooms"};
  constexpr double kStepFactor[kSteps] = {1.0, 1.5, 2.0, 4.0, 0.0, 0.0};

  constexpr uint32_t kFullCount = 0x82E3C630; // full-shader lights drawn so far in this view (Rare's counter)
  constexpr int kDetailSteps = 4, kDetailAll = 3;
  constexpr const char *kDetailNames[kDetailSteps] = {"Rare (15 / 25 lights)", "x2 (30 / 50 lights)",
                                                      "x4 (60 / 100 lights)", "All lights"};
  constexpr uint32_t kDetailFactor[kDetailSteps] = {1, 2, 4, 0};

  bool FarOn() { return REXCVAR_GET(pdz_far_lighting); }

  int Detail()
  {
    const int s = REXCVAR_GET(pdz_light_detail);
    return !FarOn() || s < 0 ? 0 : s >= kDetailSteps ? kDetailSteps - 1 : s;
  }

  // A light with a beam: Rare's glow pass (sub_82603AC0 -> sub_82608510) draws a cone for a listed light whose byte
  // +821 is set and whose size +756 is above 0.
  bool HasBeam(const uint8_t *base, uint32_t light) { return base[light + 821] != 0 && LoadF(base, light + 756) > 0.0f; }
  std::atomic<uint32_t> g_full_last{0}, g_drawn_last{0}; // full-shader lights / lights drawn in the last view drawn
  uint32_t g_drawn_run = 0;

  int Step()
  {
    const int s = REXCVAR_GET(pdz_light_draw_distance);
    return !FarOn() || s < 0 ? 0 : s >= kSteps ? kSteps - 1 : s;
  }

  // The straight-distance limit the last list build used, per (view, buffer): 0 = Rare's rule (no fade), infinity =
  // All (no range edge). Written by the build (game side buffer), read by the draws (render side buffer).
  std::atomic<float> g_limit[4][2];

  // The report: one line for view 0, a little after a step change (and once at the first build of a run).
  std::atomic<int> g_report_in{40};
  bool g_report = false;
  int g_rep_wanted = 0, g_rep_far = 0, g_rep_other = 0, g_rep_gated = 0;
  double g_rep_range = 0.0;

  int g_hidden_added = 0; // lights step 5 added in the last mask build of view 0
  std::string g_fog_text; // the view's fog at the last reading (the mask hook fills it)
  double g_fwd[3] = {};   // the camera's forward direction at the last reading
  int g_dump_stage = 0; // 1: the mask hook has filled the reading for view 0; the list build logs it
  void DumpLights(const uint8_t *base, uint32_t cap, double range);

  rex::ui::Window *g_window = nullptr;
  std::string g_title;

  std::atomic<int64_t> g_last_press_ms{0};

  constexpr int kPresets = 5;
  constexpr const char *kPresetNames[kPresets] = {"Rare", "no cap", "no cap + full lighting",
                                                  "no cap + full lighting + hidden beams",
                                                  "no cap + full lighting + all hidden rooms"};
  // The step the switches stand at now (for the key's "next").
  int CurrentPreset()
  {
    if (!FarOn() || REXCVAR_GET(pdz_light_cap) <= 200)
    {
      return 0;
    }
    if (REXCVAR_GET(pdz_light_detail) != kDetailAll)
    {
      return 1;
    }
    return REXCVAR_GET(pdz_light_draw_distance) >= kHidden ? 4 : REXCVAR_GET(pdz_far_lighting_beams) ? 3 : 2;
  }
  void SetPreset(int preset)
  {
    REXCVAR_SET(pdz_far_lighting, true);
    REXCVAR_SET(pdz_light_cap, preset >= 1 ? 2000 : 200);
    REXCVAR_SET(pdz_light_detail, preset >= 2 ? kDetailAll : 0);
    REXCVAR_SET(pdz_far_lighting_beams, preset == 3);
    REXCVAR_SET(pdz_light_draw_distance, preset >= 4 ? kHidden : 0);
    REXLOG_INFO("pdz_light_reach: far lighting step {} ({}): pdz_light_cap {}, pdz_light_detail {}, "
                "pdz_far_lighting_beams {}, pdz_light_draw_distance {}",
                preset, kPresetNames[preset], REXCVAR_GET(pdz_light_cap), REXCVAR_GET(pdz_light_detail),
                REXCVAR_GET(pdz_far_lighting_beams), REXCVAR_GET(pdz_light_draw_distance));
  }
  // pdz_light_preset (testing): applied when the number changes, from view 0's list build.
  void ApplyPreset()
  {
    static int applied = -1;
    const int preset = REXCVAR_GET(pdz_light_preset);
    if (preset == applied || preset < 0 || preset >= kPresets)
    {
      return;
    }
    applied = preset;
    SetPreset(preset);
  }

  // One press of the key, from the key bind (UI thread), the keyboard read or the test switch (game thread). A press
  // that arrives by two ways, or a held key repeating, counts once.
  void Cycle(const char *from)
  {
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    int64_t last = g_last_press_ms.load(std::memory_order_relaxed);
    if (now - last < 300 || !g_last_press_ms.compare_exchange_strong(last, now))
    {
      return;
    }
    const int step = (CurrentPreset() + 1) % kPresets;
    SetPreset(step);
    g_report_in.store(40, std::memory_order_relaxed);
    const std::string text = fmt::format("Far lighting: {}", kPresetNames[step]);
    REXLOG_INFO("pdz_light_reach: KEY PRESS ({}): far lighting step {} ({}) (pdz_far_lighting_key)", from, step,
                kPresetNames[step]);
    pdz::transwater::ShowStatus(text, 3);
    if (g_window)
    {
      rex::ui::Window *w = g_window;
      const std::string title = g_title + " | " + text;
      w->app_context().CallInUIThread([w, title] { w->SetTitle(title); });
    }
  }

  // How bright a light is drawn for this view: 1 inside the range, falling to 0 over the last part of it; and
  // Rare's cap-edge factor for the last listed light.
  float Fade(const uint8_t *base, uint32_t light, uint32_t view, uint32_t b)
  {
    if (view > 3 || b > 1)
    {
      return 1.0f;
    }
    float f = 1.0f;
    const float limit = g_limit[view][b].load(std::memory_order_relaxed);
    if (!(limit > 0.0f))
    {
      return 1.0f;
    }
    const double part = REXCVAR_GET(pdz_light_draw_distance_fade);
    if (std::isfinite(limit) && part > 0.0 && part <= 1.0)
    {
      const uint32_t v = light + kViewPos + 12 * (2 * view + b);
      const double x = LoadF(base, v), y = LoadF(base, v + 4), z = LoadF(base, v + 8);
      const double d = std::sqrt(x * x + y * y + z * z) - double(LoadF(base, light + kRadius));
      const double t = (double(limit) - d) / (part * double(limit));
      if (std::isfinite(t) && t < 1.0)
      {
        f = t > 0.0 ? float(t) : 0.0f;
      }
    }
    if (REXCVAR_GET(pdz_light_draw_distance_cap_fade))
    {
      const float edge = LoadF(base, light + kCapEdge + 4 * view);
      if (edge >= 0.0f && edge < 1.0f)
      {
        f *= edge;
      }
    }
    return f;
  }

  // Rare's draw with the light's colour (this buffer's copy) scaled for the one call.
  template <typename Original> void DrawFaded(PPCContext &ctx, uint8_t *base, Original original)
  {
    if (Step() == 0)
    {
      original(ctx, base);
      return;
    }
    const uint32_t light = ctx.r6.u32, view = ctx.r10.u32 & 0xFF, b = Load32(base, kRenderBuffer);
    const float f = light && b <= 1 ? Fade(base, light, view, b) : 1.0f;
    if (f >= 0.999f)
    {
      original(ctx, base);
      return;
    }
    const uint32_t rgb = light + b * 16 + kRgb;
    const uint32_t saved[3] = {LoadRaw32(base, rgb), LoadRaw32(base, rgb + 4), LoadRaw32(base, rgb + 8)};
    for (int i = 0; i < 3; ++i)
    {
      StoreF(base, rgb + 4 * i, LoadF(base, rgb + 4 * i) * f);
    }
    original(ctx, base);
    for (int i = 0; i < 3; ++i)
    {
      StoreRaw32(base, rgb + 4 * i, saved[i]);
    }
  }

  std::chrono::steady_clock::time_point g_last_report{};
  int g_beams_added = 0; // hidden-room lights with a beam that the last mask build of view 0 put back

  bool Beams() { return FarOn() && REXCVAR_GET(pdz_far_lighting_beams) && Step() < kHidden; }

  // Once per level (when the level's light array changes): how many lights there are and how many have a beam.
  void Census(const uint8_t *base)
  {
    static uint32_t last_first = 0, last_total = 0;
    const uint32_t total = std::min<uint32_t>(Load32(base, 0x82E3C5F4), 2000), first = Load32(base, 0x82E19AE0);
    if (total == 0 || (first == last_first && total == last_total))
    {
      return;
    }
    last_first = first;
    last_total = total;
    int dir = 0, point = 0, spot = 0, beam_point = 0, beam_spot = 0;
    for (uint32_t i = 0; i < total; ++i)
    {
      const uint32_t light = Load32(base, 0x82E19AE0 + 4 * i);
      if (!light)
      {
        continue;
      }
      const uint32_t type = Load32(base, light + kType);
      if (type == 1)
      {
        ++dir;
        continue;
      }
      ++(type == 0 ? spot : point);
      if (HasBeam(base, light))
      {
        ++(type == 0 ? beam_spot : beam_point);
        REXLOG_INFO("pdz_light_reach: census:   beam light {} type {} at ({:.0f} {:.0f} {:.0f}) radius {:.0f}, beam "
                    "size {:.0f}",
                    i, type == 0 ? "spot" : "point", LoadF(base, light + kWorldPos), LoadF(base, light + kWorldPos + 4),
                    LoadF(base, light + kWorldPos + 8), LoadF(base, light + kRadius), LoadF(base, light + 756));
      }
    }
    REXLOG_INFO("pdz_light_reach: census: {} level lights: {} directional, {} point, {} spot; with a beam: {} ({} "
                "point, {} spot); plain: {}",
                total, dir, point, spot, beam_point + beam_spot, beam_point, beam_spot,
                point + spot - beam_point - beam_spot);
  }

  // pdz_exposure_lo / _hi: the tone map's two clamp numbers (static data), written while an option is set and put
  // back to Rare's when it is cleared. Checked once: the data must hold Rare's (0.2, 2.0) or nothing is written.
  constexpr uint32_t kExposureClamp = 0x82D25858;
  void ApplyExposure(uint8_t *base)
  {
    static int state = 0; // 0 not checked, 1 Rare's values found, -1 not found
    static double last_lo = -1.0, last_hi = -1.0;
    const double lo = REXCVAR_GET(pdz_exposure_lo), hi = REXCVAR_GET(pdz_exposure_hi);
    if (state == 0)
    {
      if (!(lo > 0.0) && !(hi > 0.0))
      {
        return;
      }
      const float a = LoadF(base, kExposureClamp), b = LoadF(base, kExposureClamp + 4);
      state = std::fabs(a - 0.2f) < 1e-4f && std::fabs(b - 2.0f) < 1e-4f ? 1 : -1;
      REXLOG_INFO("pdz_light_reach: exposure clamp at {:08X}: {:.3f} / {:.3f} ({}); the next two floats {:.3f} {:.3f}",
                  kExposureClamp, a, b, state == 1 ? "Rare's 0.2 / 2.0: found" : "NOT Rare's values: left alone",
                  LoadF(base, kExposureClamp + 8), LoadF(base, kExposureClamp + 12));
    }
    if (state != 1 || (lo == last_lo && hi == last_hi))
    {
      return;
    }
    last_lo = lo;
    last_hi = hi;
    StoreF(base, kExposureClamp, lo > 0.0 ? float(lo) : 0.2f);
    StoreF(base, kExposureClamp + 4, hi > 0.0 ? float(hi) : 2.0f);
    REXLOG_INFO("pdz_light_reach: exposure clamp now {:.3f} / {:.3f} (Rare's 0.2 / 2.0)", LoadF(base, kExposureClamp),
                LoadF(base, kExposureClamp + 4));
  }

  // The key read straight from the keyboard once a frame (view 0's list build), beside the key bind: only while this
  // process owns the foreground window. The bind's key name is read again every 2 s (it can be rebound).
  void PollKey()
  {
    if (!REXCVAR_GET(pdz_far_lighting_key))
    {
      return;
    }
    static int vk = 0;
    static bool was_down = false;
    static auto last_name = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_name > std::chrono::seconds(2))
    {
      last_name = now;
      vk = int(rex::ui::ParseVirtualKey(rex::cvar::GetFlagByName("bind_light_draw_distance")));
    }
    if (vk <= 0 || vk > 0xFE)
    {
      return;
    }
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (down)
    {
      DWORD pid = 0;
      const HWND fg = GetForegroundWindow();
      down = fg && GetWindowThreadProcessId(fg, &pid) && pid == GetCurrentProcessId();
    }
    if (down && !was_down)
    {
      Cycle("the keyboard, read directly");
    }
    was_down = down;
  }

  // Rare's draw of one light with the light detail step applied (see pdz_light_detail), then the fade.
  template <typename Original> void DrawLight(PPCContext &ctx, uint8_t *base, Original original)
  {
    const int detail = Detail();
    const uint32_t count = Load32(base, kFullCount);
    static uint32_t last_count = 0;
    if (count < last_count)
    {
      g_drawn_run = 0; // Rare has reset its counter: a new view's lights
    }
    uint32_t shown = count;
    if (detail > 0)
    {
      const uint32_t k = kDetailFactor[detail];
      shown = k ? count / k : 0;
      StoreRaw32(base, kFullCount, _byteswap_ulong(shown));
      ctx.r5.u64 = k ? ctx.r5.u32 / k : 0;
    }
    DrawFaded(ctx, base, original);
    uint32_t after = Load32(base, kFullCount);
    if (detail > 0)
    {
      after = count + (after == shown + 1 ? 1u : 0u);
      StoreRaw32(base, kFullCount, _byteswap_ulong(after));
    }
    last_count = after;
    g_full_last.store(after, std::memory_order_relaxed);
    g_drawn_last.store(++g_drawn_run, std::memory_order_relaxed);
  }

  int g_log_builds = 0;        // list builds still to log (set from the cvar)
  bool g_logging = false;      // inside a logged build
  uint32_t g_build_seq = 0;
  int g_light_seq = 0;
  int g_changed = 0;           // lights whose drawn / dropped state the fix changed in this build
} // namespace

REX_HOOK_RAW(sub_82606268)
{
  const int want = REXCVAR_GET(pdz_distant_lights_log);
  if (want > 0)
  {
    g_log_builds = want;
    REXCVAR_SET(pdz_distant_lights_log, 0);
  }
  const uint32_t pass = ctx.r10.u32 & 0xFF;
  if (pass == 0)
  {
    ApplyPreset();
  }
  const int cap_option = FarOn() ? REXCVAR_GET(pdz_light_cap) : 0;
  if (ctx.r3.u32 == 200 && cap_option > 200)
  {
    ctx.r3.u64 = uint32_t(cap_option > 2000 ? 2000 : cap_option);
  }
  const uint32_t cap = ctx.r3.u32;
  const double list_range = ctx.f2.f64;
  if (REXCVAR_GET(pdz_test_light_draw_distance_key) > 0)
  {
    REXCVAR_SET(pdz_test_light_draw_distance_key, 0);
    Cycle("the test switch");
  }
  if (pass == 0)
  {
    ApplyExposure(base);
    PollKey();
    // The report every 5 s while the option is on (and once a little after every step change).
    const auto now = std::chrono::steady_clock::now();
    if (FarOn() && REXCVAR_GET(pdz_light_reach_report) && now - g_last_report > std::chrono::seconds(5) && g_report_in.load(std::memory_order_relaxed) <= 0)
    {
      g_report_in.store(1, std::memory_order_relaxed);
    }
  }
  const int step = Step();
  const uint32_t gb = Load32(base, kBuffer);
  if (pass < 4 && gb <= 1)
  {
    const double range = ctx.f2.f64;
    g_limit[pass][gb].store(step == 0 || !(range > 0.0) ? 0.0f
                            : step >= kAll ? std::numeric_limits<float>::infinity()
                                                 : float(range * kStepFactor[step]),
                            std::memory_order_relaxed);
  }
  g_report = false;
  if (pass == 0 && g_report_in.load(std::memory_order_relaxed) > 0 &&
      g_report_in.fetch_sub(1, std::memory_order_relaxed) == 1)
  {
    g_report = true;
    g_last_report = std::chrono::steady_clock::now();
    g_rep_wanted = g_rep_far = g_rep_other = g_rep_gated = 0;
    g_rep_range = ctx.f2.f64;
  }
  if (g_report)
  {
    __imp__sub_82606268(ctx, base);
    g_report = false;
    if (g_dump_stage == 1)
    {
      g_dump_stage = 0;
      DumpLights(base, cap, list_range);
    }
    const uint32_t slot = 2 * pass + (gb & 1u), count = Load32(base, kListCount + 4 * slot);
    int edged = 0;
    float edge_value = 1.0f;
    uint32_t edge_rank = 0;
    for (uint32_t i = 0; i < count && i < 2000; ++i)
    {
      const uint32_t light = Load32(base, kLists + 8000 * slot + 4 * i);
      const float e = light ? LoadF(base, light + kCapEdge + 4 * pass) : 1.0f;
      if (e != 1.0f)
      {
        ++edged;
        edge_value = e;
        edge_rank = i;
      }
    }
    Census(base);
    REXLOG_INFO("pdz_light_reach: report (view 0): far lighting {}; hidden beams {} (added in the last view: {})",
                FarOn() ? "ON" : "OFF (Rare's lighting exactly)", Beams() ? "on" : "off", g_beams_added);
    REXLOG_INFO("pdz_light_reach: report (view 0): light detail step {} ({}): {} of the {} lights drawn in the last "
                "view got Rare's full light shader (Rare's rule: at most 25)",
                Detail(), kDetailNames[Detail()], g_full_last.load(std::memory_order_relaxed),
                g_drawn_last.load(std::memory_order_relaxed));
    REXLOG_INFO("pdz_light_reach: report (view 0): step {} ({}), Rare's range {:.0f} cm (the far plane / fog end), "
                "level lights {}; lights wanting in (in range, visible, lit) {}, in the list {} (cap {}){}; not "
                "visible (mask bit clear): {} beyond Rare's range along the view, {} by rooms / side planes; other "
                "gates (off / black / not in use) {}; hidden-room lights added by step 5: {}; cap-edge factor: {} listed "
                "light(s) not at 1{}",
                step, kStepNames[step], g_rep_range, Load32(base, kLevelLights), g_rep_wanted, count, cap,
                g_rep_wanted > int(cap) ? " THE CAP BINDS" : "", g_rep_far, g_rep_other, g_rep_gated,
                step >= kHidden ? g_hidden_added : 0, edged,
                edged ? fmt::format(" (rank {} = {:.3f})", edge_rank, edge_value) : std::string());
    return;
  }
  g_logging = g_log_builds > 0;
  if (g_logging)
  {
    --g_log_builds;
    ++g_build_seq;
    g_light_seq = 0;
    g_changed = 0;
    REXLOG_INFO("distant_lights: build {} pass {} cap {} range f2 {:.1f} (f1 {:.3f} f3 {:.3f} f4 {:.3f}) level lights "
                "{} fix {} reach {:.3f}",
                g_build_seq, pass, cap, ctx.f2.f64, ctx.f1.f64, ctx.f3.f64, ctx.f4.f64,
                Load32(base, kLevelLights), REXCVAR_GET(pdz_game_fixes) && REXCVAR_GET(pdz_fix_distant_lights),
                REXCVAR_GET(pdz_distant_lights_reach));
  }
  __imp__sub_82606268(ctx, base);
  if (pass == 0 && g_dump_stage == 1)
  {
    g_dump_stage = 0;
    DumpLights(base, cap, list_range);
  }
  if (g_logging)
  {
    const uint32_t b = Load32(base, kBuffer) & 1u;
    REXLOG_INFO("distant_lights: build {} done: {} lights in the list, {} changed by the fix", g_build_seq,
                Load32(base, kListCount + 4 * (2 * pass + b)), g_changed);
    g_logging = false;
  }
}

REX_HOOK_RAW(sub_826064D0)
{
  const uint32_t light = ctx.r3.u32, pass = ctx.r10.u32 & 0xFF;
  const double range = ctx.f2.f64;
  __imp__sub_826064D0(ctx, base);
  const int step = Step();
  const bool on = step == 0 && REXCVAR_GET(pdz_game_fixes) && REXCVAR_GET(pdz_fix_distant_lights);
  if (step == 0 && !on && !g_logging && !g_report)
  {
    return;
  }
  // Rare's gates (as sub_826064D0): enabled in this buffer, active, not a directional light, visible in this pass,
  // and not black. Where any fails Rare's score stands.
  const uint32_t b = Load32(base, kBuffer);
  if (b > 1)
  {
    return;
  }
  const uint32_t type = Load32(base, light + kType);
  bool point = Load32(base, light + (b + 356) * 4) != 0 && int32_t(Load32(base, light + kActive)) >= 1 && type != 1;
  const uint32_t mask = point ? Load32(base, light + b * 340) : 0;
  const bool in_pass = pass < 32 && (mask >> pass) & 1u;
  const uint32_t rgb = light + b * 16 + kRgb;
  const bool lit = LoadF(base, rgb) > 0.0f || LoadF(base, rgb + 4) > 0.0f || LoadF(base, rgb + 8) > 0.0f;
  const double rare = ctx.f1.f64;
  double fixed = rare;
  const uint32_t v = light + kViewPos + 12 * (2 * pass + b);
  const float vx = point ? LoadF(base, v) : 0.0f, vy = point ? LoadF(base, v + 4) : 0.0f,
              vz = point ? LoadF(base, v + 8) : 0.0f;
  const double dist = std::sqrt(double(vx) * vx + double(vy) * vy + double(vz) * vz);
  const double radius = point ? LoadF(base, light + kRadius) : 0.0;
  if (point && in_pass && lit && range > 0.0 && std::isfinite(dist))
  {
    double reach = REXCVAR_GET(pdz_distant_lights_reach);
    if (!(reach > 0.05 && reach <= 4.0))
    {
      reach = 1.0;
    }
    fixed = Clamp(base, (-dist * reach + radius) / range + double(LoadF(base, kOne)));
    if (step > 0)
    {
      // pdz_light_draw_distance: the straight distance against Rare's range times the step (All: no limit).
      const double limit = step >= kAll ? kAllRange : range * kStepFactor[step];
      fixed = Clamp(base, (-dist + radius) / limit + double(LoadF(base, kOne)));
    }
    if (on || step > 0)
    {
      ctx.f1.f64 = fixed;
    }
  }
  if (g_report)
  {
    if (!point || !lit)
    {
      ++g_rep_gated;
    }
    else if (!in_pass)
    {
      ++(-double(vz) - radius > range ? g_rep_far : g_rep_other);
    }
    else if (ctx.f1.f64 > double(LoadF(base, kZero)))
    {
      ++g_rep_wanted;
    }
  }
  if (!g_logging)
  {
    return;
  }
  const double zero = LoadF(base, kZero);
  const bool changed = (rare > zero) != (fixed > zero);
  g_changed += changed;
  if (point && in_pass && lit)
  {
    REXLOG_INFO("distant_lights:   #{} light {:08X} type {} world ({:.0f} {:.0f} {:.0f}) r {:.0f} view ({:.0f} {:.0f} "
                "{:.0f}) dist {:.0f} depth {:.0f} off-axis {:.1f} deg | score Rare {:.4f} fixed {:.4f}{}",
                g_light_seq, light, type, LoadF(base, light + kWorldPos), LoadF(base, light + kWorldPos + 4),
                LoadF(base, light + kWorldPos + 8), radius, vx, vy, vz, dist, -vz,
                dist > 0.0 ? std::acos(std::fmin(1.0, std::fabs(vz) / dist)) * 57.29578 : 0.0, rare, fixed,
                changed ? (fixed > zero ? "  [fix draws it]" : "  [fix drops it]") : "");
  }
  else if (point)
  {
    REXLOG_INFO("distant_lights:   #{} light {:08X} type {} gated: pass bit {} lit {} | score {:.4f}", g_light_seq, light,
                type, in_pass, lit, rare);
  }
  ++g_light_seq;
}

// pdz_light_draw_distance and the per-light reading (pdz_light_reach_dump).
//
// Rare's visibility mask, sub_82628F10(view, camera), as read from the code: for every level light but the first,
// bit `view` of the word at light + buffer * 340 is set when (a) one of the light's rooms (count at block + 4 *
// (view + 1), up to 20 room indices at block + 20 + 80 * view; count 21 = no room list) has its bit ([room + 76])
// set in the camera room's precomputed visibility bit array, and (b) the light's sphere is not outside any of the
// camera's six frustum planes (dot(n, pos) - d > radius = outside). No distance, depth or room-count limit.
//   - Steps 1-4: the mask is built with the far plane moved out, then the camera is Rare's again.
//   - Step 5 ("All + hidden rooms"): also, a light whose rooms are not visible from the camera's room but whose
//     sphere is inside the frustum gets its bit set (it is drawn: its light falls on whatever is visible inside
//     its sphere). These lights have no shadows; the room list is what kept them from shining through walls.
REX_EXTERN(sub_82653EB8); // the camera's six frustum planes copied out: (r3 camera, r4 a 168-byte buffer)

REXCVAR_DEFINE_INT32(pdz_light_reach_dump, 0, "Debug",
                     "Testing tool: set to N > 0 to log, for view 0's next light list, every level light that is not "
                     "drawn and which of Rare's cuts dropped it (switched off, rooms, far plane, side planes, range "
                     "score, the cap), as 'pdz_light_reach: dump N'. It resets to 0")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kLevelArray = 0x82E19AE0;  // the level's light pointers (count kLevelLights)
  constexpr uint32_t kWorldRoot = 0x82D6BFD8;   // [+12] -> the world's view data
  constexpr uint32_t kWorldBuffer = 0x82FBE3C0; // its buffer index
  constexpr uint32_t kScore = 1376;             // light + 1376 + 4 * view: the score of the last build

  void Store32(uint8_t *base, uint32_t a, uint32_t v) { StoreRaw32(base, a, _byteswap_ulong(v)); }

  struct Planes
  {
    double n[6][3] = {}, d[6] = {};
  };

  // The camera's six planes as sub_82628F10 gets them (a scratch buffer on the guest stack below the caller's frame).
  Planes ReadPlanes(PPCContext &ctx, uint8_t *base, uint32_t camera)
  {
    const uint64_t r1 = ctx.r1.u64, r3 = ctx.r3.u64, r4 = ctx.r4.u64;
    ctx.r1.u64 = uint64_t(ctx.r1.u32 - 512);
    const uint32_t buf = ctx.r1.u32 + 160;
    ctx.r3.u64 = camera;
    ctx.r4.u64 = buf;
    sub_82653EB8(ctx, base);
    ctx.r1.u64 = r1;
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    Planes p;
    for (int j = 0; j < 6; ++j)
    {
      const uint32_t v = buf + 72 + 16 * j;
      p.n[j][0] = LoadF(base, v);
      p.n[j][1] = LoadF(base, v + 4);
      p.n[j][2] = LoadF(base, v + 8);
      p.d[j] = LoadF(base, v + 12);
    }
    return p;
  }

  struct RoomCtx
  {
    uint32_t bits = 0, rooms = 0;
    bool any = false; // false: the view has no camera room (Rare then sets every light's bit)
  };

  RoomCtx Rooms(const uint8_t *base)
  {
    RoomCtx rc;
    // Sun 22 Nov night (audit): each step of the chain read only where it is mapped (else: no room list, as no room).
    const uint32_t p = Load32(base, kWorldRoot + 12);
    const uint32_t buffer = Load32(base, kWorldBuffer);
    if (!p || !pdz::guestmem::Ok(base, p, 20) || !pdz::guestmem::Ok(base, p + 4 * (buffer + 7), 4))
    {
      return rc;
    }
    const uint32_t cell = Load32(base, p + 4 * (buffer + 7)), holder = Load32(base, p + 16);
    if (!cell || !holder || !pdz::guestmem::Ok(base, cell, 12) || !pdz::guestmem::Ok(base, holder, 8))
    {
      return rc;
    }
    const uint32_t world = Load32(base, holder + 4);
    if (!world || !pdz::guestmem::Ok(base, world, 108))
    {
      return rc;
    }
    const uint32_t entry = Load32(base, world + 104) + Load32(base, cell + 8);
    if (!pdz::guestmem::Ok(base, entry, 48))
    {
      return rc;
    }
    rc.bits = Load32(base, entry + 44);
    rc.rooms = Load32(base, world + 64);
    rc.any = rc.bits != 0 && rc.rooms != 0;
    return rc;
  }

  bool RoomsVisible(const uint8_t *base, const RoomCtx &rc, uint32_t block, uint32_t view, int &count)
  {
    count = int(Load32(base, block + 4 * (view + 1)));
    if (count == 21)
    {
      return true;
    }
    for (int k = 0; k < count && k < 20; ++k)
    {
      // (Sun 22 Nov night (audit): the room table entry, the room and its bit read only where mapped.)
      const uint32_t at = rc.rooms + 4 * Load32(base, block + 20 + 80 * view + 4 * k);
      const uint32_t room = pdz::guestmem::Ok(base, at, 4) ? Load32(base, at) : 0;
      if (!room || !pdz::guestmem::Ok(base, room, 80))
      {
        continue;
      }
      const uint32_t bit = Load32(base, room + 76);
      if (!pdz::guestmem::Ok(base, rc.bits + (bit >> 3), 1))
      {
        continue;
      }
      if (base[rc.bits + (bit >> 3)] & (0x80u >> (bit & 7)))
      {
        return true;
      }
    }
    return false;
  }

  // The first plane (Rare's order: 5 down to 0) the light's sphere is outside of, or -1.
  int PlaneFail(const uint8_t *base, const Planes &p, uint32_t light)
  {
    const double x = LoadF(base, light + kWorldPos), y = LoadF(base, light + kWorldPos + 4),
                 z = LoadF(base, light + kWorldPos + 8), r = LoadF(base, light + kRadius);
    for (int j = 5; j >= 0; --j)
    {
      if (p.n[j][0] * x + p.n[j][1] * y + p.n[j][2] * z - p.d[j] > r)
      {
        return j;
      }
    }
    return -1;
  }

  // The reading (view 0): what the mask decided per light, kept for the list build's log.
  struct Vis
  {
    uint32_t light;
    int rooms_n, plane;
    bool rooms;
  };
  std::vector<Vis> g_vis;
  int g_dump_id = 0, g_far_plane = -1, g_near_plane = -1;
  bool g_cam_ok = false, g_no_room = false;
  double g_cam[3] = {};

  double Det3(const double a[3], const double b[3], const double c[3])
  {
    return a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
  }

  // The camera's position: where three of the four side planes meet.
  void CameraFromPlanes(const Planes &p)
  {
    g_cam_ok = false;
    int side[4], n = 0;
    for (int j = 0; j < 6 && n < 4; ++j)
    {
      if (j != g_far_plane && j != g_near_plane)
      {
        side[n++] = j;
      }
    }
    if (n < 3 || g_far_plane < 0)
    {
      return;
    }
    const double *a = p.n[side[0]], *b = p.n[side[1]], *c = p.n[side[2]];
    const double det = Det3(a, b, c);
    if (std::fabs(det) < 1e-6)
    {
      return;
    }
    const double da = p.d[side[0]], db = p.d[side[1]], dc = p.d[side[2]];
    const double ax[3] = {da, a[1], a[2]}, bx[3] = {db, b[1], b[2]}, cx[3] = {dc, c[1], c[2]};
    const double ay[3] = {a[0], da, a[2]}, by[3] = {b[0], db, b[2]}, cy[3] = {c[0], dc, c[2]};
    const double az[3] = {a[0], a[1], da}, bz[3] = {b[0], b[1], db}, cz[3] = {c[0], c[1], dc};
    g_cam[0] = Det3(ax, bx, cx) / det;
    g_cam[1] = Det3(ay, by, cy) / det;
    g_cam[2] = Det3(az, bz, cz) / det;
    g_cam_ok = std::isfinite(g_cam[0]) && std::isfinite(g_cam[1]) && std::isfinite(g_cam[2]);
  }

  // The list build's half of the reading: every level light, and for the ones not drawn the cut that dropped it.
  void DumpLights(const uint8_t *base, uint32_t cap, double range)
  {
    const uint32_t b = Load32(base, kBuffer) & 1u, slot = b, listed = Load32(base, kListCount + 4 * slot);
    const uint32_t total = std::min<uint32_t>(Load32(base, kLevelLights), 2000);
    std::unordered_map<uint32_t, uint32_t> rank;
    for (uint32_t i = 0; i < listed && i < 2000; ++i)
    {
      rank[Load32(base, kLists + 8000 * slot + 4 * i)] = i;
    }
    const int step = Step();
    REXLOG_INFO("pdz_light_reach: dump {}: step {} ({}), camera ({:.0f} {:.0f} {:.0f}){}, Rare's range {:.0f} cm, level "
                "lights {}, in the list {} (cap {}), looking along ({:.2f} {:.2f} {:.2f}); {}{}{}",
                g_dump_id, step, kStepNames[step], g_cam[0], g_cam[1], g_cam[2], g_cam_ok ? "" : " (not found)", range,
                total, listed, cap, g_fwd[0], g_fwd[1], g_fwd[2], g_fog_text,
                g_no_room ? "; THE VIEW HAS NO CAMERA ROOM (Rare sets every light's bit)" : "",
                step >= kHidden ? fmt::format("; step 5 added {} hidden-room light(s)", g_hidden_added) : std::string());
    int n_drawn = 0, n_dir = 0, n_off = 0, n_unused = 0, n_black = 0, n_rooms = 0, n_far = 0, n_near = 0, n_side = 0,
        n_score = 0, n_cap = 0, n_other = 0, n_rooms_in_view = 0, n_rooms_beam = 0, n_drawn_beam = 0;
    for (uint32_t i = 0; i < total; ++i)
    {
      const uint32_t light = Load32(base, kLevelArray + 4 * i);
      if (!light)
      {
        continue;
      }
      const uint32_t type = Load32(base, light + kType);
      if (type == 1)
      {
        ++n_dir;
        continue;
      }
      const auto r = rank.find(light);
      if (r != rank.end())
      {
        ++n_drawn;
        n_drawn_beam += HasBeam(base, light);
        const uint32_t c = light + b * 16 + kRgb;
        const double lx = LoadF(base, light + kWorldPos), ly = LoadF(base, light + kWorldPos + 4),
                     lz = LoadF(base, light + kWorldPos + 8);
        REXLOG_INFO("pdz_light_reach: dump {}:   DRAWN rank {} light {} type {} at ({:.0f} {:.0f} {:.0f}) radius {:.0f} "
                    "colour {:.2f} {:.2f} {:.2f}, {:.0f} cm from the camera",
                    g_dump_id, r->second, i, type == 0 ? "spot" : "point", lx, ly, lz, LoadF(base, light + kRadius),
                    LoadF(base, c), LoadF(base, c + 4), LoadF(base, c + 8),
                    g_cam_ok ? std::sqrt((lx - g_cam[0]) * (lx - g_cam[0]) + (ly - g_cam[1]) * (ly - g_cam[1]) +
                                         (lz - g_cam[2]) * (lz - g_cam[2]))
                             : -1.0);
        continue;
      }
      const Vis *v = i >= 1 && i - 1 < g_vis.size() && g_vis[i - 1].light == light ? &g_vis[i - 1] : nullptr;
      const bool bit = (Load32(base, light + b * 340) & 1u) != 0;
      const uint32_t rgb = light + b * 16 + kRgb;
      const bool lit = LoadF(base, rgb) > 0.0f || LoadF(base, rgb + 4) > 0.0f || LoadF(base, rgb + 8) > 0.0f;
      const double x = LoadF(base, light + kWorldPos), y = LoadF(base, light + kWorldPos + 4),
                   z = LoadF(base, light + kWorldPos + 8), radius = LoadF(base, light + kRadius);
      const double dist = g_cam_ok ? std::sqrt((x - g_cam[0]) * (x - g_cam[0]) + (y - g_cam[1]) * (y - g_cam[1]) +
                                               (z - g_cam[2]) * (z - g_cam[2]))
                                   : -1.0;
      const float score = LoadF(base, light + kScore);
      std::string why;
      if (Load32(base, light + (b + 356) * 4) == 0)
      {
        why = "SWITCHED OFF (its enable word is 0: game logic / script, not a draw-distance cut)";
        ++n_off;
      }
      else if (int32_t(Load32(base, light + kActive)) < 1)
      {
        why = "NOT IN USE (its use count is 0)";
        ++n_unused;
      }
      else if (!lit)
      {
        why = "BLACK (colour 0)";
        ++n_black;
      }
      else if (!bit && v && !v->rooms)
      {
        const bool in_view = v->plane < 0;
        why = fmt::format("ROOMS (none of its {} room(s) is visible from the camera's room); its sphere is {} the "
                          "view{}",
                          v->rooms_n, in_view ? "INSIDE" : "outside", HasBeam(base, light) ? "; HAS A BEAM" : "");
        ++n_rooms;
        n_rooms_in_view += in_view;
        n_rooms_beam += in_view && HasBeam(base, light);
      }
      else if (!bit && v && v->plane == g_far_plane)
      {
        why = "FAR PLANE (range / fog end)";
        ++n_far;
      }
      else if (!bit && v && v->plane == g_near_plane)
      {
        why = "NEAR PLANE (behind the camera)";
        ++n_near;
      }
      else if (!bit && v && v->plane >= 0)
      {
        why = fmt::format("SIDE PLANE {} (outside the picture)", v->plane);
        ++n_side;
      }
      else if (!bit)
      {
        why = "mask bit clear (cause not found)";
        ++n_other;
      }
      else if (!(score > 0.0f))
      {
        why = fmt::format("RANGE SCORE ({:.4f} <= 0)", score);
        ++n_score;
      }
      else
      {
        why = fmt::format("THE CAP (score {:.4f}, more than {} lights wanted in)", score, cap);
        ++n_cap;
      }
      REXLOG_INFO("pdz_light_reach: dump {}:   light {} type {} at ({:.0f} {:.0f} {:.0f}) radius {:.0f} colour {:.2f} "
                  "{:.2f} {:.2f}, {:.0f} cm from the camera: {}",
                  g_dump_id, i, type == 0 ? "spot" : "point", x, y, z, radius, LoadF(base, rgb), LoadF(base, rgb + 4),
                  LoadF(base, rgb + 8), dist, why);
    }
    REXLOG_INFO("pdz_light_reach: dump {} SUMMARY: drawn {} ({} with a beam), not drawn by cut: rooms {} ({} of them "
                "with their sphere inside the view, {} of those with a beam), side planes {}, near plane {}, far plane "
                "(range) {}, range score {}, the cap {}, switched off {}, not in use {}, black {}, unknown {}; "
                "directional {}; hidden beams put back {}",
                g_dump_id, n_drawn, n_drawn_beam, n_rooms, n_rooms_in_view, n_rooms_beam, n_side, n_near, n_far, n_score,
                n_cap, n_off, n_unused, n_black, n_other, n_dir, g_beams_added);
  }
} // namespace

REX_HOOK_RAW(sub_82628F10)
{
  const int step = Step();
  const uint32_t view = ctx.r3.u32, camera = ctx.r4.u32;
  bool dump = false;
  if (view == 0 && camera != 0 && REXCVAR_GET(pdz_light_reach_dump) > 0)
  {
    g_dump_id = REXCVAR_GET(pdz_light_reach_dump);
    REXCVAR_SET(pdz_light_reach_dump, 0);
    dump = true;
  }
  const bool beams = Beams();
  if (camera == 0 || (step == 0 && !dump && !beams))
  {
    __imp__sub_82628F10(ctx, base);
    return;
  }
  const uint32_t far_raw = LoadRaw32(base, camera + kCamFar);
  const float far_rare = LoadF(base, camera + kCamFar);
  const float far_new = step == 0 ? far_rare
                        : step >= kAll ? std::fmax(far_rare * 4.0f, kAllFar) : far_rare * float(kStepFactor[step]);
  if (!(far_rare > 0.0f) || !std::isfinite(far_new))
  {
    __imp__sub_82628F10(ctx, base);
    return;
  }
  if (step > 0)
  {
    StoreF(base, camera + kCamFar, far_new);
    base[camera + kCamDirty] = 1;
  }
  __imp__sub_82628F10(ctx, base);
  bool touched = step > 0;
  const uint32_t b = Load32(base, kBuffer);
  if ((step >= kHidden || beams || dump) && view < 4 && b <= 1)
  {
    const Planes planes = ReadPlanes(ctx, base, camera);
    const RoomCtx rc = Rooms(base);
    if (dump)
    {
      // Which plane is the far one: the one that moves with camera + 80. The near one faces the other way.
      StoreF(base, camera + kCamFar, far_new * 2.0f);
      base[camera + kCamDirty] = 1;
      touched = true;
      const Planes moved = ReadPlanes(ctx, base, camera);
      g_far_plane = g_near_plane = -1;
      for (int j = 0; j < 6; ++j)
      {
        if (std::fabs(moved.d[j] - planes.d[j]) > 1.0)
        {
          g_far_plane = j;
        }
      }
      for (int j = 0; j < 6 && g_far_plane >= 0; ++j)
      {
        const double dot = planes.n[j][0] * planes.n[g_far_plane][0] + planes.n[j][1] * planes.n[g_far_plane][1] +
                           planes.n[j][2] * planes.n[g_far_plane][2];
        if (j != g_far_plane && dot < -0.99)
        {
          g_near_plane = j;
        }
      }
      CameraFromPlanes(planes);
      if (g_far_plane >= 0)
      {
        g_fwd[0] = planes.n[g_far_plane][0];
        g_fwd[1] = planes.n[g_far_plane][1];
        g_fwd[2] = planes.n[g_far_plane][2];
      }
      // The view's fog (sub_8220D988's record for this view and buffer, and the fog definition the view points at).
      {
        const uint32_t vobj = ctx.r30.u32;
        const uint32_t rec = 0x82E1C490 + 816 * (2 * view + (Load32(base, kWorldBuffer) & 1u));
        std::string def = "definition not read";
        if (vobj && Load32(base, vobj + 64) == view && Load32(base, vobj + 60) == camera)
        {
          const uint32_t fd = Load32(base, vobj + 1236);
          def = fd ? fmt::format("definition {:08X}: colour {} {} {}, start {:.0f}, end {:.0f}", fd, base[fd + 4],
                                 base[fd + 5], base[fd + 6], LoadF(base, fd + 8), LoadF(base, fd + 12))
                   : std::string("no fog definition on the view");
        }
        g_fog_text = fmt::format("fog: {} (this frame's record: start {:.0f}, end {:.0f}); {}; camera far plane {:.0f}",
                                 base[rec + 660] ? "ON" : "off", LoadF(base, rec + 668), LoadF(base, rec + 672), def,
                                 far_rare);
      }
      g_vis.clear();
      g_no_room = !rc.any;
    }
    int added = 0, beams_added = 0;
    const uint32_t total = std::min<uint32_t>(Load32(base, kLevelLights), 2000);
    for (uint32_t i = 1; i < total; ++i)
    {
      const uint32_t light = Load32(base, kLevelArray + 4 * i);
      Vis v{light, 21, -1, true};
      if (light)
      {
        const uint32_t block = light + b * 340;
        const bool beam = HasBeam(base, light);
        // Without the reading, only the lights that can be put back are looked at: bit clear, and every hidden-room
        // light at "all hidden rooms" or only those with a beam.
        const bool wanted = step >= kHidden || (beams && beam);
        if (dump || (wanted && rc.any && (Load32(base, block) & (1u << view)) == 0))
        {
          v.rooms = rc.any ? RoomsVisible(base, rc, block, view, v.rooms_n) : true;
          v.plane = PlaneFail(base, planes, light);
          if (wanted && !v.rooms && v.rooms_n >= 1 && v.rooms_n <= 20 && v.plane < 0)
          {
            Store32(base, block, Load32(base, block) | (1u << view));
            ++added;
            beams_added += beam;
          }
        }
      }
      if (dump)
      {
        g_vis.push_back(v);
      }
    }
    if (view == 0)
    {
      g_hidden_added = added;
      g_beams_added = beams_added;
    }
    if (dump)
    {
      g_dump_stage = 1;
    }
  }
  if (touched)
  {
    StoreRaw32(base, camera + kCamFar, far_raw);
    base[camera + kCamDirty] = 1;
    ctx.r3.u64 = camera;
    sub_826536F8(ctx, base);
  }
}

REX_HOOK_RAW(sub_826048F0) { DrawLight(ctx, base, __imp__sub_826048F0); }
REX_HOOK_RAW(sub_82605488) { DrawLight(ctx, base, __imp__sub_82605488); }

namespace pdz::beam_fade
{
  bool Before(PPCContext &ctx, uint8_t *base); // render/beam_fade_Mon16Nov.cpp (pdz_far_lighting_beams_no_fade)
  void After(PPCContext &ctx);
} // namespace pdz::beam_fade

// The lamp halo of a light that is fading in is drawn smaller by the same factor (Rare's two size factors).
REX_HOOK_RAW(sub_82608510)
{
  const bool no_fade = pdz::beam_fade::Before(ctx, base); // Rare's rank shrink undone first; the fade-in below stays
  if (Step() != 0)
  {
    const uint32_t light = ctx.r7.u32, view = Load32(base, ctx.r1.u32 + 260), b = Load32(base, kRenderBuffer);
    if (light && view <= 3 && b <= 1)
    {
      const double f = Fade(base, light, view, b);
      ctx.f3.f64 *= f;
      ctx.f4.f64 *= f;
    }
  }
  __imp__sub_82608510(ctx, base);
  if (no_fade)
  {
    pdz::beam_fade::After(ctx);
  }
}

namespace pdz::light_reach
{
  // F6 (bind_light_draw_distance) cycles the far lighting presets (pdz_light_preset); the step is shown in the window
  // title, a short label and the log.
  void Init(rex::ui::Window *window)
  {
    g_window = window;
    g_title = std::string("PDZRestored ") + PDZ_RESTORED_VERSION;
    rex::ui::RegisterBind("bind_light_draw_distance", "F6",
                          "Testing (needs pdz_far_lighting_key): far lighting, next step (Rare, no cap, + full lighting, + hidden "
                          "beams, + all hidden rooms)", []
                          {
      static auto last = std::chrono::steady_clock::time_point{};
      const auto now = std::chrono::steady_clock::now();
      if (now - last < std::chrono::milliseconds(300))
      {
        return; // a held key repeats
      }
      last = now;
      if (REXCVAR_GET(pdz_far_lighting_key))
      {
        Cycle("the key bind");
      } });
  }
} // namespace pdz::light_reach
