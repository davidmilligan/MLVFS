/*
 * Copyright (C) 2014 Magic Lantern Team
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
#include <errno.h>

#include "raw.h"
#include "mlv.h"

/* helper macros */
#define MIN(a,b) (((a)<(b))?(a):(b))

/* platform/target specific fseek/ftell functions go here */
uint64_t file_get_pos(FILE *stream)
{
#if defined(_WIN32)
    return _ftelli64(stream);
#else
    return ftello(stream);
#endif
}

uint32_t file_set_pos(FILE *stream, uint64_t offset, int whence)
{
#if defined(_WIN32)
    return _fseeki64(stream, offset, whence);
#else
    return fseeko(stream, offset, whence);
#endif
}

/* this structure is used to build the mlv_xref_t table */
typedef struct
{
    uint64_t    frameTime;
    uint64_t    frameOffset;
    uint16_t    fileNumber;
    uint16_t    frameType;
} frame_xref_t;

void xref_resize(frame_xref_t **table, uint32_t entries, uint32_t *allocated)
{
    /* make sure there is no crappy pointer before using */
    if(*allocated == 0)
    {
        *table = NULL;
    }

    /* only resize if the buffer is too small */
    if(entries * sizeof(frame_xref_t) > *allocated)
    {
        *allocated += (entries + 1) * sizeof(frame_xref_t);
        *table = (frame_xref_t *)realloc(*table, *allocated);
    }
}

void xref_sort(frame_xref_t *table, uint32_t entries)
{
    if (!entries) return;
    
    uint32_t n = entries;
    do
    {
        uint32_t newn = 1;
        for (uint32_t i = 0; i < n-1; ++i)
        {
            if (table[i].frameTime > table[i+1].frameTime)
            {
                frame_xref_t tmp = table[i+1];
                table[i+1] = table[i];
                table[i] = tmp;
                newn = i + 1;
            }
        }
        n = newn;
    } while (n > 1);
}

mlv_xref_hdr_t *load_index(const char *base_filename)
{
    size_t filename_size = (strlen(base_filename) + 1) * sizeof(char);
    char * filename = (char*)malloc(filename_size);
    
    if(!filename)
    {
        fprintf(stderr, "load_index: malloc error (requested size %zu)\n", filename_size);
        return NULL;
    }
    strncpy(filename, base_filename, filename_size);
    
    mlv_xref_hdr_t *block_hdr = NULL;
    FILE *in_file = NULL;

    strcpy(&filename[strlen(filename) - 3], "IDX");

    in_file = fopen(filename, "rb");
    
    free(filename);

    if (!in_file)
    {
        return NULL;
    }

    do
    {
        mlv_hdr_t buf;
        int64_t position = 0;

        position = file_get_pos(in_file);

        if(fread(&buf, sizeof(mlv_hdr_t), 1, in_file) != 1)
        {
            break;
        }

        /* jump back to the beginning of the block just read */
        file_set_pos(in_file, position, SEEK_SET);

        /* we should check the MLVI header for matching UID value to make sure its the right index... */
        if(!memcmp(buf.blockType, "XREF", 4))
        {
            block_hdr = (mlv_xref_hdr_t *)malloc(buf.blockSize);
            if (!block_hdr)
            {
                fclose(in_file);
                return NULL;
            }

            if(fread(block_hdr, buf.blockSize, 1, in_file) != 1)
            {
                free(block_hdr);
                block_hdr = NULL;
            }
        }
        else
        {
            file_set_pos(in_file, position + buf.blockSize, SEEK_SET);
        }

        /* we are at the same position as before, so abort */
        if(position == file_get_pos(in_file))
        {
            break;
        }
    }
    while(!feof(in_file));

    fclose(in_file);

    return block_hdr;
}

void save_index(const char *base_filename, mlv_file_hdr_t *ref_file_hdr, int fileCount, mlv_xref_hdr_t *index)
{
    size_t filename_size = (strlen(base_filename) + 1) * sizeof(char);
    char * filename = (char*)malloc(filename_size);
    
    if(!filename)
    {
        fprintf(stderr, "save_index: malloc error (requested size %zu)\n", filename_size);
        return;
    }
    strncpy(filename, base_filename, filename_size);
    
    FILE *out_file = NULL;

    strcpy(&filename[strlen(filename) - 3], "IDX");

    out_file = fopen(filename, "wb+");
    
    free(filename);

    if (!out_file)
    {
        return;
    }

    /* first write MLVI header */
    mlv_file_hdr_t file_hdr = *ref_file_hdr;

    /* update fields */
    file_hdr.blockSize = sizeof(mlv_file_hdr_t);
    file_hdr.videoFrameCount = 0;
    file_hdr.audioFrameCount = 0;
    file_hdr.fileNum = fileCount + 1;

    fwrite(&file_hdr, sizeof(mlv_file_hdr_t), 1, out_file);

    fwrite(index, index->blockSize, 1, out_file);

    fclose(out_file);
}

