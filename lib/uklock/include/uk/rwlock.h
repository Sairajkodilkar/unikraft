/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Simon Kuenzer <simon.kuenzer@neclab.eu>
 *
 * Copyright (c) 2018, NEC Europe Ltd., NEC Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __UK_RWLOCK_H__
#define __UK_RWLOCK_H__

#include <uk/config.h>
#include <uk/essentials.h>
#include <uk/arch/atomic.h>
#include <stddef.h>
#include <uk/assert.h>
#include <uk/wait.h>
#include <uk/wait_types.h>
#include <uk/lock-common.h>
#include <uk/thread.h>

#include <uk/print.h>

#define UK_RWLOCK_CONFIG_SPIN			0x01
#define UK_RWLOCK_CONFIG_WRITE_RECURSE	0x02

struct __align(8) uk_rwlock {
	int nActive;
	int nPendingReads;
	int nPendingWrites;
	unsigned int config_flags;
	struct uk_spinlock sl;
	struct uk_waitq shared;
	struct uk_waitq exclusive;
};


void __uk_rwlock_init(struct uk_rwlock *rwl, uint8_t config_flags);

void uk_rwlock_rlock(struct uk_rwlock *rwl);

void uk_rwlock_wlock(struct uk_rwlock *rwl);

void uk_rwlock_runlock(struct uk_rwlock *rwl);

void uk_rwlock_wunlock(struct uk_rwlock *rwl);

void uk_rwlock_upgrade(struct uk_rwlock *rwl);

void uk_rwlock_downgrade(struct uk_rwlock *rwl);

#define uk_rwlock_init(rwl) __uk_rwlock_init(rwl, 0)

#define uk_rwlock_init_config(rwl, config_flags) \
	__uk_rwlock_init(rwl, config_flags)

_LOCK_IRQF(struct uk_rwlock *,  uk_rwlock_rlock)
_LOCK_IRQF(struct uk_rwlock *,  uk_rwlock_wlock)

_UNLOCK_IRQF(struct uk_rwlock *, uk_rwlock_runlock)
_UNLOCK_IRQF(struct uk_rwlock *, uk_rwlock_wunlock)

#endif /* __UK_RWLOCK_H__ */
