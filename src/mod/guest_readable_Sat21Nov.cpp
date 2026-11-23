// Sat 21 Nov (prep job): the Nightclub explosion stutter (play log 032: about 25 frames lost in the 10 s after the oil
// drums blew up; a scripted Laboratory run with 11 deaths: 31 slow frames in the 15 s of the deaths, none before or
// after). The game thread's samples in those frames sat in our hook of Rare's prop body setup (sub_825E0EF0, every
// new prop body: dropped guns, debris, flying drums) and below it in the OS: each of the mod's "is this guest range
// readable?" checks asked Windows (rex::memory::QueryProtect = VirtualQuery) for every 4 KB page, and a death or an
// explosion makes many bodies, contact sounds, wall cuts and meter reads in one frame, each with several checks.
// pdz_fix_readable_cache (default on): the answer is kept per 64 KB block (a block is cached only when one query
// shows the whole block readable) and dropped whenever the guest decommits, releases or protects memory
// (rex::memory::GuestReleaseEpoch, bumped in the SDK's heap); so the OS is asked about once per block per such change
// instead of per check. Off = every check asks the OS for every page, as before.
#include "guest_readable_Sat21Nov.h"

#include <rex/cvar.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

#include <array>
#include <atomic>
#include <cstdint>

REXCVAR_DEFINE_BOOL(pdz_fix_readable_cache, true, "Render",
                    "Performance (Sat 21 Nov): the mod's checks that a piece of game memory is readable remember the "
                    "answer per 64 KB block until the game next frees or protects memory, instead of asking Windows "
                    "every time (explosions and deaths made hundreds of such questions in one frame: a dropped frame). "
                    "Off = ask Windows every time")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::guestmem
{
  namespace
  {
    constexpr uint32_t kBlockShift = 16; // 64 KB
    std::array<std::atomic<uint64_t>, (1u << (32 - kBlockShift)) / 64> g_bits{};
    std::atomic<uint32_t> g_epoch{~0u};

    bool PageReadable(uint8_t *base, uint32_t at, size_t *region)
    {
      size_t length = 1;
      rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
      if (!rex::memory::QueryProtect(base + at, length, access) ||
          (uint32_t(access) & uint32_t(rex::memory::PageAccess::kReadOnly)) == 0)
      {
        return false;
      }
      if (region)
      {
        *region = length;
      }
      return true;
    }

    bool SlowRange(uint8_t *base, uint32_t address, uint32_t size)
    {
      for (uint32_t at = address; at < address + size; at = (at & ~0xFFFu) + 0x1000u)
      {
        if (!PageReadable(base, at, nullptr))
        {
          return false;
        }
      }
      return true;
    }
  } // namespace

  bool CacheOn() { return REXCVAR_GET(pdz_fix_readable_cache); }

  bool Readable(uint8_t *base, uint32_t address, uint32_t size)
  {
    if (address < 0x10000u || uint64_t(address) + size > 0xFFFF0000ull)
    {
      return false;
    }
    if (size == 0)
    {
      size = 1;
    }
    if (!CacheOn())
    {
      return SlowRange(base, address, size);
    }
    const uint32_t epoch = rex::memory::GuestReleaseEpoch();
    if (g_epoch.load(std::memory_order_acquire) != epoch)
    {
      for (auto &w : g_bits)
      {
        w.store(0, std::memory_order_relaxed);
      }
      g_epoch.store(epoch, std::memory_order_release);
    }
    const uint32_t last = address + size - 1;
    for (uint32_t block = address >> kBlockShift; block <= (last >> kBlockShift); ++block)
    {
      const uint64_t bit = 1ull << (block & 63);
      if (g_bits[block >> 6].load(std::memory_order_relaxed) & bit)
      {
        continue;
      }
      const uint32_t start = block << kBlockShift;
      size_t region = 0;
      if (PageReadable(base, start, &region) && region >= (1u << kBlockShift))
      {
        if (rex::memory::GuestReleaseEpoch() == epoch) // (not across a release that happened meanwhile)
        {
          g_bits[block >> 6].fetch_or(bit, std::memory_order_relaxed);
        }
        continue;
      }
      // Not one readable region over the whole block: the part of the range in it, page by page.
      const uint32_t from = address > start ? address : start;
      const uint32_t end = start + ((1u << kBlockShift) - 1);
      const uint32_t to = last < end ? last : end;
      if (!SlowRange(base, from, to - from + 1))
      {
        return false;
      }
    }
    return true;
  }
} // namespace pdz::guestmem
