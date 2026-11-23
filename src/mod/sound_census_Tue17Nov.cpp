// pdz_sound_census (Tue 17 Nov 2026; off by default, no settings row). TU3 addresses.
//
// A list of every sound that really starts, for comparing with the sound audit's "never played" tables. Two tables,
// because the cue and the recording are known at two different points:
//
// 1. Recordings (waves), in the XACT engine itself. XACT makes the voice for a recording in exactly two routines, the
//    start of an in-memory wave (sub_82C2C770) and the start of a streamed wave (sub_82C2D9E8): they are the only
//    callers of the engine's source voice maker (sub_82C1AD38). Both take the wave instance in r3, with its wave bank
//    object at +28 and the wave's index inside that bank at +44. Whatever asked for the sound (a name lookup, an index
//    play, an animation event, a prop slot, a level script, music, an ambience layer, the mod's own sounds), a
//    recording cannot be heard without one of these two calls. A start is counted when the routine reports success.
//    The engine's one other voice maker (sub_82C1ACD0, not a wave start as far as read) is only counted, so a route
//    past the two starts would show up as a number in the file's header.
// 2. Cues, at DarkAudioSound::Prime (sub_82205C68, hooked in cue_fixes.cpp): the game's sound manager's one way to a
//    cue (the only caller of the sound bank's cue maker sub_8275D5A8). The cue is the packed value at sound + 308
//    (bank slot, cue index, category). A cue is counted when Prime reports it started.
// A wave started while a cue is being primed or played on the same thread (Prime, or the cue's Play sub_8275DD58) is
// linked to that cue in the file; a wave started elsewhere (the engine's own work) has no link.
//
// Cost: off, each hook is one switch read. On, a start is two or three loads, one lock and one table look-up; no
// names are read, nothing is logged and the system is asked nothing. The first time a cue is seen in a session its
// name (already in the sound object, sound + 52) is copied once, and the address that asked is read from the stack.
//
// File: logs\sound_census_<date>_<time>.tsv next to the exe, one row per sound per level, rewritten in place by a
// small background thread when the level changes, every 60 s and at shutdown. One log line per level left:
//   pdz_sound_census: <level>: N sounds played, file <path>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DEFINE_BOOL(pdz_sound_census, false, "Debug",
                    "Keep a list of every sound that really starts playing (cue and recording, per level, with a "
                    "count) and write it to logs\\sound_census_<date>_<time>.tsv at each level change, every 60 s "
                    "and on quit. For comparing with the sound audit. Off: nothing is recorded")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::pace
{
  std::string SpLevelName(const uint8_t *base, std::string *raw); // render/game_pace.cpp
}

