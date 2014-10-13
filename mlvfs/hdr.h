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

#ifndef mlvfs_dual_iso_h
#define mlvfs_dual_iso_h

#include <sys/types.h>
#include "dng.h"

void hdr_convert_data(struct frame_headers * frame_headers, uint16_t * image_data, off_t offset, size_t max_size);
void cr2hdr20_convert_data(struct frame_headers * frame_headers, uint16_t * image_data, int fullres);

#endif
