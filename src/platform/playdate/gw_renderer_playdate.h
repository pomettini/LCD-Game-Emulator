#ifndef GW_RENDERER_PLAYDATE_H
#define GW_RENDERER_PLAYDATE_H

#include "gw_package.h"
#include "pd_api.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct GWPlaydateRenderer {
    PlaydateAPI *pd;
    const GWPackage *package;
    bool segment_state[GW_PACKAGE_SEGMENT_COUNT];
} GWPlaydateRenderer;

int gw_pd_renderer_init(GWPlaydateRenderer *renderer, PlaydateAPI *pd,
                        const GWPackage *package);
void gw_pd_renderer_set_segment(void *userdata, uint32_t segment, bool enabled);
void gw_pd_renderer_draw(GWPlaydateRenderer *renderer);

#endif
