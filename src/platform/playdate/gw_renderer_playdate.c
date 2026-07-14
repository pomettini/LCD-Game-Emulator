/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gw_renderer_playdate.h"

#include <string.h>

#define SOURCE_WIDTH 320u
#define SOURCE_HEIGHT 240u
#define SCREEN_X 40u
#define FRAME_ROW_BYTES 52u

#define FLAG_RENDERING_INVERTED UINT32_C(0x01)
#define FLAG_SEGMENTS_4BIT UINT32_C(0x10)
#define FLAG_SEGMENTS_2BIT UINT32_C(0x100)

static const uint8_t bayer4[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
};

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void set_black(uint8_t *frame, unsigned x, unsigned y)
{
    frame[y * FRAME_ROW_BYTES + x / 8] &= (uint8_t)~(0x80u >> (x & 7));
}

static unsigned rgb565_luma(uint16_t pixel)
{
    unsigned red = (pixel >> 11) & 31;
    unsigned green = (pixel >> 5) & 63;
    unsigned blue = pixel & 31;
    return (red * 77u) / 31u + (green * 150u) / 63u +
           (blue * 29u) / 31u;
}

static void draw_background(GWPlaydateRenderer *renderer, uint8_t *frame)
{
    const uint8_t *background = renderer->package->payload +
                                renderer->package->background.offset;
    unsigned y;
    unsigned x;

    for (y = 0; y < SOURCE_HEIGHT; ++y) {
        for (x = 0; x < SOURCE_WIDTH; ++x) {
            uint16_t pixel = read_le16(background + (y * SOURCE_WIDTH + x) * 2);
            unsigned luma = rgb565_luma(pixel);
            /*
             * The source panel tint represents the physical LCD glass.  On
             * Playdate, dithering that tint makes the whole screen look gray
             * and competes with the LCD sprites, so retain only truly dark
             * printed artwork and leave the panel paper-white.
             */
            if (luma < 64u)
                set_black(frame, SCREEN_X + x, y);
        }
    }
}

static unsigned segment_pixel(const GWPackage *package, uint32_t pixel_index)
{
    const uint8_t *pixels = package->payload + package->segments.offset;
    if (package->flags & FLAG_SEGMENTS_2BIT) {
        unsigned value = (pixels[pixel_index >> 2] >>
                          (2u * (pixel_index & 3))) & 3u;
        return value * 85u;
    }
    if (package->flags & FLAG_SEGMENTS_4BIT) {
        uint8_t packed = pixels[pixel_index >> 1];
        unsigned value = (pixel_index & 1) ? packed & 15u : packed >> 4;
        return value * 17u;
    }
    return pixels[pixel_index];
}

static void draw_segment(GWPlaydateRenderer *renderer, uint8_t *frame,
                         unsigned segment)
{
    const GWPackage *package = renderer->package;
    const uint8_t *payload = package->payload;
    uint32_t pixel_offset = read_le32(payload + package->segment_offsets.offset + segment * 4);
    unsigned x0 = read_le16(payload + package->segment_x.offset + segment * 2);
    unsigned y0 = read_le16(payload + package->segment_y.offset + segment * 2);
    unsigned width = read_le16(payload + package->segment_width.offset + segment * 2);
    unsigned height = read_le16(payload + package->segment_height.offset + segment * 2);
    unsigned y;
    unsigned x;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            unsigned value = segment_pixel(package,
                                           pixel_offset + y * width + x);
            unsigned threshold = 224u + bayer4[((y0 + y) & 3) * 4 +
                                                ((x0 + x) & 3)] * 2u;
            int opaque = (package->flags & FLAG_RENDERING_INVERTED)
                ? value > 31u : value < threshold;
            if (opaque)
                set_black(frame, SCREEN_X + x0 + x, y0 + y);
        }
    }
}

int gw_pd_renderer_init(GWPlaydateRenderer *renderer, PlaydateAPI *pd,
                        const GWPackage *package)
{
    if (renderer == NULL || pd == NULL || package == NULL ||
        package->background.size != SOURCE_WIDTH * SOURCE_HEIGHT * 2u)
        return 0;
    memset(renderer, 0, sizeof(*renderer));
    renderer->pd = pd;
    renderer->package = package;
    return 1;
}

void gw_pd_renderer_set_segment(void *userdata, uint32_t segment, bool enabled)
{
    GWPlaydateRenderer *renderer = userdata;
    if (renderer != NULL && segment < GW_PACKAGE_SEGMENT_COUNT)
        renderer->segment_state[segment] = enabled;
}

void gw_pd_renderer_draw(GWPlaydateRenderer *renderer)
{
    uint8_t *frame;
    unsigned segment;

    if (renderer == NULL || renderer->package == NULL)
        return;
    renderer->pd->graphics->clear(kColorWhite);
    frame = renderer->pd->graphics->getFrame();
    draw_background(renderer, frame);
    for (segment = 0; segment < GW_PACKAGE_SEGMENT_COUNT; ++segment) {
        if (renderer->segment_state[segment])
            draw_segment(renderer, frame, segment);
    }
    renderer->pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}
