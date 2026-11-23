// Restorations (docs/TU3_BASE.md "Restoration build plan"): content Rare cut
// or never wired up, each behind its own switch, off by default (except the
// Laboratory lights-out lines, R3 below with pdz_lab_blackout_lines: on by
// default since round 3, Sun 15 Nov, as they are Rare's recorded lines; they
// act only in the Laboratory, whose script alone has the handler hooked
// below). tu3-base only; TU3 addresses. Launch and TU3 behave the same here
// (TU3_BASE "TU3 audio regressions"), so these are restorations, not fixes.
//
// - pdz_restore_mansion_door_greeting (R1): the main-door guard's
//   LV4_LET_JO_IN line. The speech builder (sub_82204718) turns event 169
//   into LV04_SPEECH_GUARD_N ("Enjoy the party." / "Miss." / "The
//   entertainment's about to begin."), but no code raises 169 (BUG_AUDIT
//   AI53). The Mansion script (sub_823326B0, the door state machine) lets Jo
//   in at 82332990 by setting bit 0x1000 in its door state word
//   ([script +1004] +12): at once on Agent and Secret Agent, after a
//   successful voice check on Perfect and Dark Agent. When that bit comes on
//   during a call, the main-door guard (script slot +1376 'end1', or +1384
//   'end2', the hard guard) says 169 through the voice (sub_8262AA40).
// - pdz_restore_light_speech (R3): the staff's lights-out lines in the
//   Laboratory blackout. Speech only: the darkness mechanic (sight range,
//   aim, search task; SP_INVENTORY "Darkness in the Laboratory blackout") is
//   Rare's and is left alone. The Laboratory handler (sub_823272E0) puts
//   the lights out when bit 0x40 of its first message mask (r5) is set
//   (8232A264): Lights_Toggle on groups 1-4, which always switches lights
//   off, and a black ambient colour. Nothing turns them back on
//   (SP_INVENTORY "Is the blackout ever reversed?"), so a repeat of the
//   message is ignored. Every voice recorded LIGHT_SHOT_OUT (147), and the
//   scientists recorded lights_out_next_solo / _question / _answer, but
//   nothing plays them (SP_INVENTORY, "A cut stealth layer?"). When the
//   lights go out, up to three of the staff nearest Jo (the Laboratory
//   script's living characters within 30 m of her, scientists apart) say 147.
//   The scientists' lines are a conversation about an outage that lasts
//   ("Maintenance is sure taking its time", "Shouldn't we evacuate?"), so
//   they wait for Jo (round 8; the first rule, "scientists within 30 m of Jo
//   when the power goes", never fired: there are none near the generator):
//   after the blackout, the first time Jo comes within
//   pdz_lab_lights_out_earshot_cm of a scientist who stands in the dark
//   (round 3, Sun 15 Nov, pdz_lab_lights_out_wide: or enters his darkened
//   room within pdz_lab_lights_out_room_cm, or comes within
//   pdz_lab_lights_out_view_cm with no wall between),
//   he and a second scientist in the dark within 15 m of him have it: one
//   asks (_question), the other answers (_answer) 4 s later; a scientist on
//   his own says lights_out_next_solo. Only scientists who have not seen
//   Jo. Once per scientist, not after the
//   power has returned (flag 40), never over story speech or a guard's line
//   (lab_stealth's one-voice rule; it waits and tries again). These lines
//   have no speech event, so they play as positioned sounds on the speaker
//   (sub_821FF3C8, as the level scripts play sounds on characters).
//   With pdz_lab_blackout_lines (lab_restorations.cpp, a) the speakers are
//   limited to staff standing inside one of the four darkened light-group
//   boxes, so nobody in the still-lit Lab One / Lab Two says the lines.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <rex/hook.h>
#include "guest_function.h" // our indirect calls: nullptr for no function (log 101)
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/keybinds.h>

#include "../game_cvars.h"
#include "lab_restorations.h"
#include "lab_stealth.h"
#include "restorations.h"
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

REX_EXTERN(__imp__sub_823326B0);
REX_EXTERN(__imp__sub_823272E0);
REX_EXTERN(sub_8262AA40);
REX_EXTERN(sub_82202D20);
REX_EXTERN(sub_821FF3C8);

namespace
{
  constexpr uint32_t kGameState = 0x82D6BFD8;   // -> game info; +4 = difficulty (0 Agent .. 3 Dark Agent)
  constexpr uint32_t kDoorState = 1004;         // script -> door state object
  constexpr uint32_t kDoorFlags = 12;           // word in it
  constexpr uint32_t kLetJoIn = 0x1000;         // set at 82332990
  constexpr uint32_t kGuardEnd1 = 1376;         // script slot 'end1'
  constexpr uint32_t kGuardEnd2 = 1384;         // script slot 'end2' (Perfect / Dark guard)
  constexpr uint32_t kVoice = 12688;            // character -> voice / speech component
  constexpr uint32_t kLetJoInEvent = 169;       // LV4_LET_JO_IN

  uint32_t Load32(const uint8_t *base, uint32_t address)
  {
    const uint8_t *p = base + address;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }

  bool IsGuestPointer(uint32_t address)
  {
    return address >= 0x00010000u && address < 0xFFFF0000u;
  }

  bool LetJoIn(const uint8_t *base, uint32_t script)
  {
    const uint32_t door = Load32(base, script + kDoorState);
    return IsGuestPointer(door) && (Load32(base, door + kDoorFlags) & kLetJoIn) != 0;
  }
} // namespace

