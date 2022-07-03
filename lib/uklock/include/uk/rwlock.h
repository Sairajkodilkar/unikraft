#ifndef __UK_RWLOCK_H__
#define __UK_RWLOCK_H__

#include <uk/config.h>

#include <uk/essentials.h>
#include <uk/arch/atomic.h>
#include <stddef.h>
#include <uk/assert.h>
#include <uk/wait.h>
#include <uk/wait_types.h>

#include <uk/print.h>

/* TODO: in future give spin as well as wait options to the user */

#define FLAG_BITS		(5)
#define READERS_SHIFT		(FLAG_BITS)
#define FLAG_MASK		((1 << READERS_SHIFT) - 1)

#define UK_RWLOCK_READ		(0x01)
#define UK_RWLOCK_READ_WAITERS		(0x02)
#define UK_RWLOCK_WRITE_WAITERS		(0x04)
#define UK_RWLOCK_WAITERS		(UK_RWLOCK_READ_WAITERS | UK_RWLOCK_WRITE_WAITERS)

#define UK_RW_READERS_LOCK(x)		((x) << READERS_SHIFT | UK_RWLOCK_READ)
#define UK_RW_ONE_READER		(1 << READERS_SHIFT)
#define UK_RW_READERS(x)		((x) >> READERS_SHIFT)

#define UK_RW_UNLOCK		(UK_RW_READERS_LOCK(0))
#define OWNER(x)		(x & ~FLAG_MASK)

#define _rw_can_read(rwlock) \
	((rwlock & (UK_RWLOCK_READ | UK_RWLOCK_WRITE_WAITERS)) == UK_RWLOCK_READ)

#define _rw_can_write(rwlock) \
	((rwlock & ~(UK_RWLOCK_WAITERS)) == UK_RW_UNLOCK)

#define _rw_can_upgrade(rwlock) \
	((rwlock & UK_RWLOCK_READ) && UK_RW_READERS(rwlock) == 1)

#include <uk/spinlock.h>

uk_spinlock prsl = UK_SPINLOCK_INITIALIZER();

#define CRITICAL(...) ({ \
		uk_spin_lock(&prsl); \
		uk_pr_crit(__VA_ARGS__); \
		uk_spin_unlock(&prsl); \
	})


struct __align(8) uk_rwlock {
	uintptr_t rwlock;
	unsigned int write_recurse;
	struct uk_waitq shared;
	struct uk_waitq exclusive;
};

void uk_rwlock_init(struct uk_rwlock *rwl);

static inline
void uk_rwlock_rlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv;

	CRITICAL("Reader lock called\n");
	for (;;) {

		/* Try to increment the lock until we are in the read mode */
		v = rwl->rwlock;
		if (_rw_can_read(v)) {
			setv = v + UK_RW_ONE_READER;
			if (ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv)
				break;
			continue;
		}

		/* Set the read waiter flag if previously it is unset */
		if (!(rwl->rwlock & UK_RWLOCK_READ_WAITERS))
			ukarch_or(&rwl->rwlock, UK_RWLOCK_READ_WAITERS);

		/* Wait for the unlock event */
		CRITICAL("Reader lock sleeping\n");
		uk_waitq_wait_event(&rwl->shared, _rw_can_read(rwl->rwlock));
	}
	CRITICAL("Reader lock acquired\n");
	return;
}

static inline
void uk_rwlock_wlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, stackbottom;
	stackbottom = uk_get_stack_bottom();

	v = rwl->rwlock;

	/* If the lock is not held by reader and owner of the lock is current
	 * thread then simply increment the write recurse count
	 */
	CRITICAL("Writer lock called\n");
	if (!(v & UK_RWLOCK_READ) && OWNER(v) == stackbottom) {
		CRITICAL("Recursive writer lock\n");
		ukarch_inc(&(rwl->write_recurse));
		return;
	}

	for (;;) {

		/* If there is no reader or no owner and read flag is set then we can
		 * acquire that lock
		 * Try to set the owner field and flag mask except reader flag
		 */
		v = rwl->rwlock;
		setv = stackbottom | (v & UK_RWLOCK_WAITERS);

		if (_rw_can_write(v)) {
			if (ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv) {
				ukarch_inc(&(rwl->write_recurse));
				break;
			}
			continue;
		}

		/* If the acquire operation fails set the write waiters flag*/
		ukarch_or(&rwl->rwlock, UK_RWLOCK_WRITE_WAITERS);

		/* Wait for the unlock event */
		CRITICAL("Writer lock sleeping\n");
		uk_waitq_wait_event(&rwl->exclusive, _rw_can_write(rwl->rwlock));
	}
	CRITICAL("Writer lock acquired\n");
	return;
}


/* FIXME: lost wakeup problem
 *			one solution: use deadline base waking up
 */