REX_EXTERN(__imp__sub_82C2C770); // XACT: start an in-memory wave (r3 = wave instance)
REX_EXTERN(__imp__sub_82C2D9E8); // XACT: start a streamed wave (r3 = wave instance)
REX_EXTERN(__imp__sub_82C1ACD0); // XACT: the other voice maker
REX_EXTERN(__imp__sub_8275DD58); // the cue's Play (r3 = the game's cue wrapper)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8; // -> the session block (+25 the mission); +32 -> the audio manager

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xFFFF0000u; }

  struct CueRow
  {
    uint32_t count = 0;
    uint32_t caller = 0;
    char name[80] = {};
  };
  struct WaveRow
  {
    uint32_t count = 0;
    uint32_t cue = 0; // the packed cue (less its category) it was first started under + 1; 0 = none
    bool streamed = false;
  };
  struct Level
  {
    uint64_t key = 0;
    std::string label;
    std::unordered_map<uint32_t, CueRow> cues;  // packed cue >> 6 (bank slot << 12 | cue index)
    std::unordered_map<uint64_t, WaveRow> waves; // wave bank object << 16 | wave index
  };

  std::mutex g_mutex; // everything below
  std::vector<Level> g_levels;
  int g_current = -1;
  std::vector<int> g_left;                        // levels left since the last write (for the log line)
  std::unordered_map<uint32_t, CueRow> g_names;   // each cue's name, copied once per session
  std::atomic<uint64_t> g_other_voices{0};
  std::atomic<bool> g_dirty{false};
  std::atomic<bool> g_started{false};
  std::atomic<bool> g_stop{false};
  std::condition_variable g_wake;
  std::thread g_thread;
  std::filesystem::path g_path;
  const uint8_t *g_base = nullptr;

  thread_local uint32_t t_cue = 0; // the cue being primed or played on this thread (packed >> 6, + 1)

  std::string LevelLabel(const uint8_t *base, uint32_t session)
  {
    std::string raw;
    const std::string name = pdz::pace::SpLevelName(base, &raw);
    if (!name.empty())
    {
      return name;
    }
    if (!raw.empty())
    {
      return raw;
    }
    return Guest(session) ? fmt::format("mission {}", int(base[session + 25])) : std::string("front end");
  }

  // The level's table (g_mutex held). A new level only when the session or its mission changes.
  Level &Current(const uint8_t *base)
  {
    const uint32_t session = Load32(base, kGame);
    const uint64_t key = (uint64_t(session) << 8) | (Guest(session) ? base[session + 25] : 0xFFu);
    if (g_current >= 0 && g_levels[size_t(g_current)].key == key)
    {
      return g_levels[size_t(g_current)];
    }
    if (g_current >= 0)
    {
      g_left.push_back(g_current);
    }
    Level level;
    level.key = key;
    level.label = LevelLabel(base, session);
    // The same level played again in this session keeps one table.
    for (size_t i = 0; i < g_levels.size(); ++i)
    {
      if (g_levels[i].label == level.label)
      {
        g_levels[i].key = key;
        g_current = int(i);
        return g_levels[i];
      }
    }
    g_levels.push_back(std::move(level));
    g_current = int(g_levels.size()) - 1;
    return g_levels.back();
  }

  bool Readable(const uint8_t *base, uint32_t address, uint32_t size)
  {
    // Sat 21 Nov (pdz_fix_readable_cache): the cached check (guest_readable_Sat21Nov.cpp); below = off.
    if (pdz::guestmem::CacheOn())
    {
      return pdz::guestmem::Readable(const_cast<uint8_t *>(base), address, size);
    }
    if (!Guest(address) || uint64_t(address) + size > 0xFFFF0000ull)
    {
      return false;
    }
    size_t length = size;
    rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
    return rex::memory::QueryProtect(const_cast<uint8_t *>(base) + address, length, access) &&
           (uint32_t(access) & uint32_t(rex::memory::PageAccess::kReadOnly)) != 0;
  }

  // The game's bank slot whose entry points at this wave bank object, or -1 (file writing only: asks the system).
  int WaveBankSlot(const uint8_t *base, uint32_t wave_bank)
  {
    const uint32_t manager = Readable(base, kGame + 32, 4) ? Load32(base, kGame + 32) : 0;
    if (!Readable(base, manager, 4))
    {
      return -1;
    }
    for (uint32_t slot = 0; slot < 64; ++slot)
    {
      const uint32_t entry = manager + (slot + 5) * 12;
      if (!Readable(base, entry, 12))
      {
        break;
      }
      for (uint32_t k = 0; k < 12; k += 4)
      {
        const uint32_t wrapper = Load32(base, entry + k);
        if (wrapper == wave_bank || (Readable(base, wrapper, 4) && Load32(base, wrapper) == wave_bank))
        {
          return int(slot);
        }
      }
    }
    return -1;
  }

  // Writes the whole file (to a .tmp, then renamed over the old one) and the log line of each level left.
  void WriteFile()
  {
    std::string text;
    std::vector<std::string> lines;
    // A copy of the tables under the lock; the text (and the questions to the system about the wave banks) after it,
    // so a sound starting meanwhile never waits for the file.
    std::vector<Level> levels;
    std::unordered_map<uint32_t, CueRow> names;
    std::vector<int> left_now;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_levels.empty() || !g_base)
      {
        return;
      }
      levels = g_levels;
      names = g_names;
      left_now.swap(g_left);
    }
    {
      const std::vector<Level> &g_levels = levels;
      const std::unordered_map<uint32_t, CueRow> &g_names = names;
      const std::vector<int> &g_left = left_now;
      std::map<uint32_t, int> slots; // wave bank object -> the game's bank slot
      text = fmt::format("# pdz_sound_census 1 | other voice makers called: {}\n"
                         "kind\tlevel\tbank_slot\tcue_index\tcue_name\twave_bank\twave_bank_slot\twave_index\t"
                         "streamed\tcount\tfirst_caller\n",
                         g_other_voices.load(std::memory_order_relaxed));
      for (const Level &level : g_levels)
      {
        for (const auto &[key, row] : level.cues)
        {
          const auto name = g_names.find(key);
          text += fmt::format("cue\t{}\t{}\t{}\t{}\t\t\t\t\t{}\t{:08X}\n", level.label, key >> 12, key & 0xFFFu,
                              name != g_names.end() ? name->second.name : "", row.count, row.caller);
        }
        for (const auto &[key, row] : level.waves)
        {
          const uint32_t bank = uint32_t(key >> 16);
          auto slot = slots.find(bank);
          if (slot == slots.end())
          {
            slot = slots.emplace(bank, WaveBankSlot(g_base, bank)).first;
          }
          const auto name = row.cue ? g_names.find(row.cue - 1) : g_names.end();
          text += fmt::format("wave\t{}\t{}\t{}\t{}\t{:08X}\t{}\t{}\t{}\t{}\t\n", level.label,
                              row.cue ? fmt::format("{}", (row.cue - 1) >> 12) : std::string(),
                              row.cue ? fmt::format("{}", (row.cue - 1) & 0xFFFu) : std::string(),
                              name != g_names.end() ? name->second.name : "", bank, slot->second,
                              uint32_t(key & 0xFFFFu), row.streamed ? 1 : 0, row.count);
        }
      }
      for (const int left : g_left)
      {
        const Level &level = g_levels[size_t(left)];
        lines.push_back(fmt::format("pdz_sound_census: {}: {} sounds played, file {}", level.label,
                                    level.cues.size() + level.waves.size(), g_path.string()));
      }
    }
    std::error_code ec;
    std::filesystem::create_directories(g_path.parent_path(), ec);
    std::filesystem::path tmp = g_path;
    tmp += L".tmp";
    if (FILE *file = _wfopen(tmp.c_str(), L"wb"))
    {
      fwrite(text.data(), 1, text.size(), file);
      fclose(file);
      std::filesystem::rename(tmp, g_path, ec);
    }
    for (const std::string &line : lines)
    {
      REXLOG_INFO("{}", line);
    }
  }

  void Worker()
  {
    auto last = std::chrono::steady_clock::now();
    std::mutex wait;
    while (!g_stop.load(std::memory_order_acquire))
    {
      {
        std::unique_lock<std::mutex> lock(wait);
        g_wake.wait_for(lock, std::chrono::seconds(1));
      }
      bool left;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        left = !g_left.empty();
      }
      const auto now = std::chrono::steady_clock::now();
      if (left || (g_dirty.load(std::memory_order_relaxed) && now - last >= std::chrono::seconds(60)))
      {
        g_dirty.store(false, std::memory_order_relaxed);
        last = now;
        WriteFile();
      }
    }
  }

  // The first recorded sound of the session: the file's name and the writer thread.
  void Start(const uint8_t *base)
  {
    if (g_started.exchange(true))
    {
      return;
    }
    g_base = base;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[40];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
    g_path = rex::filesystem::GetExecutableFolder() / "logs" / fmt::format("sound_census_{}.tsv", stamp);
    g_thread = std::thread(Worker);
  }

  void OnWave(const uint8_t *base, uint32_t wave_bank, uint32_t index, bool streamed)
  {
    Start(base);
    std::lock_guard<std::mutex> lock(g_mutex);
    WaveRow &row = Current(base).waves[(uint64_t(wave_bank) << 16) | index];
    if (row.count++ == 0)
    {
      row.cue = t_cue;
      row.streamed = streamed;
    }
    g_dirty.store(true, std::memory_order_relaxed);
  }
} // namespace

