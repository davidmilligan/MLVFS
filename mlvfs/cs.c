/*
 * Copyright (C) 2014 The Magic Lantern Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "raw.h"
#include "mlv.h"
#include "dng.h"
#include "mlvfs.h"
#include "opt_med.h"
#include "cs.h"

#define EV_RESOLUTION 32768

#define CHROMA_SMOOTH_2X2
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_2X2

#define CHROMA_SMOOTH_3X3
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_3X3

#define CHROMA_SMOOTH_5X5
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_5X5

void chroma_smooth(struct frame_headers * frame_headers, uint16_t * image_data, int method)
{
    static int black = -1;
    static int raw2ev[16384];
    static int _ev2raw[24*EV_RESOLUTION];
    int* ev2raw = _ev2raw + 10*EV_RESOLUTION;
    
    LOCK(chroma_smooth_mutex)
    {
        if(black != frame_headers->rawi_hdr.raw_info.black_level)
        {
            black = frame_headers->rawi_hdr.raw_info.black_level;
            int i;
            for (i = 0; i < 16384; i++)
            {
                raw2ev[i] = (int)(log2(MAX(1, i - black)) * EV_RESOLUTION);
            }
            
            for (i = -10*EV_RESOLUTION; i < 14*EV_RESOLUTION; i++)
            {
                ev2raw[i] = (int)(black + pow(2, (float)i / EV_RESOLUTION));
            }
        }
        
        int w = frame_headers->rawi_hdr.xRes;
        int h = frame_headers->rawi_hdr.yRes;
        
        uint16_t * buf = (uint16_t *)malloc(w*h*sizeof(uint16_t));
        memcpy(buf, image_data, w*h*sizeof(uint16_t));
        
        switch (method) {
            case 2:
                chroma_smooth_2x2(frame_headers->rawi_hdr.xRes, frame_headers->rawi_hdr.yRes, buf, image_data, raw2ev, ev2raw);
                break;
            case 3:
                chroma_smooth_3x3(frame_headers->rawi_hdr.xRes, frame_headers->rawi_hdr.yRes, buf, image_data, raw2ev, ev2raw);
                break;
            case 5:
                chroma_smooth_5x5(frame_headers->rawi_hdr.xRes, frame_headers->rawi_hdr.yRes, buf, image_data, raw2ev, ev2raw);
                break;
                
            default:
                fprintf(stderr, "Unsupported chroma smooth method\n");
                break;
        }
        
        free(buf);
        
    }
    UNLOCK(chroma_smooth_mutex)
}
