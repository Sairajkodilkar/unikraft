/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Sairaj Kodilkar <skodilkar7@gmail.com>
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

#include <uk/rwlock.h>
#include <uk/assert.h>
#include <uk/config.h>

#define __IS_CONFIG_SPIN(flag)			(flag & UK_RWLOCK_CONFIG_SPIN)
#define __IS_CONFIG_WRITE_RECURSE(flag)	(flag & UK_RWLOCK_CONFIG_WRITE_RECURSE)

void __uk_rwlock_init(struct uk_rwlock *rwl, uint8_t config_flags)
{
	UK_ASSERT(rwl);

	rwl->nActive = 0;
	rwl->nPendingReads = 0;
	rwl->nPendingWrites = 0;
	rwl->config_flags = config_flags;
	uk_spin_init(&rwl->sl);
	uk_waitq_init(&rwl->shared);
	uk_waitq_init(&rwl->exclusive);
}


void uk_rwlock_rlock(struct uk_rwlock *rwl)
{
	uk_spin_lock(&rwl->sl);
	rwl->nPendingReads++;


	/* Wait for pending writes */
	if (rwl->nPendingWrites > 0)
		uk_waitq_wait_locked(
				&rwl->shared,
				uk_spin_lock,
				uk_spin_unlock,
				&rwl->sl);

	/* Wait for existing write to unlock the lock
	 * The separate waits for the both ensures that there is no starvation
	 */
	uk_waitq_wait_event_locked(
			&rwl->shared,
			rwl->nActive >= 0,
			uk_spin_lock,
			uk_spin_unlock,
			&rwl->sl);

	rwl->nActive++;
	rwl->nPendingReads--;
	uk_spin_unlock(&rwl->sl);
}


void uk_rwlock_wlock(struct uk_rwlock *rwl)
{
	uk_spin_lock(&rwl->sl);
	rwl->nPendingWrites++;
	uk_waitq_wait_event_locked(
			&rwl->exclusive,
			rwl->nActive == 0,
			uk_spin_lock,
			uk_spin_unlock,
			&rwl->sl);
	rwl->nPendingWrites--;
	rwl->nActive = -1;
	uk_spin_unlock(&rwl->sl);
}

void uk_rwlock_runlock(struct uk_rwlock *rwl)
{
	uk_spin_lock(&rwl->sl);
	rwl->nActive--;
	if (rwl->nActive == 0 && rwl->nPendingWrites) {
		uk_spin_unlock(&rwl->sl);
		uk_waitq_wake_up_one(&rwl->exclusive);
	} else {
		uk_spin_unlock(&rwl->sl);
	}
}

void uk_rwlock_wunlock(struct uk_rwlock *rwl)
{
	int wakeReaders;

	uk_spin_lock(&rwl->sl);
	rwl->nActive = 0;
	wakeReaders = rwl->nPendingReads != 0;
	uk_spin_unlock(&rwl->sl);
	if (wakeReaders)
		uk_waitq_wake_up(&rwl->shared);
	else
		uk_waitq_wake_up_one(&rwl->exclusive);
}

void uk_rwlock_upgrade(struct uk_rwlock *rwl)
{
	uk_spin_lock(&rwl->sl);
	if (rwl->nActive == 1) {
		rwl->nActive = -1;
	} else {
		rwl->nPendingWrites++;
		rwl->nActive--;
		uk_waitq_wait_event_locked(
				&rwl->shared,
				rwl->nActive == 0,
				uk_spin_lock,
				uk_spin_unlock,
				&rwl->sl);
		rwl->nPendingWrites--;
		rwl->nActive = -1;
	}
	uk_spin_unlock(&rwl->sl);
}

void uk_rwlock_downgrade(struct uk_rwlock *rwl)
{
	int wakeReaders;

	uk_spin_lock(&rwl->sl);
	rwl->nActive = 1;
	wakeReaders = (rwl->nPendingReads != 0);
	uk_spin_unlock(&rwl->sl);
	if (wakeReaders)
		uk_waitq_wake_up(&rwl->shared);
}
