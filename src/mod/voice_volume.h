// Character voices volume (pdz_guard_voice_db) and a music duck: see voice_volume.cpp.
#pragma once

#include <cstdint>

#include <rex/ppc/func.h>

namespace voice_volume
{
  // The level music (XACT category 15) at `gain` (linear, 0.05..1; 1 = Rare's level) until set again. Re-pushes the
  // category through Rare's setter when the gain changes: call on the game thread, and step it for a ramp.
  void SetMusicDuck(PPCContext &ctx, uint8_t *base, float gain);
  float MusicDuck();
  // Re-push character speech (category 7) when pdz_guard_voice_db changed since the last push (a hot-reload).
  void FollowVoiceVolume(PPCContext &ctx, uint8_t *base);
} // namespace voice_volume
