#pragma once

/************************************************
 * UNIT_ASYNC
 * Description:
 * 	Emulates asynchronous function flow
 ***********************************************/
#include <stddef.h>
typedef ptrdiff_t async;

/** Init before calling function that uses async state. */
void async_init(async *self) { *self = (ptrdiff_t)NULL; }

/** Tells us if async function has returned. */
int async_has_returned(async *self) { return *self == (ptrdiff_t)NULL; }

/* Ignore gcc standart-specification warnings. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

/* Trick used to concantenate label name with __LINE__ */
#define ASYNC_CAT1(a, b) a##b
#define ASYNC_CAT(a, b) ASYNC_CAT1(a, b)

#define ASYNC_LABEL(act)                                                       \
	*_state = &&ASYNC_CAT(_l, __LINE__);                                   \
	act;                                                                   \
	ASYNC_CAT(_l, __LINE__) :

/** Should be placed at beginning of every async function.
 * 	Dispatches to the last saved state. */
#define ASYNC_DISPATCH(state)                                                  \
	void **_state;		   /* Computed goto state. */                  \
	async  _async_call_result; /* For asynchronous calls. */               \
	(void)_async_call_result;                                              \
                                                                               \
	_state = (void **)&state;                                              \
	if (*_state) {                                                         \
		goto **_state;                                                 \
	}
	
/** Returns from the async function, with saving state. */
#define ASYNC_YIELD(ret)                                                       \
	do {                                                                   \
		ASYNC_LABEL(return ret);                                       \
	} while (0)

/** Can be placed anywhere, saves state and yields until condition is true. */
#define ASYNC_AWAIT(cond, ret)                                                 \
	do {                                                                   \
		ASYNC_LABEL();                                                 \
		if (!(cond))                                                   \
			return ret;                                            \
	} while (0)

/** Used to return from function safely. Resets execution. */
#define ASYNC_RETURN(ret)                                                      \
	do {                                                                   \
		*_state = NULL;                                                \
		return ret;                                                    \
	} while (0)

/** Calls asynchronous function within another asynchronous function.
 *  The main function gets blocked until execution is end. */
#define ASYNC_CALL(func, state)                                                \
	do {                                                                   \
		_async_call_result = func;                                     \
		if (!state)                                                    \
			break;                                                 \
                                                                               \
		ASYNC_YIELD(_async_call_result);                               \
	} while (true)

/** When any asynchronous call is performed,
 *  current state becomes the call result. */
#define ASYNC_CALL_RESULT _async_call_result
