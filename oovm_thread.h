/** ************************************************************************
 *
 * \file oovm_thread.h
 * \brief OVM threads
 *
 ***************************************************************************/

#ifndef __OOVM_THREAD_H
#define __OOVM_THREAD_H

#include "oovm_types.h"

/**
 * \defgroup Threads Threads
 */
/**@{*/

/** \brief Thread fatal error exit codes */
enum {
    OVM_THREAD_FATAL_ABORTED = 0xe0, /**< Thread called System.abort() */
    OVM_THREAD_FATAL_ASSERT_FAILED, /**< Thread called System.assert(), and assertion failed */
    OVM_THREAD_FATAL_INVALID_OPCODE, /**< Invalid opcde */
    OVM_THREAD_FATAL_STACK_OVERFLOW,  /**< Instance stack overflow */
    OVM_THREAD_FATAL_STACK_UNDERFLOW, /**< Instance stack underflow */
    OVM_THREAD_FATAL_FRAME_STACK_OVERFLOW, /**< Frame stack overflow */
    OVM_THREAD_FATAL_FRAME_STACK_UNDERFLOW, /**< Frame stack underflow */
    OVM_THREAD_FATAL_NO_FRAME, /**< Required frame not on frame stack */
    OVM_THREAD_FATAL_UNCAUGHT_EXCEPT, /**< Exception raised but not caught */
    OVM_THREAD_FATAL_DOUBLE_EXCEPT /**< Exception during exception processing */
};

/**
 * \brief Create thread
 *
 * Create a new thread, with the given stack sizes.
 * This call only allocates spaces for the thread, it does not start it running.
 *
 * \param[in] stack_size Size of instance stack
 * \param[in] frame_stack_size Size frame stack, in bytes
 *
 * \return Cookie for new thread
 */
ovm_thread_t ovm_thread_create(unsigned stack_size, unsigned frame_stack_size);

/**
 * \brief Thread entry point
 *
 * The entry point function for a thread, called from pthread_create().
 * It expects the instance stack to by laid out as follows:
 *
\verbatim
sp-> entry method arg 1
     entry method arg 2
     ...
     entry method arg n
     slot for result of entry method
     entry method
     namespace in which to run method
--- Top of stack ---
\endverbatim
 *
 * \param[in] arg Thread cookie
 *
 * \return If the entry method returns an Integer, this functions returns that value; otherwise, returns nil.
 */
void *ovm_thread_entry(void *arg);

/**@}*/

#endif /* __OOVM_THREAD_H */
