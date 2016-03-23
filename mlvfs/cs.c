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
    if (!buf)
    {
        return;
    }
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
            err_printf("Unsupported chroma smooth method\n");
            break;
    }
    
    free(buf);
}


static inline void interpolate_horizontal(uint16_t * image_data, int i, int * raw2ev, int * ev2raw, int black)
{
    int gh1 = image_data[i + 3];
    int gh2 = image_data[i + 1];
    int gh3 = image_data[i - 1];
    int gh4 = image_data[i - 3];
    int dh1 = ABS(raw2ev[gh1] - raw2ev[gh2]);
    int dh2 = ABS(raw2ev[gh3] - raw2ev[gh4]);
    int sum = dh1 + dh2;
    if (sum == 0)
    {
        image_data[i] = image_data[i + 2];
    }
    else
    {
        int ch1 = ((sum - dh1) << 8) / sum;
        int ch2 = ((sum - dh2) << 8) / sum;
        
        int ev_corr = ((raw2ev[image_data[i + 2]] * ch1) >> 8) + ((raw2ev[image_data[i - 2]] * ch2) >> 8);
        image_data[i] = ev2raw[COERCE(ev_corr, 0, 14*EV_RESOLUTION-1)] + black;
    }
}

static inline void interpolate_vertical(uint16_t * image_data, int i, int w, int * raw2ev, int * ev2raw, int black)
{
    int gv1 = image_data[i + w * 3];
    int gv2 = image_data[i + w];
    int gv3 = image_data[i - w];
    int gv4 = image_data[i - w * 3];
    int dv1 = ABS(raw2ev[gv1] - raw2ev[gv2]);
    int dv2 = ABS(raw2ev[gv3] - raw2ev[gv4]);
    int sum = dv1 + dv2;
    if (sum == 0)
    {
        image_data[i] = image_data[i + w * 2];
    }
    else
    {
        int cv1 = ((sum - dv1) << 8) / sum;
        int cv2 = ((sum - dv2) << 8) / sum;
        
        int ev_corr = ((raw2ev[image_data[i + w * 2]] * cv1) >> 8) + ((raw2ev[image_data[i - w * 2]] * cv2) >> 8);
        image_data[i] = ev2raw[COERCE(ev_corr, 0, 14*EV_RESOLUTION-1)] + black;
    }
}

