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

#include <stdlib.h>
#include <math.h>
#include "raw.h"
#include "mlv.h"
#include "mlvfs.h"
#include "histogram.h"
#include "hdr.h"

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
    struct histogram * hist_hi = NULL;
    struct histogram * hist_lo = NULL;
    
    for(int i = 0; i < 4; i++)
        hist[i] = hist_create(white);
    
    for(uint16_t y = 4; y < height - 4; y += 5)
    {
        hist_add(hist[y % 4], &(image_data[y * width + (y + 1) % 2]), width - (y + 1) % 2, 3);
    }
    
    for(int i = 0; i < 4; i++)
    {
        median[i] = hist_median(hist[i]);
    }
    
    uint16_t dark_row_start = -1;
    if((median[2] - black) > ((median[0] - black) * 2) &&
       (median[2] - black) > ((median[1] - black) * 2) &&
       (median[3] - black) > ((median[0] - black) * 2) &&
       (median[3] - black) > ((median[1] - black) * 2))
    {
        dark_row_start = 0;
        hist_lo = hist[0];
        hist_hi = hist[2];
    }
    else if((median[0] - black) > ((median[1] - black) * 2) &&
            (median[0] - black) > ((median[2] - black) * 2) &&
            (median[3] - black) > ((median[1] - black) * 2) &&
            (median[3] - black) > ((median[2] - black) * 2))
    {
        dark_row_start = 1;
        hist_lo = hist[1];
        hist_hi = hist[0];
    }
    else if((median[0] - black) > ((median[2] - black) * 2) &&
            (median[0] - black) > ((median[3] - black) * 2) &&
            (median[1] - black) > ((median[2] - black) * 2) &&
            (median[1] - black) > ((median[3] - black) * 2))
    {
        dark_row_start = 2;
        hist_lo = hist[2];
        hist_hi = hist[0];
    }
    else if((median[1] - black) > ((median[0] - black) * 2) &&
            (median[1] - black) > ((median[3] - black) * 2) &&
            (median[2] - black) > ((median[0] - black) * 2) &&
            (median[2] - black) > ((median[3] - black) * 2))
    {
        dark_row_start = 3;
        hist_lo = hist[0];
        hist_hi = hist[2];
    }
    else
    {
        fprintf(stderr, "Could not detect dual ISO interlaced lines\n");
        return;
    }
    
    /* compare the two histograms and plot the curve between the two exposures (dark as a function of bright) */
    const int min_pix = 100;                                /* extract a data point every N image pixels */
    int data_size = (width * height / min_pix + 1);                  /* max number of data points */
    int* data_x = malloc(data_size * sizeof(data_x[0]));
    int* data_y = malloc(data_size * sizeof(data_y[0]));
    double* data_w = malloc(data_size * sizeof(data_w[0]));
    int data_num = 0;
    
    int acc_lo = 0;
    int acc_hi = 0;
    int raw_lo = 0;
    int raw_hi = 0;
    int prev_acc_hi = 0;
    
    int hist_total = hist[0]->count;
    
    for (raw_hi = 0; raw_hi < hist_total; raw_hi++)
    {
        acc_hi += hist_hi->data[raw_hi];
        
        while (acc_lo < acc_hi)
        {
            acc_lo += hist_lo->data[raw_lo];
            raw_lo++;
        }
        
        if (raw_lo >= white)
            break;
        
        if (acc_hi - prev_acc_hi > min_pix)
        {
            if (acc_hi > hist_total * 1 / 100 && acc_hi < hist_total * 99.99 / 100)    /* throw away outliers */
            {
                data_x[data_num] = raw_hi - black;
                data_y[data_num] = raw_lo - black;
                data_w[data_num] = (MAX(0, raw_hi - black + 100));    /* points from higher brightness are cleaner */
                data_num++;
                prev_acc_hi = acc_hi;
            }
        }
    }
    
    /**
     * plain least squares
     * y = ax + b
     * a = (mean(xy) - mean(x)mean(y)) / (mean(x^2) - mean(x)^2)
     * b = mean(y) - a mean(x)
     */
    
    double mx = 0, my = 0, mxy = 0, mx2 = 0;
    double weight = 0;
    for (int i = 0; i < data_num; i++)
    {
        mx += data_x[i] * data_w[i];
        my += data_y[i] * data_w[i];
        mxy += (double)data_x[i] * data_y[i] * data_w[i];
        mx2 += (double)data_x[i] * data_x[i] * data_w[i];
        weight += data_w[i];
    }
    mx /= weight;
    my /= weight;
    mxy /= weight;
    mx2 /= weight;
    double a = (mxy - mx*my) / (mx2 - mx*mx);
    double b = my - a * mx;
    
    for(int i = 0; i < 4; i++)
    {
        hist_destroy(hist[i]);
    }
    
    //TODO: what's a better way to pick a value for this?
    uint16_t shadow = black + 1 / (a * a) + b;
    
    for(int y = 0; y < height; y++)
    {
        int row_start = y * width;
        if (((y - dark_row_start + 4) % 4) >= 2)
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
                    image_data[i] = MIN(white,(image_data[i] - black) * a + black + b);
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
                    image_data[i] = y > 2 ? (y < height - 2 ? (image_data[i-width*2] + MIN(white,(image_data[i+width*2]  - black) * a + black + b)) / 2 : image_data[i-width*2]) : MIN(white,(image_data[i+width*2]  - black) * a + black + b);
                }
                
            }
        }
    }
}