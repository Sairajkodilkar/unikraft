#include <stdlib.h>
#include <stdio.h>

#include <uk/plat/lcpu.h>

#ifdef CONFIG_TEST_SPINLOCK
#include <uk/spinlock.h>
#endif

#include <uk/mutex.h>

#ifdef CONFIG_TEST_SEMAPHORE
#include <uk/semaphore.h>
#endif

#include <uk/config.h>

#define STACK_SIZE (4096)


int count = 0;
int completed = 0;

	struct uk_mutex mtx;
	#define INIT() uk_mutex_init(&mtx);
	#define LOCK() uk_mutex_lock(&mtx);
	#define UNLOCK() uk_mutex_unlock(&mtx);


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

int main() {

	INIT();

	critical_section();

	if(ukplat_lcpu_id() == 0) {
		while(completed != 4);
		printf("%d\n", count);
	}
}
