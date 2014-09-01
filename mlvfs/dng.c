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
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"

#include "dng_tag_codes.h"
#include "dng_tag_types.h"
#include "dng_tag_values.h"

#define IFD0_COUNT 28
#define MLVFS_SOFTWARE_NAME "MLVFS"
#define PACK(a) (((uint16_t)a[1] << 16) | ((uint16_t)a[0]))
#define STRING_ENTRY(a,b,c) ttAscii, (strlen(a) + 1), add_string(a, b, c)
#define DATA_SPACE 4096

static uint16_t tiff_header[] = { byteOrderII, magicTIFF, 8, 0};

struct directory_entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;
};

static uint32_t add_string(char * str, uint8_t * buffer, size_t * data_offset)
{
    uint32_t result = 0;
    size_t length = strlen(str) + 1;
    if(length <= 4)
    {
        //we can fit in 4 bytes, so just pack the string into result
        memcpy(&result, str, length);
    }
    else
    {
        result = *data_offset;
        memcpy(buffer + result, str, length);
        *data_offset +=length;
        //align to 2 bytes
        if(*data_offset % 2) *data_offset += 1;
    }
    return result;
}

size_t dng_get_header_data(struct frame_headers * frame_headers, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    /*
    - build the tiff header in a buffer
    - then copy the buffer to the output buffer according to offset and max_size
    this shouldn't be a big performance hit and it's a lot easier than trying
    to only generate the requested section of the header (most of the time the
    entire header will be requested all at once anyway, since typically the 
    requested size is at least 64kB)
    */
    size_t header_size = dng_get_header_size(frame_headers);
    uint8_t * header = malloc(header_size);
    size_t position = 0;
    if(header)
    {
        memset(header, 0 , header_size);
        memcpy(header + position, tiff_header, sizeof(tiff_header));
        position += sizeof(tiff_header);
        char make[32];
        char * model = (char*)frame_headers->idnt_hdr.cameraName;
        if(!model) model = "???";
        //make is usually the first word of cameraName
        strncpy(make, model, 32);
        char * space = strchr(make, ' ');
        if(space) *space = 0x0;
        
        size_t data_offset = position + sizeof(uint16_t) + IFD0_COUNT * sizeof(struct directory_entry) + sizeof(uint32_t);
        struct directory_entry IFD0[IFD0_COUNT] =
        {
            {tcNewSubFileType,              ttLong,     1,      sfMainImage},
            {tcImageWidth,                  ttLong,     1,      frame_headers->rawi_hdr.xRes},
            {tcImageLength,                 ttLong,     1,      frame_headers->rawi_hdr.yRes},
            {tcBitsPerSample,               ttShort,    1,      16},
            {tcCompression,                 ttShort,    1,      ccUncompressed},
            {tcPhotometricInterpretation,   ttShort,    1,      piCFA},
            {tcFillOrder,                   ttShort,    1,      1},
            {tcMake,                        STRING_ENTRY(make, header, &data_offset)},
            {tcModel,                       STRING_ENTRY(model, header, &data_offset)},
            {tcStripOffsets,                ttLong,     1,      0},
            {tcOrientation,                 ttShort,    1,      1},
            {tcSamplesPerPixel,             ttShort,    1,      1},
            {tcRowsPerStrip,                ttShort,    1,      frame_headers->rawi_hdr.yRes},
            {tcStripByteCounts,             ttShort,    1,      dng_get_image_size(frame_headers)},
            {tcPlanarConfiguration,         ttShort,    1,      pcInterleaved},
            {tcSoftware,                    STRING_ENTRY(MLVFS_SOFTWARE_NAME, header, &data_offset)},
            {tcDateTime,                    STRING_ENTRY("", header, &data_offset)}, //TODO: implement
            {tcCFARepeatPatternDim,         ttShort,    2,      0x00020002}, //2x2
            {tcCFAPattern,                  ttByte,     4,      0x02010100}, //RGGB
            //TODO: EXIF
            {tcDNGVersion,                  ttByte,     4,      0x00000401}, //1.4.0.0 in little endian
            {tcUniqueCameraModel,           STRING_ENTRY(model, header, &data_offset)},
            {tcBlackLevel,                  ttLong,     1,      frame_headers->rawi_hdr.raw_info.black_level},
            {tcWhiteLevel,                  ttLong,     1,      frame_headers->rawi_hdr.raw_info.white_level},
            {tcDefaultCropOrigin,           ttShort,    2,      PACK(frame_headers->rawi_hdr.raw_info.crop.origin)},
            {tcDefaultCropSize,             ttShort,    2,      PACK(frame_headers->rawi_hdr.raw_info.crop.size)},
            {tcColorMatrix1,                ttSRational,0,      0}, //TODO: implement
            {tcAsShotNeutral,               ttRational, 0,      0}, //TODO: implement
            {tcBaselineExposure,            ttSRational,1,      PACK(frame_headers->rawi_hdr.raw_info.exposure_bias)},
            //TODO: CDNG tags
        };
        
        *(uint16_t*)(header + position) = IFD0_COUNT;
        position += sizeof(uint16_t);
        
        memcpy(header + position, IFD0, IFD0_COUNT * sizeof(struct directory_entry));
        position += IFD0_COUNT * sizeof(struct directory_entry);
        
        //next IFD offset = 0
        *(uint32_t*)(header + position) = 0;
        position += sizeof(uint32_t);
        
        //skip over all the strings we added
        position = data_offset;
        
        //TODO: EXIF IFD
        
        size_t output_size = MIN(max_size, header_size - (size_t)MIN(0, offset));
        if(output_size)
        {
            memcpy(output_buffer, header + offset, output_size);
        }
        free(header);
        return output_size;
    }
    return 0;
}

size_t dng_get_header_size(struct frame_headers * frame_headers)
{
    return sizeof(tiff_header) + sizeof(uint16_t) + IFD0_COUNT * sizeof(struct directory_entry) + sizeof(uint32_t) + DATA_SPACE;
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