// sub_823326B0: the Mansion script's message handler (r3 = the script).
REX_HOOK_RAW(sub_823326B0)
{
  const uint32_t script = ctx.r3.u32;
  const bool watch = REXCVAR_GET(pdz_restore_mansion_door_greeting) && IsGuestPointer(script);
  const bool before = watch && LetJoIn(base, script);
  __imp__sub_823326B0(ctx, base);
  if (!watch || before || !LetJoIn(base, script))
  {
    return;
  }
  const uint32_t info = Load32(base, kGameState);
  const uint32_t difficulty = IsGuestPointer(info) ? Load32(base, info + 4) : 0;
  uint32_t guard = Load32(base, script + (difficulty >= 2 ? kGuardEnd2 : kGuardEnd1));
  if (!IsGuestPointer(guard))
  {
    guard = Load32(base, script + (difficulty >= 2 ? kGuardEnd1 : kGuardEnd2));
  }
  if (!IsGuestPointer(guard))
  {
    REXLOG_INFO("pdz_restore: Mansion door greeting: no main-door guard in the script slots");
    return;
  }
  // say(voice, event, subject female = Jo, 1, delay 0), as the level scripts call it.
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = guard + kVoice;
  frame.ctx.r4.u64 = kLetJoInEvent;
  frame.ctx.r5.u64 = 1;
  frame.ctx.r6.u64 = 1;
  frame.ctx.f1.f64 = 0.0;
  sub_8262AA40(frame.ctx, base);
  REXLOG_INFO("pdz_restore: Mansion door greeting (LV4_LET_JO_IN) from guard {:08X}", guard);
}

