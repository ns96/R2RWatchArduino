/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#include "r2r_panel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace r2r {

namespace {

// Panel geometry. The logical panel is 468x468 and the visible area is a circle
// 466 px across centred on it; the corners are behind the bezel.
constexpr int kPanelExtent   = 468;
constexpr int kVisibleCentre = 233;
constexpr int kVisibleRadius = 233;

// The band buffer lives in internal SRAM because it is the DMA source, and there
// are two of them alternating. See pushRotated() for why the second one is not
// optional.
//
// Height is kept small for two reasons: it bounds the internal-DRAM cost, which is
// the binding memory constraint on this board, and a narrower band tracks the
// circle's curve more closely, so less of the masked corner area gets transferred.
// Height is the only free variable in the memory budget, so it is halved whenever
// the buffer count doubles.
constexpr int kBandHeight   = 8;
constexpr int kBandBuffers  = 2;
constexpr size_t kBandBytes = static_cast<size_t>(kPanelExtent) * kBandHeight * 2;

// The canvas is cleared with memset, so "background" here means the same zero.
constexpr uint16_t kBackground = 0x0000;

// ----------------------------------------------------------------------------
// Borrowing the real QSPI panel
// ----------------------------------------------------------------------------
// M5GFX installs the *framebuffer* wrapper as the display's panel, and the real
// Panel_AMOLED is held in the wrapper's protected _panel member, so there is no
// public route to it.
//
// Deriving from the wrapper gives a view of that member. The derived type adds no
// data members and no virtual functions, so the pointer conversion is a no-op and
// the member read lands on the real field. This is an ABI-level accessor rather
// than a supported one, which is confined to this file and guarded at runtime: the
// borrowed panel must point back at the wrapper we borrowed it from, a relationship
// the library sets up itself in Panel_AMOLED::initPanelFb().
//
// Borrowing the panel that already exists, instead of constructing a second one, is
// the whole point. It is already configured, initialised and proven by M5GFX, and
// this class only ever calls its public setWindow() and write_bytes().
struct PanelFbView : public lgfx::Panel_AMOLED_Framebuffer
{
    lgfx::Panel_AMOLED* borrowedPanel(void) const { return _panel; }
};

} // namespace

bool BandPanel::borrowPanel(void)
{
    auto* display_panel = M5.Display.getPanel();
    if (display_panel == nullptr) {
        return false;
    }

    auto* wrap = static_cast<lgfx::Panel_AMOLED_Framebuffer*>(display_panel);
    lgfx::Panel_AMOLED* panel = static_cast<PanelFbView*>(wrap)->borrowedPanel();
    if (panel == nullptr || panel->getPanelFb() != wrap) {
        Serial.println("[BANDPANEL] panel is not a framebuffer wrapper; declining");
        return false;
    }

    _panel = panel;
    return true;
}

