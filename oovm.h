/** ************************************************************************
 *
 * \file oovm.h
 * \brief OVM library API
 *
 ***************************************************************************/

#ifndef __OOVM_H
#define __OOVM_H

#include "oovm_internal.h"

/**
 * \defgroup Builtin Built-in classes
 */
/**@{*/

/**
 * \struct ovm_consts
 * \brief Objects for all built-in classes
 */
extern struct ovm_consts {
    ovm_obj_t Metaclass;        /**< #Metaclass */
    ovm_obj_t Object;           /**< #Object */
    ovm_obj_t Boolean;          /**< #Boolen */
    ovm_obj_t Integer;          /**< #Integer */
    ovm_obj_t Float;            /**< #Float */
    ovm_obj_t Method;           /**< #Method */
    ovm_obj_t Codemethod;       /**< #Codemethod */
    ovm_obj_t String;           /**< #String */
    ovm_obj_t Pair;             /**< #Pair */
    ovm_obj_t List;             /**< #List */
    ovm_obj_t Array;            /**< #Array */
    ovm_obj_t Carray;           /**< #Carray */
    ovm_obj_t Bytearray;        /**< #Bytearray */
    ovm_obj_t Cbytearray;       /**< #Cbytearray */
    ovm_obj_t Slice;            /**< #Slice */
    ovm_obj_t Cslice;           /**< #Cslice */
    ovm_obj_t Byteslice;        /**< #Byteslice */
    ovm_obj_t Cbyteslice;       /**< #Cbyteslice */
    ovm_obj_t Set;              /**< #Set */
    ovm_obj_t Cset;             /**< #Cset */
    ovm_obj_t Dictionary;       /**< #Dictionary */
    ovm_obj_t Cdictionary;      /**< #Cdictionary */
    ovm_obj_t Namespace;        /**< #Namespace */
    ovm_obj_t File;             /**< #File */
    ovm_obj_t Module;           /**< #Module */
    ovm_obj_t Exception;        /**< #Exception */
    ovm_obj_t System;           /**< #System */
    ovm_obj_t User;             /**< #User */
    ovm_obj_t Environment;      /**< #Environment */
} ovm_consts;

/** \brief Convenience macro for #Metaclass */
#define OVM_METACLASS       (ovm_obj_class(ovm_consts.Metaclass))
/** \brief Convenience macro for #Object class */
#define OVM_CL_OBJECT       (ovm_obj_class(ovm_consts.Object))
/** \brief Convenience macro for #Boolean class */
#define OVM_CL_BOOLEAN      (ovm_obj_class(ovm_consts.Boolean))
/** \brief Convenience macro for #Integer class */
#define OVM_CL_INTEGER      (ovm_obj_class(ovm_consts.Integer))
/** \brief Convenience macro for #Float class */
#define OVM_CL_FLOAT        (ovm_obj_class(ovm_consts.Float))
/** \brief Convenience macro for #Codemethod class */
#define OVM_CL_CODEMETHOD   (ovm_obj_class(ovm_consts.Codemethod))
/** \brief Convenience macro for #Method class */
#define OVM_CL_METHOD       (ovm_obj_class(ovm_consts.Method))
/** \brief Convenience macro for #String class */
#define OVM_CL_STRING       (ovm_obj_class(ovm_consts.String))
  /** \brief Convenience macro for #Pair class */
#define OVM_CL_PAIR         (ovm_obj_class(ovm_consts.Pair))
  /** \brief Convenience macro for #List class */
#define OVM_CL_LIST         (ovm_obj_class(ovm_consts.List))
  /** \brief Convenience macro for #Array class */
#define OVM_CL_ARRAY        (ovm_obj_class(ovm_consts.Array))
  /** \brief Convenience macro for #Carray class */
#define OVM_CL_CARRAY       (ovm_obj_class(ovm_consts.Carray))
  /** \brief Convenience macro for #Bytearray class */
#define OVM_CL_BYTEARRAY    (ovm_obj_class(ovm_consts.Bytearray))
  /** \brief Convenience macro for #Cbytearray class */
#define OVM_CL_CBYTEARRAY   (ovm_obj_class(ovm_consts.Cbytearray))
/** \brief Convenience macro for #Slice class */
#define OVM_CL_SLICE        (ovm_obj_class(ovm_consts.Slice))
/** \brief Convenience macro for #Cslice class */
#define OVM_CL_CSLICE       (ovm_obj_class(ovm_consts.Cslice))
/** \brief Convenience macro for #Byteslice class */
#define OVM_CL_BYTESLICE    (ovm_obj_class(ovm_consts.Byteslice))
/** \brief Convenience macro for #Cbyteslice class */
#define OVM_CL_CBYTESLICE   (ovm_obj_class(ovm_consts.Cbyteslice))
/** \brief Convenience macro for #Set class */
#define OVM_CL_SET          (ovm_obj_class(ovm_consts.Set))
/** \brief Convenience macro for #Cset class */
#define OVM_CL_CSET         (ovm_obj_class(ovm_consts.Cset))
  /** \brief Convenience macro for #Dictionary class */
#define OVM_CL_DICTIONARY   (ovm_obj_class(ovm_consts.Dictionary))
  /** \brief Convenience macro for #Cdictionary class */
#define OVM_CL_CDICTIONARY  (ovm_obj_class(ovm_consts.Cdictionary))
/** \brief Convenience macro for #Namespace class */
#define OVM_CL_NAMESPACE    (ovm_obj_class(ovm_consts.Namespace))
/** \brief Convenience macro for #File class */
#define OVM_CL_FILE         (ovm_obj_class(ovm_consts.File))
/** \brief Convenience macro for #Module class */
#define OVM_CL_MODULE       (ovm_obj_class(ovm_consts.Module))
  /** \brief Convenience macro for #Exception class */
#define OVM_CL_EXCEPTION    (ovm_obj_class(ovm_consts.Exception))
/** \brief Convenience macro for #System class */
#define OVM_CL_SYSTEM       (ovm_obj_class(ovm_consts.System))
/** \brief Convenience macro for #Environment class */
#define OVM_CL_ENVIRONMENT  (ovm_obj_class(ovm_consts.Environment))
/** \brief Convenience macro for #User class */
#define OVM_CL_USER         (ovm_obj_class(ovm_consts.User))

/**@}*/

/**
 * \defgroup Init Initialization and startup
 */
/**@{*/

/**
 * \brief Initialize the OVM library
 *
 * Initialize the OVM library, and create the main thread.
 *
 * \param[in] stack_size Size of instance stack for main thread; 0 indicates to use the default statck size of 8K.
 * \param[in] frame_stack_size Size of frame stack for main thread; 0 indicates to use the default frame stack size of 4K.
 *
 * \return Thread cookie, for main thread
 */
