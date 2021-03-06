/*
	Glidix Runtime

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

#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <sys/types.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __sem_waiter
{
	pthread_t					thread;
	int						complete;
	struct __sem_waiter*				next;
} __sem_waiter_t;

typedef struct
{
	/**
	 * The spinlock which protects this semaphore.
	 */
	pthread_spinlock_t				spinlock;
	
	/**
	 * Current value of the semaphore.
	 */
	unsigned					value;
	
	/**
	 * List of threads waiting on the semaphore. See mutexes for how this works.
	 */
	__sem_waiter_t*					firstWaiter;
	__sem_waiter_t*					lastWaiter;
} sem_t;

/* implemented by the runtime */
int	sem_init(sem_t *sem, int pshared, unsigned value);
int	sem_destroy(sem_t *sem);
int	sem_wait(sem_t *sem);
int	sem_post(sem_t *sem);
int	sem_getvalue(sem_t *sem, int *valptr);

#ifdef __cplusplus
};	/* extern "C" */
#endif

#endif
