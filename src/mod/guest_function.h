// A guest function by address for our own indirect calls (vtable slots), or
// nullptr when the address is no recompiled function.
//
// Log 101 (Plant the Flag, split screen set up, map pack map: "[FATAL] Call to
// invalid or unregistered function at guest address 0x00000000 (LR
// 0x00000000)"): rex::runtime::ResolveIndirectFunction never returns null; for
// an address that isn't code it returns the SDK's trap, which aborts the game
// when called (thirdparty/rexglue-sdk/src/system/function_dispatcher.cpp). So
// every `if (!fn)` guard after it in our mods could never fire, and a call from
// one of our CallFrames reports target 0 and LR 0 whatever the slot held.
// GuestFunction turns the trap back into nullptr, so those guards work, and
// logs each refused address once (with who asked) for the next log.
//
// Sun 22 Nov (Counter-Op round 17: two closes to the desktop, Jungle and Trinity
// Escape): the body-fall sound called the ground slot of a jetpack rider who had
// just been killed off his jetpack; his movement controller was a destroyed
// object, whose slot was the game's runtime's pure-virtual handler (82AA2BC8:
// "R6025 pure virtual function call", then the game's exit path ends in the
// kernel bug check). That handler is refused here too (nullptr, logged once per
// caller): calling it always ends the game.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/ppc/func.h>

namespace pdz
{
  inline PPCFunc *GuestFunction(uint32_t address, const char *who = "a vtable slot")
  {
    static PPCFunc *const trap = rex::runtime::ResolveIndirectFunction(0);
    PPCFunc *fn = rex::runtime::ResolveIndirectFunction(address);
    constexpr uint32_t kPureCall = 0x82AA2BC8; // the game's runtime: _purecall (R6025, then the bug check)
    if (fn != trap && address != kPureCall)
    {
      return fn;
    }
    static std::mutex lock;
    static std::unordered_set<uint32_t> logged;
    std::lock_guard<std::mutex> hold(lock);
    if (logged.size() < 64 && logged.insert(address).second)
    {
      REXLOG_WARN("pdz: refused an indirect call to {:08X} ({}): {}", address, who,
                  address == kPureCall ? "the game's pure-virtual handler (a destroyed object: calling it ends the game)"
                                       : "not a recompiled function (log 101)");
    }
    return nullptr;
  }
} // namespace pdz