ovm_thread_t ovm_init(unsigned stack_size, unsigned frame_stack_size);

/**
 * \brief Load module and execute method
 *
 * Load the given module, and execute the given (class) method in the given
 * class in the given module.
 *
 * \param[in] th Thread cookie
 * \param[out] dst Where to store result of method call
 * \param[in] entry_module Name of module to load
 * \param[in] entry_cl Name of class
 * \param[in] entry_method Name of class method to execute
 * \param[in] argc Number of arguments in argv
 * \param[in] argv Array of method arguments (strings)
 *
 * \return Error code, as follows:
 * | Code | Description |
 * |------|-------------|
 * | 0 | No error |
 * | -2 | Entry module not found |
 * | -3 | Entry module initialization failed |
 * | -4 | Entry namespace (if specified) not found |
 * | -5 | Entry class not found |
 * | -6 | Entry method not found |
 */
int ovm_run(ovm_thread_t th, ovm_inst_t dst, char *entry_module, char *entry_cl, char *entry_method, int argc, char **argv);

/**
 * \brief Run module statically
 *
 * Run a module as part of a static build
 *
 * \param[in] th Thread
 * \param[out] dst Where to store result of entry method
 * \param[in] init Module init function
 * \param[in] entry Entry method
 * \param[in] argc Number of arguments in argv
 * \param[in] argv Array of entry method arguments (strings)
 */
void ovm_run_static(ovm_thread_t th, ovm_inst_t dst, ovm_codemethod_t init, ovm_codemethod_t entry, int argc, char **argv);

/**@}*/

/**
 * \defgroup Threads Threads
 */
/**@{*/

/**
 * \brief Save thread's errno
 *
 * Save the current value of errno
 *
 * \param[in] th Thread
 *
 * \return Current errno value
 */
static inline int ovm_thread_errno_set(ovm_thread_t th)
{
    return (th->_errno = errno);
}

/**
 * \brief Get saved errno for thread
 *
 * Return the errno value previously saved for the thread; see ovm_thread_errno_set().  If no errno value
 * was previously saved, returns 0.
 *
 * \parame[in] th Thread
 *
 * \return Previously saved errno value for thread.
 */
static inline int ovm_thread_errno(ovm_thread_t th)
{
    return (th->_errno);
}

/**@}*/

/**
 * \defgroup Memory Memory management
 */
/**@{*/

#define OVM_MEM_MIN_BUF_SIZE_LOG2  6  /**< log (base 2) of minimum buffer size (see OVM_MEM_MIN_BUF_SIZE) */
#define OVM_MEM_MIN_BUF_SIZE       (1 << OVM_MEM_MIN_BUF_SIZE_LOG2)  /**< Minimum buffer size, in bytes */
unsigned ovm_mem_max_buf_size;  /**< Maximum buffer size, in bytes */
#define OVM_MEM_ALLOC_NO_HINT  (-1)  /**< See ovm_mem_alloc() */

/**
 * \brief Allocate memory
 *
 * Allocate a piece of memory, of at least the given size.
 *
 * \param[in] size Size, in bytes, of requested memory
 * \param[in] hint Buffer hint; in the range of [0, logbase2(ovm_mem_max_buf_size) - OVM_MEM_MIN_BUF_SIZE_LOG2], which correspond to the range of buffer sizes, from minimum to maximum, respectively.  The special value OVM_MEM_ALLOC_NO_HINT indicates no hint is given.
 * \param[in] clrf true <=> Zero out allocated memory
 *
 * \return Pointer to allocated memory
 */
void *ovm_mem_alloc(unsigned size, int hint, bool clrf);

/**
 * \brief Free allocated memory
 *
 * Free a previously allocated piece of memory.
 *
 * \param[in] ptr Pointer to memory
 * \param[in] size Size, in bytes, of memory
 *
 * \return Nothing
 */
void ovm_mem_free(void *ptr, unsigned size);

/**@}*/

/**
 * \defgroup Objects Basic objects
 */
/**@{*/

/**
 * \brief Allocate object
 *
 * \param[out] dst Where to store instance for new object
 * \param[in] size Size of object, in bytes
 * \param[in] cl Class of which object is an instance
 * \param[in] hint Memory allocation hint [see ovm_mem_alloc()]
 * \param[in] init Object initialization function; if 0, object is zeroed-out
 *
 * \return Pointer to allocated object
 */
ovm_obj_t ovm_obj_alloc(ovm_inst_t dst, unsigned size, ovm_obj_class_t cl, int mem_hint, void (*init)(ovm_obj_t, va_list), ...) __attribute__((assume_aligned (1)));

/**
 * \brief Release an object
 *
 * When an object's free function is called [see ovm_class_new()], it signals that the object's
 * reference count has gone down to 0, and the object will be freed.  Therefore, it must release
 * any references to other objects, by calling this function.
 *
 * \param[in] obj Object to release
 *
 * \return Nothing
 */
static inline void ovm_obj_release(ovm_obj_t obj) /* Lock already held */
{
    if (obj == 0)  return;
    DEBUG_ASSERT(obj->ref_cnt != 0);
    if (--obj->ref_cnt > 0)  return;
    _ovm_obj_free(obj);
}

/**
 * \brief Mark an object
 *
 * When an object is marked as in use during garbage collection [see ovm_class_new()], this function must be called to signal that objects referred to must also be marked.
 *
 * \param[in] obj Object to mark
 *
 * \return Nothing
 */
void ovm_obj_mark(ovm_obj_t obj); /* Lock already held */

/**
 * \brief Assign object to object
 *
 * Assign the given source object to the given destination
 *
 * \param[out] dst Destination object
 * \param[in] src Source object
 *
 * \return Nothing
 */
static inline void ovm_obj_assign(ovm_obj_t *dst, ovm_obj_t src)
{
    _ovm_objs_lock();

    _ovm_obj_assign_nolock(dst, src);

    _ovm_objs_unlock();
}

/**@}*/

/**
 * \defgroup Basic Basic instances
 */
/**@{*/

/**
 * \brief Release an instance
 *
 * When an object's free function is called [see ovm_class_new()], it signals that the object's
 * reference count has gone down to 0, and the object will be freed.  Therefore, it must release
 * any instances it has, by calling this function.
 *
 * \param[in] inst Instance to release
 *
 * \return Nothing
 */
static inline void ovm_inst_release(ovm_inst_t inst) /* Lock already held */
{
    if (inst->type == OVM_INST_TYPE_OBJ)  ovm_obj_release(inst->objval);
}

