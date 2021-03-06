/*
	Glidix kernel

	Copyright (c) 2014-2017, Madd Games.
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	
	* Redistributions of source code must retain the above copyright notice, this
	  list of conditions and the following disclaimer.
	
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __glidix_ptr_h
#define __glidix_ptr_h

/**
 * Glidix support for pointing devices (such as mice).
 */

#include <glidix/common.h>
#include <glidix/semaphore.h>
#include <glidix/devfs.h>

/**
 * Describes a pointing device state.
 */
typedef struct
{
	/**
	 * Size of the display area.
	 */
	int						width;
	int						height;
	
	/**
	 * Position of the pointer on the display.
	 */
	int						posX;
	int						posY;
} PtrState;

typedef struct
{
	/**
	 * Current state of the device.
	 */
	PtrState					state;
	
	/**
	 * The semaphore which counts the number of updates. This should only ever go up
	 * to one, but may be more due to races, which is acceptable.
	 */
	Semaphore					semUpdate;
	
	/**
	 * Lock used to access the state.
	 */
	Semaphore					lock;
	
	/**
	 * The device file handle representing this pointing device.
	 */
	Device						dev;
	
	/**
	 * Reference count.
	 */
	int						refcount;
} PointDevice;

/**
 * Creates a new pointer device and returns a handle, or NULL on error.
 */
PointDevice *ptrCreate();

/**
 * Called by the driver to indicate that it no longer supports the device (e.g. when unloading driver).
 */
void ptrDelete(PointDevice *ptr);

/**
 * Report relative motion.
 */
void ptrRelMotion(PointDevice *ptr, int deltaX, int deltaY);

#endif
