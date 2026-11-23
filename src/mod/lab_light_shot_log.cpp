// pdz_lab_light_shot_log (diagnostics, default off): which light Rare switches off when Jo shoots a light.
//
// Rare's path (research notes A4, 9-10 Nov): an impact on world geometry with the light material (SurfaceType_Light,
// 38) calls sub_825F3978(r3 = &hit position, r4 = the prop hit or 0); it picks a light with sub_825F3CD8 (the best
// 1 - d^2 / R^2 over every eligible light, on or off) and fades it out with sparks if it was on. A blast goes through
// sub_825F3A10 instead. This file only logs: the hit, the prop, the light picked (its position and on byte) and
// blasts. Nothing is changed by the log; with pdz_lab_dark_patches on, lab_dark::AfterShot then puts out the other
// eligible lamps near the hit (lab_dark_patches.cpp).
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "guest_function.h" // (stealth round 5: the prop's health virtual)
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok
#include "lab_dark_patches.h"
#include "lab_stealth.h" // stealth rollout: lamp lures on the zone levels

REXCVAR_DEFINE_BOOL(pdz_lab_light_shot_log, false, "Diagnostics",
                    "Log every shot that reaches Rare's light switch-off (hit position, prop, the light it picks, "
                    "that light's position and on state) and every blast that puts lights out")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_825F3978); // a shot hit a light surface (r3 = &hit position, r4 = prop or 0)

namespace pdz::restored_sounds
{
  bool LightSparksOn();                                                // restored_sounds_Tue17Nov.cpp
  void LightSparks(PPCContext &ctx, uint8_t *base, const float at[3]); // the sparks' sound at the hit
} // namespace pdz::restored_sounds
REX_EXTERN(__imp__sub_825F3CD8); // pick the light for a hit -> r3 = light or 0
REX_EXTERN(__imp__sub_825F3A10); // a blast puts lights out
REX_EXTERN(__imp__sub_825EE9D0); // the prop damage component's bullet hit (r3 = component; [r3] = prop)

