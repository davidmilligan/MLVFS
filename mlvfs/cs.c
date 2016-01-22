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
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "raw.h"
#include "mlv.h"
#include "dng.h"
#include "mlvfs.h"
#include "opt_med.h"
#include "wirth.h"
#include "cs.h"

#define EV_RESOLUTION 32768
#define MAX_BLACK 16384

double * get_raw2evf(int black)
{
    static int initialized = 0;
    static double raw2ev_base[16384 + MAX_BLACK];
    
    LOCK(ev2raw_mutex)
    {
        if(!initialized)
        {
            memset(raw2ev_base, 0, MAX_BLACK * sizeof(int));
            int i;
            for (i = 0; i < 16384; i++)
            {
                raw2ev_base[i + MAX_BLACK] = log2(i) * EV_RESOLUTION;
            }
            initialized = 1;
        }
    }
    UNLOCK(ev2raw_mutex)
    
    if(black > MAX_BLACK)
    {
        fprintf(stderr, "Black level too large for processing\n");
        return NULL;
    }
    double * raw2ev = &(raw2ev_base[MAX_BLACK - black]);
    
    return raw2ev;
}

int * get_raw2ev(int black)
{
    
    static int initialized = 0;
    static int raw2ev_base[16384 + MAX_BLACK];
    
    LOCK(ev2raw_mutex)
    {
        if(!initialized)
        {
            memset(raw2ev_base, 0, MAX_BLACK * sizeof(int));
            int i;
            for (i = 0; i < 16384; i++)
            {
                raw2ev_base[i + MAX_BLACK] = (int)(log2(i) * EV_RESOLUTION);
            }
            initialized = 1;
        }
    }
    UNLOCK(ev2raw_mutex)
    
    if(black > MAX_BLACK)
    {
        fprintf(stderr, "Black level too large for processing\n");
        return NULL;
    }
    int * raw2ev = &(raw2ev_base[MAX_BLACK - black]);
    
    return raw2ev;
}

int * get_ev2raw()
{
    static int initialized = 0;
    static int _ev2raw[24*EV_RESOLUTION];
    int* ev2raw = _ev2raw + 10*EV_RESOLUTION;
    
    LOCK(ev2raw_mutex)
    {
        if(!initialized)
        {
            int i;
            for (i = -10*EV_RESOLUTION; i < 14*EV_RESOLUTION; i++)
            {
                ev2raw[i] = (int)(pow(2, (float)i / EV_RESOLUTION));
            }
            initialized = 1;
        }
    }
    UNLOCK(ev2raw_mutex)
    
    return ev2raw;
}

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
    int w = frame_headers->rawi_hdr.xRes;
    int h = frame_headers->rawi_hdr.yRes;
    int black = frame_headers->rawi_hdr.raw_info.black_level;
    
    int * raw2ev = get_raw2ev(black);
    int * ev2raw = get_ev2raw();
    
    if(raw2ev == NULL) return;
    
    uint16_t * buf = (uint16_t *)malloc(w*h*sizeof(uint16_t));
    memcpy(buf, image_data, w*h*sizeof(uint16_t));
    
    switch (method) {
        case 2:
            chroma_smooth_2x2(frame_headers->rawi_hdr.xRes, frame_headers->rawi_hdr.yRes, buf, image_data, raw2ev, ev2raw, black);
            break;
        case 3:
            chroma_smooth_3x3(frame_headers->rawi_hdr.xRes, frame_headers->rawi_hdr.yRes, buf, image_data, raw2ev, ev2raw, black);
            break;
        case 5:
            chroma_smooth_5x5(frame_headers->rawi_hdr.xRes, frame_headers->rawi_hdr.yRes, buf, image_data, raw2ev, ev2raw, black);
            break;
            
        default:
            fprintf(stderr, "Unsupported chroma smooth method\n");
            break;
    }
    
    free(buf);
}

//adapted from cr2hdr and optimized for performance
void fix_bad_pixels(struct frame_headers * frame_headers, uint16_t * image_data, int aggressive)
{
    int w = frame_headers->rawi_hdr.xRes;
    int h = frame_headers->rawi_hdr.yRes;
    int black = frame_headers->rawi_hdr.raw_info.black_level;
    
    int * raw2ev = get_raw2ev(black);
    
    if(raw2ev == NULL) return;
    
    //just guess the dark noise for speed reasons
    int dark_noise = 12 ;
    int dark_min = black - (dark_noise * 8);
    int dark_max = black + (dark_noise * 8);
    int x,y;
    for (y = 6; y < h - 6; y ++)
    {
        for (x = 6; x < w - 6; x ++)
        {
            int p = image_data[x + y * w];
            
            int neighbours[10];
            int max1 = 0;
            int max2 = 0;
            int k = 0;
            for (int i = -2; i <= 2; i+=2)
            {
                for (int j = -2; j <= 2; j+=2)
                {
                    if (i == 0 && j == 0)
                        continue;
                    int q = -(int)image_data[(x + j) + (y + i) * w];
                    neighbours[k++] = q;
                    if(q <= max1)
                    {
                        max2 = max1;
                        max1 = q;
                    }
                    else if(q <= max2)
                    {
                        max2 = q;
                    }
                }
            }
            
            if (p < dark_min) //cold pixel
            {
                image_data[x + y * w] = -median_int_wirth(neighbours, k);
            }
            else if ((raw2ev[p] - raw2ev[-max2] > 2 * EV_RESOLUTION) && (p > dark_max)) //hot pixel
            {
                image_data[x + y * w] = -kth_smallest_int(neighbours, k, 2);
            }
            else if (aggressive)
            {
                int max3 = kth_smallest_int(neighbours, k, 2);
                if(((raw2ev[p] - raw2ev[-max2] > EV_RESOLUTION) || (raw2ev[p] - raw2ev[-max3] > EV_RESOLUTION)) && (p > dark_max))
                {
                    image_data[x + y * w] = -max3;
                }
            }
        }
    }
}

