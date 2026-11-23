// Native renderer: the Laboratory's local darkness (pdz_lab_dark_visual; src/mod/lab_dark_patches.cpp). One
// full-screen multiply of the light buffer right after Rare's sun / ambient pass (pixel shader a95c4b61c177a282, one
// draw a view), by the ambient kept at each pixel: 1 away from every lamp that is out, down to the strength inside
// one. native_labdark.cpp.
#pragma once

#include <cstdint>
#include <d3d12.h>

#include "../shadow_record.h"
#include "native_device.h"
#include "native_surfaces.h"

namespace pdz::native::labdark
{
  // The pixel shader of Rare's sun / ambient pass (the same microcode in every level).
  constexpr uint64_t kAmbientPixelShader = 0xa95c4b61c177a282ull;

  // Called by the draw path right after it recorded a draw of kAmbientPixelShader (render lock held; the record
  // thread). `ps_constants` = the device struct's pixel float constants (big-endian, c0 first), `target` = colour
  // target 0 of the draw (the light buffer), `viewport` / `scissor` = the draw's host viewport and scissor,
  // `draws_before` = the draws into that target before this one (the first-use log). Records
  // the multiply pass when lab_dark has lamps for this draw; returns true when it did (the list's pipeline, root
  // signature and render targets are then the pass's own: the caller's next draw binds its state again).
  bool AfterAmbientDraw(NativeDevice &dev, const shadow::DrawRecord &r, const uint8_t *ps_constants,
                        HostSurface *target, const D3D12_VIEWPORT &viewport, const D3D12_RECT &scissor,
                        uint32_t draws_before);

  // Passes recorded so far (the draw-stats line / tests).
  uint64_t PassCount();
} // namespace pdz::native::labdark
