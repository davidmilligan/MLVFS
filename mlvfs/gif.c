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

#include "gif.h"
#include "index.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define IMAGE_SEPARATOR 0x2C
#define BPP 7
#define COLOR_TABLE_SIZE ((1 << BPP) * 3)
#define LZW_MIN_CODE_SIZE BPP
#define LZW_CC (1 << BPP)
#define LZW_EOI ((1 << BPP) + 1)
#define GIF_EOF 0x3B
#define SUB_BLOCK_SIZE ((1 << BPP) - 2)
#define FRAME_COUNT 10
#define DOWNSCALE 4

#define memwrite(buffer, data, position, length) memcpy(buffer + position, data, length); position += length;
#define memwritebyte(buffer, value, position) *(buffer + position) = value; position++;

#pragma pack(push,1)

struct gif_header
{
    //File Header
    char GIF[3];
    char version[3];
    //Logical Screen Descriptor
    uint16_t width;
    uint16_t height;
    uint8_t packed;
    uint8_t background_color_index;
    uint8_t aspect_ratio;
    //Global Color Table
    uint8_t color_table[COLOR_TABLE_SIZE];
};

struct gif_image_descriptor
{
    uint8_t image_separator; //0x2C
    uint16_t left;
    uint16_t top;
    uint16_t width;
    uint16_t height;
    uint8_t packed; //0x00
    uint8_t lzw_min_code_size; //technically part of the 'image data block', always 0x08 for our purposes
};

struct gif_image_data_sub_block
{
    uint8_t image_data_size;
    uint8_t image_data[SUB_BLOCK_SIZE];
};

#pragma pack(pop)

static uint8_t gif_animation_application_block[] = {0x21, 0xFF, 0x0B, 0x4E, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00};
static uint8_t gif_animation_graphics_block[] = {0x21, 0xF9, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00}; //0.5 sec between frames