struct focus_pixel
{
    int x;
    int y;
};

struct focus_pixel_map
{
    uint32_t camera;
    int rawi_width;
    int rawi_height;
    size_t count;
    size_t capacity;
    struct focus_pixel * pixels;
};

static int focus_pixel_map_count = 0;
static struct focus_pixel_map * focus_pixel_maps = NULL;

static int add_focus_pixel(struct focus_pixel_map * map, int x, int y)
{
    if(map->count >= map->capacity)
    {
        map->capacity *= 2;
        map->pixels = realloc(map->pixels, sizeof(struct focus_pixel) * map->capacity);
        if(!map->pixels)
        {
            fprintf(stderr, "malloc error\n");
            map->count = 0;
            return 0;
        }
    }
    map->pixels[map->count].x = x;
    map->pixels[map->count].y = y;
    map->count++;
    return 1;
}

static struct focus_pixel_map * load_focus_pixel_map(uint32_t camera_id, int width, int height)
{
    focus_pixel_map_count++;
    focus_pixel_maps = realloc(focus_pixel_maps, sizeof(struct focus_pixel_map) * focus_pixel_map_count);
    if(focus_pixel_maps)
    {
        struct focus_pixel_map * map = &(focus_pixel_maps[focus_pixel_map_count - 1]);
        map->camera = camera_id;
        map->rawi_width = width;
        map->rawi_height = height;
        map->count = 0;
        map->capacity = 0;
        map->pixels = NULL;
        char filename[1024];
        sprintf(filename, "%x_%ix%i.fpm", camera_id, width, height);
        FILE* f = fopen(filename, "r+");
        if(f)
        {
            printf("Loading focus pixel map '%s'...\n", filename);
            map->capacity = 32;
            map->pixels = malloc(sizeof(struct focus_pixel) * map->capacity);
            int x = 0;
            int y = 0;
            int ret = 2;
            while(ret != EOF)
            {
                ret = fscanf(f, "%i %i", &x, &y);
                if(ret == 2)
                {
                    if(!add_focus_pixel(map, x, y)) break;
                }
                else if(ferror(f))
                {
                    int err = errno;
                    fprintf(stderr, "load_focus_pixel_maps: file error: %s\n", strerror(err));
                    break;
                }
            }
            return map;
        }
    }
    else
    {
        fprintf(stderr, "malloc error\n");
        focus_pixel_map_count = 0;
    }
    return NULL;
}

void free_focus_pixel_maps()
{
    if(focus_pixel_maps)
    {
        for(size_t i = 0; i < focus_pixel_map_count; i++)
        {
            free(focus_pixel_maps[i].pixels);
        }
        free(focus_pixel_maps);
        focus_pixel_maps = NULL;
    }
}

static inline void interpolate_pixel(uint16_t * image_data, int x, int y, int w)
{
    //simply average the two horizontally neighboring pixels of the same color
    //simple and fast, do we need something better?
    //horizontal direction only b/c could be dual ISO
    image_data[x + y*w] = (image_data[x - 2 + y*w] >> 1) + (image_data[x + 2 + y*w] >> 1);
}

static struct focus_pixel_map * get_focus_pixel_map(struct frame_headers * frame_headers)
{
    uint32_t camera_id = frame_headers->idnt_hdr.cameraModel;
    int rawi_width = frame_headers->rawi_hdr.raw_info.width;
    int rawi_height = frame_headers->rawi_hdr.raw_info.height;
    if(focus_pixel_maps)
    {
        for(size_t i = 0; i < focus_pixel_map_count; i++)
        {
            struct focus_pixel_map * current = &(focus_pixel_maps[i]);
            if(current->camera == camera_id && current->rawi_width == rawi_width && current->rawi_height == rawi_height)
            {
                return current->count > 0 ? current : NULL;
            }
        }
    }
    return load_focus_pixel_map(camera_id, rawi_width, rawi_height);
}

void fix_focus_pixels(struct frame_headers * frame_headers, uint16_t * image_data)
{
    struct focus_pixel_map * map = get_focus_pixel_map(frame_headers);
    
    if (map)
    {
        int w = frame_headers->rawi_hdr.xRes;
        int h = frame_headers->rawi_hdr.yRes;
        //there was a bug with cropPosX, so we'll round panPosX ouselves
        int cropX = (frame_headers->vidf_hdr.panPosX + 7) & ~7;
        int cropY = frame_headers->vidf_hdr.panPosY & ~1;
        
        for (int i = 0; i < map->count; i++)
        {
            int x = map->pixels[i].x - cropX;
            int y = map->pixels[i].y - cropY;
            if (x > 1 && y > 1 && x < w - 1 && y < h - 1)
            {
                interpolate_pixel(image_data, x, y, w);
            }
        }
    }
}
