#ifndef __OOVM_INTERNAL_H
#define __OOVM_INTERNAL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#include "oovm_types.h"
#include "oovm_thread.h"

#ifdef NDEBUG
#define DEBUG_ASSERT(x)
#else
#define DEBUG_ASSERT  assert
#endif

/***************************************************************************/

#define ARRAY_SIZE(a)    (sizeof(a) / sizeof((a)[0]))
#define PTR_TO_UINT(x)                    ((__intptr_t)(x))
#define FIELD_PTR_TO_STRUCT_PTR(p, s, f)  ((s *)(PTR_TO_UINT(p) - offsetof(s, f)))
#define FIELD_SIZE(s, f)                  (sizeof(((s *) 0)->f))

/***************************************************************************/

#define OBJ_CAST_FUNC(t) \
    static inline ovm_obj_ ## t ## _t  ovm_obj_ ## t (ovm_obj_t obj) { return ((ovm_obj_ ## t ## _t) obj); }

struct ovm_obj_str {                                    
    struct ovm_obj base[1];                     
    unsigned size;                              
    char     data[0];                           
};
typedef struct ovm_obj_str *ovm_obj_str_t;
OBJ_CAST_FUNC(str);

struct ovm_obj_barray {                                 
    struct ovm_obj base[1];                     
    unsigned      size;                         
    unsigned char data[0];                      
};
typedef struct ovm_obj_barray *ovm_obj_barray_t;
OBJ_CAST_FUNC(barray);

struct ovm_obj_pair {
    struct ovm_obj base[1];
    struct ovm_inst first[1], second[1];
};
typedef struct ovm_obj_pair *ovm_obj_pair_t;
OBJ_CAST_FUNC(pair);

struct ovm_obj_list {
    struct ovm_obj  base[1];
    struct ovm_inst item[1];
    ovm_obj_t       next;
};
typedef struct ovm_obj_list *ovm_obj_list_t;
OBJ_CAST_FUNC(list);

struct ovm_obj_array {                                  
    struct ovm_obj  base[1];                    
    unsigned        size;                               
    struct ovm_inst data[0];                    
};
typedef struct ovm_obj_array *ovm_obj_array_t;
OBJ_CAST_FUNC(array);

struct ovm_obj_slice {
    struct ovm_obj  base[1];                    
    ovm_obj_t  underlying;
    unsigned   ofs, size;
};
typedef struct ovm_obj_slice *ovm_obj_slice_t;
OBJ_CAST_FUNC(slice);

struct ovm_obj_set {                                    
    struct ovm_obj base[1];                     
    unsigned       size, cnt;                          
    ovm_obj_t      data[0];                             
};
typedef struct ovm_obj_set *ovm_obj_set_t;
OBJ_CAST_FUNC(set);

struct ovm_obj_class {
    struct ovm_obj base[1];
    ovm_obj_t name, parent, ns, cl_vars, cl_methods, inst_methods;
    void (*mark)(ovm_obj_t obj);
    void (*free)(ovm_obj_t obj);
    void (*cleanup)(ovm_obj_t obj);
};
typedef struct ovm_obj_class *ovm_obj_class_t;
OBJ_CAST_FUNC(class);

struct ovm_obj_ns {
    struct ovm_obj base[1];
    ovm_obj_t name, parent, dict;
};
typedef struct ovm_obj_ns *ovm_obj_ns_t;
OBJ_CAST_FUNC(ns);

struct ovm_obj_file {
    struct ovm_obj base[1];
    ovm_obj_t filename, mode;
    FILE *fp;
};
typedef struct ovm_obj_file *ovm_obj_file_t;
OBJ_CAST_FUNC(file);

struct ovm_obj_module {
    struct ovm_obj_ns base[1];
    ovm_obj_t filename, sha1;
    void      *dlhdl;
};
typedef struct ovm_obj_module *ovm_obj_module_t;
OBJ_CAST_FUNC(module);

struct ovm_thread {
    pthread_t  id;
    struct ovm_dllist list_node[1];
    unsigned   stack_size_bytes;
    ovm_inst_t stack, stack_top, sp;
    unsigned   frame_stack_size;
    unsigned char *frame_stack, *frame_stack_top;
    struct ovm_frame    *fp;    /* Current frame */
    struct ovm_frame_ns *nsfp;  /* Current namespace */
    struct ovm_frame_mc *mcfp;  /* Current method */
    unsigned except_lvl;
    struct ovm_frame_except *xfp;
    bool exceptf;
    unsigned char *pc, *pc_instr_start;
    bool tracef;
    int _errno;
    unsigned fatal_lvl;
};

enum { OVM_FRAME_TYPE_NAMESPACE, OVM_FRAME_TYPE_METHOD_CALL, OVM_FRAME_TYPE_EXCEPTION };

struct ovm_frame {
    unsigned type;
    unsigned size;
};
typedef struct ovm_frame *ovm_frame_t;

struct ovm_frame_ns {
    struct ovm_frame base[1];
    struct ovm_frame_ns *prev;
    ovm_obj_ns_t ns;
};

struct ovm_frame_mc {
    struct ovm_frame base[1];
    struct ovm_frame_mc *prev;
    ovm_obj_class_t cl;
    ovm_inst_t      method;
    ovm_inst_t      dst;
    ovm_inst_t      bp;
    unsigned        argc;
    ovm_inst_t      ap;
};

struct ovm_frame_except {
    struct ovm_frame base[1];
    struct ovm_frame_except *prev;
    ovm_inst_t    arg;
    bool          arg_valid;
    ovm_inst_t    sp;
    unsigned char *pc;
    jmp_buf       jb;
};

