// Laboratory light level at a point (prototype; pure functions over a light table, no guest memory, no hooks, not
// in the build list yet and not wired into guard sight). Purpose: the "darkness where Jo stands" design (dark
// patches from shot-out lights, the blackout, our blackout coverage): compare the light reaching a position with the
// same position with every light on, using the falloff Rare's renderer draws with, so what looks dark on screen is
// what we would call dark.
//
// Rare's light falloff (TU3; the light research's note A2, falloff):
// - The scene lights are drawn by the light pre-pass: sub_826042F0 dispatches on the light type (+0x2A8): 2 point ->
//   sub_826048F0, 0 spot -> sub_82605488, 1 the sun -> sub_826043E0 (which also uploads the ambient, sub_82601E50).
// - Distance: both write pixel constant c3.x = 1 / (inner - outer) when inner < outer, else -1 / outer, and
//   c3.y = -c3.x * outer (826049B0..826049F0, 82605580..826055B8); the light shader takes
//   saturate(d * c3.x + c3.y) with d the true distance (rcp(rsqrt(d^2))). So the light falls off LINEARLY from 1 at
//   the inner radius (0 for 743 of the 784 Laboratory lights) to 0 at the outer radius.
// - Spot cone: c3.z = 1 / (cos_inner - cos_outer) (else 1 / cos_inner), c3.w = -c3.z * cos_outer
//   (826055BC..826055F4); the shader takes saturate(dot(L, c4) * c3.z + c3.w), L the unit vector from the surface to
//   the light and c4 the light's stored direction (+0x2E4, the negated record direction). The loader
//   (sub_821F7A40, 821F7CDC..821F7D44) makes the outer half angle 0.5 x record +0x6C and the inner half angle
//   max(0, outer - 0.5 x record +0x70).
// - Brightness: c1 = colour (+0x2C0) x intensity (+0x598), the animation update sub_826060B0 (82606200..82606234).
// - The surface then gets c1 x att x cone x N.L (and a specular term). No occlusion term except the shadow maps of
//   lights carrying shadow data (record +0x74; in the Laboratory only the four white prison-cell lights 495 / 497 /
//   498 / 508), so light goes through walls exactly as Rare draws it.
// - Not the same as Rare's "nearest light" pick for a shot light surface (sub_825F3CD8: best 1 - d^2 / outer^2):
//   that is a choice of light, not a brightness.
#include "lab_light_level.h"

#include <algorithm>
#include <cmath>

namespace lab_light_level
{
  namespace
  {
    float Saturate(float v)
    {
      return std::clamp(v, 0.0f, 1.0f);
    }

    constexpr float kMinFull = 1e-6f; // below this nothing switchable reaches the point
  } // namespace

  float DistanceTerm(const Light &light, float d)
  {
    if (!(light.outer > 0.0f))
    {
      return 0.0f;
    }
    // sub_826048F0: inner < outer -> 1 / (inner - outer); else 1 / -outer. Then y = -x * outer.
    const float k = light.inner < light.outer ? 1.0f / (light.inner - light.outer) : -1.0f / light.outer;
    return Saturate(d * k - k * light.outer);
  }

  float ConeTerm(const Light &light, float cos_angle)
  {
    if (light.type != 0)
    {
      return 1.0f;
    }
    // sub_82605488: cos_inner > cos_outer -> 1 / (cos_inner - cos_outer); else 1 / cos_inner. Then w = -z * cos_outer.
    const float k = light.cos_inner > light.cos_outer ? 1.0f / (light.cos_inner - light.cos_outer)
                                                     : (light.cos_inner != 0.0f ? 1.0f / light.cos_inner : 0.0f);
    return Saturate(cos_angle * k - k * light.cos_outer);
  }

  float Luma(const float colour[3])
  {
    return 0.299f * colour[0] + 0.587f * colour[1] + 0.114f * colour[2];
  }

  float Contribution(const Light &light, const float p[3])
  {
    if (light.type != 0 && light.type != 2)
    {
      return 0.0f; // the sun and the ambient record never count (they cannot be shot out; design note)
    }
    const float v[3] = {p[0] - light.pos[0], p[1] - light.pos[1], p[2] - light.pos[2]};
    const float d = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (d >= light.outer)
    {
      return 0.0f;
    }
    float term = DistanceTerm(light, d);
    if (light.type == 0 && d > 1e-3f)
    {
      // dot(L, c4) with L = (light - p) / d and c4 = -dir is dot(p - light, dir) / d.
      const float cos_angle = (v[0] * light.dir[0] + v[1] * light.dir[1] + v[2] * light.dir[2]) / d;
      term *= ConeTerm(light, cos_angle);
    }
    return term > 0.0f ? Luma(light.colour) * term : 0.0f;
  }

  namespace
  {
    void Accumulate(const Light *lights, size_t count, const float *intensity, const float p[3], Level &out)
    {
      for (size_t i = 0; i < count; ++i)
      {
        const float c = Contribution(lights[i], p);
        if (c <= 0.0f)
        {
          continue;
        }
        out.full += c;
        out.lit += c * (intensity ? Saturate(intensity[i]) : 1.0f);
      }
    }

    void Finish(Level &out)
    {
      out.fraction = out.full > kMinFull ? Saturate(out.lit / out.full) : 1.0f;
    }
  } // namespace

  Level LightLevel(const Light *lights, size_t count, const float *intensity, const float p[3], float ambient_now,
                   float ambient_full)
  {
    Level out;
    out.lit = std::max(ambient_now, 0.0f);
    out.full = std::max(ambient_full, 0.0f);
    Accumulate(lights, count, intensity, p, out);
    Finish(out);
    return out;
  }

  Level LightLevelSampled(const Light *lights, size_t count, const float *intensity, const float (*points)[3],
                          size_t point_count, float ambient_now, float ambient_full)
  {
    Level out;
    for (size_t k = 0; k < point_count; ++k)
    {
      out.lit += std::max(ambient_now, 0.0f);
      out.full += std::max(ambient_full, 0.0f);
      Accumulate(lights, count, intensity, points[k], out);
    }
    Finish(out);
    return out;
  }
} // namespace lab_light_level
