#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * # Ivy Bridge
 *
 * ## single
 *
 * fat lock:                  2.52s
 * thin lock, no CAS on exit: 2.26s
 * thin lock, halfword exit:  3.08s
 * thin lock, CAS on exit:    4.23s
 *
 * ## nested
 *
 * fat lock:                  0.82s
 * thin lock, no CAS on exit: 1.27s
 * thin lock, halfword exit:  2.21s
 * thin lock, CAS on exit:    3.02s
 *
 * # Cortex-A9
 *
 * ## single
 *
 * fat lock:                  19.95s
 * thin lock, no CAS on exit: 19.05s
 * thin lock, halfword exit:  18.90s
 * thin lock, CAS on exit:    30.35s
 *
 * ## nested
 *
 * fat lock:                  7.62s
 * thin lock, no CAS on exit: 8.77s
 * thin lock, halfword exit:  7.33s
 * thin lock, CAS on exit:    14.26s
 */

//#define FAT_LOCK
//#define CAS_ON_EXIT
//#define HALFWORD_EXIT

#ifdef FAT_LOCK
typedef struct {
	volatile uintptr_t owner;
	volatile uintptr_t nest;
	volatile uintptr_t entry_count;
} fat_lock_t;

typedef struct {
	fat_lock_t *lock;
} monitor_t;

static void
monitor_init (monitor_t *m)
{
	m->lock = malloc (sizeof (fat_lock_t));
	assert (m->lock);
	m->lock->owner = 0;
	m->lock->nest = 0;
	m->lock->entry_count = 0;
}

static void __attribute__ ((noinline))
monitor_enter (uintptr_t self, monitor_t *m)
{
	fat_lock_t *lock = m->lock;
	if (!lock)
		assert (false);
	for (;;) {
		uintptr_t owner = lock->owner;
		if (owner == 0) {
			if (__sync_bool_compare_and_swap (&lock->owner, 0, self)) {
				lock->nest = 1;
				break;
			}
		} else if (owner == self) {
			++lock->nest;
			break;
		} else {
			assert (false);
		}
	}
}

static void __attribute__ ((noinline))
monitor_exit (uintptr_t self, monitor_t *m)
{
	fat_lock_t *lock = m->lock;
	uintptr_t nest;

	if (!lock)
		assert (false);

	nest = lock->nest;

	assert (lock->owner == self);
	assert (nest > 0);

	if (nest == 1) {
		lock->owner = 0;
		if (lock->entry_count) {
			assert (false);
		}
	} else {
		lock->nest = nest - 1;
	}
}
#else
typedef struct {
	volatile uintptr_t sync;
} monitor_t;

#define MAKE_SYNC(o,n)	(((n) << 16) | ((o) << 3))

#define SYNC_OWNER(s)	(((s) >> 3) & 0x1fff)
#define SYNC_NEST(s)	((s) >> 16)

static void
monitor_init (monitor_t *m)
{
	m->sync = 0;
}

#ifdef HALFWORD_EXIT
static void __attribute__ ((noinline))
monitor_enter (uintptr_t self, monitor_t *m)
{
	uintptr_t sync = m->sync;

	for (;;) {
		uintptr_t nest = SYNC_NEST (sync);
		uintptr_t new_sync;
		if (nest == 0) {
			new_sync = MAKE_SYNC (self, 1);
			new_sync = __sync_val_compare_and_swap (&m->sync, sync, new_sync);
			if (new_sync == sync)
				break;
			sync = new_sync;
		} else if (SYNC_OWNER (sync) == self) {
			*((uint16_t*)&m->sync + 1) = nest + 1;
			break;
		} else {
			assert (false);
		}
	}
}

static void __attribute__ ((noinline))
monitor_exit (uintptr_t self, monitor_t *m)
{
	uintptr_t sync = m->sync;

	uintptr_t nest = SYNC_NEST (sync);

	assert (SYNC_OWNER (sync) == self);
	assert (nest > 0);

	*((uint16_t*)&m->sync + 1) = nest - 1;
}
#else
static void __attribute__ ((noinline))
monitor_enter (uintptr_t self, monitor_t *m)
{
	uintptr_t sync = m->sync;

	for (;;) {
		uintptr_t nest = SYNC_NEST (sync);
		uintptr_t new_sync;
		if (nest == 0) {
			new_sync = MAKE_SYNC (self, 1);
			new_sync = __sync_val_compare_and_swap (&m->sync, sync, new_sync);
			if (new_sync == sync)
				break;
			sync = new_sync;
		} else if (SYNC_OWNER (sync) == self) {
			new_sync = MAKE_SYNC (self, nest + 1);
			m->sync = new_sync;
			break;
		} else {
			assert (false);
		}
	}
}

static void __attribute__ ((noinline))
monitor_exit (uintptr_t self, monitor_t *m)
{
	uintptr_t sync = m->sync;

	for (;;) {
		uintptr_t nest = SYNC_NEST (sync);
		uintptr_t new_sync;

		assert (SYNC_OWNER (sync) == self);
		assert (nest > 0);
		if (nest == 1)
			new_sync = 0;
		else
			new_sync = MAKE_SYNC (self, nest - 1);
#ifdef CAS_ON_EXIT
		new_sync = __sync_val_compare_and_swap (&m->sync, sync, new_sync);
		if (new_sync == sync)
			break;
		sync = new_sync;
#else
		m->sync = new_sync;
		break;
#endif
	}
}
#endif
#endif

static void
bench_single (void)
{
	uintptr_t self = 1;
	monitor_t m;

	monitor_init (&m);

	for (int i = 0; i < 200000000; ++i) {
		monitor_enter (self, &m);
		monitor_exit (self, &m);
	}
}

static void
bench_nested (void)
{
	uintptr_t self = 1;
	monitor_t m;

	monitor_init (&m);

	for (int i = 0; i < 20000000; ++i) {
		for (int j = 0; j < 10; ++j)
			monitor_enter (self, &m);
		for (int j = 0; j < 10; ++j)
			monitor_exit (self, &m);
	}
}

int
main (int argc, char *argv[])
{
	assert (argc == 2);
	if (!strcmp (argv [1], "single"))
		bench_single ();
	else if (!strcmp (argv [1], "nested"))
		bench_nested ();
	else
		assert (false);
	return 0;
}
