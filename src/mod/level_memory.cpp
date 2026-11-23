// Level memory (Fri 13 Nov 2026): Rare's heaps, how full they get, and a larger budget for the level heaps.
//
// How Rare lays out memory (TU3, read in the code):
// - One table of 23 heaps at 0x82D28458, 0x30 bytes each: +4 name, +8 kind, +0x10 size in bytes, +0x14 the most
//   blocks the heap can track, +0x18 the heap object (filled when the heap is made), +0x20 flags for a physical
//   allocation, +0x28 the block's address, +0x2C its size. The sizes are plain data in the game image.
// - All heaps are made ONCE, at the first allocation of the session (8239D5A8, three passes over the table by kind;
//   8239DB88(index) makes one). Each is ONE block asked from the system: kind 2 = physical memory (XPhysicalAlloc;
//   the GPU heaps), kind 1 = the process heap, kind 6 = the heap manager's own virtual block. Nothing of Rare's sits
//   "behind" a heap: a heap can't grow once made, but a larger size written into the table before the heaps are made
//   gives a larger block, at whatever address the system returns.
// - A level never re-makes a heap: the level's heaps (the names starting "Level") are emptied when a level is left.
// - Allocation: 8239D740(bytes, heap index) -> 8273CF98(heap object, bytes). The heap object counts for itself:
//   +0x30 bytes in use, +0x34 bytes free, +0x3C blocks in use, +0x40 the most blocks ever, +0x48 the largest request,
//   +0x60 requests so far. A request the heap can't give returns 0 (and calls the heap's +0x18 handler if it has one).
//
// pdz_level_memory_log (Debug, off): 'pdz_level_memory:' lines: every heap as it is made, the heaps at the points of
// a level's life (load job start / end, level ready, unload), the fullest each heap got in between (every 15 s when
// it changed), and a per-heap fingerprint of the requests of a level's load (count, bytes, an order-free hash) so two
// runs can be compared.
// Always on, whatever the switches: a refused request and Rare's disc-error screen are logged with the guest call
// stack (these are Rare's "out of memory" paths; a healthy run has none).
// pdz_level_memory_scale / pdz_level_memory_sizes: the larger budget (see the cvars).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(pdz_level_memory_log, false, "Debug",
                    "Level memory research (logging only): 'pdz_level_memory:' lines with Rare's heaps as they are "
                    "made, how full each is at a level's load / start / unload, the fullest each got, and a "
                    "fingerprint of the requests of each level load");
REXCVAR_DEFINE_STRING(pdz_level_memory_trace, "", "Debug",
                      "Level memory research (with pdz_level_memory_log): 'HeapName:bytes' logs every request of "
                      "exactly that many bytes from that heap with its guest call stack (the first 400)");
REXCVAR_DEFINE_DOUBLE(pdz_level_memory_scale, 1.25, "Gameplay",
                      "A larger memory budget for a level: Rare's level heaps (LevelCPU, LevelDynamicCPU, LevelGPUWC, "
                      "LevelGPURW, LevelCPU2, LevelDynamicGPUWC) are made this many times their size (1.0 = Rare's; "
                      "at most 2.0; default 1.25 on this branch, for Counter-Operative's hands packages). A raise that "
                      "would leave the console under 64 MB of free physical memory is cut down to fit. Nothing else "
                      "changes: the same heaps, the same order, only more free room. Read once, when the game starts");
REXCVAR_DEFINE_STRING(pdz_level_memory_sizes, "", "Gameplay",
                      "Explicit heap sizes in MB, by Rare's heap name: 'LevelGPUWC:260,LevelCPU:80' (comma separated). "
                      "A heap named here ignores pdz_level_memory_scale; a size below Rare's is refused. Read once, "
                      "when the game starts");

REX_EXTERN(__imp__sub_8273CF98); // the heap object's allocate: (heap object, bytes) -> address or 0
REX_EXTERN(__imp__sub_8239DB88); // make heap (index) from its table entry
REX_EXTERN(__imp__sub_82225470); // Rare's disc-error screen (never returns)