namespace sound_census
{
  // From the Prime hook (cue_fixes.cpp), before Rare's routine: the cue this thread is priming.
  uint32_t PrimeBegin(const uint8_t *base, uint32_t sound)
  {
    if (!REXCVAR_GET(pdz_sound_census) || !Guest(sound))
    {
      return 0;
    }
    const uint32_t before = t_cue;
    const uint32_t cue = Load32(base, sound + 308);
    t_cue = cue == 0xFFFFFFFFu ? 0 : (cue >> 6) + 1;
    return before + 1;
  }

  // After Rare's routine: `started` is its result (0 = the sound did not start); `stack` the hook's r1 at entry.
  void PrimeEnd(const uint8_t *base, uint32_t sound, uint32_t started, uint32_t stack, uint32_t token)
  {
    if (!token)
    {
      return;
    }
    const uint32_t key = t_cue ? t_cue - 1 : 0;
    const bool have = t_cue != 0;
    t_cue = token - 1;
    if (!have || !started)
    {
      return;
    }
    Start(base);
    std::lock_guard<std::mutex> lock(g_mutex);
    CueRow &row = Current(base).cues[key];
    if (row.count++ == 0)
    {
      CueRow &name = g_names[key];
      if (!name.name[0])
      {
        // Once per cue per session: its name from the sound object, and who asked (the return address of the
        // routine that called the play routine, kept in that routine's frame).
        const char *text = reinterpret_cast<const char *>(base + sound + 52);
        size_t length = strnlen(text, 255);
        if (length > 15 && std::strncmp(text, "XACT_SOUNDBANK_", 15) == 0)
        {
          text += 15;
          length -= 15;
        }
        length = length < sizeof(name.name) - 1 ? length : sizeof(name.name) - 1;
        std::memcpy(name.name, text, length);
        for (size_t i = 0; i < length; ++i)
        {
          if (uint8_t(name.name[i]) < 0x20)
          {
            name.name[i] = '?';
          }
        }
      }
      const uint32_t frame = Guest(stack) ? Load32(base, stack) : 0;
      row.caller = Guest(frame) ? Load32(base, frame - 8) : 0;
    }
    g_dirty.store(true, std::memory_order_relaxed);
  }

