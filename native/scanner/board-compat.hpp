#pragma once
// GCC12 language/library headers with the board's glibc 2.17. Use the existing
// realtime condvar implementation, not the glibc 2.30 clockwait entry point.
#include <bits/c++config.h>
#undef _GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT
#undef _GLIBCXX_USE_PTHREAD_MUTEX_CLOCKLOCK
#undef _GLIBCXX_USE_PTHREAD_RWLOCK_CLOCKLOCK
