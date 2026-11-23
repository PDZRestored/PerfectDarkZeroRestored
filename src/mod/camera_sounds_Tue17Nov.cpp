// Security cameras: the sounds Rare recorded for a camera that is destroyed and falls (pdz_fix_camera_sounds, a
// restoration, on by default; Tue 17 Nov 2026). TU3 addresses.
//
// What a security camera is (corrected in the night of Mon 16 / Tue 17 Nov: the first version of this file was
// written against the sentry gun): the prop class DP_SecurityCamera, type index 10 in the class name table at
// 82D351E8, built by sub_825BDE50 (called with r5 = 10), vtable 820C9028 (+4 the update sub_825BE5D0, +180 the hit
// handler sub_825BF4A8, +196 the blast handler sub_825BF8C8, +116 "is it working" = prop +970 clear, +120 switch off
// sub_825C0590). The vtable 820BE9A8 with the update sub_825320D8 and the death routine sub_825337A0 is DP_AutoGun
// (type 44, the sentry gun: its strings are MUZZLE / autogun, and the frag grenade explosion cue is its own); this
// file no longer touches it.
//
// How a camera is hit and dies (read in the code):
//   - sub_825BF4A8, a bullet hit. If the part hit is the lens (hit record +108 = 15, 16 or 17) and the lens is whole
//     (prop +984 clear): prop +984 = 1, the break routine sub_825BFCA0(camera, hitter, 0) and the effect at prop +1044
//     (the lens: the 'breaktexture'; this is the one-shot glass crack heard in play). Every hit then counts in the byte
//     prop +972; at the fifth (or at once from weapon type 37) the count is set to 5 and sub_825BFCA0(camera, hitter, 1)
//     is called: the explosion.
//   - sub_825BF8C8, a blast: sub_825BFCA0(camera, hitter, 1).
//   - sub_825C0590 (switched off by something else): sub_825BFCA0(camera, 0, 0), then prop +985 = 1.
//   - the update sub_825BE5D0 repeats the same from the state of its network object (prop +1032) for the other machine.
//   - sub_825BFCA0(camera, hitter, exploded): prop +970 = 1 (out of action for good), the level is told (sub_82518128),
//     the motor loop is stopped (the sound at prop +1128: STOCK_SEC_CAM_MOTOR_LP, which the update starts while the
//     head turns and stops when it is still), and with `exploded` set: a blast at the camera (sub_82169ED0), the
//     effect at prop +1040 (sub_82228240) and the camera's head part (prop +976) is hidden on the mount and made again
//     as a separate falling object (a new 1008-byte prop, sub_825CC180, pushed off by sub_825E50B0).
// Its sounds: no sound call at all in sub_825BFCA0, the hit handler or the blast handler, and the fallen head is a
// plain debris object: nothing names a landing sound. (Whether the effect at +1040 carries a sound of its own was
// not read; in play the explosion is silent.) There is no landing callback to hook cheaply: the landing is timed.
// The stock bank holds two cues recorded for exactly this that nothing in the exe or the data names:
//   STOCK_SECCAMERA_SMASH (one take) and STOCK_SECCAMERA_FALL (many takes).
// (Also unused: GUNS_BULLETHIT_CAMERA and STOCK_SEC_CAM_ALARM_LP. Not touched here.)
//
// The restoration: sub_825BFCA0 is hooked (an event routine, not a per-frame one). A call with `exploded` set starts
// the smash cue at the camera at once and the fall cue at the same place after pdz_camera_fall_sound_delay_s (the drop
// from a ceiling mount: about 3 m, 0.8 s; Rare's own delayed play, sub_822061C8's delay). Both are positioned sounds
// with the cues' own loudness and Rare's attenuation, made with Rare's own calls (sub_82202D20, sub_821FF428,
// sub_822061C8). Should a cue not be in the loaded banks, the fallback named in its cvar is used; if that is missing
// too, nothing is played (and the log says so). Nothing of the AI is touched: no noise event, no perception write.
// Another module may ask to be told (the stealth layer's noise rules): SetDestroyedListener for the explosion, and
// SetLensHitListener for a call without `exploded` that puts a working camera out with its lens broken (prop +984).
// Each of the two events writes one "pdz_camera_sounds:" line; with pdz_sound_log every call of sub_825BFCA0 does.
#include <atomic>
#include <chrono>
#include <bit>
#include <cstring>
#include <string>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>

