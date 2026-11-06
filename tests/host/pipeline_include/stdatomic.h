/* Win32 mutex-backed atomic model for threaded pipeline tests. */
#pragma once
#include <windows.h>
typedef volatile LONG atomic_bool;
typedef volatile LONG atomic_uint;
LONG mock_atomic_load(volatile LONG *p);
LONG mock_atomic_exchange(volatile LONG *p,LONG value);
#define atomic_init(p,v) (*(p)=(v))
#define atomic_load(p) mock_atomic_load(p)
#define atomic_store(p,v) mock_atomic_exchange((p),(v))
#define atomic_exchange(p,v) mock_atomic_exchange((p),(v))
