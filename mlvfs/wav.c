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
#include <string.h>
#include "raw.h"
#include "mlv.h"
#include "index.h"

struct wav_header {
    //file header
    char RIFF[4];               // "RIFF"
    uint32_t file_size;
    char WAVE[4];               // "WAVE"
    //subchunk1
    char fmt[4];                // "fmt"
    uint32_t subchunk1_size;    // 16
    uint16_t audio_format;      // 1 (PCM)
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    //subchunk2
    char data[4];               // "data"
    uint32_t subchunk2_size;
    //audio data start
};

int has_audio(const char *path)
{
    mlv_file_hdr_t mlv_file_hdr;
    FILE * mlv_file = fopen(path, "rb");
    if(mlv_file)
    {
        if(fread(&mlv_file_hdr, sizeof(mlv_file_hdr_t), 1, mlv_file))
        {
            return !memcmp(mlv_file_hdr.fileMagic, "MLVI", 4) && mlv_file_hdr.audioClass == 1;
        }
        fclose(mlv_file);
    }
    return 0;
}

//TODO: implement
size_t wav_get_data(const char *path, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    return 0;
}

//TODO: implement
static int wav_get_wavi(const char *path, mlv_wavi_hdr_t * wavi_hdr)
{
    return 0;
}

size_t wav_get_size(const char *path)
{
    mlv_file_hdr_t mlv_file_hdr;
    mlv_wavi_hdr_t mlv_wavi_hdr;
    FILE * mlv_file = fopen(path, "rb");
    if(mlv_file)
    {
        if(fread(&mlv_file_hdr, sizeof(mlv_file_hdr_t), 1, mlv_file) && !memcmp(mlv_file_hdr.fileMagic, "MLVI", 4) && wav_get_wavi(path, &mlv_wavi_hdr))
        {
            return sizeof(struct wav_header) + mlv_wavi_hdr.bytesPerSecond * mlv_file_hdr.sourceFpsNom * mlv_get_frame_count(path) / mlv_file_hdr.sourceFpsDenom;
        }
        fclose(mlv_file);
    }
    
    return 0;
}