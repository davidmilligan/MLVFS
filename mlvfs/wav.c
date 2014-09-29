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
#include "mlvfs.h"
#include "wav.h"

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
    uint16_t block_align;       // ???
    uint16_t bits_per_sample;
    //subchunk2
    char data[4];               // "data"
    uint32_t subchunk2_size;
    //audio data start
};

int wav_get_wavi(const char *path, mlv_wavi_hdr_t * wavi_hdr)
{
    FILE **chunk_files = NULL;
    uint32_t chunk_count = 0;
    
    chunk_files = load_chunks(path, &chunk_count);
    if(!chunk_files || !chunk_count)
    {
        return 0;
    }
    
    mlv_xref_hdr_t *block_xref = get_index(path);
    mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)block_xref)[sizeof(mlv_xref_hdr_t)]);
    
    int found = 0;
    mlv_hdr_t mlv_hdr;
    uint32_t hdr_size;
    
    for(uint32_t block_xref_pos = 0; (block_xref_pos < block_xref->entryCount) && !found; block_xref_pos++)
    {
        /* get the file and position of the next block */
        uint32_t in_file_num = xrefs[block_xref_pos].fileNumber;
        int64_t position = xrefs[block_xref_pos].frameOffset;
        
        /* select file */
        FILE *in_file = chunk_files[in_file_num];
        
        file_set_pos(in_file, position, SEEK_SET);
        fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, in_file);
        file_set_pos(in_file, position, SEEK_SET);
        if(!memcmp(mlv_hdr.blockType, "WAVI", 4))
        {
            hdr_size = MIN(sizeof(mlv_wavi_hdr_t), mlv_hdr.blockSize);
            fread(wavi_hdr, hdr_size, 1, in_file);
            found = 1;
            break;
        }
    }
    
    free(block_xref);
    close_chunks(chunk_files, chunk_count);
    
    return found;
}

int has_audio(const char *path)
{
    int result = 0;
    mlv_file_hdr_t mlv_file_hdr;
    FILE * mlv_file = fopen(path, "rb");
    if(mlv_file)
    {
        if(fread(&mlv_file_hdr, sizeof(mlv_file_hdr_t), 1, mlv_file))
        {
            result = !memcmp(mlv_file_hdr.fileMagic, "MLVI", 4) && mlv_file_hdr.audioClass == 1;
        }
        fclose(mlv_file);
    }
    return result;
}

size_t wav_get_data(const char *path, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    mlv_wavi_hdr_t wavi_hdr;
    size_t size = wav_get_size(path);
    size_t read = 0;
    if(wav_get_wavi(path, &wavi_hdr))
    {
        FILE **chunk_files = NULL;
        uint32_t chunk_count = 0;
        
        chunk_files = load_chunks(path, &chunk_count);
        if(!chunk_files || !chunk_count)
        {
            return 0;
        }
        mlv_xref_hdr_t *block_xref = get_index(path);

        read = wav_get_data_direct(chunk_files, block_xref, &wavi_hdr, size, output_buffer, offset, MIN(max_size, size - offset));

        free(block_xref);
        close_chunks(chunk_files, chunk_count);
        
        return read;
    }
    return 0;
}

