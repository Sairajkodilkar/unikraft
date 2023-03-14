#include <uk/arch/atomic.h>
#include <uk/spinlock.h>
#include <uk/mutex.h>
#include <uk/semaphore.h>
#include <uk/rwlock.h>
#include <uk/plat/lcpu.h>
#include <uk/sched.h>
#include <uk/schedcoop.h>
#include <stdlib.h>

#define STACK_SIZE (4096)

unsigned long count = 0;
unsigned int pcnt;
unsigned int completed = 0;
unsigned int completed1 = 0;

struct uk_spinlock cnt_lock_spin = UK_SPINLOCK_INITIALIZER();
struct uk_mutex cnt_lock_mutex = UK_MUTEX_INITIALIZER(cnt_lock_mutex);
struct uk_semaphore cnt_lock_semaphore;
struct uk_rwlock cnt_lock_rw;

static inline void sync_cpus(unsigned int *var, unsigned int cnt)
{
	ukarch_inc(var);
	while(ukarch_load_n(var) != cnt)
		;
}

struct uk_spinlock sched_lock = UK_SPINLOCK_INITIALIZER();

void __noreturn processor_read()
{
	uk_spin_lock(&sched_lock);
	struct uk_sched *s;
	struct uk_alloc *a = uk_alloc_get_default();
	uk_spin_unlock(&sched_lock);

	s = uk_schedcoop_create(a);
	uk_sched_start(s);

	sync_cpus(&completed1, pcnt);

	uk_rwlock_rlock(&cnt_lock_rw);
	uk_pr_debug("count is %ld\n", count);
	uk_rwlock_runlock(&cnt_lock_rw);

	sync_cpus(&completed, pcnt);

	while(1);
}

void __noreturn processor_write()
{
	uk_spin_lock(&sched_lock);
	struct uk_sched *s;
	struct uk_alloc *a = uk_alloc_get_default();
	uk_spin_unlock(&sched_lock);

	s = uk_schedcoop_create(a);
	uk_sched_start(s);

	sync_cpus(&completed1, pcnt);

	uk_rwlock_wlock(&cnt_lock_rw);
	for(int i = 0; i < 10000; i++) {
		for (int i = 0; i < 1000; i++) {
			//uk_spin_lock(&cnt_lock_spin);
			count = count + 1000;
			//uk_spin_unlock(&cnt_lock_spin);
		}
	}
	uk_rwlock_downgrade(&cnt_lock_rw);
	uk_rwlock_runlock(&cnt_lock_rw);

	sync_cpus(&completed, pcnt);

	while(1);
}

int main() {
	unsigned int num = 1;
	unsigned int lcpuidx;
	void *sp;
	ukplat_lcpu_entry_t entry[] = { processor_write };

	uk_rwlock_init(&cnt_lock_rw);

	pcnt = ukplat_lcpu_count();

	lcpuidx = 1;
	for(lcpuidx = 1; lcpuidx < pcnt; lcpuidx++) {
		sp = (char *) malloc(STACK_SIZE) + STACK_SIZE;
		if(lcpuidx > pcnt / 2)
			entry[0] = processor_read;
		ukplat_lcpu_start(&lcpuidx, &num, &sp, &entry[0], 0);
	}

	sync_cpus(&completed1, pcnt);
	sync_cpus(&completed, pcnt);

	if(count != 1000LU * 1000LU * 10000LU * 3LU) 
		uk_pr_err("Test failed %lu\n", count);
	else
		uk_pr_warn("Test Passed\n");

	return 0;
}
