// Laboratory dark patches (pdz_lab_dark_patches): see lab_dark_patches.cpp.
#pragma once

#include <cstdint>

#include <rex/ppc/func.h>

namespace lab_dark
{
  // pdz_lab_dark_patches is on and the Laboratory runs.
  bool On();
  // pdz_lab_dark_patches is on (any level): a stealth zone row may use the light measure (Fraction / MeterFactor) for
  // its meter only (Nightclub helper, Sun 15 Nov).
  bool MeasureSwitch();
  // The light at Jo (0 dark .. 1 bright), measured at most once a second (AI clock `now`). With pdz_lab_dark_absolute
  // (default): the light at Jo / the fixed bright reference pdz_lab_dark_reference; without: / the light at the same
  // spot with every lamp on (rounds 2 to 5).
  float Fraction(const uint8_t *base, float now);
  // Mansion helper (Sun 15 Nov): a zone level's extra light at Jo (summed over the three samples; the moon where it is
  // not blocked) and its reference (0 = pdz_lab_dark_reference), used by the next samples (never on the Laboratory).
  void SetZoneLight(float extra, float reference);
  // The last sample: lit (ambient x 3 + lamps + extra), the ambient luma, the extra, the reference, the fraction.
  // False before the first sample.
  bool LastReading(float &lit, float &ambient, float &extra, float &reference, float &fraction);
  // A guard's sight range of Jo is multiplied by this (1 in full light; near_cm / range in the dark).
  float RangeScale(float range);
  // The same range scale for something lying at `pos` (a body): the light there instead of the light at Jo.
  float RangeScaleAt(const uint8_t *base, const float pos[3], float range);
  // The stealth meter's light factor (0.3 dark .. 1 lit).
  float MeterFactor();
  // Round 7: Rare's generator blackout is on (its ambient is black). Then RangeScale / RangeScaleAt answer 1 and the
  // stealth meter uses Rare's own dark bit: Rare's darkness only, nothing of ours on top.
  bool RareBlackout();
  // Round 8: the lights changed all at once (the power return): the light at Jo is measured again at once, not up to
  // a second later.
  void Resample();
  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start (first load, mission restart, reload): the measure's
  // memory of the attempt before (level-start ambient, lamps seen on, blackout flag) is dropped at the next sample,
  // which is taken at once. The measure only: nothing drawn changes. Any thread.
  void LevelReset();
  // Round 7: the reach (outer radius, cm) of the scene lamp at `pos` (the largest within 350 cm of it; 0: none there).
  float LampReach(const uint8_t *base, const float pos[3]);
  // Once a frame on the game thread (lab_stealth.cpp's sight update): the light sample and the test tool.
  void Tick(PPCContext &ctx, uint8_t *base, float now);
  // After Rare's shot-on-a-light-surface sub_825F3978 (lab_light_shot_log.cpp): the other lamps within reach go out.
  void AfterShot(PPCContext &ctx, uint8_t *base, const float hit[3], uint32_t prop, bool rare_put_one_out);

  // ---------------------------------------------------------------------------------------------------------------
  // Local darkness (pdz_lab_dark_visual): a lamp that was on and is now out takes most of the ambient light away
  // within its reach (its outer radius, fading out to kVisualFalloff x the radius). The same rule is drawn by the
  // native renderer (render/native/native_labdark.cpp) and counted by the light measure above.
  // ---------------------------------------------------------------------------------------------------------------
  constexpr float kVisualFalloff = 1.5f; // the darkening ends at this x the lamp's outer radius
  constexpr int kVisualMaxLamps = 32;    // lamps one view draws (the nearest to the camera)

  // pdz_lab_dark_patches and pdz_lab_dark_visual are on, the Laboratory is loaded (also while paused) and the native
  // renderer draws.
  bool VisualOn(const uint8_t *base);
  // The part of the ambient light left inside a lamp that is out (1 - pdz_lab_dark_visual_strength, 0..1).
  float VisualStrength();
  // The ambient light kept at a world position (1 = untouched, VisualStrength() = inside a lamp that is out):
  // the factor the native renderer multiplies the ambient pass with there. 1 while VisualOn is false. Any thread.
  float AmbientKeep(const uint8_t *base, const float pos[3]);

  // One view's lamps for the native renderer, in Rare's view space (the space the light pass shaders rebuild from the
  // depth buffer), made by the hook on Rare's ambient / sun pass sub_826043E0 for the draws it issues.
  struct VisualFrame
  {
    uint32_t count = 0;
    float strength = 1.0f;
    float lamp[kVisualMaxLamps][4];   // view-space position, outer radius
    float weight[kVisualMaxLamps];    // 0..1: how far the lamp has gone out (1 - its brightness)
  };
  // The frame of the ambient pass that recorded draw `sequence` (shadow ring sequence). False: nothing to draw.
  // Called by the native draw path (the record thread) for the ambient pass's draw only.
  bool VisualFrameFor(uint32_t sequence, VisualFrame &out);
  // True while local darkness is wanted (the native pass builds its pipeline at the first ambient draw then, before
  // any lamp is out). One relaxed load.
  bool VisualArmed();
} // namespace lab_dark