static inline
void uk_rwlock_runlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv;
	struct uk_waitq *queue = NULL;

	v = rwl->rwlock;

	CRITICAL("Reader unlocked called\n");
	if (!(v & UK_RWLOCK_READ) || UK_RW_READERS(v) == 0) {
		CRITICAL("Cant unlocked reader lock\n");
		return;
	}

	for (;;) {

		/* Decrement the reader count and unset the waiters*/
		setv = (v - UK_RW_ONE_READER) & ~(UK_RWLOCK_WAITERS);

		/* If there are waiters then select the appropriate queue
		 * Since we are giving priority try to select writer's queue
		 */
		if (UK_RW_READERS(setv) == 0 && (v & UK_RWLOCK_WAITERS)) {
			queue = &rwl->shared;
			if (v & UK_RWLOCK_WRITE_WAITERS) {
				CRITICAL("Writers selected\n");
				setv |= (v & UK_RWLOCK_READ_WAITERS);
				queue = &rwl->exclusive;
			}
		} else {
			setv |= (v & UK_RWLOCK_WAITERS);
		}

		/* Try to unlock*/
		if (ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv)
			break;

		v = rwl->rwlock;
	}
	CRITICAL("Reader unlocked\n");
	/* wakeup the relevent queue */
	if (queue) {
		CRITICAL("Waking up the queue\n");
		uk_waitq_wake_up(queue);
	}
	return;
}

static inline
void uk_rwlock_wunlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, stackbottom;
	struct uk_waitq *queue;

	v = rwl->rwlock;
	stackbottom = uk_get_stack_bottom();
	queue = NULL;

	CRITICAL("Writer unlocked called\n");

	if ((v & UK_RWLOCK_READ) || OWNER(v) != stackbottom) {
		CRITICAL("Cant unlock writer");
		return;
	}

	/* Handle recursion
	 * If the number of recursive writers are not zero then return
	 */
	if (ukarch_sub_fetch(&(rwl->write_recurse), 1) != 0) {
		CRITICAL("Recursive writer unlock");
		return;
	}

	/* All recusive locks have been released, time to unlock*/
	for (;;) {

		setv = UK_RW_UNLOCK;

		/* Check if there are waiters, if yes select the appropriate queue */
		if (v & UK_RWLOCK_WAITERS) {
			queue = &rwl->shared;
			if (v & UK_RWLOCK_WRITE_WAITERS) {
				CRITICAL("Writers selected\n");
				setv |= (v & UK_RWLOCK_READ_WAITERS);
				queue = &rwl->exclusive;
			}
		}

		/* Try to set the lock */
		if (ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv)
			break;
		v = rwl->rwlock;
	}

	CRITICAL("Writer Unlocked\n");

	/* Wakeup the waiters */
	if (queue) {
		CRITICAL("Waking up the queue\n");
		uk_waitq_wake_up(queue);
	}

	return;
}


static inline
void uk_rwlock_upgrade(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, stackbottom;
	stackbottom = uk_get_stack_bottom();
	v = rwl->rwlock;

	CRITICAL("Upgrade called\n");

	/* If there are no readers then upgrade is invalid */
	if (UK_RW_READERS(v) == 0) {
		CRITICAL("Cant upgrade the lock\n");
		return;
	}

	for (;;) {

		/* Try to set the owner and relevent waiter flags */
		setv = stackbottom | (v & UK_RWLOCK_WAITERS);
		v = rwl->rwlock;

		if (_rw_can_upgrade(v)) {
			if (ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv) {
				ukarch_inc(&(rwl->write_recurse));
				break;
			}
			continue;
		}
		/* If we cannot upgrade wait till readers count is 0 */
		ukarch_or(&rwl->rwlock, UK_RWLOCK_WRITE_WAITERS);
		CRITICAL("Sleeping for upgrade\n");
		uk_waitq_wait_event(&rwl->exclusive, _rw_can_upgrade(rwl->rwlock));
	}
	CRITICAL("Upgraded the lock\n");
	return;
}

static inline
void uk_rwlock_downgrade(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, stackbottom;

	stackbottom = uk_get_stack_bottom();
	v = rwl->rwlock;

	CRITICAL("Downgrade called\n");

	if ((v & UK_RWLOCK_READ) || OWNER(v) != stackbottom) {
		CRITICAL("Cant downgrade the lock\n");
		return;
	}

	rwl->write_recurse = 0;

	for (;;) {
		/* Set only write waiter flags since we are waking up the reads */
		setv = (UK_RW_UNLOCK + UK_RW_ONE_READER)
				| (v & UK_RWLOCK_WRITE_WAITERS);

		if (ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv)
			break;

		v = rwl->rwlock;
	}
	CRITICAL("Downgraded successfully\n");
	uk_waitq_wake_up(&rwl->shared);
	return;
}

#endif /* __UK_RWLOCK_H__ */