/**
 * \brief Mark an instance
 *
 * When an object is marked as in use during garbage collection [see
 * ovm_class_new()], this function must be called so thtat instances with in the
 * object are also marked.
 *
 * \param[in] inst Instance to mark
 *
 * \return Nothing
 */
static inline void ovm_inst_mark(ovm_inst_t inst) /* Lock already held */
{
    if (inst->type == OVM_INST_TYPE_OBJ)  ovm_obj_mark(inst->objval);
}

/**
 * \brief Assign an object to an instance
 *
 * \param[out] dst Instance to assign to
 * \param[in] obj Object to assign
 *
 * \return Nothing
 */
static inline void ovm_inst_assign_obj(ovm_inst_t dst, ovm_obj_t obj)
{
    _ovm_objs_lock();

    _ovm_inst_assign_obj_nolock(dst, obj);

    _ovm_objs_unlock();
}

/**
 * \brief Assign once instance to another
 *
 * \param[out] dst Instance to assign to
 * \param[in] src Instance to assign from
 *
 * \return Nothing
 */
static inline void ovm_inst_assign(ovm_inst_t dst, ovm_inst_t src)
{
    _ovm_objs_lock();

    _ovm_inst_assign_nolock(dst, src);

    _ovm_objs_unlock();
}

/**
 * \brief Test if instance is nil
 *
 * Return true if the given instance is nil
 *
 * \param[in] inst Instance to test
 *
 * \return true iff given instance is nil
 */
static inline bool ovm_inst_is_nil(ovm_inst_t inst)
{
    return (inst->type == OVM_INST_TYPE_OBJ && inst->objval == 0);
}

/**
 * \brief Lookup raw class of object
 *
 * Return the raw class of which the given object is an instance.
 * Raw class means that the `__instanceof__` member of an instance of the User class is not consulted.
 *
 * \param[in] obj Object
 *
 * \return Class of object 
 */
static inline ovm_obj_class_t ovm_obj_inst_of_raw(ovm_obj_t obj)
{
    return (obj == 0 ? OVM_CL_OBJECT : ovm_obj_class(obj->inst_of));
}

/**
 * \brief Lookup raw class of instance
 *
 * Return the raw class of which the given instance is an instance.
 * Raw class means that the `__instanceof__` member of an instance of the User class is not consulted.
 *
 * \param[in] inst Instance
 *
 * \return Class of instance
 */
static inline ovm_obj_class_t ovm_inst_of_raw(ovm_inst_t inst)
{
    switch (inst->type) {
    case OVM_INST_TYPE_OBJ:
        return (ovm_obj_inst_of_raw(inst->objval));
    case OVM_INST_TYPE_BOOL:
        return (OVM_CL_BOOLEAN);
    case OVM_INST_TYPE_INT:
        return (OVM_CL_INTEGER);
    case OVM_INST_TYPE_FLOAT:
        return (OVM_CL_FLOAT);
    case OVM_INST_TYPE_METHOD:
        return (OVM_CL_METHOD);
    case OVM_INST_TYPE_CODEMETHOD:
        return (OVM_CL_CODEMETHOD);
    default:
        abort();
    }

    return (0);
}

/**
 * \brief Test if one class is a subclass of another
 * 
 * Return true if one class is a subclass (i.e. direct or indirect child) of another.
 *
 * \param[in] cl1 Class to test
 * \param[in] cl2 Potential parent to test
 *
 * \return true iff _cl1_ is a subclass of _cl2_
 */
static inline bool ovm_is_subclass_of(ovm_obj_class_t cl1, ovm_obj_class_t cl2)
{
    for (; cl1 != 0; cl1 = ovm_obj_class(cl1->parent)) {
        if (cl1 == cl2)  return (true);
    }

    return (false);
}

/**
 * \brief Look up class of object
 *
 * Return class of which given object is an instance, respecting user classes.
 * 
 * \param[out] dst Where to write result
 * \param[in] obj Object
 *
 * \return Nothing
 */
void ovm_obj_inst_of(ovm_inst_t dst, ovm_obj_t obj);

/**
 * \brief Look up class of instance
 *
 * Return class of which given instance is an instance, respecting user classes.
 *
 * \param[out] dst Where to write result
 * \param[in] inst Instance
 *
 * \return Nothing
 */
void ovm_inst_of(ovm_inst_t dst, ovm_inst_t inst);

/**@}*/

/**
 * \defgroup Exceptions Exceptions
 */
/**@{*/

/**
 * \brief Push exception frame
 *
 * In order to catch an exception, an exception frame must be pushed.
 *
 * \param[in] th Thread
 * \param[out] arg Where value raised in exception will be stored
 *
 * \return A jmpbuf, suitable for passing to setjmp(3)
 */
struct __jmp_buf_tag *ovm_frame_except_push(ovm_thread_t th, ovm_inst_t arg);

/** \brief Conveniece macro, to push exception frame */
#define OVM_FRAME_EXCEPT_PUSH(th, arg) setjmp(ovm_frame_except_push((th), (arg)))

/**
 * \brief Pop exception frames
 *
 * \param[in] th Thread
 *
 * Pop the given number of exception frames.  An example of where this might be
 * > 1 is breaking out from nested exception frame pushes.
 *
 * \param[in] th Thread
 * \param[in] n Number of exception frames to pop
 *
 * \return Nothing
 */
void ovm_frame_except_pop(ovm_thread_t th, unsigned n);

/**
 * \brief Check if exception occured
 *
 * Check if an exception has occurred for the given thread.  Note that calling
 * this function will clear the thread's internal exception-occurred flag.
 *
 * \param[in] th Thread
 *
 * \return true iff an exception has occured
 */
static inline bool ovm_except_chk(ovm_thread_t th)
{
    bool result = th->exceptf;
    th->exceptf = false;
    return (result);
}

/**
 * \brief Raise an exception
 *
 * Execution for the thread will
 * jump to just after the last exception frame was pushed, and the thread's
 * internal exception-occurred flag will be set [see ovm_except_chk()].
 * 
 * \param[in] th Thread
 * \param[in] arg Exception argument; assigned to instance given in
 * ovm_frame_except_push().
 *
 * \return Never returns
 *
 * \note If no exception frame has been pushed, the thread will terminate, with an
 * exit code of OVM_THREAD_FATAL_UNCAUGHT_EXCEPT.
 */
void ovm_except_raise(ovm_thread_t th, ovm_inst_t arg) __attribute__((noreturn));

/**
 * \brief Reraise the current exception
 *
 * After an exception has been caught, raise it again, essentially passing it up
 * to the next highest exception catcher.
 *
 * \param[in] th Thread
 *
 * \return Never returns 
 *
 * \note If no exception frame has been
 * pushed, or this function is called when no exception has occurred, the thread
 * will terminate, with an exit code of OVM_THREAD_FATAL_NO_FRAME.
 */