mlv_xref_hdr_t *make_index(FILE **chunk_files, uint32_t chunk_count)
{
    mlv_xref_hdr_t *index = NULL;
    frame_xref_t *frame_xref_table = NULL;
    uint32_t frame_xref_entries = 0;
    uint32_t frame_xref_allocated = 0;
    mlv_file_hdr_t main_header;
    memset(&main_header, 0, sizeof(mlv_file_hdr_t));

    for(uint32_t chunk = 0; chunk < chunk_count; chunk++)
    {
        int64_t position = 0;

        file_set_pos(chunk_files[chunk], 0, SEEK_SET);

        while(1)
        {
            mlv_hdr_t buf;
            uint64_t timestamp = 0;
            size_t read;

            if((read = fread(&buf, sizeof(mlv_hdr_t), 1, chunk_files[chunk])) != 1)
            {
                if(ferror(chunk_files[chunk]))
                {
                    int err = errno;
                    fprintf(stderr, "make_index: File #%d, %zu bytes read, fread error: %s\n", chunk, read, strerror(err));
                }
                break;
            }

            /* unexpected block header size? */
            if(buf.blockSize < sizeof(mlv_hdr_t) || buf.blockSize > 1024 * 1024 * 1024)
            {
                fprintf(stderr, "make_index: Invalid header size: %d bytes at 0x%08llX\n", buf.blockSize, position);
                break;
            }

            /* file header */
            if(!memcmp(buf.blockType, "MLVI", 4))
            {
                mlv_file_hdr_t file_hdr;
                size_t hdr_size = MIN(sizeof(mlv_file_hdr_t), buf.blockSize);

                file_set_pos(chunk_files[chunk], position, SEEK_SET);

                /* read the whole header block, but limit size to either our local type size or the written block size */
                if(fread(&file_hdr, hdr_size, 1, chunk_files[chunk]) != 1)
                {
                    //bmp_printf(FONT_MED, 30, 190, "File ends prematurely during MLVI");
                    break;
                }

                /* is this the first file? */
                if(file_hdr.fileNum == 0)
                {
                    memcpy(&main_header, &file_hdr, sizeof(mlv_file_hdr_t));
                }
                else
                {
                    /* no, its another chunk */
                    if(main_header.fileGuid != file_hdr.fileGuid)
                    {
                        //bmp_printf(FONT_MED, 30, 190, "Error: GUID within the file chunks mismatch!");
                        break;
                    }
                }

                /* emulate timestamp zero (will overwrite version string) */
                timestamp = 0;
            }
            else
            {
                /* all other blocks have a timestamp */
                timestamp = buf.timestamp;
            }

            /* dont index NULL blocks */
            if(memcmp(buf.blockType, "NULL", 4))
            {
                xref_resize(&frame_xref_table, frame_xref_entries + 1, &frame_xref_allocated);

                /* add xref data */
                frame_xref_table[frame_xref_entries].frameTime = timestamp;
                frame_xref_table[frame_xref_entries].frameOffset = position;
                frame_xref_table[frame_xref_entries].fileNumber = chunk;
                frame_xref_table[frame_xref_entries].frameType =
                    !memcmp(buf.blockType, "VIDF", 4) ? MLV_FRAME_VIDF :
                    !memcmp(buf.blockType, "AUDF", 4) ? MLV_FRAME_AUDF :
                    MLV_FRAME_UNSPECIFIED;

                frame_xref_entries++;
            }

            position += buf.blockSize;
            file_set_pos(chunk_files[chunk], position, SEEK_SET);
        }
    }

    xref_sort(frame_xref_table, frame_xref_entries);

    size_t size = sizeof(mlv_xref_hdr_t) + frame_xref_entries * sizeof(mlv_xref_t);
    index = (mlv_xref_hdr_t *)malloc(size);
    if (!index)
    {
        free(frame_xref_table);
        return NULL;
    }
    mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)index)[sizeof(mlv_xref_hdr_t)]);

    memset(index, 0, size);
    memcpy(index->blockType, "XREF", 4);
    index->blockSize = (uint32_t)size;
    index->entryCount = frame_xref_entries;

    for(uint32_t entry = 0; entry < frame_xref_entries; entry++)
    {
        xrefs[entry].frameOffset = frame_xref_table[entry].frameOffset;
        xrefs[entry].fileNumber = frame_xref_table[entry].fileNumber;
        xrefs[entry].frameType = frame_xref_table[entry].frameType;
    }

    free(frame_xref_table);

    return index;
}

