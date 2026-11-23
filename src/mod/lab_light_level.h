// Laboratory light level at a point (prototype, pure functions, not wired into anything yet): how much of the
// scene light that reaches a position is still on, measured with Rare's own light falloff. See lab_light_level.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace lab_light_level
{
  // One scene light, as Rare's light object holds it after the darkbackground loader (sub_821F7A40) built it.
  // Guest offsets on the light object (TU3) for when this is wired to the live list (lab_restorations' kLightArray /
  // kLightCount): type +0x2A8, outer radius +0x2AC, inner radius +0x2B0, cos(inner half angle) +0x2B4, cos(outer
  // half angle) +0x2B8, colour +0x2C0, position +0x2D0, -direction +0x2E4, intensity +0x598 (0 off .. 1 on, fades).
  struct Light
  {
    uint8_t type = 2;              // 0 spot, 2 point; anything else (1 the sun, 3 ambient) never counts
    float pos[3] = {0, 0, 0};      // cm (+0x2D0)
    float dir[3] = {0, -1, 0};     // spots: where the spot points, unit (= the record's direction = -(+0x2E4))
    float outer = 0.0f;            // cm (+0x2AC): no light at or beyond it
    float inner = 0.0f;            // cm (+0x2B0): full light inside it (0 in most Laboratory lights)
    float cos_inner = 1.0f;        // spots (+0x2B4)
    float cos_outer = 0.0f;        // spots (+0x2B8)
    float colour[3] = {1, 1, 1};   // record colour (+0x2C0; Rare multiplies it by the intensity +0x598)
  };

  // Rare's distance term for one light at distance d (cm): the light pre-pass constant c3.xy written by
  // sub_826048F0 (point) / sub_82605488 (spot) and the shader's saturate(d * c3.x + c3.y).
  float DistanceTerm(const Light &light, float d);

  // Rare's spot cone term (c3.zw, c4 of sub_82605488): 1 for point lights. cos_angle = cosine between the spot's
  // direction and the direction from the light to the point.
  float ConeTerm(const Light &light, float cos_angle);

  // Luma of a colour (Rec. 601 weights): one number per light for "how bright".
  float Luma(const float colour[3]);

  // What one light at full intensity contributes at point p: luma x distance term x cone term. No surface normal
  // (N.L is left out: a character is lit from every side) and no occlusion (Rare's light pre-pass has none for these
  // lights; see notes A2).
  float Contribution(const Light &light, const float p[3]);

  struct Level
  {
    float lit = 0.0f;      // sum over the lights with their current intensity (+ ambient_now)
    float full = 0.0f;     // the same sum with every light at full intensity (+ ambient_full)
    float fraction = 1.0f; // lit / full; 1 when full is (almost) 0: no switchable light reaches the point at all
  };

  // The light level at one point. intensity[i] is light i's current intensity (0 off .. 1 on; Rare's +0x598, or
  // 0 / 1 from the on byte); nullptr = every light on. ambient_now / ambient_full: an optional constant term (luma of
  // Rare's ambient colour now / before the blackout; 0 / 0 = dynamic lights only).
  Level LightLevel(const Light *lights, size_t count, const float *intensity, const float p[3],
                   float ambient_now = 0.0f, float ambient_full = 0.0f);

  // The same over several sample points (feet, chest, head ...): the sums are added before dividing, so one point
  // just outside a light's radius does not flip the answer.
  Level LightLevelSampled(const Light *lights, size_t count, const float *intensity, const float (*points)[3],
                          size_t point_count, float ambient_now = 0.0f, float ambient_full = 0.0f);
} // namespace lab_light_level