void ovm_except_reraise(ovm_thread_t th) __attribute__((noreturn));

/**
 * \brief Raise system.invalid-value exception
 *
 * Raise an exception indicating that an invalid value was given
 *
 * \param[in] th Thread
 * \param[in] inst Offending instance
 *
 * \return Never returns
 *
 * \exception system.invalid-value
 */
void ovm_except_inv_value(ovm_thread_t th, ovm_inst_t inst) __attribute__((noreturn));

/**
 * \brief Raise system.number-of-arguments exception
 *
 * Raise an exception indicating that a method was passed the incorrect number of arguments
 *
 * \param[in] th Thread
 * \param[in] expected Expected number of arguments
 *
 * \return Never returns
 *
 * \exception system.number-of-arguments
 */
void ovm_except_num_args(ovm_thread_t th, unsigned expected) __attribute__((noreturn));

/**
 * \brief Raise system.number-of-arguments exception
 *
 * Raise an exception indicating that a method was passed the incorrect number of arguments
 *
 * \param[in] th Thread
 * \param[in] min Minimum number of arguments expected
 *
 * \return Never returns
 *
 * \exception system.number-of-arguments
 */
void ovm_except_num_args_min(ovm_thread_t th, unsigned min) __attribute__((noreturn));

/**
 * \brief Raise system.number-of-arguments exception
 *
 * Raise an exception indicating that a method was passed the incorrect number of arguments
 *
 * \param[in] th Thread
 * \param[in] min Minimum number of arguments expected
 * \param[in] min Maximum number of arguments expected
 *
 * \return Never returns
 *
 * \exception system.number-of-arguments
 */
void ovm_except_num_args_range(ovm_thread_t th, unsigned min, unsigned max) __attribute__((noreturn));

/**
 * \brief Raise system.no-attribute exception
 *
 * Raise an exception indicating that an instance has no attribute
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 * \param[in] attr Name of attribute not found
 *
 * \return Never returns
 *
 * \exception system.no-attribute
 */
void ovm_except_no_attr(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t attr) __attribute__((noreturn));

/**
 * \brief Raise system.index-range exception
 *
 * Raise an exception indicating that an index is out of range
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 * \param[in] idx Offending index
 *
 * \return Never returns
 *
 * \exception system.index-range
 */
void ovm_except_idx_range(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t idx) __attribute__((noreturn));

/**
 * \brief Raise system.index-range exception
 *
 * Raise an exception indicating that an index range is out of range
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 * \param[in] idx Offending index
 * \param[in] len Offending length
 *
 * \return Never returns
 *
 * \exception system.index-range
 */
void ovm_except_idx_range2(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t idx, ovm_inst_t len) __attribute__((noreturn));

/**
 * \brief Raise system.key-not-found exception
 *
 * Raise an exception indicating that a key is not found in a container, e.g. a Dictionary
 *
 * \param[in] th Thread
 * \param[in] inst Container instance
 * \param[in] key Offending key
 *
 * \return Never returns
 *
 * \exception system.key-not-found
 */
void ovm_except_key_not_found(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t key) __attribute__((noreturn));

/**
 * \brief Raise system.modify-constant exception
 *
 * Raise an exception indicating that an attempt was made to modofy a constant member of a container
 *
 * \param[in] th Thread
 * \param[in] inst Container instance
 * \param[in] key Offending key
 *
 * \return Never returns
 *
 * \exception system.modify-constant
 */
void ovm_except_modify_const(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t key) __attribute__((noreturn));

/**
 * \brief Raise system.descent-loop exception
 *
 * Raise an exception indicating that a loop in descent through data structures has been detected
 *
 * \param[in] th Thread
 *
 * \return Never returns
 *
 * \exception system.descent-loop
 */
void ovm_except_descent_loop(ovm_thread_t th) __attribute__((noreturn));

/**@}*/

/**
 * \defgroup Stack Basic stack operations
 */
/**@{*/

/**
 * \brief Allocate space on instance stack
 *
 * \param[in] th Thread
 * \param[in] n Number of instances to allocate
 *
 * \return Stack pointer before allocation occured
 *
 * \note Exceeding the size of the thread's stack will cause the thread to exit,
 * with the exit code OOVM_THREAD_FATAL_STACK_OVERFLOW (see oovm_thread.h).
 */
static inline ovm_inst_t ovm_stack_alloc(ovm_thread_t th, unsigned n)
{
    ovm_inst_t result = th->sp;
    ovm_inst_t p = _ovm_stack_alloc(th, n);
    memset(p, 0, n * sizeof(*p));
    th->sp = p;
    return (result);
}

/**
 * \brief Push object on instance stack
 *
 * \param[in] th Thread
 * \param[in] obj Object to push
 *
 * \return Nothing
 *
 * \note Exceeding the size of the thread's stack will cause the thread to exit,
 * with the exit code OOVM_THREAD_FATAL_STACK_OVERFLOW (see oovm_thread.h).
 */
static inline void ovm_stack_push_obj(ovm_thread_t th, ovm_obj_t obj)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);

    _ovm_objs_lock();

    _ovm_inst_assign_obj_nolock_norelease(th->sp = p, obj);
    
    _ovm_objs_unlock();
}

/**
 * \brief Push instance on instance stack
 *
 * \param[in] th Thread
 * \param[in] inst Instance to push
 *
 * \return Nothing
 *
 * \note Exceeding the thread's maximum stack size will cause the thread to
 * exit, with the exit code OOVM_THREAD_FATAL_STACK_OVERFLOW (see
 * oovm_thread.h).
 */
static inline void ovm_stack_push(ovm_thread_t th, ovm_inst_t inst)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);

    _ovm_objs_lock();

    _ovm_inst_assign_nolock_norelease(th->sp = p, inst);

    _ovm_objs_unlock();
}

/**
 * \brief Pop the instance stack
 *
 * All instances, from the current top of stack, until (but not including) the
 * given stack location, are popped from the stack, i.e. the thread's stack
 * pointer is set to the given value.
 *
 * \param[in] th Thread
 * \param[in] p Stack location to pop until
 *
 * \return Nothing
 *
 * \note Giving a stack position beyond the top of the thread's stack will cause
 * the thread to exit, with the exit code OVM_THREAD_FATAL_STACK_UNDERFLOW.
 */
static inline void ovm_stack_unwind(ovm_thread_t th, ovm_inst_t p)
{
    if (p > th->stack_top)  OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_STACK_UNDERFLOW, 0);
    register ovm_inst_t q = th->sp;
    DEBUG_ASSERT(p >= q);

    _ovm_objs_lock();
    
    for (; q < p; ++q)  ovm_inst_release(q);
    th->sp = p;

    _ovm_objs_unlock();
}

