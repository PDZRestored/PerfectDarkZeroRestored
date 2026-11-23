// Native renderer: a picture of the player's own for the COUNTER OPERATIVE entry of MISSION MODE (Counter-Op round
// 12, item 11). See native_menu_card.h for how it is shown; this file holds the switch, the PNG load and the state
// the menu code (game thread) and the texture cache (record thread) share.
//
// The picture is the player's file: it is read where it lies, never copied, never written, never shipped. Rare's
// cards are 1024 x 1024 with transparency (straight alpha, not premultiplied: the transparent texels of Rare's co-op
// card carry colour) and only their middle shows; a smaller PNG is centred on a transparent 1024 x 1024 in memory. It
// is never scaled and never cropped: a PNG larger than 1024 in either direction is refused.
#include "native_menu_card.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#include <stb_image.h>

REXCVAR_DEFINE_STRING(pdz_counterop_menu_card_file, "", "Gameplay",
                      "Counter-Operative menu entry (default empty = off): the full path of a PNG of your own to show "
                      "as the COUNTER OPERATIVE entry's picture in MISSION MODE instead of Rare's co-op picture. "
                      "Native renderer only. Rare's pictures are 1024 x 1024 with a transparent background and only "
                      "the middle shows; a smaller PNG is centred on a transparent 1024 x 1024 (never scaled, never "
                      "cropped; larger than 1024 is refused). A missing or unreadable file = Rare's picture, one log "
                      "line. The file is only read")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::native::menucard
{
  std::atomic<bool> g_armed{false};
  std::atomic<uint64_t> g_generation{0};

  namespace
  {
    std::mutex g_mutex;
    std::shared_ptr<const std::vector<uint8_t>> g_pixels; // the loaded picture (g_mutex)
    std::string g_tried_path;                              // the cvar value of the last load attempt (g_mutex)
    bool g_tried_ok = false;                               // ... and whether it gave g_pixels
    std::atomic<uint64_t> g_shown_generation{0};           // the load whose first draw was logged

    // Loads `path` into a kSize x kSize RGBA8 image; false (one log line) when it can't be used.
    bool Load(const std::string &path, std::shared_ptr<const std::vector<uint8_t>> &out)
    {
      std::error_code ec;
      const std::filesystem::path file = std::filesystem::u8path(path);
      const uint64_t bytes = std::filesystem::is_regular_file(file, ec) ? std::filesystem::file_size(file, ec) : 0;
      if (ec || bytes == 0 || bytes > (64ull << 20))
      {
        REXLOG_WARN("pdz_counterop: menu card: '{}' is missing or not a readable file (pdz_counterop_menu_card_file): "
                    "the COUNTER OPERATIVE entry keeps Rare's co-op picture",
                    path);
        return false;
      }
      std::vector<uint8_t> data(size_t(bytes), 0);
      {
        std::ifstream in(file, std::ios::binary);
        if (!in || !in.read(reinterpret_cast<char *>(data.data()), std::streamsize(data.size())))
        {
          REXLOG_WARN("pdz_counterop: menu card: '{}' could not be read (pdz_counterop_menu_card_file): the COUNTER "
                      "OPERATIVE entry keeps Rare's co-op picture",
                      path);
          return false;
        }
      }
      int width = 0, height = 0, channels = 0;
      stbi_uc *rgba = stbi_load_from_memory(data.data(), int(data.size()), &width, &height, &channels, 4);
      if (!rgba || width <= 0 || height <= 0)
      {
        const char *reason = stbi_failure_reason();
        REXLOG_WARN("pdz_counterop: menu card: '{}' is not a PNG this build can decode ({}): the COUNTER OPERATIVE "
                    "entry keeps Rare's co-op picture",
                    path, reason ? reason : "no reason given");
        if (rgba)
        {
          stbi_image_free(rgba);
        }
        return false;
      }
      if (uint32_t(width) > kSize || uint32_t(height) > kSize)
      {
        REXLOG_WARN("pdz_counterop: menu card: '{}' is {} x {}, larger than Rare's {} x {} card; it is never scaled "
                    "or cropped, so the COUNTER OPERATIVE entry keeps Rare's co-op picture",
                    path, width, height, kSize, kSize);
        stbi_image_free(rgba);
        return false;
      }
      // Centred on transparent black; the PNG's own bytes untouched (straight alpha, as Rare's card).
      auto image = std::make_shared<std::vector<uint8_t>>(size_t(kSize) * kSize * 4, uint8_t(0));
      const uint32_t x0 = (kSize - uint32_t(width)) / 2, y0 = (kSize - uint32_t(height)) / 2;
      for (uint32_t y = 0; y < uint32_t(height); ++y)
      {
        std::memcpy(image->data() + (size_t(y0 + y) * kSize + x0) * 4, rgba + size_t(y) * size_t(width) * 4,
                    size_t(width) * 4);
      }
      stbi_image_free(rgba);
      REXLOG_INFO("pdz_counterop: menu card: loaded '{}': {} x {} ({} channel(s) in the file), centred on a "
                  "transparent {} x {} at ({}, {}) to ({}, {}), not scaled; shown for the COUNTER OPERATIVE entry "
                  "by the native renderer (pdz_counterop_menu_card_file)",
                  path, width, height, channels, kSize, kSize, x0, y0, x0 + uint32_t(width) - 1,
                  y0 + uint32_t(height) - 1);
      out = std::move(image);
      return true;
    }
  } // namespace

  std::shared_ptr<const std::vector<uint8_t>> Pixels(uint64_t &generation)
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    generation = g_generation.load(std::memory_order_relaxed);
    return g_pixels;
  }

  void NoteShown(uint32_t guest_base_address, uint64_t frame)
  {
    const uint64_t generation = Generation();
    if (g_shown_generation.load(std::memory_order_relaxed) == generation)
    {
      return;
    }
    g_shown_generation.store(generation, std::memory_order_relaxed);
    REXLOG_INFO("pdz_counterop: menu card: first shown: the player's picture bound in place of the guest texture "
                "{:08X} (k_DXT2_3 {} x {}, Rare's co-op card picture) in frame {}",
                guest_base_address, kSize, kSize, frame);
  }

  void Disarm() { g_armed.store(false, std::memory_order_relaxed); }
} // namespace pdz::native::menucard