// --- pdz_restore_light_speech (R3) ---
REXCVAR_DEFINE_STRING(pdz_lab_lights_out_sci1, "s2s1", "Gameplay",
                      "pdz_restore_light_speech: setup ids (comma separated) of Laboratory scientists who say their "
                      "lights-out lines from Rare's SCI1 recordings whatever their voice number is. Default s2s1, the "
                      "hub archaeologist: Rare cast him and s2s2 as the SCI1 and the SCI2 man in their own scripted "
                      "lines, so the pair has two voices and SCI1's question can come up with its \"Mesa\" take. "
                      "Empty = everyone speaks the set of his own voice number (the hub pair: one voice twice)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_lights_out_earshot_cm, 1500.0, "Gameplay",
                      "pdz_restore_light_speech: after the Laboratory blackout the scientists' recorded lights-out "
                      "conversation plays the first time Jo comes within this distance (cm) of a scientist who stands "
                      "in the dark (walls or not; pdz_lab_lights_out_wide adds two wider rules)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Round 3, Sun 15 Nov (helper L, item 9 (a)).
REXCVAR_DEFINE_BOOL(pdz_lab_lights_out_wide, true, "Gameplay",
                    "pdz_restore_light_speech: the scientists' lights-out conversation after the Laboratory blackout "
                    "also starts when Jo enters the darkened room he stands in (one of the four blackout areas: main "
                    "hall, generator room, cutscene room, security) within pdz_lab_lights_out_room_cm of him, or when "
                    "she is within pdz_lab_lights_out_view_cm of him with no wall between (the AI sight ray, head to "
                    "head). Off = only within pdz_lab_lights_out_earshot_cm")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_lights_out_room_cm, 4500.0, "Gameplay",
                      "pdz_lab_lights_out_wide: the farthest (cm) Jo may be from the scientist when she enters his "
                      "room (the main hall is 80 m long: 45 m reaches the hub scientists from either end)")
    .range(0.0, 10000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_lab_lights_out_view_cm, 3000.0, "Gameplay",
                      "pdz_lab_lights_out_wide: within this distance (cm) with no wall between Jo and the scientist")
    .range(0.0, 10000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
namespace
{
  using Clock = std::chrono::steady_clock;

  constexpr uint64_t kLightsMessage = 0x40;    // r5 bit: lights out, groups 1-4 (8232A264)
  constexpr uint32_t kSlotFirst = 1148;        // Laboratory script character slots: s1m1 ...
  constexpr uint32_t kSlotLast = 1700;         // ... s12b (zgli and the cutscene actors follow)
  constexpr uint32_t kSlotStep = 8;
  constexpr uint32_t kCharVoiceIndex = 12704;  // 15 SCI1, 16 SCI2
  constexpr uint32_t kCharPosition = 48;       // vec3, cm
  constexpr uint32_t kHealthSlot = 332;        // character vtable: health (f1)
  constexpr uint32_t kParticipantList = 0x82E34224; // -> first participant (+4 next)
  constexpr uint32_t kParticipantKind = 944;   // 2 player, 3 bot
  constexpr uint32_t kParticipantCharacter = 964; // participant -> its character
  constexpr uint32_t kLightShotOut = 147;      // LIGHT_SHOT_OUT
  constexpr float kHearing2 = 3000.0f * 3000.0f; // staff within 30 m of Jo
  constexpr float kPair2 = 1500.0f * 1500.0f;    // two scientists within 15 m make a pair (the hub's two stand 10.9 m apart)
  constexpr int kFirstSpeakers = 3;
  constexpr auto kAnswerDelay = std::chrono::milliseconds(4000); // (the questions run up to about 3.5 s)
  constexpr uint32_t kRefSize = 1024;          // cue reference (the handler's are 544 bytes apart)
  constexpr uint32_t kNameSize = 128;

  std::mutex g_light_mutex;
  uint32_t g_light_script = 0;
  bool g_dark = false;
  std::vector<uint32_t> g_spoke;      // scientists who had their lights-out line in this blackout
  uint32_t g_lamp_script = 0;         // round 11b: the run the lamp talk's list belongs to
  std::vector<uint32_t> g_lamp_spoke; // ... scientists who had the lights-out talk for shot-out lamps in it
  Clock::time_point g_conv_check{};   // the next look for scientists within earshot
  Clock::time_point g_conv_wait_log{};
  uint32_t g_ref = 0;
  // Round 11d (item 3): what the blackout conversation knows of each scientist, sampled twice a second.
  struct Track
  {
    float pos[3] = {};
    float health = 0.0f;
    Clock::time_point at{}, still_since{}, hurt_at{};
    bool hurt = false;
  };
  std::unordered_map<uint32_t, Track> g_track;
  Clock::time_point g_unsettled_log{};
  struct PendingLine
  {
    uint32_t character = 0;
    std::string cue;
    Clock::time_point when{};
    uint32_t takes = 0; // round 11b: the takes it may be (bit i = take i + 1), 0 = XACT's own pick
  };
  PendingLine g_pending;
  // Follow-up (Sun 15 Nov): the stealth layer's lamp talk (pdz_lab_lamp_talk) was used in this session, so Tick must
  // run for its pending answer even with pdz_restore_light_speech and pdz_lab_blackout_lines off (both on by default
  // since round 3, Sun 15 Nov; switched off by hand this still holds).
  std::atomic<bool> g_lamp_talk_used{false};
  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start (restorations::LevelReset, from the stealth layer's
  // clear): the blackout's and the lamp talk's memory is dropped at the next use, under g_light_mutex. The script
  // pointer alone does not tell a mission restart: the new script can sit where the old one was.
  std::atomic<bool> g_level_reset{false};
  std::atomic<bool> g_key_reset{false};
  void ApplyLevelReset() // under g_light_mutex
  {
    if (!g_level_reset.exchange(false, std::memory_order_relaxed))
    {
      return;
    }
    g_light_script = 0;
    g_dark = false;
    g_spoke.clear();
    g_lamp_script = 0;
    g_lamp_spoke.clear();
    g_conv_check = Clock::time_point{};
    g_conv_wait_log = Clock::time_point{};
    g_track.clear();
    g_unsettled_log = Clock::time_point{};
    g_pending = PendingLine{};
  }

  float LoadFloat(const uint8_t *base, uint32_t address)
  {
    const uint32_t bits = Load32(base, address);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
  }

  // A character's position (+48, cm; as cut_scenarios.cpp / anim_viewer.cpp
  // read it in TU3).
  bool PositionOf(PPCContext &ctx, uint8_t *base, uint32_t character, float out[3])
  {
    (void)ctx;
    if (!IsGuestPointer(character))
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      out[i] = LoadFloat(base, character + kCharPosition + 4 * i);
    }
    return true;
  }

  // Sun 22 Nov night (audit): a script slot's character (or one kept for 4 s) may have died and its memory been reused:
  // its health getter only when the character is mapped (up to its voice field), its vtable is in the game image and
  // the slot is image code (GuestFunction refuses _purecall). Otherwise nullptr.
  PPCFunc *HealthGetter(const uint8_t *base, uint32_t character)
  {
    if (!pdz::guestmem::Ok(base, character, kCharVoiceIndex + 4))
    {
      return nullptr;
    }
    const uint32_t vtable = Load32(base, character);
    if (vtable < 0x82000000u || vtable >= 0x83000000u || !pdz::guestmem::Ok(base, vtable + kHealthSlot, 4))
    {
      return nullptr;
    }
    const uint32_t slot = Load32(base, vtable + kHealthSlot);
    return slot >= 0x82000000u && slot < 0x833E0000u ? pdz::GuestFunction(slot) : nullptr;
  }

  bool Alive(PPCContext &ctx, uint8_t *base, uint32_t character)
  {
    PPCFunc *fn = HealthGetter(base, character);
    if (!fn)
    {
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = character;
    frame.ctx.r4.u64 = 0;
    fn(frame.ctx, base);
    return frame.ctx.f1.f64 > 0.0;
  }

  float Distance2(const float a[3], const float b[3]);
  float Health(PPCContext &ctx, uint8_t *base, uint32_t character)
  {
    PPCFunc *fn = HealthGetter(base, character); // (Sun 22 Nov night, audit)
    if (!fn)
    {
      return 0.0f;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = character;
    frame.ctx.r4.u64 = 0;
    fn(frame.ctx, base);
    return float(frame.ctx.f1.f64);
  }

  // Round 11d (item 3): after the generator blackout (which is loud) the scientists who may have the lights-out
  // conversation are the ones who stay where they are: calm at their work, or cowering / hiding (Rare's own
  // behaviour when the shooting starts: they stop and stay put). Never one who is fleeing or running for the alarm
  // (he moves), and never one who is being shot (his health fell in the last 5 s). Told apart by what they do, not
  // by Rare's task: SETTLED = he has not moved more than 25 cm between two looks (half a second apart) for 1.5 s,
  // and his health has not dropped for 5 s. `why` says what is missing.
  bool Settled(PPCContext &ctx, uint8_t *base, uint32_t character, const float pos[3], Clock::time_point now, const char **why)
  {
    Track &t = g_track[character];
    const float health = Health(ctx, base, character);
    const bool fresh = t.at == Clock::time_point{} || now - t.at > std::chrono::seconds(2);
    if (fresh)
    {
      t.still_since = now;
      t.hurt = false;
    }
    else
    {
      if (Distance2(pos, t.pos) > 25.0f * 25.0f)
      {
        t.still_since = now;
      }
      if (health < t.health - 0.01f)
      {
        t.hurt = true;
        t.hurt_at = now;
      }
    }
    std::memcpy(t.pos, pos, sizeof(t.pos));
    t.health = health;
    t.at = now;
    if (t.hurt && now - t.hurt_at < std::chrono::seconds(5))
    {
      *why = "he was hurt in the last 5 s";
      return false;
    }
    if (now - t.still_since < std::chrono::milliseconds(1500))
    {
      *why = "he is moving (fleeing, running for the alarm, or walking)";
      return false;
    }
    return true;
  }

  // Jo's character: the first human participant's (+964).
  uint32_t Player(const uint8_t *base)
  {
    uint32_t p = Load32(base, kParticipantList);
    for (int guard = 0; IsGuestPointer(p) && guard < 64; ++guard, p = Load32(base, p + 4))
    {
      if (Load32(base, p + kParticipantKind) == 2)
      {
        const uint32_t character = Load32(base, p + kParticipantCharacter);
        return IsGuestPointer(character) ? character : 0;
      }
    }
    return 0;
  }

  struct Staff
  {
    uint32_t character;
    float pos[3];
    float d2; // to Jo
    bool scientist;
  };

  float Distance2(const float a[3], const float b[3])
  {
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
      d2 += (a[i] - b[i]) * (a[i] - b[i]);
    }
    return d2;
  }

  // The living staff within earshot of Jo, nearest first. With
  // pdz_lab_blackout_lines only staff inside a darkened light-group box;
  // `lit` counts the ones that filter dropped.
  std::vector<Staff> NearbyStaff(PPCContext &ctx, uint8_t *base, uint32_t script, size_t *lit)
  {
    std::vector<Staff> out;
    float jo[3] = {};
    const uint32_t player = Player(base);
    const bool have_jo = player && PositionOf(ctx, base, player, jo);
    // pdz_lab_blackout_coverage 3 darkens every room: no box filter then.
    const bool boxes = REXCVAR_GET(pdz_lab_blackout_lines) && lab_restorations::BlackoutCoverage() < 3;
    for (uint32_t slot = kSlotFirst; slot <= kSlotLast; slot += kSlotStep)
    {
      const uint32_t c = Load32(base, script + slot);
      if (!IsGuestPointer(c) || c == player || !Alive(ctx, base, c))
      {
        continue;
      }
      Staff s{c, {}, 0.0f, false};
      if (!PositionOf(ctx, base, c, s.pos))
      {
        continue;
      }
      s.d2 = have_jo ? Distance2(s.pos, jo) : 0.0f;
      if (have_jo && s.d2 > kHearing2)
      {
        continue;
      }
      if (boxes && !lab_restorations::InDarkenedBox(s.pos))
      {
        if (lit)
        {
          ++*lit;
        }
        continue; // his room stays lit
      }
      const uint32_t voice = Load32(base, c + kCharVoiceIndex);
      s.scientist = voice == 15 || voice == 16;
      out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const Staff &a, const Staff &b) { return a.d2 < b.d2; });
    return out;
  }

  void Say(PPCContext &ctx, uint8_t *base, uint32_t character, uint32_t event)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = character + kVoice;
    frame.ctx.r4.u64 = event;
    frame.ctx.r5.u64 = 1; // no _MALE take: the plain line
    frame.ctx.r6.u64 = 0;
    frame.ctx.f1.f64 = 0.0;
    sub_8262AA40(frame.ctx, base);
  }

  // A character's setup id (e.g. s2s1), for the log.
  std::string NameOf(const uint8_t *base, uint32_t character)
  {
    // Sun 22 Nov night (audit): each pointer and the text only while mapped.
    const uint32_t data = pdz::guestmem::Ok(base, character + 0x54, 4) ? Load32(base, character + 0x54) : 0;
    const uint32_t text = pdz::guestmem::Ok(base, data + 0x34, 4) ? Load32(base, data + 0x34) : 0;
    std::string s;
    for (int i = 0; pdz::guestmem::Ok(base, text, 32) && i < 31 && base[text + i]; ++i)
    {
      s += char(base[text + i]);
    }
    return s.empty() ? std::string("?") : s;
  }

  // Round 8: which of Rare's two recorded sets a scientist says his lights-out lines from: the set of his voice
  // number (15 SCI1, 16 SCI2), unless pdz_lab_lights_out_sci1 names him. The two hub scientists both carry voice
  // number 16, but Rare cast them as two men: s2s1's own idle mutter (LV05_SPEECH_SCIENTIST_30) and his lines of the
  // cut glyph exchange (32, 34, 36) are in the SCI1 man's bank (sci1_lv05), s2s2's (29; 31, 33, 35) in the SCI2
  // man's; so s2s1 speaks SCI1 here.
  bool SpeaksSci1(const uint8_t *base, uint32_t character)
  {
    const std::string list = "," + std::string(REXCVAR_GET(pdz_lab_lights_out_sci1)) + ",";
    if (list.find("," + NameOf(base, character) + ",") != std::string::npos)
    {
      return true;
    }
    return !pdz::guestmem::Ok(base, character + kCharVoiceIndex, 4) || // (Sun 22 Nov night, audit)
           Load32(base, character + kCharVoiceIndex) != 16;
  }

  // XACT_SOUNDBANK_<SCI1|SCI2>_<line> as a positioned sound on the speaker.
  void PlayOn(PPCContext &ctx, uint8_t *base, uint32_t character, const std::string &line, uint32_t takes = 0)
  {
    const uint32_t voice = Load32(base, character + kCharVoiceIndex);
    const std::string name = std::string("XACT_SOUNDBANK_") + (SpeaksSci1(base, character) ? "SCI1_" : "SCI2_") + line;
    if (name.size() + 1 > kNameSize)
    {
      return;
    }
    if (takes)
    {
      lab_stealth::OrderTake(base, name.substr(15), takes); // round 11b: only these takes (the lamp trigger)
    }
    if (!g_ref)
    {
      g_ref = REX_KERNEL_MEMORY()->SystemHeapAlloc(kRefSize + kNameSize);
      if (!g_ref)
      {
        return;
      }
    }
    const uint32_t text = g_ref + kRefSize;
    std::memcpy(base + text, name.c_str(), name.size() + 1);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g_ref;
      frame.ctx.r4.u64 = text;
      sub_82202D20(frame.ctx, base);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = g_ref;
    frame.ctx.r4.u64 = 0;
    frame.ctx.r6.u64 = character; // attached to the speaker
    frame.ctx.r7.u64 = 1;
    frame.ctx.f1.f64 = 0.0;
    sub_821FF3C8(frame.ctx, base);
    lab_stealth::WatchLine(name.substr(15)); // round 11d: its real start (or its failure) is logged
    REXLOG_INFO("pdz_restore: lights out: {} is asked to say {} ({:08X}, voice {}); its real start is logged next",
                NameOf(base, character), name.substr(15), character, voice);
  }

  // Round 11c (item 6; log 005 19:20:34 and 19:24:36: the talk for shot-out lamps was made and started by XACT as
  // a sound on the speaker and nothing was heard): the lamp talk's lines go through Rare's speech (lab_stealth's
  // warning-line route: it waits for the gap, is asked for until the sound has started, and logs WARNING LINE PLAYED
  // / NOT PLAYED). The cue keeps its set, so s2s1 still asks from the SCI1 set (the Mesa take). Only when the
  // stealth code does not track the speaker is it played on him as before.
  void LampLine(PPCContext &ctx, uint8_t *base, uint32_t character, const std::string &line, uint32_t takes)
  {
    const std::string cue = std::string(SpeaksSci1(base, character) ? "SCI1_" : "SCI2_") + line;
    if (lab_stealth::StaffLine(base, character, cue, takes, 4.2f, "lights-out talk for shot-out lamps"))
    {
      REXLOG_INFO("pdz_restore: lights out (shot-out lamps): {} is to say {} through Rare's speech ({:08X})",
                  NameOf(base, character), cue, character);
      return;
    }
    REXLOG_INFO("pdz_restore: lights out (shot-out lamps): {} is not tracked by the stealth code: {} is played on him as a "
                "sound (it may not be heard)",
                NameOf(base, character), cue);
    PlayOn(ctx, base, character, line, takes);
  }

  void LightsOut(PPCContext &ctx, uint8_t *base, uint32_t script)
  {
    size_t lit = 0;
    const std::vector<Staff> staff = NearbyStaff(ctx, base, script, &lit);
    if (REXCVAR_GET(pdz_lab_blackout_lines))
    {
      if (lab_restorations::BlackoutCoverage() < 3)
      {
        REXLOG_INFO("pdz_lab_blackout_lines: lights out: {} staff within 30 m inside a darkened box, {} in lit rooms skipped",
                    staff.size(), lit);
      }
      else
      {
        REXLOG_INFO("pdz_lab_blackout_lines: lights out: {} staff within 30 m (coverage 3: every room is dark, no box filter)",
                    staff.size());
      }
    }
    // Round 8: the scientists' conversation waits for Jo (Conversation below); the other staff comment now.
    size_t n = 0;
    for (const Staff &s : staff)
    {
      if (n >= size_t(kFirstSpeakers))
      {
        break;
      }
      if (s.scientist)
      {
        continue;
      }
      Say(ctx, base, s.character, kLightShotOut);
      REXLOG_INFO("pdz_restore: lights out: {} says LIGHT_SHOT_OUT ({:08X})", NameOf(base, s.character), s.character);
      ++n;
    }
    REXLOG_INFO("pdz_restore: lights out: LIGHT_SHOT_OUT from {} of {} staff within 30 m; the scientists' conversation "
                "waits until Jo is within {:.0f} cm of one who stands in the dark",
                n, staff.size(), double(REXCVAR_GET(pdz_lab_lights_out_earshot_cm)));
  }

  // Round 8: after the blackout, twice a second: a scientist in the dark whom Jo has come within earshot of has his
  // lights-out line, with a partner if one stands in the dark within 15 m of him. Under g_light_mutex.
  void Conversation(PPCContext &ctx, uint8_t *base)
  {
    const uint32_t script = g_light_script;
    if (!g_dark || !IsGuestPointer(script) || lab_restorations::CurrentScript() != script || g_pending.character)
    {
      return;
    }
    const auto now = Clock::now();
    if (now < g_conv_check)
    {
      return;
    }
    g_conv_check = now + std::chrono::milliseconds(500);
    const uint32_t flags = Load32(base, script + 0x3EC);
    if (IsGuestPointer(flags) && (Load32(base, flags + 4) & (1u << 8)) != 0)
    {
      return; // flag 40: the power is back (pdz_lab_power_return): nobody stands in the dark
    }
    float jo[3] = {};
    const uint32_t player = Player(base);
    if (!player || !PositionOf(ctx, base, player, jo))
    {
      return;
    }
    const bool boxes = REXCVAR_GET(pdz_lab_blackout_lines) && lab_restorations::BlackoutCoverage() < 3;
    const float earshot = float(REXCVAR_GET(pdz_lab_lights_out_earshot_cm));
    struct Sci
    {
      uint32_t c;
      float pos[3];
      float d2;
    };
    std::vector<Sci> dark;
    for (uint32_t slot = kSlotFirst; slot <= kSlotLast; slot += kSlotStep)
    {
      const uint32_t c = Load32(base, script + slot);
      if (!IsGuestPointer(c) || c == player || !pdz::guestmem::Ok(base, c, kCharVoiceIndex + 4)) // (Sun 22 Nov night, audit)
      {
        continue;
      }
      const uint32_t voice = Load32(base, c + kCharVoiceIndex);
      if ((voice != 15 && voice != 16) || std::find(g_spoke.begin(), g_spoke.end(), c) != g_spoke.end() ||
          (g_lamp_script == script && std::find(g_lamp_spoke.begin(), g_lamp_spoke.end(), c) != g_lamp_spoke.end()))
      {
        continue; // (round 11b: once per scientist, whichever trigger it was)
      }
      Sci s{c, {}, 0.0f};
      if (!PositionOf(ctx, base, c, s.pos) || (boxes && !lab_restorations::InDarkenedBox(s.pos)) || !Alive(ctx, base, c) ||
          !lab_stealth::CalmStaff(base, c))
      {
        continue; // (a scientist who has seen Jo has other things to say)
      }
      s.d2 = Distance2(s.pos, jo);
      const char *why = "";
      if (!Settled(ctx, base, c, s.pos, now, &why))
      {
        // Round 11d: not while he flees, runs for the alarm or is being shot; he counts once he stays put.
        if (s.d2 <= earshot * earshot && now >= g_unsettled_log)
        {
          g_unsettled_log = now + std::chrono::seconds(5);
          REXLOG_INFO("pdz_restore: lights out: Jo is {:.0f} cm from {} (in the dark), but {}: no lights-out line from him "
                      "until he stays put",
                      std::sqrt(s.d2), NameOf(base, c), why);
        }
        continue;
      }
      dark.push_back(s);
    }
    std::sort(dark.begin(), dark.end(), [](const Sci &a, const Sci &b) { return a.d2 < b.d2; });
    if (dark.empty())
    {
      return;
    }
    // Round 3, Sun 15 Nov (L, item 9 (a); log 002 18:54:19: Jo came into the main hall at its far end and had to walk
    // round to within 15 m of the hub pair). The nearest scientist in the dark within earshot (walls or not), as
    // before; else, with pdz_lab_lights_out_wide, the nearest one whose darkened room (Rare's blackout box) Jo is in,
    // within pdz_lab_lights_out_room_cm, or within pdz_lab_lights_out_view_cm with no wall between (the stealth
    // layer's AI sight ray, head to head; at most four rays a look, nearest first).
    size_t pick = dark.size();
    std::string how = fmt::format("within {:.0f} cm of", earshot);
    if (dark.front().d2 <= earshot * earshot)
    {
      pick = 0;
    }
    else if (REXCVAR_GET(pdz_lab_lights_out_wide))
    {
      const int jo_room = lab_restorations::DarkenedBoxOf(jo);
      const float room_cm = float(REXCVAR_GET(pdz_lab_lights_out_room_cm));
      const float view_cm = float(REXCVAR_GET(pdz_lab_lights_out_view_cm));
      const float jo_head[3] = {jo[0], jo[1] + 140.0f, jo[2]};
      int rays = 0;
      for (size_t i = 0; i < dark.size() && pick == dark.size(); ++i)
      {
        const float d = std::sqrt(dark[i].d2);
        if (jo_room != 0 && d <= room_cm && lab_restorations::DarkenedBoxOf(dark[i].pos) == jo_room)
        {
          pick = i;
          how = fmt::format("into his darkened room (blackout area {}, {:.0f} cm from him, pdz_lab_lights_out_room_cm {:.0f}) of",
                            jo_room, d, room_cm);
        }
        else if (d <= view_cm && rays < 4)
        {
          ++rays;
          const float head[3] = {dark[i].pos[0], dark[i].pos[1] + 150.0f, dark[i].pos[2]};
          if (lab_stealth::SightLineClear(ctx, base, jo_head, head))
          {
            pick = i;
            how = fmt::format("within {:.0f} cm with no wall between (pdz_lab_lights_out_view_cm) of", view_cm);
          }
        }
      }
    }
    if (pick >= dark.size())
    {
      return;
    }
    const Sci &a = dark[pick];
    const bool wide = !(dark.front().d2 <= earshot * earshot); // (the trigger was one of the wide rules)
    if (!lab_stealth::SpeechFree(base))
    {
      if (now >= g_conv_wait_log)
      {
        g_conv_wait_log = now + std::chrono::seconds(5);
        REXLOG_INFO("pdz_restore: lights out: Jo is {:.0f} cm from {} (in the dark): his line waits for story speech or a "
                    "guard's line to end",
                    std::sqrt(a.d2), NameOf(base, a.c));
      }
      return;
    }
    if (wide)
    {
      // (the start lines below say "came within <earshot> cm"; this one says which rule it was)
      REXLOG_INFO("pdz_stealth_r3: LIGHTS OUT: Jo came {} {} ({:.0f} cm from him, beyond the {:.0f} cm earshot): the "
                  "lights-out conversation starts now (pdz_lab_lights_out_wide)",
                  how, NameOf(base, a.c), std::sqrt(a.d2), earshot);
    }
    const Sci *b = nullptr;
    for (size_t i = 0; i < dark.size(); ++i)
    {
      if (i == pick)
      {
        continue; // (round 3: the scientist the trigger chose need not be the nearest)
      }
      if (Distance2(a.pos, dark[i].pos) <= kPair2 && (!b || Distance2(a.pos, dark[i].pos) < Distance2(a.pos, b->pos)))
      {
        b = &dark[i];
      }
    }
    if (b && SpeaksSci1(base, b->c) && !SpeaksSci1(base, a.c))
    {
      // A pair of two voices: the SCI1 man asks (his question set holds the "Mesa" take), the SCI2 man answers.
      const Sci *asks = b;
      static Sci answers;
      answers = a;
      g_spoke.push_back(asks->c);
      g_spoke.push_back(answers.c);
      REXLOG_INFO("pdz_restore: lights out: Jo came within {:.0f} cm of {} in the dark ({:.0f} cm): the conversation: {} "
                  "(SCI1 set) asks, {} (SCI2 set, {:.0f} cm from him) answers in {:.1f} s",
                  earshot, NameOf(base, answers.c), std::sqrt(answers.d2), NameOf(base, asks->c), NameOf(base, answers.c),
                  std::sqrt(Distance2(asks->pos, answers.pos)), std::chrono::duration<double>(kAnswerDelay).count());
      PlayOn(ctx, base, asks->c, "LIGHTS_OUT_NEXT_QUESTION");
      g_pending = PendingLine{answers.c, "LIGHTS_OUT_NEXT_ANSWER", now + kAnswerDelay};
      lab_stealth::SpeechBusy(base, 8.0f);
      return;
    }
    g_spoke.push_back(a.c);
    if (b)
    {
      g_spoke.push_back(b->c);
      REXLOG_INFO("pdz_restore: lights out: Jo came within {:.0f} cm of {} at ({:.0f} {:.0f} {:.0f}) in the dark ({:.0f} cm): "
                  "the conversation: he asks, {} ({:.0f} cm from him) answers in {:.1f} s",
                  earshot, NameOf(base, a.c), a.pos[0], a.pos[1], a.pos[2], std::sqrt(a.d2), NameOf(base, b->c),
                  std::sqrt(Distance2(a.pos, b->pos)), std::chrono::duration<double>(kAnswerDelay).count());
      PlayOn(ctx, base, a.c, "LIGHTS_OUT_NEXT_QUESTION");
      g_pending = PendingLine{b->c, "LIGHTS_OUT_NEXT_ANSWER", now + kAnswerDelay};
      lab_stealth::SpeechBusy(base, 8.0f);
    }
    else
    {
      REXLOG_INFO("pdz_restore: lights out: Jo came within {:.0f} cm of {} at ({:.0f} {:.0f} {:.0f}) in the dark ({:.0f} cm): no "
                  "other scientist in the dark within 15 m of him: his solo line",
                  earshot, NameOf(base, a.c), a.pos[0], a.pos[1], a.pos[2], std::sqrt(a.d2));
      PlayOn(ctx, base, a.c, "LIGHTS_OUT_NEXT_SOLO");
      lab_stealth::SpeechBusy(base, 4.0f);
    }
  }
} // namespace

