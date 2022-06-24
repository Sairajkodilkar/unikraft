#include <uk/rwlock.h>

void uk_rwlock_init(struct uk_rwlock *rwl)
{
	UK_ASSERT(rwl);

	rwl->rwlock = UK_RWLOCK_UNLOCK;
	rwl->write_recurse = 0;
	uk_waitq_init(&shared);
	uk_waitq_init(&exclusive);
}
