/*
 * Copyright (C) 2014 David Milligan
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

#include <math.h>
#include "raw.h"
#include "mlv.h"
#include "hdr.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define SCALE_PIXEL(data, index, ev, black) (ev >= 0 ? (((data[index] - black) << ev) + black) : (((data[index] - black) >> -ev) + black))

#define INTERPOLATE_PIXEL(data, index, width, ev, black, count) {\
if(index + width * 2 < count){\
    if(index >= width * 2) \
        data[index] = (uint16_t)(((uint32_t)SCALE_PIXEL(data, index + width * 2, ev, black) + (uint32_t)data[index - width * 2]) / 2);\
    else \
        data[index] = SCALE_PIXEL(data, index + width * 2, ev, black);\
}\
else if(index >= width * 2) data[index] = data[index - width * 2]; }\

//this is just meant to be fast
void hdr_convert_data(struct frame_headers * frame_headers, uint16_t * image_data, off_t offset, size_t max_size)
{
    uint64_t pixel_start_index = MAX(0, offset) / 2;
    size_t output_size = max_size - (offset < 0 ? (size_t)(-offset) : 0);
    uint64_t pixel_count = output_size / 2;
    
    //TODO: autodetect this stuff -> compute and compare medians of the rows like cr2hdr
    uint16_t iso1 = 100;
    uint16_t iso2 = 1600;
    //it appears that this can change from frame to frame, so will have to figure it out for each frame (not just each sequence)
    uint16_t dark_row_start = 3;
    
    int16_t iso_difference_ev = (int16_t)log2((MAX(iso1, iso2) / MIN(iso1, iso2)));
    
    //we have 2 extra bits to spare, so scale the lower ISO up by max of 2 bits, and scale the high ISO down by the remainder
    int16_t iso1_ev = iso1 < iso2 ? MIN(2, iso_difference_ev) : MIN(0, 2 - iso_difference_ev);
    int16_t iso2_ev = iso2 < iso1 ? MIN(2, iso_difference_ev) : MIN(0, 2 - iso_difference_ev);
    uint16_t width = frame_headers->rawi_hdr.xRes;
    uint16_t black = frame_headers->rawi_hdr.raw_info.black_level;
    uint16_t white = frame_headers->rawi_hdr.raw_info.white_level;
    uint16_t shadow = (1 << (iso_difference_ev * 2)) + black;
    
    for(size_t pixel_index = 0; pixel_index < pixel_count; pixel_index++)
    {
        uint16_t y = (pixel_index + pixel_start_index) / width + dark_row_start;
        if (y % 4 < 2)
        {
            if(image_data[pixel_index] <= shadow)
            {
                INTERPOLATE_PIXEL(image_data, pixel_index, width, iso2_ev, black, pixel_count);
            }
            else
            {
                image_data[pixel_index] = SCALE_PIXEL(image_data, pixel_index, iso1_ev, black);
            }
        }
        else
        {
            if(image_data[pixel_index] >= white)
            {
                INTERPOLATE_PIXEL(image_data, pixel_index, width, iso1_ev, black, pixel_count);
            }
            else
            {
                image_data[pixel_index] = SCALE_PIXEL(image_data, pixel_index, iso2_ev, black);
            }
        }
    }
}