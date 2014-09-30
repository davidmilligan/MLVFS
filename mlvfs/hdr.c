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
#include "mlvfs.h"
#include "histogram.h"
#include "hdr.h"

#define FIXP_ONE 65536

//this is just meant to be fast
void hdr_convert_data(struct frame_headers * frame_headers, uint16_t * image_data, off_t offset, size_t max_size)
{
    uint16_t width = frame_headers->rawi_hdr.xRes;
    uint16_t height = frame_headers->rawi_hdr.yRes;
    uint16_t black = frame_headers->rawi_hdr.raw_info.black_level;
    uint16_t white = frame_headers->rawi_hdr.raw_info.white_level;
    
    //compute the median of the green channel for each multiple of 4 rows
    uint16_t median[4];
    struct histogram * hist[4];
    for(int i = 0; i < 4; i++)
        hist[i] = hist_create(white);
    
    for(uint16_t y = 4; y < height - 4; y ++)
    {
        hist_add(hist[y % 4], &(image_data[y * width + (y + 1) % 2]), width - (y + 1) % 2, 1);
    }
    
    for(int i = 0; i < 4; i++)
    {
        median[i] = hist_median(hist[i]);
        hist_destroy(hist[i]);
    }
    
    uint32_t correction = FIXP_ONE;
    uint16_t dark_row_start = -1;
    if((median[2] - black) > ((median[0] - black) * 2) &&
       (median[2] - black) > ((median[1] - black) * 2) &&
       (median[3] - black) > ((median[0] - black) * 2) &&
       (median[3] - black) > ((median[1] - black) * 2))
    {
        dark_row_start = 0;
        correction = (median[2] - black + median[3] - black) * FIXP_ONE / ((median[0] - black + median[1] - black));
    }
    else if((median[0] - black) > ((median[1] - black) * 2) &&
            (median[0] - black) > ((median[2] - black) * 2) &&
            (median[3] - black) > ((median[1] - black) * 2) &&
            (median[3] - black) > ((median[2] - black) * 2))
    {
        dark_row_start = 1;
        correction = (median[0] - black + median[3] - black) * FIXP_ONE / ((median[1] - black + median[2] - black));
    }
    else if((median[0] - black) > ((median[2] - black) * 2) &&
            (median[0] - black) > ((median[3] - black) * 2) &&
            (median[1] - black) > ((median[2] - black) * 2) &&
            (median[1] - black) > ((median[3] - black) * 2))
    {
        dark_row_start = 2;
        correction = (median[0] - black + median[1] - black) * FIXP_ONE / ((median[2] - black + median[3] - black));
    }
    else if((median[1] - black) > ((median[0] - black) * 2) &&
            (median[1] - black) > ((median[3] - black) * 2) &&
            (median[2] - black) > ((median[0] - black) * 2) &&
            (median[2] - black) > ((median[3] - black) * 2))
    {
        dark_row_start = 3;
        correction = (median[1] - black + median[2] - black) * FIXP_ONE / ((median[0] - black + median[3] - black));
    }
    uint16_t shadow = black + correction * 4 / FIXP_ONE;
    
    for(int y = 0; y < height; y++)
    {
        int row_start = y * width;
        if (((y - dark_row_start) % 4) >= 2)
        {
            //bright row
            for(int i = row_start; i < row_start + width; i++)
            {
                if(image_data[i] >= white)
                {
                    image_data[i] = y > 2 ? (y < height - 2 ? (image_data[i-width*2] + image_data[i+width*2]) / 2 : image_data[i-width*2]) : image_data[i+width*2];
                }
                else
                {
                    image_data[i] = MIN(white,((image_data[i] - black) * FIXP_ONE / correction) + black);
                }
            }
        }
        else
        {
            //dark row
            for(int i = row_start; i < row_start + width; i++)
            {
                if(image_data[i] < shadow)
                {
                    image_data[i] = y > 2 ? (y < height - 2 ? (image_data[i-width*2] + MIN(white,((image_data[i+width*2] - black) * FIXP_ONE / correction) + black)) / 2 : image_data[i-width*2]) : MIN(white,((image_data[i+width*2] - black) * FIXP_ONE / correction) + black);
                }
                
            }
        }
    }
}