/**
 * \brief Pop the instance stack
 *
 * The given number of instances are popped from the thread's instance stack.
 * 
 * \param[in] th Thread
 * \param[in] n Number of instances to pop
 *
 * \return Nothing
 *
 * \note Giving a number of instances larger than the thread's current stack size will cause
 * the thread to exit, with the exit code OVM_THREAD_FATAL_STACK_UNDERFLOW.
 */
static inline void ovm_stack_free(ovm_thread_t th, unsigned n)
{
    ovm_stack_unwind(th, th->sp + n);
}

/**
 * \brief Perform stack free followed by stack allocation
 *
 * An optimized call, to perform stack free immediately followed by an
 * allocation.
 *
 * \param[in] th Thread
 * \param[in] size_free Number of instances to free
 * \param[in] size_alloc Number of instances to allocate
 *
 * \return Stack pointer after free and before allocation
 *
 * \note Giving a number of instances to free larger than the thread's current stack size will cause
 * the thread to exit, with the exit code OVM_THREAD_FATAL_STACK_UNDERFLOW.
 * Allocating a number of instances that will exceeed the thread's stack maximum
 * size size will cause
 * the thread to exit, with the exit code OVM_THREAD_FATAL_STACK_OVERFLOW.
 */
static inline ovm_inst_t ovm_stack_free_alloc(ovm_thread_t th, unsigned size_free, unsigned size_alloc)
{
    ovm_inst_t p = th->sp;
    if ((p + size_free) > th->stack_top)  OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_STACK_UNDERFLOW, 0);
    if (size_alloc >= size_free) {
        unsigned n;
        for (n = size_free; n > 0; --n, ++p)  ovm_inst_assign_obj(p, 0);
        ovm_stack_alloc(th, size_alloc - size_free);
    } else {
        ovm_stack_free(th, size_free - size_alloc);
        for (p = th->sp; size_alloc > 0; --size_alloc, ++p)  ovm_inst_assign_obj(p, 0);
    }
    
    return (p);
}

/**@}*/

/**
 * \defgroup Accessors Instance value accessors
 */
/**@{*/

/**
 * \brief Return string value of instance
 *
 * Return the string value for an instance of String.
 *
 * \param[in] inst Instance
 *
 * \return String object
 *
 * \note The instance is not checked that is in fact a String.

 */
static inline ovm_obj_str_t ovm_inst_strval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_str(inst->objval));
}

/**
 * \brief Return pair value of instance
 *
 * Return the pair value for an instance of Pair.
 *
 * \param[in] inst Instance
 *
 * \return Pair object
 *
 * \note The instance is not checked that is in fact a Pair.
 */
static inline ovm_obj_pair_t ovm_inst_pairval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_pair(inst->objval));
}

/**
 * \brief Return list value of instance
 *
 * Return the list value for an instance of List.
 *
 * \param[in] inst Instance
 *
 * \return List object
 *
 * \note The instance is not checked that is in fact a List.
 */
static inline ovm_obj_list_t ovm_inst_listval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_list(inst->objval));
}

/**
 * \brief Return array value of instance
 *
 * Return the array value for an instance of Array or Carray.
 *
 * \param[in] inst Instance
 *
 * \return Array object
 *
 * \note The instance is not checked that is in fact an Array or Carray.
 */
static inline ovm_obj_array_t ovm_inst_arrayval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_array(inst->objval));
}

/**
 * \brief Return bytearray value of instance
 *
 * Return the bytearray value for an instance of Bytearray or Cbytearray.
 *
 * \param[in] inst Instance
 *
 * \return Array object
 *
 * \note The instance is not checked that is in fact a Bytearray or Cbytearray.
 */
static inline ovm_obj_barray_t ovm_inst_barrayval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_barray(inst->objval));
}

/**
 * \brief Return slice value of instance
 *
 * Return the slice value for an instance of Slice or Cslice.
 *
 * \param[in] inst Instance
 *
 * \return Array object
 *
 * \note The instance is not checked that is in fact a Slice or Cslice.
 */
static inline ovm_obj_slice_t ovm_inst_sliceval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_slice(inst->objval));
}

/**
 * \brief Return set value of instance
 *
 * Return the set value for an instance of Set, Cset, Dictionary or CDictionary.
 *
 * \param[in] inst Instance
 *
 * \return Set object
 *
 * \note The instance is not checked that is in fact a Set, Cset, Dictionary or CDictionary.
 */
static inline ovm_obj_set_t ovm_inst_setval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_set(inst->objval));
}

/**
 * \brief Return class value of instance
 *
 * Return the class value for an instance of Metaclass.
 *
 * \param[in] inst Instance
 *
 * \return Class object
 *
 * \note The instance is not checked that is in fact a class.
 */
static inline ovm_obj_class_t ovm_inst_classval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_class(inst->objval));
}

/**
 * \brief Return namespace value of instance
 *
 * Return the namespace value for an instance of Namespace.
 *
 * \param[in] inst Instance
 *
 * \return Namespace object
 *
 * \note The instance is not checked that is in fact a Namespace.
 */
static inline ovm_obj_ns_t ovm_inst_nsval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_ns(inst->objval));
}

/**
 * \brief Return module value of instance
 *
 * Return the module value for an instance of Module.
 *
 * \param[in] inst Instance
 *
 * \return Module object
 *
 * \note The instance is not checked that is in fact a Module.
 */
static inline ovm_obj_module_t ovm_inst_moduleval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_module(inst->objval));
}

/**
 * \brief Return file value of instance
 *
 * Return the file value for an instance of File.
 *
 * \param[in] inst Instance
 *
 * \return File object
 *
 * \note The instance is not checked that is in fact a File.
 */
static inline ovm_obj_file_t ovm_inst_fileval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_file(inst->objval));
}

/**
 * \brief Return boolean value of instance
 *
 * Return the boolean value for an instance of Boolean.
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return Boolean value, i.e. true or false
 *
 * \exception system.invalid-value Given instance is not instance of Boolen
 */
static inline bool ovm_inst_boolval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type != OVM_INST_TYPE_BOOL)  ovm_except_inv_value(th, inst);
    return (inst->boolval);
}

/**
 * \brief Return integer value of instance
 *
 * Return the integer value for an instance of Integer.
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return Integer value
 *
 * \exception system.invalid-value Given instance is not instance of Integer
 */
static inline ovm_intval_t ovm_inst_intval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type != OVM_INST_TYPE_INT)  ovm_except_inv_value(th, inst);
    return (inst->intval);
}

