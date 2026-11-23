// Native renderer: a picture of the player's own for the COUNTER OPERATIVE entry of MISSION MODE
// (native_menu_card.cpp, pdz_counterop_menu_card_file; Counter-Op round 12, item 11).
//
// The entry's card is Rare's co-op card scene (counter_op_menu.cpp, pdz_counterop_menu_coop_card), whose two picture
// elements draw one guest texture: 1024 x 1024 DXT2_3, no mips. While that card is the info card on screen ("armed":
// from its XuiSceneCreate to the next XuiSceneCreate of anything else) Textures::Lookup serves a host texture made
// from the PNG for every fetch of that shape instead of the guest one. Nothing is written to guest memory, the guest
// texture stays cached as always, and with the cvar empty (the default) the only cost is one atomic load per Lookup.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace pdz::native::menucard
{
  constexpr uint32_t kSize = 1024;        // Rare's card pictures: 1024 x 1024
  constexpr uint32_t kGuestFormat = 19;   // xenos::TextureFormat k_DXT2_3

  extern std::atomic<bool> g_armed;       // the counter-op card is the info card on screen and a picture is loaded
  extern std::atomic<uint64_t> g_generation; // bumped by every successful load (0 = none yet)

  // Record thread (Textures::Lookup): cheap tests.
  inline bool Armed() { return g_armed.load(std::memory_order_relaxed); }
  inline uint64_t Generation() { return g_generation.load(std::memory_order_relaxed); }
  // The loaded picture: kSize x kSize RGBA8 (straight alpha, the PNG's bytes centred on transparent black), or null.
  std::shared_ptr<const std::vector<uint8_t>> Pixels(uint64_t &generation);
  // The replacement was bound for a draw (logs the first one of each load).
  void NoteShown(uint32_t guest_base_address, uint64_t frame);
  // Never substitute inside a level (Textures::NoteLevelChange).
  void Disarm();
} // namespace pdz::native::menucard

// Game thread (counter_op_menu.cpp, PdzCounterOpCreateInstead: every XuiSceneCreate while the counter-op menu is
// on). true = the scene just created is the counter-op entry's card made from Rare's co-op card; false = any other
// scene (the card is no longer the one shown).
void PdzCounterOpMenuCardScene(bool counterop_card);