size_t wav_get_data_direct(FILE ** chunk_files, mlv_xref_hdr_t * block_xref, mlv_wavi_hdr_t * wavi_hdr, size_t file_size, uint8_t * output_buffer, off_t offset, size_t length)
{
    #ifdef _WIN32 // Windows does not support C99 designated initializers
    struct wav_header header;
    strncpy(header.RIFF, "RIFF", 4);
    header.file_size = (uint32_t)file_size;
    strncpy(header.WAVE, "WAVE", 4);
    strncpy(header.fmt, "fmt\x20", 4);
    header.subchunk1_size = 16;
    header.audio_format = 1;
    header.num_channels = wavi_hdr->channels;
    header.sample_rate = wavi_hdr->samplingRate;
    header.byte_rate = wavi_hdr->bytesPerSecond;
    header.block_align = 4;
    header.bits_per_sample = wavi_hdr->bitsPerSample;
    strncpy(header.data, "data", 4);
    header.subchunk2_size = (uint32_t)(file_size - sizeof(struct wav_header) + 8);
    #else
    struct wav_header header =
    {
        .RIFF = "RIFF",
        .file_size = (uint32_t)file_size,
        .WAVE = "WAVE",
        .fmt = "fmt\x20",
        .subchunk1_size = 16,
        .audio_format = 1,
        .num_channels = wavi_hdr->channels,
        .sample_rate = wavi_hdr->samplingRate,
        .byte_rate = wavi_hdr->bytesPerSecond,
        .block_align = 4,
        .bits_per_sample = wavi_hdr->bitsPerSample,
        .data = "data",
        .subchunk2_size = (uint32_t)(file_size - sizeof(struct wav_header) + 8),
    };
    #endif

    int64_t output_position = 0;
    if(offset < sizeof(struct wav_header))
    {
        memcpy(output_buffer, &header + offset, MIN(sizeof(struct wav_header) - offset, length));
        output_position += MIN(sizeof(struct wav_header) - offset, length);
    }

    mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)block_xref)[sizeof(mlv_xref_hdr_t)]);
    mlv_audf_hdr_t audf_hdr;
    int64_t audio_position = 0;
    int64_t requested_audio_offset = offset - sizeof(struct wav_header);

    for(uint32_t block_xref_pos = 0; (block_xref_pos < block_xref->entryCount); block_xref_pos++)
    {
        if(xrefs[block_xref_pos].frameType == MLV_FRAME_AUDF)
        {
            uint32_t in_file_num = xrefs[block_xref_pos].fileNumber;
            int64_t position = xrefs[block_xref_pos].frameOffset;
            FILE *in_file = chunk_files[in_file_num];

            file_set_pos(in_file, position, SEEK_SET);
            fread(&audf_hdr, sizeof(mlv_audf_hdr_t), 1, in_file);
            if(!memcmp(audf_hdr.blockType, "AUDF", 4))
            {
                size_t frame_size = audf_hdr.blockSize - sizeof(mlv_audf_hdr_t) - audf_hdr.frameSpace;
                int64_t frame_end = audio_position + frame_size;
                if(frame_end >= requested_audio_offset)
                {
                    int64_t start_offset = offset - sizeof(struct wav_header) - audio_position;
                    start_offset = MAX(0, start_offset);
                    file_set_pos(in_file, position + sizeof(mlv_audf_hdr_t) + audf_hdr.frameSpace + start_offset, SEEK_SET);
                    fread(output_buffer + output_position, MIN(frame_size - start_offset, length - output_position), 1, in_file);
                    output_position += MIN(frame_size - start_offset, length - output_position);
                    if(length - output_position <= 0) break;
                }
                audio_position += frame_size;
            }
        }
    }

    if(output_position < length)
    {
        memset(output_buffer + output_position, 0, length - output_position);
    }

    return length;
}

size_t wav_get_size(const char *path)
{
    size_t result = 0;
    mlv_file_hdr_t mlv_file_hdr;
    mlv_wavi_hdr_t mlv_wavi_hdr;
    FILE * mlv_file = fopen(path, "rb");
    if(mlv_file)
    {
        if(fread(&mlv_file_hdr, sizeof(mlv_file_hdr_t), 1, mlv_file) && !memcmp(mlv_file_hdr.fileMagic, "MLVI", 4) && wav_get_wavi(path, &mlv_wavi_hdr))
        {
            result = sizeof(struct wav_header) + (uint64_t)mlv_wavi_hdr.bytesPerSecond * (uint64_t)mlv_file_hdr.sourceFpsDenom * (uint64_t)mlv_get_frame_count(path) / (uint64_t)mlv_file_hdr.sourceFpsNom;
        }
        fclose(mlv_file);
    }
    
    return result;
}
