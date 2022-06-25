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
#define UK_RWLOCK_WAITERS			(UK_RWLOCK_READ_WAITERS | UK_RWLOCK_WRITE_WAITERS)

#define UK_RW_READERS_LOCK(x)		((x) << READERS_SHIFT | UK_RWLOCK_READ)
#define UK_RW_ONE_READER			(1 << READERS_SHIFT)
#define UK_RW_READERS(x)			((x) >> READERS_SHIFT)

#define UK_RW_UNLOCK				(UK_RW_READERS_LOCK(0))
#define OWNER(x)					(x & ~FLAG_MASK)

struct uk_rwlock {
	volatile uintptr_t rwlock __aligned(8);
	unsigned int write_recurse;
	struct uk_waitq shared;
	struct uk_waitq exclusive;
};

void uk_rwlock_init(struct uk_rwlock *rwl);

static inline 
bool _rw_can_read(unsigned int rwlock)
{
	/* Give priority to the writers */
	if(rwlock & (UK_RWLOCK_READ | UK_RWLOCK_WRITE_WAITERS 
				| UK_RWLOCK_WRITE_SPINNERS) == UK_RWLOCK_READ) {
		/* Success when there are no write pending */
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

		while(_rw_can_read(v)) {
			setv = v + UK_RW_ONE_READER;
			if(ukarch_compare_exchange_sync(&rwl->lock, v, setv) == setv) {
				break;
			}
		}

		/* Either the writer has the lock or the writer is waiting */

		if(!(v & UK_RWLOCK_READ_WAITERS)) {
			setv = v | UK_RWLOCK_READ_WAITERS;
			while(ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) != setv) {
				v = rwl->rwlock;
				setv = v | UK_RWLOCK_READ_WAITERS;
			}
		}

		uk_waitq_wait_event(&rwl->shared, _rw_can_read(rwl->rwlock));
		v = rwl->rwlock;
	}
	return;
}

static inline
void uk_rwlock_wlock(struct uk_rwlock *rwl)
{
	uintptr_t v, setv, mask;
	uintptr_t stackbottom = uk_get_stack_bottom();

	v = rwl->rwlock;

	/* If the lock is not held by reader and owner of the lock is current
	 * thread then simply increment the write recurse count
	 */
	if(~(v & UK_RWLOCK_READ) && OWNER(v) == stackbottom) {
		ukarch_inc(&(rwl->write_recurse));
		return;
	}

	for(;;) {

		mask = v & (UK_RWLOCK_WAITERS | UK_RWLOCK_WRITE_SPINNERS | UK_RWLOCK_WRITE_RECURSED);

		/* If there is no reader or no owner and read flag is set then we can
		 * acquire that lock
		 */
		if(v & ~mask == UK_RW_UNLOCK) {
			setv = mask | stackbottom;
			/* Try to set the owner field and flag mask except reader flag*/
			if(ukarch_compare_exchange_sync(&rwl->lock, v, setv) == setv) {
				ukarch_inc(&(rwl->write_recurse));
				break;
			}
		}

		if(!(v & UK_RWLOCK_WRITE_WAITERS)) {
			setv = v | UK_RWLOCK_WRITE_WAITERS;
			while(ukarch_compare_exchange_sync(&rwl->lock, v, setv) != setv) {
				v = rwl->rwlock;
				setv = v | UK_RWLOCK_WRITE_WAITERS;
			}
		}

		uk_waitq_wait_event(&rwl->exclusive, ((rwl->rwlock & ~mask) == UK_RW_UNLOCK));
		v = rwl->rwlock;
	}
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

	if(~(v & UK_RWLOCK_READ))
		return;

	for(;;) {
		setv = (v - UK_RW_ONE_READER) & ~(UK_RWLOCK_WAITERS);

		if(UK_RW_READERS(setv) == 0 && v & UK_RWLOCK_WAITERS) {
			queue = &rwl->shared;
			if(v & UK_RWLOCK_WRITE_WAITERS) {
				setv |= (v & UK_RWLOCK_READ_WAITERS);
				queue = &rwl->exclusive;
			}
		}

		if(ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv)
			break;

		v = rwl->rwlock;
	}

	uk_waitq_wake_up(queue);

	return;
}

static inline
void uk_rwlock_wunlock(struct uk_rwlock *rwl)
{
	uintptr_t v, stackbottom; 
	v = rwl->rwlock;
	stackbottom = uk_get_stack_bottom();

	if((v & UK_RWLOCK_READ) || OWNER(v) != stackbottom)
		return;

	/* Handle recursion */
	if(ukarch_sub_fetch(&(rwl->write_recurse), 1) != 0)
		return;

	/* All recusive locks have been released, time to unlock*/
	for(;;) {
		setv = UK_RW_UNLOCK | 
			(v & (UK_RWLOCK_WRITE_SPINNERS | UK_RWLOCK_WRITE_RECURSED));

		if(v & UK_RWLOCK_WRITE_WAITERS) {
			setv |= (v & UK_RWLOCK_READ_WAITERS);
			queue = &rwl->exclusive;
		} else if(v & UK_RWLOCK_READ_WAITERS){
			queue = &rwl->shared;
		}

		if(ukarch_compare_exchange_sync(&rwl->rwlock, v, setv) == setv)
			break;

		v = rwl->rwlock;
	}

	uk_waitq_wake_up(queue);
}

static inline void uk_rwlock_upgrade(struct uk_rwlock *rwl);
static inline void uk_rwlock_downgrade(struct uk_rwlock *rwl);

#endif /* __UK_RWLOCK_H__ */
