// Character voices volume (pdz_guard_voice_db) and a music duck other modules can ask for (voice_volume.h).
//
// Rare's audio layer sets XACT category volumes through one thunk, sub_82C161D0 (r3 = category, f1 = linear volume;
// its only caller sub_8275DC98 sends clamp(level / 127) x 0.9, after Rare's category table 0x82E67C48 + 32 x cat
// (+0x1C level 0..100) and the player's music / SFX option). Research notes: the round 3 research folder,
// voice_volume_duck.md. Category 7 (speech_characters) holds every character's speech bank (guards,
// scientists, women, named characters); category 15 (LevelTunes) is the level music. This hook multiplies what Rare
// sends: category 7 by pdz_guard_voice_db, category 15 by the current duck. Rare's own levels, fades and options
// are never written, so nothing has to be restored. A changed value is applied by re-pushing the category through
// Rare's own setter sub_821FBE08(cat, its current level, 0) on the game thread.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "voice_volume.h"

REXCVAR_DEFINE_DOUBLE(pdz_guard_voice_db, 3.0, "Audio",
                      "Guard voices: extra volume in dB for every character's speech (guards, scientists, named "
                      "characters; XACT category speech_characters) on top of Rare's level and the SFX option. Rare's "
                      "own level is 0; the default +3 dB makes guards easier to hear over the music. -12 to +9")
    .range(-12.0, 9.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_82C161D0); // XACT SetVolume thunk (r3 category, f1 linear volume)
REX_EXTERN(sub_821FBE08);        // Rare's category set (r3 category, r4 level 0..100, r5 cancel a fade)

namespace
{
  constexpr uint32_t kSpeechCategory = 7;   // speech_characters
  constexpr uint32_t kMusicCategory = 15;   // LevelTunes
  constexpr uint32_t kCategoryTable = 0x82E67C48; // 32 bytes per category; +0x1C level, +0x1E enabled
  constexpr uint32_t kCategories = 25;
  float g_duck = 1.0f;          // music gain asked for by SetMusicDuck (1 = none)
  std::chrono::steady_clock::time_point g_duck_set; // a duck not renewed for 2 s no longer applies (left the level)
  bool DuckLive()
  {
    return g_duck < 0.999f && std::chrono::steady_clock::now() - g_duck_set < std::chrono::seconds(2);
  }
  double g_voice_db_pushed = 0; // the pdz_guard_voice_db value last re-pushed

  void Repush(PPCContext &ctx, uint8_t *base, uint32_t cat)
  {
    if (cat >= kCategories)
    {
      return;
    }
    const uint32_t entry = kCategoryTable + 32 * cat;
    if (base[entry + 0x1E] == 0)
    {
      return; // not enabled: Rare pushes nothing for it either
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = cat;
    frame.ctx.r4.u64 = base[entry + 0x1C];
    frame.ctx.r5.u64 = 0;
    sub_821FBE08(frame.ctx, base);
  }
} // namespace

REX_HOOK_RAW(sub_82C161D0)
{
  const uint32_t cat = ctx.r3.u32 & 0xFFFF;
  if (cat == kSpeechCategory)
  {
    const double db = REXCVAR_GET(pdz_guard_voice_db);
    if (db != 0.0 && std::isfinite(db))
    {
      ctx.f1.f64 *= std::pow(10.0, std::clamp(db, -12.0, 9.0) / 20.0);
    }
  }
  else if (cat == kMusicCategory && DuckLive())
  {
    ctx.f1.f64 *= double(g_duck);
  }
  __imp__sub_82C161D0(ctx, base);
}

namespace voice_volume
{
  void SetMusicDuck(PPCContext &ctx, uint8_t *base, float gain)
  {
    gain = std::clamp(gain, 0.05f, 1.0f);
    g_duck_set = std::chrono::steady_clock::now();
    if (std::fabs(gain - g_duck) < 1e-4f)
    {
      return;
    }
    const bool started = g_duck >= 0.999f && gain < 0.999f, ended = g_duck < 0.999f && gain >= 0.999f;
    g_duck = gain;
    Repush(ctx, base, kMusicCategory);
    if (started || ended)
    {
      REXLOG_INFO("pdz_guard_voice: level music duck {} (category {} re-pushed at level {})", started ? "starts" : "ends",
                  kMusicCategory, base[kCategoryTable + 32 * kMusicCategory + 0x1C]);
    }
  }

  float MusicDuck()
  {
    return g_duck;
  }

  void FollowVoiceVolume(PPCContext &ctx, uint8_t *base)
  {
    const double db = REXCVAR_GET(pdz_guard_voice_db);
    if (db != g_voice_db_pushed)
    {
      g_voice_db_pushed = db;
      Repush(ctx, base, kSpeechCategory);
      REXLOG_INFO("pdz_guard_voice: character speech {:+.1f} dB (category {} re-pushed)", db, kSpeechCategory);
    }
  }
} // namespace voice_volume