#include "restored_voice_Wed18Nov.h"
#include <rex/logging.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>

#include "../game_cvars.h" // pdz_sound_log

REXCVAR_DEFINE_BOOL(pdz_fix_camera_sounds, true, "Gameplay",
                    "A security camera that is destroyed plays the smash sound Rare recorded for it "
                    "(STOCK_SECCAMERA_SMASH) and, as it lands, its fall sound (STOCK_SECCAMERA_FALL). The game "
                    "names neither, and a destroyed camera is silent. Off = as shipped")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_camera_smash_cue, "XACT_SOUNDBANK_STOCK_SECCAMERA_SMASH,XACT_SOUNDBANK_STOCK_ELECTRICAL_EXPLOSION",
                      "Gameplay",
                      "pdz_fix_camera_sounds: the cue for a destroyed camera, then the fallback if the loaded banks "
                      "lack it (full names, separated by a comma). STOCK_ELECTRICAL_EXPLOSION first gives a small "
                      "electrical explosion instead of Rare's camera smash")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_camera_fall_cue, "XACT_SOUNDBANK_STOCK_SECCAMERA_FALL,XACT_SOUNDBANK_STOCK_LIGHT_METAL_FALL",
                      "Gameplay",
                      "pdz_fix_camera_sounds: the cue for the destroyed camera landing, then the fallback (full "
                      "names, separated by a comma). Empty = no landing sound")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_camera_fall_sound_delay_s, 0.8, "Gameplay",
                      "pdz_fix_camera_sounds: seconds from the camera's destruction to its landing sound (a 3 m "
                      "drop takes about 0.8 s)")
    .range(0.0, 5.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_825BFCA0); // the security camera's break routine (r3 = the camera, r4 = the hitter, r5 = exploded)
REX_EXTERN(sub_82202C78);        // the name resolver (r3 = name): the cue, or -1
REX_EXTERN(sub_82202D20);        // cue reference (r3 = ref {char name[256]; cue}, r4 = name)
REX_EXTERN(sub_821FF428);        // sound from a reference (r3 = ref, r4 = position or 0, r5, r6): the sound, or 0
REX_EXTERN(sub_822061C8);        // play (r3 = sound, f1 = delay in seconds)

