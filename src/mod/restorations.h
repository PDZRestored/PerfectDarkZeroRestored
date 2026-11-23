// Restorations (pdz_restore_*): see restorations.cpp.
#pragma once

#include <cstdint>

struct PPCContext;

namespace restorations
{
  // Called every audio tick from src/music/music_restore.cpp: delayed lines
  // (pdz_restore_light_speech).
  void Tick(PPCContext &ctx, uint8_t *base);

  // Round 11b: the scientists' lights-out talk for lamps Jo shot out (lab_stealth.cpp decides when): `a` and, when
  // not 0, `b` have the conversation now (one asks, the other answers 4 s later); `b` 0: `a` says his solo line.
  // Only takes that suit a few dead lamps. False when it cannot start now.
  bool LampTalk(PPCContext &ctx, uint8_t *base, uint32_t a, uint32_t b);
  // Round 11j: this scientist speaks Rare's SCI1 set (his voice number's, or pdz_lab_lights_out_sci1 names him).
  bool SpeaksSci1(const uint8_t *base, uint32_t character);

  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start (first load, mission restart, reload): the blackout
  // conversation's and the lamp talk's memory of the attempt before is dropped at their next use. Any thread.
  void LevelReset();

  // pdz_test_lab_blackout_key (testing tool): registers the Home key.
  void RegisterBinds();
} // namespace restorations
