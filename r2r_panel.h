/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Arduino.h>
#include <cstdint>
#include <M5Unified.h>
#include <lgfx/v1/panel/Panel_AMOLED.hpp>

namespace r2r {

// ============================================================================
// Direct QSPI panel sink for the CO5300
// ============================================================================
// For this board M5GFX wraps the panel in a Panel_AMOLED_Framebuffer, so the
// display object the renderer normally draws to is a 438 KB PSRAM framebuffer
// rather than the panel. Every endWrite() then costs, per scanline, a PSRAM read
// plus a QSPI write, with a window command for each line: 468 window commands and
// a full framebuffer round trip per frame. That serialisation is the single
// largest cost in the frame and it also leaves the panel idle for much of it.
//
// This class talks to the panel instead. It composes the rotated canvas into a
// small internal-SRAM band sprite and hands each band over as one window command
// plus one DMA, so the PSRAM round trip disappears and the number of window
// commands drops from one per line to one per band.
//
// Two things make it safe to run alongside the M5GFX framebuffer, which stays
// installed and untouched:
//   - The bands are clipped to the chord of the visible circle, so the corners,
//     which are behind the bezel, are never transferred.
//   - Both subslicing paths already clip in software, so a band composed into a
//     band-sized sprite produces exactly the pixels the full-frame push would
//     have produced in that band.
class BandPanel
{
public:
    // Borrows the live QSPI panel and allocates the band buffer. Returns false if
    // either step fails, in which case the caller must keep using the M5GFX path.
    bool begin(void);

    // True once a usable panel and buffer are held. Both are acquired in begin()
    // and released together, so this never changes after a successful begin().
    bool ready(void) const { return _panel != nullptr && _buffer != nullptr; }

    // Compose the canvas into bands and stream them to the panel.
    void pushRotated(LGFX_Sprite& canvas, float angleDeg, float zoom);

    // Diagnostics for the periodic profile line.
    int     bandCount(void)  const { return _band_count; }
    int64_t composeUs(void)  const { return _compose_us; }
    int64_t transferUs(void) const { return _transfer_us; }
    int64_t setupUs(void)    const { return _setup_us; }
    int64_t clearUs(void)    const { return _clear_us; }
    int64_t affineUs(void)   const { return _affine_us; }
    size_t  bytesSent(void)  const { return _bytes_sent; }

    // True once the hand-rolled compositor has been proved equal to the library's
    // on real canvas content. Until then, and if it ever disagreed, the library
    // path is used instead.
    bool ownAffine(void)     const { return _affine_ok; }

private:
    lgfx::Panel_AMOLED* _panel = nullptr;
    LGFX_Sprite         _band;
    // One allocation holding the alternating band buffers. Two are required: the
    // panel's DMA reads a band buffer asynchronously, so composing the next band
    // into the same memory would race the transfer. See pushRotated().
    uint8_t*            _buffer = nullptr;
    size_t              _buffer_size = 0;

    int     _band_count  = 0;
    int64_t _compose_us  = 0;
    int64_t _transfer_us = 0;
    int64_t _setup_us    = 0;
    int64_t _clear_us    = 0;
    int64_t _affine_us   = 0;
    size_t  _bytes_sent  = 0;

    bool _affine_ok     = false;
    bool _self_test_done = false;

    bool borrowPanel(void);

    // Composes one band with a hand written affine blit. Produces exactly the pixels
    // LGFXBase::push_image_affine() produces for the same destination, but without
    // its generic per-pixel conversion call and transparency test.
    void composeBand(LGFX_Sprite& canvas, int x0, int y, int w, int h,
                     float angleDeg, float zoom, uint8_t* buffer);

    // Compares composeBand() against the library on the real canvas, over a spread of
    // angles and band positions, and enables the fast path only on an exact match.
    void runSelfTest(LGFX_Sprite& canvas, float zoom);
};

} // namespace r2r