bool BandPanel::begin(void)
{
    if (!borrowPanel()) {
        Serial.println("[BANDPANEL] QSPI panel unavailable; using M5GFX framebuffer path");
        return false;
    }

    _buffer = static_cast<uint8_t*>(
        heap_caps_malloc(kBandBytes * kBandBuffers, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (_buffer == nullptr) {
        Serial.printf("[BANDPANEL] %u B band buffer refused; using M5GFX framebuffer path\n",
                      static_cast<unsigned>(kBandBytes * kBandBuffers));
        _panel = nullptr;
        return false;
    }
    std::memset(_buffer, 0, kBandBytes * kBandBuffers);
    _buffer_size = kBandBytes * kBandBuffers;

    // Attach the first buffer so the sprite is valid, then set the depth it will
    // use. The per-band attachments reuse the same memory with a different width.
    _band.setColorDepth(16);
    _band.setBuffer(_buffer, kPanelExtent, kBandHeight, 16);

    Serial.printf("[BANDPANEL] direct QSPI path active | bands %dx%d x%d (%u B) | free heap %u\n",
                  kPanelExtent, kBandHeight, kBandBuffers,
                  static_cast<unsigned>(_buffer_size), ESP.getFreeHeap());
    return true;
}

void BandPanel::pushRotated(LGFX_Sprite& canvas, float angleDeg, float zoom)
{
    if (!ready()) {
        return;
    }

    _band_count  = 0;
    _compose_us  = 0;
    _transfer_us = 0;
    _bytes_sent  = 0;

    const int radius_sq = kVisibleRadius * kVisibleRadius;
    int slot = 0;

    for (int y = 0; y < kPanelExtent; y += kBandHeight) {
        int h = kPanelExtent - y;
        if (h > kBandHeight) {
            h = kBandHeight;
        }

        // Clip to the chord of this band's widest row, which is the row nearest the
        // centre. Taking the band midpoint instead would clip too narrowly just above
        // and just below the centre, exactly where the rotated square is widest, and
        // would visibly truncate it.
        const int nearest = std::clamp(kVisibleCentre, y, y + h - 1);
        const int dy = (nearest >= kVisibleCentre) ? (nearest - kVisibleCentre)
                                                  : (kVisibleCentre - nearest);
        if (dy >= kVisibleRadius) {
            continue;
        }

        int half = static_cast<int>(std::sqrt(static_cast<float>(radius_sq - dy * dy))) + 1;
        if (half > kVisibleCentre) {
            half = kVisibleCentre;
        }

        int x0 = kVisibleCentre - half;
        int x1 = kVisibleCentre + half;
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > kPanelExtent - 1) {
            x1 = kPanelExtent - 1;
        }

        // Panel_AMOLED::setWindow() silently discards any window whose x or width is
        // odd, so the band geometry is snapped to even boundaries before use.
        x0 &= ~1;
        int w = ((x1 - x0) + 1 + 1) & ~1;
        if (x0 + w > kPanelExtent) {
            w = (kPanelExtent - x0) & ~1;
        }
        if (w <= 0) {
            continue;
        }
        if (static_cast<size_t>(w) * h * 2 > kBandBytes) {
            continue;
        }

        // Alternate between the two band buffers. This is not an optimisation.
        //
        // Panel_AMOLED::write_bytes() starts an asynchronous DMA and returns
        // immediately, so the buffer it was handed keeps being read for the whole
        // duration of the transfer. Composing the next band into that same buffer
        // races the DMA, and the DMA always loses: fillScreen() is a flat memset
        // that finishes in a few microseconds while the DMA needs hundreds to read
        // the same bytes, so the panel receives a mix of the cleared buffer and the
        // half-composed one.
        //
        // With two buffers the race is gone by construction, because every
        // write_bytes() waits for the previous band's transfer (the other buffer)
        // before it starts, which also guarantees the buffer about to be composed
        // into is already idle.
        uint8_t* buffer = _buffer + (slot ? kBandBytes : 0);
        slot ^= 1;

        const int64_t t0 = esp_timer_get_time();

        // Re-point the sprite at this band's width, clear it, then place the canvas so
        // that its centre lands on the panel centre. The pivot is expressed in the
        // band sprite's own coordinates, which is what keeps the composition identical
        // to the full-frame push for the rows this band covers.
        _band.setBuffer(buffer, w, h, 16);
        _band.fillScreen(kBackground);
        canvas.pushRotateZoom(&_band,
                              static_cast<float>(kVisibleCentre - x0),
                              static_cast<float>(kVisibleCentre - y),
                              angleDeg, zoom, zoom);

        const int64_t t1 = esp_timer_get_time();

        _panel->setWindow(x0, y, x0 + w - 1, y + h - 1);
        _panel->write_bytes(buffer, static_cast<uint32_t>(w) * h * 2, true);

        const int64_t t2 = esp_timer_get_time();

        _compose_us  += t1 - t0;
        _transfer_us += t2 - t1;
        _bytes_sent  += static_cast<size_t>(w) * h * 2;
        _band_count++;
    }
}

} // namespace r2r