struct ovm_clist {
    struct ovm_dllist buf_list[1];
    unsigned      len;
};

/***************************************************************************/

#define _OVM_STR_CONST(s)  sizeof(s), s
#define _STRINGIZE(s)  # s
#define STRINGIZE(s)  _STRINGIZE(s)
#define OVM_STR_CONST(s)   _OVM_STR_CONST(STRINGIZE(s))
#define _OVM_STR_CONST_HASH(s)  _OVM_STR_CONST(s), __str_hash__(s)
#define OVM_STR_CONST_HASH(s)  _OVM_STR_CONST_HASH(STRINGIZE(s))
#define _OVM_STR_CONST_HASHC(s)  _OVM_STR_CONST(s), true, __str_hash__(s)
#define OVM_STR_CONST_HASHC(s)  _OVM_STR_CONST_HASHC(STRINGIZE(s))

#define _CONCAT(a, b)  a ## b
#define CONCAT(a, b)  _CONCAT(a, b)

#define _METHOD_NAME(mod, cl, func)  CONCAT(CONCAT(CONCAT(CONCAT(mod, $), cl), $), func)
#define METHOD_NAME(nm)  _METHOD_NAME(METHOD_MODULE, METHOD_CLASS, nm)

#define _CM_DECL(nm) \
    void nm (ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
#define CM_DECL(nm)  _CM_DECL(METHOD_NAME(nm))
#define _METHOD_DECL(nm) \
    void nm (ovm_thread_t th)
#define METHOD_DECL(nm)  _METHOD_DECL(METHOD_NAME(nm))

/***************************************************************************/

void ovm_thread_fatal(ovm_thread_t th, unsigned line, unsigned exit_code, const char *fmt, ...) __attribute__((noreturn));

#define OVM_THREAD_FATAL(th, exit_code, fmt, ...)  ovm_thread_fatal((th), __LINE__, (exit_code), (fmt), ## __VA_ARGS__)

static inline ovm_inst_t _ovm_stack_alloc(ovm_thread_t th, unsigned n)
{
    ovm_inst_t p = th->sp - n;
    if (p < th->stack)  OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_STACK_OVERFLOW, 0);
    return (p);
}

extern pthread_mutex_t ovm_objs_mutex[1];

static inline void _ovm_objs_lock(void)
{
    pthread_mutex_lock(ovm_objs_mutex);
}

static inline void _ovm_objs_unlock(void)
{
    pthread_mutex_unlock(ovm_objs_mutex);
}

#define OVM_INST_INIT(_dst, _type, _field, _val)        \
    _dst->type = _type;  _dst->hash_valid = false;  _dst->_field = _val

/* Assignment
   - Lock already held
   - Source retained
   - Destination as uninitialized, old dst not released
 */

void _ovm_obj_free(ovm_obj_t obj); /* Lock already held */

static inline void ovm_obj_retain(ovm_obj_t obj) /* Lock already held */
{
    if (obj == 0)  return;
    obj->ref_cnt++;
    DEBUG_ASSERT(obj->ref_cnt != 0);
}

static inline void _ovm_obj_assign_nolock_norelease(ovm_obj_t *dst, ovm_obj_t src) /* Lock already held */
{
    ovm_obj_retain(*dst = src);
}

static inline void _ovm_inst_assign_obj_nolock_norelease(ovm_inst_t dst, ovm_obj_t obj) /* Lock already held */
{
    OVM_INST_INIT(dst, OVM_INST_TYPE_OBJ, objval, obj);
    ovm_obj_retain(obj);
}

static inline void ovm_obj_release(ovm_obj_t obj); /* Lock already held */

static inline void ovm_inst_retain(ovm_inst_t inst) /* Lock already held */
{
    if (inst->type == OVM_INST_TYPE_OBJ)  ovm_obj_retain(inst->objval);
}

static inline void _ovm_inst_assign_nolock_norelease(ovm_inst_t dst, ovm_inst_t src) /* Lock already held */
{
#if 0  // Causes a SEGV with optimization, gcc assumes inst is aligned
    *dst = *src;
#else
    memcpy(dst, src, sizeof(*dst));
#endif
    ovm_inst_retain(dst);
}

/* Assignment
   - Lock already held
   - Source retained
   - Old destination released
 */

static inline void _ovm_obj_assign_nolock(ovm_obj_t *dst, ovm_obj_t src) /* Lock already held */
{
    ovm_obj_t old = *dst;
    _ovm_obj_assign_nolock_norelease(dst, src);
    ovm_obj_release(old);
}

static inline void _ovm_inst_assign_obj_nolock(ovm_inst_t dst, ovm_obj_t obj) /* Lock already held */
{
    ovm_obj_t old = dst->type == OVM_INST_TYPE_OBJ ? dst->objval : 0;
    _ovm_inst_assign_obj_nolock_norelease(dst, obj);
    ovm_obj_release(old);
}

static inline void _ovm_inst_assign_nolock(ovm_inst_t dst, ovm_inst_t src) /* Lock already held */
{
    ovm_obj_t old = (dst->type == OVM_INST_TYPE_OBJ) ? dst->objval : 0;
    _ovm_inst_assign_nolock_norelease(dst, src);
    ovm_obj_release(old);    
}

/* Assignment
   - Lock acquired
   - Source retained
   - Old destination released
 */

static inline ovm_obj_list_t ovm_list_next(ovm_obj_list_t li)
{
    return (ovm_obj_list(li->next));
}

#endif /* __OOVM_INTERNAL_H */