void PdzCounterOpMenuCardScene(bool counterop_card)
{
  using namespace pdz::native::menucard;
  if (!counterop_card)
  {
    // Another scene: the card is not (or no longer) the one shown.
    if (g_armed.exchange(false, std::memory_order_relaxed))
    {
      // Test aid, twice per run at most: the card was left (another scene asked for) before any draw used the
      // picture. Normal when the entry is passed quickly or the native renderer is off; a fault if it is every time.
      static uint32_t logged = 0;
      if (g_shown_generation.load(std::memory_order_relaxed) != Generation() && logged < 2)
      {
        ++logged;
        REXLOG_INFO("pdz_counterop: menu card: another scene was asked for before the picture was drawn once (the "
                    "native renderer off, or the entry left at once)");
      }
    }
    return;
  }
  const std::string path = REXCVAR_GET(pdz_counterop_menu_card_file);
  if (path.empty())
  {
    Disarm();
    return;
  }
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (path != g_tried_path)
    {
      // Once per value of the switch (a bad file is reported once, not at every visit of the entry).
      g_tried_path = path;
      std::shared_ptr<const std::vector<uint8_t>> image;
      g_tried_ok = Load(path, image);
      if (g_tried_ok)
      {
        g_pixels = std::move(image);
        g_generation.fetch_add(1, std::memory_order_relaxed);
      }
      else
      {
        g_pixels.reset();
      }
    }
    ok = g_tried_ok && g_pixels;
  }
  g_armed.store(ok, std::memory_order_relaxed);
}
