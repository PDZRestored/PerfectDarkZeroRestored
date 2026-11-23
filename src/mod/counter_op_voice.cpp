// Counter-Operative: player 2 sounds like the guard he took (round 4, item 3b).
// Built only with PDZ_COUNTEROP.
//
// A human character has ONE voice object, at +0x1E9C (class vtable 820CC438; built by the human constructor at
// 825A6BC4-825A6BCC from his setup: +0xC = setup +56 head id, +0x10 = setup +64 voice index, +0x24 = the human).
// Every voiced sound a human makes is named from that one index by Rare's cue name builder 82204718
// ("XACT_SOUNDBANK_<VOICE>_<EVENT>", voice table 82D2B098, 0x24 a row):
//   hurt        825AD9B0 -> 8262C0F0   SHORT_SCREAM (111)
//   death       825AD788 -> 8262C290   FINAL_BREATH (113) 1 in 3, else SHORT_SCREAM (111)
//   jump etc.   824FC5F8, 82506B7C, 825BD424   HUP (115)
//   roll / land 82696C30, 82697460     SHARP_EXHALE (116)
//   melee       82158CE0               MELEE_GRUNT (117)
//   gas         825482C8               COUGHING (114)
//   (fall)      8269EE40 -> 8262C1C0   SHORT_SCREAM (111)
// A guard's speech component (guard +0x3190) derives from the same class, so his voice index is the same field:
// guard +0x3190 +0x10 = guard +0x31A0 (set at 825380BC / 8257FEEC from his setup word +0x54 = setup byte +87).
// Rare's own "change the voice" is 8262CD78(voice object, head id, voice index): it stops the playing line, drops the
// lip-sync handle and re-runs the init 8262C890 (which loads the voice's banks only if the voice table row has none:
// 822048D8). Multiplayer uses it when a player's body changes (825B4AA4). So: one call per spawn, no hook at all.
//
// Call-in points for counter_op.cpp (marked COUNTER-OP VOICE):
//   - in SpawnAt(), after the "spawns at" log line (the guard is e.character, his setup e.setup):
//       PdzCounterOpVoiceSpawn(ctx, base, g.p2c, guard, e.setup);
//   - in Reset():  PdzCounterOpVoiceReset();
//   - optional (untested): in OwnDeath(), before HideP2():
//       PdzCounterOpVoiceDeath(ctx, base, p2c);
// Declarations for counter_op.cpp:
//   void PdzCounterOpVoiceSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2c, uint32_t guard, uint32_t setup);
//   void PdzCounterOpVoiceDeath(PPCContext &ctx, uint8_t *base, uint32_t p2c);
//   void PdzCounterOpVoiceReset();
// Add this file to the PDZ_COUNTEROP target_sources line in CMakeLists.txt.
// If item 3a rebuilds player 2 through Rare's factory on every spawn, the new human's voice comes from his setup
// +64 again: either write the guard's voice there next to the body / head ids for the duration of Rare's call, or
// keep calling PdzCounterOpVoiceSpawn after the rebuild (it is a no-op when the voice already matches).
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(pdz_counterop_guard_voice, true, "Gameplay",
                    "Counter-Operative (default on): player 2's hurt, effort (jump, roll, melee), cough and death "
                    "sounds use the voice of the guard he replaced, set again on every spawn. Off = the co-op "
                    "partner's own voice")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_guard_voice_death, false, "Gameplay",
                    "Counter-Operative (default off, untested): when player 2's life ends through Counter-Op's own "
                    "death, play Rare's human death sound (a last breath or a short scream) in the guard's voice at "
                    "his place. Off: only the replaced guard's own death scream from Rare's kill is heard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_8262CD78); // set voice: (voice object, head id, voice index); no-op when both already match
REX_EXTERN(sub_8262C290); // a human's death sound: (voice object)

namespace
{
  constexpr uint32_t kHumanVtable = 0x820C7DE0, kGuardVtable = 0x820C45A0;
  constexpr uint32_t kHumanVoice = 0x1E9C;  // the human's voice object
  constexpr uint32_t kGuardVoice = 0x3190;  // the guard's speech component (same base class)
  constexpr uint32_t kVoiceHead = 0x0C, kVoiceIndex = 0x10, kVoiceOwner = 0x24;
  constexpr uint32_t kSetupVoice = 0x54;    // AI setup record: voice index (the byte at +87)
  constexpr uint32_t kVoiceTable = 0x82D2B098, kVoiceRow = 0x24, kRowBanks = 0x14; // +0x14 sound bank, +0x18 / +0x1C waves
  constexpr uint32_t kVoiceCount = 26;

