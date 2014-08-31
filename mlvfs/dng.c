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

#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"


//TODO: implement this function
size_t dng_get_header_data(struct frame_headers * frame_headers, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    size_t header_size = dng_get_header_size(frame_headers);
    memset(output_buffer, 0, header_size);
    return header_size;
}

//TODO: implement this function
size_t dng_get_header_size(struct frame_headers * frame_headers)
{
    return 0;
}

/**
 * 14-bit encoding:
 
 hi          lo
 aaaaaaaa aaaaaabb
 bbbbbbbb bbbbcccc
 cccccccc ccdddddd
 dddddddd eeeeeeee
 eeeeeeff ffffffff
 ffffgggg gggggggg
 gghhhhhh hhhhhhhh
 */
size_t dng_get_image_data(struct frame_headers * frame_headers, FILE * file, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    //unpack bits to 16 bit little endian (LSB first)
    uint64_t pixel_start_index = (size_t)MAX(0, offset) / 2; //lets hope offsets are always even for now
    uint64_t pixel_start_address = pixel_start_index * 14 / 8;
    size_t output_size = max_size - (offset < 0 ? (size_t)(-offset) : 0);
    uint64_t pixel_count = output_size / 2;
    uint64_t packed_size = (pixel_count + 1) * 16 / 14;
    uint8_t * packed_bits = malloc((size_t)packed_size);
    uint8_t * buffer = output_buffer + (offset < 0 ? (size_t)(-offset) : 0);
    memset(buffer, 0, output_size);
    if(packed_bits)
    {
        fseek(file, frame_headers->position + frame_headers->vidf_hdr.frameSpace + sizeof(mlv_vidf_hdr_t) + (size_t)pixel_start_address, SEEK_SET);
        fread(packed_bits, (size_t)packed_size, 1, file);
        for(size_t pixel_index = 0; pixel_index < pixel_count; pixel_index++)
        {
            uint64_t pixel_address = (pixel_index + pixel_start_index) * 14 / 8 - pixel_start_address;
            uint64_t pixel_offset = (pixel_index + pixel_start_index) * 14 % 8;
            uint64_t byte1_address = pixel_address + (pixel_address % 2 ? -1 : 1);
            uint64_t byte2_address = pixel_address + (pixel_address % 2 ? 2 : 0);
            uint64_t byte3_address = byte1_address + 2;
            switch(pixel_offset)
            {
                case 0:
                    buffer[pixel_index * 2 + 1] = (packed_bits[byte1_address] >> 2) & 0x3F;
                    buffer[pixel_index * 2] = ((packed_bits[byte1_address] << 6) & 0xC0) | ((packed_bits[byte2_address] >> 2) & 0x3F);
                    break;
                case 2:
                    buffer[pixel_index * 2 + 1] = packed_bits[byte1_address] & 0x3F;
                    buffer[pixel_index * 2] = packed_bits[byte2_address];
                    break;
                case 4:
                    buffer[pixel_index * 2 + 1] = (((packed_bits[byte1_address] << 2) & 0x3C) | ((packed_bits[byte2_address] >> 6) & 0x03)) & 0x3F;
                    buffer[pixel_index * 2] = ((packed_bits[byte2_address] << 2) & 0xFC) | ((packed_bits[byte3_address] >> 6) & 0x03);
                    break;
                case 6:
                    buffer[pixel_index * 2 + 1] = (((packed_bits[byte1_address] << 4) & 0x30) | ((packed_bits[byte2_address] >> 4) & 0x0F)) & 0x3F;
                    buffer[pixel_index * 2] = ((packed_bits[byte2_address] << 4) & 0xF0) | ((packed_bits[byte3_address] >> 4) & 0x0F);
                    break;
            }
        }
        free(packed_bits);
    }
    return max_size;
}

size_t dng_get_image_size(struct frame_headers * frame_headers)
{
    if(frame_headers->rawi_hdr.raw_info.bits_per_pixel != 14)
    {
        fprintf(stderr, "Only 14-bit source data is supported");
        return 0;
    }
    return (frame_headers->vidf_hdr.blockSize - frame_headers->vidf_hdr.frameSpace - sizeof(mlv_vidf_hdr_t)) * 16 / 14; //16 bit
}

size_t dng_get_size(struct frame_headers * frame_headers)
{
    return dng_get_header_size(frame_headers) + dng_get_image_size(frame_headers);
}
