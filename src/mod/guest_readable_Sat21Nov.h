// Sat 21 Nov (prep job): one cached "is this guest range readable?" check for the mod's hooks.
#pragma once
#include <cstdint>

namespace pdz::guestmem
{
  // True while pdz_fix_readable_cache is on (the files' own per-page checks are used when it is off).
  bool CacheOn();
  // The guest range [address, address + size) is readable now. Asks the OS (VirtualQuery) at most once per 64 KB
  // block until guest memory is next decommitted / released / protected (rex::memory::GuestReleaseEpoch).
  bool Readable(uint8_t *base, uint32_t address, uint32_t size);
  // Sat 21 Nov evening: a pointer read from guest memory may be followed for n bytes (inside the guest's range
  // AND mapped readable now). For log / name helpers that follow pointers of objects not proven to be guards.
  inline bool Ok(const uint8_t *base, uint32_t a, uint32_t n)
  {
    return a >= 0x10000u && uint64_t(a) + n <= 0xFFFF0000ull && Readable(const_cast<uint8_t *>(base), a, n);
  }
} // namespace pdz::guestmem