  // From the app's shutdown: the last write.
  void Shutdown()
  {
    static std::atomic<bool> done{false}; // called as the window closes and again at shutdown: once
    if (!g_started.load() || done.exchange(true))
    {
      return;
    }
    g_stop.store(true, std::memory_order_release);
    g_wake.notify_all();
    if (g_thread.joinable())
    {
      g_thread.join();
    }
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_current >= 0)
      {
        g_left.push_back(g_current);
      }
    }
    WriteFile();
  }
} // namespace sound_census

REX_HOOK_RAW(sub_82C2C770)
{
  if (!REXCVAR_GET(pdz_sound_census))
  {
    __imp__sub_82C2C770(ctx, base);
    return;
  }
  const uint32_t instance = ctx.r3.u32;
  const uint32_t bank = Guest(instance) ? Load32(base, instance + 28) : 0;
  const uint32_t index = Guest(instance) ? (uint32_t(base[instance + 44]) << 8) | base[instance + 45] : 0;
  __imp__sub_82C2C770(ctx, base);
  if (bank && ctx.r3.s32 >= 0)
  {
    OnWave(base, bank, index, false);
  }
}

REX_HOOK_RAW(sub_82C2D9E8)
{
  if (!REXCVAR_GET(pdz_sound_census))
  {
    __imp__sub_82C2D9E8(ctx, base);
    return;
  }
  const uint32_t instance = ctx.r3.u32;
  const uint32_t bank = Guest(instance) ? Load32(base, instance + 28) : 0;
  const uint32_t index = Guest(instance) ? (uint32_t(base[instance + 44]) << 8) | base[instance + 45] : 0;
  __imp__sub_82C2D9E8(ctx, base);
  if (bank && ctx.r3.s32 >= 0)
  {
    OnWave(base, bank, index, true);
  }
}

REX_HOOK_RAW(sub_82C1ACD0)
{
  if (REXCVAR_GET(pdz_sound_census))
  {
    g_other_voices.fetch_add(1, std::memory_order_relaxed);
  }
  __imp__sub_82C1ACD0(ctx, base);
}

// The cue's Play: wrapper + 12 -> the sound manager's handle, whose first word is the sound (set by Prime).
REX_HOOK_RAW(sub_8275DD58)
{
  if (!REXCVAR_GET(pdz_sound_census))
  {
    __imp__sub_8275DD58(ctx, base);
    return;
  }
  const uint32_t before = t_cue;
  const uint32_t wrapper = ctx.r3.u32;
  const uint32_t handle = Guest(wrapper) ? Load32(base, wrapper + 12) : 0;
  const uint32_t sound = Guest(handle) ? Load32(base, handle) : 0;
  if (Guest(sound))
  {
    const uint32_t cue = Load32(base, sound + 308);
    t_cue = cue == 0xFFFFFFFFu ? 0 : (cue >> 6) + 1;
  }
  __imp__sub_8275DD58(ctx, base);
  t_cue = before;
}