  // Rare's voice table (82D2B098) and, from the sound banks (out\audio\cue_channels.tsv), which of the six human
  // events each voice has: bit 0 SHORT_SCREAM, 1 FINAL_BREATH, 2 COUGHING, 3 HUP, 4 SHARP_EXHALE, 5 MELEE_GRUNT.
  struct VoiceInfo
  {
    const char *name;
    uint8_t has;
  };
  constexpr VoiceInfo kVoices[kVoiceCount] = {
      {"CARRINGTON", 0x00}, {"CHANDRA", 0x39},  {"CLAY", 0x3F},     {"DRCARROL", 0x3F}, {"ELITE1", 0x3F},
      {"ELITE2", 0x3F},     {"ELITE3", 0x3F},   {"GANGSTA1", 0x3F}, {"GANGSTA2", 0x3F}, {"GANGSTA3", 0x3F},
      {"JACK", 0x3F},       {"JO", 0x3F},       {"JON", 0x3F},      {"KILLIAN", 0x00},  {"MAIHEM", 0x3F},
      {"SCI1", 0x3F},       {"SCI2", 0x3F},     {"TROOPER1", 0x3F}, {"TROOPER2", 0x3F}, {"TROOPER3", 0x3F},
      {"VIRGIL", 0x3F},     {"WOMAN1", 0x3F},   {"WOMAN2", 0x07},   {"ZEIGLER", 0x00},  {"ZHANGLI", 0x3F},
      {"ZHANGLI", 0x3F}};
  constexpr uint32_t kWoman1 = 21, kWoman2 = 22;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  const char *Name(uint32_t voice) { return voice < kVoiceCount ? kVoices[voice].name : "(none)"; }

  // The voice's sound bank is loaded (Rare's loader 822048D8 fills the row's three slots once; every voice a level's
  // characters were built with has them, a dormant guard's too).
  bool Loaded(const uint8_t *base, uint32_t voice)
  {
    return voice < kVoiceCount && Load32(base, kVoiceTable + voice * kVoiceRow + kRowBanks) != 0;
  }

  struct VoiceState
  {
    uint32_t human = 0;       // the human object whose own voice is remembered below
    int32_t own_voice = -1;   // Rare's voice for him (his setup's), before the first change
    uint32_t spawns = 0;
    bool death_logged = false;
  } vs;

  void SetVoice(PPCContext &ctx, uint8_t *base, uint32_t p2c, uint32_t voice)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400) & ~0xFu;
    frame.ctx.r3.u64 = p2c + kHumanVoice;
    frame.ctx.r4.u64 = Load32(base, p2c + kHumanVoice + kVoiceHead); // unchanged: only the voice differs
    frame.ctx.r5.u64 = voice;
    sub_8262CD78(frame.ctx, base);
  }
} // namespace

void PdzCounterOpVoiceReset()
{
  vs = VoiceState{};
}