static inline void interpolate_pixel(uint16_t * image_data, int i, int w, int * raw2ev, int * ev2raw, int black)
{
    int gv1 = image_data[i + w * 3];
    int gv2 = image_data[i + w];
    int gv3 = image_data[i - w];
    int gv4 = image_data[i - w * 3];
    int gh1 = image_data[i + 3];
    int gh2 = image_data[i + 1];
    int gh3 = image_data[i - 1];
    int gh4 = image_data[i - 3];
    int dv1 = ABS(raw2ev[gv1] - raw2ev[gv2]);
    int dv2 = ABS(raw2ev[gv3] - raw2ev[gv4]);
    int dh1 = ABS(raw2ev[gh1] - raw2ev[gh2]);
    int dh2 = ABS(raw2ev[gh3] - raw2ev[gh4]);
    int sum = dh1 + dh2 + dv1 + dv2;
    
    if (sum == 0)
    {
        image_data[i] = image_data[i + 2];
    }
    else
    {
        int cv1 = ((sum - dv1) << 8) / (3 * sum);
        int cv2 = ((sum - dv2) << 8) / (3 * sum);
        int ch1 = ((sum - dh1) << 8) / (3 * sum);
        int ch2 = ((sum - dh2) << 8) / (3 * sum);
        
        int ev_corr =
        ((raw2ev[image_data[i + w * 2]] * cv1) >> 8) +
        ((raw2ev[image_data[i - w * 2]] * cv2) >> 8) +
        ((raw2ev[image_data[i + 2]] * ch1) >> 8) +
        ((raw2ev[image_data[i - 2]] * ch2) >> 8);
        
        image_data[i] = ev2raw[COERCE(ev_corr, 0, 14*EV_RESOLUTION-1)] + black;
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

struct bad_pixel_map
{
    uint64_t file_guid;
    int aggressive;
    size_t count;
    size_t capacity;
    struct focus_pixel * pixels;
};

static int add_bad_pixel(struct bad_pixel_map * map, int x, int y)
{
    if(map->count >= map->capacity)
    {
        map->capacity *= 2;
        map->pixels = realloc(map->pixels, sizeof(struct focus_pixel) * map->capacity);
        if(!map->pixels)
        {
            err_printf("malloc error\n");
            map->count = 0;
            return 0;
        }
    }
    map->pixels[map->count].x = x;
    map->pixels[map->count].y = y;
    map->count++;
    return 1;
}


#define BAD_PIXEL_MAP_COUNT 8
static struct bad_pixel_map bad_pixel_maps[BAD_PIXEL_MAP_COUNT] = { 0 };
static int current_bad_pixel_map = 0;

//adapted from cr2hdr and optimized for performance
void fix_bad_pixels(struct frame_headers * frame_headers, uint16_t * image_data, int aggressive, int dual_iso)
{
    int w = frame_headers->rawi_hdr.xRes;
    int h = frame_headers->rawi_hdr.yRes;
    int black = frame_headers->rawi_hdr.raw_info.black_level;
    int cropX = (frame_headers->vidf_hdr.panPosX + 7) & ~7;
    int cropY = frame_headers->vidf_hdr.panPosY & ~1;
    
    int * raw2ev = get_raw2ev(black);
    int * ev2raw = get_ev2raw();
    
    if(raw2ev == NULL) return;
    
    struct bad_pixel_map * map = NULL;
    for(int i = 0; i < BAD_PIXEL_MAP_COUNT; i++)
    {
        if(frame_headers->file_hdr.fileGuid && frame_headers->file_hdr.fileGuid == bad_pixel_maps[i].file_guid && aggressive == bad_pixel_maps[i].aggressive)
        {
            map = &(bad_pixel_maps[i]);
        }
    }
    if(!map)
    {
        map = &(bad_pixel_maps[current_bad_pixel_map]);
        current_bad_pixel_map = (current_bad_pixel_map + 1) % BAD_PIXEL_MAP_COUNT;
        if (map->pixels)
        {
            free(map->pixels);
        }
        map->file_guid = frame_headers->file_hdr.fileGuid;
        map->aggressive = aggressive;
        map->count = 0;
        map->capacity = 32;
        map->pixels = malloc(sizeof(struct focus_pixel) * map->capacity);
        
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
                        if (i == 0 && j == 0) continue;
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
                    add_bad_pixel(map, x + cropX, y + cropY);
                }
                else if ((raw2ev[p] - raw2ev[-max2] > 2 * EV_RESOLUTION) && (p > dark_max)) //hot pixel
                {
                    add_bad_pixel(map, x + cropX, y + cropY);
                }
                else if (aggressive)
                {
                    int max3 = kth_smallest_int(neighbours, k, 2);
                    if(((raw2ev[p] - raw2ev[-max2] > EV_RESOLUTION) || (raw2ev[p] - raw2ev[-max3] > EV_RESOLUTION)) && (p > dark_max))
                    {
                        add_bad_pixel(map, x + cropX, y + cropY);
                    }
                }
            }
        }
        printf("%zu bad pixels found for %llx (crop: %d, %d):\n", map->count, map->file_guid, cropX, cropY);
        for (int m = 0; m < map->count; m++)
        {
            printf("%d %d\n", map->pixels[m].x, map->pixels[m].y);
        }
    }
    
    for (int m = 0; m < map->count; m++)
    {
        int x = map->pixels[m].x - cropX;
        int y = map->pixels[m].y - cropY;
        int i = x + y*w;
        if (x > 2 && x < w - 3 && y > 2 && y < h - 3)
        {
            if (dual_iso)
            {
                interpolate_horizontal(image_data, i, raw2ev, ev2raw, black);
            }
            else
            {
                interpolate_pixel(image_data, i, w, raw2ev, ev2raw, black);
            }
        }
    }
}

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
            err_printf("malloc error\n");
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
                    err_printf("file error: %s\n", strerror(err));
                    break;
                }
            }
            return map;
        }
    }
    else
    {
        err_printf("malloc error\n");
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
    for(size_t i = 0; i < BAD_PIXEL_MAP_COUNT; i++)
    {
        if(bad_pixel_maps[i].pixels) free(bad_pixel_maps[i].pixels);
    }
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

void fix_focus_pixels(struct frame_headers * frame_headers, uint16_t * image_data, int dual_iso)
{
    struct focus_pixel_map * map = get_focus_pixel_map(frame_headers);
    
    if (map)
    {
        int w = frame_headers->rawi_hdr.xRes;
        int h = frame_headers->rawi_hdr.yRes;
        //there was a bug with cropPosX, so we'll round panPosX ouselves
        int cropX = (frame_headers->vidf_hdr.panPosX + 7) & ~7;
        int cropY = frame_headers->vidf_hdr.panPosY & ~1;
        
        int black = frame_headers->rawi_hdr.raw_info.black_level;
        int * raw2ev = get_raw2ev(black);
        int * ev2raw = get_ev2raw();
        
        if(raw2ev == NULL)
        {
            err_printf("raw2ev LUT error\n");
            return;
        }
        
        for (int m = 0; m < map->count; m++)
        {
            int x = map->pixels[m].x - cropX;
            int y = map->pixels[m].y - cropY;
            
            int i = x + y*w;
            if (x > 2 && x < w - 3 && y > 2 && y < h - 3)
            {
                if (dual_iso)
                {
                    interpolate_horizontal(image_data, i, raw2ev, ev2raw, black);
                }
                else
                {
                    interpolate_pixel(image_data, i, w, raw2ev, ev2raw, black);
                }
            }
            else if(i > 0 && i < w * h)
            {
                int horizontal_edge = (x >= w - 3 && x < w) || (x >= 0 && x <= 3);
                int vertical_edge = (y >= h - 3 && y < h) || (y >= 0 && y <= 3);
                //handle edge pixels
                if (horizontal_edge && !vertical_edge && !dual_iso)
                {
                    interpolate_vertical(image_data, i, w, raw2ev, ev2raw, black);
                }
                else if (vertical_edge && !horizontal_edge)
                {
                    interpolate_horizontal(image_data, i, raw2ev, ev2raw, black);
                }
                else if(x >= 0 && x <= 3)
                {
                    image_data[i] = image_data[i + 2];
                }
                else if(x >= w - 3 && x < w)
                {
                    image_data[i] = image_data[i - 2];
                }
            }
        }
    }
}