namespace
{
  constexpr uint32_t kTable = 0x82D28458, kEntry = 0x30, kHeaps = 23;
  constexpr uint32_t kName = 4, kKind = 8, kSize = 0x10, kBlocks = 0x14, kObject = 0x18, kBase = 0x28, kBlockSize = 0x2C;
  constexpr uint32_t kUsed = 0x30, kFree = 0x34, kLive = 0x3C, kLivePeak = 0x40, kLargest = 0x48, kRequests = 0x60;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xFFFF0000u; }
  std::string HeapName(const uint8_t *base, uint32_t index)
  {
    std::string out;
    const uint32_t p = index < kHeaps ? Load32(base, kTable + index * kEntry + kName) : 0;
    for (uint32_t i = 0; Ptr(p) && i < 31 && base[p + i]; ++i)
    {
      out += char(base[p + i]);
    }
    return out.empty() ? fmt::format("heap {}", index) : out;
  }
  double Mb(uint64_t bytes) { return double(bytes) / 1048576.0; }

  // What this file keeps per heap (host side; the game's own counters are read where they are).
  struct Track
  {
    std::atomic<uint32_t> object{0};   // the heap object, once made
    std::atomic<uint32_t> low_free{0xFFFFFFFFu}; // the least free bytes seen since the last reset
    std::atomic<uint32_t> high_live{0}; // the most blocks in use since the last reset
    std::atomic<uint32_t> failed{0};    // refused requests this session
    // fingerprint of the requests since the last reset
    std::atomic<uint64_t> count{0}, bytes{0}, hash{0};
    uint32_t rare_size = 0, rare_blocks = 0; // the table's own values, before any scaling
    uint32_t printed_low = 0xFFFFFFFFu;
  };
  Track g_track[kHeaps];
  std::atomic<uint32_t> g_made{0};       // heaps made so far
  std::atomic<uint32_t> g_failed_total{0}, g_disc_errors{0};
  std::atomic<uint32_t> g_tick{0};
  std::atomic<int64_t> g_next_print_ms{0};
  std::mutex g_print_mutex;

  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  int IndexOf(uint32_t object)
  {
    for (uint32_t i = 0; i < kHeaps; ++i)
    {
      if (g_track[i].object.load(std::memory_order_relaxed) == object)
      {
        return int(i);
      }
    }
    return -1;
  }
  uint64_t Mix(uint64_t x)
  {
    x ^= x >> 33, x *= 0xFF51AFD7ED558CCDull, x ^= x >> 33, x *= 0xC4CEB9FE1A85EC53ull, x ^= x >> 33;
    return x;
  }
  // The guest call stack: Rare's functions save lr at -8 of the caller's frame.
  std::string GuestStack(uint32_t lr_now, uint32_t sp, const uint8_t *base, int frames = 10)
  {
    std::string out = fmt::format("{:08X}", lr_now);
    for (int i = 0; i < frames && Ptr(sp); ++i)
    {
      const uint32_t next = Load32(base, sp);
      if (!Ptr(next) || next <= sp || next - sp > 0x100000u)
      {
        break;
      }
      const uint32_t lr = Load32(base, next - 8);
      if (lr >= 0x82000000u && lr < 0x83000000u)
      {
        out += fmt::format(" < {:08X}", lr);
      }
      sp = next;
    }
    return out;
  }
  std::string HeapLine(const uint8_t *base, uint32_t i)
  {
    const uint32_t e = kTable + i * kEntry, obj = Load32(base, e + kObject);
    if (!Ptr(obj))
    {
      return fmt::format("{} (not made)", HeapName(base, i));
    }
    const uint32_t used = Load32(base, obj + kUsed), free_now = Load32(base, obj + kFree);
    const uint32_t low = g_track[i].low_free.load(std::memory_order_relaxed);
    return fmt::format("{} {:.2f} of {:.2f} MB used, {:.2f} free (least free {:.2f}), blocks {} of {} (most {} / ever {}), "
                       "largest request {:.2f} MB, requests {}, refused {}",
                       HeapName(base, i), Mb(used), Mb(Load32(base, e + kSize)), Mb(free_now),
                       low == 0xFFFFFFFFu ? Mb(free_now) : Mb(low), Load32(base, obj + kLive), Load32(base, e + kBlocks),
                       g_track[i].high_live.load(std::memory_order_relaxed), Load32(base, obj + kLivePeak),
                       Mb(Load32(base, obj + kLargest)), Load32(base, obj + kRequests),
                       g_track[i].failed.load(std::memory_order_relaxed));
  }
  // The console's memory outside Rare's heaps (the SDK's guest memory): what is still free of the 512 MB of physical
  // memory (the GPU heaps and every other physical allocation share it) and of the two virtual ranges.
  std::string ConsoleMemory()
  {
    auto *memory = REX_KERNEL_MEMORY();
    if (!memory)
    {
      return "console memory: unknown";
    }
    auto *physical = memory->GetPhysicalHeap();
    auto *v4k = memory->LookupHeapByType(false, 4096);
    auto *v64k = memory->LookupHeapByType(false, 64 * 1024);
    return fmt::format("console memory free: physical {:.1f} of {:.1f} MB; virtual 4K-page range {:.1f} of {:.1f} MB, "
                       "64K-page range {:.1f} of {:.1f} MB",
                       physical ? Mb(uint64_t(physical->unreserved_page_count()) * physical->page_size()) : 0.0,
                       physical ? Mb(uint64_t(physical->total_page_count()) * physical->page_size()) : 0.0,
                       v4k ? Mb(uint64_t(v4k->unreserved_page_count()) * v4k->page_size()) : 0.0,
                       v4k ? Mb(uint64_t(v4k->total_page_count()) * v4k->page_size()) : 0.0,
                       v64k ? Mb(uint64_t(v64k->unreserved_page_count()) * v64k->page_size()) : 0.0,
                       v64k ? Mb(uint64_t(v64k->total_page_count()) * v64k->page_size()) : 0.0);
  }
  void PrintAll(const uint8_t *base, const std::string &why, bool fingerprint)
  {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    REXLOG_INFO("pdz_level_memory: ---- {} (refused requests this session {}, disc-error screens {}); {}", why,
                g_failed_total.load(), g_disc_errors.load(), ConsoleMemory());
    for (uint32_t i = 0; i < kHeaps; ++i)
    {
      if (!Ptr(Load32(base, kTable + i * kEntry + kObject)))
      {
        continue;
      }
      REXLOG_INFO("pdz_level_memory: {:2} {}", i, HeapLine(base, i));
      if (fingerprint)
      {
        REXLOG_INFO("pdz_level_memory: {:2} {} fingerprint since the last mark: {} requests, {} bytes, hash {:016X}", i,
                    HeapName(base, i), g_track[i].count.load(), g_track[i].bytes.load(), g_track[i].hash.load());
      }
      g_track[i].printed_low = g_track[i].low_free.load(std::memory_order_relaxed);
    }
  }
  void ResetMarks()
  {
    for (Track &t : g_track)
    {
      t.low_free = 0xFFFFFFFFu, t.high_live = 0, t.count = 0, t.bytes = 0, t.hash = 0;
      t.printed_low = 0xFFFFFFFFu;
    }
  }

  // The explicit size for a heap name in pdz_level_memory_sizes, in bytes; 0 = not named.
  uint32_t ExplicitSize(const std::string &name)
  {
    const std::string list = "," + std::string(REXCVAR_GET(pdz_level_memory_sizes)) + ",";
    const std::string key = "," + name + ":";
    const size_t at = list.find(key);
    if (at == std::string::npos)
    {
      return 0;
    }
    const double mb = std::atof(list.c_str() + at + key.size());
    return mb > 0.0 && mb < 2048.0 ? uint32_t(mb * 1048576.0) & ~0xFFFFu : 0;
  }
  bool LevelHeap(const std::string &name)
  {
    return name == "LevelCPU" || name == "LevelDynamicCPU" || name == "LevelGPUWC" || name == "LevelGPURW" ||
           name == "LevelCPU2" || name == "LevelDynamicGPUWC";
  }
} // namespace