void build_index(const char *base_filename, FILE **chunk_files, uint32_t chunk_count)
{
    // read the MLVI header from the first file
    // TODO: add some error checking
    mlv_file_hdr_t main_header;
    file_set_pos(chunk_files[0], 0, SEEK_SET);
    if(!fread(&main_header, sizeof(mlv_file_hdr_t), 1, chunk_files[0]))
    {
        if(ferror(chunk_files[0]))
        {
            int err = errno;
            fprintf(stderr, "build_index: fread error: %s\n", strerror(err));
        }
        else
        {
            fprintf(stderr, "build_index: could not read main header\n");
        }
    }

    mlv_xref_hdr_t *index = make_index(chunk_files, chunk_count);
    save_index(base_filename, &main_header, chunk_count, index);

    free(index);
}

FILE **load_chunks(const char *base_filename, uint32_t *entries)
{
    uint32_t seq_number = 0;
    size_t filename_size = (strlen(base_filename) + 1) * sizeof(char);
    char * filename = (char*)malloc(filename_size);

    if(!filename)
    {
        fprintf(stderr, "load_chunks: malloc error (requested size %zu)\n", filename_size);
        return NULL;
    }
    *entries = 0;

    strncpy(filename, base_filename, filename_size);
    FILE **files = (FILE **)malloc(sizeof(FILE*));

    files[0] = fopen(filename, "rb");
    if (!files[0])
    {
        int err = errno;
        fprintf(stderr, "load_chunks: fopen error: %s\n", strerror(err));
        return NULL;
    }

    (*entries)++;
    while(seq_number < 99)
    {
        files = (FILE **)realloc(files, (*entries + 1) * sizeof(FILE*));

        /* check for the next file M00, M01 etc */
        char seq_name[3];

        #if defined(_WIN32)
        _snprintf(seq_name, 3, "%02d", seq_number);
        #else
        snprintf(seq_name, 3, "%02d", seq_number);
        #endif
        seq_number++;

        strcpy(&filename[strlen(filename) - 2], seq_name);

        files[*entries] = fopen(filename, "rb");

        /* when succeeded, check for next chunk, else abort */
        if (files[*entries])
        {
            (*entries)++;
        }
        else
        {
            break;
        }
    }
    free(filename);
    return files;
}

void close_chunks(FILE **chunk_files, uint32_t chunk_count)
{
    if(!chunk_files || !chunk_count || chunk_count > 100)
    {
        fprintf(stderr, "close_chunks: faulty parameters\n");
        return;
    }

    for(uint32_t pos = 0; pos < chunk_count; pos++)
    {
        fclose(chunk_files[pos]);
    }

    free(chunk_files);
}

mlv_xref_hdr_t *force_index(const char *base_filename)
{
    FILE **chunk_files = NULL;
    uint32_t chunk_count = 0;

    chunk_files = load_chunks(base_filename, &chunk_count);
    if(!chunk_files || !chunk_count)
    {
        return NULL;
    }

    build_index(base_filename, chunk_files, chunk_count);
    close_chunks(chunk_files, chunk_count);

    return load_index(base_filename);
}

mlv_xref_hdr_t *get_index(const char *base_filename)
{
    mlv_xref_hdr_t *table = NULL;

    table = load_index(base_filename);

    if(!table)
    {
        table = force_index(base_filename);
    }

    return table;
}

mlv_xref_hdr_t *get_new_index(const char *base_filename)
{
    FILE **chunk_files = NULL;
    uint32_t chunk_count = 0;

    chunk_files = load_chunks(base_filename, &chunk_count);
    if(!chunk_files || !chunk_count)
    {
        return NULL;
    }

    mlv_xref_hdr_t *index = make_index(chunk_files, chunk_count);
    close_chunks(chunk_files, chunk_count);

    return index;
}

int mlv_get_frame_count(const char *real_path)
{
    uint32_t videoFrameCount = 0;
    
    mlv_xref_hdr_t *block_xref = get_index(real_path);
    if(block_xref == NULL) return 0;
    
    mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)block_xref)[sizeof(mlv_xref_hdr_t)]);
    
    for(uint32_t block_xref_pos = 0; block_xref_pos < block_xref->entryCount; block_xref_pos++)
    {
        if(xrefs[block_xref_pos].frameType == MLV_FRAME_VIDF)
        {
            videoFrameCount++;
        }
    }
    
    // If there are no VIDF frames at all, the IDX file is probably an old format, and needs to be re-built
    // TODO: clean up the following repetition of code
    if(videoFrameCount == 0)
    {
        free(block_xref);
        block_xref = force_index(real_path);
		if (block_xref == NULL) return 0;
        xrefs = (mlv_xref_t *)&(((uint8_t*)block_xref)[sizeof(mlv_xref_hdr_t)]);
        
        for(uint32_t block_xref_pos = 0; block_xref_pos < block_xref->entryCount; block_xref_pos++)
        {
            if(xrefs[block_xref_pos].frameType == MLV_FRAME_VIDF)
            {
                videoFrameCount++;
            }
        }
    }
    
    free(block_xref);
    
    return videoFrameCount;
}
