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

#ifndef _index_h_
#define _index_h_

#include <stdio.h>
#include <stdint.h>

#include "raw.h"
#include "mlv.h"

mlv_xref_hdr_t *get_index(const char *base_filename);
mlv_xref_hdr_t *force_index(const char *base_filename);

FILE **load_chunks(const char *base_filename, uint32_t *entries);
void close_chunks(FILE **chunk_files, uint32_t chunk_count);

int mlv_get_frame_count(const char *real_path);

/* platform/target specific fseek/ftell functions go here */
uint64_t file_get_pos(FILE *stream);
uint32_t file_set_pos(FILE *stream, uint64_t offset, int whence);

#endif