// Call-ins from the level's life (counter_op_hands.cpp's hooks of Rare's load job, level start and unload).
void PdzLevelMemoryMark(const uint8_t *base, const char *what, bool reset_after)
{
  if (REXCVAR_GET(pdz_level_memory_log))
  {
    PrintAll(base, what, true);
  }
  if (reset_after)
  {
    ResetMarks();
  }
}
// For other mods' own decisions and log lines: free bytes of a heap by Rare's name (0 = no such heap / not made).
uint32_t PdzLevelMemoryFree(const uint8_t *base, const char *name)
{
  for (uint32_t i = 0; i < kHeaps; ++i)
  {
    const uint32_t obj = Load32(base, kTable + i * kEntry + kObject);
    if (Ptr(obj) && HeapName(base, i) == name)
    {
      return Load32(base, obj + kFree);
    }
  }
  return 0;
}
uint32_t PdzLevelMemoryRefused() { return g_failed_total.load(); }
// How many bytes larger than Rare's a heap was made (0 = Rare's size, or not made yet).
uint32_t PdzLevelMemoryExtra(const uint8_t *base, const char *name)
{
  for (uint32_t i = 0; i < kHeaps; ++i)
  {
    if (g_track[i].rare_size && HeapName(base, i) == name)
    {
      const uint32_t now = Load32(base, kTable + i * kEntry + kSize);
      return now > g_track[i].rare_size ? now - g_track[i].rare_size : 0;
    }
  }
  return 0;
}

