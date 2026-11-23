// What the player really hears (Fri 20 Nov 2026 night; Sat 21 Nov evening: F12 listing pdz_snd_heard_f12 on, the
// continuous lines pdz_snd_heard_log off; Debug). TU3 addresses. LOG ONLY:
// nothing here changes a sound, a voice, the AI or anything the player hears. Research:
// research notes: RESEARCH_heard_Fri20Nov.md.
//
// WHY. Our estimate of a restored sound's loudness at the player (loudness x (1 - distance / range)) said 0.49-0.66
//   for a pigeon's coo that was not heard at all. So the numbers here are the ENGINE'S OWN, read where they land in
//   the guest's XAudio (the 360's XAudio mixes in guest code; the SDK's audio_mute only zeroes the host output in
//   SDLAudioDriver, so every value below is the same muted or not, and the master / host gain is not in them).
// THE CHAIN, per 3D sound (READ in the generated code):
//   1. XACT plays the cue's recording on an XAudio SOURCE VOICE and sets its volume with the voice's SetVolume
//      (wrapper sub_826AD238: stores voice +140, then applies +140 x the XAudio voice category's volume, the global
//      [82E421E0] +72 / +76 for voice category +144 = 0 / 1, else 1.0; sub_826B7930). XACT's number = the cue's
//      sound / track / variation / category / RPC volume as XACT has combined it. Read: voice +140 and the category.
//   2. Rare routes the cue to its sound GROUP's submix voice (sub_8275E420, from the group join sub_821FD7D0; the cue's
//      SetOutputVoices) with a channel map at volume = sound +332 (or 1.0 for a sound with byte +344 set: footsteps).
//      The XAudio SetOutputVoices wrapper sub_826ACBB0 (voice r3, {count, {dest voice, map}}) is hooked: the map that
//      lands on the source voice is the SEND.
//   3. Rare's 3D pass (sub_821FCFE0 -> sub_8275E708, X3DAudio) sets the group voice's output map every audio update:
//      X3DAudio's coefficients x f2 (= group +96, the loudness copied from sound +332, times our wall cut: walls_
//      Fri20Nov.cpp multiplies f2) through the SetOutputMaps wrapper sub_826ACC08 (voice r3, {count, {output index,
//      map}}), hooked: the map gain is the 3D GAIN; when the call comes from the 3D pass (return 8275EA8C) the f2 it
//      used is still in f27 (sub_8275E708 keeps it there), so distance / pan = 3D gain / f27 and wall cut = f27 / +96.
//   A map's gain: for each source channel, sqrt(sum of its coefficients squared) (X3DAudio pans with constant power),
//   the largest of them. HEARD = xact x send x 3D gain, before the master voice.
//   The voice destroy wrapper sub_826AC9C0 forgets a voice. These four hooks only copy a few floats into a table
//   under a spin lock (no syscalls, no guest calls).
// LINES (all "pdz_snd_heard:"):
//   - each restored cue's first 3 starts per level (0.4 s after it starts, when it has joined its group), then every
//     2 s while any restored cue plays (pdz_snd_heard_cues: which cues count as restored);
//   - F12 (the mark key, via restored_sounds_r3's pointer) or pdz_snd_heard_now set to a new non-zero value: a
//     LISTING of every sound playing now (placed ones with the whole chain, loudest first; unplaced ones by name),
//     with the listener's and Jo's places;
//   - each line carries the REFERENCE: what Rare's STOCK_FSTEP_CONC_WALK (a guard's footstep: kind 3, range 12 m)
//     would give at the same distance through the same chain: its xact x send x loudness as measured on live footsteps
//     this session (else the bank's figures), x the distance curve measured on this session's groups at
//     distance / 12 m (else Rare's straight line), x the same wall cut.
//   - SHOT HEARING (item 3): for every loud shot (Rare's perception profile noise radius +0x30 rising to 10 m or
//     more; profiles 82E5D590, 0x50 each) and every loud sound event (82D2D124 list, radius 10 m or more: explosions),
//     one line per character whose hearing loop (sub_8218AAC8, the stealth layer's hook calls in) runs within reach:
//     heard / blocked by N walls (the stealth layer's rule: lab_stealth::SoundWallsBetween, the radius halved per
//     wall) / out of Rare's range (distance >= radius x his profile's hearing +0x38). Where the stealth layer does not
//     act for him (Counter-Op, a level or zone without it), Rare's own decision: distance only, no wall test.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/func.h>
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DECLARE(bool, pdz_stealth_wall_muffle); // lab_stealth.cpp
REXCVAR_DECLARE(bool, pdz_snd_wall_cut);        // walls_Fri20Nov.cpp

// Sat 21 Nov evening (log 034's crash; the split): the F12 listing (pdz_snd_heard_f12, on) and the continuous lines
// (pdz_snd_heard_log: START / PLAYING, LOUD SHOT / LOUD SOUND EVENT / SHOT HEARING; now OFF by default: a debug tool
// is off in the release) are two switches. The mixer hooks keep their table while either is on.
REXCVAR_DEFINE_BOOL(pdz_snd_heard_log, false, "Debug",
                    "Log only: continuous lines of what the player really hears, read where the game's own mixer "
                    "gets it (the cue's volume, the send to its 3D group, the 3D gain), with a guard's footstep at "
                    "the same distance for comparison ('pdz_snd_heard:' lines: restored sounds' first starts and "
                    "every 2 s, and who hears each loud shot). The F12 listing is pdz_snd_heard_f12. Changes nothing")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_heard_f12, true, "Debug",
                    "Log only: F12 writes a 'what can I hear here' listing to the log: every sound playing now, "
                    "with its owner, distance, walls and the level the game's own mixer gives it. Changes nothing")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_heard_wall_probe, false, "Testing",
                    "Test aid (Sat 21 Nov evening): each listed placed sound in an F12 / pdz_snd_heard_now listing also "
                    "gets its wall counts by each ray kind and how enclosed it and the listener are (many rays: a test "
                    "tool, not for play)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_heard_first_ms, 400, "Testing",
                     "Test aid (Sun 22 Nov): the heard-level meter's first reading of a listed sound is taken this many "
                     "ms after it starts (400 = before; a short hit of 0.3 s needs about 60)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_heard_first_n, 3, "Testing",
                     "Test aid (Sun 22 Nov): how many starts of each listed sound get a START line (3 = before)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_heard_who_test, false, "Testing",
                    "Test aid: at each level start, the heard-level meter's name helper is called on every object "
                    "in Rare's two character lists and the perception profiles, and each name (or hex id) is logged "
                    "('pdz_snd_heard: WHO TEST' lines)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_heard_now, 0, "Testing",
                     "Test aid for pdz_snd_heard_log: each time this changes to a new non-zero value, the full 'what "
                     "can I hear here' listing (as F12 gives it) is logged, tagged with the value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_snd_heard_cues,
                      "STOCK_PIGEON_COO,STOCK_PIGEON_FLAPS,STOCK_FLUOR_BUZZ,STOCK_FUSEBOX_HUM_LP,STOCK_FLIES_BUZZING,"
                      "STOCK_COMPUTER_LARGE_LP,STOCK_VEND_HUM,STOCK_AQUA_BUBBLE_LP,STOCK_PIPE_FLOW,STOCK_NEONSIGN_HUM,"
                      "STOCK_BODYFALL_,STOCK_AUDIOSCOPE_ZOOM,STOCK_SPARK_SMALL,STOCK_SPARKING,STOCK_METAL_SWING_",
                      "Debug",
                      "pdz_snd_heard_log: comma-separated parts of cue names (after XACT_SOUNDBANK_) that count as "
                      "restored sounds (their starts and every 2 s while one plays are logged)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_826ACBB0); // XAudio voice SetOutputVoices wrapper (r3 voice, r4 -> {u8 count, -> {dest, map}})
REX_EXTERN(__imp__sub_826ACC08); // XAudio voice SetOutputMaps wrapper (r3 voice, r4 -> {u8 count, -> {u8 index, map}})
REX_EXTERN(__imp__sub_826AC9C0); // XAudio voice destroy wrapper (r3 voice)
REX_EXTERN(__imp__sub_8275E420); // Rare: route a cue (r3 = cue wrapper, its +0 the XACT cue) to r4 -> {voice} at f1
REX_EXTERN(__imp__sub_821FCFE0); // Rare's 3D pass (r3 = the sound manager; the game thread, while in play)