namespace restorations
{
  // Round 11j: which of Rare's two scientist sets this scientist speaks (lab_stealth.cpp: his warnings and lamp
  // comments use the same set as his lights-out lines).
  bool SpeaksSci1(const uint8_t *base, uint32_t character)
  {
    return IsGuestPointer(character) && ::SpeaksSci1(base, character);
  }

  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start. Flags only (any thread, no lock): the memory is
  // dropped at the next use (ApplyLevelReset; the test key's timed messages in the message hook).
  void LevelReset()
  {
    g_level_reset.store(true, std::memory_order_relaxed);
    g_key_reset.store(true, std::memory_order_relaxed);
  }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    if (!REXCVAR_GET(pdz_restore_light_speech) && !REXCVAR_GET(pdz_lab_blackout_lines) &&
        !g_lamp_talk_used.load(std::memory_order_relaxed))
    {
      return;
    }
    lab_stealth::WatchTick(); // round 11d: a line that never started is logged
    std::lock_guard<std::mutex> lock(g_light_mutex);
    ApplyLevelReset(); // (stealth round 3c, fix 1)
    Conversation(ctx, base);
    if (!g_pending.character || Clock::now() < g_pending.when)
    {
      return;
    }
    const PendingLine line = g_pending;
    g_pending = PendingLine{};
    if (Alive(ctx, base, line.character))
    {
      if (line.takes)
      {
        LampLine(ctx, base, line.character, line.cue, line.takes); // (round 11c: the lamp talk's answer)
      }
      else
      {
        PlayOn(ctx, base, line.character, line.cue, line.takes);
      }
    }
  }

  // Round 11b (item 6): the lights-out talk for lamps Jo shot out (lab_stealth.cpp decides when and who). Only the
  // takes that suit a local outage of a few lights (the others stay for the generator blackout):
  //   SCI1 question 1 2 3 5, SCI2 question 3 4 5; SCI1 answer 1 3, SCI2 answer 1 3 4; SCI1 solo 1 2, SCI2 solo 1 2 4 5.
  // `b` 0: `a` says his solo line. False: it cannot start now (an answer is pending, `a` has had it, the switch is off).
  bool LampTalk(PPCContext &ctx, uint8_t *base, uint32_t a, uint32_t b)
  {
    // Follow-up (Sun 15 Nov): the stealth layer's lamp talk no longer needs pdz_restore_light_speech (a separate
    // restoration: the generator blackout's lines; on by default since round 3). lab_stealth.cpp asks only with pdz_lab_lamp_talk
    // on and the layer running on the Laboratory.
    if (!IsGuestPointer(a))
    {
      return false;
    }
    g_lamp_talk_used.store(true, std::memory_order_relaxed);
    std::unique_lock<std::mutex> lock(g_light_mutex, std::try_to_lock);
    if (lock.owns_lock())
    {
      ApplyLevelReset(); // (stealth round 3c, fix 1)
    }
    if (!lock.owns_lock() || g_pending.character)
    {
      return false;
    }
    const uint32_t script = lab_restorations::CurrentScript();
    if (script != g_lamp_script)
    {
      g_lamp_script = script; // a new run of the level
      g_lamp_spoke.clear();
    }
    // (round 3, Sun 15 Nov, L: nor a scientist who had the generator blackout's conversation in this run: the same
    // recorded lines are never said twice by one man, whichever trigger came first)
    const auto spoke = [script](uint32_t c) {
      return std::find(g_lamp_spoke.begin(), g_lamp_spoke.end(), c) != g_lamp_spoke.end() ||
             (script == g_light_script && std::find(g_spoke.begin(), g_spoke.end(), c) != g_spoke.end());
    };
    if (spoke(a) || !Alive(ctx, base, a))
    {
      return false;
    }
    if (b && (!IsGuestPointer(b) || spoke(b) || !Alive(ctx, base, b)))
    {
      b = 0;
    }
    g_lamp_spoke.push_back(a);
    if (!b)
    {
      REXLOG_INFO("pdz_restore: lights out (shot-out lamps): {} is alone: his solo line", NameOf(base, a));
      LampLine(ctx, base, a, "LIGHTS_OUT_NEXT_SOLO", SpeaksSci1(base, a) ? 0x03u : 0x1Bu);
      return true;
    }
    g_lamp_spoke.push_back(b);
    uint32_t asks = a, answers = b;
    if (SpeaksSci1(base, b) && !SpeaksSci1(base, a))
    {
      asks = b; // a pair of two voices: the SCI1 man asks (his question set holds the "Mesa" take)
      answers = a;
    }
    REXLOG_INFO("pdz_restore: lights out (shot-out lamps): the conversation: {} ({} set) asks, {} ({} set) answers in {:.1f} s",
                NameOf(base, asks), SpeaksSci1(base, asks) ? "SCI1" : "SCI2", NameOf(base, answers),
                SpeaksSci1(base, answers) ? "SCI1" : "SCI2", std::chrono::duration<double>(kAnswerDelay).count());
    LampLine(ctx, base, asks, "LIGHTS_OUT_NEXT_QUESTION", SpeaksSci1(base, asks) ? 0x17u : 0x1Cu);
    g_pending = PendingLine{answers, "LIGHTS_OUT_NEXT_ANSWER", Clock::now() + kAnswerDelay,
                            SpeaksSci1(base, answers) ? 0x05u : 0x0Du};
    return true;
  }
} // namespace restorations

