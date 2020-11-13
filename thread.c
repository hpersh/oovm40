#include "oovm.h"

static ovm_obj_t cl_thread;

struct ovm_obj_thread {
    struct ovm_obj base[1];
    pthread_t      id;
};
typedef struct ovm_obj_thread *ovm_obj_thread_t;

static inline ovm_obj_thread_t ovm_obj_thread(ovm_obj_t obj)
{
    return ((ovm_obj_thread_t) obj);
}

static inline ovm_obj_thread_t ovm_inst_threadval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_thread(inst->objval));
}

static ovm_obj_thread_t ovm_inst_threadval(ovm_thread_t th, ovm_inst_t inst)
{
    if (ovm_inst_of_raw(inst) != ovm_obj_class(cl_thread))  ovm_except_inv_value(th, inst);
    return (ovm_inst_threadval_nochk(inst));
}

static void thread_cleanup(ovm_obj_t obj)
{
    pthread_cancel(ovm_obj_thread(obj)->id);
}

static void thread_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_thread(obj)->id = va_arg(ap, pthread_t);
}

ovm_obj_thread_t thread_newc(ovm_inst_t dst, pthread_t id)
{
    return (ovm_obj_thread(ovm_obj_alloc(dst, sizeof(*ovm_obj_thread(0)), ovm_obj_class(cl_thread), 1, thread_init, id)));
}

#define METHOD_MODULE  thread
#define METHOD_CLASS   Thread

CM_DECL(new)
{
    ovm_method_argc_chk_min(th, 2);
    switch (argv[1].type) {
    case OVM_INST_TYPE_CODEMETHOD:
    case OVM_INST_TYPE_METHOD:
	break;
    default:
	ovm_except_inv_value(th, &argv[1]);
    }

    ovm_thread_t newth = ovm_thread_create(0, 0);

    unsigned n = argc - 2;
    newth->sp -= n + 3;		/* ns method dst args --  */

    ovm_inst_t p, q;
    for (q = &argv[2], p = newth->sp; n > 0; --n, ++p, ++q)  ovm_inst_assign(p, q);
    ovm_inst_assign(++p, &argv[1]);
    ovm_stack_push_obj(th, ovm_consts.Namespace);
    ovm_method_callsch(th, th->sp, OVM_STR_CONST_HASH(current), 1);
    ovm_method_callsch(th, ++p, OVM_STR_CONST_HASH(parent), 1);
    ovm_stack_free(th, 1);

    pthread_create(&newth->id, 0, ovm_thread_entry, newth);

    thread_newc(dst, newth->id);
}

CM_DECL(current)
{
    thread_newc(dst, th->id);
}

CM_DECL(detach)
{
    pthread_detach(ovm_inst_threadval(th, &argv[0])->id);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(cancel)
{
    pthread_cancel(ovm_inst_threadval(th, &argv[0])->id);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(join)
{
    void *result;
    pthread_join(ovm_inst_threadval(th, &argv[0])->id, &result);
    ovm_int_newc(dst, PTR_TO_UINT(result));
}

/***************************************************************************/

static ovm_obj_t cl_mutex;

struct ovm_obj_mutex {
    struct ovm_obj  base[1];
    pthread_mutex_t mutex[1];
};
typedef struct ovm_obj_mutex *ovm_obj_mutex_t;

static inline ovm_obj_mutex_t ovm_obj_mutex(ovm_obj_t obj)
{
    return ((ovm_obj_mutex_t) obj);
}

static inline ovm_obj_mutex_t ovm_inst_mutexval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_mutex(inst->objval));
}

static ovm_obj_mutex_t ovm_inst_mutexval(ovm_thread_t th, ovm_inst_t inst)
{
    if (ovm_inst_of_raw(inst) != ovm_obj_class(cl_mutex))  ovm_except_inv_value(th, inst);
    return (ovm_inst_mutexval_nochk(inst));
}

static void mutex_cleanup(ovm_obj_t obj)
{
    pthread_mutex_destroy(ovm_obj_mutex(obj)->mutex);
}

static void mutex_init(ovm_obj_t obj, va_list ap)
{
    pthread_mutex_init(ovm_obj_mutex(obj)->mutex, 0);
}

static ovm_obj_mutex_t mutex_newc(ovm_inst_t dst)
{
    return (ovm_obj_mutex(ovm_obj_alloc(dst, sizeof(struct ovm_obj_mutex), ovm_obj_class(cl_mutex), 0, mutex_init)));
}

#undef  METHOD_CLASS
#define METHOD_CLASS  Mutex

CM_DECL(new)
{
    mutex_newc(dst);
}

CM_DECL(lock)
{
    pthread_mutex_lock(ovm_inst_mutexval(th, &argv[0])->mutex);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(unlock)
{
    pthread_mutex_unlock(ovm_inst_mutexval(th, &argv[0])->mutex);
    ovm_inst_assign(dst, &argv[0]);
}

/***************************************************************************/

void __thread_init__(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
    ovm_inst_t old = th->sp;

    ovm_stack_push(th, &argv[0]);
    
    ovm_stack_push_obj(th, ovm_consts.Object);
    ovm_class_new(th, OVM_STR_CONST_HASH(Thread), 0, thread_cleanup, thread_cleanup);
    cl_thread = th->sp->objval;

#undef  METHOD_CLASS
#define METHOD_CLASS  Thread
  
    ovm_classmethod_add(th, OVM_STR_CONST_HASH(new),     METHOD_NAME(new));
    ovm_classmethod_add(th, OVM_STR_CONST_HASH(current), METHOD_NAME(current));
    ovm_method_add(th, OVM_STR_CONST_HASH(detach),  METHOD_NAME(detach));
    ovm_method_add(th, OVM_STR_CONST_HASH(cancel),  METHOD_NAME(cancel));
    ovm_method_add(th, OVM_STR_CONST_HASH(join),    METHOD_NAME(join));
  
    ovm_stack_free(th, 1);

    ovm_stack_push_obj(th, ovm_consts.Object);
    ovm_class_new(th, OVM_STR_CONST_HASH(Mutex), 0, mutex_cleanup, mutex_cleanup);
    cl_mutex = th->sp->objval;

#undef  METHOD_CLASS
#define METHOD_CLASS  Mutex
  
    ovm_classmethod_add(th, OVM_STR_CONST_HASH(new), METHOD_NAME(new));
    ovm_method_add(th, OVM_STR_CONST_HASH(lock),   METHOD_NAME(lock));
    ovm_method_add(th, OVM_STR_CONST_HASH(unlock), METHOD_NAME(unlock));

    ovm_stack_unwind(th, old);
}

// Local Variables:
// c-basic-offset: 4
// End:
