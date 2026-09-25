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
// Affine maths
// ----------------------------------------------------------------------------
// These two functions are transcriptions of LGFXBase::make_rotation_matrix() and
// the file-local make_invert_affine32() in LGFXBase.cpp. They have to be exact,
// including the fixed point rounding, because the pixel a destination lands on is
// decided entirely by these integers: a one-unit difference in any of the inverse
// matrix entries moves the sampling grid and changes glyph edges. The literal below
// is copied from LGFXBase.cpp so that sinf()/cosf() see the same input.
constexpr uint32_t kFpScale = 16;
constexpr float    kDegToRad = 0.017453292519943295769236907684886f;

void makeRotationMatrix(float* m, float dst_x, float dst_y, float src_x, float src_y,
                        float angle, float zoom_x, float zoom_y)
{
    const float rad = fmodf(angle, 360) * kDegToRad;
    const float sin_f = sinf(rad);
    const float cos_f = cosf(rad);
    m[0] =  cos_f * zoom_x;
    m[1] = -sin_f * zoom_y;
    m[2] =  dst_x - src_x * m[0] - src_y * m[1];
    m[3] =  sin_f * zoom_x;
    m[4] =  cos_f * zoom_y;
    m[5] =  dst_y - src_x * m[3] - src_y * m[4];
}

bool makeInvertAffine32(int32_t* out, const float* m)
{
    float det = m[0] * m[4] - m[1] * m[3];
    if (det == 0.0f) {
        return false;
    }
    det = static_cast<float>(1 << kFpScale) / det;
    out[0] = static_cast<int32_t>(roundf(det *  m[4]));
    out[1] = static_cast<int32_t>(roundf(det * -m[1]));
    out[2] = static_cast<int32_t>(roundf(det * (m[1] * m[5] - m[2] * m[4])));
    out[3] = static_cast<int32_t>(roundf(det * -m[3]));
    out[4] = static_cast<int32_t>(roundf(det *  m[0]));
    out[5] = static_cast<int32_t>(roundf(det * (m[2] * m[3] - m[0] * m[5])));
    return true;
}

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

// Same pixels as LGFXBase::push_image_affine(), without the per-pixel overhead.
//
// The library walks the destination a row at a time and, for each row, clips the
// span against the rotated source quad and then calls a generic copy routine that
// per pixel re-derives the source index, calls a conversion function through a
// function pointer, and tests against a transparency value that can never match.
// That costs about 15 cycles per pixel, and at roughly 175k destination pixels per
// frame it is the single largest cost in the frame.
//
// The clipping here is a transcription of the library's, integer for integer, so
// the span and the starting source coordinate for every row are identical. Only the
// inner loop differs: the source index is computed directly and the values are
// copied as plain 16-bit words. Both sides use rgb565_2Byte, and color conversion
// between identical formats is the identity, so a direct word copy is exact.
void BandPanel::composeBand(LGFX_Sprite& canvas, int x0, int y, int w, int h,
                            float angleDeg, float zoom, uint8_t* buffer)
{
    const int32_t src_w = canvas.width();
    const int32_t src_h = canvas.height();
    // The library's alignment mask is 7 >> (bits >> 1), which is zero at 16bpp, so
    // the source stride is the source width.
    const int32_t src_bitwidth = src_w;

    float m[6];
    makeRotationMatrix(m,
                       static_cast<float>(kVisibleCentre - x0) + 0.5f,
                       static_cast<float>(kVisibleCentre - y) + 0.5f,
                       canvas.getPivotX() + 0.5f,
                       canvas.getPivotY() + 0.5f,
                       angleDeg, zoom, zoom);

    int32_t ia[6];
    if (!makeInvertAffine32(ia, m)) {
        return;
    }

    // Destination rows this band can be touched on, clipped to the band.
    int32_t min_y = static_cast<int32_t>(m[3] * static_cast<float>(src_w << kFpScale));
    int32_t max_y = static_cast<int32_t>(m[4] * static_cast<float>(src_h << kFpScale));
    if ((min_y < 0) == (max_y < 0)) {
        max_y += min_y;
        min_y = 0;
    }
    if (min_y > max_y) {
        std::swap(min_y, max_y);
    }
    {
        const int32_t offset_y32 = static_cast<int32_t>(
            m[5] * static_cast<float>(1 << kFpScale) + static_cast<float>(1 << (kFpScale - 1)));
        min_y = std::max<int32_t>(0, (offset_y32 + min_y - 1) >> kFpScale);
        max_y = std::min<int32_t>(h, (offset_y32 + max_y + 1) >> kFpScale);
        if (min_y >= max_y) {
            return;
        }
    }

    // Step the inverse matrix to the first row, then advance by one row per iteration.
    const int32_t row_offset = (min_y << 1) - 1;
    ia[2] += ((ia[0] + ia[1] * row_offset) >> 1);
    ia[5] += ((ia[3] + ia[4] * row_offset) >> 1);

    const int32_t scale_w = src_w << kFpScale;
    const int32_t xs1 = (ia[0] < 0 ? -scale_w : 1) - ia[0];
    const int32_t xs2 = (ia[0] < 0 ? 0 : (1 - scale_w)) - ia[0];
    const int32_t scale_h = src_h << kFpScale;
    const int32_t ys1 = (ia[3] < 0 ? -scale_h : 1) - ia[3];
    const int32_t ys2 = (ia[3] < 0 ? 0 : (1 - scale_h)) - ia[3];

    const int32_t cl = 0;
    const int32_t cr = w;

    const uint16_t* const src = static_cast<const uint16_t*>(canvas.getBuffer());
    uint16_t* const dst = reinterpret_cast<uint16_t*>(buffer);
    const int32_t addx = ia[0];
    const int32_t addy = ia[3];

    int32_t row = min_y - max_y;
    do {
        ia[2] += ia[1];
        ia[5] += ia[4];

        const int32_t left = std::max(cl, std::max(
            ia[0] ? (ia[2] + xs1) / -ia[0] : cl,
            ia[3] ? (ia[5] + ys1) / -ia[3] : cl));
        const int32_t right = std::min(cr, std::min(
            ia[0] ? (ia[2] + xs2) / -ia[0] : cr,
            ia[3] ? (ia[5] + ys2) / -ia[3] : cr));

        if (left < right) {
            int32_t sx32 = ia[2] + left * ia[0];
            int32_t sy32 = ia[5] + left * ia[3];
            uint16_t* d = dst + static_cast<int32_t>(row + max_y) * w + left;
            int32_t n = right - left;
            do {
                *d++ = src[(sx32 >> kFpScale) + (sy32 >> kFpScale) * src_bitwidth];
                sx32 += addx;
                sy32 += addy;
            } while (--n);
        }
    } while (++row);
}

void BandPanel::runSelfTest(LGFX_Sprite& canvas, float zoom)
{
    // A spread of angles and band positions, covering the quad edges falling inside,
    // outside and across the band, and both matrix diagonals taking each sign.
    static const float kAngles[] = { 0.0f, 7.5f, 33.0f, 91.0f, 179.4f, 271.0f };
    static const int   kRows[]   = { 96, 232, 384 };

    int mismatch = 0;
    int compared = 0;

    for (float angle : kAngles) {
        for (int row : kRows) {
            uint8_t* ref  = _buffer;
            uint8_t* mine = _buffer + kBandBytes;

            _band.setBuffer(ref, kPanelExtent, kBandHeight, 16);
            _band.fillScreen(kBackground);
            canvas.pushRotateZoom(&_band,
                                  static_cast<float>(kVisibleCentre),
                                  static_cast<float>(kVisibleCentre - row),
                                  angle, zoom, zoom);

            _band.setBuffer(mine, kPanelExtent, kBandHeight, 16);
            _band.fillScreen(kBackground);
            composeBand(canvas, 0, row, kPanelExtent, kBandHeight, angle, zoom, mine);

            compared += kPanelExtent * kBandHeight;
            const uint16_t* a = reinterpret_cast<const uint16_t*>(ref);
            const uint16_t* b = reinterpret_cast<const uint16_t*>(mine);
            for (int i = 0; i < kPanelExtent * kBandHeight; ++i) {
                if (a[i] != b[i]) {
                    ++mismatch;
                }
            }
        }
    }

    _affine_ok = (mismatch == 0);
    Serial.printf("[BANDPANEL] affine self-test: %d of %d px differ -> %s\n",
                  mismatch, compared,
                  _affine_ok ? "using own compositor" : "using M5GFX compositor");
}

void BandPanel::pushRotated(LGFX_Sprite& canvas, float angleDeg, float zoom)
{
    if (!ready()) {
        return;
    }

    // The compositor can only be validated once the canvas has real content on it,
    // so the check runs on the first frame rather than in begin().
    if (!_self_test_done) {
        _self_test_done = true;
        runSelfTest(canvas, zoom);
    }

    _band_count  = 0;
    _compose_us  = 0;
    _transfer_us = 0;
    _setup_us    = 0;
    _clear_us    = 0;
    _affine_us   = 0;
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
        const int64_t t_setup = esp_timer_get_time();
        _band.fillScreen(kBackground);
        const int64_t t_clear = esp_timer_get_time();
        if (_affine_ok) {
            composeBand(canvas, x0, y, w, h, angleDeg, zoom, buffer);
        } else {
            canvas.pushRotateZoom(&_band,
                                  static_cast<float>(kVisibleCentre - x0),
                                  static_cast<float>(kVisibleCentre - y),
                                  angleDeg, zoom, zoom);
        }

        const int64_t t1 = esp_timer_get_time();

        _panel->setWindow(x0, y, x0 + w - 1, y + h - 1);
        _panel->write_bytes(buffer, static_cast<uint32_t>(w) * h * 2, true);

        const int64_t t2 = esp_timer_get_time();

        _setup_us    += t_setup - t0;
        _clear_us    += t_clear - t_setup;
        _affine_us   += t1 - t_clear;
        _compose_us  += t1 - t0;
        _transfer_us += t2 - t1;
        _bytes_sent  += static_cast<size_t>(w) * h * 2;
        _band_count++;
    }
}

} // namespace r2r