// Make heap (index): the larger budget is written into Rare's table just before Rare makes the heap from it.
REX_HOOK_RAW(sub_8239DB88)
{
  const uint32_t index = ctx.r3.u32;
  if (index >= kHeaps)
  {
    __imp__sub_8239DB88(ctx, base);
    return;
  }
  const uint32_t e = kTable + index * kEntry;
  const std::string name = HeapName(base, index);
  const uint32_t rare_size = Load32(base, e + kSize), rare_blocks = Load32(base, e + kBlocks);
  g_track[index].rare_size = rare_size, g_track[index].rare_blocks = rare_blocks;
  uint32_t want = ExplicitSize(name);
  const double scale = std::clamp(double(REXCVAR_GET(pdz_level_memory_scale)), 1.0, 2.0);
  if (!want && scale > 1.0 && LevelHeap(name))
  {
    want = uint32_t(double(rare_size) * scale) & ~0xFFFFu;
  }
  std::string change = "Rare's size";
  // A physical heap (kind 2) shares the console's 512 MB with every other physical allocation: a raise never takes
  // the free physical memory under kPhysicalReserve (measured: after Rare's heaps 183.7 MB stay free for the whole
  // session and nothing else asks for physical memory later).
  if (want > rare_size && Load32(base, e + kKind) == 2)
  {
    constexpr uint64_t kPhysicalReserve = 64ull << 20;
    auto *memory = REX_KERNEL_MEMORY();
    auto *physical = memory ? memory->GetPhysicalHeap() : nullptr;
    const uint64_t free_now = physical ? uint64_t(physical->unreserved_page_count()) * physical->page_size() : 0;
    // Rare's heaps still to come are not counted here: the level heaps are made after Rare's three big system heaps,
    // and the physical ones after LevelGPUWC are under 1 MB together.
    const uint64_t room = free_now > kPhysicalReserve + rare_size ? free_now - kPhysicalReserve : rare_size;
    if (want > room)
    {
      REXLOG_WARN("pdz_level_memory: heap {} '{}': the asked {:.2f} MB is cut to {:.2f} MB ({:.1f} MB of physical "
                  "memory free, {:.0f} MB kept free)",
                  index, name, Mb(want), Mb(room & ~0xFFFFull), Mb(free_now), Mb(kPhysicalReserve));
      want = uint32_t(room) & ~0xFFFFu;
    }
  }
  if (want > rare_size)
  {
    // The block table grows with the size (a heap with more room and no more blocks would refuse on the count).
    const uint32_t blocks = uint32_t(std::min<uint64_t>(uint64_t(rare_blocks) * want / rare_size + 1, 0x7FFFu));
    Store32(base, e + kSize, want);
    Store32(base, e + kBlocks, std::max(blocks, rare_blocks));
    change = fmt::format("RAISED from Rare's {:.2f} MB / {} blocks", Mb(rare_size), rare_blocks);
  }
  else if (want)
  {
    change = fmt::format("Rare's size (the asked {:.2f} MB is not above it)", Mb(want));
  }
  __imp__sub_8239DB88(ctx, base);
  const uint32_t obj = Load32(base, e + kObject);
  g_track[index].object = obj;
  ++g_made;
  if (want > rare_size || REXCVAR_GET(pdz_level_memory_log))
  {
    REXLOG_INFO("pdz_level_memory: heap {} '{}' made: kind {}, {:.2f} MB, {} blocks, at {:08X} (+{:X}), heap object {:08X}; {}",
                index, name, Load32(base, e + kKind), Mb(Load32(base, e + kSize)), Load32(base, e + kBlocks),
                Load32(base, e + kBase), Load32(base, e + kBlockSize), obj, change);
    REXLOG_INFO("pdz_level_memory: after heap {}: {}", index, ConsoleMemory());
  }
  if (!Ptr(obj) || (Load32(base, e + kKind) == 2 && !Ptr(Load32(base, e + kBase))))
  {
    REXLOG_ERROR("pdz_level_memory: heap {} '{}' could NOT be made at {:.2f} MB ({}): the console's memory is used up. "
                 "Lower pdz_level_memory_scale / pdz_level_memory_sizes",
                 index, name, Mb(Load32(base, e + kSize)), change);
  }
}

