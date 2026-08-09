// spinlock.h - Simple spinlock that preserves the caller interrupt state.

#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

// Mutual exclusion lock.
struct spinlock {
  uint locked;        // is lock held?
  const char *name;   // name of lock.
};

void initlock(struct spinlock*, const char*);
void acquire(struct spinlock*);
void release(struct spinlock*);
int holding(struct spinlock*);

#endif // __SPINLOCK_H__
