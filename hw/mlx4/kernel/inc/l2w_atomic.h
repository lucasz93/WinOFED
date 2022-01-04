#pragma once

#include "complib/cl_atomic.h"

typedef volatile __int32	atomic_t;	/* as atomic32_t */

#define atomic_inc	cl_atomic_inc
#define atomic_dec	cl_atomic_dec

static inline atomic_t atomic_read(atomic_t *pval) 
{
	return *pval;	
}

static inline void atomic_set(atomic_t *pval, long val)
{
	*pval = (__int32)val;
}

/**
* atomic_inc_and_test - increment and test
* pval: pointer of type atomic_t
* 
* Atomically increments pval by 1 and
* returns true if the result is 0, or false for all other
* cases.
*/ 
static inline int
atomic_inc_and_test(atomic_t *pval)
{ 
	return cl_atomic_inc(pval) == 0;
}

/**
* atomic_dec_and_test - decrement and test
* pval: pointer of type atomic_t
* 
* Atomically decrements pval by 1 and
* returns true if the result is 0, or false for all other
* cases.
*/ 
static inline int
atomic_dec_and_test(atomic_t *pval)
{ 
	return cl_atomic_dec(pval) == 0;
}


/**
* atomic_dec_return - decrement and return the value
* pval: pointer of type atomic_t
* 
* Atomically decrements pval by 1 and retruns the new value
*/ 
static inline int
atomic_dec_return(atomic_t *pval)
{ 
	return cl_atomic_dec(pval);
}

