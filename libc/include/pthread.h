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

#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	PTHREAD_SCOPE_SYSTEM				0
#define	PTHREAD_SCOPE_PROCESS				1

#define	PTHREAD_CREATE_JOINABLE				0
#define	PTHREAD_CREATE_DETACHED				1

#define	PTHREAD_INHERIT_SCHED				0
#define	PTHREAD_EXPLICIT_SCHED				1

#define	PTHREAD_STACK_MIN				0x1000

#define	PTHREAD_MUTEX_NORMAL				0
#define	PTHREAD_MUTEX_ERRORCHECK			1
#define	PTHREAD_MUTEX_RECURSIVE				2
#define	PTHREAD_MUTEX_DEFAULT				PTHREAD_MUTEX_NORMAL

#define	PTHREAD_PRIO_NONE				0

#define	PTHREAD_MUTEX_INITIALIZER			{0, 0, 0, 0, NULL, NULL}

typedef struct
{
	int						type;
	int						protocol;
	int						prioceiling;
} pthread_mutexattr_t;

typedef struct __pthread_mutex_waiter_tag
{
	pthread_t					thread;
	struct __pthread_mutex_waiter_tag*		next;
} __pthread_mutex_waiter;

typedef struct
{
	/**
	 * The spinlock which protects the mutex.
	 */
	pthread_spinlock_t				spinlock;
	
	/**
	 * Type of mutex.
	 */
	int						type;
	
	/**
	 * The thread which currently holds the mutex; invalid if released.
	 */
	pthread_t					owner;
	
	/**
	 * Number of times this mutex is held. May be larger than 1 if recursive.
	 * 0 means it is released.
	 */
	int						count;
	
	/**
	 * Queue of waiting threads. Each struct is allocated on a waiting thread's
	 * stack, and after being removed from the linked list by the releasing thread,
	 * they are cleaned up by the waiting thread.
	 */
	__pthread_mutex_waiter*				firstWaiter;
	__pthread_mutex_waiter*				lastWaiter;
} pthread_mutex_t;

/* implemented by libglidix directly */
int		pthread_create(pthread_t *thread, const pthread_attr_t *attr, void*(*start_routine)(void*), void *arg);
pthread_t	pthread_self();
int		pthread_join(pthread_t thread, void **retval);
int		pthread_detach(pthread_t thread);
int		pthread_kill(pthread_t thread, int sig);

/* implemented by the runtime */
int		pthread_spin_init(pthread_spinlock_t *sl);
int		pthread_spin_destroy(pthread_spinlock_t *sl);
int		pthread_spin_lock(pthread_spinlock_t *lock);
int		pthread_spin_trylock(pthread_spinlock_t *lock);
int		pthread_spin_unlock(pthread_spinlock_t *lock);
int		pthread_attr_init(pthread_attr_t *attr);
int		pthread_attr_destroy(pthread_attr_t *attr);
int		pthread_attr_setscope(pthread_attr_t *attr, int scope);
int		pthread_attr_getscope(const pthread_attr_t *attr, int *scope);
int		pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int		pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int		pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched);
int		pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inheritsched);
int		pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr);
int		pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr);
int		pthread_attr_setstack(pthread_attr_t *attr, void *stackaddr, size_t stacksize);
int		pthread_attr_getstack(const pthread_attr_t *attr, void **stackaddr, size_t *stacksize);
int		pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int		pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
int		pthread_mutexattr_init(pthread_mutexattr_t *attr);
int		pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int		pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int		pthread_mutex_destroy(pthread_mutex_t *mutex);
int		pthread_mutex_lock(pthread_mutex_t *mutex);
int		pthread_mutex_trylock(pthread_mutex_t *mutex);
int		pthread_mutex_unlock(pthread_mutex_t *mutex);

#ifdef __cplusplus
};	/* extern "C" */
#endif

#endif