// --- pdz_test_lab_blackout_key (testing tool) ---
// The Home key sends the generator blackout to the Laboratory script as the
// generator switch did in log 114: in one message the lights (r5 bit 0x40,
// 8232A264), Chandra's CHAN_06 with the mast sound (r7 high word bit 0x8,
// 823280F0) and the darkness (r7 high word bit 0x10, 823282D4); then
// TANNOY_TRIGGER02 (r6 bit 0x2) 10 s later and TANNOY_TRIGGER03 (r6 bit 0x4)
// 52 s later. The generator alarm (r7 high word bit 0x4: CHAN_04 and
// TANNOY_TRIGGER01) wasn't heard in log 114 and isn't sent. The bits are
// merged into the next call of Rare's handler (only the Laboratory script has
// it, so the key does nothing elsewhere); with the switch on, every message
// the handler gets is logged, so a real switch use can be compared.
namespace
{
  struct TimedBits
  {
    Clock::time_point when;
    uint64_t r5 = 0, r6 = 0, r7 = 0;
    const char *what = "";
  };
  std::atomic<bool> g_blackout_key{false};
  std::atomic<bool> g_key_pending{false}; // timed bits still to send (pdz_test_lab_blackout_now's later tannoys)
  std::mutex g_key_mutex;
  std::vector<TimedBits> g_key_bits;
} // namespace