size_t gif_get_data(const char * path, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    struct frame_headers frame_headers;
    if(mlv_get_frame_headers(path, 0, &frame_headers))
    {
        int frame_count = mlv_get_frame_count(path);
        FILE **chunk_files = NULL;
        uint32_t chunk_count = 0;
        chunk_files = load_chunks(path, &chunk_count);
        if(!chunk_files || !chunk_count)
        {
            return 0;
        }
        
        uint16_t width = frame_headers.rawi_hdr.xRes / DOWNSCALE;
        uint16_t height = frame_headers.rawi_hdr.yRes / DOWNSCALE;
        uint16_t black_level = frame_headers.rawi_hdr.raw_info.black_level;
        size_t gif_size = gif_get_size(&frame_headers);
        size_t image_data_size = frame_headers.rawi_hdr.xRes * frame_headers.rawi_hdr.yRes * 2;
        uint8_t gamma[1024];
        
        for (int i = 0; i < 1024; i++)
        {
            int g = (i > (black_level>>4)) ? log2f(i - (black_level>>4)) * 255 / 10 : 0;
            gamma[i] = g * g / 255 / 2;
        }
        
        struct gif_header header =
        {
            .GIF = "GIF",
            .version = "89a",
            .width = width,
            .height = height,
            .packed = 0xF6,
            .background_color_index = 0,
            .aspect_ratio = 0,
        };
        //generate the color table, for simplicy we just use a simple greyscale palatte
        //127 colors so that 1 pixel = 1 byte (there's an extra bit for special LZW codes, so 7 bits of color allowed)
        int i = 0;
        uint8_t color = 0;
        while(i <= COLOR_TABLE_SIZE - 3)
        {
            header.color_table[i++] = color;
            header.color_table[i++] = color;
            header.color_table[i++] = color;
            color += 2;
        }
        struct gif_image_descriptor image_descriptor =
        {
            .image_separator = IMAGE_SEPARATOR,
            .left = 0,
            .top = 0,
            .width = width,
            .height = height,
            .packed = 0x00,
            .lzw_min_code_size = LZW_MIN_CODE_SIZE
        };
        
        uint8_t* gif_buffer = malloc(gif_size);
        uint32_t position = 0;
        if(gif_buffer)
        {
			uint16_t* image_data = malloc(image_data_size);
			if (!image_data)
			{
				free(gif_buffer);
				return 0;
			}

            //file headers
            memwrite(gif_buffer, &header, position, sizeof(struct gif_header));
            memwrite(gif_buffer, gif_animation_application_block, position, sizeof(gif_animation_application_block));
            for(int gif_frame = 0; gif_frame < FRAME_COUNT; gif_frame++)
            {
                int mlv_frame_number = gif_frame * frame_count / FRAME_COUNT;
                
                if(!mlv_get_frame_headers(path, mlv_frame_number, &frame_headers))
                {
                    fprintf(stderr, "GIF Error: could not get MLV frame headers\n");
                    continue;
                }
                get_image_data(&frame_headers, chunk_files[frame_headers.fileNumber], (uint8_t*) image_data, 0, image_data_size);
                
                //image headers
                memwrite(gif_buffer, gif_animation_graphics_block, position, sizeof(gif_animation_graphics_block));
                memwrite(gif_buffer, &image_descriptor, position, sizeof(struct gif_image_descriptor));
                
                //encode image data
                
                //we skip the LZW compression, and essential have just "uncompressed GIF", all we have to do to achive this
                //is to make sure to emit a CC (Clear Code) every 2^n-2 bytes to keep the code size from increasing
                //http://en.wikipedia.org/wiki/GIF#Uncompressed_GIF
                //since we have to put out a CC here anyway, we just make the sub blocks this size too
                //(we could make them up to 255 in size, but that makes the code more complicated)
                
                uint32_t sub_block_position = 0;
                struct gif_image_data_sub_block current_sub_block;
                memset(&current_sub_block, 0, sizeof(struct gif_image_data_sub_block));
                current_sub_block.image_data[0] = LZW_CC;
                for(int y = 0; y < height; y++)
                {
                    for(int x = 0; x < width; x++)
                    {
                        sub_block_position++;
                        current_sub_block.image_data[sub_block_position] = gamma[image_data[y * DOWNSCALE * width * DOWNSCALE + x * DOWNSCALE + 1]>>4];
                        if(sub_block_position == SUB_BLOCK_SIZE - 1)
                        {
                            current_sub_block.image_data_size = sub_block_position + 1;
                            memwrite(gif_buffer, &current_sub_block, position, sub_block_position + 2);
                            sub_block_position = 0;
                            memset(&current_sub_block, 0, sizeof(struct gif_image_data_sub_block));
                            current_sub_block.image_data[0] = LZW_CC;
                        }
                    }
                }
                sub_block_position++;
                current_sub_block.image_data[sub_block_position] = LZW_EOI;
                current_sub_block.image_data_size = sub_block_position + 1;
                memwrite(gif_buffer, &current_sub_block, position, sub_block_position + 2);
                memwritebyte(gif_buffer, 0x00, position);
                
            }
            memwritebyte(gif_buffer, GIF_EOF, position);
            
            memcpy(output_buffer, gif_buffer + offset, MIN(max_size, gif_size - offset));
            free(gif_buffer);
            close_chunks(chunk_files, chunk_count);
            return max_size;
        }
        else
        {
            close_chunks(chunk_files, chunk_count);
            fprintf(stderr, "malloc error (requested size: %zu)\n", image_data_size);
        }
    }
    return 0;
}

size_t gif_get_size(struct frame_headers * frame_headers)
{
    uint16_t width = frame_headers->rawi_hdr.xRes / DOWNSCALE;
    uint16_t height = frame_headers->rawi_hdr.yRes / DOWNSCALE;
    
    size_t header_size = sizeof(struct gif_header) + sizeof(gif_animation_application_block);
    size_t frame_header_size = sizeof(gif_animation_graphics_block) + sizeof(struct gif_image_descriptor);
    size_t pixels = width * height + 1;
    size_t lzw_overhead = ((pixels / (SUB_BLOCK_SIZE - 1)) + 1) * 2;
    size_t frame_size = frame_header_size + pixels + lzw_overhead + 1;
    
    return header_size + FRAME_COUNT * frame_size + 1;
}