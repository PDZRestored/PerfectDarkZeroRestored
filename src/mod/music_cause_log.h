#pragma once

#include <cstdint>

#include <rex/hook.h>

// pdz_music_log: one line per level music change, with what caused it.
namespace music_cause_log
{
  bool Enabled();

  struct Pick
  {
    uint32_t world = 0, wanted = 0, lr = 0;
    float dt = 0.0f;
  };
  // Around Rare's per-frame picker (launch 821F8708, hook sub_82200928).
  Pick BeforePick(const PPCContext &ctx, const uint8_t *base);
  void AfterPick(PPCContext &ctx, uint8_t *base, const Pick &before);

  struct Type
  {
    uint32_t from = 0, to = 0, lr = 0;
  };
  // Around the music type change (launch 821F8B28, hook sub_82200C68).
  Type BeforeType(const PPCContext &ctx, const uint8_t *base);
  void AfterType(const uint8_t *base, const Type &before);

  // After the level music setup (launch 821F8450, hook sub_82200738).
  void AfterSetup(const uint8_t *base);
} // namespace music_cause_log
