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

#ifndef __mlvfs__gif__
#define __mlvfs__gif__

#include <sys/types.h>

#include "mlvfs.h"

size_t gif_get_data(const char * path, uint8_t * output_buffer, off_t offset, size_t max_size);
size_t gif_get_size(struct frame_headers * frame_headers);

#endif /* defined(__mlvfs__gif__) */
