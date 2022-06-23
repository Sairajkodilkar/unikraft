#ifndef __UK_RWLOCK_H__
#define __UK_RWLOCK_H__

/* Requirements:
 *	Owner field to support the recursive lock
 *	Read count
 *	Is reader or Is writer
 */
struct uk_rwlock {
	/* TODO: discuss that should we store struc tread ** since it would give us
	 * 12 free bits which can be used for storing flags resulting in lower
	 * memory usage
	 */
	unsigned int *owner;
	unsigned char flags;
};

void uk_rwlock_init(struct uk_rwlock *rwl);

void uk_rwlock_rlock(struct uk_rwlock *rwl);
void uk_rwlock_runlock(struct uk_rwlock *rwl);

void uk_rwlock_wlock(struct uk_rwlock *rwl);
void uk_rwlock_wunlock(struct uk_rwlock *rwl);

void uk_rwlock_upgrade(struct uk_rwlock *rwl);
void uk_rwlock_downgrade(struct uk_rwlock *rwl);

#endif /* __UK_RWLOCK_H__ */
