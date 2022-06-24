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
	volatile uintptr_t rwlock;
	unsigned int write_recurse;
	struct uk_waitq shared;
	struct uk_waitq exclusive;
};

void uk_rwlock_init(struct uk_rwlock *rwl);

static inline 
bool _rw_can_read(unsigned int rwlock)
{
	if(rwlock & (UK_RWLOCK_READ | UK_RWLOCK_WRITE_WAITERS | UK_RWLOCK_WRITE_SPINNERS)
			== UK_RWLOCK_READ) {
		return true;
	}
	return false
}

static inline 
void uk_rwlock_rlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, rwait;

	v = rwl->rwlock;

	for(;;) {

		if(_rw_can_read(v)) {
			setv = v + RW_ONE_READER;
			if(ukarch_compare_exchange_sync(&rwl->lock, v, setv) == setv) {
				break;
			}
		}

		if(!(v & UK_RWLOCK_READ_WAITERS)) {
			rwait = v | UK_RWLOCK_READ_WAITERS;
			while(ukarch_compare_exchange_sync(&rwl->lock, v, rwait) != rwait) {
				v = rwl->rwlock;
				rwait = v | UK_RWLOCK_READ_WAITERS;
			}
		}

		uk_waitq_wait_event(&rwl->shared, (rwl->rwlock & UK_RWLOCK_READ));
		v = rwl->rwlock;
	}
	return;
}

static inline void uk_rwlock_wlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, mask;
	uintptr_t stackbottom = uk_get_stack_bottom();

	v = rwl->rwlock;

	if(~(v & UK_RWLOCK_READ) && unlikely(OWNER(v) == stackbottom)) {
		ukarch_inc(&(rwl->write_recurse));
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

		if(!(v & UK_RWLOCK_WRITE_WAITERS)) {
			rwait = v | UK_RWLOCK_WRITE_WAITERS;
			while(ukarch_compare_exchange_sync(&rwl->lock, v, rwait) != rwait) {
				v = rwl->rwlock;
				rwait = v | UK_RWLOCK_WRITE_WAITERS;
			}
		}

		uk_waitq_wait_event(&rwl->exclusive, ((rwl->rwlock & ~mask) == UK_RW_UNLOCK));
		v = rwl->rwlock;
	}
}

static inline void uk_rwlock_runlock(struct uk_rwlock *rwl);
static inline void uk_rwlock_wunlock(struct uk_rwlock *rwl);

static inline void uk_rwlock_upgrade(struct uk_rwlock *rwl);
static inline void uk_rwlock_downgrade(struct uk_rwlock *rwl);

#endif /* __UK_RWLOCK_H__ */