// Once per spawn (SpawnAt): player 2 takes the replaced guard's voice. guard = the guard character (may be dead or
// dormant), setup = his AI setup record (used when the character is not a guard object).
void PdzCounterOpVoiceSpawn(PPCContext &ctx, uint8_t *base, uint32_t p2c, uint32_t guard, uint32_t setup)
{
  if (!Ptr(p2c) || Load32(base, p2c) != kHumanVtable || Load32(base, p2c + kHumanVoice + kVoiceOwner) != p2c)
  {
    REXLOG_INFO("pdz_counterop: voice: player 2's character {:08X} is not a human with a voice object: left alone",
                p2c);
    return;
  }
  const uint32_t now = Load32(base, p2c + kHumanVoice + kVoiceIndex);
  if (vs.human != p2c)
  {
    vs.human = p2c; // a new human object (level start, or a rebuild): its voice is Rare's own again
    vs.own_voice = int32_t(now);
  }
  ++vs.spawns;
  if (!REXCVAR_GET(pdz_counterop_guard_voice))
  {
    if (vs.own_voice >= 0 && now != uint32_t(vs.own_voice))
    {
      SetVoice(ctx, base, p2c, uint32_t(vs.own_voice));
      REXLOG_INFO("pdz_counterop: voice: pdz_counterop_guard_voice is off: player 2 back to his own voice {} {}",
                  vs.own_voice, Name(uint32_t(vs.own_voice)));
    }
    return;
  }
  uint32_t voice = kVoiceCount;
  const char *from = "none";
  if (Ptr(guard) && Load32(base, guard) == kGuardVtable)
  {
    voice = Load32(base, guard + kGuardVoice + kVoiceIndex);
    from = "the guard's speech component (+0x31A0)";
  }
  else if (Ptr(setup))
  {
    voice = Load32(base, setup + kSetupVoice);
    from = "the guard's setup (+0x54)";
  }
  const uint32_t asked = voice;
  const char *rule = "as is";
  // WOMAN2 has the screams, the last breath and the cough but no HUP / SHARP_EXHALE / MELEE_GRUNT: WOMAN1 (the same
  // family; four takes of each) when the level has her loaded, else WOMAN2 with silent efforts.
  if (voice == kWoman2 && Loaded(base, kWoman1))
  {
    voice = kWoman1;
    rule = "WOMAN2 has no effort sounds: WOMAN1 instead";
  }
  // A voice with none of the six (CARRINGTON 0 = an unset setup byte, KILLIAN, ZEIGLER), out of the table, or not
  // loaded: keep what he has (the last life's guard voice; on the first life Rare's own).
  if (voice >= kVoiceCount || kVoices[voice].has == 0 || !Loaded(base, voice))
  {
    REXLOG_INFO("pdz_counterop: voice: life #{}: guard {:08X} has voice {} {} ({}): {}; player 2 keeps voice {} {}",
                vs.spawns, guard, asked, Name(asked), from,
                voice >= kVoiceCount ? "not a voice" : kVoices[voice].has == 0 ? "no hurt / effort sounds recorded"
                                                                               : "its bank is not loaded",
                now, Name(now));
    return;
  }
  if (voice != now)
  {
    SetVoice(ctx, base, p2c, voice);
  }
  const uint32_t after = Load32(base, p2c + kHumanVoice + kVoiceIndex);
  const uint8_t has = kVoices[voice].has;
  REXLOG_INFO("pdz_counterop: voice: life #{}: player 2 speaks as {} {} (guard {:08X}, voice {} {} from {}; {}); was {} "
              "{}, Rare's own {} {}; now {} {}{}; sounds: scream {}, last breath {}, cough {}, hup {}, exhale {}, "
              "melee grunt {}",
              vs.spawns, voice, Name(voice), guard, asked, Name(asked), from, rule, now, Name(now), vs.own_voice,
              Name(uint32_t(vs.own_voice)), after, Name(after), after == voice ? "" : " (NOT TAKEN: Rare's init "
                                                                                     "changed it)",
              has & 1 ? "yes" : "no", has & 2 ? "yes" : "no", has & 4 ? "yes" : "no", has & 8 ? "yes" : "no",
              has & 16 ? "yes" : "no", has & 32 ? "yes" : "no");
}

// Optional, off by default (pdz_counterop_guard_voice_death): Counter-Op's own death never runs Rare's human kill
// (825AD788), so the human death sound is not played; this plays it (8262C290: FINAL_BREATH 1 in 3, else
// SHORT_SCREAM, in his current voice, at his place). Call before HideP2().
void PdzCounterOpVoiceDeath(PPCContext &ctx, uint8_t *base, uint32_t p2c)
{
  if (!REXCVAR_GET(pdz_counterop_guard_voice) || !REXCVAR_GET(pdz_counterop_guard_voice_death) || !Ptr(p2c) ||
      Load32(base, p2c) != kHumanVtable || Load32(base, p2c + kHumanVoice + kVoiceOwner) != p2c)
  {
    return;
  }
  rex::CallFrame frame(ctx);
  frame.ctx.r1.u64 = (ctx.r1.u32 - 0x400) & ~0xFu;
  frame.ctx.r3.u64 = p2c + kHumanVoice;
  sub_8262C290(frame.ctx, base);
  if (!vs.death_logged)
  {
    vs.death_logged = true;
    const uint32_t voice = Load32(base, p2c + kHumanVoice + kVoiceIndex);
    REXLOG_INFO("pdz_counterop: voice: first own death with a human death sound (8262C290) in voice {} {}", voice,
                Name(voice));
  }
}
