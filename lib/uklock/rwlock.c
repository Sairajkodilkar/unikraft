#include <uk/rwlock.h>

void uk_rwlock_init(struct uk_rwlock *rwl)
{
	//UK_ASSERT(rwl);

	rwl->rwlock = UK_RW_UNLOCK;
	rwl->write_recurse = 0;
	uk_waitq_init(&rwl->shared);
	uk_waitq_init(&rwl->exclusive);
}