namespace pdz::camsound
{
  using Destroyed = void (*)(PPCContext &ctx, uint8_t *base, uint32_t prop, const float pos[3]);
  void SetDestroyedListener(Destroyed fn); // the game thread, once per exploded camera
  using LensHit = void (*)(PPCContext &ctx, uint8_t *base, uint32_t prop, const float pos[3]);
  void SetLensHitListener(LensHit fn); // the game thread, once per camera put out by a shot to its lens
  bool IsSecurityCamera(const uint8_t *base, uint32_t prop);
} // namespace pdz::camsound

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;           // -> the game state / session block (a pointer while in a game)
  constexpr uint32_t kCameraVtable = 0x820C9028;   // DP_SecurityCamera (type 10)
  constexpr uint32_t kCameraOut = 970;             // byte: out of action (sub_825BFCA0 sets it)
  constexpr uint32_t kCameraHits = 972;            // byte: bullet hits so far; 5 = exploded
  constexpr uint32_t kCameraLensBroken = 984;      // byte: the lens was shot (sub_825BF4A8)
  constexpr uint32_t kPropPosition = 48;           // x, y, z (cm): where Rare's own blast is made
  constexpr uint32_t kSoundRefSize = 260;          // {char name[256]; cue id}
  constexpr uint32_t kNameMax = 96;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  void StoreF(uint8_t *base, uint32_t a, float v)
  {
    const uint32_t bits = std::bit_cast<uint32_t>(v);
    uint8_t *p = base + a;
    p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
  }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xFFFF0000u; }

  std::atomic<pdz::camsound::Destroyed> g_listener{nullptr};
  std::atomic<pdz::camsound::LensHit> g_lens_listener{nullptr};
  // The cameras that exploded lately (the game thread only): one event per camera, whoever calls the routine again.
  // (Ten seconds: a later level may put another camera at the same address.)
  struct Exploded
  {
    uint32_t prop = 0;
    std::chrono::steady_clock::time_point at{};
  };
  Exploded g_exploded[16];
  int g_exploded_next = 0;
  // Guest scratch (the game thread only; allocated once at the first destroyed camera): two cue names, two positions.
  uint32_t g_name[2] = {}, g_where[2] = {};
  uint64_t g_destroyed = 0;

  bool Scratch(uint8_t *base)
  {
    for (int i = 0; i < 2; ++i)
    {
      if (!g_name[i])
      {
        g_name[i] = REX_KERNEL_MEMORY()->SystemHeapAlloc(kNameMax);
      }
      if (!g_where[i])
      {
        g_where[i] = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
      }
      if (!g_name[i] || !g_where[i])
      {
        return false;
      }
    }
    return true;
  }

  // The first name of the comma-separated list that the loaded banks have, copied to the guest buffer `slot`; "" if
  // none. Asked only when a camera is destroyed.
  std::string PickCue(PPCContext &ctx, uint8_t *base, const std::string &list, int slot)
  {
    size_t at = 0;
    while (at < list.size())
    {
      size_t end = list.find(',', at);
      if (end == std::string::npos)
      {
        end = list.size();
      }
      size_t a = at, b = end;
      while (a < b && list[a] == ' ')
      {
        ++a;
      }
      while (b > a && list[b - 1] == ' ')
      {
        --b;
      }
      at = end + 1;
      if (b <= a || b - a >= kNameMax)
      {
        continue;
      }
      const std::string name = list.substr(a, b - a);
      std::memset(base + g_name[slot], 0, kNameMax);
      std::memcpy(base + g_name[slot], name.data(), name.size());
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g_name[slot];
      sub_82202C78(frame.ctx, base);
      if (int32_t(frame.ctx.r3.u32) != -1)
      {
        return name;
      }
    }
    return {};
  }

  // Rare's own calls: the cue named in the guest buffer `slot`, at `pos`, after `delay` seconds. False: no sound made.
  bool Play(PPCContext &ctx, uint8_t *base, int slot, const float pos[3], float delay)
  {
    std::memset(base + g_where[slot], 0, 64);
    for (int k = 0; k < 3; ++k)
    {
      StoreF(base, g_where[slot] + 4 * k, pos[k]);
    }
    uint32_t sound = 0;
    {
      rex::CallFrame frame(ctx);
      const std::string zeros(kSoundRefSize, 0);
      const uint32_t ref = rex::ppc::stack_push(frame.ctx, base, zeros.data(), kSoundRefSize);
      frame.ctx.r3.u64 = ref;
      frame.ctx.r4.u64 = g_name[slot];
      sub_82202D20(frame.ctx, base);
      frame.ctx.r3.u64 = ref;
      frame.ctx.r4.u64 = g_where[slot];
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      sub_821FF428(frame.ctx, base);
      sound = frame.ctx.r3.u32;
    }
    if (!Guest(sound))
    {
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = sound;
    frame.ctx.f1.f64 = double(delay);
    sub_822061C8(frame.ctx, base);
    pdz::snd::FreeWhenEnded(base, sound); // (Wed 18 Nov: it kept its sound object for ever)
    return true;
  }
} // namespace

namespace pdz::camsound
{
  void SetDestroyedListener(Destroyed fn) { g_listener.store(fn, std::memory_order_relaxed); }
  void SetLensHitListener(LensHit fn) { g_lens_listener.store(fn, std::memory_order_relaxed); }

  bool IsSecurityCamera(const uint8_t *base, uint32_t prop)
  {
    return Guest(prop) && (prop & 3u) == 0 && prop < 0xC0000000u && Load32(base, prop) == kCameraVtable;
  }
} // namespace pdz::camsound