namespace lab_stealth
{
  int SoundWallsBetween(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3]); // lab_stealth.cpp
}
namespace pdz::walls // walls_Fri20Nov.cpp (Sat 21 Nov evening): the wall cut's own count, and a test probe
{
  int Count(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3], int *straight, int *storeys);
  std::string Probe(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3]);
}
namespace pdz::pace
{
  std::string SpLevelName(const uint8_t *base, std::string *raw); // render/game_pace.cpp
}

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;      // -> the session (+8 == 21 in play)
  constexpr uint32_t kPool = 0x82E6915C;      // +4 the free list, +20 -> the array of 80 sounds x 372
  constexpr int kPoolSize = 80;
  constexpr uint32_t kStride = 372;
  constexpr uint32_t kListeners = 0x82D3EBD4; // 6 listener pointers (+128 position)
  constexpr uint32_t kPlayerOne = 0x82E3C3E4;
  constexpr uint32_t kPhysicsWorld = 0x82E3C2F4;
  constexpr uint32_t kXAudio = 0x82E421E0;    // -> XAudio's globals (+72 / +76 the two voice category volumes)
  constexpr uint32_t kSourceVtable = 0x820DF940, kSubmixVtable = 0x820DF9A0;
  constexpr uint32_t kFrom3D = 0x8275EA8C;    // sub_8275E708's return from its SetOutputMaps call
  constexpr uint32_t kAiClock = 0x82E2F334;   // float seconds (per level)
  constexpr uint32_t kProfileTable = 0x82E5D590;
  constexpr uint32_t kProfiles = 170;         // Rare's hearing loop walks 170
  constexpr uint32_t kProfileSlot = 0x354;    // a character's profile index
  constexpr uint32_t kSoundEvents = 0x82D2D124;
  constexpr char kRefCue[] = "XACT_SOUNDBANK_STOCK_FSTEP_CONC_WALK";
  constexpr float kRefRangeCm = 1200.0f;      // Rare's range picker: kind 3, not _RUN: 12 m

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }
  bool Finite(float f) { return f == f && f > -1.0e7f && f < 1.0e7f; }
  float Db(float g) { return g > 1.0e-6f ? 20.0f * std::log10(g) : -120.0f; }
  // A guest range is mapped and readable (syscalls: only on the log paths, never on a hook).
  bool Readable(uint8_t *base, uint32_t address, uint32_t size)
  {
    // Sat 21 Nov (pdz_fix_readable_cache): the cached check (guest_readable_Sat21Nov.cpp); below = off.
    if (pdz::guestmem::CacheOn())
    {
      return pdz::guestmem::Readable(const_cast<uint8_t *>(base), address, size);
    }
    if (address < 0x10000u || uint64_t(address) + size > 0xFFFF0000ull)
    {
      return false;
    }
    for (uint32_t at = address; at < address + size; at = (at & ~0xFFFu) + 0x1000u)
    {
      size_t length = 1;
      rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
      if (!rex::memory::QueryProtect(base + at, length, access) ||
          (uint32_t(access) & uint32_t(rex::memory::PageAccess::kReadOnly)) == 0)
      {
        return false;
      }
    }
    return true;
  }
  // Sat 21 Nov evening (the crash of log 034, 20:12:26: a name pointer that was name text, 0x53706964 "Spid"): the
  // range [a, a + n) is really mapped and readable now (the cached check), not only inside the guest's range.
  bool Safe(const uint8_t *base, uint32_t a, uint32_t n)
  {
    return a >= 0x10000u && uint64_t(a) + n <= 0xFFFF0000ull && pdz::guestmem::Readable(const_cast<uint8_t *>(base), a, n);
  }
  // Rare's character classes at +12 (17 Nov: every NPC 33, the player 2, a corpse 43).
  bool CharacterClass(uint32_t cls) { return cls == 33u || cls == 2u || cls == 43u; }
  bool MeterOn() { return REXCVAR_GET(pdz_snd_heard_f12) || REXCVAR_GET(pdz_snd_heard_log) || REXCVAR_GET(pdz_snd_heard_who_test); }
  std::string Text(uint8_t *base, uint32_t p, uint32_t max)
  {
    if (!Readable(base, p, max))
    {
      return {};
    }
    std::string s;
    for (uint32_t n = 0; n < max && base[p + n] >= 32 && base[p + n] < 127; ++n)
    {
      s.push_back(char(base[p + n]));
    }
    return s;
  }
  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  bool InPlay(const uint8_t *base)
  {
    const uint32_t session = Load32(base, kGame);
    return Guest(session) && Safe(base, session, 12) && Load32(base, session + 8) == 21;
  }
  bool RaysReady(const uint8_t *base) { return Guest(Load32(base, kPhysicsWorld)) && Guest(Load32(base, kPhysicsWorld - 4)); }

  // Categories in pdz.xgs order (sound +312, the cue handle's low 6 bits; as audio_presence_log.cpp).
  const char *const kCategories[] = {
      "Global", "Default", "Music", "footsteps", "gunshots", "richochets", "shelldrops",
      "speech_characters", "Tanoys", "speech_level", "effect", "Vehicle", "Ambience", "GunBits",
      "Radios", "LevelTunes", "frontend", "speech_radio", "speech_comms", "cutscenes",
      "FrontendTunes", "CutsceneTunes", "cutscenes_front", "cutscenes_rear", "speech_cutscenes"};
  const char *Category(uint32_t i) { return i < std::size(kCategories) ? kCategories[i] : "?"; }

  // ---- The bank's own figures (read offline from the game's banks, not at run time: XACT keeps them inside its
  // engine). sound = the XSB sound's volume (linear), category = its category's volume in pdz.xgs (linear); the
  // recording = RMS / peak dBFS of its wave(s) (mean RMS, loudest peak over the variations). See the research file.
  struct BankCue
  {
    const char *cue; // after XACT_SOUNDBANK_
    float sound_lin;
    int category;
    float category_lin;
    float rms_dbfs, peak_dbfs;
  };
  // (stock.xsb; every track of these is 0 dB and none has an RPC or DSP; category volumes from pdz.xgs. The byte to
  // dB fit is FNA's; 0xB4 = 0 dB. Table: research notes: bank_volumes_Fri20Nov.tsv.)
  constexpr BankCue kBank[] = {
      {"STOCK_PIGEON_COO", 0.176f, 10, 0.897f, -10.9f, -0.0f},       // -15.1 dB; loops (255): the caller must stop it
      {"STOCK_PIGEON_FLAPS", 0.635f, 10, 0.897f, -20.6f, 0.0f},
      {"STOCK_FLUOR_BUZZ", 1.0f, 10, 0.897f, -20.3f, 0.0f},
      {"STOCK_FUSEBOX_HUM_LP", 0.635f, 10, 0.897f, -11.7f, -0.5f},
      {"STOCK_FLIES_BUZZING", 0.795f, 12, 0.897f, -19.6f, -6.8f},
      {"STOCK_COMPUTER_LARGE_LP", 0.098f, 10, 0.897f, -13.9f, -0.2f}, // -20.2 dB
      {"STOCK_VEND_HUM", 0.635f, 10, 0.897f, -8.7f, -0.0f},
      {"STOCK_AQUA_BUBBLE_LP", 0.176f, 10, 0.897f, -16.6f, -0.0f},
      {"STOCK_PIPE_FLOW", 0.635f, 12, 0.897f, -19.5f, 0.0f},
      {"STOCK_NEONSIGN_HUM", 0.249f, 12, 0.897f, -9.4f, 0.0f},
      {"STOCK_BODYFALL_GENERIC", 0.635f, 10, 0.897f, -13.1f, 0.0f},
      {"STOCK_BODYFALL_METAL", 0.635f, 10, 0.897f, -12.7f, 0.0f},
      {"STOCK_BODYFALL_WATER", 1.0f, 10, 0.897f, -17.9f, 0.0f},
      {"STOCK_BODYFALL_WOOD", 0.635f, 10, 0.897f, -13.0f, 0.0f},
      {"STOCK_AUDIOSCOPE_ZOOM", 0.395f, 10, 0.897f, -9.4f, 0.0f},    // loops (255)
      {"STOCK_FSTEP_CONC_WALK", 0.635f, 3, 0.897f, -19.5f, 0.0f},    // the reference
      {"STOCK_SWEEPING_UP", 0.5f, 10, 0.897f, -15.8f, -0.6f},
      {"STOCK_COOLANT_MOTORS", 0.176f, 10, 0.897f, -18.3f, 0.0f},
  };
  const BankCue *BankOf(const char *name)
  {
    if (std::strncmp(name, "XACT_SOUNDBANK_", 15) == 0)
    {
      name += 15;
    }
    for (const BankCue &b : kBank)
    {
      if (std::strcmp(b.cue, name) == 0)
      {
        return &b;
      }
    }
    return nullptr;
  }

  // ---- The voice table: written by the XAudio hooks (any thread), read by the log paths (the game thread). ----
  struct Spin
  {
    std::atomic_flag f = ATOMIC_FLAG_INIT;
    void lock()
    {
      while (f.test_and_set(std::memory_order_acquire))
      {
      }
    }
    void unlock() { f.clear(std::memory_order_release); }
  };
  struct Guard
  {
    Spin &s;
    explicit Guard(Spin &s_) : s(s_) { s.lock(); }
    ~Guard() { s.unlock(); }
  };
  constexpr int kOuts = 4;
  struct VoiceRec
  {
    uint32_t dest[kOuts] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}; // 0 = the master; ~0 = not seen
    float gain[kOuts] = {-1.0f, -1.0f, -1.0f, -1.0f};                            // -1 = XAudio's default map
    uint32_t cue = 0;      // Rare's cue wrapper that routed it (sub_8275E420 running on this thread), 0 = not seen
    float f2 = -1.0f;      // the loudness x wall cut the 3D pass used for this (group) voice
    uint32_t f2_tick = 0;  // the audio update of that
  };
  Spin g_lock;
  std::unordered_map<uint32_t, VoiceRec> g_voices; // key: the XAudio voice
  struct CueRoute
  {
    uint32_t group_voice = 0;
    float send = -1.0f;
  };
  std::unordered_map<uint32_t, CueRoute> g_routes; // key: Rare's cue wrapper (link +4)
  std::atomic<uint32_t> g_tick{0};                 // audio updates (the 3D pass hook)
  thread_local uint32_t t_cue = 0;

  // A channel map {u8 count, -> entries of {u8 dest channel, u8 source channel, -, -, float volume}}: per source
  // channel sqrt(sum of volume^2), the largest.
  float MapGain(const uint8_t *base, uint32_t map)
  {
    if (!Guest(map) || !pdz::guestmem::Ok(base, map, 8)) // (Sun 22 Nov night (audit): only where mapped)
    {
      return -1.0f;
    }
    const uint32_t count = base[map], entries = Load32(base, map + 4);
    if (count == 0 || count > 64 || !Guest(entries) || !pdz::guestmem::Ok(base, entries, 8 * count))
    {
      return count == 0 ? 0.0f : -1.0f;
    }
    float sum[8] = {};
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t e = entries + 8 * i;
      const uint32_t src = base[e + 1] & 7u;
      const float v = LoadF(base, e + 4);
      if (Finite(v))
      {
        sum[src] += v * v;
      }
    }
    float best = 0.0f;
    for (float s : sum)
    {
      best = std::max(best, s);
    }
    return std::sqrt(best);
  }

  // ---- Measuring one sound ----
  struct Meas
  {
    uint32_t sound = 0, group = 0, group_voice = 0, owner = 0, flags = 0, state = 0, kind = 0;
    std::string name, owner_text;
    bool placed = false, in_group = false;
    float pos[3] = {}, gpos[3] = {}, ears[3] = {};
    float d = -1.0f, range = 0.0f, loud = 0.0f, g96 = -1.0f, grange = 0.0f, span = 0.0f, lowpass = 0.0f;
    bool single = false; // byte +344: sent at 1.0 (footsteps)
    int walls = -1;
    std::string probe; // pdz_snd_heard_wall_probe
    // From the mixer:
    float xact = -1.0f, voice_vol = -1.0f, voice_cat = -1.0f, send = -1.0f, gain3d = -1.0f, f2 = -1.0f;
    int sources = 0, sources_linked = 0;
    bool f2_live = false;
    float heard = -1.0f;
  };

  float CatVolume(const uint8_t *base, uint32_t category)
  {
    const uint32_t g = Load32(base, kXAudio);
    if (category >= 2 || !Guest(g) || !Safe(base, g + 72, 8))
    {
      return 1.0f;
    }
    const float v = LoadF(base, g + 72 + 4 * category);
    return Finite(v) ? v : 1.0f;
  }

  std::string OwnerText(uint8_t *base, uint32_t owner, uint32_t flags, bool restored)
  {
    if (!(flags & 0x8u) || !owner)
    {
      return restored ? "none (a fixed place: one of ours)"
                      : "none (a fixed place: Rare's placed level emitter or a one-off at a spot)";
    }
    if (owner == Load32(base, kPlayerOne))
    {
      return "Jo";
    }
    if (!Guest(owner) || !Readable(base, owner, 96))
    {
      return fmt::format("{:08X} (not readable)", owner);
    }
    const uint32_t setup = Load32(base, owner + 84);
    std::string model, id;
    if (Guest(setup) && Readable(base, setup, 144))
    {
      model = Text(base, Load32(base, setup + 140), 64);
      id = Text(base, Load32(base, setup + 0x34), 31);
    }
    if (!model.empty() && model.find("props_") != std::string::npos)
    {
      return fmt::format("prop {} ({:08X})", model, owner);
    }
    if (!id.empty())
    {
      return fmt::format("character {} ({:08X})", id, owner);
    }
    if (!model.empty())
    {
      return fmt::format("object {} ({:08X})", model, owner);
    }
    return fmt::format("object {:08X} (class {:08X}, no name)", owner, Load32(base, owner));
  }

  // The listener a group plays to (its +40), else the first.
  bool Ears(const uint8_t *base, uint32_t group, float out[3])
  {
    uint32_t li = group ? Load32(base, group + 40) : 0;
    li = li < 6 ? li : 0;
    const uint32_t listener = Load32(base, kListeners + 4 * li);
    if (!Guest(listener) || !Safe(base, listener + 128, 12))
    {
      return false;
    }
    for (int j = 0; j < 3; ++j)
    {
      out[j] = LoadF(base, listener + 128 + 4 * j);
    }
    return Finite(out[0]) && Finite(out[1]) && Finite(out[2]);
  }

  // Restored names (pdz_snd_heard_cues), re-read when the setting changes.
  std::string g_cues_text;
  std::vector<std::string> g_cue_parts;
  void RefreshCueParts()
  {
    const std::string now = REXCVAR_GET(pdz_snd_heard_cues);
    if (now == g_cues_text)
    {
      return;
    }
    g_cues_text = now;
    g_cue_parts.clear();
    size_t at = 0;
    while (at <= now.size())
    {
      const size_t comma = now.find(',', at);
      std::string part = now.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
      part.erase(0, part.find_first_not_of(' '));
      while (!part.empty() && part.back() == ' ')
      {
        part.pop_back();
      }
      if (!part.empty())
      {
        g_cue_parts.push_back(part);
      }
      if (comma == std::string::npos)
      {
        break;
      }
      at = comma + 1;
    }
  }
  bool Restored(const char *name)
  {
    for (const std::string &p : g_cue_parts)
    {
      if (std::strstr(name, p.c_str()))
      {
        return true;
      }
    }
    return false;
  }

  // ---- What has been learnt this session: the distance curve and the reference footstep ----
  constexpr int kBins = 40; // distance / range in steps of 0.05, 0 .. 2
  struct Bin
  {
    double sum = 0.0;
    int n = 0;
  };
  std::array<Bin, kBins> g_curve{};
  struct Ref
  {
    float xact = -1.0f, send = -1.0f, loud = -1.0f;
    int n = 0;
  } g_ref;

  void Learn(const Meas &m)
  {
    if (m.in_group && m.f2_live && m.f2 > 1.0e-4f && m.gain3d >= 0.0f && m.grange > 1.0f && m.d >= 0.0f)
    {
      const float u = m.d / m.grange;
      const int b = int(u / 0.05f);
      if (b >= 0 && b < kBins)
      {
        Bin &bin = g_curve[size_t(b)];
        if (bin.n > 1000)
        {
          bin.sum *= 0.5, bin.n /= 2;
        }
        bin.sum += double(std::min(m.gain3d / m.f2, 2.0f));
        ++bin.n;
      }
    }
    if (m.name == kRefCue && m.xact >= 0.0f)
    {
      const float k = g_ref.n ? 0.8f : 0.0f; // (a running mean that follows the latest steps)
      g_ref.xact = g_ref.n ? k * g_ref.xact + (1.0f - k) * m.xact : m.xact;
      if (m.send >= 0.0f)
      {
        g_ref.send = g_ref.send >= 0.0f ? k * g_ref.send + (1.0f - k) * m.send : m.send;
      }
      if (m.g96 >= 0.0f)
      {
        g_ref.loud = g_ref.loud >= 0.0f ? k * g_ref.loud + (1.0f - k) * m.g96 : m.g96;
      }
      ++g_ref.n;
    }
  }
  // X3DAudio's distance gain at distance / range = u: measured this session, else Rare's straight line.
  float Curve(float u, bool &measured)
  {
    const int b = int(u / 0.05f);
    if (b >= 0 && b < kBins && g_curve[size_t(b)].n > 0)
    {
      measured = true;
      return float(g_curve[size_t(b)].sum / double(g_curve[size_t(b)].n));
    }
    measured = false;
    return std::max(0.0f, 1.0f - u);
  }

  bool Measure(PPCContext &ctx, uint8_t *base, uint32_t s, bool count_walls, Meas &m)
  {
    m.sound = s;
    const char *raw = reinterpret_cast<const char *>(base + s + 52);
    m.name.assign(raw, strnlen(raw, 255));
    m.flags = Load32(base, s + 336);
    m.state = Load32(base, s + 340);
    m.kind = Load32(base, s + 312) & 63u;
    m.owner = Load32(base, s + 12);
    m.placed = (m.flags & 0x10u) != 0;
    m.range = LoadF(base, s + 324);
    m.loud = LoadF(base, s + 332);
    m.single = base[s + 344] != 0;
    for (int j = 0; j < 3; ++j)
    {
      m.pos[j] = LoadF(base, s + 16 + 4 * j);
    }
    m.owner_text = OwnerText(base, m.owner, m.flags, Restored(m.name.c_str()));
    const uint32_t link = Load32(base, s);
    uint32_t wrapper = 0;
    if (Guest(link) && Readable(base, link, 28))
    {
      wrapper = Load32(base, link + 4);
      const uint32_t group = Load32(base, link + 12);
      if (Guest(group) && Readable(base, group, 100) && base[group + 4] != 0)
      {
        m.group = group;
        m.in_group = true;
        m.group_voice = Load32(base, group);
        m.g96 = LoadF(base, group + 96);
        const float r2 = LoadF(base, group + 84);
        m.grange = Finite(r2) && r2 > 0.0f ? std::sqrt(r2) : 0.0f;
        m.lowpass = LoadF(base, group + 24);
        m.span = LoadF(base, group + 36);
        for (int j = 0; j < 3; ++j)
        {
          m.gpos[j] = LoadF(base, group + 52 + 4 * j);
        }
      }
    }
    const float *src = m.in_group ? m.gpos : m.pos;
    if (Ears(base, m.group, m.ears) && Finite(src[0]) && Finite(src[1]) && Finite(src[2]))
    {
      const float dx = src[0] - m.ears[0], dy = src[1] - m.ears[1], dz = src[2] - m.ears[2];
      m.d = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    if (count_walls && m.placed && m.d > 150.0f && RaysReady(base))
    {
      m.walls = pdz::walls::Count(ctx, base, src, m.ears, nullptr, nullptr); // (Sat 21 Nov evening: the cut's own count)
    }
    // The mixer's side (copied under the lock; the voices are read after it).
    struct Src
    {
      uint32_t voice;
      float send;
      bool linked;
    };
    std::array<Src, 8> srcs{};
    int n_src = 0;
    {
      const Guard hold(g_lock);
      if (m.group_voice)
      {
        if (const auto it = g_voices.find(m.group_voice); it != g_voices.end())
        {
          m.gain3d = it->second.gain[0];
          m.f2 = it->second.f2;
          m.f2_live = it->second.f2 >= 0.0f && g_tick.load(std::memory_order_relaxed) - it->second.f2_tick <= 4;
        }
      }
      if (wrapper)
      {
        if (const auto it = g_routes.find(wrapper); it != g_routes.end() && m.send < 0.0f)
        {
          m.send = it->second.send; // (what Rare asked; the landed map below wins)
        }
      }
      for (const auto &[voice, rec] : g_voices)
      {
        for (int i = 0; i < kOuts; ++i)
        {
          const bool to_group = m.group_voice && rec.dest[i] == m.group_voice;
          const bool mine = wrapper && rec.cue == wrapper;
          if ((to_group || (mine && !m.in_group)) && n_src < int(srcs.size()))
          {
            srcs[size_t(n_src++)] = {voice, rec.gain[i], mine};
            break;
          }
        }
      }
    }
    // Prefer the voices this cue itself routed; else every voice sending into its group (a shared group: say so).
    bool any_linked = false;
    for (int i = 0; i < n_src; ++i)
    {
      any_linked = any_linked || srcs[size_t(i)].linked;
    }
    for (int i = 0; i < n_src; ++i)
    {
      const Src &v = srcs[size_t(i)];
      if (any_linked && !v.linked)
      {
        continue;
      }
      if (!Guest(v.voice) || !Readable(base, v.voice, 160) || Load32(base, v.voice) != kSourceVtable)
      {
        continue;
      }
      ++m.sources;
      m.sources_linked += v.linked ? 1 : 0;
      const float vol = LoadF(base, v.voice + 140);
      const float cat = CatVolume(base, base[v.voice + 144]);
      const float x = Finite(vol) ? vol * cat : -1.0f;
      if (x > m.xact) // (the loudest of the cue's voices)
      {
        m.xact = x, m.voice_vol = vol, m.voice_cat = cat;
        if (v.send >= 0.0f)
        {
          m.send = v.send;
        }
      }
    }
    if (m.in_group && m.xact >= 0.0f && m.send >= 0.0f && m.gain3d >= 0.0f)
    {
      m.heard = m.xact * m.send * m.gain3d;
    }
    Learn(m);
    return true;
  }

  std::string F(float v, const char *fmt_text = "{:.3f}")
  {
    return v < 0.0f ? std::string("?") : fmt::format(fmt::runtime(fmt_text), v);
  }

  std::string Format(const Meas &m, const char *tag)
  {
    const char *short_name = m.name.rfind("XACT_SOUNDBANK_", 0) == 0 ? m.name.c_str() + 15 : m.name.c_str();
    const BankCue *bank = BankOf(m.name.c_str());
    const float wall_cut = m.f2_live && m.g96 > 1.0e-4f ? m.f2 / m.g96 : -1.0f;
    const float dist_gain = m.f2_live && m.f2 > 1.0e-4f && m.gain3d >= 0.0f ? m.gain3d / m.f2 : -1.0f;
    const float range_cm = m.grange > 1.0f ? m.grange : m.range;
    const float line = range_cm > 1.0f && m.d >= 0.0f ? m.loud * std::max(0.0f, 1.0f - m.d / range_cm) : -1.0f;
    std::string chain;
    if (!m.placed)
    {
      chain = fmt::format("NOT PLACED (heard everywhere; no 3D group): xact {}", F(m.xact));
    }
    else if (!m.in_group)
    {
      chain = fmt::format("not in a 3D group now (just started, refused or out of its group's reach): xact {}", F(m.xact));
    }
    else
    {
      chain = fmt::format(
          "xact {} (voice volume {} x XAudio category {}; {} voice(s){}) x send {} ({}) x 3D {} [= loudness {} x wall cut {} x "
          "distance/pan {}{}] = HEARD {} ({} dB)",
          F(m.xact), F(m.voice_vol), F(m.voice_cat), m.sources,
          m.sources == 0 ? ": none found" : m.sources_linked ? "" : ", by its group only (shared group)", F(m.send),
          m.single ? "byte +344: 1.0" : "Rare sends +332", F(m.gain3d), F(m.g96), F(wall_cut), F(dist_gain),
          m.f2_live ? "" : " (3D pass not seen for this group)", F(m.heard), m.heard >= 0.0f ? fmt::format("{:.1f}", Db(m.heard)) : "?");
    }
    // The reference footstep at this distance, through this chain.
    std::string ref;
    if (m.d >= 0.0f)
    {
      bool measured = false;
      const float curve = Curve(m.d / kRefRangeCm, measured);
      const BankCue *rb = BankOf(kRefCue);
      const bool learnt = g_ref.n > 0 && g_ref.xact >= 0.0f;
      const float rx = learnt ? g_ref.xact : rb ? rb->sound_lin * rb->category_lin : 1.0f;
      const float rs = g_ref.send >= 0.0f ? g_ref.send : 1.0f;
      const float rl = g_ref.loud >= 0.0f ? g_ref.loud : 1.0f;
      const float rw = wall_cut >= 0.0f ? wall_cut : 1.0f;
      const float r = rx * rs * rl * curve * rw;
      ref = fmt::format("REFERENCE STOCK_FSTEP_CONC_WALK at {:.1f} m: {:.3f} ({:.1f} dB) [xact {:.3f} ({}) x send {:.2f} x loudness {:.2f}{} "
                        "x distance {:.3f} ({} at {:.2f} of 12 m) x wall cut {:.2f}]{}",
                        m.d / 100.0f, r, Db(r), rx, learnt ? fmt::format("measured on {} steps", g_ref.n) : "bank: no step measured yet",
                        rs, rl, g_ref.loud >= 0.0f ? "" : " (not measured)", curve, measured ? "measured curve" : "Rare's line",
                        m.d / kRefRangeCm, rw,
                        m.heard >= 0.0f && r > 1.0e-6f ? fmt::format(" | this / reference {:+.1f} dB", Db(m.heard) - Db(r)) : "");
    }
    const std::string bank_text =
        bank ? fmt::format("bank: sound {:.1f} dB, category {} {:.1f} dB, recording RMS {:.1f} / peak {:.1f} dBFS", Db(bank->sound_lin),
                           Category(uint32_t(bank->category)), Db(bank->category_lin), bank->rms_dbfs, bank->peak_dbfs)
             : std::string("bank: not in the table");
    return fmt::format(
        "pdz_snd_heard: {} {} [kind {} {}] owner {} | at ({:.0f} {:.0f} {:.0f}){}, {} m from the listener at ({:.0f} {:.0f} {:.0f}), "
        "walls {} | {} | range {} m, Rare's line loudness x (1 - d / range) = {} | low-pass {} (span {} m) | {} | {} "
        "| state {}, flags {:08X}, sound {:08X}, group {:08X}{}",
        tag, short_name, m.kind, Category(m.kind), m.owner_text, m.pos[0], m.pos[1], m.pos[2],
        m.in_group && (std::fabs(m.gpos[0] - m.pos[0]) + std::fabs(m.gpos[1] - m.pos[1]) + std::fabs(m.gpos[2] - m.pos[2]) > 100.0f)
            ? fmt::format(" (its group at ({:.0f} {:.0f} {:.0f}))", m.gpos[0], m.gpos[1], m.gpos[2])
            : std::string(),
        m.d >= 0.0f ? fmt::format("{:.1f}", m.d / 100.0f) : std::string("?"), m.ears[0], m.ears[1], m.ears[2],
        m.walls >= 0 ? fmt::format("{}", m.walls) : std::string("-"), chain,
        range_cm > 1.0f ? fmt::format("{:.1f}", range_cm / 100.0f) : std::string("none"), F(line), F(m.lowpass, "{:.2f}"),
        F(m.span / 100.0f, "{:.1f}"), bank_text, ref, m.state, m.flags, m.sound, m.group,
        m.probe.empty() ? std::string() : " | WALL PROBE: " + m.probe);
  }

  // ---- The pool ----
  // Live (not free) sounds, in pool order.
  int LiveSounds(const uint8_t *base, std::array<uint32_t, kPoolSize> &out)
  {
    const uint32_t array = Load32(base, kPool + 20);
    if (!Guest(array))
    {
      return 0;
    }
    std::array<bool, kPoolSize> free{};
    for (uint32_t f = Load32(base, kPool + 4), n = 0; Guest(f) && n <= uint32_t(kPoolSize); ++n, f = Load32(base, f))
    {
      const uint32_t i = (f - array) / kStride;
      if (f < array || i >= uint32_t(kPoolSize) || (f - array) % kStride)
      {
        break;
      }
      free[i] = true;
    }
    int n = 0;
    for (int i = 0; i < kPoolSize; ++i)
    {
      const uint32_t s = array + uint32_t(i) * kStride;
      const uint32_t state = Load32(base, s + 340);
      if (!free[size_t(i)] && base[s + 52] != 0 && state >= 2 && state <= 5)
      {
        out[size_t(n++)] = s;
      }
    }
    return n;
  }

  // Restored sounds seen starting.
  struct Track
  {
    std::string name;
    int64_t due_ms = 0; // the first line (0 = done)
    bool first = false;  // one of the first 3 starts of its cue on this level
    bool ref = false;    // the reference footstep: measured (learnt), logged only for its first 2
  };
  std::unordered_map<uint32_t, Track> g_track;
  std::unordered_map<std::string, int> g_starts; // per level
  std::string g_level;
  int64_t g_scan_ms = 0, g_periodic_ms = 0, g_level_ms = 0;
  size_t g_round = 0;
  int32_t g_now_seen = 0;
  int64_t g_who_due_ms = 0; // pdz_snd_heard_who_test: when this level's test runs

  void Listing(PPCContext &ctx, uint8_t *base, const std::string &tag)
  {
    if (!InPlay(base))
    {
      REXLOG_INFO("pdz_snd_heard: LISTING {}: not in a level: nothing plays", tag);
      return;
    }
    RefreshCueParts();
    std::array<uint32_t, kPoolSize> live{};
    const int n = LiveSounds(base, live);
    std::vector<Meas> placed;
    std::string flat;
    int flat_n = 0;
    for (int i = 0; i < n; ++i)
    {
      Meas m;
      const uint32_t s = live[size_t(i)];
      const bool place = (Load32(base, s + 336) & 0x10u) != 0;
      Measure(ctx, base, s, false, m);
      if (place)
      {
        placed.push_back(std::move(m));
      }
      else
      {
        ++flat_n;
        flat += fmt::format("{}{} [kind {}] xact {}", flat.empty() ? "" : "; ",
                            m.name.rfind("XACT_SOUNDBANK_", 0) == 0 ? m.name.substr(15) : m.name, m.kind, F(m.xact));
      }
    }
    std::sort(placed.begin(), placed.end(), [](const Meas &a, const Meas &b) {
      if ((a.heard >= 0.0f) != (b.heard >= 0.0f))
      {
        return a.heard >= 0.0f;
      }
      return a.heard != b.heard ? a.heard > b.heard : a.d < b.d;
    });
    // Walls for the sounds within their reach (Rare's ray of block-AI-sound geometry, as the wall cut counts them).
    int counted = 0;
    for (Meas &m : placed)
    {
      const float r = m.grange > 1.0f ? m.grange : m.range;
      if (counted < 32 && m.d > 150.0f && (r <= 1.0f || m.d < 1.25f * r) && RaysReady(base))
      {
        const float *src = m.in_group ? m.gpos : m.pos;
        m.walls = pdz::walls::Count(ctx, base, src, m.ears, nullptr, nullptr); // (Sat 21 Nov evening: the cut's own count)
        if (REXCVAR_GET(pdz_snd_heard_wall_probe))
        {
          m.probe = pdz::walls::Probe(ctx, base, src, m.ears);
        }
        ++counted;
      }
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    float ears[3] = {};
    const bool have_ears = Ears(base, 0, ears);
    REXLOG_INFO("pdz_snd_heard: LISTING {} on {}: listener (camera) at ({:.0f} {:.0f} {:.0f}){}, Jo at {}: {} sounds playing, {} placed "
                "(below, loudest first), {} not placed; wall cut {}; reference footsteps measured {}",
                tag, g_level.empty() ? std::string("?") : g_level, ears[0], ears[1], ears[2], have_ears ? "" : " (no listener)",
                Guest(jo) ? fmt::format("({:.0f} {:.0f} {:.0f})", LoadF(base, jo + 48), LoadF(base, jo + 52), LoadF(base, jo + 56))
                          : std::string("(none)"),
                n, placed.size(), flat_n, REXCVAR_GET(pdz_snd_wall_cut) ? "on" : "off", g_ref.n);
    int k = 0;
    for (const Meas &m : placed)
    {
      if (++k > 48)
      {
        REXLOG_INFO("pdz_snd_heard: LISTING {}: {} more placed sounds not listed", tag, int(placed.size()) - 48);
        break;
      }
      REXLOG_INFO("{}", Format(m, fmt::format("LISTING {} #{}", tag, k).c_str()));
    }
    REXLOG_INFO("pdz_snd_heard: LISTING {}: not placed (everywhere, no distance): {}", tag, flat.empty() ? std::string("none") : flat);
  }

  void WhoTest(const uint8_t *base, const std::string &level); // (below)
  void Tick(PPCContext &ctx, uint8_t *base)
  {
    const int32_t want = REXCVAR_GET(pdz_snd_heard_now);
    // Sat 21 Nov evening: the continuous lines only with pdz_snd_heard_log; with only pdz_snd_heard_f12 on, the
    // reference footsteps are still measured silently (so an F12 listing has a measured reference).
    const bool on = REXCVAR_GET(pdz_snd_heard_log);
    if (want != g_now_seen)
    {
      g_now_seen = want;
      if (want != 0)
      {
        Listing(ctx, base, fmt::format("pdz_snd_heard_now={}", want));
      }
    }
    if (!MeterOn())
    {
      return;
    }
    const int64_t now = NowMs();
    if (now - g_level_ms >= 2000)
    {
      g_level_ms = now;
      const std::string level = pdz::pace::SpLevelName(base, nullptr);
      if (level != g_level)
      {
        g_level = level;
        g_starts.clear();
        g_track.clear();
        g_who_due_ms = now + 3000;
      }
    }
    if (g_who_due_ms && now >= g_who_due_ms && InPlay(base))
    {
      g_who_due_ms = 0;
      if (REXCVAR_GET(pdz_snd_heard_who_test))
      {
        WhoTest(base, g_level.empty() ? std::string("?") : g_level);
      }
    }
    if (now - g_scan_ms >= 250)
    {
      g_scan_ms = now;
      RefreshCueParts();
      std::array<uint32_t, kPoolSize> live{};
      const int n = LiveSounds(base, live);
      std::unordered_map<uint32_t, Track> next;
      for (int i = 0; i < n; ++i)
      {
        const uint32_t s = live[size_t(i)];
        const char *raw = reinterpret_cast<const char *>(base + s + 52);
        const bool restored = Restored(raw);
        const bool ref = std::strncmp(raw, kRefCue, sizeof(kRefCue)) == 0;
        if ((!restored || !on) && !ref)
        {
          continue;
        }
        const std::string name(raw, strnlen(raw, 255));
        if (const auto it = g_track.find(s); it != g_track.end() && it->second.name == name)
        {
          next[s] = it->second; // (the same sound still playing)
          continue;
        }
        Track t;
        t.name = name;
        t.ref = ref;
        const int count = ++g_starts[name];
        t.first = count <= (ref ? 2 : std::max(1, int(REXCVAR_GET(pdz_snd_heard_first_n))));
        t.due_ms = now + (ref ? 150 : std::max(1, int(REXCVAR_GET(pdz_snd_heard_first_ms))));
        next[s] = t;
      }
      g_track.swap(next);
    }
    // First lines (and the reference's silent measures).
    for (auto &[s, t] : g_track)
    {
      if (t.due_ms && now >= t.due_ms)
      {
        t.due_ms = 0;
        Meas m;
        Measure(ctx, base, s, t.first && on, m);
        if (on && t.first && m.name == t.name)
        {
          REXLOG_INFO("{}", Format(m, t.ref ? fmt::format("REFERENCE START {}", g_starts[t.name]).c_str()
                                            : fmt::format("START {} of this cue on this level", g_starts[t.name]).c_str()));
        }
      }
    }
    // Every 2 s while a restored sound plays: each of them (at most 6 a time, in turn).
    if (on && now - g_periodic_ms >= 2000)
    {
      g_periodic_ms = now;
      std::vector<uint32_t> playing;
      for (const auto &[s, t] : g_track)
      {
        if (!t.ref)
        {
          playing.push_back(s);
        }
      }
      std::sort(playing.begin(), playing.end());
      for (size_t i = 0; i < playing.size() && i < 6; ++i)
      {
        const uint32_t s = playing[(g_round + i) % playing.size()];
        Meas m;
        Measure(ctx, base, s, true, m);
        if (m.name == g_track[s].name)
        {
          REXLOG_INFO("{}", Format(m, fmt::format("PLAYING ({} restored now)", playing.size()).c_str()));
        }
      }
      g_round += 6;
    }
  }

  // ---- Item 3: who hears a loud shot ----
  struct Loud
  {
    uint32_t id = 0;      // the shooter (a profile's owner), or the sound event
    bool event = false;
    float pos[3] = {};
    float radius = 0.0f;  // Rare's noise radius (cm)
    float at = 0.0f;      // AI clock
    bool counts = true;   // Rare's loop counts this profile (flags +0x44 & 5 == 5)
    std::vector<uint32_t> done; // listeners logged
  };
  std::vector<Loud> g_loud;
  float g_shot_clock = -1.0f;
  std::array<float, kProfiles> g_prev_radius{}, g_prev_timer{};
  std::array<float, kProfiles> g_last_event_at{};
  std::vector<std::pair<uint32_t, float>> g_prev_events; // (event, radius)
  int g_wall_budget = 0;

  // A character's setup id for the log, else his hex id. Sat 21 Nov evening (the crash of log 034, Datacore 20:12:26:
  // for an object in reach that was not a guard, c +0x54 -> +0x34 held name TEXT, "Spid" = 0x53706964, and reading
  // through it faulted: the old range test was not a mapped-memory test). Now only Rare's character classes (+12:
  // 33 NPC, 2 player, 43 corpse) are named, and every address is tested as readable first. Never faults.
  std::string Who(const uint8_t *base, uint32_t c, int *named = nullptr)
  {
    if (named)
    {
      *named = 0;
    }
    if (c == Load32(base, kPlayerOne) && c)
    {
      if (named)
      {
        *named = 1;
      }
      return "Jo";
    }
    if (!Safe(base, c, 0x58))
    {
      return fmt::format("{:08X} (not readable)", c);
    }
    const uint32_t cls = Load32(base, c + 12);
    if (!CharacterClass(cls))
    {
      return fmt::format("{:08X} (class {}, not a character)", c, cls);
    }
    const uint32_t data = Load32(base, c + 0x54);
    const uint32_t text = Safe(base, data, 0x38) ? Load32(base, data + 0x34) : 0;
    std::string s;
    if (Safe(base, text, 32))
    {
      for (uint32_t i = 0; i < 31 && base[text + i] >= 32 && base[text + i] < 127; ++i)
      {
        s += char(base[text + i]);
      }
    }
    if (s.empty())
    {
      return fmt::format("{:08X} (class {}, no name)", c, cls);
    }
    if (named)
    {
      *named = 1;
    }
    return fmt::format("{} ({:08X})", s, c);
  }

  // pdz_snd_heard_who_test: Who() on every object of Rare's two character lists (+0x710 first node, +0x714 end;
  // node +0 the character, +8 the next) and every perception profile's owner. Once per level, 3 s into play.
  void WhoTest(const uint8_t *base, const std::string &level)
  {
    constexpr uint32_t kLists[] = {0x82E454A0u, 0x82E44D80u};
    std::vector<uint32_t> seen;
    int named = 0, hex = 0;
    auto one = [&](uint32_t c, const char *where) {
      if (!c || std::find(seen.begin(), seen.end(), c) != seen.end())
      {
        return;
      }
      seen.push_back(c);
      int ok = 0;
      const std::string who = Who(base, c, &ok);
      (ok ? named : hex) += 1;
      std::string model;
      if (Safe(base, c, 0x58))
      {
        const uint32_t setup = Load32(base, c + 0x54);
        if (Safe(base, setup, 144))
        {
          const uint32_t m = Load32(base, setup + 140);
          if (Safe(base, m, 48))
          {
            for (uint32_t i = 0; i < 47 && base[m + i] >= 32 && base[m + i] < 127; ++i)
            {
              model += char(base[m + i]);
            }
          }
        }
      }
      REXLOG_INFO("pdz_snd_heard: WHO TEST {} {}: {} | vtable {:08X}, class {}{}", level, where, who,
                  Safe(base, c, 16) ? Load32(base, c) : 0u, Safe(base, c, 16) ? Load32(base, c + 12) : 0u,
                  model.empty() ? std::string() : fmt::format(", model {}", model));
    };
    for (const uint32_t list : kLists)
    {
      uint32_t node = Load32(base, list + 0x710);
      const uint32_t end = Load32(base, list + 0x714);
      for (int i = 0; node != end && i < 512 && Safe(base, node, 12); ++i)
      {
        one(Load32(base, node), list == kLists[0] ? "list A" : "list B");
        node = Load32(base, node + 8);
      }
    }
    for (uint32_t i = 0; i < kProfiles; ++i)
    {
      one(Load32(base, kProfileTable + 0x50 * i + 4), "profile");
    }
    REXLOG_INFO("pdz_snd_heard: WHO TEST {} DONE: {} objects, {} named, {} as hex, no fault", level, int(seen.size()), named, hex);
  }

  void ScanLoud(const uint8_t *base, float clock)
  {
    // Old ones go (1.5 s of AI time: every hearing loop has run by then).
    g_loud.erase(std::remove_if(g_loud.begin(), g_loud.end(),
                                [clock](const Loud &l) { return clock < l.at || clock - l.at > 1.5f; }),
                 g_loud.end());
    g_wall_budget = std::min(g_wall_budget + 2, 24); // (wall counts for these lines: about 2 a frame at most)
    for (uint32_t i = 0; i < kProfiles; ++i)
    {
      const uint32_t slot = kProfileTable + 0x50 * i;
      const float r = LoadF(base, slot + 0x30), timer = LoadF(base, slot + 0x40);
      const uint32_t owner = Load32(base, slot + 4);
      const bool fired = Finite(r) && r >= 1000.0f && Guest(owner) && Safe(base, owner, 60) &&
                         (r > g_prev_radius[i] + 1.0f || (Finite(timer) && timer < g_prev_timer[i] - 1.0e-4f));
      g_prev_radius[i] = Finite(r) ? r : 0.0f;
      g_prev_timer[i] = Finite(timer) ? timer : 0.0f;
      if (!fired || (g_last_event_at[i] > 0.0f && clock >= g_last_event_at[i] && clock - g_last_event_at[i] < 1.0f))
      {
        continue; // (a burst: one line set a second per shooter)
      }
      g_last_event_at[i] = clock;
      Loud l;
      l.id = owner;
      l.radius = r;
      l.at = clock;
      l.counts = (Load32(base, slot + 0x44) & 5u) == 5u;
      for (int j = 0; j < 3; ++j)
      {
        l.pos[j] = LoadF(base, owner + 48 + 4 * j);
      }
      std::string model; // (pdz_snd_heard_who_test: the shooter's model too)
      if (REXCVAR_GET(pdz_snd_heard_who_test) && Safe(base, owner, 0x58))
      {
        const uint32_t setup = Load32(base, owner + 0x54);
        const uint32_t m = Safe(base, setup, 144) ? Load32(base, setup + 140) : 0;
        if (Safe(base, m, 48))
        {
          for (uint32_t k = 0; k < 47 && base[m + k] >= 32 && base[m + k] < 127; ++k)
          {
            model += char(base[m + k]);
          }
        }
        model = fmt::format(" [vtable {:08X}, class {}, model {}]", Load32(base, owner), Load32(base, owner + 12), model.empty() ? "?" : model);
      }
      REXLOG_INFO("pdz_snd_heard: LOUD SHOT by {}{} at ({:.0f} {:.0f} {:.0f}): Rare's noise radius {:.0f} cm (profile {}){}; the hearing "
                  "of each character within reach follows",
                  Who(base, owner), model, l.pos[0], l.pos[1], l.pos[2], r, i,
                  l.counts ? "" : " BUT its profile flags do not have 0x5: Rare's hearing loop skips it (nobody hears it)");
      g_loud.push_back(std::move(l));
    }
    // Sound events (explosions and the like): new entries of Rare's list.
    std::vector<std::pair<uint32_t, float>> events;
    const uint32_t entries = Load32(base, kSoundEvents), first = Load32(base, kSoundEvents + 8);
    int index = Guest(entries) && Guest(first) && first >= entries ? int(int16_t(((first - entries) >> 3) & 0xFFFF)) : -1;
    for (int guard = 0; index >= 0 && guard < 128; ++guard)
    {
      const uint32_t entry = entries + 8 * uint32_t(index);
      if (!Safe(base, entry, 8))
      {
        break;
      }
      const uint32_t ev = Load32(base, entry + 4);
      index = int(int16_t(Load32(base, entry) & 0xFFFF));
      if (!Guest(ev) || !Safe(base, ev, 24))
      {
        continue;
      }
      const float r = LoadF(base, ev);
      if (!Finite(r) || !(r > 0.0f))
      {
        continue;
      }
      events.push_back({ev, r});
      const bool known = std::any_of(g_prev_events.begin(), g_prev_events.end(),
                                     [ev, r](const std::pair<uint32_t, float> &p) { return p.first == ev && std::fabs(p.second - r) < 1.0f; });
      if (known || r < 1000.0f)
      {
        continue;
      }
      Loud l;
      l.id = ev;
      l.event = true;
      l.radius = r;
      l.at = clock;
      for (int j = 0; j < 3; ++j)
      {
        l.pos[j] = LoadF(base, ev + 8 + 4 * j);
      }
      REXLOG_INFO("pdz_snd_heard: LOUD SOUND EVENT (an explosion or the like) at ({:.0f} {:.0f} {:.0f}): heard within {:.0f} cm by "
                  "Rare's rule (and his zone-hop limit, not tested here); the hearing of each character within reach follows",
                  l.pos[0], l.pos[1], l.pos[2], r);
      g_loud.push_back(std::move(l));
    }
    g_prev_events.swap(events);
  }
} // namespace

namespace pdz::r3
{
  // F12 (restored_sounds_r3_Wed18Nov.cpp's pointer, the game thread): "what can I hear here".
  void SoundHeardF12(PPCContext &ctx, uint8_t *base)
  {
    if (REXCVAR_GET(pdz_snd_heard_f12) || REXCVAR_GET(pdz_snd_heard_log))
    {
      Listing(ctx, base, "F12");
    }
  }
} // namespace pdz::r3

// Rare's hearing loop, per listener (lab_stealth.cpp's hook of sub_8218AAC8 calls this first; the game thread).
// layer: the stealth layer acts for this listener now (else Rare's own rule decides). Cheap when nothing loud
// happened: one clock read and, once a frame, 170 profile reads.
void PdzSndHeardHearing(PPCContext &ctx, uint8_t *base, uint32_t c, bool layer)
{
  if (!REXCVAR_GET(pdz_snd_heard_log) || !Guest(c) || !Safe(base, c, kProfileSlot + 4))
  {
    return;
  }
  const float clock = LoadF(base, kAiClock);
  if (!Finite(clock))
  {
    return;
  }
  if (clock != g_shot_clock)
  {
    g_shot_clock = clock;
    ScanLoud(base, clock);
  }
  if (g_loud.empty())
  {
    return;
  }
  const uint32_t jo = Load32(base, kPlayerOne);
  const int32_t pslot = int32_t(Load32(base, c + kProfileSlot));
  const float factor = pslot >= 0 && pslot < 171 ? LoadF(base, kProfileTable + 0x50 * uint32_t(pslot) + 0x38) : -1.0f;
  const float me[3] = {LoadF(base, c + 48), LoadF(base, c + 52), LoadF(base, c + 56)};
  for (Loud &l : g_loud)
  {
    if (c == jo || c == l.id || std::find(l.done.begin(), l.done.end(), c) != l.done.end())
    {
      continue;
    }
    l.done.push_back(c);
    const float dx = me[0] - l.pos[0], dy = me[1] - l.pos[1], dz = me[2] - l.pos[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    // Rare's reach: a shot's radius x his hearing; a sound event's radius as it is.
    const float reach = l.event ? l.radius : (Finite(factor) && factor > 0.0f ? l.radius * factor : -1.0f);
    if (!(d == d) || d > std::max(2.0f * std::max(reach, l.radius), std::max(reach, l.radius) + 2000.0f))
    {
      continue; // (far beyond any reach: not listed)
    }
    const bool in_rare = reach > 0.0f && d < reach && (l.event || l.counts);
    int walls = -1;
    if (d < std::max(reach, l.radius) && g_wall_budget > 0 && RaysReady(base))
    {
      --g_wall_budget;
      const float ears[3] = {me[0], me[1] + 60.0f, me[2]}; // (the stealth layer's ears: hip + 60 cm)
      walls = lab_stealth::SoundWallsBetween(ctx, base, l.pos, ears);
    }
    std::string verdict;
    if (!in_rare)
    {
      verdict = !l.event && !l.counts ? "NOT HEARD: Rare's loop skips this shooter's profile (flags)"
                : reach <= 0.0f       ? "NOT HEARD: he has no hearing profile"
                                      : fmt::format("OUT OF RANGE by Rare's rule ({:.0f} cm >= reach {:.0f} cm)", d, reach);
    }
    else if (layer)
    {
      if (walls < 0)
      {
        verdict = "in Rare's range; the stealth layer decides by walls (not counted here: budget)";
      }
      else if (REXCVAR_GET(pdz_stealth_wall_muffle))
      {
        const float within = walls > 0 ? reach / float(1 << std::min(walls, 4)) : reach;
        verdict = d < within ? fmt::format("HEARD by the stealth layer's rule ({} wall(s): heard within {:.0f} cm{})", walls, within,
                                           l.event ? "; explosions may use the gentler r3 rule" : "")
                             : fmt::format("BLOCKED by {} wall(s) (the stealth layer's rule: heard within {:.0f} cm only{})", walls, within,
                                           l.event ? "; explosions may use the gentler r3 rule" : "");
      }
      else
      {
        verdict = walls > 0 ? fmt::format("BLOCKED by {} wall(s) (the stealth layer's old rule: any wall blocks)", walls)
                            : std::string("HEARD (no wall between)");
      }
      verdict += " (a guard already fighting hears as Rare has it)";
    }
    else
    {
      verdict = fmt::format("HEARD by Rare's own rule (distance only, no wall test; the stealth layer does not act for him){}",
                            walls >= 0 ? fmt::format("; {} wall(s) on the line", walls) : std::string());
    }
    std::string model; // (pdz_snd_heard_who_test: the listener's class and model too)
    if (REXCVAR_GET(pdz_snd_heard_who_test))
    {
      const uint32_t setup = Load32(base, c + 0x54);
      const uint32_t m = Safe(base, setup, 144) ? Load32(base, setup + 140) : 0;
      if (Safe(base, m, 48))
      {
        for (uint32_t k = 0; k < 47 && base[m + k] >= 32 && base[m + k] < 127; ++k)
        {
          model += char(base[m + k]);
        }
      }
      model = fmt::format(" [vtable {:08X}, class {}, model {}]", Load32(base, c), Load32(base, c + 12), model.empty() ? "?" : model);
    }
    REXLOG_INFO("pdz_snd_heard: SHOT HEARING: {}{} at ({:.0f} {:.0f} {:.0f}), {:.1f} m from the {} {}: Rare's reach {} ({}): {}", Who(base, c), model,
                me[0], me[1], me[2], d / 100.0f, l.event ? "sound event at" : "shot of",
                l.event ? fmt::format("({:.0f} {:.0f} {:.0f})", l.pos[0], l.pos[1], l.pos[2]) : Who(base, l.id),
                reach > 0.0f ? fmt::format("{:.0f} cm", reach) : std::string("?"),
                l.event ? fmt::format("radius {:.0f} cm", l.radius)
                        : fmt::format("noise radius {:.0f} cm x his hearing {}", l.radius, Finite(factor) ? fmt::format("{:.2f}", factor) : "?"),
                verdict);
  }
}

// ---- The hooks ----

// XAudio SetOutputVoices (voice r3; r4 -> {u8 count; -> entries {dest voice (0 = the master), channel map}}).
REX_HOOK_RAW(sub_826ACBB0)
{
  const uint32_t voice = ctx.r3.u32, outs = ctx.r4.u32;
  if (MeterOn() && Guest(voice))
  {
    VoiceRec rec;
    if (Guest(outs) && pdz::guestmem::Ok(base, outs, 8)) // (Sun 22 Nov night (audit): only where mapped)
    {
      const uint32_t count = base[outs], entries = Load32(base, outs + 4);
      for (uint32_t i = 0; i < count && i < uint32_t(kOuts) && Guest(entries) && pdz::guestmem::Ok(base, entries + 8 * i, 8); ++i)
      {
        rec.dest[i] = Load32(base, entries + 8 * i);
        rec.gain[i] = MapGain(base, Load32(base, entries + 8 * i + 4));
      }
    }
    const Guard hold(g_lock);
    if (g_voices.size() > 4096)
    {
      g_voices.clear(); // (voices freed past the destroy wrapper: start again)
    }
    VoiceRec &r = g_voices[voice];
    const uint32_t cue = t_cue ? t_cue : r.cue;
    const float f2 = r.f2;
    const uint32_t f2_tick = r.f2_tick;
    r = rec;
    r.cue = cue, r.f2 = f2, r.f2_tick = f2_tick;
  }
  __imp__sub_826ACBB0(ctx, base);
}

// XAudio SetOutputMaps (voice r3; r4 -> {u8 count; -> entries {u8 output index, channel map}}). From the 3D pass
// (return 8275EA8C) f27 holds the loudness x wall cut sub_8275E708 multiplied the coefficients by.
REX_HOOK_RAW(sub_826ACC08)
{
  const uint32_t voice = ctx.r3.u32, maps = ctx.r4.u32;
  if (MeterOn() && Guest(voice) && Guest(maps) && pdz::guestmem::Ok(base, maps, 8)) // (Sun 22 Nov night (audit): Ok)
  {
    const uint32_t count = base[maps], entries = Load32(base, maps + 4);
    float gain[kOuts] = {-2.0f, -2.0f, -2.0f, -2.0f};
    for (uint32_t i = 0; i < count && i < 8 && Guest(entries) && pdz::guestmem::Ok(base, entries + 8 * i, 8); ++i)
    {
      const uint32_t index = base[entries + 8 * i];
      if (index < uint32_t(kOuts))
      {
        gain[index] = MapGain(base, Load32(base, entries + 8 * i + 4));
      }
    }
    const bool from3d = uint32_t(ctx.lr) == kFrom3D;
    const float f27 = float(ctx.f27.f64);
    const Guard hold(g_lock);
    if (g_voices.size() <= 4096)
    {
      VoiceRec &r = g_voices[voice];
      for (int i = 0; i < kOuts; ++i)
      {
        if (gain[i] > -1.5f)
        {
          r.gain[i] = gain[i];
        }
      }
      if (from3d)
      {
        r.f2 = Finite(f27) ? f27 : -1.0f;
        r.f2_tick = g_tick.load(std::memory_order_relaxed);
      }
    }
  }
  __imp__sub_826ACC08(ctx, base);
}

// XAudio voice destroy (voice r3).
REX_HOOK_RAW(sub_826AC9C0)
{
  const uint32_t voice = ctx.r3.u32;
  {
    const Guard hold(g_lock);
    g_voices.erase(voice);
  }
  __imp__sub_826AC9C0(ctx, base);
}

// Rare routes a cue (r3 = its wrapper: +0 the XACT cue) to the voice at [r4] with a map at volume f1 (the group join
// sub_821FD7D0: the group's submix at sound +332, or 1.0 for +344 sounds). XACT's own SetOutputVoices calls for the
// cue's source voices run inside, on this thread: they are linked to the wrapper.
REX_HOOK_RAW(sub_8275E420)
{
  const uint32_t wrapper = ctx.r3.u32, to = ctx.r4.u32;
  const float send = float(ctx.f1.f64);
  if (MeterOn() && Guest(wrapper))
  {
    {
      const Guard hold(g_lock);
      if (g_routes.size() > 1024)
      {
        g_routes.clear();
      }
      g_routes[wrapper] = {Guest(to) ? Load32(base, to) : 0, Finite(send) ? send : -1.0f};
    }
    const uint32_t before = t_cue;
    t_cue = wrapper;
    __imp__sub_8275E420(ctx, base);
    t_cue = before;
    return;
  }
  __imp__sub_8275E420(ctx, base);
}

// Rare's 3D pass (once an audio update, in play, the game thread): the update count, then the log's own work.
REX_HOOK_RAW(sub_821FCFE0)
{
  __imp__sub_821FCFE0(ctx, base);
  g_tick.fetch_add(1, std::memory_order_relaxed);
  if (MeterOn() || REXCVAR_GET(pdz_snd_heard_now) != g_now_seen)
  {
    Tick(ctx, base);
  }
}