/**
 * \brief Return float value of instance
 *
 * Return the floating-point value for an instance of Float.
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return Float value
 *
 * \exception system.invalid-value Given instance is not instance of Float
 */
static inline ovm_floatval_t ovm_inst_floatval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type != OVM_INST_TYPE_FLOAT)  ovm_except_inv_value(th, inst);
    return (inst->floatval);
}

/**
 * \brief Return method value of instance
 *
 * Return the method value for an instance of Method.
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return Method value
 *
 * \exception system.invalid-value Given instance is not instance of Method
 */
static inline ovm_method_t ovm_inst_methodval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type != OVM_INST_TYPE_METHOD)  ovm_except_inv_value(th, inst);
    return (inst->methodval);
}

/**
 * \brief Return codemethod value of instance
 *
 * Return the code method value for an instance of Codemethod
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return Code method value
 *
 * \exception system.invalid-value Given instance is not instance of Codemethod
 */
static inline ovm_codemethod_t ovm_inst_codemethodval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type != OVM_INST_TYPE_CODEMETHOD)  ovm_except_inv_value(th, inst);
    return (inst->codemethodval);
}

/**
 * \brief Return string object of instance
 *
 * Return the string object for an instance
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return String object
 *
 * \exception system.invalid-value Given instance is not an object which is an instance of String
 */
static inline ovm_obj_str_t ovm_inst_strval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_obj_inst_of_raw(obj) == OVM_CL_STRING)  return (ovm_obj_str(obj));
    }

    ovm_except_inv_value(th, inst);
}
  
/**
 * \brief Return pair object of instance
 *
 * Return the pair object for an instance
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return Pair object
 *
 * \exception system.invalid-value Given instance is not an object whish is instance of Pair
 */
static inline ovm_obj_pair_t ovm_inst_pairval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_obj_inst_of_raw(obj) == OVM_CL_PAIR)  return (ovm_obj_pair(obj));
    }

    ovm_except_inv_value(th, inst);
}

/**
 * \brief Return list object of instance
 *
 * Return the list object for an instance of List
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return List object
 *
 * \exception system.invalid-value Given instance is not instance of List
 */
static inline ovm_obj_list_t ovm_inst_listval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (obj == 0 || ovm_obj_inst_of_raw(obj) == OVM_CL_LIST)  return (ovm_obj_list(obj));
    }

    ovm_except_inv_value(th, inst);
}

/**
 * \brief Return array object of instance
 *
 * Return the array object for an instance of Array or Carray
 *
 * \param[in] th Thread
 * \param[in] inst Instance
 *
 * \return Array object
 *
 * \exception system.invalid-value Given instance is not instance of Array or Carray
 */
static inline ovm_obj_array_t ovm_inst_arrayval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_is_subclass_of(ovm_obj_inst_of_raw(obj), OVM_CL_ARRAY)) {
            return (ovm_obj_array(obj));
        }
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_barray_t ovm_inst_barrayval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_is_subclass_of(ovm_obj_inst_of_raw(obj), OVM_CL_BYTEARRAY)) {
            return (ovm_obj_barray(obj));
        }
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_slice_t ovm_inst_sliceval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_is_subclass_of(ovm_obj_inst_of_raw(obj), OVM_CL_SLICE)) {
            return (ovm_obj_slice(obj));
        }
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_set_t ovm_inst_setval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_is_subclass_of(ovm_obj_inst_of_raw(obj), OVM_CL_SET)) {
            return (ovm_obj_set(obj));
        }
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_set_t ovm_inst_dictval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_is_subclass_of(ovm_obj_inst_of_raw(obj), OVM_CL_DICTIONARY)) {
            return (ovm_obj_set(obj));
        }
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_ns_t ovm_inst_nsval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_is_subclass_of(ovm_obj_inst_of_raw(obj), OVM_CL_NAMESPACE)) {
            return (ovm_obj_ns(obj));
        }
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_module_t ovm_inst_moduleval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_obj_inst_of_raw(obj) == OVM_CL_MODULE)  return (ovm_obj_module(obj));
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_file_t ovm_inst_fileval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        if (ovm_obj_inst_of_raw(obj) == OVM_CL_FILE)  return (ovm_obj_file(obj));
    }

    ovm_except_inv_value(th, inst);
}

static inline ovm_obj_class_t ovm_inst_classval(ovm_thread_t th, ovm_inst_t inst)
{
    if (inst->type == OVM_INST_TYPE_OBJ) {
        ovm_obj_t obj = inst->objval;
        ovm_obj_class_t cl = ovm_obj_inst_of_raw(obj);
        if (cl == OVM_METACLASS || cl == 0)  return (ovm_obj_class(obj));
    }

    ovm_except_inv_value(th, inst);
}

/**@}*/

/**
 * \defgroup Constructors Basic constructors
 */
/**@{*/

static inline void ovm_bool_newc(ovm_inst_t dst, bool val)
{
    _ovm_objs_lock();

    ovm_inst_release(dst);
    OVM_INST_INIT(dst, OVM_INST_TYPE_BOOL, boolval, val);

    _ovm_objs_unlock();
}

static inline void ovm_bool_pushc(ovm_thread_t th, bool val)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);
    OVM_INST_INIT(p, OVM_INST_TYPE_BOOL, boolval, val);

    th->sp = p;
}

static inline void ovm_int_newc(ovm_inst_t dst, ovm_intval_t val)
{
    _ovm_objs_lock();

    ovm_inst_release(dst);
    OVM_INST_INIT(dst, OVM_INST_TYPE_INT, intval, val);

    _ovm_objs_unlock();
}

static inline void ovm_int_pushc(ovm_thread_t th, ovm_intval_t val)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);
    OVM_INST_INIT(p, OVM_INST_TYPE_INT, intval, val);

    th->sp = p;
}

static inline void ovm_float_newc(ovm_inst_t dst, ovm_floatval_t val)
{
    _ovm_objs_lock();

    ovm_inst_release(dst);
    OVM_INST_INIT(dst, OVM_INST_TYPE_FLOAT, floatval, val);

    _ovm_objs_unlock();
}

static inline void ovm_float_pushc(ovm_thread_t th, ovm_floatval_t val)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);
    OVM_INST_INIT(p, OVM_INST_TYPE_FLOAT, floatval, val);

    th->sp = p;
}

static inline void ovm_method_newc(ovm_inst_t dst, ovm_method_t m)
{
    _ovm_objs_lock();

    ovm_inst_release(dst);
    OVM_INST_INIT(dst, OVM_INST_TYPE_METHOD, methodval, m);

    _ovm_objs_unlock();
}

static inline void ovm_method_pushc(ovm_thread_t th, ovm_method_t m)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);
    OVM_INST_INIT(p, OVM_INST_TYPE_METHOD, methodval, m);

    th->sp = p;
}