REX_HOOK_RAW(sub_825BFCA0)
{
  const uint32_t prop = ctx.r3.u32;
  const bool exploded = (ctx.r5.u32 & 0xFFu) != 0; // (the routine tests the low byte)
  const bool camera = pdz::camsound::IsSecurityCamera(base, prop);
  const uint8_t out_before = camera ? base[prop + kCameraOut] : 0;
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_825BFCA0(ctx, base);
  if (!camera || !Guest(Load32(base, kGame)))
  {
    return;
  }
  const bool lens = base[prop + kCameraLensBroken] != 0;
  const bool log = REXCVAR_GET(pdz_sound_log);
  float pos[3];
  for (int k = 0; k < 3; ++k)
  {
    pos[k] = LoadF(base, prop + kPropPosition + 4 * k);
  }
  if (!(pos[0] == pos[0] && pos[1] == pos[1] && pos[2] == pos[2]))
  {
    return; // not a number: nowhere to play it
  }
  if (!exploded)
  {
    // Put out without the explosion: the lens shot (prop +984 set by the hit handler just before), or switched off.
    const bool lens_event = out_before == 0 && lens;
    if (lens_event || log)
    {
      REXLOG_INFO("pdz_camera_sounds: security camera {:08X} at ({:.0f} {:.0f} {:.0f}) put out of action without the "
                  "explosion (from {:08X}; was out before {}, lens broken {}, hits {}): {}",
                  prop, pos[0], pos[1], pos[2], caller, out_before, lens, base[prop + kCameraHits],
                  lens_event ? "a shot to the lens (the game's own glass effect; no sound of ours)"
                             : "not a lens shot: nothing follows");
    }
    if (lens_event)
    {
      if (const pdz::camsound::LensHit listener = g_lens_listener.load(std::memory_order_relaxed))
      {
        listener(ctx, base, prop, pos);
      }
    }
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  for (const Exploded &seen : g_exploded)
  {
    if (seen.prop == prop && now - seen.at < std::chrono::seconds(10))
    {
      if (log)
      {
        REXLOG_INFO("pdz_camera_sounds: security camera {:08X}: the explosion again (from {:08X}): already done", prop,
                    caller);
      }
      return;
    }
  }
  g_exploded[g_exploded_next] = {prop, now};
  g_exploded_next = (g_exploded_next + 1) % int(sizeof(g_exploded) / sizeof(g_exploded[0]));
  ++g_destroyed;
  std::string smash, fall;
  bool smash_ok = false, fall_ok = false;
  const double delay = REXCVAR_GET(pdz_camera_fall_sound_delay_s);
  if (REXCVAR_GET(pdz_fix_camera_sounds) && REXCVAR_GET(pdz_game_fixes) && Scratch(base))
  {
    smash = PickCue(ctx, base, REXCVAR_GET(pdz_camera_smash_cue), 0);
    if (!smash.empty())
    {
      smash_ok = Play(ctx, base, 0, pos, 0.0f);
    }
    fall = PickCue(ctx, base, REXCVAR_GET(pdz_camera_fall_cue), 1);
    if (!fall.empty())
    {
      fall_ok = Play(ctx, base, 1, pos, float(delay >= 0.0 && delay <= 5.0 ? delay : 0.8));
    }
  }
  REXLOG_INFO("pdz_camera_sounds: security camera {:08X} at ({:.0f} {:.0f} {:.0f}) exploded (from {:08X}; hits {}, lens "
              "broken {}; camera {} of this run): smash {} | landing {}{}",
              prop, pos[0], pos[1], pos[2], caller, base[prop + kCameraHits], lens, g_destroyed,
              !REXCVAR_GET(pdz_fix_camera_sounds) ? std::string("off (pdz_fix_camera_sounds)")
              : smash.empty()                     ? std::string("no cue of pdz_camera_smash_cue is in the loaded banks")
              : smash_ok                          ? smash + " started"
                                                  : smash + " could not be started",
              !REXCVAR_GET(pdz_fix_camera_sounds) ? std::string("off")
              : fall.empty()                      ? std::string("no cue of pdz_camera_fall_cue is in the loaded banks")
              : fall_ok                           ? fall
                                                  : fall + " could not be started",
              fall_ok ? fmt::format(" in {:.2f} s (timed, not detected)", delay) : std::string());
  if (const pdz::camsound::Destroyed listener = g_listener.load(std::memory_order_relaxed))
  {
    listener(ctx, base, prop, pos);
  }
}