// The heap object's allocate. Hot (every allocation of the game): one compare when nothing is wrong and the log is off.
REX_HOOK_RAW(sub_8273CF98)
{
  const uint32_t heap = ctx.r3.u32, bytes = ctx.r4.u32;
  const bool log = REXCVAR_GET(pdz_level_memory_log);
  const uint32_t lr = uint32_t(ctx.lr), sp = ctx.r1.u32;
  __imp__sub_8273CF98(ctx, base);
  if (ctx.r3.u32 == 0) [[unlikely]]
  {
    const int i = IndexOf(heap);
    const uint32_t n = ++g_failed_total;
    if (i >= 0)
    {
      ++g_track[i].failed;
    }
    if (n <= 40 || (n % 1000) == 0)
    {
      REXLOG_WARN("pdz_level_memory: REFUSED request #{}: {:.3f} MB ({} bytes) from {}; guest stack {}", n, Mb(bytes),
                  bytes, i >= 0 ? HeapLine(base, uint32_t(i)) : fmt::format("heap object {:08X}", heap),
                  GuestStack(lr, sp, base));
    }
    return;
  }
  if (!log)
  {
    return;
  }
  const int i = IndexOf(heap);
  if (i < 0)
  {
    return;
  }
  Track &t = g_track[i];
  {
    // pdz_level_memory_trace: read once; the name is compared only when the size matches.
    static std::once_flag once;
    static uint32_t trace_bytes = 0;
    static std::string trace_heap;
    static std::atomic<uint32_t> traced{0};
    std::call_once(once, [] {
      const std::string v = REXCVAR_GET(pdz_level_memory_trace);
      const size_t colon = v.find(':');
      if (colon != std::string::npos)
      {
        trace_heap = v.substr(0, colon);
        trace_bytes = uint32_t(std::strtoul(v.c_str() + colon + 1, nullptr, 10));
      }
    });
    if (trace_bytes && bytes == trace_bytes && HeapName(base, uint32_t(i)) == trace_heap && traced.fetch_add(1) < 400)
    {
      REXLOG_INFO("pdz_level_memory: trace: {} bytes from {} -> {:08X}; guest stack {}", bytes, trace_heap,
                  ctx.r3.u32, GuestStack(lr, sp, base, 8));
    }
  }
  const uint32_t free_now = Load32(base, heap + kFree), live = Load32(base, heap + kLive);
  if (free_now < t.low_free.load(std::memory_order_relaxed))
  {
    t.low_free.store(free_now, std::memory_order_relaxed);
  }
  if (live > t.high_live.load(std::memory_order_relaxed))
  {
    t.high_live.store(live, std::memory_order_relaxed);
  }
  t.count.fetch_add(1, std::memory_order_relaxed);
  t.bytes.fetch_add(bytes, std::memory_order_relaxed);
  t.hash.fetch_add(Mix(bytes), std::memory_order_relaxed);
  // Every 15 s (checked once in 256 requests): the heaps whose fullest point moved.
  if ((g_tick.fetch_add(1, std::memory_order_relaxed) & 0xFF) == 0)
  {
    const int64_t now = NowMs();
    int64_t due = g_next_print_ms.load(std::memory_order_relaxed);
    if (now >= due && g_next_print_ms.compare_exchange_strong(due, now + 15000))
    {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      for (uint32_t k = 0; k < kHeaps; ++k)
      {
        const uint32_t low = g_track[k].low_free.load(std::memory_order_relaxed);
        if (low != g_track[k].printed_low && Ptr(g_track[k].object.load()))
        {
          g_track[k].printed_low = low;
          REXLOG_INFO("pdz_level_memory: fullest: {:2} {}", k, HeapLine(base, k));
        }
      }
    }
  }
}

// Rare's disc-error screen: who asked for it, and the heaps at that moment.
REX_HOOK_RAW(sub_82225470)
{
  ++g_disc_errors;
  REXLOG_ERROR("pdz_level_memory: Rare's DISC-ERROR screen is asked for (82225470); guest stack {}; refused requests "
               "this session {}",
               GuestStack(uint32_t(ctx.lr), ctx.r1.u32, base, 14), g_failed_total.load());
  PrintAll(base, "at the disc-error screen", false);
  __imp__sub_82225470(ctx, base);
}