static inline void ovm_codemethod_newc(ovm_inst_t dst, ovm_codemethod_t m)
{
    _ovm_objs_lock();

    ovm_inst_release(dst);
    OVM_INST_INIT(dst, OVM_INST_TYPE_CODEMETHOD, codemethodval, m);

    _ovm_objs_unlock();
}

static inline void ovm_codemethod_pushc(ovm_thread_t th, ovm_codemethod_t m)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);
    OVM_INST_INIT(p, OVM_INST_TYPE_CODEMETHOD, codemethodval, m);

    th->sp = p;
}

/**
 * \brief Construct a string instance
 *
 * \param[out] dst Where to write instance
 * \param[in] size Size of string, including null terminator
 * \param[in] data String data
 *
 * \return Nothing
 */
void ovm_str_newc(ovm_inst_t dst, unsigned size, const char *data);

/**
 * \brief Construct a string instance
 *
 * \param[out] dst Where to write instance
 * \param[in] size Size of string, including null terminator
 * \param[in] data String data
 * \param[in] hash Hash value for string
 *
 * \return Nothing
 */
void ovm_str_newch(ovm_inst_t dst, unsigned size, const char *data, unsigned hash);

/**
 * \brief Construct a string instance
 *
 * \param[out] dst Where to write instance
 * \param[in] data String data
 *
 * \return Nothing
 */
void ovm_str_newc1(ovm_inst_t dst, const char *data);

/**
 * \brief Push a string instance
 *
 * \param[in] th Thread
 * \param[in] size Size of string, including null terminator
 * \param[in] data String data
 *
 * \return Nothing
 */
void ovm_str_pushc(ovm_thread_t th, unsigned size, const char *data);

void ovm_str_pushch(ovm_thread_t th, unsigned size, const char *data, unsigned hash);
void ovm_str_pushc1(ovm_thread_t th, const char *data);
void ovm_str_clist(ovm_inst_t dst, struct ovm_clist *cl);
void ovm_bytearray_newc(ovm_inst_t dst, unsigned size, unsigned char *data);
void ovm_bytearray_clist(ovm_inst_t dst, struct ovm_clist *cl);
void ovm_file_newc(ovm_thread_t th, ovm_inst_t dst, unsigned name_size, const char *name, unsigned mode_size, const char *mode, FILE *fp);

/**@}*/

/**
 * \defgroup Classes Classes
 */
/**@{*/

/**
 * \brief Create a class
 *
 * Create a new class.
 * When calling, the stack must have the parent class for the new class at the top, followed by the namespace in which the class will be created.
 * When completed, the stack will have the new class at the top, followed by the namespace.
 * 
 * \param[in] th Thread
 * \param[in] name_size Size of class name string
 * \param[in] name Name of new class
 * \param[in] name_hash Hash value for class name string
 * \param[in] mark The function that will be called to mark all object references within an instance of the class, for garbage collection.
 * \param[in] free The function that will be called to release all object references within an instance of the class, and clean up any other resources held by the instance.
 * \param[in] cleanup The function that will be called to clean up any other resources held by an instance of the class.
 * 
 * \return Nothing
 *
 * \except system.invalid-value Invalid argument value
 */
void ovm_class_new(ovm_thread_t th, unsigned name_size, const char *name, unsigned name_hash, void (*mark)(ovm_obj_t obj), void (*free)(ovm_obj_t obj), void (*cleanup)(ovm_obj_t obj));

/**
 * \brief Create a user class
 *
 * Create a new class, which will create user-type instances.
 * When called, expects that the stack will have the parent class for the new class at the top, followed by the namespace in which the class will be created.
 * When completed, the stack will have the new class at the top, followed by the namespace.
 * 
 * \param[in] th Thread
 * \param[in] name_size Size of class name string
 * \param[in] name Name of new class
 * \param[in] name_hash Hash value for class name string
 * 
 * \return Nothing
 *
 * \except system.invalid-value Invalid argument value
 */
void ovm_user_class_new(ovm_thread_t th, unsigned name_size, const char *name, unsigned name_hash);

/**
 * \brief Add a class method to a class
 *
 * Add the given function as a class method to the given class.
 * When called, expects that the stack will have the class in which the method will be added at the top.
 * When completed, the stack is unchanged.

 * \param[in] th Thread
 * \param[in] sel_size Size of selector string
 * \param[in] sel Selector
 * \param[in] sel_hash Hash value for selector string
 * \param[in] type One of OVM_INST_TYPE_CODEMETHOD or OVM_INST_TYPE_METHOD
 * \param[in] func Method function
 * 
 * \return Nothing
 *
 * \except system.invalid-value Invalid argument value
 */
void ovm_classmethod_add(ovm_thread_t th, unsigned sel_size, const char *sel, unsigned sel_hash, unsigned type, void *func);

/**
 * \brief Add an instance method to a class
 *
 * Add the given function as an instance method to the given class.
 * When called, expects that the stack will have the class in which the method will be added at the top.
 * When completed, the stack is unchanged.

 * \param[in] th Thread
 * \param[in] sel_size Size of selector string
 * \param[in] sel Selector
 * \param[in] sel_hash Hash value for selector string
 * \param[in] type One of OVM_INST_TYPE_CODEMETHOD or OVM_INST_TYPE_METHOD
 * \param[in] func Method function
 * 
 * \return Nothing
 *
 * \except system.invalid-value Invalid argument value
 */
void ovm_method_add(ovm_thread_t th, unsigned sel_size, const char *sel, unsigned sel_hash, unsigned type, void *func);

/**
 * \brief Delete a class method from a class
 *
 * Delete the class method with the given selector from the given class.

 * \param[in] cl Class
 * \param[in] sel_size Size of selector string
 * \param[in] sel Selector
 * \param[in] sel_hash Selector hash value
 * 
 * \return Nothing
 */
void ovm_classmethod_del(ovm_obj_class_t cl, unsigned sel_size, const char *sel, unsigned sel_hash);

/**
 * \brief Delete an instance method from a class
 *
 * Delete the method with the given selector from the given class.

 * \param[in] cl Class
 * \param[in] sel_size Size of selector string
 * \param[in] sel Selector
 * \param[in] sel_hash Selector hash value
 * 
 * \return Nothing
 */
void ovm_method_del(ovm_obj_class_t cl, unsigned sel_size, const char *sel, unsigned sel_hash);

/**@}*/

/**
 * \defgroup Methods Methods
 */
/**@{*/

