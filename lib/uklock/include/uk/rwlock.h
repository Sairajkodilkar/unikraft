#ifndef __UK_RWLOCK_H__
#define __UK_RWLOCK_H__

/* Requirements:
 *	Owner field to support the recursive lock
 *	Read count
 *	Is reader or Is writer
 */
/* TODO: in future give spin as well as wait options to the user 
 */

#define FLAG_BITS		(5)
#define READERS_SHIFT		(FLAG_BITS)
#define FLAG_MASK		(~(1 << READERS_SHIFT))

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
	rwlock & (UK_RWLOCK_READ | UK_RWLOCK_WRITE_WAITERS) == UK_RWLOCK_READ

#define _rw_can_write(v) \
	((v & ~(UK_RWLOCK_WAITERS)) == UK_RW_UNLOCK);

struct uk_rwlock __aligned(8) {
	volatile uintptr_t rwlock;
	unsigned int write_recurse;
	struct uk_waitq shared;
	struct uk_waitq exclusive;
};

void uk_rwlock_init(struct uk_rwlock *rwl);

static inline
void _rwlock_set_flag(uintptr_t *rwlock, uintptr_t flag) {

	for(;;) {
		v = *rwlock;
		setv = v | flag;
		if((v & flag)
				|| ukarch_compare_exchange_sync(rwlock, v, setv) == setv)
			break;
	}
	return;
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

		/* Set the read waiter flag if previously it is unset */
		_rwlock_set_flag(&rwl->rwlock, UK_RWLOCK_READ_WAITERS);

		/* Wait for the unlock event */
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

		/* If there is no reader or no owner and read flag is set then we can
		 * acquire that lock
		 */
		if(_rw_can_write(v)) {
			setv = stackbottom | (v & UK_RWLOCK_WAITERS);
			/* Try to set the owner field and flag mask except reader flag*/
			if(ukarch_compare_exchange_sync(&rwl->lock, v, setv) == setv) {
				ukarch_inc(&(rwl->write_recurse));
				break;
			}
		}

		_rwlock_set_flag(&rwl->rwlock, UK_RWLOCK_WRITE_WAITERS);

		uk_waitq_wait_event(&rwl->exclusive, _rw_can_write(rwl->rwlock));
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
		setv = UK_RW_UNLOCK; 

		if(v & UK_RWLOCK_WAITERS) {
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
}

static inline void uk_rwlock_upgrade(struct uk_rwlock *rwl);
static inline void uk_rwlock_downgrade(struct uk_rwlock *rwl);

#endif /* __UK_RWLOCK_H__ */
