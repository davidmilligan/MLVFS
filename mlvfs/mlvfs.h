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

#ifndef mlvfs_mlvfs_h
#define mlvfs_mlvfs_h

#define MLVFS_SOFTWARE_NAME "MLVFS"

//Let the DNGs be "writeable" for AE, even though they're not actually writable
//You'll get an error if you actually try to write to them
#define ALLOW_WRITEABLE_DNGS

//some macros for simple thread synchronization
#define LOCK(x) static pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER; pthread_mutex_lock(&x);
#define UNLOCK(x) pthread_mutex_unlock(&x);

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))
#define ABS(a) (a > 0 ? a : -a)

#endif