/**
 * \brief Check fixed number of method arguments
 *
 * Check that a method received the correct, fixed number of arguments; if not, raise an exception
 *
 * \param[in] th Thread
 * \param[in] expected Expected number of arguments 
 *
 * \return Nothing
 *
 * \exception system.number-of-arguments Raised if passed number of arguments is not equal
 *                                       to expected number of arguments
 */
void ovm_method_argc_chk_exact(ovm_thread_t th, unsigned expected);

/**
 * \brief Check minimum number of method arguments
 *
 * Check that a method received a minimum number of arguments; if not, raise an exception
 *
 * \param[in] th Thread
 * \param[in] min Minimum number of arguments 
 *
 * \return Nothing
 *
 * \exception system.number-of-arguments Raised if passed number of arguments is less than
 *                                       the given minimum number of arguments
 */
void ovm_method_argc_chk_min(ovm_thread_t th, unsigned min);

/**
 * \brief Check range of number of method arguments
 *
 * Check that a method received a number of arguments within a given range; if not, raise an exception
 *
 * \param[in] th Thread
 * \param[in] min Minimum number of arguments 
 * \param[in] min Maximum number of arguments 
 *
 * \return Nothing
 *
 * \exception system.number-of-arguments Raised if passed number of arguments is not within
 *                                       the given range
 */
void ovm_method_argc_chk_range(ovm_thread_t th, unsigned min, unsigned max);

/**
 * \brief Push variable method arguments as array
 *
 * Check that a minimum number of arguments were passed to the method, and push any arguments over that
 * minimum onto the stack as an Array.
 *
 * \param[in] th Thread
 * \param[in] num_fixed Minimum number of arguments
 *
 * \return Array object
 *
 * \exception system.number-of-arguments Raised if number of arguments passed is less than
 *                                       the given minimum number of arguments
 */
ovm_obj_array_t ovm_method_array_arg_push(ovm_thread_t th, unsigned num_fixed);

/**
 * \brief Call a method
 *
 * Check that a minimum number of arguments were passed to the method, and push any arguments over that
 * minimum onto the stack as an Array.
 *
 * \param[in] th Thread
 * \param[out] dst Where to put method result
 * \param[in] sel_size Size of method selector
 * \param[in] sel Method selector
 * \param[in] sel_hash Hash vale for method selector
 * \param[in] argc Number of method arguments
 *
 * \return Nothing
 *
 * \exception system.no-method Raised if given selector is not found
 */
void ovm_method_callsch(ovm_thread_t th, ovm_inst_t dst, unsigned sel_size, const char *sel, unsigned sel_hash, unsigned argc);

/**@}*/

/**
 * \defgroup Environment Environment
 */
/**@{*/

/**
 * \brief Fetch from Environment
 *
 * Look up the given name in the Environment, and return the associated value.  Raises an exception if
 * the given name is not found.
 *
 * \param[in] th Thread
 * \param[out] dst Where to write result
 * \param[in] nm_size Size of name string
 * \param[in] nm Name string
 * \param[in] nm_hash Hash value of name string
 *
 * \return Nothing
 *
 * \exception system.no-variable Given name not found in Environment
 */
void ovm_environ_atc(ovm_thread_t th, ovm_inst_t dst, unsigned nm_size, const char *nm, unsigned nm_hash);

/**
 * \brief Fetch from Environment
 *
 * Look up the given name in the Environment, and push the associated value onto the stack.  Raises an exception if
 * the given name is not found.
 *
 * \param[in] th Thread
 * \param[in] nm_size Size of name string
 * \param[in] nm Name string
 * \param[in] nm_hash Hash value of name string
 *
 * \return Nothing
 *
 * \exception system.no-variable Given name not found in Environment
 */
void ovm_environ_atc_push(ovm_thread_t th, unsigned nm_size, const char *nm, unsigned nm_hash);

/**
 * \brief Store to Environment
 *
 * Store the given value under the given name in the Environment, i.e. the current namespace.
 *
 * \param[in] th Thread
 * \param[in] nm_size Size of name string
 * \param[in] nm Name string
 * \param[in] nm_hash Hash value of name string
 * \param[in] val Value to store
 *
 * \return Nothing
 */
void ovm_environ_atcput(ovm_thread_t th, unsigned nm_size, const char *nm, unsigned nm_hash, ovm_inst_t val);

/**@}*/






/**
 * \brief Test top of stack
 *
 * Test the Boolean value on the top of the stack, and return true if it was true.
 * The value at the top of the stack is popped.
 * If the value at the stop of the stack is not a Boolean, an exception is raised.
 *
 * \return true <=> Top of stack was true
 *
 * \exception system.invalid-value Top of stack was not an instance of Boolean
 */

bool ovm_bool_if(ovm_thread_t th);




/**
 * \defgroup Clists Clists
 */
/**@{*/

/**
 * \brief Initialize a clist
 *
 * \param[in] cl clist
 *
 * \return Nothing
 */
void ovm_clist_init(struct ovm_clist *cl);

/**
 * \brief Append a character string to a clist
 *
 * \param[in] cl clist
 * \param[in] n Size of string
 * \param[in] s String
 *
 * \return Nothing
 */
void ovm_clist_appendc(struct ovm_clist *cl, unsigned n, const char *s);

/**
 * \brief Append a character string to a clist
 *
 * \param[in] cl clist
 * \param[in] s String (null-terminated)
 *
 * \return Nothing
 */
void ovm_clist_appendc1(struct ovm_clist *cl, const char *s);

/**
 * \brief Append a character to a clist
 *
 * \param[in] cl clist
 * \param[in] c Character
 *
 * \return Nothing
 */
void ovm_clist_append_char(struct ovm_clist *cl, char c);

/**
 * \brief Append a string object to a clist
 *
 * \param[in] cl clist
 * \param[in] s String object
 *
 * \return Nothing
 */
void ovm_clist_append_str(ovm_thread_t th, struct ovm_clist *cl, ovm_obj_str_t s);

/**
 * \brief Convert a clist array of characters
 *
 * \param[in] buf_size Size of buffer
 * \param[in] buf Buffer to write to
 * \param[in] cl clist
 *
 * \return Actual number of characters written
 */
unsigned  ovm_clist_to_barray(unsigned buf_size, unsigned char *buf, struct ovm_clist *cl);

/**
 * \brief Concatenate 2 clists
 *
 * \param[in] dst Destination clist, has source clist appended to it
 * \param[in] src Source clist
 *
 * \return Nothing
 */
void ovm_clist_concat(struct ovm_clist *dst, struct ovm_clist *src);

/**
 * \brief Free clist
 *
 * \param[in] cl clist to free
 *
 * \return Nothing
 */
void ovm_clist_fini(struct ovm_clist *cl);

/**@}*/

#endif /* __OOVM_H */

/*
Local Variables:
c-basic-offset: 4
End:
*/