namespace
{
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
  bool Ptr(uint32_t a) { return a >= 0x00010000u && a < 0xFFFF0000u; }
  constexpr uint32_t kLightPosition = 0x2D0;
  constexpr uint32_t kLightOnTable = 0x82E1BA20; // [this] + 0x5AC + ... : the on byte (A4 note)
  thread_local bool t_in_shot = false;
  thread_local uint32_t t_picked = 0; // the light Rare's pick returned during this shot
  thread_local bool t_picked_on = false; // ... and it was on (state 1 / 2), so Rare's shot path puts it out
  // Subway helper: one lure per lamp: a second report of the same lamp within 1 s and 3 m (both paths of one shot) is
  // not a new incident.
  bool LureFresh(const float at[3])
  {
    static float last[3] = {0.0f, 0.0f, 0.0f};
    static std::chrono::steady_clock::time_point when{};
    static bool any = false;
    const auto now = std::chrono::steady_clock::now();
    const float dx = at[0] - last[0], dy = at[1] - last[1], dz = at[2] - last[2];
    const bool same = any && now - when < std::chrono::seconds(1) && dx * dx + dy * dy + dz * dz < 300.0f * 300.0f;
    last[0] = at[0];
    last[1] = at[1];
    last[2] = at[2];
    when = now;
    any = true;
    return !same;
  }
  // Stealth round 5 (fix 5): a prop's health through its own virtual (vtable +332, r4 = 0, the call Rare's bullet-hit
  // handler starts with); -1 when the slot is no recompiled function.
  float PropHealth(PPCContext &ctx, uint8_t *base, uint32_t prop)
  {
    // Sun 22 Nov night (audit): the prop is mapped and its vtable is in the game image (after Rare's handler the prop
    // may be torn down); GuestFunction refuses _purecall.
    if (!pdz::guestmem::Ok(base, prop, 4))
    {
      return -1.0f;
    }
    const uint32_t vtable = Load32(base, prop);
    const bool image = vtable >= 0x82000000u && vtable < 0x83000000u;
    PPCFunc *fn = image ? pdz::GuestFunction(Load32(base, vtable + 332), "a prop's health read") : nullptr;
    if (!fn)
    {
      return -1.0f;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = prop;
    frame.ctx.r4.u64 = 0;
    fn(frame.ctx, base);
    const float h = float(frame.ctx.f1.f64);
    return h == h ? h : -1.0f;
  }
} // namespace

REX_HOOK_RAW(sub_825F3978)
{
  const bool dark = lab_dark::On();
  // Stealth rollout: on a zone level (Nightclub, Subway, Mansion with the layer on) a lamp Rare's own pick puts out is
  // a lamp lure for the guards of the zone (lab_stealth::LightIncident). The Laboratory keeps lab_dark's path above.
  const bool lure = !dark && lab_stealth::LampLureOn(base);
  const bool sparks_sound = pdz::restored_sounds::LightSparksOn(); // (Tue 17 Nov: the sparks' own sound)
  if (!REXCVAR_GET(pdz_lab_light_shot_log) && !dark && !lure && !sparks_sound)
  {
    __imp__sub_825F3978(ctx, base);
    return;
  }
  const uint32_t hit = ctx.r3.u32, prop = ctx.r4.u32;
  // Rare makes the sparks (effect 13 at the hit, 825F39D8-825F39F4) exactly when the light it takes is on (state 1
  // or 2): with a prop, the prop's linked light (prop +812); without, its pick (noted by the sub_825F3CD8 hook).
  bool prop_light_on = false;
  if (sparks_sound && Ptr(prop))
  {
    const uint32_t linked = Load32(base, prop + 0x32C);
    const uint32_t state = Ptr(linked) ? Load32(base, linked + 0x590 + 4 * Load32(base, 0x82FBE3C0)) : 0;
    prop_light_on = state == 1 || state == 2;
  }
  const uint32_t lr_in = uint32_t(ctx.lr); // (round 3c: Rare's impact routine that reports the hit)
  float at[3] = {0.0f, 0.0f, 0.0f};
  if (Ptr(hit))
  {
    at[0] = LoadF(base, hit);
    at[1] = LoadF(base, hit + 4);
    at[2] = LoadF(base, hit + 8);
  }
  if (Ptr(hit) && REXCVAR_GET(pdz_lab_light_shot_log))
  {
    REXLOG_INFO("pdz_lab_light_shot: light-surface hit at ({:.1f}, {:.1f}, {:.1f}), prop {:08X}, from {:08X}",
                LoadF(base, hit), LoadF(base, hit + 4), LoadF(base, hit + 8), prop, uint32_t(ctx.lr));
  }
  t_in_shot = true;
  t_picked = 0;
  t_picked_on = false;
  if (lure && Ptr(prop) && lab_stealth::LampPropLureOn(base))
  {
    // Subway helper: with a prop Rare does not pick (825F3978-825F399C): it takes the prop's linked scene light
    // (prop +0x32C; the light fittings warehouse_light, striplight, streetlight, tunnellight) and puts it out if its
    // state is 1 or 2 (825F39A8-825F39C8: light +0x590 + 4 * [82FBE3C0]). The same test, before Rare's call.
    const uint32_t linked = Load32(base, prop + 0x32C);
    if (Ptr(linked))
    {
      const uint32_t state = Load32(base, linked + 0x590 + 4 * Load32(base, 0x82FBE3C0));
      t_picked = linked;
      t_picked_on = state == 1 || state == 2;
    }
  }
  // Round 3c (Sun 15 Nov night, fix 2): the picked light's REAL state after Rare's call. Rare's pick takes the
  // best-scoring light for any hit on a light surface, however far from a fitting, and sub_82602648 may leave it on:
  // "Rare's pick was on" alone never means a lamp went out.
  __imp__sub_825F3978(ctx, base);
  t_in_shot = false;
  if (sparks_sound && Ptr(hit) && (Ptr(prop) ? prop_light_on : (Ptr(t_picked) && t_picked_on)))
  {
    pdz::restored_sounds::LightSparks(ctx, base, at); // (Rare's sparks were made at this hit: their sound)
  }
  const uint32_t picked = t_picked; // (Rare's pick, set by the sub_825F3CD8 hook during the call, or the prop's linked light)
  const uint32_t state_before = t_picked_on ? 1u : 0u;
  const uint32_t state_after = Ptr(picked) ? Load32(base, picked + 0x590 + 4 * (Load32(base, kLightOnTable) & 1)) : 0;
  const bool really_out = t_picked_on && Ptr(picked) && state_after != 1 && state_after != 2;
  if (dark && Ptr(hit))
  {
    // (The Laboratory: its lamps fade for real; counted at once, as before, but only when the pick is no longer on.)
    lab_dark::AfterShot(ctx, base, at, prop, really_out);
  }
  else if (lure && Ptr(hit) && Ptr(picked) && t_picked_on)
  {
    // (A lamp Rare's pick found on, a prop's linked light too (Subway's fittings). Nothing else is switched off: the
    // local darkness is the Laboratory's alone. The stealth layer watches the light and counts it only once it is
    // really out: never a light Rare left on, never a Mansion candle. The Subway dedupe as before.)
    float lamp[3] = {at[0], at[1], at[2]};
    for (int k = 0; k < 3; ++k)
    {
      const float v = LoadF(base, picked + kLightPosition + 4 * k);
      lamp[k] = v == v ? v : lamp[k];
    }
    if (!lab_stealth::LampPropLureOn(base) || LureFresh(lamp)) // (the dedupe: Subway only)
    {
      lab_stealth::LampShot(ctx, base, picked, at, prop, lr_in, state_before, state_after, 1);
    }
  }
}

// Subway helper: the bullet-hit handler of the prop damage component (r3 = the component, [r3] = the prop) releases the
// prop's OWN light first (prop +0x320 = 0, 825EEA18-825EEA44): Subway's lamps with a light of their own
// (trainstriplight, striplightyell) go out this way, not through 825F3978. On a zone level with lamp lures: a prop
// whose own light was there before Rare's call and is gone after it is a lamp lure at that light's position.
// Stealth round 5, Mon 16 Nov (fix 5): with the stealth layer running, the prop's health (its own virtual at vtable
// +332, the read Rare's handler itself starts with) is read before and after Rare's handler, so that the layer learns
// of a prop a bullet takes down (a security camera: lab_stealth_r5_C_Mon16Nov.inc). Layer off: one test, Rare's.
REX_HOOK_RAW(sub_825EE9D0)
{
  const uint32_t comp = ctx.r3.u32;
  const uint32_t prop = Ptr(comp) ? Load32(base, comp) : 0;
  const uint32_t own = Ptr(prop) ? Load32(base, prop + 0x320) : 0;
  if (Ptr(prop) && lab_stealth::PropHitsWatched(base))
  {
    const uint32_t vtable = Load32(base, prop);
    const float before = PropHealth(ctx, base, prop);
    float at[3];
    for (int k = 0; k < 3; ++k)
    {
      at[k] = LoadF(base, prop + 48 + 4 * k); // (the prop's place, as a character's)
    }
    // Sun 22 Nov night (audit): the two texts are copied only from mapped memory.
    char name[96] = {};
    if (const uint32_t np = Load32(base, prop + 136); Ptr(np) && np < 0xC0000000u && pdz::guestmem::Ok(base, np, 96)) // (the prop's name, a bounded copy)
    {
      const char *text = reinterpret_cast<const char *>(base + np);
      for (int i = 0; i < 95 && text[i]; ++i)
      {
        name[i] = text[i] >= 32 && text[i] < 127 ? text[i] : '?';
      }
    }
    // (overnight Mon 16 / Tue 17 Nov) The prop's model name (its setup +84 -> +140, as lab_grate_sound.cpp reads it): a
    // bounded copy, taken before Rare's handler. Subway's cameras are plain Static props: only the model tells.
    char model[96] = {};
    if (const uint32_t setup = Load32(base, prop + 84); Ptr(setup) && setup < 0xC0000000u && pdz::guestmem::Ok(base, setup + 140, 4))
    {
      if (const uint32_t mp = Load32(base, setup + 140); Ptr(mp) && mp < 0xC0000000u && pdz::guestmem::Ok(base, mp, 96))
      {
        const char *text = reinterpret_cast<const char *>(base + mp);
        for (int i = 0; i < 95 && text[i] >= 32 && text[i] < 127; ++i)
        {
          model[i] = text[i];
        }
      }
    }
    const bool lamp_path = Ptr(own) && lab_stealth::LampPropLureOn(base);
    float lamp[3] = {};
    if (lamp_path)
    {
      for (int k = 0; k < 3; ++k)
      {
        lamp[k] = LoadF(base, own + kLightPosition + 4 * k); // (read before Rare's call drops its reference)
      }
    }
    __imp__sub_825EE9D0(ctx, base);
    // Sun 22 Nov night (audit): Rare's handler may have torn the prop down: its memory is read only while mapped.
    const bool prop_ok = pdz::guestmem::Ok(base, prop, 0x324);
    if (lamp_path && prop_ok && Load32(base, prop + 0x320) == 0 && lamp[0] == lamp[0] && lamp[1] == lamp[1] && lamp[2] == lamp[2])
    {
      if (REXCVAR_GET(pdz_lab_light_shot_log))
      {
        REXLOG_INFO("pdz_lab_light_shot: prop {:08X} lost its own light {:08X} at ({:.1f}, {:.1f}, {:.1f}) (bullet hit, "
                    "825EE9D0): lamp lure",
                    prop, own, lamp[0], lamp[1], lamp[2]);
      }
      if (LureFresh(lamp))
      {
        lab_stealth::LightIncident(ctx, base, lamp, 1);
      }
    }
    if (before >= 0.0f && at[0] == at[0] && at[1] == at[1] && at[2] == at[2])
    {
      const bool gone = !prop_ok || Load32(base, prop) != vtable;
      const float after = gone ? 0.0f : PropHealth(ctx, base, prop);
      lab_stealth::PropBulletHit(base, prop, vtable, name, model, at, before, after, gone);
    }
    return;
  }
  if (!Ptr(own) || !lab_stealth::LampPropLureOn(base))
  {
    __imp__sub_825EE9D0(ctx, base);
    return;
  }
  float lamp[3];
  for (int k = 0; k < 3; ++k)
  {
    lamp[k] = LoadF(base, own + kLightPosition + 4 * k); // (read before Rare's call drops its reference)
  }
  __imp__sub_825EE9D0(ctx, base);
  // Sun 22 Nov night (audit): Rare's handler may have torn the prop down: its memory is read only while mapped.
  if (pdz::guestmem::Ok(base, prop, 0x324) && Load32(base, prop + 0x320) == 0 && lamp[0] == lamp[0] && lamp[1] == lamp[1] &&
      lamp[2] == lamp[2])
  {
    if (REXCVAR_GET(pdz_lab_light_shot_log))
    {
      REXLOG_INFO("pdz_lab_light_shot: prop {:08X} lost its own light {:08X} at ({:.1f}, {:.1f}, {:.1f}) (bullet hit, "
                  "825EE9D0): lamp lure",
                  prop, own, lamp[0], lamp[1], lamp[2]);
    }
    if (LureFresh(lamp))
    {
      lab_stealth::LightIncident(ctx, base, lamp, 1);
    }
  }
}

REX_HOOK_RAW(sub_825F3CD8)
{
  const bool log = t_in_shot && REXCVAR_GET(pdz_lab_light_shot_log);
  __imp__sub_825F3CD8(ctx, base);
  if (t_in_shot)
  {
    t_picked = ctx.r3.u32;
    // Rare puts its pick out only if it is on (A4: a second shot at the same spot picks the same, already dark lamp).
    const uint32_t state = Ptr(t_picked) ? Load32(base, t_picked + 0x590 + 4 * (Load32(base, kLightOnTable) & 1)) : 0;
    t_picked_on = state == 1 || state == 2;
  }
  if (!log)
  {
    return;
  }
  const uint32_t light = ctx.r3.u32;
  if (!Ptr(light))
  {
    REXLOG_INFO("pdz_lab_light_shot: no light picked");
    return;
  }
  const uint32_t table = Load32(base, kLightOnTable);
  REXLOG_INFO("pdz_lab_light_shot: picked light {:08X} at ({:.1f}, {:.1f}, {:.1f}), on-table {:08X}", light,
              LoadF(base, light + kLightPosition), LoadF(base, light + kLightPosition + 4),
              LoadF(base, light + kLightPosition + 8), table);
}

REX_HOOK_RAW(sub_825F3A10)
{
  if (REXCVAR_GET(pdz_lab_light_shot_log) && Ptr(ctx.r3.u32))
  {
    REXLOG_INFO("pdz_lab_light_shot: blast at ({:.1f}, {:.1f}, {:.1f}) r4 {:08X} f1 {:.1f}", LoadF(base, ctx.r3.u32),
                LoadF(base, ctx.r3.u32 + 4), LoadF(base, ctx.r3.u32 + 8), ctx.r4.u32, ctx.f1.f64);
  }
  __imp__sub_825F3A10(ctx, base);
}