namespace restorations
{
  void RegisterBinds()
  {
    rex::ui::RegisterBind("bind_test_lab_blackout", "Home",
                          "Testing tool: Laboratory generator blackout (pdz_test_lab_blackout_key)", []
                          {
      static Clock::time_point last_event{};
      const auto now = Clock::now();
      const bool fresh = now - last_event > std::chrono::milliseconds(300);
      last_event = now;
      if (fresh && REXCVAR_GET(pdz_test_lab_blackout_key))
      {
        g_blackout_key.store(true);
      } });
  }
} // namespace restorations

// sub_823272E0: the Laboratory script's message handler (r3 = the script;
// r5-r9 = 64-bit message masks). pdz_test_lab_blackout_now (testing tool,
// for input scripts) sends the same messages as the key when its value
// changes to 1; to 2 it sends the switch route instead: CHAN_04 +
// TANNOY_TRIGGER01 (flag 130, r7 high bit 0x4) in place of CHAN_06, plus the
// switch-route marker flag 37 (r5 low bit 0x20; Rare's handler never tests
// it, only lab_restorations.cpp's pdz_lab_power_return does).
REX_HOOK_RAW(sub_823272E0)
{
  const int32_t blackout_now = REXCVAR_GET(pdz_test_lab_blackout_now);
  static std::atomic<int32_t> last_now{0};
  int route = 0; // 0 none, 1 lockbot-style (CHAN_06), 2 the switch (CHAN_04 + flag 37)
  if (blackout_now != last_now.load())
  {
    last_now.store(blackout_now);
    if (blackout_now == 1 || blackout_now == 2)
    {
      route = int(blackout_now);
    }
  }
  if (g_key_reset.exchange(false, std::memory_order_relaxed))
  {
    // Stealth round 3c (fix 1): a level start: the test key's timed messages of the attempt before are dropped.
    std::lock_guard<std::mutex> lock(g_key_mutex);
    g_key_bits.clear();
    g_key_pending.store(false);
    g_blackout_key.store(false);
  }
  if (REXCVAR_GET(pdz_test_lab_blackout_key) || route != 0 || g_key_pending.load())
  {
    std::lock_guard<std::mutex> lock(g_key_mutex);
    const auto now = Clock::now();
    if (g_blackout_key.exchange(false))
    {
      route = 1;
    }
    if (route)
    {
      g_key_bits.clear();
      g_key_pending.store(true);
      if (route == 2)
      {
        g_key_bits.push_back({now, 0x40 | 0x20, 0, uint64_t(0x4 | 0x10) << 32,
                              "lights out, CHAN_04 + TANNOY_TRIGGER01, darkness, switch-route marker (flag 37)"});
      }
      else
      {
        g_key_bits.push_back({now, 0x40, 0, uint64_t(0x8 | 0x10) << 32, "lights out, CHAN_06, darkness"});
      }
      g_key_bits.push_back({now + std::chrono::seconds(10), 0, 0x2, 0, "TANNOY_TRIGGER02"});
      g_key_bits.push_back({now + std::chrono::seconds(52), 0, 0x4, 0, "TANNOY_TRIGGER03"});
      REXLOG_INFO("pdz_test: blackout {} in Laboratory (script {:08X}), route {}",
                  route == int(blackout_now) ? "cvar (pdz_test_lab_blackout_now)" : "key pressed", ctx.r3.u32,
                  route == 2 ? "switch" : "lockbot");
    }
    for (auto it = g_key_bits.begin(); it != g_key_bits.end();)
    {
      if (now < it->when)
      {
        ++it;
        continue;
      }
      ctx.r5.u64 |= it->r5;
      ctx.r6.u64 |= it->r6;
      ctx.r7.u64 |= it->r7;
      REXLOG_INFO("pdz_test: blackout key: sent {} (r5 {:X}, r6 {:X}, r7 {:X})", it->what, it->r5, it->r6, it->r7);
      it = g_key_bits.erase(it);
    }
    g_key_pending.store(!g_key_bits.empty());
    if (REXCVAR_GET(pdz_test_lab_blackout_key) && (ctx.r5.u64 | ctx.r6.u64 | ctx.r7.u64 | ctx.r8.u64 | ctx.r9.u64))
    {
      REXLOG_INFO("pdz_test: Laboratory message: r5 {:016X} r6 {:016X} r7 {:016X} r8 {:016X} r9 {:016X}", ctx.r5.u64,
                  ctx.r6.u64, ctx.r7.u64, ctx.r8.u64, ctx.r9.u64);
    }
  }
  const uint32_t script = ctx.r3.u32;
  const bool lights = (ctx.r5.u64 & kLightsMessage) != 0;
  const lab_restorations::Message message{ctx.r5.u64, ctx.r6.u64, ctx.r7.u64, ctx.r8.u64, ctx.r9.u64};
  const bool lab = IsGuestPointer(script) && (message.r5 | message.r6 | message.r7 | message.r8 | message.r9) != 0;
  if (lab)
  {
    lab_restorations::BeforeLabMessage(ctx, base, script, message);
  }
  __imp__sub_823272E0(ctx, base);
  if (lab)
  {
    lab_restorations::AfterLabMessage(ctx, base, script, message);
  }
  if (!lights || !IsGuestPointer(script) ||
      (!REXCVAR_GET(pdz_restore_light_speech) && !REXCVAR_GET(pdz_lab_blackout_lines)))
  {
    return;
  }
  std::lock_guard<std::mutex> lock(g_light_mutex);
  ApplyLevelReset(); // (stealth round 3c, fix 1)
  if (script != g_light_script)
  {
    g_light_script = script; // a new run of the level
    g_dark = false;
    g_pending = PendingLine{};
    g_spoke.clear();
    g_track.clear();
  }
  if (g_dark)
  {
    REXLOG_INFO("pdz_restore: lights message again, already dark");
    return;
  }
  g_dark = true;
  LightsOut(ctx, base, script);
}
