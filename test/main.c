#include <stdlib.h>
#include <stdio.h>

#include <uk/plat/lcpu.h>

#ifdef CONFIG_TEST_SPINLOCK
#include <uk/spinlock.h>
#endif

#ifdef CONFIG_TEST_MUTEX
#include <uk/mutex.h>
#endif

#ifdef CONFIG_TEST_SEMAPHORE
#include <uk/semaphore.h>
#endif

#include <uk/config.h>

#define STACK_SIZE (4096)


int count = 0;
int completed = 0;

#ifdef CONFIG_TEST_SPINLOCK
	uk_spinlock sl;
	#define INIT() uk_spin_init(&sl);
	#define LOCK() uk_spin_lock(&sl);
	#define UNLOCK() uk_spin_unlock(&sl);
#elif CONFIG_TEST_MUTEX
	struct uk_mutex mtx;
	#define INIT() uk_mutex_init(&mtx);
	#define LOCK() uk_mutex_lock(&mtx);
	#define UNLOCK() uk_mutex_unlock(&mtx);
#elif CONFIG_TEST_SEMAPHORE
	struct uk_semaphore s;
	#define INIT() uk_semaphore_init(&s, 1);
	#define LOCK() uk_semaphore_down(&s);
	#define UNLOCK() uk_semaphore_up(&s);
#endif


void critical_section() {
	for(int i = 0; i < 1000000; i++) {
		LOCK();
		count++;
		UNLOCK();
	}
	LOCK();
	completed += 1;
	UNLOCK();
}

void __noreturn entry1() {
	critical_section();
	while(1);
}

int main() {

	INIT();
	__lcpuidx lcpuidx[] = {1, 2};
	unsigned int num = 2;

	void *sp1 = (char *) malloc(STACK_SIZE) + STACK_SIZE;
	void *sp2 = (char *) malloc(STACK_SIZE) + STACK_SIZE;

	void *sp[] = {sp1, sp2};

	ukplat_lcpu_entry_t entry[] = {entry1, entry1};

	ukplat_lcpu_start(lcpuidx, &num, sp, entry, 0);

	critical_section();
	while(completed != 3);

	printf("%d\n", count);
}
