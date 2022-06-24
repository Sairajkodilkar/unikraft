#ifndef __UK_RWLOCK_H__
#define __UK_RWLOCK_H__

/* Requirements:
 *	Owner field to support the recursive lock
 *	Read count
 *	Is reader or Is writer
 */
/* TODO: in future give spin as well as wait options to the user 
 */

#define READERS_SHIFT				(5)
#define FLAG_MASK					(~(1 << READERS_SHIFT))

#define UK_RWLOCK_READ				(0x01)
#define UK_RWLOCK_READ_WAITERS		(0x02)
#define UK_RWLOCK_WRITE_WAITERS		(0x04)
#define UK_RWLOCK_WRITE_SPINNERS	(0x08)
#define UK_RWLOCK_WRITE_RECURSED	(0x10)

#define UK_RW_READERS_LOCK(x)		((x) << READERS_SHIFT | UK_RWLOCK_READ)

#define UK_RW_UNLOCK				(UK_RW_READERS_LOCK(0))
#define OWNER(x)					(x & ~FLAG_MASK)

struct uk_rwlock {
	/* TODO: discuss that should we store struc thread ** since it would give us
	 * 12 free bits which can be used for storing flags resulting in lower
	 * memory usage
	 */
	volatile uintptr_t rwlock;
	unsigned int write_recurse;
	struct uk_waitq shared;
	struct uk_waitq exclusive;
};

void uk_rwlock_init(struct uk_rwlock *rwl);

static inline void uk_rwlock_rlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv;

	v = rwl->rwlock;

	for(;;) {

		if(v & UK_RWLOCK_READ) {
			setv = v + RW_ONE_READER;
			if(ukarch_compare_exchange_sync(&rwl->lock, v, setv) == setv) {
				//TODO: change flags here
				break;
			}
		}

		/* TODO: handle waiter flag */
		uk_waitq_wait_event(&rwl->shared, (rwl->rwlock & UK_RWLOCK_READ));

		v = READ_ONCE(rwl->rwlock);
	}
	return;
}

static inline void uk_rwlock_wlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, mask;
	uintptr_t stackbottom = uk_get_stack_bottom();

	v = rwl->rwlock;

	if(~(v & UK_RWLOCK_READ) && unlikely(OWNER(v) == stackbottom)) {
		ukarch_inc(rwl->write_recurse);
		return;
	}

	for(;;) {

		mask = v & (UK_RWLOCK_WAITERS & UK_RWLOCK_WRITE_SPINNERS);
		setv = mask | stackbottom | ~UK_RWLOCK_READ;

		if(v & ~mask == UK_RW_UNLOCK) {
			if(ukarch_compare_exchange_sync(&rwl->lock, v, setv) == setv) {
				break;
			}
		}
		/*TODO: handle waiter flags */
		uk_waitq_wait_event(&rwl->exclusive, ((rwl->rwlock & ~mask) == UK_RW_UNLOCK))
		v = rwl->rwlock;
	}
}

static inline void uk_rwlock_runlock(struct uk_rwlock *rwl);
static inline void uk_rwlock_wunlock(struct uk_rwlock *rwl);

static inline void uk_rwlock_upgrade(struct uk_rwlock *rwl);
static inline void uk_rwlock_downgrade(struct uk_rwlock *rwl);

#endif /* __UK_RWLOCK_H__ */
