/***************************************************************************
 ***************************************************************************
 *
 * Source for main library, implementing virtual machine
 *
 * Main points
 * - A namespace, called "main", that contains all predefined classes
 * - Support for multiple threads
 * - Each thread has an instance stack, and a frame stack
 *   - Instance stack is used for scratch space and passing arguments to
 *     methods
 *   - Frame stack is used for tracking entry into namespaces, calling
 *     methods, and catching exceptions
 *
 ***************************************************************************
 ***************************************************************************/

#include "oovm.h"
#include "oovm_hash.h"

#include <ctype.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>

/***************************************************************************/

/* Utility functions */

static unsigned round_up_to_power_of_2(unsigned val)
{
    unsigned result = val;
    for (;;) {
        unsigned k = val & (val - 1);
        if (k == 0)  return (result);
        result = (val = k) << 1;
        DEBUG_ASSERT(result != 0);
    }
}

static unsigned ulog2(unsigned val)
{
    DEBUG_ASSERT(val != 0 && (val & (val - 1)) == 0);
    unsigned k;
    for (k = 0; val != 0; val >>= 1, ++k);
    return (k - 1);
}

static bool slice(ovm_intval_t *pofs, ovm_intval_t *plen, ovm_intval_t size)
{
    ovm_intval_t ofs = *pofs, len = *plen;
    if (ofs < 0)  ofs += size;
    if (len < 0)  {
        ofs += len;
        len = -len;
    }
    if (ofs < 0 || (ofs + len) > size) return (false);
    *pofs = ofs;
    *plen = len;
    return (true);
}

static inline bool slice1(ovm_intval_t *pofs, ovm_intval_t size)
{
    ovm_intval_t len = 1;
    return (slice(pofs, &len, size));
}

/***************************************************************************/

/* Fatal error handling */

static void fatal(const char *fmt, ...)
{
    fflush(stdout);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
    exit(1);
}

static const char *fatal_mesg(unsigned exit_code)
{
    static const char * const tbl[] = {
        "Aborted",
        "Assertion failed",
        "Invalid instruction",
        "Stack overflow",
        "Stack underflow",
        "Frame stack overflow",
        "Frame stack underflow",
        "No frame",
        "Stack access range",
        "Uncaught exception",
        "Double exception",
    };

    if (exit_code < _OVM_THREAD_FATAL_FIRST)  return (0);
    unsigned ofs = exit_code - _OVM_THREAD_FATAL_FIRST;
    if (ofs >= ARRAY_SIZE(tbl))  return (0);
    return (tbl[ofs]);
};

static ovm_thread_t main_thread;
static void backtrace(ovm_thread_t th);

__attribute__((noreturn))
void ovm_thread_fatal(ovm_thread_t th, unsigned line, unsigned exit_code, const char *fmt, ...)
{
    fflush(stdout);
    fprintf(stderr, "Thread %lu fatal", pthread_self());
    const char *p = fatal_mesg(exit_code);
    if (p != 0)  fprintf(stderr, ": %s", p);
    if (fmt != 0) {
        fputs(" - ", stderr);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    fputc('\n', stderr);
    fflush(stderr);

    ++th->fatal_lvl;
    if (th->fatal_lvl == 1)  backtrace(th);
    
    if (th == main_thread)  exit(exit_code);
    pthread_exit((void *)(intptr_t) exit_code);
}

#define OVM_THREAD_FATAL(th, exit_code, fmt, ...)  ovm_thread_fatal((th), __LINE__, (exit_code), (fmt), ## __VA_ARGS__)

/***************************************************************************/

static bool dict_ats(ovm_inst_t dst, ovm_obj_set_t s, unsigned size, const char *data, ovm_intval_t hash);

static inline void user_obj_inst_get(ovm_inst_t dst, ovm_obj_t obj)
{
#ifndef NDEBUG
    bool f =
#endif
        dict_ats(dst, ovm_obj_set(obj), _OVM_STR_CONST_HASH("__instanceof__"));
#ifndef NDEBUG
    assert(f);
#endif
    ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
}

void ovm_obj_inst_of(ovm_inst_t dst, ovm_obj_t obj)
{
    ovm_obj_class_t cl = ovm_obj_inst_of_raw(obj);

    if (cl != OVM_CL_USER) {
        ovm_inst_assign_obj(dst, cl->base);
        return;
    }

    user_obj_inst_get(dst, obj);
}

void ovm_inst_of(ovm_inst_t dst, ovm_inst_t src)
{
    ovm_obj_class_t cl = ovm_inst_of_raw(src);

    if (cl != OVM_CL_USER) {
        ovm_inst_assign_obj(dst, cl->base);
        return;
    }

    user_obj_inst_get(dst, src->objval);
}

/***************************************************************************/

/* Memory management */

struct mem_stat {
    unsigned alloced, freed, collected, in_use, in_use_max;
};

__attribute__((unused))
static void mem_stat_print(FILE *fp, struct mem_stat *s)
{
    fprintf(fp, "alloced=%u, freed=%u, collected=%u, in_use=%u, in_use_max=%u",
            s->alloced, s->freed, s->collected, s->in_use, s->in_use_max
            );
}

static inline void mem_stat_update_alloc(struct mem_stat *s, unsigned n)
{
    s->alloced += n;
    if ((s->in_use += n) > s->in_use_max)  s->in_use_max = s->in_use;
}

static bool collectingf, collect_againf;

static inline void mem_stat_update_free(struct mem_stat *s, unsigned n)
{
    *(collectingf ? &s->collected : &s->freed) += n;
    s->in_use -= n;
}

struct mem_buf {
    struct ovm_dllist list_node[1];     /* Link for free buf list */
};

struct mem_buf_page {
    struct ovm_dllist list_node[1];     /* Link for page list */
    unsigned     buf_tbl_idx;   /* Index into mem_buf_tbl */
    unsigned     in_use_buf_cnt;        /* Number of buffers in page in use */
};

static struct mem_buf_info {
    unsigned  buf_size;             /* Buffer size */
    struct ovm_dllist    page_list[1];      /* List of allocated pages */
    struct ovm_dllist    free_buf_list[1]; /* List of free buffers */
    struct mem_stat page_stats[1];    /* Page statistics */
    struct mem_stat buf_stats[1];     /* Buffer statistics */
} *mem_buf_tbl;

/* TODO: Add some page hysteresis */

static unsigned long mem_page_size, mem_page_size_log2;

static inline void *page_align(void *buf)
{
    return ((void *)(PTR_TO_UINT(buf) & ~(mem_page_size - 1)));
}

static inline struct mem_buf_page *buf_to_page(void *p)
{
    return ((struct mem_buf_page *) page_align(p));
}

static inline unsigned bytes_to_pages(unsigned bytes)
{
    return (((bytes - 1) >> mem_page_size_log2) + 1);
}

static inline unsigned pages_to_bytes(unsigned npages)
{
    return (npages << mem_page_size_log2);
}

static inline unsigned bytes_page_align(unsigned bytes)
{
    return (pages_to_bytes(bytes_to_pages(bytes)));
}

static void collect(void);

static unsigned mem_collect_alloc_cnt;
enum {
    MEM_COLLECT_ALLOC_LIMIT = 1000000
};

static struct mem_stat mem_pages_stats[1];

static inline void *mem_pages_alloc(unsigned npages)
{
    void *result;
    unsigned try;
    for (try = 0;; ++try) {
        result = mmap(0, pages_to_bytes(npages), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (result != MAP_FAILED)  break;
        if (try >= 1)  fatal("Out of memory");
        collect();
    }
    mem_stat_update_alloc(mem_pages_stats, npages);
    return (result);
}

static inline void mem_pages_free(void *p, unsigned npages)
{
    munmap(p, pages_to_bytes(npages));
    mem_stat_update_free(mem_pages_stats, npages);
}

static pthread_mutex_t mutex_mem[1] = { PTHREAD_MUTEX_INITIALIZER };

static inline void mem_lock(void)
{
    pthread_mutex_lock(mutex_mem);
}

static inline void mem_unlock(void)
{
    pthread_mutex_unlock(mutex_mem);
}

#ifndef NDEBUG
static bool mem_trace = 0;
#define MEM_TRACE(fmt, ...)  if (mem_trace)  printf(fmt, __VA_ARGS__);
#else
#define MEM_TRACE(fmt, ...)
#endif

static unsigned mem_buf_tbl_size;

void *ovm_mem_alloc(unsigned size, int hint, bool clrf)
{
    void *result = 0;

    mem_lock();
  
    ++mem_collect_alloc_cnt;
    if (mem_collect_alloc_cnt >= MEM_COLLECT_ALLOC_LIMIT) {
        collect();
        mem_collect_alloc_cnt = 0;
    }

    if (size > ovm_mem_max_buf_size) {
        result = mem_pages_alloc(bytes_to_pages(size));
    } else {
        unsigned i = 0;
        if (hint >= 0 && hint < mem_buf_tbl_size && size <= mem_buf_tbl[hint].buf_size) {
            i = hint;
        } else {
            int cmp = 0;
            unsigned a, b;
            for (a = 0, b = mem_buf_tbl_size; b > a; ) {
                i = (a + b) / 2;
                if (size == mem_buf_tbl[i].buf_size) {
                    cmp = 0;
                    break;
                }
                if (size < mem_buf_tbl[i].buf_size) {
                    cmp = -1;
                    b   = i;
                    continue;
                }
                cmp = 1;
                a   = i + 1;
            }
            if (cmp > 0)  ++i;
        }
        struct mem_buf_info *bi = &mem_buf_tbl[i];
        if (ovm_dllist_empty(bi->free_buf_list)) {
            struct mem_buf_page *page = (struct mem_buf_page *) mem_pages_alloc(1);
            ovm_dllist_insert(page->list_node, ovm_dllist_end(bi->page_list));
            page->buf_tbl_idx = i;
            unsigned char *p;
            unsigned      rem;
            for (p = (unsigned char *)(page + 1), rem = mem_page_size - sizeof(*page); rem >= bi->buf_size; rem -= bi->buf_size, p += bi->buf_size) {
                ovm_dllist_insert(((struct mem_buf *) p)->list_node, ovm_dllist_end(bi->free_buf_list));
            }
            mem_stat_update_alloc(bi->page_stats, 1);
        }
        struct mem_buf *b = FIELD_PTR_TO_STRUCT_PTR(ovm_dllist_last(bi->free_buf_list), struct mem_buf, list_node);
        ovm_dllist_erase(b->list_node);
        ++buf_to_page(b)->in_use_buf_cnt;
        if (clrf)  memset(b, 0, bi->buf_size);
        mem_stat_update_alloc(bi->buf_stats, 1);
        result = b;
    }
  
    mem_unlock();

    MEM_TRACE("_mem_alloc(size=%u, hint=%d, clrf=%u) = %p\n", size, hint, clrf, result);    
    
    return (result);
}

void ovm_mem_free(void *ptr, unsigned size)
{
    MEM_TRACE("mem_free(ptr=%p, size=%u)\n", ptr, size);
    
    mem_lock();

    if (size > ovm_mem_max_buf_size) {
        mem_pages_free(ptr, bytes_to_pages(size));
    } else {
        struct mem_buf *q = (struct mem_buf *) ptr;
        struct mem_buf_page *page = buf_to_page(q);
        struct mem_buf_info *bi = &mem_buf_tbl[page->buf_tbl_idx];
        ovm_dllist_insert(q->list_node, ovm_dllist_end(bi->free_buf_list));
        mem_stat_update_free(bi->buf_stats, 1);
        if (--page->in_use_buf_cnt == 0) {
            unsigned char *p;
            unsigned      rem;
            for (p = (unsigned char *)(page + 1), rem = mem_page_size - sizeof(*page);
                 rem >= bi->buf_size;
                 rem -= bi->buf_size, p += bi->buf_size
                 ) {
                ovm_dllist_erase(((struct mem_buf *) p)->list_node);
            }
            ovm_dllist_erase(page->list_node);
            mem_pages_free(page, 1);
            mem_stat_update_free(bi->page_stats, 1);
        }
    }

    mem_unlock();
}

static void mem_init(void)
{
    mem_page_size      = sysconf(_SC_PAGE_SIZE);
    mem_page_size_log2 = ulog2(mem_page_size);
    ovm_mem_max_buf_size   = mem_page_size >> 2;
    mem_buf_tbl_size   = (mem_page_size_log2 - 2) + 1 - OVM_MEM_MIN_BUF_SIZE_LOG2;

    mem_buf_tbl = (struct mem_buf_info *) calloc(mem_buf_tbl_size, sizeof(*mem_buf_tbl));
    if (mem_buf_tbl == 0)  fatal("Out of memory");
    unsigned buf_size, i;
    for (i = 0, buf_size = OVM_MEM_MIN_BUF_SIZE; buf_size <= ovm_mem_max_buf_size; buf_size <<= 1, ++i) {
        mem_buf_tbl[i].buf_size = buf_size;
        ovm_dllist_init(mem_buf_tbl[i].page_list);
        ovm_dllist_init(mem_buf_tbl[i].free_buf_list);
    }
}

/***************************************************************************/

/* Object management */

static struct ovm_dllist _obj_list[2], *obj_list_white = &_obj_list[0], *obj_list_grey = &_obj_list[1];

pthread_mutex_t ovm_objs_mutex[1];
static pthread_mutexattr_t obj_mutex_attr[1];

static void objs_init(void)
{
  ovm_dllist_init(obj_list_white);
  ovm_dllist_init(obj_list_grey);
  pthread_mutex_init(ovm_objs_mutex, 0);
  pthread_mutexattr_init(obj_mutex_attr);
  pthread_mutexattr_settype(obj_mutex_attr, PTHREAD_MUTEX_ERRORCHECK);
}

void ovm_debug_obj_chk();

static void class_mark(ovm_obj_t obj), class_free(ovm_obj_t obj);

void ovm_obj_mark(ovm_obj_t obj) /* Lock already held */
{
    if (obj == 0 || ++obj->ref_cnt > 1)  return;
    ovm_dllist_erase(obj->list_node);
    ovm_dllist_insert(obj->list_node, ovm_dllist_end(obj_list_white));
    ovm_obj_class_t cl = ovm_obj_inst_of_raw(obj);
    ovm_obj_mark(cl->base);
    void (*f)(ovm_obj_t) = (cl == 0) ? class_mark : cl->mark;
    if (f != 0)  (*f)(obj);
}

static inline void obj_destroy(ovm_obj_t obj) /* Lock already held */
{
    pthread_mutex_destroy(obj->mutex);
    ovm_mem_free(obj, obj->size);
}

void _ovm_obj_free(ovm_obj_t obj) /* Lock already held */
{
    if (collectingf) {
        collect_againf = true;
        return;
    }
    
    ovm_dllist_erase(obj->list_node);
    ovm_obj_class_t cl = ovm_obj_inst_of_raw(obj);
    ovm_obj_release(cl->base);
    void (*f)(ovm_obj_t) = (cl == 0) ? class_free : cl->free;
    if (f != 0)  (*f)(obj);
    obj_destroy(obj);
}

static ovm_obj_t _obj_alloc(ovm_inst_t dst, unsigned size, ovm_obj_class_t cl, int mem_hint, void (*init)(ovm_obj_t, va_list), va_list ap)
{
    _ovm_objs_lock();

    ovm_obj_t result = (ovm_obj_t) ovm_mem_alloc(size, mem_hint, true);
    ovm_dllist_insert(result->list_node, ovm_dllist_end(obj_list_white));
    result->size = size;
    _ovm_obj_assign_nolock_norelease(&result->inst_of, cl->base);
    pthread_mutex_init(result->mutex, obj_mutex_attr);
    if (init != 0)  (*init)(result, ap);
    _ovm_inst_assign_obj_nolock(dst, result);

    _ovm_objs_unlock();

    return (result);
}

ovm_obj_t ovm_obj_alloc(ovm_inst_t dst, unsigned size, ovm_obj_class_t cl, int mem_hint, void (*init)(ovm_obj_t, va_list), ...)
{
    va_list ap;
    va_start(ap, init);

    ovm_obj_t result = _obj_alloc(dst, size, cl, mem_hint, init, ap);

    va_end(ap);
    return (result);
}

static inline int _obj_lock(ovm_obj_t obj)
{
    return (pthread_mutex_lock(obj->mutex));
}

static inline void obj_lock(ovm_obj_t obj)
{
#if 0
    DEBUG_ASSERT(_obj_lock(obj) == 0);
#else
    int rc = _obj_lock(obj);
    if (rc != 0) {
        fprintf(stderr, "_obj_lock() => %d, errno = %d\n", rc, errno);
        abort();
    }
#endif
}

static inline void obj_lock_loop_chk(ovm_thread_t th, ovm_obj_t obj)
{
    int rc = _obj_lock(obj);
    if (rc == 0)  return;
    assert(rc == EDEADLK);
    ovm_except_descent_loop(th);
}

static inline void obj_unlock(ovm_obj_t obj)
{
    pthread_mutex_unlock(obj->mutex);
}

struct ovm_consts ovm_consts;
static ovm_obj_t ns_main;
static struct ovm_dllist thread_list[1];

static void collect(void)
{
    collectingf = true;

    do {
        collect_againf = false;
        
        struct ovm_dllist *p = obj_list_white;
        obj_list_white = obj_list_grey;
        obj_list_grey = p;
        for (p = ovm_dllist_first(obj_list_grey); p != ovm_dllist_end(obj_list_grey); p = ovm_dllist_next(p)) {
            FIELD_PTR_TO_STRUCT_PTR(p, struct ovm_obj, list_node)->ref_cnt = 0;
        }
    
        {
            unsigned n;
            ovm_obj_t *q;
            for (q = (ovm_obj_t *) &ovm_consts, n = sizeof(ovm_consts); n > 0; n -= sizeof(*q), ++q) {
                ovm_obj_mark(*q);
            }
        }
        ovm_obj_mark(ns_main);
        for (p = ovm_dllist_first(thread_list); p != ovm_dllist_end(thread_list); p = ovm_dllist_next(p)) {
            ovm_thread_t th = FIELD_PTR_TO_STRUCT_PTR(p, struct ovm_thread, list_node);
            ovm_inst_t q;
            for (q = th->sp; q < th->stack_top; ++q)  ovm_inst_mark(q);
        }

        while ((p = ovm_dllist_first(obj_list_grey)) != ovm_dllist_end(obj_list_grey)) {
            ovm_obj_t obj = FIELD_PTR_TO_STRUCT_PTR(p, struct ovm_obj, list_node);
            ovm_obj_class_t cl = ovm_obj_inst_of_raw(obj);
            if (cl != 0) {
                void (*f)(ovm_obj_t) = cl->cleanup;
                if (f != 0)  (*f)(obj);
            }
            ovm_dllist_erase(p);
            obj_destroy(obj);
        }    
    } while (collect_againf);
    
    collectingf = false;
}

/***************************************************************************/

/* Frame management */

static struct ovm_frame *frame_push(ovm_thread_t th, unsigned type, unsigned size)
{
    unsigned char *p = (unsigned char *) th->fp - size;
    if (p < th->frame_stack) {
        OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_FRAME_STACK_OVERFLOW, 0);
    }
    struct ovm_frame *fr = (struct ovm_frame *) p;
    fr->type = type;
    fr->size = size;
    th->fp = fr;
    return (fr);
}

static struct ovm_frame *frame_ns_push(ovm_thread_t th, ovm_obj_ns_t ns)
{
    struct ovm_frame_ns *fr = (struct ovm_frame_ns *) frame_push(th, OVM_FRAME_TYPE_NAMESPACE, sizeof(*fr));
    fr->ns = ns;
    fr->prev = th->nsfp;
    th->nsfp = fr;
    return (fr->base);
}

static inline struct ovm_frame_ns *thread_nsfp(ovm_thread_t th)
{
    struct ovm_frame_ns *result = th->nsfp;
    if (result == 0)  OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_NO_FRAME, 0);
    return (result);
}

static ovm_obj_ns_t ns_up(ovm_thread_t th, unsigned n)
{
    struct ovm_frame_ns *fr;
    for (fr = thread_nsfp(th); fr != 0; fr = fr->prev, --n) {
        if (n == 0)  return (fr->ns);
    }

    assert(0);                  /* Should never happen, main module is always topmost */
    
    return (0);
}

static ovm_obj_module_t module_cur(ovm_obj_ns_t ns)
{
    for (; ns != 0; ns = ovm_obj_ns(ns->parent)) {
        if (ovm_obj_inst_of_raw(ns->base) == OVM_CL_MODULE)  return (ovm_obj_module(ns->base));
    }
    
    assert(0);                  /* Should never happen, main module is always topmost */

    return (0);
}

static struct ovm_frame *frame_mc_push(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_inst_t method, unsigned argc, ovm_inst_t argv)
{
    struct ovm_frame_mc *fr = (struct ovm_frame_mc *) frame_push(th, OVM_FRAME_TYPE_METHOD_CALL, sizeof(*fr));
    fr->dst      = dst;
    fr->cl       = cl;
    fr->method   = method;
    fr->bp       = th->sp;
    fr->argc     = argc;
    fr->ap       = argv;
    fr->prev = th->mcfp;
    th->mcfp = fr;
    return (fr->base);
}

static inline struct ovm_frame_mc *thread_mcfp(ovm_thread_t th)
{
    struct ovm_frame_mc *result = th->mcfp;
    if (result == 0)  OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_NO_FRAME, 0);
    return (result);
}

static ovm_obj_class_t class_up(ovm_thread_t th, unsigned n)
{
    struct ovm_frame_mc *fr;
    for (fr = thread_mcfp(th); fr != 0; fr = fr->prev) {
        ovm_obj_class_t cl = fr->cl;
        if (cl == 0)  continue;
        if (n == 0)  return (cl);
        --n;
    }
    return (0);
}

struct __jmp_buf_tag *ovm_frame_except_push(ovm_thread_t th, ovm_inst_t arg)
{
    struct ovm_frame_except *fr = (struct ovm_frame_except *) frame_push(th, OVM_FRAME_TYPE_EXCEPTION, sizeof(*fr));
    fr->arg       = arg;
    fr->arg_valid = false;
    fr->sp        = th->sp;
    fr->pc        = th->pc;
    fr->prev      = th->xfp;
    th->xfp       = fr;
    return (fr->jb);
}

static unsigned frame_pop1(ovm_thread_t th)
{
    struct ovm_frame *fp = th->fp;
    unsigned char *p = (unsigned char *) fp + fp->size;
    if (p > th->frame_stack_top) {
        OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_FRAME_STACK_UNDERFLOW, 0);
    }

    unsigned type = fp->type;
    switch (type) {
    case OVM_FRAME_TYPE_NAMESPACE:
        th->nsfp = ((struct ovm_frame_ns *) fp)->prev;
        break;
    case OVM_FRAME_TYPE_METHOD_CALL:
        {
            struct ovm_frame_mc *fr = (struct ovm_frame_mc *) fp;
            ovm_stack_unwind(th, fr->bp);
            th->mcfp = fr->prev;
        }
        break;
    case OVM_FRAME_TYPE_EXCEPTION:
        {
            struct ovm_frame_except *fr = (struct ovm_frame_except *) fp;
            ovm_stack_unwind(th, fr->sp);
            th->xfp = fr->prev;
        }
        break;
    default:
        assert(0);
    }

    th->fp = (struct ovm_frame *) p;

    return (type);
}

static void frame_unwind(ovm_thread_t th, struct ovm_frame *fp)
{
    if ((unsigned char *) th->fp >= th->frame_stack_top) {
        OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_FRAME_STACK_UNDERFLOW, 0);
    }
    while (th->fp < fp)  frame_pop1(th);
}

static void frame_pop(ovm_thread_t th, struct ovm_frame *fp)
{
    if ((unsigned char *) th->fp >= th->frame_stack_top) {
        OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_FRAME_STACK_UNDERFLOW, 0);
    }
    while (th->fp <= fp)  frame_pop1(th);
}

void ovm_frame_except_pop(ovm_thread_t th, unsigned n)
{
    if (th->except_lvl > 0)  --th->except_lvl;
    DEBUG_ASSERT(th->except_lvl == 0);
    if ((unsigned char *) th->fp >= th->frame_stack_top) {
        OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_FRAME_STACK_UNDERFLOW, 0);
    }
    while (n > 0) {
        if (frame_pop1(th) == OVM_FRAME_TYPE_EXCEPTION)  --n;
    }
}

/***************************************************************************/

/* Thread management */

ovm_thread_t ovm_thread_create(unsigned stack_size, unsigned frame_stack_size)
{
    if (stack_size == 0)        stack_size = 8192;
    if (frame_stack_size == 0)  frame_stack_size = mem_page_size;
    
    _ovm_objs_lock();

    ovm_thread_t th = (ovm_thread_t) ovm_mem_alloc(sizeof(*th), 4, true);
    th->stack_size_bytes = stack_size * sizeof(th->sp[0]);
    th->stack = (ovm_inst_t) ovm_mem_alloc(th->stack_size_bytes, OVM_MEM_ALLOC_NO_HINT, true);
    th->sp = th->stack_top = th->stack + stack_size;
    th->frame_stack_size = frame_stack_size;
    th->frame_stack = (unsigned char *) ovm_mem_alloc(th->frame_stack_size, OVM_MEM_ALLOC_NO_HINT, true);
    th->frame_stack_top = th->frame_stack + th->frame_stack_size;
    th->fp = (struct ovm_frame *) th->frame_stack_top;

    ovm_dllist_insert(th->list_node, thread_list);

    _ovm_objs_unlock();

    return (th);
}

static void method_run(ovm_thread_t th, ovm_inst_t dst, ovm_obj_ns_t ns, ovm_obj_class_t cl, ovm_inst_t method, unsigned argc, ovm_inst_t argv);

static pthread_key_t pthread_key_self;

void *ovm_thread_entry(void *arg)
{
    ovm_thread_t th = (ovm_thread_t) arg;
    
    pthread_setspecific(pthread_key_self, th);

    ovm_inst_t sp = th->sp, top = th->stack_top, dst = &top[-3];
    method_run(th, dst, ovm_inst_nsval_nochk(&top[-1]), 0, &top[-2], dst - sp, sp);

    pthread_exit((void *)(intptr_t)(dst->type == OVM_INST_TYPE_INT ? dst->intval : 0));
}

static void pthread_destructor(void *p)
{
    ovm_thread_t th = (ovm_thread_t) p;

    _ovm_objs_lock();

    ovm_dllist_erase(th->list_node);    
    ovm_inst_t q;
    for (q = th->sp; q > th->stack_top; ++q)  ovm_inst_release(q);

    _ovm_objs_unlock();
    
    ovm_mem_free(th->frame_stack, th->frame_stack_size);
    ovm_mem_free(th->stack, th->stack_size_bytes);
    ovm_mem_free(th, sizeof(*th));
}

static ovm_thread_t threading_init(unsigned stack_size, unsigned frame_stack_size)
{
    pthread_key_create(&pthread_key_self, pthread_destructor);
    
    ovm_dllist_init(thread_list);

    main_thread = ovm_thread_create(stack_size, frame_stack_size);
    pthread_setspecific(pthread_key_self, main_thread);
    main_thread->id = pthread_self();

    return (main_thread);
}

/***************************************************************************/

/* Clists */

struct clist_buf_hdr {
    struct ovm_dllist list_node[1];
    unsigned        len;
};

#define MEM_MIN_BLK_SIZE  64

struct clist_buf {
    struct clist_buf_hdr hdr[1];
    char                 data[MEM_MIN_BLK_SIZE - sizeof(struct clist_buf_hdr)];
};

void ovm_clist_init(struct ovm_clist *cl)
{
    ovm_dllist_init(cl->buf_list);
    cl->len = 0;
}

void ovm_clist_appendc(struct ovm_clist *cl, unsigned n, const char *s)
{
    if (n < 1)  return;
    --n;
    cl->len += n;  
    unsigned k;
    struct clist_buf *q;
    struct ovm_dllist *p = ovm_dllist_last(cl->buf_list);
    if (p != ovm_dllist_end(cl->buf_list)) {
        q = FIELD_PTR_TO_STRUCT_PTR(FIELD_PTR_TO_STRUCT_PTR(p, struct clist_buf_hdr, list_node), struct clist_buf, hdr);
        k = sizeof(q->data) - q->hdr->len;
        if (n < k)  k = n;
        memcpy(q->data + q->hdr->len, s, k);
        q->hdr->len += k;
        n -= k;
        s += k;
    }
    while (n > 0) {
        q = (struct clist_buf *) ovm_mem_alloc(OVM_MEM_MIN_BUF_SIZE, 0, false);
        ovm_dllist_insert(q->hdr->list_node, ovm_dllist_end(cl->buf_list));
        k = sizeof(q->data);
        if (n < k)  k = n;
        memcpy(q->data, s, k);
        q->hdr->len = k;
        n -= k;
        s += k;
    }
}

void ovm_clist_appendc1(struct ovm_clist *cl, const char *s)
{
    ovm_clist_appendc(cl, strlen(s) + 1, s);
}
     
void ovm_clist_append_char(struct ovm_clist *cl, char c)
{
    ovm_clist_appendc(cl, 2, &c);
}

void ovm_clist_append_str(ovm_thread_t th, struct ovm_clist *cl, ovm_obj_str_t s)
{
    ovm_clist_appendc(cl, s->size, s->data);
}

unsigned ovm_clist_to_barray(unsigned buf_size, unsigned char *buf, struct ovm_clist *cl)
{
    unsigned result = 0;
    struct ovm_dllist *p;
    unsigned rem = buf_size;
    for (p = ovm_dllist_first(cl->buf_list); rem > 0 && p != ovm_dllist_end(cl->buf_list); p = ovm_dllist_next(p)) {
        struct clist_buf *q = FIELD_PTR_TO_STRUCT_PTR(FIELD_PTR_TO_STRUCT_PTR(p, struct clist_buf_hdr, list_node), struct clist_buf, hdr);
        unsigned n = q->hdr->len;
        if (n > rem)  n = rem;
        memcpy(buf, q->data, n);
        buf += n;
        rem -= n;
        result += n;
    }

    return (result);
}

void ovm_clist_concat(struct ovm_clist *cl1, struct ovm_clist *cl2)
{
    struct ovm_dllist *p;
    for (p = ovm_dllist_first(cl2->buf_list); p != ovm_dllist_end(cl2->buf_list); p = ovm_dllist_next(p)) {
        struct clist_buf *q = FIELD_PTR_TO_STRUCT_PTR(FIELD_PTR_TO_STRUCT_PTR(p, struct clist_buf_hdr, list_node), struct clist_buf, hdr);
        ovm_clist_appendc(cl1, q->hdr->len + 1, q->data);
    }
    
}

void ovm_clist_fini(struct ovm_clist *cl)
{
    while (!ovm_dllist_empty(cl->buf_list)) {
        struct ovm_dllist *p = ovm_dllist_first(cl->buf_list);
        ovm_dllist_erase(p);
        ovm_mem_free(FIELD_PTR_TO_STRUCT_PTR(FIELD_PTR_TO_STRUCT_PTR(p, struct clist_buf_hdr, list_node), struct clist_buf, hdr), OVM_MEM_MIN_BUF_SIZE);
    }
    cl->len = 0;
}

/***************************************************************************/

/* Exceptions */

static ovm_obj_str_t str_newc(ovm_inst_t dst, unsigned size, const char *data);
static ovm_obj_str_t str_newch(ovm_inst_t dst, unsigned size, const char *data, unsigned hash);
static inline ovm_obj_str_t str_newc1(ovm_inst_t dst, const char *data)
{
    return (str_newc(dst, strlen(data) + 1, data));
}
static ovm_obj_set_t user_new_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl);
static ovm_obj_str_t class_write_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl);
static ovm_obj_str_t method_write(ovm_inst_t dst, ovm_inst_t src);
static void dict_ats_put(ovm_thread_t th, ovm_obj_set_t s, unsigned size, const char *data, ovm_intval_t hash, ovm_inst_t val);

static void backtrace(ovm_thread_t th)
{
    fputs("Backtrace:\n", stderr);
    
    ovm_inst_t work = ovm_stack_alloc(th, 1);

    struct ovm_frame_mc *p;
    unsigned lvl;
    for (lvl = 0, p = th->mcfp; p != 0; p = p->prev, ++lvl) {
        fprintf(stderr, "%3u: ", lvl);
        if (p->method != 0) {
            method_write(&work[-1], p->method);
            fputs(ovm_inst_strval(th, &work[-1])->data, stderr);
        } else {
            fputs("???", stderr);
        }
        fputs(".call(", stderr);
        unsigned n;
        ovm_inst_t q;
        const char *s = "";
        for (q = p->ap, n = p->argc; n > 0; --n, ++q, s = ", ") {
            ovm_inst_assign(th->sp, q);
            ovm_method_callsch(th, &work[-1], _OVM_STR_CONST_HASH("write"), 1);
            fprintf(stderr, "%s%s", s, ovm_inst_strval(th, &work[-1])->data);
        }
        fputs(")\n", stderr);
    }

    fflush(stderr);

    ovm_stack_unwind(th, work);
}

static ovm_obj_set_t except_newc(ovm_thread_t th, ovm_inst_t dst, unsigned type_size, const char *type)
{
    ovm_obj_set_t x = user_new_unsafe(th, dst, OVM_CL_EXCEPTION);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    str_newc(&work[-1], type_size, type);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(type), &work[-1]);

    ovm_stack_unwind(th, work);
    
    return (x);
}

static void except_raise1(ovm_thread_t th)
{
    if (++th->except_lvl > 1)  OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_DOUBLE_EXCEPT, 0);
}

__attribute__((noreturn))
static void except_uncaught(ovm_thread_t th, ovm_inst_t x)
{
    fflush(stdout);

    ovm_stack_push(th, x);
    ovm_method_callsch(th, th->sp, OVM_STR_CONST_HASH(write), 1);
    fprintf(stderr, "\nException: %s\n", ovm_inst_strval(th, th->sp)->data);

    ovm_stack_free(th, 1);
    
    OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_UNCAUGHT_EXCEPT, 0);
}

__attribute__((noreturn))
static void except_longjmp(ovm_thread_t th, struct ovm_frame_except *xfr, ovm_inst_t arg)
{
    ovm_inst_assign(xfr->arg, arg);
    xfr->arg_valid = true;
    frame_unwind(th, xfr->base);
    ovm_stack_unwind(th, xfr->sp);
    th->pc = xfr->pc;
    th->exceptf = true;
    longjmp(xfr->jb, 1);
}

__attribute__((noreturn))
static void except_raise2(ovm_thread_t th, ovm_inst_t x, ovm_inst_t m)
{
    dict_ats_put(th, ovm_inst_setval_nochk(x), OVM_STR_CONST_HASH(method), m);

    struct ovm_frame_except *xfr = th->xfp;
    if (xfr == 0)  except_uncaught(th, x);
    except_longjmp(th, xfr, x);
}

__attribute__((noreturn))
void ovm_except_raise(ovm_thread_t th, ovm_inst_t x)
{
    except_raise1(th);
    except_raise2(th, x, th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_reraise(ovm_thread_t th)
{
    struct ovm_frame_except *xfr = th->xfp;
    if (xfr == 0 || !xfr->arg_valid)  OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_NO_FRAME, 0);
    ovm_inst_t arg = xfr->arg;
    struct ovm_frame_except *prev = xfr->prev;
    if (prev == 0)  except_uncaught(th, arg);
    th->xfp = prev;
    except_longjmp(th, prev, arg);
}

__attribute__((noreturn))
void ovm_except_inv_value(ovm_thread_t th, ovm_inst_t inst)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.invalid-value"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(value), inst);

    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_no_methodc(ovm_thread_t th, ovm_inst_t recvr, unsigned sel_size, const char *sel)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.no-method"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(receiver), recvr);
    str_newc(&work[-2], sel_size, sel);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(selector), &work[-2]);

    ovm_stack_free(th, 1);
    
    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_no_var(ovm_thread_t th, ovm_inst_t var)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.no-variable"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(name), var);
    
    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_num_args(ovm_thread_t th, unsigned expected)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.number-of-arguments"));
    ovm_int_newc(&work[-2], expected);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(expected), &work[-2]);
    ovm_int_newc(&work[-2], thread_mcfp(th)->argc);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(got), &work[-2]);

    ovm_stack_free(th, 1);
    
    except_raise2(th, &work[-1], th->mcfp->method);
}

void ovm_method_argc_chk_exact(ovm_thread_t th, unsigned expected)
{
    if (thread_mcfp(th)->argc != expected)  ovm_except_num_args(th, expected);
}

__attribute__((noreturn))
void ovm_except_num_args_min(ovm_thread_t th, unsigned min)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.number-of-arguments"));
    ovm_int_newc(&work[-2], min);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(minimum), &work[-2]);
    ovm_int_newc(&work[-2], thread_mcfp(th)->argc);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(got), &work[-2]);

    ovm_stack_free(th, 1);

    except_raise2(th, &work[-1], th->mcfp->method);
}

void ovm_method_argc_chk_min(ovm_thread_t th, unsigned min)
{
    if (thread_mcfp(th)->argc < min)  ovm_except_num_args_min(th, min);
}

__attribute__((noreturn))
void ovm_except_num_args_range(ovm_thread_t th, unsigned min, unsigned max)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 2);    

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.number-of-arguments"));
    ovm_int_newc(&work[-2], min);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(minimum), &work[-2]);
    ovm_int_newc(&work[-2], max);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(maximum), &work[-2]);
    ovm_int_newc(&work[-2], thread_mcfp(th)->argc);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(got), &work[-2]);

    ovm_stack_free(th, 1);

    except_raise2(th, &work[-1], th->mcfp->method);
}

void ovm_method_argc_chk_range(ovm_thread_t th, unsigned min, unsigned max)
{
    unsigned argc = thread_mcfp(th)->argc;
    if (argc < min || argc > max)  ovm_except_num_args_range(th, min, max);
}

__attribute__((noreturn))
void ovm_except_no_attr(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t attr)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.no-attribute"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(instance), inst);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(attribute), attr);

    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_idx_range(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t idx)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.index-range"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(instance), inst);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(index), idx);
    
    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_idx_range2(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t idx, ovm_inst_t len)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.index-range"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(instance), inst);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(index), idx);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(length), len);

    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_key_not_found(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t key)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.key-not-found"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(instance), inst);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(key), key);

    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_modify_const(ovm_thread_t th, ovm_inst_t inst, ovm_inst_t key)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.modify-constant"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(instance), inst);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(key), key);

    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_file_open(ovm_thread_t th, ovm_inst_t filename, ovm_inst_t mode)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.file-open"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(filename), filename);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(mode), mode);
    ovm_int_newc(&work[-2], ovm_thread_errno(th));
    dict_ats_put(th, x, _OVM_STR_CONST_HASH("errno"), &work[-2]);
    char mesg[80];
    char *p = strerror_r(ovm_thread_errno(th), mesg, sizeof(mesg));
    str_newc1(&work[-2], p);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(message), &work[-2]);

    ovm_stack_free(th, 1);

    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_module_load(ovm_thread_t th, ovm_inst_t name, unsigned mesg_size, const char *mesg)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_obj_set_t x = except_newc(th, &work[-1], _OVM_STR_CONST("system.module-load"));
    dict_ats_put(th, x, OVM_STR_CONST_HASH(name), name);
    str_newc(&work[-2], mesg_size, mesg);
    dict_ats_put(th, x, OVM_STR_CONST_HASH(message), &work[-2]);

    ovm_stack_free(th, 1);

    except_raise2(th, &work[-1], th->mcfp->method);
}

__attribute__((noreturn))
void ovm_except_descent_loop(ovm_thread_t th)
{
    except_raise1(th);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    except_newc(th, &work[-1], _OVM_STR_CONST("system.descent-loop"));

    except_raise2(th, &work[-1], th->mcfp->method);
}

/***************************************************************************/

/* Constructors, internal class functions, desctructors */

/* Things marked as 'unsafe' write to the destination before they are finished
   with their arguments; therefore, the destination cannot overlap with the
   arguments.
   They are more optimal when the caller knows the destination and arguments
   don't overlap; the caller can use scratch space if it cannet be sure
   of overlap.
*/

static void method_findc_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_inst_t recvr, unsigned sel_size, const char *sel, unsigned sel_hash, ovm_inst_t found_cl);
void ovm_method_callsch(ovm_thread_t th, ovm_inst_t dst, unsigned sel_size, const char *sel, unsigned sel_hash, unsigned argc);

struct ovm_str_newv_item {
    unsigned   size;
    const char *data;
};

static void str_newv_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_str_t s = ovm_obj_str(obj);
    unsigned size = va_arg(ap, unsigned);
    unsigned n = va_arg(ap, unsigned);
    struct ovm_str_newv_item *a = va_arg(ap, struct ovm_str_newv_item *);
    s->size = size;
    char *p = s->data;
    for (; n > 0; --n, ++a) {
        unsigned k = a->size;
        if (k > 0) --k;
        memcpy(p, a->data, k);
        p += k;
    }
    *p = 0;
}

static inline ovm_obj_str_t str_newv_size(ovm_inst_t dst, unsigned size, unsigned n, struct ovm_str_newv_item *a) /* Safe */
{
    return (ovm_obj_str(ovm_obj_alloc(dst, sizeof(*ovm_obj_str(0)) + size * sizeof(ovm_obj_str(0)->data[0]), OVM_CL_STRING, OVM_MEM_ALLOC_NO_HINT, str_newv_obj_init, size, n, a)));
}

static void str_newc_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_str_t s = ovm_obj_str(obj);
    unsigned size = va_arg(ap, unsigned);
    char *data = va_arg(ap, char *);
    s->size = size;
    --size;
    memcpy(s->data, data, size);
    s->data[size] = 0;    
}

static ovm_obj_str_t str_newc(ovm_inst_t dst, unsigned size, const char *data) /* Safe */
{
    DEBUG_ASSERT(size > 0);

    return (ovm_obj_str(ovm_obj_alloc(dst, sizeof(*ovm_obj_str(0)) + size * sizeof(ovm_obj_str(0)->data[0]), OVM_CL_STRING, OVM_MEM_ALLOC_NO_HINT, str_newc_obj_init, size, data)));
}

static ovm_obj_str_t str_newch(ovm_inst_t dst, unsigned size, const char *data, unsigned hash) /* Safe */
{
    ovm_obj_str_t result = str_newc(dst, size, data);
    dst->hash = hash;
    dst->hash_valid = true;

    return (result);
}

static void str_newcl_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_str_t s = ovm_obj_str(obj);
    unsigned size = va_arg(ap, unsigned);
    s->size = size;
    struct ovm_clist *cl = va_arg(ap, struct ovm_clist *);
    --size;
    ovm_clist_to_barray(size, (unsigned char *) s->data, cl);
    s->data[size] = 0;
}

static ovm_obj_str_t str_new_clist(ovm_inst_t dst, struct ovm_clist *cl)
{
    unsigned data_size = cl->len + 1;
    return (ovm_obj_str(ovm_obj_alloc(dst, sizeof(*ovm_obj_str(0)) + data_size * sizeof(ovm_obj_str(0)->data[0]), OVM_CL_STRING, OVM_MEM_ALLOC_NO_HINT, str_newcl_obj_init, data_size, cl)));
}

void ovm_str_newc(ovm_inst_t dst, unsigned size, const char *data)
{
    str_newc(dst, size, data);
}

void ovm_str_newch(ovm_inst_t dst, unsigned size, const char *data, unsigned hash)
{
    str_newch(dst, size, data, hash);
}

void ovm_str_newc1(ovm_inst_t dst, const char *data)
{
    str_newc(dst, strlen(data) + 1, data);
}

void ovm_str_pushc(ovm_thread_t th, unsigned size, const char *data)
{
    ovm_stack_alloc(th, 1);
    str_newc(th->sp, size, data);
}

void ovm_str_pushch(ovm_thread_t th, unsigned size, const char *data, unsigned hash)
{
    ovm_stack_alloc(th, 1);
    str_newch(th->sp, size, data, hash);
}

void ovm_str_clist(ovm_inst_t dst, struct ovm_clist *cl)
{
    str_new_clist(dst, cl);
}

static inline ovm_obj_str_t str_slicec(ovm_inst_t dst, ovm_obj_str_t s, unsigned ofs, unsigned len)
{
    return (str_newc(dst, len + 1, &s->data[ofs]));
}

static void str_joinc_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_str_t s = ovm_obj_str(obj);
    unsigned size = va_arg(ap, unsigned);
    unsigned ldr_size = va_arg(ap, unsigned);
    char *ldr = va_arg(ap, char *);
    unsigned sep_size = va_arg(ap, unsigned);
    char *sep = va_arg(ap, char *);
    unsigned trlr_size = va_arg(ap, unsigned);
    char *trlr = va_arg(ap, char *);
    ovm_obj_list_t li = va_arg(ap, ovm_obj_list_t);

    s->size = size;
    char *p = (char *) s->data;
    if (ldr_size > 0) {
        memcpy(p, ldr, ldr_size);
        p += ldr_size;
    }
    bool f = false;
    for (; li != 0; li = ovm_list_next(li), f = true) {
        if (sep_size > 0 && f) {
            memcpy(p, sep, sep_size);
            p += sep_size;
        }
        ovm_obj_str_t ss = ovm_inst_strval_nochk(li->item);
        unsigned k = ss->size;
        if (k > 0)  --k;
        memcpy(p, ss->data, k);
        p += k;
    }
    if (trlr_size > 0) {
        memcpy(p, trlr, trlr_size);
        p += trlr_size;
    }
    *p = 0;
}

static ovm_obj_str_t str_joinc_size(ovm_inst_t dst, 
                                    unsigned size,
                                    unsigned ldr_size,  const char *ldr,
                                    unsigned sep_size,  const char *sep,
                                    unsigned trlr_size, const char *trlr,
                                    ovm_obj_list_t li
                                    )
{
    if (ldr_size > 0)   --ldr_size;
    if (sep_size > 0)   --sep_size;
    if (trlr_size > 0)  --trlr_size;
 
    return (ovm_obj_str(ovm_obj_alloc(dst, sizeof(*ovm_obj_str(0)) + size * sizeof(ovm_obj_str(0)->data[0]), OVM_CL_STRING, OVM_MEM_ALLOC_NO_HINT, str_joinc_obj_init, size, ldr_size, ldr, sep_size, sep, trlr_size, trlr, li)));
}

static ovm_obj_str_t str_joinc(ovm_thread_t th,
                               ovm_inst_t dst,
                               unsigned ldr_size,  const char *ldr,
                               unsigned sep_size,  const char *sep,
                               unsigned trlr_size, const char *trlr,
                               ovm_obj_list_t li
                               )
{
    unsigned data_size = 1;
    data_size += ldr_size;
    if (ldr_size > 0)   --data_size;
    unsigned ss = sep_size;
    if (ss > 0)  --ss;
    bool f = false;
    ovm_obj_list_t p;
    for (p = li; p != 0; p = ovm_list_next(p), f = true) {
        if (f) data_size += ss;
        data_size += ovm_inst_strval(th, p->item)->size - 1;
    }
    data_size += trlr_size;
    if (trlr_size > 0)   --data_size;

    return (str_joinc_size(dst, data_size, ldr_size, ldr, sep_size, sep, trlr_size, trlr, li));
}

static void pair_walk(ovm_obj_t obj, void (*func)(ovm_inst_t))
{
    ovm_obj_pair_t pr = ovm_obj_pair(obj);
    (*func)(pr->first);
    (*func)(pr->second);
}

static void pair_mark(ovm_obj_t obj)
{
    pair_walk(obj, ovm_inst_mark);
}

static void pair_free(ovm_obj_t obj)
{
    pair_walk(obj, ovm_inst_release);
}

static void pair_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_pair_t pr = ovm_obj_pair(obj);
    _ovm_inst_assign_nolock_norelease(pr->first, va_arg(ap, ovm_inst_t));
    _ovm_inst_assign_nolock_norelease(pr->second, va_arg(ap, ovm_inst_t));
}

static ovm_obj_pair_t pair_new(ovm_inst_t dst, ovm_inst_t first, ovm_inst_t second)
{
    return (ovm_obj_pair(ovm_obj_alloc(dst, sizeof(*ovm_obj_pair(0)), OVM_CL_PAIR, 2, pair_obj_init, first, second)));
}

static ovm_obj_pair_t pair_copydeep(ovm_thread_t th, ovm_inst_t dst, ovm_obj_pair_t pr)
{
    obj_lock_loop_chk(th, pr->base);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_inst_assign(th->sp, pr->first);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(copydeep), 1);
    ovm_inst_assign(th->sp, pr->second);
    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(copydeep), 1);
    
    pr = pair_new(dst, &work[-1], &work[-2]);

    ovm_stack_unwind(th, work);

    obj_unlock(pr->base);

    return (pr);
}

static void list_mark(ovm_obj_t obj)
{
    ovm_obj_list_t li = ovm_obj_list(obj);
    ovm_inst_mark(li->item);
    for (obj = li->next; obj != 0; obj = li->next) {
        li = ovm_obj_list(obj);
        if (++obj->ref_cnt > 1)  return;
        ovm_dllist_erase(obj->list_node);
        ovm_dllist_insert(obj->list_node, ovm_dllist_end(obj_list_white));
        ovm_obj_mark(ovm_obj_inst_of_raw(obj)->base);
        ovm_inst_mark(li->item);
    }
}

static void list_free(ovm_obj_t obj)
{
    ovm_obj_list_t li = ovm_obj_list(obj);
    ovm_obj_t next;
    ovm_inst_release(li->item);
    for (obj = li->next; obj != 0; obj = next) {
        li = ovm_obj_list(obj);
        next = li->next;
        if (--obj->ref_cnt > 0)  break;
        ovm_dllist_erase(obj->list_node);
        ovm_obj_release(ovm_obj_inst_of_raw(obj)->base);
        ovm_inst_release(li->item);
        ovm_mem_free(obj, obj->size);
    }
}

static void list_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_list_t li = ovm_obj_list(obj);
    _ovm_inst_assign_nolock_norelease(li->item, va_arg(ap, ovm_inst_t)); 
    ovm_obj_list_t next = va_arg(ap, ovm_obj_list_t);
    _ovm_obj_assign_nolock_norelease(&li->next, next->base);
}

static ovm_obj_list_t list_new(ovm_inst_t dst, ovm_inst_t item, ovm_obj_list_t next)
{
    return (ovm_obj_list(ovm_obj_alloc(dst, sizeof(*ovm_obj_list(0)), OVM_CL_LIST, 2, list_obj_init, item, next)));
}

struct list_newlc_ctxt {
    ovm_obj_t *p;
};

static void list_newlc_init(struct list_newlc_ctxt *ctxt, ovm_inst_t inst)
{
    ovm_inst_assign_obj(inst, 0);
    ctxt->p = &inst->objval;
}

static void list_newlc_concat(struct list_newlc_ctxt *ctxt, ovm_obj_list_t li)
{
    _ovm_objs_lock();
    
    _ovm_obj_assign_nolock_norelease(ctxt->p, li->base);

    _ovm_objs_unlock();

    ovm_obj_t next;
    for (;;) {
        next = li->next;
        if (next == 0)  break;
        li = ovm_obj_list(next);
    }
    ctxt->p = &li->next;
}

static unsigned list_size(ovm_obj_list_t li)
{
    unsigned result = 0;
    for (; li != 0; li = ovm_list_next(li), ++result);
    return (result);
}

static ovm_obj_list_t list_reverse_unsafe(ovm_inst_t dst, ovm_obj_list_t li)
{
    ovm_inst_assign_obj(dst, 0);
    ovm_obj_list_t result = 0;
    for ( ; li != 0; li = ovm_list_next(li)) {
        result = list_new(dst, li->item, ovm_inst_listval_nochk(dst));
    }
    
    return (result);
}

static ovm_obj_list_t list_slicec_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_list_t li, ovm_intval_t idx, ovm_intval_t len)
{
    for (; idx > 0; --idx, li = ovm_list_next(li));

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_inst_assign_obj(dst, 0);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    for (; len > 0; --len, li = ovm_list_next(li)) {
        list_newlc_concat(lc, list_new(&work[-1], li->item, 0));
    }
    
    ovm_stack_unwind(th, work);

    return (ovm_inst_listval_nochk(dst));
}

static inline ovm_obj_list_t list_copy_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_list_t li) /* Shallow copy */
{
    return (list_slicec_unsafe(th, dst, li, 0, list_size(li)));
}

static ovm_obj_list_t list_copydeep_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_list_t li)
{
    obj_lock_loop_chk(th, li->base);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign_obj(dst, 0);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    for (; li != 0; li = ovm_list_next(li)) {
        ovm_inst_assign(th->sp, li->item);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(copydeep), 1);
        list_newlc_concat(lc, list_new(&work[-1], &work[-1], 0));
    }

    ovm_stack_unwind(th, work);

    obj_unlock(li->base);
    
    return (ovm_inst_listval_nochk(dst));;
}

static ovm_obj_list_t list_concat_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_list_t li1, ovm_obj_list_t li2)
{
    if (li2 == 0) {
        ovm_inst_assign_obj(dst, li1->base);
    } else {
        ovm_inst_t work = ovm_stack_alloc(th, 1);

        ovm_inst_assign_obj(dst, 0);
        struct list_newlc_ctxt lc[1];
        list_newlc_init(lc, dst);
        for (; li1 != 0; li1 = ovm_list_next(li1)) {
            list_newlc_concat(lc, list_new(&work[-1], li1->item, 0));
        }
        list_newlc_concat(lc, li2);

        ovm_stack_unwind(th, work);
    }

    return (ovm_inst_listval_nochk(dst));
}

static void array_walk(ovm_obj_t obj, void (*func)(ovm_inst_t))
{
    ovm_obj_array_t a = ovm_obj_array(obj);
    unsigned n;
    ovm_inst_t inst;
    for (inst = a->data, n = a->size; n > 0; --n, ++inst)  (*func)(inst);
}

static void array_mark(ovm_obj_t obj)
{
    array_walk(obj, ovm_inst_mark);
}

static void array_free(ovm_obj_t obj)
{
    array_walk(obj, ovm_inst_release);
}

static void array_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_array_t a = ovm_obj_array(obj);
    a->size = va_arg(ap, unsigned);
    memset(a->data, 0, a->size * sizeof(a->data[0]));
}

static inline ovm_obj_array_t array_newc(ovm_inst_t dst, ovm_obj_class_t cl, unsigned data_size, ovm_inst_t data)
{
    ovm_obj_array_t a = ovm_obj_array(ovm_obj_alloc(dst, sizeof(*ovm_obj_array(0)) + data_size * sizeof(ovm_obj_array(0)->data[0]), cl, OVM_MEM_ALLOC_NO_HINT, array_obj_init, data_size));
    if (data != 0) {
        ovm_inst_t p;
        for (p = a->data; data_size > 0; --data_size, ++p, ++data)  ovm_inst_assign(p, data);
    }
    return (a);
}

static inline ovm_obj_array_t array_slicec(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_array_t a, ovm_intval_t idx, ovm_intval_t len)
{
    return (array_newc(dst, cl, len, &a->data[idx]));
}

static inline ovm_obj_array_t array_copy(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_array_t a)
{
    return (array_slicec(dst, cl, a, 0, a->size));
}

static ovm_obj_array_t array_copydeep_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_array_t a)
{
    obj_lock_loop_chk(th, a->base);
    
    ovm_obj_array_t aa = array_newc(dst, cl, a->size, 0);
    ovm_inst_t p, q;
    unsigned n;
    
    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    for (p = a->data, q = aa->data, n = a->size; n > 0; --n, ++p, ++q) {
        ovm_inst_assign(&work[-1], p);
        ovm_method_callsch(th, q, OVM_STR_CONST_HASH(copydeep), 1);
    }
    
    ovm_stack_unwind(th, work);

    obj_unlock(a->base);
    
    return (aa);
}

static void barray_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_barray_t b = ovm_obj_barray(obj);
    unsigned size = va_arg(ap, unsigned);
    b->size = size;
    unsigned char *data = va_arg(ap, unsigned char *);
    if (data != 0) {
        memcpy(b->data, data, size);
    } else {
        memset(b->data, 0, size);
    }
}

static inline ovm_obj_barray_t barray_newc(ovm_inst_t dst, ovm_obj_class_t cl, unsigned data_size, unsigned char *data)
{
    return (ovm_obj_barray(ovm_obj_alloc(dst, sizeof(*ovm_obj_barray(0)) + data_size * sizeof(ovm_obj_barray(0)->data[0]), cl, OVM_MEM_ALLOC_NO_HINT, barray_obj_init, data_size, data)));
}

static void bytearr_cl_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_barray_t b = ovm_obj_barray(obj);
    b->size = va_arg(ap, unsigned);
    ovm_clist_to_barray(b->size, b->data, va_arg(ap, struct ovm_clist *));
}

static inline ovm_obj_barray_t barray_new_clist(ovm_inst_t dst, ovm_obj_class_t cl, struct ovm_clist *clist)
{
    unsigned data_size = clist->len;
    return (ovm_obj_barray(ovm_obj_alloc(dst, sizeof(*ovm_obj_barray(0)) + data_size * sizeof(ovm_obj_barray(0)->data[0]), cl, OVM_MEM_ALLOC_NO_HINT, bytearr_cl_obj_init, data_size, clist)));
}

static inline ovm_obj_barray_t barray_slicec(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_barray_t b, ovm_intval_t idx, ovm_intval_t len)
{
    return (barray_newc(dst, cl, len, &b->data[idx]));
}

static inline ovm_obj_barray_t barray_copy(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_barray_t b)
{
    return (barray_slicec(dst, cl, b, 0, b->size));
}

static void slice_mark(ovm_obj_t obj)
{
    ovm_obj_mark(ovm_obj_slice(obj)->underlying);
}

static void slice_free(ovm_obj_t obj)
{
    ovm_obj_release(ovm_obj_slice(obj)->underlying);
}

static void slice_obj_init(ovm_obj_t obj, va_list ap)
{
    _ovm_obj_assign_nolock_norelease(&ovm_obj_slice(obj)->underlying, va_arg(ap, ovm_obj_t));
}

static ovm_obj_slice_t slice_new(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_t underlying, unsigned ofs, unsigned size)
{
    ovm_obj_slice_t result = ovm_obj_slice(ovm_obj_alloc(dst, sizeof(*ovm_obj_slice(0)), cl, 1, slice_obj_init, underlying));
    result->ofs  = ofs;
    result->size = size;

    return (result);
}

static void set_walk(ovm_obj_set_t s, void (*func)(ovm_obj_t))
{
    ovm_obj_t *p;
    unsigned  n;
    for (p = s->data, n = s->size; n > 0; --n, ++p)  (*func)(*p);
}

static void set_mark(ovm_obj_t s)
{
    set_walk(ovm_obj_set(s), ovm_obj_mark);
}

static void set_free(ovm_obj_t s)
{
    set_walk(ovm_obj_set(s), ovm_obj_release);
}

static void set_obj_init(ovm_obj_t obj, va_list ap)
{
    static pthread_mutexattr_t mutex_attr[1];
    static bool initf = false;
    if (!initf) {
        pthread_mutexattr_init(mutex_attr);
        pthread_mutexattr_settype(mutex_attr, PTHREAD_MUTEX_ERRORCHECK);
        initf = true;
    }

    ovm_obj_set_t s = ovm_obj_set(obj);
    unsigned size = va_arg(ap, unsigned);
    s->size = size;
    memset(s->data, 0, size * sizeof(s->data[0]));
}

static ovm_obj_set_t set_newc(ovm_inst_t dst, ovm_obj_class_t cl, unsigned size)
{
    size = round_up_to_power_of_2(size);
    return (ovm_obj_set(ovm_obj_alloc(dst, sizeof(*ovm_obj_set(0)) + size * sizeof(ovm_obj_set(0)->data[0]), cl, OVM_MEM_ALLOC_NO_HINT, set_obj_init, size)));
}

static bool class_ats(ovm_inst_t dst, ovm_obj_class_t cl, unsigned size, const char *data, unsigned hash);

static unsigned class_default_size(ovm_thread_t th, ovm_obj_class_t cl, unsigned default_size)
{
    unsigned result = default_size;

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    if (class_ats(&work[-1], cl, _OVM_STR_CONST_HASH("default-size"))) {
        if (work[-1].type == OVM_INST_TYPE_INT) {
            unsigned n = work[-1].intval;
            if (n > 0)  result = n;
        }
    }

    ovm_stack_unwind(th, work);

    return (result);
}

static ovm_obj_set_t set_copy_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_set_t s) /* Shallow copy */
{
    obj_lock_loop_chk(th, s->base);

    ovm_obj_set_t ss = set_newc(dst, cl, s->size);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_obj_t *p, *q;
    unsigned n;
    for (q = ss->data, p = s->data, n = s->size; n > 0; --n, ++p, ++q) {
        list_copy_unsafe(th, &work[-1], ovm_obj_list(*p));
        ovm_obj_assign(q, work[-1].objval);
    }
    ss->cnt = s->cnt;

    ovm_stack_unwind(th, work);

    obj_unlock(s->base);
    
    return (ss);
}

static ovm_obj_set_t set_copydeep_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_set_t s)
{
    obj_lock_loop_chk(th, s->base);

    ovm_obj_set_t ss = set_newc(dst, cl, s->size);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_t *p, *q;
    unsigned n;
    for (p = s->data, q = ss->data, n = s->size; n > 0; --n, ++p, ++q) {
        list_copydeep_unsafe(th, &work[-1], ovm_obj_list(*p));
        ovm_obj_assign(q, work[-1].objval);
    }
    ss->cnt = s->cnt;

    ovm_stack_unwind(th, work);
    
    obj_unlock(s->base);

    return (ss);
}

static void file_walk(ovm_obj_t obj, void (*func)(ovm_obj_t))
{
    ovm_obj_file_t f = ovm_obj_file(obj);
    (*func)(f->filename);
    (*func)(f->mode);
}

static void file_mark(ovm_obj_t obj)
{
    file_walk(obj, ovm_obj_mark);
}

static void file_cleanup(ovm_obj_t obj)
{
    FILE *fp = ovm_obj_file(obj)->fp;
    if (fp != 0)  fclose(fp);
}

static void file_free(ovm_obj_t obj)
{
    file_walk(obj, ovm_obj_release);
    file_cleanup(obj);
}

static void file_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_file_t f = ovm_obj_file(obj);
    _ovm_obj_assign_nolock_norelease(&f->filename, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&f->mode, va_arg(ap, ovm_obj_t));
    f->fp = va_arg(ap, FILE *);
}

static ovm_obj_file_t file_new(ovm_inst_t dst, ovm_obj_str_t path, ovm_obj_str_t mode, FILE *fp)
{
    return (ovm_obj_file(ovm_obj_alloc(dst, sizeof(*ovm_obj_file(0)), OVM_CL_FILE, 1, file_obj_init, path, mode, fp)));
}

void ovm_file_newc(ovm_thread_t th, ovm_inst_t dst, unsigned name_size, const char *name, unsigned mode_size, const char *mode, FILE *fp)
{
    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_obj_str_t s1 = str_newc(&work[-1], name_size, name);
    ovm_obj_str_t s2 = str_newc(&work[-2], mode_size, mode);
    file_new(dst, s1, s2, fp);

    ovm_stack_unwind(th, work);
}

static ovm_obj_file_t file_copy(ovm_inst_t dst, ovm_obj_file_t obj)
{
    int fd = dup(fileno(obj->fp));
    if (fd < 0)  return (0);
    FILE *fp = fdopen(fd, ovm_obj_str(obj->mode)->data);
    if (fp == 0)  return (0);

    return (file_new(dst, ovm_obj_str(obj->filename), ovm_obj_str(obj->mode), fp));
}

static void ns_walk(ovm_obj_ns_t ns, void (*func)(ovm_obj_t))
{
    (*func)(ns->name);
    (*func)(ns->parent);
    (*func)(ns->dict);
}

static void ns_mark(ovm_obj_t obj)
{
    ns_walk(ovm_obj_ns(obj), ovm_obj_mark);
}

static void ns_free(ovm_obj_t obj)
{
    ns_walk(ovm_obj_ns(obj), ovm_obj_release);
}

static void ns_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_ns_t ns = ovm_obj_ns(obj);
    _ovm_obj_assign_nolock_norelease(&ns->name, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&ns->parent, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&ns->dict, va_arg(ap, ovm_obj_t));
}

static ovm_intval_t str_inst_hash(ovm_inst_t inst);
static inline void ns_ats_put(ovm_thread_t th, ovm_obj_ns_t ns, unsigned name_size, const char *name, unsigned name_hash, ovm_inst_t val);

static inline ovm_obj_ns_t ns_new(ovm_thread_t th, ovm_inst_t dst, ovm_obj_str_t name, unsigned name_hash, ovm_obj_set_t dict, ovm_obj_ns_t parent)
{
    ovm_obj_ns_t result = ovm_obj_ns(ovm_obj_alloc(dst, sizeof(*result), OVM_CL_NAMESPACE, 1, ns_obj_init, name, parent, dict));

    if (parent != 0)  ns_ats_put(th, parent, name->size, name->data, name_hash, dst);
    
    return (result);
}

static void module_walk(ovm_obj_module_t m, void (*func)(ovm_obj_t))
{
    ns_walk(m->base, func);
    (*func)(m->filename);
    (*func)(m->sha1);
}

static void module_mark(ovm_obj_t obj)
{
    module_walk(ovm_obj_module(obj), ovm_obj_mark);
}

static void *module_func(void *dlhdl, ovm_obj_str_t modname, char *sym, unsigned mesg_bufsize, char *mesg);

static void module_cleanup(ovm_obj_t obj)
{
    ovm_obj_module_t m = ovm_obj_module(obj);
    void *dlhdl = m->dlhdl;
    if (dlhdl == 0)  return;

    void (*fini_func)(void) = (void (*)(void)) module_func(dlhdl, ovm_obj_str(m->base->name), "fini", 0, 0);
    if (fini_func != 0)  (*fini_func)();
    dlclose(dlhdl);
}

static void module_free(ovm_obj_t obj)
{
    module_walk(ovm_obj_module(obj), ovm_obj_release);
    module_cleanup(obj);
}

static void module_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_module_t m = ovm_obj_module(obj);
    ovm_obj_ns_t ns = m->base;

    _ovm_obj_assign_nolock_norelease(&ns->name, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&ns->parent, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&ns->dict, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&m->filename, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&m->sha1, va_arg(ap, ovm_obj_t));
    m->dlhdl = va_arg(ap, void *);
}

static ovm_obj_module_t _module_new(ovm_thread_t th, ovm_inst_t dst, ovm_obj_str_t name, unsigned name_hash, ovm_obj_set_t dict, ovm_obj_str_t filename, ovm_obj_str_t sha1, void *dlhdl, ovm_obj_ns_t parent)
{
    ovm_obj_module_t result = ovm_obj_module(ovm_obj_alloc(dst, sizeof(*result), OVM_CL_MODULE, 1, module_obj_init, name, parent, dict, filename, sha1, dlhdl));

    if (parent != 0)  ns_ats_put(th, parent, name->size, name->data, name_hash, dst);
    
    return (result);
}

static ovm_obj_module_t module_new(ovm_thread_t th, ovm_inst_t dst, ovm_obj_str_t name, unsigned name_hash, ovm_obj_str_t filename, ovm_obj_str_t sha1, void *dlhdl, ovm_obj_ns_t parent)
{
    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_module_t result = _module_new(th, dst, name, name_hash, set_newc(&work[-1], OVM_CL_DICTIONARY, 32), filename, sha1, dlhdl, parent);

    ovm_stack_unwind(th, work);

    return (result);
}

static inline ovm_intval_t str_hash(ovm_obj_str_t s);

static ovm_obj_module_t module_clone(ovm_thread_t th, ovm_inst_t dst, ovm_obj_module_t m, ovm_obj_ns_t parent)
{
    ovm_obj_str_t nm = ovm_obj_str(m->base->name), filenm = ovm_obj_str(m->filename);
    unsigned nm_hash = str_hash(nm);
    void *dlhdl = m->dlhdl;

    assert(dlopen(filenm->data, RTLD_NOW) == dlhdl); /* Bump reference count */
    return (_module_new(th, dst, nm, nm_hash, ovm_obj_set(m->base->dict), filenm, ovm_obj_str(m->sha1), dlhdl, parent));
}

static void class_walk(ovm_obj_t obj, void (*func)(ovm_obj_t))
{
    ovm_obj_class_t cl = ovm_obj_class(obj);
    (*func)(cl->name);
    (*func)(cl->parent);
    (*func)(cl->ns);
    (*func)(cl->cl_vars);
    (*func)(cl->cl_methods);
    (*func)(cl->inst_methods);
}

static void class_mark(ovm_obj_t obj)
{
    class_walk(obj, ovm_obj_mark);
}

static void class_free(ovm_obj_t obj)
{
    class_walk(obj, ovm_obj_release);
}

static void class_obj_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_class_t cl = ovm_obj_class(obj);
    _ovm_obj_assign_nolock_norelease(&cl->name, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&cl->parent, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&cl->ns, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&cl->cl_vars, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&cl->cl_methods, va_arg(ap, ovm_obj_t));
    _ovm_obj_assign_nolock_norelease(&cl->inst_methods, va_arg(ap, ovm_obj_t));
}

static inline ovm_obj_class_t class_alloc(ovm_inst_t dst)
{
    return (ovm_obj_class(ovm_obj_alloc(dst, sizeof(*ovm_obj_class(0)), OVM_METACLASS, 2, 0)));
}

#define CL_VARS_DICT_SIZE    16
#define CL_METHOD_DICT_SIZE  128

static ovm_obj_class_t class_new(ovm_thread_t th, ovm_inst_t dst, ovm_obj_ns_t ns, unsigned name_size, const char *name, unsigned name_hash, ovm_obj_class_t parent, void (*mark)(ovm_obj_t obj), void (*free)(ovm_obj_t obj), void (*cleanup)(ovm_obj_t obj))
{
    ovm_inst_t work = ovm_stack_alloc(th, 4);

    ovm_obj_class_t cl = ovm_obj_class(ovm_obj_alloc(dst, sizeof(*ovm_obj_class(0)), OVM_METACLASS, 2, class_obj_init,
                                                     str_newc(&work[-1], name_size, name), parent, ns,
                                                     set_newc(&work[-2], OVM_CL_DICTIONARY, CL_VARS_DICT_SIZE),
                                                     set_newc(&work[-3], OVM_CL_DICTIONARY, CL_METHOD_DICT_SIZE),
                                                     set_newc(&work[-4], OVM_CL_DICTIONARY, CL_METHOD_DICT_SIZE)
                                                     )
                                       );
    cl->mark = mark;
    cl->free = free;
    cl->cleanup = cleanup;
    
    ovm_stack_unwind(th, work);
    
    ns_ats_put(th, ns, name_size, name, name_hash, dst);

    return (cl);
}

/* ns parent -- ns cl */

void ovm_class_new(ovm_thread_t th, unsigned name_size, const char *name, unsigned name_hash, void (*mark)(ovm_obj_t obj), void (*free)(ovm_obj_t obj), void (*cleanup)(ovm_obj_t obj))
{
    class_new(th, th->sp, ovm_inst_nsval(th, &th->sp[1]), name_size, name, name_hash, ovm_inst_classval(th, th->sp), mark, free, cleanup);
}

static void user_cl_alloc(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    user_new_unsafe(th, &work[-1], ovm_inst_classval_nochk(&argv[0]));
    ovm_inst_assign(dst, &work[-1]);

    ovm_stack_unwind(th, work);
}

static ovm_obj_set_t user_new_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl)
{
    ovm_obj_set_t result = set_newc(dst, OVM_CL_USER, 16);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign_obj(&work[-1], cl->base);
    dict_ats_put(th, result, _OVM_STR_CONST_HASH("__instanceof__"), &work[-1]);
    
    ovm_stack_unwind(th, work);
    
    return (result);
}

/***************************************************************************/

/* Assorted helper fucntions, for implementing methods */

static ovm_intval_t str_hashc(unsigned size, const char *data)
{
    return (mem_hash(size - 1, data));
}

static inline ovm_intval_t str_hash(ovm_obj_str_t s)
{
    return (str_hashc(s->size, s->data));
}

static ovm_intval_t str_inst_hash(ovm_inst_t inst)
{
    if (!inst->hash_valid) {
        inst->hash = str_hash(ovm_inst_strval_nochk(inst));
        inst->hash_valid = true;
    }
    
    return (inst->hash);
}
 
static inline bool str_equalc(ovm_obj_str_t s, unsigned size, const char *data)
{
    return (s->size == size && strcmp(s->data, data) == 0);
}

static inline bool str_equal(ovm_obj_str_t s1, ovm_obj_str_t s2)
{
    return (str_equalc(s1, s2->size, s2->data));
}

static inline bool str_equal_inst(ovm_obj_str_t s, ovm_inst_t inst)
{
    return (ovm_inst_of_raw(inst) == OVM_CL_STRING && str_equal(s, ovm_inst_strval_nochk(inst)));
}

static inline int str_indexc(ovm_obj_str_t s1, const char *s2, unsigned ofs)
{
    char *p = strstr(s1->data + ofs, s2);
    return (p == 0 ? -1 : p - s1->data);
}

static inline int str_index(ovm_obj_str_t s1, ovm_obj_str_t s2, unsigned ofs)
{
    return (str_indexc(s1, s2->data, ofs));
}

static unsigned list_hash(ovm_thread_t th, ovm_obj_list_t li)
{
    ovm_inst_t work = ovm_stack_alloc(th, 1);

    unsigned result;
    for (result = 0; li != 0; li = ovm_list_next(li)) {
        ovm_inst_assign(&work[-1], li->item);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(hash), 1);
        result += ovm_inst_intval(th, &work[-1]);
    }

    ovm_stack_unwind(th, work);
    
    return (result);
}

static inline bool array_at(ovm_inst_t dst, ovm_obj_array_t a, ovm_intval_t idx)
{
    if (!slice1(&idx, a->size))  return (false);
    ovm_inst_assign(dst, &a->data[idx]);
    return (true);
}

static inline bool array_at_put(ovm_obj_array_t a, ovm_intval_t idx, ovm_inst_t val)
{
    if (!slice1(&idx, a->size))  return (false);
    ovm_inst_assign(&a->data[idx], val);
    return (true);
}

static unsigned array_hash(ovm_thread_t th, ovm_obj_t obj, unsigned size, ovm_inst_t data)
{
    obj_lock_loop_chk(th, obj);
        
    ovm_inst_t work = ovm_stack_alloc(th, 1);

    unsigned result = 0;
    for (; size > 0; --size, ++data) {
        ovm_inst_assign(&work[-1], data);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(hash), 1);
        result += ovm_inst_intval(th, &work[-1]);
    }
    
    obj_unlock(obj);

    return (result);
}

static ovm_obj_t *set_find(ovm_thread_t th, ovm_obj_set_t s, ovm_inst_t key, ovm_obj_t **bucket)
{
    ovm_obj_t *result = 0;

    if (!key->hash_valid) {
        ovm_stack_push(th, key);
        ovm_method_callsch(th, th->sp, OVM_STR_CONST_HASH(hash), 1);
        key->hash = ovm_inst_intval(th, th->sp);
        key->hash_valid = true;
        ovm_stack_free(th, 1);
    }
    ovm_obj_t *p = &s->data[key->hash & (s->size - 1)];
    if (bucket) *bucket = p;
    ovm_obj_list_t li;

    ovm_inst_t work = ovm_stack_alloc(th, 4);

    method_findc_unsafe(th, &work[-1], key, OVM_STR_CONST_HASH(equal), &work[-2]);
    ovm_inst_t m = &work[-1];
    ovm_obj_class_t cl = ovm_inst_classval_nochk(&work[-2]);
    ovm_inst_assign(th->sp, key);
    ovm_inst_t arg = &th->sp[1];
    for (; (li = ovm_obj_list(*p)) != 0; p = &li->next) {
        ovm_inst_assign(arg, li->item);
        method_run(th, arg, 0, cl, m, 2, th->sp);
        if (ovm_inst_boolval(th, arg)) {
            result = p;
            break;
        }
    }

    ovm_stack_unwind(th, work);
    
    return (result);
}

static bool set_at(ovm_thread_t th, ovm_obj_set_t s, ovm_inst_t key)
{
    obj_lock(s->base);

    bool result = (set_find(th, s, key, 0) != 0);
    
    obj_unlock(s->base);

    return (result);
}

static void set_put(ovm_thread_t th, ovm_obj_set_t s, ovm_inst_t key)
{
    obj_lock(s->base);

    ovm_obj_t *bucket, *p = set_find(th, s, key, &bucket);
    if (p == 0) {
        ovm_inst_t work = ovm_stack_alloc(th, 1);
        
        ovm_obj_assign(bucket, list_new(&work[-1], key, ovm_obj_list(*bucket))->base);

        ovm_stack_unwind(th, work);
        
        ++s->cnt;
    }
    
    obj_unlock(s->base);
}

static void set_del(ovm_thread_t th, ovm_obj_set_t s, ovm_inst_t key)
{
    obj_lock(s->base);

    ovm_obj_t *p = set_find(th, s, key, 0);
    if (p != 0) {
        ovm_obj_assign(p, ovm_obj_list(*p)->next);
        DEBUG_ASSERT(s->cnt > 0);
        --s->cnt;
    }
    
    obj_unlock(s->base);
}

static void set_clear(ovm_obj_set_t s)
{
    obj_lock(s->base);

    ovm_obj_t *p;
    unsigned n;
    for (p = s->data, n = s->size; n > 0; --n, ++p)  ovm_obj_assign(p, 0);

    obj_unlock(s->base);
}

static inline ovm_obj_t *dict_finds(ovm_obj_set_t s, unsigned size, const char *data, unsigned hash, ovm_obj_t **bucket)
{
    ovm_obj_t *result = 0;
    ovm_obj_t *p = &s->data[hash & (s->size - 1)];
    if (bucket) *bucket = p;
    ovm_obj_list_t li;
    for (; (li = ovm_obj_list(*p)) != 0; p = &li->next) {
        ovm_inst_t k = ovm_inst_pairval_nochk(li->item)->first;
        if (ovm_inst_of_raw(k) != OVM_CL_STRING)  continue;
        if (str_equalc(ovm_inst_strval_nochk(k), size, data)) {
            result = p;
            break;
        }
    }

    return (result);
}

static inline bool dict_ats(ovm_inst_t dst, ovm_obj_set_t s, unsigned size, const char *data, ovm_intval_t hash)
{
    obj_lock(s->base);

    ovm_obj_t *p = dict_finds(s, size, data, hash, 0);
    bool result = (p != 0);
    if (result)  ovm_inst_assign(dst, ovm_obj_list(*p)->item);
    
    obj_unlock(s->base);

    return (result);
}

static void dict_ats_put(ovm_thread_t th, ovm_obj_set_t s, unsigned size, const char *data, ovm_intval_t hash, ovm_inst_t val)
{
    obj_lock(s->base);

    ovm_obj_t *bucket;
    ovm_obj_t *p = dict_finds(s, size, data, hash, &bucket);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    if (p == 0) {
        ++s->cnt;
        DEBUG_ASSERT(s->cnt > 0);
        str_newc(&work[-1], size, data);
    } else {
        if (size > 2 && data[0] == '#') {
            obj_unlock(s->base);

            ovm_stack_alloc(th, 1);

            ovm_inst_assign_obj(&work[-1], s->base);
            str_newc(&work[-2], size, data);
            ovm_except_modify_const(th, &work[-1], &work[-2]);
        }
        ovm_obj_list_t li = ovm_obj_list(*p);
        ovm_inst_assign(&work[-1], ovm_inst_pairval_nochk(li->item)->first);
        ovm_obj_assign(p, li->next);
    }

    pair_new(&work[-1], &work[-1], val);
    ovm_obj_assign(bucket, list_new(&work[-1], &work[-1], ovm_obj_list(*bucket))->base);

    ovm_stack_unwind(th, work);
    
    obj_unlock(s->base);
}

static ovm_obj_t *dict_find(ovm_thread_t th, ovm_obj_set_t s, ovm_inst_t key, ovm_obj_t **bucket)
{
    ovm_obj_t *result = 0;

    if (!key->hash_valid) {
        ovm_stack_push(th, key);
        ovm_method_callsch(th, th->sp, OVM_STR_CONST_HASH(hash), 1);
        key->hash = ovm_inst_intval(th, th->sp);
        key->hash_valid = true;
        ovm_stack_free(th, 1);
    }
    ovm_obj_t *p = &s->data[key->hash & (s->size - 1)];
    if (bucket) *bucket = p;
    ovm_obj_list_t li;

    ovm_inst_t work = ovm_stack_alloc(th, 4);

    method_findc_unsafe(th, &work[-1], key, OVM_STR_CONST_HASH(equal), &work[-2]);
    ovm_inst_t m = &work[-1];
    ovm_obj_class_t cl = ovm_inst_classval_nochk(&work[-2]);
    ovm_inst_assign(th->sp, key);
    ovm_inst_t arg = &th->sp[1];
    for (; (li = ovm_obj_list(*p)) != 0; p = &li->next) {
        ovm_inst_assign(arg, ovm_inst_pairval_nochk(li->item)->first);
        method_run(th, arg, 0, cl, m, 2, th->sp);
        if (ovm_inst_boolval(th, arg)) {
            result = p;
            break;
        }
    }

    ovm_stack_unwind(th, work);
    
    return (result);
}

static bool dict_at(ovm_thread_t th, ovm_inst_t dst, ovm_obj_set_t s, ovm_inst_t key)
{
    obj_lock(s->base);

    ovm_obj_t *p = dict_find(th, s, key, 0);
    bool result = (p != 0);
    if (result)  ovm_inst_assign(dst, ovm_obj_list(*p)->item);

    obj_unlock(s->base);

    return (result);    
}

static void dict_at_put(ovm_thread_t th, ovm_obj_set_t s, ovm_inst_t key, ovm_inst_t val)
{
    obj_lock(s->base);

    ovm_obj_t *bucket, *p = dict_find(th, s, key, &bucket);
    if (p == 0) {
        ++s->cnt;
        DEBUG_ASSERT(s->cnt > 0);
    } else {
        if (ovm_inst_of_raw(key) == OVM_CL_STRING) {
            ovm_obj_str_t ks = ovm_inst_strval_nochk(key);
            if (ks->size > 2 && ks->data[0] == '#') {
                obj_unlock(s->base);

                ovm_inst_t work = ovm_stack_alloc(th, 1);

                ovm_inst_assign_obj(&work[-1], s->base);
                ovm_except_modify_const(th, &work[-1], key);
            }
        }
        ovm_obj_assign(p, ovm_obj_list(*p)->next);
    }

    /* Rather than keeping the pair in the list and over-writing the second 
     * value in a pair, when a key already exists, create a new pair.
     * This means that anyone who has references to pairs in the dict won't
     * see their value mutate as the dict is mutated.
     */

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    pair_new(&work[-1], key, val);
    ovm_obj_assign(bucket, list_new(&work[-1], &work[-1], ovm_obj_list(*bucket))->base);

    ovm_stack_unwind(th, work);

    obj_unlock(s->base);
}

static void dict_put(ovm_thread_t th, ovm_obj_set_t d, ovm_obj_pair_t pr)
{
    dict_at_put(th, d, pr->first, pr->second);
}

static void dict_dels(ovm_obj_set_t s, unsigned key_size, const char *key, unsigned key_hash)
{
    obj_lock(s->base);

    ovm_obj_t *p = dict_finds(s, key_size, key, key_hash, 0);
    if (p != 0) {
        ovm_obj_assign(p, ovm_obj_list(*p)->next);
        DEBUG_ASSERT(s->cnt > 0);
        --s->cnt;
    }

    obj_unlock(s->base);
}

static void dict_del(ovm_thread_t th, ovm_obj_set_t s, ovm_inst_t key)
{
    obj_lock(s->base);

    ovm_obj_t *p = dict_find(th, s, key, 0);
    if (p != 0) {
        ovm_obj_assign(p, ovm_obj_list(*p)->next);
        DEBUG_ASSERT(s->cnt > 0);
        --s->cnt;
    }

    obj_unlock(s->base);
}

static void dict_merge(ovm_thread_t th, ovm_obj_set_t to, ovm_obj_set_t from)
{
    ovm_obj_t *p;
    unsigned n;
    for (p = from->data, n = from->size; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li)) {
            dict_put(th, to, ovm_inst_pairval_nochk(li->item));
        }
    }
}

static inline bool ns_ats(ovm_inst_t dst, ovm_obj_ns_t ns, unsigned nm_size, const char *nm, unsigned hash)
{
    return (dict_ats(dst, ovm_obj_set(ns->dict), nm_size, nm, hash));
}

static inline void ns_ats_put(ovm_thread_t th, ovm_obj_ns_t ns, unsigned name_size, const char *name, unsigned name_hash, ovm_inst_t val)
{
    dict_ats_put(th, ovm_obj_set(ns->dict), name_size, name, name_hash, val);
}

static bool class_ats(ovm_inst_t dst, ovm_obj_class_t cl, unsigned size, const char *data, unsigned hash)
{
    bool result = dict_ats(dst, ovm_obj_set(cl->cl_vars), size, data, hash);
    if (result)  ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
    return (result);
}

static inline void class_ats_put(ovm_thread_t th, ovm_obj_class_t cl, unsigned size, const char *data, unsigned hash, ovm_inst_t val)
{
    dict_ats_put(th, ovm_obj_set(cl->cl_vars), size, data, hash, val);
}

static inline ovm_obj_set_t cl_dict(ovm_obj_class_t cl, unsigned ofs)
{
    return (*(ovm_obj_set_t* )((unsigned char *) cl + ofs));
}

#define CL_OFS_CL_METHODS_DICT    offsetof(struct ovm_obj_class, cl_methods)
#define CL_OFS_INST_METHODS_DICT  offsetof(struct ovm_obj_class, inst_methods)

static inline bool method_findc1_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t search_cl, unsigned method_dict_ofs, unsigned sel_size, const char *sel, unsigned sel_hash, ovm_inst_t found_cl)
{
    bool result = false;
  
    for (; search_cl != 0; search_cl = ovm_obj_class(search_cl->parent)) {
        if (!dict_ats(dst, cl_dict(search_cl, method_dict_ofs), sel_size, sel, sel_hash)) {
            continue;
        }
        ovm_inst_t f = ovm_inst_pairval_nochk(dst)->second;
        switch (f->type) {
        case OVM_INST_TYPE_CODEMETHOD:
        case OVM_INST_TYPE_METHOD:
            break;
        default:
            continue;
        }
        ovm_inst_assign(dst, f);
        if (found_cl != 0)  ovm_inst_assign_obj(found_cl, search_cl->base);
        result = true;

        break;
    }

    return (result);
}

static inline bool method_findc_noexcept_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_inst_t recvr, unsigned sel_size, const char *sel, unsigned sel_hash, ovm_inst_t found_cl)
{
    bool result = false;
    
    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_inst_of(&work[-1], recvr);
    ovm_obj_class_t cl = ovm_inst_classval_nochk(&work[-1]);
    if (!(sel_size > 2 && sel[0] == '_' && sel[1] != '_') || class_up(th, 0) == cl) {
        result = ((cl == 0 || cl->base == ovm_consts.Metaclass)
                  && method_findc1_unsafe(th, dst, ovm_inst_classval_nochk(recvr), CL_OFS_CL_METHODS_DICT, sel_size, sel, sel_hash, found_cl)
                  )
            || method_findc1_unsafe(th, dst, cl, CL_OFS_INST_METHODS_DICT, sel_size, sel, sel_hash, found_cl);
    }

    ovm_stack_unwind(th, work);
    
    return (result);
}

static inline void method_findc_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_inst_t recvr, unsigned sel_size, const char *sel, unsigned sel_hash, ovm_inst_t found_cl)
{
    if (!method_findc_noexcept_unsafe(th, dst, recvr, sel_size, sel, sel_hash, found_cl))  ovm_except_no_methodc(th, recvr, sel_size, sel);
}

static unsigned interp_uint32(ovm_thread_t th)
{
    unsigned result = 0, n;
    for (n = 4; n > 0; --n, ++th->pc)  result = (result << 8) | *th->pc;
    return (result);
}

static long long _interp_intval(ovm_thread_t th, unsigned initial_bits, bool unsignedf)
{
    unsigned char op = *th->pc;
    long long result;
    unsigned n = op >> 5, sign_bit = 0;
    if (n == 7) {
	n = 8;
	result = 0;
	unsignedf = true;
    } else {
	result = op & ((1 << initial_bits) - 1);
        sign_bit = initial_bits + (n << 3) - 1;
    }
    for (++th->pc; n > 0; --n, ++th->pc) {
        result = (result << 8) | *th->pc;
    }
    if (!unsignedf) {
        long long m = 1LL << sign_bit;
        if ((result & m) != 0)  result |= ~(m - 1);
    }

    return (result);
}

static inline long long interp_intval(ovm_thread_t th)
{
    return (_interp_intval(th, 5, false));
}

static inline unsigned long long interp_uintval(ovm_thread_t th)
{
    return ((unsigned long long) _interp_intval(th, 5, true));
}

static void interp_strval(ovm_thread_t th, unsigned *size, const char **data)
{
    unsigned long long _size = interp_uintval(th);
    *size = _size;
    *data = (const char *) th->pc;
    th->pc += _size;
}

static ovm_floatval_t interp_floatval(ovm_thread_t th)
{
    unsigned size;
    const char *data;
    interp_strval(th, &size, &data);
    long double result;
    sscanf(data, "%La", &result);
    return (result);
}

static unsigned symbol_lkup(unsigned bufsize, char *buf, void *addr)
{
    Dl_info dlinfo[1];
    if (dladdr(addr, dlinfo) == 0 || dlinfo->dli_sname == 0) {
        snprintf(buf, bufsize, "%p", addr);
	return (strlen(buf) + 1);
    }
    const char *q = dlinfo->dli_sname;
    unsigned n = strlen(q) + 1;
    char sbuf[n], *p;
    for (p = sbuf; n > 0; --n, ++p, ++q) {
	char c = *q;
	*p = (c == '$') ? '.' : c;
    }
    char obuf[20];
    if (p == dlinfo->dli_saddr) {
	obuf[0] = 0;
    } else {
	snprintf(obuf, sizeof(obuf), "+0x%lx", (unsigned char *) addr - (unsigned char *) dlinfo->dli_saddr);
    }
    snprintf(buf, bufsize, "%s%s", sbuf, obuf);
        
    return (strlen(buf) + 1);
}

static void interp_invalid_opcode(ovm_thread_t th)
{
    unsigned char *p = th->pc_instr_start;
    char sbuf[64];
    symbol_lkup(sizeof(sbuf), sbuf, p);
    unsigned n = th->pc - p;
    char bbuf[3 * n + 1], *q;
    for (q = bbuf; n > 0; --n, q += 3, ++p) {
	sprintf(q, "%02x ", *p);
    }
    OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_INVALID_OPCODE, "%s: %s", sbuf, bbuf);
}

static ovm_inst_t interp_base_ofs(ovm_thread_t th)
{
    unsigned char op = *th->pc;
    unsigned n = op >> 5;
    long long ofs = _interp_intval(th, 3, false);
    struct ovm_frame_mc *mcfp = thread_mcfp(th);
    ovm_inst_t result;
    switch (op & 0x18) {
    case 0:
        if (ofs < 0)  interp_invalid_opcode(th);
        result = th->sp + ofs;
        if (result >= mcfp->bp)  interp_invalid_opcode(th);
        break;
    case 0x08:
        if (ofs >= 0)  interp_invalid_opcode(th);
        result = mcfp->bp + ofs;
        if (result < th->sp)  interp_invalid_opcode(th);
        break;
    case 0x10:
        if (ofs < 0 || ofs >= mcfp->argc)  interp_invalid_opcode(th);
        result = mcfp->ap + ofs;
        break;
    default:
        if (n != 0 || ofs != 0)  interp_invalid_opcode(th);
        result = mcfp->dst;
    }

    return (result);
}

static inline void method_newc(ovm_inst_t dst, ovm_method_t m)
{
    _ovm_objs_lock();

    ovm_inst_release(dst);
    OVM_INST_INIT(dst, OVM_INST_TYPE_METHOD, methodval, m);

    _ovm_objs_unlock();
}

static inline void method_pushc(ovm_thread_t th, ovm_method_t m)
{
    ovm_inst_t p = _ovm_stack_alloc(th, 1);
    OVM_INST_INIT(p, OVM_INST_TYPE_METHOD, methodval, m);

    th->sp = p;
}

#ifdef NDEBUG
#define interp_debug(x)
#define interp_trace(x)
#else

static void interp_debug(ovm_thread_t th)
{
}

static void interp_trace(ovm_thread_t th)
{
    if (!th->tracef)  return;
    unsigned char *p = th->pc_instr_start;
    char buf[64];
    symbol_lkup(sizeof(buf), buf, p);
    fprintf(stderr, "%s: ", buf);
    for ( ; p < th->pc; ++p) {
	fprintf(stderr, "%02x ", *p);
    }
    fputs("\n", stderr);
}

#endif /* NDEBUG */

static void interp(ovm_thread_t th, ovm_method_t m, bool tracef)
{
    unsigned char *old = th->pc;
    th->pc = m;
    struct ovm_frame_mc *mcfp = thread_mcfp(th);
    for (;;) {
	th->pc_instr_start = th->pc;
	interp_debug(th);
        unsigned char op = *th->pc++;
        switch (op) {
        case 0x00:
	    interp_trace(th);
            break;

        case 0x01:
	    {
		unsigned long long n = interp_uintval(th);
		interp_trace(th);
		ovm_stack_free(th, n);
	    }
            break;

        case 0x02:
	    {
		unsigned long long n = interp_uintval(th);
		interp_trace(th);
		ovm_stack_alloc(th, n);
	    }
            break;

        case 0x03:
            {
                unsigned size_free = interp_uintval(th);
                unsigned size_alloc = interp_uintval(th);
		interp_trace(th);
                ovm_stack_free_alloc(th, size_free, size_alloc);
            }
            break;

        case 0x04:
            {
                ovm_inst_t dst = interp_base_ofs(th);
                ovm_inst_t src = interp_base_ofs(th);
		interp_trace(th);
                ovm_inst_assign(dst, src);
            }
            break;

        case 0x05:
	    {
		ovm_inst_t src = interp_base_ofs(th);
		interp_trace(th);
		ovm_stack_push(th, src);
	    }
            break;

        case 0x06:
            {
                ovm_inst_t dst = interp_base_ofs(th);
                unsigned sel_size;
                const char *sel;
                interp_strval(th, &sel_size, &sel);
                unsigned sel_hash = interp_uint32(th);
		unsigned argc = interp_uintval(th);
		interp_trace(th);
		ovm_method_callsch(th, dst, sel_size, sel, sel_hash, argc);
            }
            break;

        case 0x07:
	    interp_trace(th);
	    goto _return;

        case 0x08:
            {
		interp_trace(th);		
                struct ovm_frame_mc *mcfp = thread_mcfp(th);
                ovm_inst_assign(mcfp->dst, &mcfp->ap[0]);
            }
	    goto _return;

        case 0x09:
	    {
		ovm_inst_t var = interp_base_ofs(th);
		interp_trace(th);
		setjmp(ovm_frame_except_push(th, var));
	    }
            break;

        case 0x0a:
	    {
		ovm_inst_t arg = interp_base_ofs(th);
		interp_trace(th);
		ovm_except_raise(th, arg);
	    }
            break;

        case 0x0b:
	    interp_trace(th);
            ovm_except_reraise(th);
            break;

        case 0x0c:
	    interp_trace(th);
            ovm_frame_except_pop(th, 1);
            break;

        case 0x0d:
	    {
		unsigned n = interp_uintval(th);
		interp_trace(th);
		ovm_frame_except_pop(th, n);
	    }
            break;

        case 0x0e:
	    {
		long long ofs = interp_intval(th);
		interp_trace(th);
		th->pc += ofs;
	    }
            break;

        case 0x0f:
	    {
		long long ofs = interp_intval(th);
		interp_trace(th);
		if (ovm_inst_boolval(th, th->sp))  th->pc += ofs;
	    }
            break;
            
        case 0x10:
	    {
		long long ofs = interp_intval(th);
		interp_trace(th);
		if (!ovm_inst_boolval(th, th->sp))  th->pc += ofs;
	    }
            break;
            
        case 0x11:
	    {
		long long ofs = interp_intval(th);
		interp_trace(th);
		if (ovm_except_chk(th))  th->pc += ofs;
	    }
            break;

        case 0x12:
	    {
		long long ofs = interp_intval(th);
		interp_trace(th);
		if (ovm_inst_boolval(th, th->sp))  th->pc += ofs;
		ovm_stack_free(th, 1);
	    }
            break;

        case 0x13:
	    {
		long long ofs = interp_intval(th);
		interp_trace(th);
		if (!ovm_inst_boolval(th, th->sp))  th->pc += ofs;
		ovm_stack_free(th, 1);
	    }
            break;

        case 0x14:
            {
                ovm_inst_t dst = interp_base_ofs(th);
                unsigned size;
                const char *data;
                interp_strval(th, &size, &data);
		unsigned hash = interp_uint32(th);
		interp_trace(th);
		ovm_environ_atc(th, dst, size, data, hash);
            }
            break;
            
        case 0x15:
            {
                unsigned size;
                const char *data;
                interp_strval(th, &size, &data);
		unsigned hash = interp_uint32(th);
		interp_trace(th);
                ovm_environ_atc_push(th, size, data, hash);
            }
            break;

        case 0x16:
	    {
		ovm_inst_t dst = interp_base_ofs(th);
		interp_trace(th);
		ovm_inst_assign_obj(dst, 0);
	    }
            break;

        case 0x17:
	    interp_trace(th);
            ovm_stack_push_obj(th, 0);
            break;

        case 0x18:
        case 0x19:
	    {
		ovm_inst_t dst = interp_base_ofs(th);
		interp_trace(th);
		ovm_bool_newc(dst, op & 1);
	    }
            break;
            
        case 0x1a:
        case 0x1b:
	    interp_trace(th);
            ovm_bool_pushc(th, op & 1);
            break;
            
        case 0x1c:
            {
                ovm_inst_t dst = interp_base_ofs(th);
		ovm_intval_t val = interp_intval(th);
		interp_trace(th);
                ovm_int_newc(dst, val); 
            }
            break;

        case 0x1d:
	    {
		ovm_intval_t val = interp_intval(th);
		interp_trace(th);
		ovm_int_pushc(th, val);
	    }
            break;
             
        case 0x1e:
            {
                ovm_inst_t dst = interp_base_ofs(th);
		ovm_floatval_t val = interp_floatval(th);
		interp_trace(th);
                ovm_float_newc(dst, val);
            }
            break;

        case 0x1f:
	    {
		ovm_floatval_t val = interp_floatval(th);
		interp_trace(th);
		ovm_float_pushc(th, val);
	    }
            break;

        case 0x20:
            {
                ovm_inst_t dst = interp_base_ofs(th);
		long long ofs = interp_intval(th);
		interp_trace(th);
                method_newc(dst, (ovm_method_t)(th->pc + ofs));
            }
            break;

        case 0x21:
	    {
		long long ofs = interp_intval(th);
		interp_trace(th);
		method_pushc(th, (ovm_method_t)(th->pc + ofs));
	    }
            break;

        case 0x22:
            {
                ovm_inst_t dst = interp_base_ofs(th);
                unsigned size;
                const char *data;
                interp_strval(th, &size, &data);
		interp_trace(th);
                ovm_str_newc(dst, size, data);
            }
            break;
            
        case 0x23:
            {
                unsigned size;
                const char *data;
                interp_strval(th, &size, &data);
		interp_trace(th);
                ovm_str_pushc(th, size, data);
            }
            break;
            
        case 0x24:
            {
                ovm_inst_t dst = interp_base_ofs(th);
                unsigned size;
                const char *data;
                interp_strval(th, &size, &data);
		unsigned hash = interp_uint32(th);
		interp_trace(th);
                ovm_str_newch(dst, size, data, hash);
            }
            break;
            
        case 0x25:
            {
                unsigned size;
                const char *data;
                interp_strval(th, &size, &data);
		unsigned hash = interp_uint32(th);
		interp_trace(th);
                ovm_str_pushch(th, size, data, hash);
            }
            break;

	case 0x26:
	    {
		unsigned expected = interp_uintval(th);
		interp_trace(th);
		if (mcfp->argc != expected) {
		    ovm_except_num_args(th, expected);
		}
	    }
	    break;

	case 0x27:
	    {
		unsigned n = interp_uintval(th);
		interp_trace(th);
		ovm_method_array_arg_push(th, n);
	    }
	    break;
	    
        default:
            interp_invalid_opcode(th);
        }
    }

 _return:
    th->pc = old;
}

static inline void method_run(ovm_thread_t th, ovm_inst_t dst, ovm_obj_ns_t ns, ovm_obj_class_t cl, ovm_inst_t method, unsigned argc, ovm_inst_t argv)
{
    struct ovm_frame *fr = frame_mc_push(th, dst, cl, method, argc, argv);
    if (cl != 0)  ns = ovm_obj_ns(cl->ns);
    if (ns != 0)  frame_ns_push(th, ns);

    switch (method->type) {
    case OVM_INST_TYPE_CODEMETHOD:
        (*method->codemethodval)(th, dst, argc, argv);
        break;
    case OVM_INST_TYPE_METHOD:
        interp(th, method->methodval, true);
        break;
    default:
        abort();
    }

    frame_pop(th, fr);
}

void ovm_method_callsch(ovm_thread_t th, ovm_inst_t dst, unsigned sel_size, const char *sel, unsigned sel_hash, unsigned argc)
{
    ovm_inst_t argv = th->sp, recvr = &argv[0];
    
    ovm_inst_t work = ovm_stack_alloc(th, 2);

    method_findc_unsafe(th, &work[-1], recvr, sel_size, sel, sel_hash, &work[-2]);
    method_run(th, dst, 0, ovm_inst_classval_nochk(&work[-2]), &work[-1], argc, argv);
  
    ovm_stack_unwind(th, work);
}

ovm_obj_array_t ovm_method_array_arg_push(ovm_thread_t th, unsigned num_fixed)
{
    struct ovm_frame_mc *mcfp = thread_mcfp(th);
    if (mcfp->argc < num_fixed)  ovm_except_num_args_min(th, num_fixed);
    ovm_stack_alloc(th, 1);
    return (array_newc(th->sp, OVM_CL_ARRAY, mcfp->argc - num_fixed, &mcfp->ap[num_fixed]));
}

bool ovm_bool_if(ovm_thread_t th)
{
    bool result = ovm_inst_boolval(th, th->sp);
    ovm_stack_free(th, 1);
    return (result);
}

/***************************************************************************/

/* Parsing strings into instances */

static bool parse(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data);

static void parse_trim(unsigned *size, const char **data)
{
    unsigned n = *size;
    const char *p = *data, *end = p + n;
    for (; n > 0; --n, ++p) {
        if (!isspace(*p))  break;
    }
    for (; n > 0; --n) {
        if (!isspace(*--end))  break;
    }
    *size = n;
    *data = p;
}

static int parse_delim_find(char delim, unsigned size, const char *data)
{
    const char *p;
    for (p = data; size > 0; --size, ++p) {
        char c = *p;
        char close_delim;
        switch (c) {
        case '"':
            for (--size, ++p; size > 0; --size, ++p) {
                c = *p;
                if (c == '\\') {
                    --size, ++p;
                    if (size == 0)  break;
                    continue;
                }
                if (c == '"')  break;
            }
            if (size == 0)  return (-1);
            continue;
        case '<':
            close_delim = '>';
            break;
        case '(':
            close_delim = ')';
            break;
        case '[':
            close_delim = ']';
            break;
        case '{':
            close_delim = '}';
            break;
        default:
            if (c == delim)  return (p - data);
            continue;
        }

        int ofs = parse_delim_find(close_delim, size - 1, p + 1);
        if (ofs < 0)  return (-1);
        ++ofs;
        size -= ofs;
        p += ofs;
    }

    return (-1);
}


static bool parse_nil(ovm_inst_t dst, unsigned size, const char *data)
{
    if (size == 4 && strncmp(data, "#nil", 4) == 0) {
        ovm_inst_assign_obj(dst, 0);
        return (true);
    }

    return (false);
}

static bool parse_bool(ovm_inst_t dst, unsigned size, const char *data)
{
    if (size == 5 && strncmp(data, "#true", 5) == 0) {
        ovm_bool_newc(dst, 1);
        return (true);
    }
    if (size == 6 && strncmp(data, "#false", 6) == 0) {
        ovm_bool_newc(dst, 0);
        return (true);
    }

    return (false);
}

static int parse_digit(char c, unsigned base)
{
    if (c < '0')  return (-1);
    int d;
    if (c <= '9') {
        d = c - '0';
    } else {
        c = toupper(c);
        if (c < 'A')  return (-1);
        d = c - 'A' + 10;
    }
    
    return (d >= base ? -1 : d);
}

static bool parse_int_base(ovm_inst_t dst, unsigned size, const char *data, unsigned base, bool allow_negf)
{
    ovm_intval_t val = 0;
    bool negf = false;
    unsigned i;
    for (i = 0; size > 0; --size, ++i, ++data) {
        char c = *data;
        if (c == '-' && i == 0 && allow_negf) {
            negf = true;
            continue;
        }
        int d = parse_digit(c, base);
        if (d < 0)  return (false);
        val = base * val + d;
    }
    if (negf)  val = -val;
    ovm_int_newc(dst, val);

    return (true);
}

static bool parse_int(ovm_inst_t dst, unsigned size, const char *data)
{
    if (size >= 3 && data[0] == '0') {
        switch (toupper(data[1])) {
        case 'B':
            return (parse_int_base(dst, size - 2, data + 2, 2, false));
        case 'X':
            return (parse_int_base(dst, size - 2, data + 2, 16, false));
        default: ;
        }
    }
    if (size >= 2 && data[0] == '0')  return (parse_int_base(dst, size - 1, data + 1, 8, false));
    return (parse_int_base(dst, size, data, 10, true));
}

static bool parse_float(ovm_inst_t dst, unsigned size, const char *data)
{
    unsigned n = size;
    const char *p = data;
    if (n >= 1 && *p == '-') {
        --n;  ++p;
    }
    char c;
    unsigned k;
    for (k = 0; n > 0; --n, ++p, ++k) {
        c = *p;
        if (c == '.' || toupper(c) == 'E')  break;
        if (!isdigit(c))  return (false);
    }
    if (k == 0)  return (false);
    if (n > 0 && c == '.') {
        for (--n, ++p, k = 0; n > 0; --n, ++p, ++k) {
            c = *p;
            if (toupper(c) == 'E')  break;
            if (!isdigit(c))  return (false);
        }
        if (k == 0)  return (false);
    }
    if (n > 0 && toupper(c) == 'E') {
        --n, ++p;
        if (n > 0 && ((c = *p) == '+' || c == '-')) {
            --n, ++p;
        }
        for (k = 0; n > 0; --n, ++p, ++k) {
            c = *p;
            if (!isdigit(c))  return (false);
        }
        if (k == 0)  return (false);
    }

    char buf[size + 1];
    memcpy(buf, data, size);
    buf[size] = 0;
    ovm_floatval_t val;
    assert(sscanf(buf, "%Lg", &val) == 1);
    ovm_float_newc(dst, val);

    return (true);
}

static bool _parse_string(struct ovm_clist *cl, unsigned size, const char *data)
{
    for (; size > 0; --size, ++data) {
        char c = *data;
        if (c == '"')  return (false);
        if (c == '\\') {
            --size;  ++data;
            if (size == 0)  return (false);
            switch (*data) {
            case '\\':
                c = '\\';
                break;
            case '"':
                c = '"';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'x':
                {
                    --size;  ++data;
                    if (size < 2)  break;
                    int d = parse_digit(data[0], 16);
                    if (d < 0)  return (false);
                    unsigned x = d << 4;
                    d = parse_digit(data[1], 16);
                    if (d < 0)  return (false);
                    c = x + d;
                    --size;  ++data;
                }
                break;
            default:
                return (false);
            }
        }
        ovm_clist_append_char(cl, c);
    }

    return (true);
}

static bool parse_string(ovm_inst_t dst, unsigned size, const char *data)
{
    if (size < 2 || data[0] != '"' || data[size - 1] != '"')  return (false);

    struct ovm_clist cl[1];
    ovm_clist_init(cl);
    bool result = _parse_string(cl, size - 2, data + 1);
    if (result)  str_new_clist(dst, cl);
    ovm_clist_fini(cl);

    return (result);
}

static bool parse_pair(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data)
{
    if (size < 2 || data[0] != '<' || data[size - 1] != '>')  return (false);
    size -= 2;
    ++data;
    int ofs = parse_delim_find(',', size, data);
    if (ofs < 0)  return (false);

    bool result = false;
    
    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    result = parse(th, &work[-1], ofs, data) && parse(th, &work[-2], size - (ofs + 1), data + ofs + 1);
    if (result)  pair_new(dst, &work[-1], &work[-2]);

    ovm_stack_unwind(th, work);

    return (result);    
}

static bool parse_list(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data)
{
    if (size < 2 || data[0] != '(' || data[size - 1] != ')')  return (false);
    size -= 2;
    ++data;

    bool result = false;
    
    ovm_inst_t work = ovm_stack_alloc(th, 2);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);

    while (size > 0) {
        int ofs = parse_delim_find(',', size, data);
        if (!parse(th, &work[-2], (ofs < 0) ? size : ofs, data))  break;
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
        if (ofs < 0) {
            result = true;
            break;
        }
        ++ofs;
        size -= ofs;
        data += ofs;
    }

    if (result)  ovm_inst_assign(dst, &work[-1]);
    ovm_stack_unwind(th, work);

    return (result);
}

static void list_to_array_size_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, unsigned size, ovm_obj_list_t li);

static bool parse_array(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data)
{
    if (size < 2 || data[0] != '[' || data[size - 1] != ']')  return (false);
    size -= 2;
    ++data;

    bool result = false;
    
    ovm_inst_t work = ovm_stack_alloc(th, 2);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    
    unsigned asize = 0;
    while (size > 0) {
        int ofs = parse_delim_find(',', size, data);
        if (!parse(th, &work[-2], (ofs < 0) ? size : ofs, data))  break;
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
        ++asize;
        if (ofs < 0) {
            result = true;
            break;
        }
        ++ofs;
        size -= ofs;
        data += ofs;
    }

    if (result)  list_to_array_size_unsafe(th, dst, OVM_CL_ARRAY, asize, ovm_inst_listval_nochk(&work[-1]));
    ovm_stack_unwind(th, work);

    return (result);
}

static bool parse_set(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data)
{
    if (size < 2 || data[0] != '{' || data[size - 1] != '}')  return (false);
    size -= 2;
    ++data;

    bool result = false;

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    ovm_obj_set_t s = set_newc(&work[-1], OVM_CL_SET, 16);
    
    for (; size > 0; --size, ++data) {
        int ofs = parse_delim_find(',', size, data);
        if (!parse(th, &work[-2], (ofs < 0) ? size : ofs, data))  break;
        set_put(th, s, &work[-2]);
        if (ofs < 0) {
            result = true;
            break;
        }
        ++ofs;
        size -= ofs;
        data += ofs;
    }

    if (result)  ovm_inst_assign(dst, &work[-1]);
    ovm_stack_unwind(th, work);
    
    return (result);
}

static bool parse_dict(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data)
{
    if (size < 2 || data[0] != '{' || data[size - 1] != '}')  return (false);
    size -= 2;
    ++data;

    bool result = false;

    ovm_inst_t work = ovm_stack_alloc(th, 3);
    ovm_obj_set_t s = set_newc(&work[-1], OVM_CL_DICTIONARY, 16);
    
    for (; size > 0; --size, ++data) {
        int ofs = parse_delim_find(',', size, data);
        unsigned nn = (ofs < 0) ? size : ofs;
        int ofs2 = parse_delim_find(':', nn, data);
        if (ofs2 < 0
            || !parse(th, &work[-2], ofs2, data)
            || !parse(th, &work[-3], nn - (ofs2 + 1), data + ofs2 + 1)
            ) {
            break;
        }
        dict_at_put(th, s, &work[-2], &work[-3]);
        if (ofs < 0) {
            result = true;
            break;
        }
        ++ofs;
        size -= ofs;
        data += ofs;
    }

    if (result)  ovm_inst_assign(dst, &work[-1]);
    ovm_stack_unwind(th, work);
    
    return (result);
}


static bool parse_object(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data)
{
    bool result = false;
    
    int ofs_at = parse_delim_find('@', size, data);
    int ofs_dict = parse_delim_find('{', size, data);
    if (ofs_at < 0 || ofs_dict < 0)  return (false);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    if (parse_dict(th, &work[-2], size - ofs_dict, data + ofs_dict)) {
        ovm_environ_atc(th, &work[-1], ofs_at, data, str_hashc(ofs_at, data));
        ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(new), 2);
        result = true;
    }

    ovm_stack_unwind(th, work);

    return (result);
}


static bool parse(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data)
{
    parse_trim(&size, &data);
    return (size > 0
            && (parse_nil(dst, size, data)
                || parse_bool(dst, size, data)
                || parse_int(dst, size, data)
                || parse_float(dst, size, data)
                || parse_string(dst, size, data)
                || parse_pair(th, dst, size, data)
                || parse_list(th, dst, size, data)
                || parse_array(th, dst, size, data)
                || parse_dict(th, dst, size, data)
                || parse_set(th, dst, size, data)
                || parse_object(th, dst, size, data)
                )
            );
}

/***************************************************************************/

#define CM_ARGC_CHK(n)           ovm_method_argc_chk_exact(th, (n))
#define CM_ARGC_MIN_CHK(n)       ovm_method_argc_chk_min(th, (n))
#define CM_ARGC_RANGE_CHK(a, b)  ovm_method_argc_chk_range(th, (a), (b))

#define METHOD_MODULE  main

/* Methods for Object class */

/*  HERE  */


#undef  METHOD_CLASS
#define METHOD_CLASS  Object

CM_DECL(init)
{
    CM_ARGC_RANGE_CHK(1, 2);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_USER)  ovm_except_inv_value(th, recvr);
    if (argc == 2)  dict_merge(th, ovm_inst_setval_nochk(recvr), ovm_inst_dictval(th, &argv[1]));
    ovm_inst_assign(dst, recvr);
}

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, !ovm_inst_is_nil(&argv[0]));
}

CM_DECL(List)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_is_nil(recvr)) {
        ovm_inst_assign_obj(dst, 0);
        return;
    }
    if (ovm_inst_of_raw(recvr) != OVM_CL_USER)  ovm_except_inv_value(th, recvr);
    ovm_obj_set_t s = ovm_inst_setval_nochk(recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    ovm_obj_t *p;
    unsigned n;
    for (p = s->data, n = s->size; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li)) {
            ovm_inst_t i = li->item;
            ovm_inst_t k = ovm_inst_pairval_nochk(i)->first;
            DEBUG_ASSERT(ovm_inst_of_raw(k) == OVM_CL_STRING);
            if (str_equalc(ovm_inst_strval_nochk(k), _OVM_STR_CONST("__instanceof__")))  continue;

            /* This works because of the way dicts work -- when an existing key
             * is over-writted, a new pair is created, and entered into the hash
             * table.  So, making references to pairs in dict is permissible,
             * changes to the dict won't affect the list.
             * See dict_ats_put() / dict_at_put().
             */
            list_newlc_concat(lc, list_new(&work[-2], i, 0));
        }
    }
    ovm_inst_assign(dst, &work[-1]);
}

/* Method 'String' is alias for 'write' */

static void method_redirect(ovm_thread_t th, ovm_inst_t dst, unsigned sel_size, const char *sel, unsigned sel_hash, unsigned argc, ovm_inst_t argv)
{
    ovm_inst_t old = ovm_stack_alloc(th, argc);

    ovm_inst_t p, q;
    unsigned n;
    for (p = th->sp, q = argv, n = argc; n > 0; --n, ++p, ++q)  ovm_inst_assign(p, q);
    ovm_method_callsch(th, dst, sel_size, sel, sel_hash, argc);

    ovm_stack_unwind(th, old);
}

CM_DECL(new)
{
    CM_ARGC_MIN_CHK(1);

    ovm_stack_push(th, &argv[0]);
    ovm_inst_t p = th->sp;
    ovm_method_callsch(th, p, _OVM_STR_CONST_HASH("__alloc__"), 1);

    if (argc > 1) {
        unsigned n = argc - 1;
        ovm_stack_alloc(th, n);
        ovm_inst_t q = th->sp;
        ovm_inst_assign(q, p);
        for (++q, p = &argv[1]; n > 0; --n, ++p, ++q)  ovm_inst_assign(q, p);
    }
    ovm_method_callsch(th, dst, _OVM_STR_CONST_HASH("__init__"), argc);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_is_nil(recvr)) {
        ovm_inst_assign(dst, recvr);
        return;
    }
    if (ovm_inst_of_raw(recvr) == OVM_CL_USER) {
        ovm_inst_t work = ovm_stack_alloc(th, 1);

        set_copy_unsafe(th, &work[-1], OVM_CL_USER, ovm_inst_setval_nochk(recvr));
        ovm_inst_assign(dst, &work[1]);
        
        return;
    }

    ovm_except_inv_value(th, recvr);
}

CM_DECL(copydeep)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_is_nil(recvr)) {
        ovm_inst_assign(dst, recvr);
        return;
    }
    if (ovm_inst_of_raw(recvr) == OVM_CL_USER) {
        ovm_inst_t work = ovm_stack_alloc(th, 1);

        set_copydeep_unsafe(th, &work[-1], OVM_CL_USER, ovm_inst_setval_nochk(recvr));
        ovm_inst_assign(dst, &work[-1]);

        return;
    }
    
    ovm_except_inv_value(th, recvr);
}

static bool _obj_at(ovm_thread_t th, ovm_inst_t dst, ovm_inst_t inst, ovm_inst_t key)
{
    bool result = false;

    ovm_obj_str_t s = ovm_inst_strval(th, key);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_of(&work[-1], inst);
    if (!(s->size > 2 && s->data[0] == '_' && s->data[1] != '_') || class_up(th, 1) == ovm_inst_classval_nochk(&work[-1])) {
        str_inst_hash(key);
        result = dict_ats(dst, ovm_inst_setval_nochk(inst), s->size, s->data, key->hash);
    }

    ovm_stack_unwind(th, work);

    return (result);
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], key = &argv[1];
    if (ovm_inst_of_raw(recvr) != OVM_CL_USER)  ovm_except_inv_value(th, recvr);
    if (!_obj_at(th, dst, recvr, key))  ovm_inst_assign_obj(dst, 0);
}

CM_DECL(ate)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], key = &argv[1];
    if (ovm_inst_of_raw(recvr) != OVM_CL_USER)  ovm_except_inv_value(th, recvr);
    if (_obj_at(th, dst, recvr, key)) {
        ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
        return;
    }
    ovm_except_no_attr(th, recvr, key);
}

CM_DECL(atdefault)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0], key = &argv[1];
    if (ovm_inst_of_raw(recvr) != OVM_CL_USER)  ovm_except_inv_value(th, recvr);
    if (_obj_at(th, dst, recvr, key)) {
        ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
        return;
    }
    ovm_inst_assign(dst, &argv[2]);
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_USER)  ovm_except_inv_value(th, recvr);
    ovm_inst_t key = &argv[1];
    ovm_obj_str_t s = ovm_inst_strval(th, key);
    str_inst_hash(key);
    ovm_inst_t val = &argv[2];
    dict_ats_put(th, ovm_inst_setval_nochk(recvr), s->size, s->data, key->hash, val);
    ovm_inst_assign(dst, val);
}

CM_DECL(cons)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_inst_is_nil(recvr))  ovm_except_inv_value(th, recvr);
    list_new(dst, &argv[1], 0);
}

CM_DECL(enumerate)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign(&work[-3], &argv[0]);
    ovm_method_callsch(th, &work[-3], OVM_STR_CONST_HASH(List), 1);
    ovm_obj_list_t li = ovm_inst_listval_nochk(&work[-3]);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    unsigned i;
    for (i = 0; li != 0; li = ovm_list_next(li), ++i) {
        ovm_int_newc(&work[-2], i);
        pair_new(&work[-2], &work[-2], li->item);
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
    }
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_bool_newc(dst, recvr->type == OVM_INST_TYPE_OBJ && arg->type == OVM_INST_TYPE_OBJ
                  && recvr->objval == arg->objval
                  );
}

CM_DECL(isnil)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, ovm_inst_is_nil(&argv[0]));
}

CM_DECL(instanceof)
{
    CM_ARGC_CHK(1);
    ovm_inst_of(dst, &argv[0]);
}

CM_DECL(method)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_str_t s = ovm_inst_strval(th, arg);
    str_inst_hash(arg);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_of(&work[-1], recvr);
    if (!method_findc1_unsafe(th, dst, ovm_inst_classval_nochk(&work[-1]), CL_OFS_INST_METHODS_DICT, s->size, s->data, arg->hash, 0)) {
        ovm_inst_assign_obj(dst, 0);
    }
}

static void ovm_stdout(ovm_thread_t th, ovm_inst_t dst)
{
    if (!class_ats(dst, OVM_CL_FILE, _OVM_STR_CONST_HASH("stdout"))) {
        ovm_file_newc(th, dst, _OVM_STR_CONST("stdout"), _OVM_STR_CONST("w"), stdout);
    }
}

CM_DECL(print)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_inst_assign(&work[-2], recvr);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(String), 1);
    ovm_stdout(th, &work[-2]);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 2);
    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(flush), 1);

    ovm_inst_assign(dst, recvr);
}

CM_DECL(println)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_inst_assign(&work[-2], recvr);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(String), 1);
    ovm_stdout(th, &work[-2]);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(writeln), 2);

    ovm_inst_assign(dst, recvr);
}

CM_DECL(reverse)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_inst_is_nil(recvr))  ovm_except_inv_value(th, recvr);
    ovm_inst_assign(dst, recvr);
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_inst_is_nil(recvr))  ovm_except_inv_value(th, recvr);
    ovm_int_newc(dst, 0);
}

static ovm_obj_str_t obj_write_unsafe(ovm_inst_t dst, ovm_obj_t obj)
{
    if (obj == 0) {
        return (str_newc(dst, _OVM_STR_CONST("#nil")));
    }

    unsigned size = 1;
    struct ovm_str_newv_item a[3];

    ovm_obj_inst_of(dst, obj);
    ovm_obj_str_t s = ovm_obj_str(ovm_inst_classval_nochk(dst)->name);
    a[0].size = s->size;
    a[0].data = s->data;
    size += s->size - 1;
    static const char at[] = "@";
    a[1].size = sizeof(at);
    a[1].data = at;
    ++size;
    char buf[19];
    snprintf(buf, sizeof(buf), "%p", obj);
    unsigned n = strlen(buf);
    a[2].size = n + 1;
    a[2].data = buf;
    size += n;
    ovm_obj_str_t result = str_newv_size(dst, size, ARRAY_SIZE(a), a);

    return (result);
}

static ovm_obj_str_t user_write_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_set_t s)
{
    obj_lock(s->base);

    ovm_obj_t *p;
    unsigned n;
    struct ovm_str_newv_item a[4];
    static const char ldr[] = "{", quote[] = "\"", sep[] = ", ", sep2[] = "\": ", trlr[] = "}";
    a[0].size = sizeof(quote);
    a[0].data = quote;
    a[2].size = sizeof(sep2);
    a[2].data = sep2;
    ovm_inst_assign_obj(dst, 0);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    unsigned size = sizeof(ldr) + sizeof(trlr) - 1;
    bool f = false;

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    for (p = s->data, n = s->size; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li), f = true) {
            ovm_obj_pair_t pr = ovm_inst_pairval_nochk(li->item);
            ovm_inst_t k = pr->first;
            DEBUG_ASSERT(ovm_inst_of_raw(k) == OVM_CL_STRING);
            ovm_obj_str_t s = ovm_inst_strval_nochk(k);
            if (str_equalc(s, _OVM_STR_CONST("__instanceof__")))  continue;
            if (f)  size += sizeof(sep) - 1;
            unsigned size2 = sizeof(quote) + sizeof(sep2) - 1;
            a[1].size = s->size;
            a[1].data = s->data;
            size2 += s->size - 1;
            ovm_inst_assign(th->sp, pr->second);
            ovm_method_callsch(th, th->sp, OVM_STR_CONST_HASH(write), 1);
            s = ovm_inst_strval(th, th->sp);
            a[3].size = s->size;
            a[3].data = s->data;
            size2 += s->size - 1;
            ovm_obj_str_t ss = str_newv_size(&work[-1], size2, ARRAY_SIZE(a), a);
            size += ss->size - 1;
            list_newlc_concat(lc, list_new(&work[-1], &work[-1], 0));
        }
    }

    ovm_stack_unwind(th, work);
    
    obj_unlock(s->base);

    ovm_obj_str_t result = str_joinc_size(dst, size, sizeof(ldr), ldr, sizeof(sep), sep, sizeof(trlr), trlr, ovm_inst_listval_nochk(dst));

    return (result);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (recvr->type != OVM_INST_TYPE_OBJ)  ovm_except_inv_value(th, recvr);
    ovm_obj_t obj = recvr->objval;

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_obj_str_t s1 = obj_write_unsafe(&work[-1], obj);
    if (ovm_obj_inst_of_raw(obj) != OVM_CL_USER) {
        ovm_inst_assign(dst, &work[-1]);
        return;
    }

    ovm_stack_alloc(th, 1);
    ovm_obj_str_t s2 = user_write_unsafe(th, &work[-2], ovm_obj_set(obj));
    struct ovm_str_newv_item a[2];
    a[0].size = s1->size;
    a[0].data = s1->data;
    a[1].size = s2->size;
    a[1].data = s2->data;
    str_newv_size(dst, s1->size + s2->size - 1, ARRAY_SIZE(a), a);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Boolean

/* Method 'Boolean' is alias for 'copy' */

CM_DECL(Integer)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ovm_inst_boolval(th, &argv[0]) ? 1 : 0);
}

/* Method 'String' is alias for 'write' */

CM_DECL(new)
{
    CM_ARGC_CHK(2);
    method_redirect(th, dst, OVM_STR_CONST_HASH(Boolean), 1, &argv[1]);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

/* Method 'copydeep' is alias for 'copy' */

CM_DECL(and)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, ovm_inst_boolval(th, &argv[0]) && ovm_inst_boolval(th, &argv[1]));
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    ovm_inst_t arg = &argv[1];
    bool b = ovm_inst_boolval(th, &argv[0]);
    ovm_bool_newc(dst, arg->type == OVM_INST_TYPE_BOOL && arg->boolval == b);
}

CM_DECL(not)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, !ovm_inst_boolval(th, &argv[0]));
}

CM_DECL(or)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, ovm_inst_boolval(th, &argv[0]) || ovm_inst_boolval(th, &argv[1]));
}

static const char str_true[] = "#true", str_false[] = "#false";

static const char *bool_to_str(bool val, unsigned *size)
{
    if (val) {
        *size = sizeof(str_true);
        return (str_true);
    }
    *size = sizeof(str_false);
    return (str_false);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    unsigned size;
    const char *p = bool_to_str(ovm_inst_boolval(th, &argv[0]), &size);
    str_newc(dst, size, p);
}

CM_DECL(xor)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, ovm_inst_boolval(th, &argv[0]) ^ ovm_inst_boolval(th, &argv[1]));
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Integer

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, ovm_inst_intval(th, &argv[0]) != 0);
}

/* Method 'Integer' is alias for 'copy' */

CM_DECL(Float)
{
    CM_ARGC_CHK(1);
    ovm_float_newc(dst, (ovm_floatval_t) ovm_inst_intval(th, &argv[0]));
}

/* Method 'String' is alias for 'write' */

CM_DECL(new)
{
    CM_ARGC_MIN_CHK(2);
    method_redirect(th, dst, OVM_STR_CONST_HASH(Integer), argc - 1, &argv[1]);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

/* Method 'copydeep' is alias for 'copy' */

CM_DECL(add)
{
    CM_ARGC_CHK(2);
    ovm_intval_t i = ovm_inst_intval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    switch (arg->type) {
    case OVM_INST_TYPE_INT:
        ovm_int_newc(dst, i + arg->intval);
        return;
    case OVM_INST_TYPE_FLOAT:
        ovm_float_newc(dst, (ovm_floatval_t) i + arg->floatval);
        return;
    default: ;
    }

    ovm_except_inv_value(th, arg);
}

static int int_cmp(ovm_thread_t th, ovm_intval_t i, ovm_inst_t arg)
{
    switch (arg->type) {
    case OVM_INST_TYPE_INT:
        {
            ovm_intval_t j = arg->intval;
            return ((i < j) ? -1 : ((i > j) ? 1 : 0));
        }
    case OVM_INST_TYPE_FLOAT:
        {
            ovm_floatval_t f = (ovm_floatval_t) i, g = arg->floatval;
            return ((f < g) ? -1 : ((f > g) ? 1 : 0));
        }
    default: ;
    }
    
    ovm_except_inv_value(th, arg);
}

CM_DECL(band)
{
    CM_ARGC_CHK(2);
    ovm_int_newc(dst, ovm_inst_intval(th, &argv[0]) & ovm_inst_intval(th, &argv[1]));
}

CM_DECL(bor)
{
    CM_ARGC_CHK(2);
    ovm_int_newc(dst, ovm_inst_intval(th, &argv[0]) | ovm_inst_intval(th, &argv[1]));
}

CM_DECL(cmp)
{
    CM_ARGC_CHK(2);
    ovm_int_newc(dst, int_cmp(th, ovm_inst_intval(th, &argv[0]), &argv[1]));
}

CM_DECL(div)
{
    CM_ARGC_CHK(2);
    ovm_intval_t i = ovm_inst_intval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    switch (arg->type) {
    case OVM_INST_TYPE_INT:
        ovm_int_newc(dst, i / arg->intval);
        return;
    case OVM_INST_TYPE_FLOAT:
        ovm_float_newc(dst, (ovm_floatval_t) i / arg->floatval);
        return;
    default: ;
    }

    ovm_except_inv_value(th, arg);
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    ovm_intval_t i = ovm_inst_intval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    ovm_bool_newc(dst, arg->type == OVM_INST_TYPE_INT && arg->intval == i);
}

CM_DECL(ge)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, int_cmp(th, ovm_inst_intval(th, &argv[0]), &argv[1]) >= 0);
}

CM_DECL(gt)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, int_cmp(th, ovm_inst_intval(th, &argv[0]), &argv[1]) > 0);
}

CM_DECL(hash)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!recvr->hash_valid) {
        recvr->hash = mem_hash(sizeof(recvr->intval), &recvr->intval);
        recvr->hash_valid = true;
    }
    ovm_int_newc(dst, recvr->hash);
}

CM_DECL(le)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, int_cmp(th, ovm_inst_intval(th, &argv[0]), &argv[1]) <= 0);
}

CM_DECL(lt)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, int_cmp(th, ovm_inst_intval(th, &argv[0]), &argv[1]) < 0);
}

CM_DECL(minus)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, -ovm_inst_intval(th, &argv[0]));
}

CM_DECL(mod)
{
    CM_ARGC_CHK(2);
    ovm_int_newc(dst, ovm_inst_intval(th, &argv[0]) % ovm_inst_intval(th, &argv[1]));
}

CM_DECL(mul)
{
    CM_ARGC_CHK(2);
    ovm_intval_t i = ovm_inst_intval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    switch (arg->type) {
    case OVM_INST_TYPE_INT:
        ovm_int_newc(dst, i * arg->intval);
        return;
    case OVM_INST_TYPE_FLOAT:
        ovm_float_newc(dst, (ovm_floatval_t) i * arg->floatval);
        return;
    default: ;
    }

    ovm_except_inv_value(th, arg);
}

CM_DECL(sub)
{
    CM_ARGC_CHK(2);
    ovm_intval_t i = ovm_inst_intval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    switch (arg->type) {
    case OVM_INST_TYPE_INT:
        ovm_int_newc(dst, i - arg->intval);
        return;
    case OVM_INST_TYPE_FLOAT:
        ovm_float_newc(dst, (ovm_floatval_t) i - arg->floatval);
        return;
    default: ;
    }

    ovm_except_inv_value(th, arg);
}

CM_DECL(write)
{
    CM_ARGC_RANGE_CHK(1, 2);
    ovm_intval_t val = ovm_inst_intval(th, &argv[0]);
    char buf[8 * sizeof(val) + 1];
    if (argc < 2) {
        snprintf(buf, sizeof(buf), "%lld", val);
        str_newc1(dst, buf);
        return;
    }
    
    ovm_intval_t base = ovm_inst_intval(th, &argv[1]);
    if (base < 2 || base > 16)  ovm_except_inv_value(th, &argv[1]);
    char *p = &buf[sizeof(buf)];
    *--p = 0;
    if (val == 0) {
        *--p = '0';
    } else {
        while (val != 0) {
            static const char digits[] = "0123456789ABCDEF";
            *--p = digits[val % base];
            val /= base;
        }
    }
    str_newc(dst, &buf[sizeof(buf)] - p, p);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Float

/* String is alias for write */

CM_DECL(sub)
{
    ovm_floatval_t val = ovm_inst_floatval(th, &argv[0]), val2;
    ovm_inst_t arg = &argv[1];
    switch (arg->type) {
    case OVM_INST_TYPE_INT:
        val2 = (ovm_floatval_t) arg->intval;
        break;
    case OVM_INST_TYPE_FLOAT:
        val2 =  arg->floatval;
        break;
    default:
        ovm_except_inv_value(th, arg);
    }

    ovm_float_newc(dst, val - val2);    
}

CM_DECL(div)
{
    ovm_floatval_t numer = ovm_inst_floatval(th, &argv[0]), denom;
    ovm_inst_t arg = &argv[1];
    switch (arg->type) {
    case OVM_INST_TYPE_INT:
        denom = (ovm_floatval_t) arg->intval;
        break;
    case OVM_INST_TYPE_FLOAT:
        denom =  arg->floatval;
        break;
    default:
        ovm_except_inv_value(th, arg);
    }

    ovm_float_newc(dst, numer / denom);
}

CM_DECL(write)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%Lg", ovm_inst_floatval(th, &argv[0]));
    str_newc1(dst, buf);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Codemethod

CM_DECL(call)
{
    CM_ARGC_MIN_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (recvr->type != OVM_INST_TYPE_CODEMETHOD)  ovm_except_inv_value(th, recvr);

    /* Override the "main" namespace pushed by calling this method, use namespace this method was called from */

    method_run(th, dst, ns_up(th, 1), 0, recvr, argc - 1, &argv[1]);
}

CM_DECL(calla)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    if (recvr->type != OVM_INST_TYPE_CODEMETHOD)  ovm_except_inv_value(th, recvr);
    ovm_obj_list_t li = ovm_inst_listval(th, arg);
    unsigned _argc = list_size(li);
    if (_argc < 1)  ovm_except_inv_value(th, arg);

    ovm_stack_alloc(th, _argc);

    ovm_inst_t p;
    for (p = th->sp; li != 0; li = ovm_list_next(li), ++p) {
        ovm_inst_assign(p, li->item);
    }

    /* Override the "main" namespace pushed by calling this method, use  namespace this method was called from */

    method_run(th, dst, ns_up(th, 1), 0, recvr, _argc, th->sp);
}

static ovm_obj_str_t method_write(ovm_inst_t dst, ovm_inst_t src)
{
    struct ovm_str_newv_item a[3] = { { 0 } };
    unsigned size = 1;
    void *p;
    static const char m[] = "&Method(", cm[] = "&Codemethod(";
    const char *q;
    unsigned n;
    switch (src->type) {
    case OVM_INST_TYPE_METHOD:
        p = src->methodval;
        q = m;
        n = sizeof(m);
        break;
    case OVM_INST_TYPE_CODEMETHOD:
        p = src->codemethodval;
        q = cm;
        n = sizeof(cm);
        break;
    default:
        abort();
    }
    a[0].size = n;
    a[0].data = q;;
    size += n - 1;
    char buf[64];
    n = symbol_lkup(sizeof(buf), buf, p);
    a[1].size = n;
    a[1].data = buf;
    size += n - 1;
    static const char rpar[] = ")";
    a[2].size = sizeof(rpar);
    a[2].data = rpar;
    ++size;
    return (str_newv_size(dst, size, ARRAY_SIZE(a), a));
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (recvr->type != OVM_INST_TYPE_CODEMETHOD)  ovm_except_inv_value(th, recvr);
    method_write(dst, recvr);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Method

CM_DECL(call)
{
    CM_ARGC_MIN_CHK(2);
    ovm_inst_t recvr = &argv[0];
    if (recvr->type != OVM_INST_TYPE_METHOD)  ovm_except_inv_value(th, recvr);

    /* Override the "main" namespace pushed by calling this method, use namespace this method was called from */

    method_run(th, dst, ns_up(th, 1), 0, recvr, argc - 1, &argv[1]);
}

CM_DECL(calla)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    if (recvr->type != OVM_INST_TYPE_METHOD)  ovm_except_inv_value(th, recvr);
    ovm_obj_list_t li = ovm_inst_listval(th, arg);
    unsigned _argc = list_size(li);
    if (_argc < 1)  ovm_except_inv_value(th, arg);

    ovm_stack_alloc(th, _argc);

    ovm_inst_t p;
    for (p = th->sp; li != 0; li = ovm_list_next(li), ++p) {
        ovm_inst_assign(p, li->item);
    }

    /* Override the "main" namespace pushed by calling this method, use namespace this method was called from */

    method_run(th, dst, ns_up(th, 1), 0, recvr, _argc, th->sp);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (recvr->type != OVM_INST_TYPE_METHOD)  ovm_except_inv_value(th, recvr);
    method_write(dst, recvr);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  String

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_str_t s = ovm_inst_strval(th, recvr);
    if (parse_bool(dst, s->size - 1, s->data))  return;
    ovm_except_inv_value(th, recvr);
}

CM_DECL(Integer)
{
    CM_ARGC_RANGE_CHK(1, 2);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_str_t s = ovm_inst_strval(th, recvr);
    ovm_intval_t base = 10;
    if (argc == 2) {
        ovm_inst_t arg = &argv[1];
        base = ovm_inst_intval(th, arg);
        if (base < 2 || base > 16)  ovm_except_inv_value(th, arg);
        if (parse_int_base(dst, s->size - 1, s->data, base, true))  return;
    } else if (parse_int(dst, s->size - 1, s->data))  return;
    ovm_except_inv_value(th, recvr);
}

/* Method 'String' is alias for 'copy' */

static void str_to_array_unsafe(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_str_t s)
{
    ovm_obj_array_t a = array_newc(dst, cl, s->size - 1, 0);
    ovm_inst_t p;
    const char *q;
    unsigned n;
    for (p = a->data, q = s->data, n = a->size; n > 0; --n, ++p, ++q) {
        str_newc(p, 2, q);
    }
}

CM_DECL(Array)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    str_to_array_unsafe(&work[-1], OVM_CL_ARRAY, ovm_inst_strval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Carray)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    str_to_array_unsafe(&work[-1], OVM_CL_CARRAY, ovm_inst_strval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Bytearray)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_str_t s = ovm_inst_strval(th, recvr);
    
    struct ovm_clist cl[1];
    ovm_clist_init(cl);
    bool f = _parse_string(cl, s->size - 1, s->data);
    if (f)  barray_new_clist(dst, OVM_CL_BYTEARRAY, cl);
    ovm_clist_fini(cl);
    if (!f)  ovm_except_inv_value(th, recvr);
}

CM_DECL(Cbytearray)
{
    CM_ARGC_CHK(1);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    barray_newc(dst, OVM_CL_CBYTEARRAY, s->size - 1, (unsigned char *) s->data);
}

CM_DECL(Slice)
{
    CM_ARGC_CHK(3);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    ovm_intval_t ofs = ovm_inst_intval(th, &argv[1]);
    ovm_intval_t size = ovm_inst_intval(th, &argv[2]);
    if (!slice(&ofs, &size, s->size - 1))  ovm_except_idx_range2(th, &argv[0], &argv[1], &argv[2]);
    slice_new(dst, OVM_CL_CSLICE, s->base, ofs, size);
}

/* Method 'Cslice' is alias for 'Slice' */

CM_DECL(new)
{
    CM_ARGC_CHK(2);
    method_redirect(th, dst, OVM_STR_CONST_HASH(String), 1, &argv[1]);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

/* Method 'copydeep' is alias for 'copy' */

/* Method 'add' is alias for 'concat' */
 
CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_str_t s = ovm_inst_strval(th, recvr);
    ovm_intval_t ofs = ovm_inst_intval(th, arg);
    if (!slice1(&ofs, s->size - 1))  ovm_except_idx_range(th, recvr, arg);
    str_slicec(dst, s, ofs, 1);
}

CM_DECL(call)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_str_t s = ovm_inst_strval(th, recvr);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[1]);
    unsigned _argc = list_size(li);
    if (_argc < 1)  ovm_except_inv_value(th, &argv[1]);
    str_inst_hash(recvr);

    ovm_stack_alloc(th, _argc);

    ovm_inst_t p;
    for (p = th->sp; li != 0; li = ovm_list_next(li), ++p) {
        ovm_inst_assign(p, li->item);
    }
    ovm_method_callsch(th, dst, s->size, s->data, recvr->hash, _argc);
}

CM_DECL(cmp)
{
    CM_ARGC_CHK(2);
    ovm_obj_str_t s1 = ovm_inst_strval(th, &argv[0]);
    ovm_obj_str_t s2 = ovm_inst_strval(th, &argv[1]);
    ovm_int_newc(dst, strcmp(s1->data, s2->data));
}

CM_DECL(concat)
{
    CM_ARGC_CHK(2);
    ovm_obj_str_t s1 = ovm_inst_strval(th, &argv[0]);
    ovm_obj_str_t s2 = ovm_inst_strval(th, &argv[1]);
    struct ovm_str_newv_item a[2] = {
        { s1->size, s1->data },
        { s2->size, s2->data }
    };
    str_newv_size(dst, s1->size + s2->size - 1, ARRAY_SIZE(a), a);
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    ovm_bool_newc(dst, ovm_inst_of_raw(arg) == OVM_CL_STRING && str_equal(s, ovm_inst_strval_nochk(arg)));
}

CM_DECL(format)
{
    ovm_obj_array_t a = ovm_method_array_arg_push(th, 1);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_str_t s = ovm_inst_strval(th, recvr);
    unsigned n = s->size - 1, ofs = 0, size = 1;

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);

    while (ofs < n) {
        int i = str_indexc(s, "[", ofs);
        if (i < 0) {
            if (str_indexc(s, "]", ofs) >= 0)  ovm_except_inv_value(th, recvr);
            unsigned k = n - ofs;
            str_slicec(&work[-2], s, ofs, k);
            list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
            size += k;
            break;
        }
        int j = str_indexc(s, "]", i);
        if (j <= (i + 1))  ovm_except_inv_value(th, recvr);
        if (i > ofs) {
            unsigned k = i - ofs;
            str_slicec(&work[-2], s, ofs, k);
            list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
            size += k;
        }
        ++i;
        if (!parse_int(&work[-2], j - i, s->data + i))  ovm_except_inv_value(th, recvr);
        if (!array_at(&work[-2], a, work[-2].intval))  ovm_except_idx_range(th, &work[0], &work[-2]);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(String), 1);
        size += ovm_inst_strval(th, &work[-2])->size - 1;
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
        ofs = j + 1;
    }

    str_joinc_size(dst, size, 0, 0, 0, 0, 0, 0, ovm_inst_listval_nochk(&work[-1]));
}

CM_DECL(hash)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    str_inst_hash(recvr);
    ovm_int_newc(dst, recvr->hash);
}

CM_DECL(index)
{
    CM_ARGC_RANGE_CHK(2, 3);
    ovm_obj_str_t s1 = ovm_inst_strval(th, &argv[0]);
    ovm_obj_str_t s2 = ovm_inst_strval(th, &argv[1]);
    ovm_intval_t ofs = 0;
    if (argc == 3) {
        ofs = ovm_inst_intval(th, &argv[2]);
        if (!slice1(&ofs, s1->size - 1))  ovm_except_idx_range(th, &argv[0], &argv[2]);
    }
    int i = str_index(s1, s2, ofs);
    if (i < 0) {
        ovm_inst_assign_obj(dst, 0);
        return;
    }
    ovm_int_newc(dst, i);
}

CM_DECL(rindex)
{
    CM_ARGC_RANGE_CHK(2, 3);
    ovm_obj_str_t s1 = ovm_inst_strval(th, &argv[0]);
    ovm_obj_str_t s2 = ovm_inst_strval(th, &argv[1]);
    ovm_intval_t n = s2->size - 1;
    ovm_intval_t ofs = s1->size - 1;
    if (argc == 3) {
        ofs = ovm_inst_intval(th, &argv[2]);
        if (!slice1(&ofs, s1->size - 1))  ovm_except_idx_range(th, &argv[0], &argv[2]);
    }
    for (; ofs >= n; --ofs) {
        unsigned k = ofs - n;
        if (strncmp(s1->data + k, s2->data, n) == 0) {
            ovm_int_newc(dst, k);
            return;
        }
    }
    ovm_inst_assign_obj(dst, 0);
}

CM_DECL(join)
{
    CM_ARGC_CHK(2);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[1]);
    str_joinc(th, dst, 0, 0, s->size, s->data, 0, 0, li);
}

CM_DECL(parse)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_str_t s = ovm_inst_strval(th, recvr);
    if (parse(th, dst, s->size - 1, s->data))  return;
    ovm_except_inv_value(th, recvr);
}

CM_DECL(rjoin)
{
    CM_ARGC_CHK(2);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    unsigned size = 1, ss = s->size - 1;
    bool f = false;
    for ( ; li != 0; li = ovm_list_next(li), f = true) {
        if (f)  size += ss;
        list_new(&work[-1], li->item, ovm_inst_listval_nochk(&work[-1]));
        size += ovm_inst_strval(th, li->item)->size - 1;
    }
    str_joinc_size(dst, size, 0, 0, s->size, s->data, 0, 0, ovm_inst_listval_nochk(&work[-1]));
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);
    ovm_intval_t size = ovm_inst_strval(th, &argv[0])->size;
    if (size > 0)  --size;
    ovm_int_newc(dst, size);
}

CM_DECL(slice)
{
    CM_ARGC_CHK(3);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    ovm_inst_t ofs = &argv[1], len = &argv[2];
    ovm_intval_t _ofs = ovm_inst_intval(th, ofs);
    ovm_intval_t _len = ovm_inst_intval(th, len);
    if (!slice(&_ofs, &_len, s->size - 1))  ovm_except_idx_range(th, ofs, len);
    str_slicec(dst, s, _ofs, _len);
}

static ovm_obj_list_t str_splitc_unsafe(ovm_thread_t th, ovm_inst_t dst, unsigned size, const char *data, unsigned delim_size, const char *delim)
{
    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign_obj(dst, 0);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    unsigned k;
    for (--size; size > 0; size -= k, data += k) {
        const char *r = strstr(data, delim);
        if (r == 0) {
            str_newc(&work[-1], size + 1, data);
            list_newlc_concat(lc, list_new(&work[-1], &work[-1], 0));
            break;
        }
        k = r - data;
        str_newc(&work[-1], k + 1, data);
        list_newlc_concat(lc, list_new(&work[-1], &work[-1], 0));
        k += delim_size - 1;
    }

    ovm_stack_unwind(th, work);

    return (ovm_inst_listval_nochk(dst));
}

CM_DECL(split)
{
    CM_ARGC_CHK(2);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    ovm_obj_str_t d = ovm_inst_strval(th, &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    str_splitc_unsafe(th, &work[-1], s->size, s->data, d->size, d->data);
    ovm_inst_assign(dst, &work[-1]);
}

static bool barray_needs_quotes(unsigned size, unsigned char *data)
{
    for (; size > 0; --size, ++data) {
        unsigned char c = *data;
        if (!isprint(c) || isspace(c) || c == '"')  return (true);
    }
    return (false);
}

static void barray_write(struct ovm_clist *cl, unsigned size, unsigned char *data)
{
    for (; size > 0; --size, ++data) {
        unsigned char c = *data;
        if (c == '"') {
            ovm_clist_appendc(cl, _OVM_STR_CONST("\\\""));
            continue;
        }
        if (isprint(c)) {
            ovm_clist_append_char(cl, c);
            continue;
        }
        char buf[5];
        snprintf(buf, sizeof(buf), "\\x%02x", c);
        ovm_clist_appendc(cl, sizeof(buf), buf);
    }
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[0]);
    struct ovm_clist cl[1];
    ovm_clist_init(cl);
    unsigned n = s->size;
    if (n > 0)  --n;
    ovm_clist_append_char(cl, '"');
    barray_write(cl, n, (unsigned char *) s->data);
    ovm_clist_append_char(cl, '"');
    str_new_clist(dst, cl);
    ovm_clist_fini(cl);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Pair

/* Method 'String' is alias for 'write' */

/* Method 'Pair' is alias for 'copy' */

CM_DECL(List)
{
    CM_ARGC_CHK(1);
    ovm_obj_pair_t pr = ovm_inst_pairval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_new(&work[-1], pr->second, 0);
    list_new(dst, pr->first, ovm_inst_listval_nochk(&work[-1]));
}

CM_DECL(new)
{
    switch (argc) {
    case 2:
        method_redirect(th, dst, OVM_STR_CONST_HASH(Pair), 1, &argv[1]);
        break;
    case 3:
        pair_new(dst, &argv[1], &argv[2]);
        break;
    default:
        ovm_except_num_args_range(th, 2, 3);
    }
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(copydeep)
{
    CM_ARGC_CHK(1);
    pair_copydeep(th, dst, ovm_inst_pairval(th, &argv[0]));
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    bool result = false;
    ovm_obj_pair_t pr = ovm_inst_pairval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    if (ovm_inst_of_raw(arg) == OVM_CL_PAIR) {
        ovm_obj_pair_t pr2 = ovm_inst_pairval_nochk(arg);
        
        ovm_inst_t work = ovm_stack_alloc(th, 2);

        ovm_inst_assign(&work[-2], pr->first);
        ovm_inst_assign(&work[-1], pr2->first);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(equal), 2);
        if (ovm_inst_boolval(th, &work[-1])) {
            ovm_inst_assign(&work[-2], pr->second);
            ovm_inst_assign(&work[-1], pr2->second);
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(equal), 2);
            result = ovm_inst_boolval(th, &work[-1]);
        }
    }
    ovm_bool_newc(dst, result); 
}

CM_DECL(first)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, ovm_inst_pairval(th, &argv[0])->first);
}

CM_DECL(hash)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!recvr->hash_valid) {
        ovm_obj_pair_t pr = ovm_inst_pairval(th, recvr);

        ovm_inst_t work = ovm_stack_alloc(th, 1);
        
        ovm_inst_assign(th->sp, pr->first);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(hash), 1);
        recvr->hash = ovm_inst_intval(th, &work[-1]);
        ovm_inst_assign(th->sp, pr->second);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(hash), 1);
        recvr->hash += ovm_inst_intval(th, &work[-1]);
        recvr->hash_valid = true;
    }

    ovm_int_newc(dst, recvr->hash);
}
 
CM_DECL(second)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, ovm_inst_pairval(th, &argv[0])->second);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_obj_pair_t pr = ovm_inst_pairval(th, &argv[0]);

    obj_lock_loop_chk(th, pr->base);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_inst_assign(th->sp, pr->first);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 1);
    ovm_inst_assign(th->sp, pr->second);
    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(write), 1);
    ovm_obj_str_t s1 = ovm_inst_strval(th, &work[-1]);
    ovm_obj_str_t s2 = ovm_inst_strval(th, &work[-2]);
    struct ovm_str_newv_item a[] = {
        { _OVM_STR_CONST("<") },
        { s1->size, s1->data },
        { _OVM_STR_CONST(", ") },
        { s2->size, s2->data },
        { _OVM_STR_CONST(">") }
    };
    str_newv_size(dst, 1 + s1->size - 1 + 2 + s2->size - 1 + 1 + 1, ARRAY_SIZE(a), a);

    obj_unlock(pr->base);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  List

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, !ovm_inst_is_nil(&argv[0]));
}

/* Method 'String' is alias for 'write' */

/* Method 'List' is alias for 'copy' */

static void list_to_array_size_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, unsigned size, ovm_obj_list_t li)
{
    ovm_obj_array_t a = array_newc(dst, cl, size, 0);
    ovm_inst_t p;
    for (p = a->data; li != 0; li = ovm_list_next(li), ++p) {
        ovm_inst_assign(p, li->item);
    }
}

static inline void list_to_array_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_inst_t arg)
{
    ovm_obj_list_t li = ovm_inst_listval(th, arg);
    list_to_array_size_unsafe(th, dst, cl, list_size(li), li);
}

CM_DECL(Array)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_to_array_unsafe(th, &work[-1], OVM_CL_ARRAY, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Carray)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_to_array_unsafe(th, &work[-1], OVM_CL_CARRAY, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

static void list_to_set_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_inst_t arg)
{
    ovm_obj_list_t li = ovm_inst_listval(th, arg);
    ovm_obj_set_t s = set_newc(dst, cl, class_default_size(th, cl, 16));
    for (; li != 0; li = ovm_list_next(li))  set_put(th, s, li->item);
}

CM_DECL(Set)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_to_set_unsafe(th, &work[-1], OVM_CL_SET, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Cset)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_to_set_unsafe(th, &work[-1], OVM_CL_CSET, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

static void list_to_dict_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_inst_t arg)
{
    ovm_obj_list_t li = ovm_inst_listval(th, arg);
    ovm_obj_set_t s = set_newc(dst, cl, class_default_size(th, cl, 16));
    for (; li != 0; li = ovm_list_next(li)) {
        ovm_inst_t item = li->item;
        if (ovm_inst_of_raw(item) != OVM_CL_PAIR)  ovm_except_inv_value(th, arg);
        ovm_obj_pair_t pr = ovm_inst_pairval_nochk(item);
        dict_at_put(th, s, pr->first, pr->second);
    }
}

CM_DECL(Dictionary)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_to_dict_unsafe(th, &work[-1], OVM_CL_DICTIONARY, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Cdictionary)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_to_dict_unsafe(th, &work[-1], OVM_CL_CDICTIONARY, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(new)
{
    switch (argc) {
    case 2:
        method_redirect(th, dst, OVM_STR_CONST_HASH(List), 1, &argv[1]);
        break;
    case 3:
        list_new(dst, &argv[1], ovm_inst_listval(th, &argv[2]));
        break;
    default:
        ovm_except_num_args_range(th, 2, 3);
    }    
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(copydeep)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_copydeep_unsafe(th, &work[-1], ovm_inst_listval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_list_t li = ovm_inst_listval(th, recvr);
    ovm_intval_t ofs = ovm_inst_intval(th, arg);
    if (!slice1(&ofs, list_size(li)))  ovm_except_idx_range(th, recvr, arg);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_slicec_unsafe(th, &work[-1], li, ofs, 1);
    ovm_inst_assign(dst, ovm_inst_is_nil(&work[-1]) ? &work[-1] : ovm_inst_listval_nochk(&work[-1])->item);
}

CM_DECL(car)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, ovm_inst_listval(th, &argv[0])->item);
}

CM_DECL(cdr)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_listval(th, &argv[0])->next);
}

CM_DECL(concat)
{
    CM_ARGC_CHK(2);
    ovm_obj_list_t li1 = ovm_inst_listval(th, &argv[0]);
    ovm_obj_list_t li2 = ovm_inst_listval(th, &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    list_concat_unsafe(th, &work[-1], li1, li2);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(cons)
{
    CM_ARGC_CHK(2);
    list_new(dst, &argv[1], ovm_inst_listval(th, &argv[0]));
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    bool result = false;
    if (ovm_inst_of_raw(arg) == OVM_CL_LIST) {
        ovm_obj_list_t li2 = ovm_inst_listval_nochk(arg);

        ovm_inst_t work = ovm_stack_alloc(th, 2);
        
        for (; li != 0 && li2 != 0; li = ovm_list_next(li), li2 = ovm_list_next(li2)) {
            ovm_inst_assign(th->sp, li->item);
            ovm_inst_assign(&th->sp[1], li2->item);
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(equal), 2);
            if (!ovm_inst_boolval(th, &work[-1]))  break;
        }
        result = (li == 0 && li2 == 0);
    }
    ovm_bool_newc(dst, result);
}

CM_DECL(hash)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!recvr->hash_valid) {
        recvr->hash = list_hash(th, ovm_inst_listval(th, recvr));
        recvr->hash_valid = true;
    }

    ovm_int_newc(dst, recvr->hash);
}

CM_DECL(map1)
{
    CM_ARGC_CHK(2);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 3);
    
    ovm_inst_assign(th->sp, &argv[1]);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    for (; li != 0; li = ovm_list_next(li)) {
        list_new(&th->sp[1], li->item, 0);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(call), 2);
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
    }
    
    ovm_inst_assign(dst, &work[-1]);
}       

CM_DECL(map)
{
    CM_ARGC_CHK(2);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 3);
    
    ovm_inst_assign(th->sp, &argv[1]);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    for (; li != 0; li = ovm_list_next(li)) {
        ovm_inst_assign(&th->sp[1], li->item);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(call), 2);
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
    }
    
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(reduce1)
{
    CM_ARGC_CHK(3);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign(th->sp, &argv[1]);
    ovm_inst_assign(&work[-1], &argv[2]);
    for (; li != 0; li = ovm_list_next(li)) {
        list_new(&th->sp[1], li->item, 0);
        list_new(&th->sp[1], &work[-1], ovm_inst_listval_nochk(&th->sp[1]));
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(call), 2);
    }

    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(reduce)
{
    CM_ARGC_CHK(3);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign(th->sp, &argv[1]);
    ovm_inst_assign(&work[-1], &argv[2]);
    for (; li != 0; li = ovm_list_next(li)) {
        list_new(&th->sp[1], &work[-1], ovm_inst_listval(th, li->item));
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(call), 2);
    }
    
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(reverse)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_reverse_unsafe(&work[-1], ovm_inst_listval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, list_size(ovm_inst_listval(th, &argv[0])));
}

CM_DECL(slice)
{
    CM_ARGC_CHK(3);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[0]);
    ovm_inst_t ofs = &argv[1], len = &argv[2];
    ovm_intval_t _ofs = ovm_inst_intval(th, ofs);
    ovm_intval_t _len = ovm_inst_intval(th, len);
    if (!slice(&_ofs, &_len, list_size(li)))  ovm_except_idx_range(th, ofs, len);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    list_slicec_unsafe(th, &work[-1], li, _ofs, _len);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_obj_list_t li = ovm_inst_listval(th, &argv[0]);

    obj_lock_loop_chk(th, li->base);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    static const char ldr[] = "(", sep[] = ", ", trlr[] = ")";
    unsigned size = sizeof(ldr) + sizeof(trlr) - 1;
    bool f = false;
    ovm_obj_list_t p;
    for (p = li; p != 0; p = ovm_list_next(p), f = true) {
        if (f)  size += sizeof(sep) - 1;
        ovm_inst_assign(th->sp, p->item);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(write), 1);
        size += ovm_inst_strval(th, &work[-2])->size - 1;
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
    }

    str_joinc_size(dst, size, sizeof(ldr), ldr, sizeof(sep), sep, sizeof(trlr), trlr, ovm_inst_listval_nochk(&work[-1]));

    obj_unlock(li->base);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Array

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_ARRAY)) ovm_except_inv_value(th, recvr);
    ovm_bool_newc(dst, ovm_inst_arrayval_nochk(recvr)->size > 0);
}

/* Method 'Integer' is alias for 'size' */

/* Method 'String' is alias for 'write' */

CM_DECL(List)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_ARRAY)) ovm_except_inv_value(th, recvr);
    ovm_obj_array_t a = ovm_inst_arrayval_nochk(recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    ovm_inst_t p;
    unsigned n;
    for (p = a->data, n = a->size; n > 0; --n, ++p) {
        list_newlc_concat(lc, list_new(&work[-2], p, 0));
    }
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Array)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(Carray)
{
    CM_ARGC_CHK(1);
    array_copy(dst, OVM_CL_CARRAY, ovm_inst_arrayval(th, &argv[0]));
}

CM_DECL(Slice)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_array_t a = ovm_inst_arrayval(th, recvr);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]), len = ovm_inst_intval(th, &argv[2]);
    if (!slice(&idx, &len, a->size))  ovm_except_idx_range2(th, recvr, &argv[1], &argv[2]);
    slice_new(dst, OVM_CL_SLICE, a->base, idx, len);
}

CM_DECL(Cslice)
{
    CM_ARGC_CHK(3);
    ovm_obj_array_t a = ovm_inst_arrayval(th, &argv[0]);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]), len = ovm_inst_intval(th, &argv[2]);
    if (!slice(&idx, &len, a->size))  ovm_except_idx_range2(th, &argv[0], &argv[1], &argv[2]);
    slice_new(dst, OVM_CL_CSLICE, a->base, idx, len);
}

CM_DECL(new)
{
    CM_ARGC_CHK(2);
    ovm_inst_t arg = &argv[1];
    ovm_obj_class_t cl = ovm_inst_of_raw(arg);
    if (cl == OVM_CL_INTEGER) {
        array_newc(dst, OVM_CL_ARRAY, arg->intval, 0);
        return;
    }
    if (cl == OVM_CL_ARRAY) {
        array_copy(dst, OVM_CL_ARRAY, ovm_inst_arrayval_nochk(arg));
        return;
    }

    method_redirect(th, dst, OVM_STR_CONST_HASH(Array), 1, arg);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_obj_array_t a = ovm_inst_arrayval(th, &argv[0]);
    array_copy(dst, ovm_obj_class(a->base->inst_of), a);
}

CM_DECL(copydeep)
{
    CM_ARGC_CHK(1);
    ovm_obj_array_t a = ovm_inst_arrayval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    array_copydeep_unsafe(th, &work[-1], ovm_obj_inst_of_raw(a->base), a);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    if (!array_at(dst, ovm_inst_arrayval(th, recvr), ovm_inst_intval(th, arg))) {
        ovm_except_idx_range(th, recvr, arg);
    }
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0], arg = &argv[1], val = &argv[2];
    if (ovm_inst_of_raw(recvr) != OVM_CL_ARRAY)  ovm_except_inv_value(th, recvr);
    if (!array_at_put(ovm_inst_arrayval_nochk(recvr), ovm_inst_intval(th, arg), val)) {
        ovm_except_idx_range(th, recvr, arg);
    }
    ovm_inst_assign(dst, val);
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    ovm_obj_array_t a = ovm_inst_arrayval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    bool result = false;
    if (ovm_is_subclass_of(ovm_inst_of_raw(arg), OVM_CL_ARRAY)) {
        ovm_obj_array_t a2 = ovm_inst_arrayval_nochk(arg);
        if (a2->size == a->size) {
            ovm_inst_t work = ovm_stack_alloc(th, 2);

            ovm_inst_t p, q;
            unsigned n;
            for (p = a->data, q = a2->data, n = a->size; n > 0; --n, ++p, ++q) {
                ovm_inst_assign(th->sp, p);
                ovm_inst_assign(&th->sp[1], q);
                ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(equal), 2);
                if (!ovm_inst_boolval(th, &work[-1]))  break;
            }
            result = (n == 0);
        }
    }
    ovm_bool_newc(dst, result);
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ovm_inst_arrayval(th, &argv[0])->size);
}

CM_DECL(slice)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_array_t a = ovm_inst_arrayval(th, recvr);
    ovm_inst_t ofs = &argv[1], len = &argv[2];
    ovm_intval_t _ofs = ovm_inst_intval(th, ofs);
    ovm_intval_t _len = ovm_inst_intval(th, len);
    if (!slice(&_ofs, &_len, a->size))  ovm_except_idx_range2(th, recvr, ofs, len);
    array_slicec(dst, ovm_obj_inst_of_raw(a->base), a, _ofs, _len);
}

static void array_write_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_t obj, unsigned data_size, ovm_inst_t data, unsigned ldr_size, const char *ldr, unsigned trlr_size, const char *trlr)
{
    obj_lock_loop_chk(th, obj);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign_obj(dst, 0);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    static const char sep[] = ", ";
    unsigned size = ldr_size + trlr_size - 1;
    bool f = false;
    for (; data_size > 0; --data_size, ++data, f = true) {
        if (f)  size += sizeof(sep) - 1;
        ovm_inst_assign(th->sp, data);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 1);
        size += ovm_inst_strval(th, &work[-1])->size - 1;
        list_newlc_concat(lc, list_new(&work[-1], &work[-1], 0));
    }
    
    str_joinc_size(dst, size, ldr_size, ldr, sizeof(sep), sep, trlr_size, trlr, ovm_inst_listval_nochk(dst));

    ovm_stack_unwind(th, work);

    obj_unlock(obj);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_ARRAY)  ovm_except_inv_value(th, recvr);
    ovm_obj_array_t a = ovm_inst_arrayval_nochk(recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    array_write_unsafe(th, &work[-1], a->base, a->size, a->data, _OVM_STR_CONST("["), _OVM_STR_CONST("]"));
    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Carray

/* Method 'Boolean' inherited */

CM_DECL(Array)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_CARRAY)  ovm_except_inv_value(th, recvr);
    array_copy(dst, OVM_CL_CARRAY, ovm_inst_arrayval_nochk(recvr));
}

/* Method 'Carray' is alias for 'copy' */

/* Method 'Cslice' inherited */

/* Method 'Integer' inherited */

/* Method 'Slice'  inherited */

/* Method 'String' is alias for 'write' */

/* Method 'List' inherited */
 
CM_DECL(new)
{
    CM_ARGC_CHK(1);
    method_redirect(th, dst, OVM_STR_CONST_HASH(Carray), 1, &argv[1]);
}

/* Method 'at' inherited */

/* Method 'atput' inherited */
 
CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

/* Method 'copydeep' inherited */

/* Method 'equal' inherited */

CM_DECL(hash)
{    
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!recvr->hash_valid) {
        if (ovm_inst_of_raw(recvr) != OVM_CL_CARRAY) ovm_except_inv_value(th, recvr);
        ovm_obj_array_t a = ovm_inst_arrayval_nochk(recvr);
        recvr->hash = array_hash(th, a->base, a->size, a->data);
        recvr->hash_valid = true;
    }

    ovm_int_newc(dst, recvr->hash);
}

/* Method 'size' inherited */

/* Method 'slice' inherited */
 
CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_CARRAY) ovm_except_inv_value(th, recvr);
    ovm_obj_array_t a = ovm_inst_arrayval_nochk(recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    array_write_unsafe(th, &work[-1], a->base, a->size, a->data, _OVM_STR_CONST("#Carray.new(["), _OVM_STR_CONST("])"));
    ovm_inst_assign(dst, &work[-1]);
}
 
/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Bytearray

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_BYTEARRAY)) ovm_except_inv_value(th, recvr);
    ovm_bool_newc(dst, ovm_inst_barrayval_nochk(recvr)->size > 0);
}

/* Method 'Integer' is alias for 'size' */

CM_DECL(String)
{
    CM_ARGC_CHK(1);
    ovm_obj_barray_t b = ovm_inst_barrayval(th, &argv[0]);
    struct ovm_clist cl[1];
    ovm_clist_init(cl);
    bool f = barray_needs_quotes(b->size, b->data);
    if (f)  ovm_clist_append_char(cl, '"');
    barray_write(cl, b->size, b->data);
    if (f)  ovm_clist_append_char(cl, '"');
    str_new_clist(dst, cl);
    ovm_clist_fini(cl);
}

CM_DECL(List)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_BYTEARRAY)) ovm_except_inv_value(th, recvr);
    ovm_obj_barray_t b = ovm_inst_barrayval_nochk(recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    unsigned char *p;
    unsigned n;
    for (p = b->data, n = b->size; n > 0; --n, ++p) {
        ovm_int_newc(&work[-2], *p);
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
    }
    ovm_inst_assign(dst, &work[-1]);
}

static void barray_to_array_unsafe(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_barray_t b)
{
    ovm_obj_array_t a = array_newc(dst, cl, b->size, 0);
    ovm_inst_t p;
    unsigned char *q;
    unsigned n;
    for (p = a->data, q = b->data, n = b->size; n > 0; --n, ++p, ++q) {
        ovm_int_newc(p, *q);
    }
}

CM_DECL(Array)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_BYTEARRAY)) ovm_except_inv_value(th, recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    barray_to_array_unsafe(&work[-1], OVM_CL_ARRAY, ovm_inst_barrayval_nochk(recvr));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Carray)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_BYTEARRAY)) ovm_except_inv_value(th, recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    barray_to_array_unsafe(&work[-1], OVM_CL_CARRAY, ovm_inst_barrayval_nochk(recvr));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Bytearray)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(Slice)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_barray_t b = ovm_inst_barrayval(th, recvr);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]), len = ovm_inst_intval(th, &argv[2]);
    if (!slice(&idx, &len, b->size))  ovm_except_idx_range2(th, recvr, &argv[1], &argv[2]);
    slice_new(dst, OVM_CL_SLICE, b->base, idx, len);
}

CM_DECL(Cslice)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_barray_t b = ovm_inst_barrayval(th, recvr);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]), len = ovm_inst_intval(th, &argv[2]);
    if (!slice(&idx, &len, b->size))  ovm_except_idx_range2(th, recvr, &argv[1], &argv[2]);
    slice_new(dst, OVM_CL_CSLICE, b->base, idx, len);
}

CM_DECL(new)
{
    CM_ARGC_CHK(2);
    ovm_inst_t arg = &argv[1];
    ovm_obj_class_t cl = ovm_inst_of_raw(arg);
    if (cl == OVM_CL_INTEGER) {
        barray_newc(dst, OVM_CL_BYTEARRAY, arg->intval, 0);
        return;
    }
    if (cl == OVM_CL_BYTEARRAY) {
        barray_copy(dst, OVM_CL_BYTEARRAY, ovm_inst_barrayval_nochk(arg));
        return;
    }

    method_redirect(th, dst, OVM_STR_CONST_HASH(Bytearray), 1, arg);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_obj_barray_t b = ovm_inst_barrayval(th, &argv[0]);
    barray_copy(dst, ovm_obj_inst_of_raw(b->base), b);
}

/* Method 'copydeep' is alias for 'copy' */

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_barray_t b = ovm_inst_barrayval(th, recvr);
    ovm_intval_t idx = ovm_inst_intval(th, arg);
    if (!slice1(&idx, b->size))  ovm_except_idx_range(th, recvr, arg);
    ovm_int_newc(dst, b->data[idx]);
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0], arg = &argv[1], val = &argv[2];
    if (ovm_inst_of_raw(recvr) != OVM_CL_BYTEARRAY)  ovm_except_inv_value(th, recvr);
    ovm_obj_barray_t b = ovm_inst_barrayval_nochk(recvr);
    ovm_intval_t idx = ovm_inst_intval(th, arg);
    ovm_intval_t byte = ovm_inst_intval(th, val);
    if (!slice1(&idx, b->size))  ovm_except_idx_range(th, recvr, arg);
    if (byte < 0 || byte > 255)  ovm_except_inv_value(th, val);
    b->data[idx] = byte;
    ovm_inst_assign(dst, val);
}

CM_DECL(cmp)
{
    CM_ARGC_CHK(2);
    ovm_obj_barray_t b1 = ovm_inst_barrayval(th, &argv[0]);
    ovm_obj_barray_t b2 = ovm_inst_barrayval(th, &argv[1]);
    unsigned size1 = b1->size, size2 = b2->size;
    unsigned n = size1;
    if (size2 < n)  n = size2;
    int result = memcmp(b1->data, b2->data, n);
    if (result == 0) {
        result = (size1 < size2) ? -1 : ((size1 > size2) ? 1 : 0);
    }
    ovm_int_newc(dst, result);
}

CM_DECL(equal)
{
    CM_ARGC_CHK(2);
    ovm_obj_barray_t b = ovm_inst_barrayval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    bool result = false;
    if (ovm_is_subclass_of(ovm_inst_of_raw(arg), OVM_CL_BYTEARRAY)) {
        ovm_obj_barray_t b2 = ovm_inst_barrayval_nochk(arg);
        result = (b->size == b2->size && memcmp(b->data, b2->data, b->size) == 0);
    }
    ovm_bool_newc(dst, result);
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ovm_inst_barrayval(th, &argv[0])->size);
}

CM_DECL(slice)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0], arg1 = &argv[1], arg2 = &argv[2];
    ovm_obj_barray_t b = ovm_inst_barrayval(th, recvr);
    ovm_intval_t ofs = ovm_inst_intval(th, arg1);
    ovm_intval_t len = ovm_inst_intval(th, arg2);
    if (!slice(&ofs, &len, b->size))  ovm_except_idx_range2(th, recvr, arg1, arg2);
    barray_slicec(dst, ovm_obj_inst_of_raw(b->base), b, ofs, len);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_BYTEARRAY)  ovm_except_inv_value(th, recvr);
    ovm_obj_barray_t b = ovm_inst_barrayval_nochk(recvr);
    struct ovm_clist cl[1];
    ovm_clist_init(cl);
    ovm_clist_appendc(cl, _OVM_STR_CONST("#Bytearray(\""));
    barray_write(cl, b->size, b->data);
    ovm_clist_appendc(cl, _OVM_STR_CONST("\")"));
    str_new_clist(dst, cl);
    ovm_clist_fini(cl);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Cbytearray

/* Method 'Boolean' inherited */

/* Method 'Integer' inherited */

/* Method 'String' is alias for 'write */

/* Method 'List inherited */

/* Method 'Array' inheritied */

/* Method 'Carray' inherited */

CM_DECL(new)
{
    CM_ARGC_CHK(1);
    method_redirect(th, dst, OVM_STR_CONST_HASH(Cbytearray), 1, &argv[1]);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

/* Method 'copydeep' is alias for 'copy' */

/* Method 'at' inherited */

/* Method 'atput' inherited */

/* Method 'equal' inherited */

/* Method 'size' inherited */

/* Method 'slice' inherited */

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_CBYTEARRAY)  ovm_except_inv_value(th, recvr);
    ovm_obj_barray_t b = ovm_inst_barrayval_nochk(recvr);
    struct ovm_clist cl[1];
    ovm_clist_init(cl);
    ovm_clist_appendc(cl, _OVM_STR_CONST("#Cbytearray("));
    barray_write(cl, b->size, b->data);
    ovm_clist_append_char(cl, ')');
    str_new_clist(dst, cl);
    ovm_clist_fini(cl);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Slice

CM_DECL(Array)
{
    CM_ARGC_CHK(1);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);
    unsigned n = sl->size;

    ovm_inst_t work = ovm_stack_alloc(th, 4);
    
    array_newc(&work[-1], OVM_CL_ARRAY, n, 0);
    unsigned i;
    for (i = 0; i < n; ++i) {
        ovm_inst_assign(&work[-4], &argv[0]);
        ovm_int_newc(&work[-3], i);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(at), 2);
        ovm_inst_assign(&work[-4], &work[-1]);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(atput), 3);
    }

    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(List)
{
    CM_ARGC_CHK(1);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);
    unsigned n = sl->size;

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    ovm_inst_assign(&work[-3], &argv[0]);
    unsigned i;
    for (i = 0; i < n; ++i) {
        ovm_int_newc(&work[-2], i);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(at), 2);
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
    }

    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Slice)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, recvr);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]), len = ovm_inst_intval(th, &argv[2]);
    if (!slice(&idx, &len, sl->size))  ovm_except_idx_range2(th, recvr, &argv[1], &argv[2]);
    slice_new(dst, OVM_CL_SLICE, sl->base, idx, len);
}

CM_DECL(new)
{
    CM_ARGC_CHK(4);

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign(&work[-3], &argv[1]);
    ovm_inst_assign(&work[-2], &argv[2]);
    ovm_inst_assign(&work[-1], &argv[3]);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(Slice), 3);
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]);
    if (!slice1(&idx, sl->size))  ovm_except_idx_range(th, &argv[0], &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    ovm_inst_assign_obj(&work[-2], sl->underlying);
    ovm_int_newc(&work[-1], sl->ofs + idx);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(at), 2);
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]);
    if (!slice1(&idx, sl->size))  ovm_except_idx_range(th, &argv[0], &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 3);
    
    ovm_inst_assign_obj(&work[-3], sl->underlying);
    ovm_int_newc(&work[-2], sl->ofs + idx);
    ovm_inst_assign(&work[-1], &argv[2]);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(atput), 3);
}

CM_DECL(hash)
{
    CM_ARGC_CHK(1);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign_obj(&work[-1], sl->base);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(slice), 1);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(hash), 1);
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);

    ovm_int_newc(dst, ovm_inst_sliceval(th, &argv[0])->size);
}

CM_DECL(slice)
{
    CM_ARGC_CHK(1);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);
    
    ovm_inst_t work = ovm_stack_alloc(th, 3);
    ovm_inst_assign_obj(&work[-3], sl->underlying);
    ovm_int_newc(&work[-2], sl->ofs);
    ovm_int_newc(&work[-1], sl->size);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(slice), 3);
}

static void slice_write_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_slice_t sl, unsigned ldr_size, const char *ldr, unsigned trlr_size, const char *trlr)
{
    ovm_inst_t work = ovm_stack_alloc(th, 2);

    static const char sep[] = ", ";
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    unsigned size = ldr_size - 1 + trlr_size - 1 + 1;
    unsigned i;
    for (i = 0; i < sl->size; ++i) {
        if (i > 0)  size += sizeof(sep) - 1;
        ovm_inst_assign_obj(&work[-2], sl->base);
        ovm_int_newc(&work[-1], i);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(at), 2);
        ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(write), 1);
        size += ovm_inst_strval(th, &work[-2])->size - 1;
        list_newlc_concat(lc, list_new(&work[-2], &work[-2], 0));
    }

    str_joinc_size(dst, size, ldr_size, ldr, sizeof(sep), sep, trlr_size, trlr, ovm_inst_listval_nochk(dst));
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    slice_write_unsafe(th, &work[-1], sl, _OVM_STR_CONST("#Slice(["), _OVM_STR_CONST("])"));

    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Cslice

CM_DECL(Array)
{
}

CM_DECL(List)
{
}

CM_DECL(Cslice)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, recvr);
    ovm_intval_t idx = ovm_inst_intval(th, &argv[1]), len = ovm_inst_intval(th, &argv[2]);
    if (!slice(&idx, &len, sl->size))  ovm_except_idx_range2(th, recvr, &argv[1], &argv[2]);
    slice_new(dst, OVM_CL_CSLICE, sl->base, idx, len);
}

CM_DECL(new)
{
    CM_ARGC_CHK(4);

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign(&work[-3], &argv[1]);
    ovm_inst_assign(&work[-2], &argv[2]);
    ovm_inst_assign(&work[-1], &argv[3]);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(Cslice), 3);
}

/* Method 'at' inherited */

/* Method 'atput' inherited */

/* Method 'size' inherited */

/* Method 'hash' inherited */

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_obj_slice_t sl = ovm_inst_sliceval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    slice_write_unsafe(th, &work[-1], sl, _OVM_STR_CONST("#Cslice(["), _OVM_STR_CONST("])"));

    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Set

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, ovm_inst_setval(th, &argv[0])->cnt > 0);
}

/* Method 'Integer' is alias for 'size' */

/* Method 'String' is alias for 'write' */

CM_DECL(List)
{
    CM_ARGC_CHK(1);
    ovm_obj_set_t s = ovm_inst_setval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    ovm_obj_t *p;
    unsigned n;
    for (p = s->data, n = s->size; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li)) {
            list_newlc_concat(lc, list_new(&work[-2], li->item, 0));
        }
    }
    ovm_inst_assign(dst, &work[-1]);
}

static void set_to_array_unsafe(ovm_inst_t dst, ovm_obj_class_t cl, ovm_obj_set_t s)
{
    ovm_obj_array_t a = array_newc(dst, cl, s->size, 0);
    ovm_obj_t *p;
    unsigned n;
    ovm_inst_t q;
    for (p = s->data, n = s->size, q = a->data; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li)) {
            ovm_inst_assign(q, li->item);
            ++q;
        }       
    }
}

CM_DECL(Array)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    set_to_array_unsafe(&work[-1], OVM_CL_ARRAY, ovm_inst_setval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Carray)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    set_to_array_unsafe(&work[-1], OVM_CL_CARRAY, ovm_inst_setval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Set)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(Cset)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    set_copy_unsafe(th, &work[-1], OVM_CL_CSET, ovm_inst_setval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(new)
{
    if (argc == 1) {
        set_newc(dst, OVM_CL_SET, class_default_size(th, OVM_CL_SET, 16));
        return;
    }
    if (argc == 2) {
        ovm_inst_t arg = &argv[1];
        ovm_obj_class_t cl = ovm_inst_of_raw(arg);
        
        if (cl == OVM_CL_INTEGER) {
            ovm_intval_t i = arg->intval;
            if (i < 1)  ovm_except_inv_value(th, arg);
            set_newc(dst, OVM_CL_SET, i);
            return;
        }
        if (cl == OVM_CL_SET) {
            ovm_inst_t work = ovm_stack_alloc(th, 1);
            
            set_copy_unsafe(th, &work[-1], OVM_CL_SET, ovm_inst_setval_nochk(arg));
            ovm_inst_assign(dst, &work[-1]);

            return;
        }

        method_redirect(th, dst, OVM_STR_CONST_HASH(Set), 1, arg);
        return;
    }

    ovm_except_num_args_range(th, 1, 2);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_obj_set_t s = ovm_inst_setval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    set_copy_unsafe(th, &work[-1], ovm_obj_inst_of_raw(s->base), s);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(copydeep)
{
    CM_ARGC_CHK(1);
    ovm_obj_set_t s = ovm_inst_setval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    set_copydeep_unsafe(th, &work[-1], ovm_obj_inst_of_raw(s->base), s);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_bool_newc(dst, set_at(th, ovm_inst_setval(th, &argv[0]), &argv[1]));
}

CM_DECL(del)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_SET)  ovm_except_inv_value(th, recvr);
    ovm_inst_t arg = &argv[1];
    set_del(th, ovm_inst_setval_nochk(recvr), arg);
    ovm_inst_assign(dst, arg);
}

CM_DECL(delall)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_SET)  ovm_except_inv_value(th, recvr);
    set_clear(ovm_inst_setval_nochk(recvr));
    ovm_inst_assign(dst, recvr);
}

CM_DECL(put)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    if (ovm_inst_of_raw(recvr) != OVM_CL_SET)  ovm_except_inv_value(th, recvr);
    set_put(th, ovm_inst_setval_nochk(recvr), arg);
    ovm_inst_assign(dst, arg);
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ovm_inst_setval(th, &argv[0])->cnt);
}

CM_DECL(tablesize)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ovm_inst_setval(th, &argv[0])->size);
}

static void set_write_unsafe(ovm_thread_t th, ovm_inst_t dst, unsigned ldr_size, const char *ldr, unsigned trlr_size, const char *trlr, ovm_obj_set_t s)
{
    obj_lock_loop_chk(th, s->base);

    unsigned size = ldr_size + trlr_size - 1;

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign_obj(dst, 0);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    ovm_obj_t *p;
    unsigned n;
    static const char sep[] = ", ";
    bool f = false;
    for (p = s->data, n = s->size; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li), f = true) {
            if (f)  size += sizeof(sep) - 1;
            ovm_inst_assign(th->sp, li->item);
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 1);
            size += ovm_inst_strval(th, &work[-1])->size - 1;
            list_newlc_concat(lc, list_new(&work[-1], &work[-1], 0));
        }
    }

    str_joinc_size(dst, size, ldr_size, ldr, sizeof(sep), sep, trlr_size, trlr, ovm_inst_listval_nochk(dst));

    ovm_stack_unwind(th, work);
    
    obj_unlock(s->base);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    set_write_unsafe(th, &work[-1], _OVM_STR_CONST("{"), _OVM_STR_CONST("}"), ovm_inst_setval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Cset

/* Method 'Boolean' inherited */

/* Method 'String' alias for 'write' */

/* Method 'List' inherited */

/* Method 'Array' inherited */

/* Method 'Carray' inherited */

CM_DECL(Set)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    set_copy_unsafe(th, &work[-1], OVM_CL_SET, ovm_inst_setval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

/* Method 'Cset' alias for 'copy' */

CM_DECL(new)
{
    CM_ARGC_CHK(2);
    method_redirect(th, dst, OVM_STR_CONST_HASH(Cset), 1, &argv[1]);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

/* Method 'copydeep' inherited */

/* Method 'at' inherited */

/* Method 'put' inherited */

/* Method 'size' inherited */
        
/* Method 'tablesize' inherited */
        
CM_DECL(write)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    set_write_unsafe(th, &work[-1], _OVM_STR_CONST("#Cset(("), _OVM_STR_CONST("))"), ovm_inst_setval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Dictionary

CM_DECL(Boolean)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, ovm_inst_dictval(th, &argv[0])->size > 0);
}

/* Method 'Integer' alias for 'size' */

/* Method 'String' alias for 'write' */

CM_DECL(List)
{
    CM_ARGC_CHK(1);
    ovm_obj_set_t s = ovm_inst_dictval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, &work[-1]);
    ovm_obj_t *p;
    unsigned n;
    for (p = s->data, n = s->size; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li)) {
            list_newlc_concat(lc, list_new(&work[-2], li->item, 0));
        }
    }
    ovm_inst_assign(dst, &work[-1]);
}

static void dict_to_array_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl, ovm_inst_t arg)
{
    ovm_obj_set_t s = ovm_inst_dictval(th, arg);
    ovm_obj_array_t a = array_newc(dst, cl, s->cnt, 0);
    ovm_obj_t *p;
    unsigned n;
    ovm_inst_t q;
    for (p = s->data, n = s->size, q = a->data; n > 0; --n, ++p, ++q) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li)) {
            ovm_inst_assign(q, li->item);
        }
    }
}

CM_DECL(Array)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    dict_to_array_unsafe(th, &work[-1], OVM_CL_ARRAY, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Carray)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    dict_to_array_unsafe(th, &work[-1], OVM_CL_CARRAY, &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(Dictionary)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(Cdictionary)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    set_copy_unsafe(th, &work[-1], OVM_CL_CDICTIONARY, ovm_inst_dictval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(new)
{
    if (argc == 1) {
        set_newc(dst, OVM_CL_DICTIONARY, class_default_size(th, OVM_CL_DICTIONARY, 16));
        return;
    }
    if (argc == 2) {
        ovm_inst_t arg = &argv[1];
        ovm_obj_class_t cl = ovm_inst_of_raw(arg);
        if (cl == OVM_CL_INTEGER) {
            ovm_intval_t i = arg->intval;
            if (i < 1)  ovm_except_inv_value(th, arg);
            set_newc(dst, OVM_CL_DICTIONARY, i);
            return;
        }
        if (cl == OVM_CL_DICTIONARY) {
            ovm_inst_t work = ovm_stack_alloc(th, 1);
            
            set_copy_unsafe(th, &work[-1], OVM_CL_DICTIONARY, ovm_inst_setval_nochk(arg));
            ovm_inst_assign(dst, &work[-1]);

            return;
        }

        method_redirect(th, dst, OVM_STR_CONST_HASH(Dictionary), 1, arg);
        return;
    }

    ovm_except_num_args_range(th, 1, 2);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_obj_set_t d = ovm_inst_dictval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    set_copy_unsafe(th, &work[-1], ovm_obj_inst_of_raw(d->base), d);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(copydeep)
{
    CM_ARGC_CHK(1);
    ovm_obj_set_t d = ovm_inst_dictval(th, &argv[0]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    set_copydeep_unsafe(th, &work[-1], ovm_obj_inst_of_raw(d->base), d);
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    if  (!dict_at(th, dst, ovm_inst_dictval(th, &argv[0]), &argv[1])) {
        ovm_inst_assign_obj(dst, 0);
    }
}

CM_DECL(ate)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    if  (dict_at(th, dst, ovm_inst_dictval(th, recvr), arg)) {
        ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
        return;
    }
    ovm_except_key_not_found(th, recvr, arg);
}

CM_DECL(atdefault)
{
    CM_ARGC_CHK(3);
    if  (dict_at(th, dst, ovm_inst_dictval(th, &argv[0]), &argv[1])) {
        ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
        return;
    }
    ovm_inst_assign(dst, &argv[2]);
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_inst_t val = &argv[2];
    dict_at_put(th, ovm_inst_dictval(th, &argv[0]), &argv[1], val);
    ovm_inst_assign(dst, val);
}

CM_DECL(atputnew)
{
    CM_ARGC_CHK(3);
    ovm_obj_set_t d = ovm_inst_dictval(th, &argv[0]);
    ovm_inst_t key = &argv[1], val = &argv[2];

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    if  (!dict_at(th, &work[-1], d, key)) {
        dict_at_put(th, d, key, val);
    }
            
    ovm_inst_assign(dst, val);
}

CM_DECL(del)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    if (ovm_inst_of_raw(recvr) != OVM_CL_DICTIONARY)  ovm_except_inv_value(th, recvr);
    dict_del(th, ovm_inst_setval_nochk(recvr), arg);
    ovm_inst_assign(dst, arg);
}

CM_DECL(delall)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_DICTIONARY)  ovm_except_inv_value(th, recvr);
    set_clear(ovm_inst_setval_nochk(recvr));
    ovm_inst_assign(dst, recvr);
}

CM_DECL(put)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0];
    if (ovm_inst_of_raw(recvr) != OVM_CL_DICTIONARY)  ovm_except_inv_value(th, recvr);
    ovm_obj_pair_t pr = ovm_inst_pairval(th, &argv[1]);
    dict_at_put(th, ovm_inst_setval_nochk(recvr), pr->first, pr->second);
    ovm_inst_assign(dst, &argv[1]);
}

CM_DECL(size)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ovm_inst_dictval(th, &argv[0])->cnt);
}

CM_DECL(tablesize)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ovm_inst_dictval(th, &argv[0])->size);
}

static void dict_write_unsafe(ovm_thread_t th, ovm_inst_t dst, unsigned ldr_size, const char *ldr, unsigned trlr_size, const char *trlr, ovm_inst_t arg)
{
    ovm_obj_set_t d = ovm_inst_dictval(th, arg);

    obj_lock_loop_chk(th, d->base);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    struct ovm_str_newv_item a[3];
    static const char sep[] = ", ", sep2[] = ": ";
    a[1].size = sizeof(sep2);
    a[1].data = sep2;
    ovm_inst_assign_obj(dst, 0);
    struct list_newlc_ctxt lc[1];
    list_newlc_init(lc, dst);
    unsigned size = ldr_size + trlr_size - 1;
    ovm_obj_t *p;
    unsigned n;
    bool f = false;
    for (p = d->data, n = d->size; n > 0; --n, ++p) {
        ovm_obj_list_t li;
        for (li = ovm_obj_list(*p); li != 0; li = ovm_list_next(li), f = true) {
            if (f)  size += sizeof(sep) - 1;
            ovm_obj_pair_t pr = ovm_inst_pairval_nochk(li->item);
            ovm_inst_assign(th->sp, pr->first);
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 1);
            ovm_obj_str_t s1 = ovm_inst_strval(th, &work[-1]);
            ovm_inst_assign(th->sp, pr->second);
            ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(write), 1);
            ovm_obj_str_t s2 = ovm_inst_strval(th, &work[-2]);
            a[0].size = s1->size;
            a[0].data = s1->data;
            a[2].size = s2->size;
            a[2].data = s2->data;
            ovm_obj_str_t s = str_newv_size(&work[-1], s1->size - 1 + 2 + s2->size - 1 + 1, ARRAY_SIZE(a), a);
            size += s->size - 1;
            list_newlc_concat(lc, list_new(&work[-1], &work[-1], 0));
        }
    }
    
    str_joinc_size(dst, size, ldr_size, ldr, sizeof(sep), sep, trlr_size, trlr, ovm_inst_listval_nochk(dst));

    ovm_stack_unwind(th, work);
    
    obj_unlock(d->base);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    dict_write_unsafe(th, &work[-1], _OVM_STR_CONST("{"), _OVM_STR_CONST("}"), &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Cdictionary

/* Method 'Boolean' inherited */

/* Method 'Integer' inherited */

/* Method 'List' inherited */

/* Method 'Array' inherited */

/* Method 'Carray' inherited */

CM_DECL(Dictionary)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    set_copy_unsafe(th, &work[-1], OVM_CL_DICTIONARY, ovm_inst_dictval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(new)
{
    CM_ARGC_CHK(1);
    method_redirect(th, dst, OVM_STR_CONST_HASH(Cdictionary), 1, &argv[1]);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(copydeep)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    set_copydeep_unsafe(th, &work[-1], OVM_CL_CDICTIONARY, ovm_inst_dictval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

/* Method 'at' inherited */

/* Method 'atput' inherited */

/* Method 'del' inherited */

/* Method 'delall' inherited */

/* Method 'size' inherited */

/* Method 'tablesize' inherited */

CM_DECL(write)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    dict_write_unsafe(th, &work[-1], _OVM_STR_CONST("#Cdictionary.new({"), _OVM_STR_CONST("})"), &argv[0]);
    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Namespace

/* Method 'String' alias for 'write' */

CM_DECL(Dictionary)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_NAMESPACE))  ovm_except_inv_value(th, recvr);
    ovm_inst_assign_obj(dst, ovm_inst_nsval_nochk(recvr)->dict);
}

CM_DECL(new)
{
    CM_ARGC_CHK(3);
    ovm_inst_t arg = &argv[1];
    ovm_obj_str_t name = ovm_inst_strval(th, arg);
    str_inst_hash(arg);
    ovm_obj_ns_t parent = ovm_inst_nsval(th, &argv[2]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ns_new(th, dst, name, arg->hash, set_newc(&work[-1], OVM_CL_DICTIONARY, 32), parent);
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_NAMESPACE))  ovm_except_inv_value(th, recvr);
    ovm_inst_t k = &argv[1];
    ovm_obj_str_t s = ovm_inst_strval(th, k);
    str_inst_hash(k);
    if (!ns_ats(dst, ovm_inst_nsval_nochk(recvr), s->size, s->data, k->hash)) {
        ovm_inst_assign_obj(dst, 0);
    }
}

CM_DECL(ate)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_NAMESPACE))  ovm_except_inv_value(th, recvr);
    ovm_inst_t k = &argv[1];
    ovm_obj_str_t s = ovm_inst_strval(th, k);
    str_inst_hash(k);
    if (!ns_ats(dst, ovm_inst_nsval_nochk(recvr), s->size, s->data, k->hash)) {
        ovm_except_no_var(th, k);
    }
    ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_NAMESPACE))  ovm_except_inv_value(th, recvr);
    ovm_inst_t k = &argv[1], val = &argv[2];
    ovm_obj_str_t s = ovm_inst_strval(th, k);
    str_inst_hash(k);
    ns_ats_put(th, ovm_inst_nsval_nochk(recvr), s->size, s->data, k->hash, val);
    ovm_inst_assign(dst, val);
}

CM_DECL(current)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ns_up(th, 1)->base);
}

CM_DECL(name)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_nsval(th, &argv[0])->name);
}

CM_DECL(parent)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_nsval(th, &argv[0])->parent);
}

static ovm_obj_str_t ns_write_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_ns_t ns)
{
    ovm_obj_str_t result;

    if (ns == ovm_obj_ns(ns_main)) {
        result = ovm_obj_str(ns->name);
        ovm_inst_assign_obj(dst, result->base);
    } else {
        ovm_inst_assign_obj(dst, 0);

        ovm_inst_t work = ovm_stack_alloc(th, 1);
    
        unsigned size = 1;
        bool f = false;
        for (; ns != 0; ns = ovm_obj_ns(ns->parent), f = true) {
            if (ns == ovm_obj_ns(ns_main))  break;
            if (f)  ++size;
            ovm_obj_str_t s = ovm_obj_str(ns->name);
            size += s->size - 1;
            ovm_inst_assign_obj(&work[-1], s->base);
            list_new(dst, &work[-1], ovm_inst_listval_nochk(dst));
        }
        result = str_joinc_size(dst, size, 0, 0, _OVM_STR_CONST("."), 0, 0, ovm_inst_listval_nochk(dst));

        ovm_stack_unwind(th, work);
    }

    return (result);
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    if (!ovm_is_subclass_of(ovm_inst_of_raw(recvr), OVM_CL_NAMESPACE))  ovm_except_inv_value(th, recvr);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ns_write_unsafe(th, &work[-1], ovm_inst_nsval_nochk(recvr));
    ovm_inst_assign(dst, &work[-1]);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Module

/* Method 'String' inherited */

/* Method 'Dictionary' inherited */

static bool module_file_chk(ovm_inst_t dst, unsigned path_size, const char *path, unsigned name_size, const char *name, ovm_inst_t sha1)
{
    static const char prefix[] = "/liboovm", suffix[] = ".so";
    struct ovm_str_newv_item a[] = {
        { path_size, path },
        { sizeof(prefix), prefix },
        { name_size, name },
        { sizeof(suffix), suffix }
    };
    ovm_obj_str_t s = str_newv_size(dst, path_size - 1 + sizeof(prefix) - 1 + name_size - 1 + sizeof(suffix), ARRAY_SIZE(a), a);
    if (access(s->data, R_OK) != 0)  return (false);
    static const char sha1_cmd[] = "/usr/bin/sha1sum";
    unsigned cmd_bufsize = sizeof(sha1_cmd) - 1 + 1 + s->size;
    char cmd_buf[cmd_bufsize];
    snprintf(cmd_buf, cmd_bufsize, "%s %s", sha1_cmd, s->data);
    FILE *fp = popen(cmd_buf, "r");
    if (fp == 0)  return (false);
    char sha1_buf[41];
    fgets(sha1_buf, sizeof(sha1_buf), fp);
    sha1_buf[sizeof(sha1_buf) - 1] = 0;
    pclose(fp);
    str_newc(sha1, sizeof(sha1_buf), sha1_buf);
    
    return (true);
}

static bool module_file_chk_path_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_str_t modname, ovm_inst_t sha1)
{
    bool result = false;
    
    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    if (class_ats(&work[-1], OVM_CL_MODULE, OVM_STR_CONST_HASH(path))
        && ovm_inst_of_raw(&work[-1]) == OVM_CL_LIST
        ) {
        ovm_obj_list_t li;
        for (li = ovm_inst_listval_nochk(&work[-1]); li != 0; li = ovm_list_next(li)) {
            if (ovm_inst_of_raw(li->item) != OVM_CL_STRING)  continue;
            ovm_obj_str_t s = ovm_inst_strval_nochk(li->item);
            if (module_file_chk(dst, s->size, s->data, modname->size, modname->data, sha1)) {
                result = true;
                break;
            }
        }
    } else {
        result = module_file_chk(dst, _OVM_STR_CONST("."), modname->size, modname->data, sha1);
    }

    ovm_stack_unwind(th, work);

    return (result);
}

static pthread_mutex_t module_mutex[1];
static pthread_mutexattr_t module_mutex_attr[1];

static void module_mutex_init(void)
{
  pthread_mutexattr_init(module_mutex_attr);
  pthread_mutexattr_settype(module_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(module_mutex, module_mutex_attr);
}

static ovm_obj_module_t module_new_and_init(ovm_thread_t th, ovm_inst_t dst, ovm_obj_str_t modname, unsigned modname_hash, ovm_obj_str_t filename, ovm_obj_str_t sha1, void *dlhdl, ovm_obj_ns_t parent, ovm_inst_t init_func, ovm_obj_set_t modules_loaded)
{
    ovm_obj_module_t m = module_new(th, dst, modname, modname_hash, filename, sha1, dlhdl, parent);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign(&work[-1], dst);

    method_run(th, &work[-1], ovm_inst_nsval_nochk(dst), 0, init_func, 1, &th->sp[0]);
    
    ovm_stack_unwind(th, work);
    
    dict_ats_put(th, modules_loaded, modname->size, modname->data, modname_hash, dst);

    return (m);
}

static ovm_obj_set_t modules_loaded_dict(ovm_inst_t dst)
{
    if (!class_ats(dst, OVM_CL_MODULE, OVM_STR_CONST_HASH(loaded))
        || ovm_inst_of_raw(dst) != OVM_CL_DICTIONARY
        ) {
        fprintf(stderr, "Missing loaded modules dictionary, system corrupted\n");
        abort();
    }
    
    return (ovm_inst_setval_nochk(dst));
}

static void *module_func(void *dlhdl, ovm_obj_str_t modname, char *funcname, unsigned mesg_bufsize, char *mesg)
{
    unsigned sym_bufsize = modname->size + strlen(funcname) + 5;
    char sym[sym_bufsize];
    snprintf(sym, sym_bufsize, "__%s_%s__", modname->data, funcname);
    void *result = dlsym(dlhdl, sym);
    if (result == 0 && mesg != 0)  snprintf(mesg, mesg_bufsize, "cannot find module function %s", sym);
    
    return (result);
}

static bool module_load_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_str_t modname, unsigned modname_hash, ovm_obj_str_t filename, ovm_obj_str_t sha1, ovm_obj_ns_t parent, unsigned mesg_bufsize, char *mesg)
{
    bool result = false;

    pthread_mutex_lock(module_mutex);
    
    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    /* Check if module already loaded; if yes, create a namespace instead */

    ovm_obj_set_t modules_loaded = modules_loaded_dict(&work[-1]);
    
    if (dict_ats(&work[-2], modules_loaded, modname->size, modname->data, modname_hash)) {
        /* Module already loaded */

        do {
            ovm_obj_module_t m = ovm_inst_moduleval_nochk(ovm_inst_pairval_nochk(&work[-2])->second);
            if (strcmp(sha1->data, ovm_obj_str(m->sha1)->data) != 0) {
                snprintf(mesg, mesg_bufsize, "SHA1 conflict");
                break;
            }

            module_clone(th, dst, m, parent);
            
            result = true;
        } while (0);
    } else {
        /* Module not already loaded */
        
        do {
            void *dlhdl = dlopen(filename->data, RTLD_NOW);
            if (dlhdl == 0) {
                snprintf(mesg, mesg_bufsize, "load failed, %s", dlerror());
                break;
            }
	    
	    void *init_func = 0;
	    do {
		init_func = module_func(dlhdl, modname, "code", mesg_bufsize, mesg);
		if (init_func != 0) {
		    method_newc(&work[-2], (ovm_method_t) init_func);
		    break;
		}
		init_func = module_func(dlhdl, modname, "init", mesg_bufsize, mesg);
		if (init_func != 0) {
		    ovm_codemethod_newc(&work[-2], (ovm_codemethod_t) init_func);
		    break;
		}
	    } while (0);
	    if (init_func == 0)  break;
            module_new_and_init(th, dst, modname, modname_hash, filename, sha1, dlhdl, parent, &work[-2], modules_loaded);      
            
            result = true;
        } while (0);
    }

    ovm_stack_unwind(th, work);

    pthread_mutex_unlock(module_mutex);
    
    return (result);
}

CM_DECL(new)
{
    CM_ARGC_CHK(2);
    ovm_inst_t arg = &argv[1];
    ovm_obj_str_t name = ovm_inst_strval(th, arg);
    str_inst_hash(arg);
    ovm_obj_ns_t parent = ns_up(th, 1);

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    if (!module_file_chk_path_unsafe(th, &work[-2], name, &work[-3])) {
        ovm_except_module_load(th, arg, _OVM_STR_CONST("module not found"));
    }

    char mesg[132];
    
    if (!module_load_unsafe(th, &work[-1], name, arg->hash, ovm_inst_strval_nochk(&work[-2]), ovm_inst_strval_nochk(&work[-3]), parent, sizeof(mesg), mesg)) {
        ovm_except_module_load(th, arg, strlen(mesg) + 1, mesg);
    }

    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(current)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, module_cur(ns_up(th, 1))->base->base);
}

/* Method 'at' inherited */

/* Method 'atput' inherited */

CM_DECL(filename)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_moduleval(th, &argv[0])->filename);
}

CM_DECL(sha1)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_moduleval(th, &argv[0])->sha1);
}

/* Method 'write' inherited */

static void module_cl_init(ovm_thread_t th, ovm_obj_class_t cl)
{
    module_mutex_init();

    const char *modpath = getenv("OVM_MODULE_PATH");
    
    ovm_inst_t work = ovm_stack_alloc(th, 1);
        
    if (modpath != 0) {
        str_splitc_unsafe(th, &work[-1], strlen(modpath) + 1, modpath, _OVM_STR_CONST(":"));
        class_ats_put(th, cl, OVM_STR_CONST_HASH(path), &work[-1]);
    }

    set_newc(&work[-1], OVM_CL_DICTIONARY, 16);
    class_ats_put(th, cl, OVM_STR_CONST_HASH(loaded), &work[-1]);
    
    ovm_stack_unwind(th, work);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  File

/* Method 'Boolean' alias for 'eof' */

/* Method 'Integer' alias for 'tell' */

CM_DECL(new)
{
    CM_ARGC_CHK(3);
    ovm_obj_str_t filename = ovm_inst_strval(th, &argv[1]);
    ovm_obj_str_t mode = ovm_inst_strval(th, &argv[2]);
    FILE *fp = fopen(filename->data, mode->data);
    if (fp == 0) {
        ovm_thread_errno_set(th);
        ovm_except_file_open(th, &argv[1], &argv[2]);
    }
    file_new(dst, filename, mode, fp);
}

CM_DECL(copy)
{
    CM_ARGC_CHK(1);
    file_copy(dst, ovm_inst_fileval(th, &argv[0]));
}

/* Method 'copydeep' alias for 'copy */

CM_DECL(eof)
{
    CM_ARGC_CHK(1);
    ovm_bool_newc(dst, feof(ovm_inst_fileval(th, &argv[0])->fp) != 0);
}

CM_DECL(flush)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];
    fflush(ovm_inst_fileval(th, recvr)->fp);
    ovm_inst_assign(dst, recvr);
}

CM_DECL(filename)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_fileval(th, &argv[0])->filename);
}

CM_DECL(mode)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_fileval(th, &argv[0])->mode);
}

CM_DECL(read)
{
    CM_ARGC_CHK(2);
    ovm_obj_file_t f = ovm_inst_fileval(th, &argv[0]);
    unsigned n = ovm_inst_intval(th, &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_barray_t b = barray_newc(&work[-1], OVM_CL_BYTEARRAY, n + 1, 0);
    int nn = fread(b->data, 1, n, f->fp);
    if (nn <= 0) {
        if (ferror(f->fp)) {
            ovm_int_newc(dst, -1);      
        } else {
            str_newc(dst, 1, "");
        } 
    } else {
        b->data[nn] = 0;
        str_newc(dst, nn + 1, (const char *) b->data);
    }
}

CM_DECL(readb)
{
    CM_ARGC_CHK(2);
    ovm_obj_file_t f = ovm_inst_fileval(th, &argv[0]);
    unsigned n = ovm_inst_intval(th, &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_obj_barray_t b = barray_newc(&work[-1], OVM_CL_BYTEARRAY, n, 0);
    int nn = fread(b->data, 1, n, f->fp);
    if (nn <= 0) {
        if (ferror(f->fp)) {
            ovm_int_newc(dst, -1);      
        } else {
            barray_newc(dst, OVM_CL_BYTEARRAY, 0, 0);
        } 
    } else if (nn < n) {
        barray_newc(dst, OVM_CL_BYTEARRAY, nn, b->data);
    } else if (nn == n) {
        ovm_inst_assign(dst, &work[-1]);
    }
}

CM_DECL(readln)
{   
    CM_ARGC_RANGE_CHK(1, 2);
    ovm_obj_file_t f = ovm_inst_fileval(th, &argv[0]);
    unsigned n = 0;
    bool unlimitedf = false;
    if (argc == 2) {
        n = ovm_inst_intval(th, &argv[1]);
        if (n == 0)  unlimitedf = true;
    } else {
        unlimitedf = true;
    }
    struct ovm_clist cl[1];
    ovm_clist_init(cl);
    while (unlimitedf || n > 0) {
        int c = getc(f->fp);
        if (c < 0) {
            if (feof(f->fp))  break;
            ovm_int_newc(dst, -1);
            goto done;
        }
        ovm_clist_append_char(cl, c);
        if (c == '\n')  break;
        if (n > 0)  --n;
    }
    str_new_clist(dst, cl);

 done:
    ovm_clist_fini(cl);
}

CM_DECL(tell)
{
    CM_ARGC_CHK(1);
    ovm_int_newc(dst, ftell(ovm_inst_fileval(th, &argv[0])->fp));
}

CM_DECL(unread)
{
    CM_ARGC_CHK(2);
    ovm_obj_file_t f = ovm_inst_fileval(th, &argv[0]);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[1]);
    if (s->size != 2)  ovm_except_inv_value(th, &argv[1]);
    ungetc(s->data[0], f->fp);
}

CM_DECL(write)
{
    if (argc == 1) {
        ovm_obj_file_t f = ovm_inst_fileval(th, &argv[0]);
        static const char s1[] = "{filename: ", s2[] = ", mode: ", s3[] = ", ofs: ", s4[] = ", eof: ", s5[] = "}";
        struct ovm_str_newv_item a[10];
        unsigned size = 1;

        ovm_inst_t work = ovm_stack_alloc(th, 1);
        
        ovm_obj_str_t s = obj_write_unsafe(&work[-1], f->base);
        a[0].size = s->size;
        a[0].data = s->data;
        size += s->size - 1;
        a[1].size = sizeof(s1);
        a[1].data = s1;
        size += sizeof(s1) - 1;
        s = ovm_obj_str(f->filename);
        a[2].size = s->size;
        a[2].data = s->data;
        size += s->size - 1;
        a[3].size = sizeof(s2);
        a[3].data = s2;
        size += sizeof(s2) - 1;
        s = ovm_obj_str(f->mode);
        a[4].size = s->size;
        a[4].data = s->data;
        size += s->size - 1;
        a[5].size = sizeof(s3);
        a[5].data = s3;
        size += sizeof(s3) - 1;
        char ofs_buf[32];
        snprintf(ofs_buf, sizeof(ofs_buf), "%ld", ftell(f->fp));
        unsigned n = strlen(ofs_buf);
        a[6].size = n + 1;
        a[6].data = ofs_buf;
        size += n;
        a[7].size = sizeof(s4);
        a[7].data = s4;
        size += sizeof(s4) - 1;
        a[8].data = bool_to_str(feof(f->fp), &a[8].size);
        size += a[8].size - 1;
        a[9].size = sizeof(s5);
        a[9].data = s5;
        size += sizeof(s5) - 1;

        str_newv_size(dst, size, ARRAY_SIZE(a), a);

        return;
    }
    if (argc == 2) {
        ovm_obj_file_t f = ovm_inst_fileval(th, &argv[0]);
        ovm_inst_t arg = &argv[1];
        ovm_obj_class_t cl = ovm_inst_of_raw(arg);
        const char *p;
        unsigned n;
        if (cl == OVM_CL_STRING) {
            ovm_obj_str_t s = ovm_inst_strval_nochk(arg);
            n = s->size - 1;
            p = s->data;
        } else if (cl == OVM_CL_BYTEARRAY) {
            ovm_obj_barray_t b = ovm_inst_barrayval_nochk(arg);
            n = b->size;
            p = (const char *) b->data;
        } else {
            ovm_except_inv_value(th, arg);
        }
        ovm_int_newc(dst, fwrite(p, 1, n, f->fp));

        return;
    }

    ovm_except_num_args_range(th, 1, 2);        
}

CM_DECL(writeln)
{
    CM_ARGC_CHK(2);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_inst_assign(&work[-2], &argv[0]);
    ovm_inst_assign(&work[-1], &argv[1]);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 2);
    str_newc(&work[-1], _OVM_STR_CONST("\n"));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 2);
}

static void file_cl_init(ovm_thread_t th, ovm_obj_class_t cl)
{
    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_obj_str_t s1 = str_newc(&work[-3], _OVM_STR_CONST("stdin"));
    ovm_obj_str_t s2 = str_newc(&work[-2], OVM_STR_CONST(r));
    file_new(&work[-1], s1, s2, stdin);
    class_ats_put(th, cl, OVM_STR_CONST_HASH(stdin), &work[-1]);
    s1 = str_newc(&work[-3],  _OVM_STR_CONST("stdout"));
    s2 = str_newc(&work[-2], OVM_STR_CONST(w));
    file_new(&work[-1], s1, s2, stdout);
    class_ats_put(th, cl, OVM_STR_CONST_HASH(stdout), &work[-1]);
    s1 = str_newc(&work[-3],  _OVM_STR_CONST("stderr"));
    file_new(&work[-1], s1, s2, stderr);
    class_ats_put(th, cl, OVM_STR_CONST_HASH(stderr), &work[-1]);

    ovm_stack_unwind(th, work);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Exception

CM_DECL(new)
{
    CM_ARGC_CHK(2);
    ovm_obj_str_t s = ovm_inst_strval(th, &argv[1]);
    except_newc(th, dst, s->size, s->data);
}

CM_DECL(raise)
{
    CM_ARGC_CHK(1);
    ovm_inst_t recvr = &argv[0];

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    ovm_inst_of(&work[-1], recvr);
    if (ovm_inst_classval_nochk(&work[-1]) != OVM_CL_EXCEPTION)  ovm_except_inv_value(th, recvr);
    except_raise1(th);
    except_raise2(th, recvr, th->mcfp->prev->method);
}

CM_DECL(reraise)
{
    CM_ARGC_CHK(1);
    ovm_except_reraise(th);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  System

CM_DECL(exit)
{
    CM_ARGC_CHK(1);
    exit(ovm_inst_intval(th, &argv[0]));
}

CM_DECL(abort)
{
    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    FILE *fp;
    if (class_ats(&work[-1], OVM_CL_FILE, _OVM_STR_CONST_HASH("stderr"))
        && ovm_inst_of_raw(&work[-1]) == OVM_CL_FILE
        ) {
        fp = ovm_inst_fileval_nochk(&work[-1])->fp;
    } else {
        fp = stderr;
    }

    fputs(ovm_inst_strval(th, &argv[1])->data, fp);

    OVM_THREAD_FATAL(th, OVM_THREAD_FATAL_ABORTED, 0);
}

CM_DECL(assert)
{
    ovm_inst_t f = &argv[1];
    if (!ovm_inst_boolval(th, f)) {
        ovm_stack_alloc(th, 2);

        ovm_inst_assign(th->sp, &argv[0]);
        ovm_inst_assign(&th->sp[1], &argv[2]);
        ovm_method_callsch(th, &th->sp[1], OVM_STR_CONST_HASH(abort), 2);
    }
    ovm_inst_assign(dst, f);
}

#ifndef NDEBUG

CM_DECL(collect)
{
    _ovm_objs_lock();

    collect();

    _ovm_objs_unlock();
}

#endif

/***************************************************************************/

void ovm_environ_atc(ovm_thread_t th, ovm_inst_t dst, unsigned nm_size, const char *nm, unsigned hash)
{
    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    ovm_inst_assign_obj(&work[-2], ovm_consts.Environment);
    ovm_str_newch(&work[-1], nm_size, nm, hash);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(ate), 2);
    
    ovm_stack_unwind(th, work);
}

void ovm_environ_atc_push(ovm_thread_t th, unsigned nm_size, const char *nm, unsigned hash)
{
    ovm_inst_t work = ovm_stack_alloc(th, 3);
    
    ovm_inst_assign_obj(&work[-3], ovm_consts.Environment);
    ovm_str_newch(&work[-2], nm_size, nm, hash);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    
    ovm_stack_free(th, 2);
}

void ovm_environ_atcput(ovm_thread_t th, unsigned nm_size, const char *nm, unsigned hash, ovm_inst_t val)
{
    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign_obj(&work[-3], ovm_consts.Environment);
    ovm_str_newch(&work[-2], nm_size, nm, hash);
    ovm_inst_assign(&work[-2], val);
    ovm_method_callsch(th, &th->sp[2], OVM_STR_CONST_HASH(atput), 3);

    ovm_stack_unwind(th, work);
}

#undef  METHOD_CLASS
#define METHOD_CLASS  Environment

static bool environ_at(ovm_thread_t th, ovm_inst_t dst, ovm_inst_t nm)
{
    ovm_obj_str_t s = ovm_inst_strval(th, nm);
    str_inst_hash(nm);

    ovm_obj_ns_t ns = ns_up(th, 1), module_ns = module_cur(ns)->base;

    return (ns_ats(dst, ns, s->size, s->data, nm->hash)
            || ((module_ns != ns) && ns_ats(dst, module_ns, s->size, s->data, nm->hash))
            || ns_ats(dst, ovm_obj_ns(ns_main), s->size, s->data, nm->hash)
            );
}

CM_DECL(at)
{
    CM_ARGC_CHK(2);
    if (!environ_at(th, dst, &argv[1]))  ovm_inst_assign_obj(dst, 0);
}

CM_DECL(ate)
{
    CM_ARGC_CHK(2);
    if (!environ_at(th, dst, &argv[1]))  ovm_except_no_var(th, &argv[1]);
    ovm_inst_assign(dst, ovm_inst_pairval_nochk(dst)->second);
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_inst_t arg = &argv[1], val = &argv[2];
    ovm_obj_str_t s = ovm_inst_strval(th, arg);
    str_inst_hash(arg);
    ns_ats_put(th, ns_up(th, 1), s->size, s->data, arg->hash, val);
    ovm_inst_assign(dst, val);
}

/***************************************************************************/

#undef  METHOD_CLASS
#define METHOD_CLASS  Metaclass

/* Method 'String' alias for 'write' */

CM_DECL(new)
{
    if (argc < 3 || argc > 4)  ovm_except_num_args_range(th, 3, 4);
    ovm_obj_str_t nm = ovm_inst_strval(th, &argv[1]);
    ovm_obj_class_t parent = ovm_inst_classval(th, &argv[2]);
    ovm_obj_ns_t ns = (argc == 4) ? ovm_inst_nsval(th, &argv[3]) : ns_up(th, 1);
    str_inst_hash(&argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_obj_class_t cl = class_new(th, &work[-1], ns, nm->size, nm->data, argv[1].hash, parent, set_mark, set_free, 0);
    ovm_codemethod_newc(&work[-2], user_cl_alloc);
    dict_ats_put(th, ovm_obj_set(cl->cl_methods), _OVM_STR_CONST_HASH("__alloc__"), &work[-2]);

    ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(at)
{
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_class_t cl = ovm_inst_classval(th, recvr);
    ovm_obj_str_t s = ovm_inst_strval(th, arg);
    str_inst_hash(arg);
    if (!dict_ats(dst, ovm_obj_set(cl->cl_vars), s->size, s->data, arg->hash)) {
        ovm_inst_assign_obj(dst, 0);
    }
}

CM_DECL(ate)
{
    CM_ARGC_CHK(2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_class_t cl = ovm_inst_classval(th, recvr);
    ovm_obj_str_t s = ovm_inst_strval(th, arg);
    str_inst_hash(arg);
    if (!class_ats(dst, cl, s->size, s->data, arg->hash)) {
        ovm_except_no_attr(th, recvr, arg);
    }
}

CM_DECL(atput)
{
    CM_ARGC_CHK(3);
    ovm_inst_t recvr = &argv[0], arg = &argv[1], val = &argv[2];
    ovm_obj_class_t cl = ovm_inst_classval(th, recvr);
    ovm_obj_str_t s = ovm_inst_strval(th, arg);
    str_inst_hash(arg);
    class_ats_put(th, cl, s->size, s->data, arg->hash, val);
    ovm_inst_assign(dst, val);
}

CM_DECL(name)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_classval(th, &argv[0])->name);
}

CM_DECL(parent)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_classval(th, &argv[0])->parent);
}

CM_DECL(classmethod)
{
    CM_ARGC_CHK(2);
    ovm_inst_t arg = &argv[1];
    ovm_obj_class_t cl = ovm_inst_classval(th, &argv[0]);
    ovm_obj_str_t s = ovm_inst_strval(th, arg);
    str_inst_hash(arg);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    if (method_findc1_unsafe(th, &work[-1], cl, CL_OFS_CL_METHODS_DICT, s->size, s->data, arg->hash, 0)) {
        ovm_inst_assign(dst, &work[-1]);
    } else {
        ovm_inst_assign_obj(dst, 0);
    }
}

CM_DECL(classmethods)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_classval(th, &argv[0])->cl_methods);
}

CM_DECL(classvariables)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_classval(th, &argv[0])->cl_vars);
}

CM_DECL(method)
{
    CM_ARGC_CHK(2);
    ovm_inst_t arg = &argv[1];
    ovm_obj_class_t cl = ovm_inst_classval(th, &argv[0]);
    ovm_obj_str_t s = ovm_inst_strval(th, arg);
    str_inst_hash(arg);
    
    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    if (method_findc1_unsafe(th, &work[-1], cl, CL_OFS_INST_METHODS_DICT, s->size, s->data, arg->hash, 0)) {
        ovm_inst_assign(dst, &work[-1]);
    } else {
        ovm_inst_assign_obj(dst, 0);
    }
}

CM_DECL(methods)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, ovm_inst_classval(th, &argv[0])->inst_methods);
}

CM_DECL(current)
{
    CM_ARGC_CHK(1);
    ovm_inst_assign_obj(dst, class_up(th, 1)->base);
}

static ovm_obj_str_t class_write_unsafe(ovm_thread_t th, ovm_inst_t dst, ovm_obj_class_t cl)
{
    ovm_obj_str_t s1 = ns_write_unsafe(th, dst, ovm_obj_ns(cl->ns));
    ovm_obj_str_t s2 = ovm_obj_str(cl->name);
    if (s1->size <= 1) {
        ovm_inst_assign_obj(dst, s2->base);
        return (s2);
    }
    
    struct ovm_str_newv_item a[] = {
        { s1->size, s1->data },
        { _OVM_STR_CONST(".") },
        { s2->size, s2->data }
    };
    return (str_newv_size(dst, s1->size - 1 + 1 + s2->size, ARRAY_SIZE(a), a));
}

CM_DECL(write)
{
    CM_ARGC_CHK(1);

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    class_write_unsafe(th, &work[-1], ovm_inst_classval(th, &argv[0]));
    ovm_inst_assign(dst, &work[-1]);
}

static void _method_add(ovm_thread_t th, ovm_obj_set_t dict, unsigned sel_size, const char *sel, unsigned sel_hash, void *func)
{
    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_codemethod_newc(&work[-1], (ovm_codemethod_t) func);
    dict_ats_put(th, dict, sel_size, sel, sel_hash, &work[-1]);
    
    ovm_stack_unwind(th, work);
}

/* cl -- cl */

void ovm_classmethod_add(ovm_thread_t th, unsigned sel_size, const char *sel, unsigned sel_hash, void *func)
{
    _method_add(th, ovm_obj_set(ovm_inst_classval(th, th->sp)->cl_methods), sel_size, sel, sel_hash, func);
}

/* cl -- cl */

void ovm_method_add(ovm_thread_t th, unsigned sel_size, const char *sel, unsigned sel_hash, void *func)
{
    _method_add(th, ovm_obj_set(ovm_inst_classval(th, th->sp)->inst_methods), sel_size, sel, sel_hash, func);
}

void ovm_classmethod_del(ovm_obj_class_t cl, unsigned sel_size, const char *sel, unsigned sel_hash)
{
    dict_dels(ovm_obj_set(cl->cl_methods), sel_size, sel, sel_hash);
}

void ovm_method_del(ovm_obj_class_t cl, unsigned sel_size, const char *sel, unsigned sel_hash)
{
    dict_dels(ovm_obj_set(cl->inst_methods), sel_size, sel, sel_hash);
}

/***************************************************************************/

struct ovm_str_const {
    unsigned   size;
    const char *data;
};

static struct {
    ovm_obj_t *dst;
    struct ovm_str_const name[1];
    ovm_obj_t *parent;
    void      (*mark)(ovm_obj_t);
    void      (*free)(ovm_obj_t);
    void      (*cleanup)(ovm_obj_t);
    void      (*init)(ovm_thread_t, ovm_obj_class_t);
} cl_init_tbl[] = {
    { .dst       = &ovm_consts.Metaclass,
      .name      = {{ _OVM_STR_CONST("#Metaclass") }},
      .parent    = &ovm_consts.Object,
      .mark      = class_mark,
      .free      = class_free
    },
    { .dst       = &ovm_consts.Object,
      .name      = {{ _OVM_STR_CONST("#Object") }}
    },
    { .dst       = &ovm_consts.Boolean,
      .name      = {{ _OVM_STR_CONST("#Boolean") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.Integer,
      .name      = {{ _OVM_STR_CONST("#Integer") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.Float,
      .name      = {{ _OVM_STR_CONST("#Float") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.Method,
      .name      = {{ _OVM_STR_CONST("#Method") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.Codemethod,
      .name      = {{ _OVM_STR_CONST("#Codemethod") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.String,
      .name      = {{ _OVM_STR_CONST("#String") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.Pair,
      .name      = {{ _OVM_STR_CONST("#Pair") }},
      .parent    = &ovm_consts.Object,
      .mark      = pair_mark,
      .free      = pair_free
    },
    { .dst       = &ovm_consts.List,
      .name      = {{ _OVM_STR_CONST("#List") }},
      .parent    = &ovm_consts.Object,
      .mark      = list_mark,
      .free      = list_free
    },
    { .dst       = &ovm_consts.Array,
      .name      = {{ _OVM_STR_CONST("#Array") }},
      .parent    = &ovm_consts.Object,
      .mark      = array_mark,
      .free      = array_free
    },
    { .dst       = &ovm_consts.Carray,
      .name      = {{ _OVM_STR_CONST("#Carray") }},
      .parent    = &ovm_consts.Array,
      .mark      = array_mark,
      .free      = array_free
    },
    { .dst       = &ovm_consts.Bytearray,
      .name      = {{ _OVM_STR_CONST("#Bytearray") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.Cbytearray,
      .name      = {{ _OVM_STR_CONST("#Cbytearray") }},
      .parent    = &ovm_consts.Bytearray
    },
    { .dst       = &ovm_consts.Slice,
      .name      = {{ _OVM_STR_CONST("#Slice") }},
      .parent    = &ovm_consts.Object,
      .mark      = slice_mark,
      .free      = slice_free
    },
    { .dst       = &ovm_consts.Cslice,
      .name      = {{ _OVM_STR_CONST("#Cslice") }},
      .parent    = &ovm_consts.Slice,
      .mark      = slice_mark,
      .free      = slice_free
    },
    { .dst       = &ovm_consts.Set,
      .name      = {{ _OVM_STR_CONST("#Set") }},
      .parent    = &ovm_consts.Object,
      .mark      = set_mark,
      .free      = set_free
    },
    { .dst       = &ovm_consts.Cset,
      .name      = {{ _OVM_STR_CONST("#Cset") }},
      .parent    = &ovm_consts.Set,
      .mark      = set_mark,
      .free      = set_free
    },
    { .dst       = &ovm_consts.Dictionary,
      .name      = {{ _OVM_STR_CONST("#Dictionary") }},
      .parent    = &ovm_consts.Object,
      .mark      = set_mark,
      .free      = set_free,
    },
    { .dst       = &ovm_consts.Cdictionary,
      .name      = {{ _OVM_STR_CONST("#Cdictionary") }},
      .parent    = &ovm_consts.Dictionary,
      .mark      = set_mark,
      .free      = set_free,
    },
    { .dst       = &ovm_consts.Namespace,
      .name      = {{ _OVM_STR_CONST("#Namespace") }},
      .parent    = &ovm_consts.Object,
      .mark      = ns_mark,
      .free      = ns_free
    },
    { .dst       = &ovm_consts.Module,
      .name      = {{ _OVM_STR_CONST("#Module") }},
      .parent    = &ovm_consts.Namespace,
      .mark      = module_mark,
      .free      = module_free,
      .cleanup   = module_cleanup,
      .init      = module_cl_init
    },
    { .dst       = &ovm_consts.User,
      .name      = {{ _OVM_STR_CONST("#__User_Class") }},
      .parent    = &ovm_consts.Object,
      .mark      = set_mark,
      .free      = set_free
    },
    { .dst       = &ovm_consts.File,
      .name      = {{ _OVM_STR_CONST("#File") }},
      .parent    = &ovm_consts.Object,
      .mark      = file_mark,
      .free      = file_free,
      .cleanup   = file_cleanup,
      .init      = file_cl_init
    },
    { .dst       = &ovm_consts.Exception,
      .name      = {{ _OVM_STR_CONST("#Exception") }},
      .parent    = &ovm_consts.User,
      .mark      = set_mark,
      .free      = set_free
    },
    { .dst       = &ovm_consts.System,
      .name      = {{ _OVM_STR_CONST("#System") }},
      .parent    = &ovm_consts.Object
    },
    { .dst       = &ovm_consts.Environment,
      .name      = {{ _OVM_STR_CONST("#Environment") }},
      .parent    = &ovm_consts.Object
    }
};

#define METHOD_INITF2(_sel, _cl, _func)                 \
    {                                                   \
        .cl   = &ovm_consts. METHOD_CLASS,                      \
            .dict_ofs = METHOD_INIT_DICT_OFS,           \
            .sel  = {{ OVM_STR_CONST(_sel) }},          \
            .func = _METHOD_NAME(METHOD_MODULE, _cl, _func)     \
            }

#define METHOD_INITF(_sel, _func)               \
    {                                           \
        .cl   = &ovm_consts. METHOD_CLASS,              \
            .dict_ofs = METHOD_INIT_DICT_OFS,   \
            .sel  = {{ OVM_STR_CONST(_sel) }},  \
            .func = METHOD_NAME(_func)          \
            }

#define METHOD_INIT(sel)  METHOD_INITF(sel, sel)

static struct {
    ovm_obj_t            *cl;
    unsigned             dict_ofs;
    struct ovm_str_const sel[1];
    ovm_codemethod_t     func;
} method_init_tbl[] = {

#undef  METHOD_CLASS
#define METHOD_CLASS  Metaclass

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),
    METHOD_INIT(name),
    METHOD_INIT(parent),
    METHOD_INIT(classmethods),
    METHOD_INIT(classvariables),
    METHOD_INIT(methods),
    METHOD_INIT(current),
    METHOD_INITF2(equal, Object, equal),
    METHOD_INIT(write),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(name),
    METHOD_INIT(parent),
    METHOD_INIT(classmethods),
    METHOD_INIT(classvariables),
    METHOD_INIT(methods),
    METHOD_INIT(at),
    METHOD_INIT(ate),
    METHOD_INIT(atput),
    METHOD_INIT(write),
    METHOD_INIT(method),
    METHOD_INIT(classmethod),

#undef  METHOD_CLASS
#define METHOD_CLASS  Object

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(__init__, init),
    METHOD_INIT(Boolean),
    METHOD_INIT(List),
    METHOD_INITF(String, write),
    METHOD_INIT(copy),
    METHOD_INIT(copydeep),
    METHOD_INIT(at),
    METHOD_INIT(ate),
    METHOD_INIT(atdefault),
    METHOD_INIT(atput),
    METHOD_INIT(cons),
    METHOD_INIT(enumerate),
    METHOD_INIT(equal),
    METHOD_INIT(isnil),
    METHOD_INIT(instanceof),
    METHOD_INIT(method),
    METHOD_INIT(reverse),
    METHOD_INIT(size),
    METHOD_INIT(print),
    METHOD_INIT(println),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Boolean

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(Boolean, copy),
    METHOD_INIT(Integer),
    METHOD_INITF(String, write),
    METHOD_INIT(copy),
    METHOD_INITF(copydeep, copy),
    METHOD_INIT(and),
    METHOD_INIT(equal),
    METHOD_INIT(not),
    METHOD_INIT(or),
    METHOD_INIT(write),
    METHOD_INIT(xor),

#undef  METHOD_CLASS
#define METHOD_CLASS  Integer

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),
    
#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(Boolean),
    METHOD_INITF(Integer, copy),
    METHOD_INIT(Float),
    METHOD_INITF(String, write),
    METHOD_INIT(copy),
    METHOD_INITF(copydeep, copy),
    METHOD_INIT(add),
    METHOD_INIT(band),
    METHOD_INIT(bor),
    METHOD_INIT(cmp),
    METHOD_INIT(div),
    METHOD_INIT(equal),
    METHOD_INIT(ge),
    METHOD_INIT(gt),
    METHOD_INIT(hash),
    METHOD_INIT(le),
    METHOD_INIT(lt),
    METHOD_INIT(minus),
    METHOD_INIT(mod),
    METHOD_INIT(mul),
    METHOD_INIT(sub),
    METHOD_INIT(write),  
    
#undef  METHOD_CLASS
#define METHOD_CLASS  Float

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(String, write),
    METHOD_INIT(sub),
    METHOD_INIT(div),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Method

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(call),
    METHOD_INIT(calla),
    METHOD_INIT(write),
    
#undef  METHOD_CLASS
#define METHOD_CLASS  Codemethod

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(call),
    METHOD_INIT(calla),
    METHOD_INIT(write),
    
#undef  METHOD_CLASS
#define METHOD_CLASS  String

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(Boolean),
    METHOD_INIT(Integer),
    METHOD_INITF(String, copy),
    METHOD_INIT(Array),
    METHOD_INIT(Carray),
    METHOD_INIT(Bytearray),
    METHOD_INIT(Cbytearray),
    METHOD_INIT(Slice),
    METHOD_INITF(Cslice, Slice),
    METHOD_INIT(copy),
    METHOD_INITF(copydeep, copy),
    METHOD_INITF(add, concat),
    METHOD_INIT(at),
    METHOD_INIT(call),
    METHOD_INIT(cmp),
    METHOD_INIT(concat),
    METHOD_INIT(equal),
    METHOD_INIT(format),
    METHOD_INIT(hash),
    METHOD_INIT(index),
    METHOD_INIT(join),
    METHOD_INIT(parse),
    METHOD_INIT(rindex),
    METHOD_INIT(rjoin),
    METHOD_INIT(size),
    METHOD_INIT(slice),
    METHOD_INIT(split),
    METHOD_INIT(write),
    
#undef  METHOD_CLASS
#define METHOD_CLASS  Pair

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),
    
#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(String, write),
    METHOD_INITF(Pair, copy),
    METHOD_INIT(List),
    METHOD_INIT(copy),
    METHOD_INIT(copydeep),
    METHOD_INIT(equal),
    METHOD_INIT(first),
    METHOD_INIT(hash),
    METHOD_INIT(second),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  List

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),
    
#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(Boolean),
    METHOD_INITF(String, write),
    METHOD_INITF(List, copy),
    METHOD_INIT(Array),
    METHOD_INIT(Carray),
    METHOD_INIT(Set),
    METHOD_INIT(Cset),
    METHOD_INIT(Dictionary),
    METHOD_INIT(Dictionary),
    METHOD_INIT(copy),
    METHOD_INIT(copydeep),
    METHOD_INIT(at),
    METHOD_INIT(car),
    METHOD_INIT(cdr),
    METHOD_INIT(concat),
    METHOD_INIT(cons),
    METHOD_INIT(equal),
    METHOD_INIT(hash),
    METHOD_INIT(map1),
    METHOD_INIT(map),
    METHOD_INIT(reduce1),
    METHOD_INIT(reduce),
    METHOD_INIT(reverse),
    METHOD_INIT(size),
    METHOD_INIT(slice),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Array

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(Boolean),
    METHOD_INITF(Integer, size),
    METHOD_INITF(String, write),
    METHOD_INIT(List),
    METHOD_INIT(Array),
    METHOD_INIT(Carray),
    METHOD_INIT(Slice),
    METHOD_INIT(Cslice),
    METHOD_INIT(copy),
    METHOD_INIT(copydeep),
    METHOD_INIT(at),
    METHOD_INIT(atput),
    METHOD_INIT(equal),
    METHOD_INIT(size),
    METHOD_INIT(slice),
    METHOD_INIT(write),
    
#undef  METHOD_CLASS
#define METHOD_CLASS  Carray

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(String, write),
    METHOD_INIT(Array),
    METHOD_INITF(Carray, copy),
    METHOD_INIT(copy),
    METHOD_INIT(hash),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Bytearray

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(Boolean),
    METHOD_INITF(Integer, size),
    METHOD_INIT(String),
    METHOD_INIT(List),
    METHOD_INIT(Array),
    METHOD_INIT(Carray),
    METHOD_INIT(Slice),
    METHOD_INIT(Cslice),
    METHOD_INIT(copy),
    METHOD_INITF(copydeep, copy),
    METHOD_INIT(at),
    METHOD_INIT(atput),
    METHOD_INIT(cmp),
    METHOD_INIT(equal),
    METHOD_INIT(size),
    METHOD_INIT(slice),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Cbytearray

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(String, write),
    METHOD_INIT(copy),
    METHOD_INITF(copydeep, copy),
    METHOD_INIT(write),
    
#undef  METHOD_CLASS
#define METHOD_CLASS  Slice

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(String, write),
    METHOD_INIT(List),
    METHOD_INIT(Array),
    METHOD_INIT(Slice),
    METHOD_INIT(at),
    METHOD_INIT(atput),
    METHOD_INIT(hash),
    METHOD_INIT(size),
    METHOD_INIT(slice),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Cslice

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(String, write),
    METHOD_INIT(Cslice),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Set

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(Boolean),
    METHOD_INITF(Integer, size),
    METHOD_INITF(String, write),
    METHOD_INIT(List),
    METHOD_INIT(Array),
    METHOD_INIT(Carray),
    METHOD_INIT(Set),
    METHOD_INIT(Cset),
    METHOD_INIT(copy),
    METHOD_INIT(copydeep),
    METHOD_INIT(at),
    METHOD_INIT(del),
    METHOD_INIT(delall),
    METHOD_INIT(put),
    METHOD_INIT(size),
    METHOD_INIT(tablesize),
    METHOD_INIT(write),
    
#undef  METHOD_CLASS
#define METHOD_CLASS  Cset

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(String, write),
    METHOD_INIT(Set),
    METHOD_INITF(Cset, copy),
    METHOD_INIT(copy),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Dictionary

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT
  
    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT
  
    METHOD_INIT(Boolean),
    METHOD_INITF(Integer, size),
    METHOD_INITF(String, write),
    METHOD_INIT(List),
    METHOD_INIT(Array),
    METHOD_INIT(Carray),
    METHOD_INIT(Dictionary),
    METHOD_INIT(Cdictionary),
    METHOD_INIT(copy),
    METHOD_INIT(copydeep),
    METHOD_INIT(at),
    METHOD_INIT(ate),
    METHOD_INIT(atdefault),
    METHOD_INIT(atput),
    METHOD_INIT(atputnew),
    METHOD_INIT(del),
    METHOD_INIT(delall),
    METHOD_INIT(put),
    METHOD_INIT(size),
    METHOD_INIT(tablesize),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Cdictionary

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT
  
    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT
  
    METHOD_INIT(copy),
    METHOD_INIT(copydeep),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Namespace

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT
  
    METHOD_INIT(new),
    METHOD_INIT(current),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT
  
    METHOD_INITF(String, write),
    METHOD_INIT(Dictionary),
    METHOD_INIT(at),
    METHOD_INIT(ate),
    METHOD_INIT(atput),
    METHOD_INIT(name),
    METHOD_INIT(parent),
    METHOD_INIT(write),

#undef  METHOD_CLASS
#define METHOD_CLASS  Module

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT
  
    METHOD_INIT(new),
    METHOD_INIT(current),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT
  
    METHOD_INIT(filename),
    METHOD_INIT(sha1),

#undef  METHOD_CLASS
#define METHOD_CLASS  File

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT
  
    METHOD_INIT(new),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INITF(Boolean, eof),
    METHOD_INITF(Integer, tell),
    METHOD_INIT(copy),
    METHOD_INITF(copydeep, copy),
    METHOD_INIT(eof),
    METHOD_INIT(filename),
    METHOD_INIT(flush),
    METHOD_INIT(mode),
    METHOD_INIT(read),
    METHOD_INIT(readb),
    METHOD_INIT(readln),
    METHOD_INIT(tell),
    METHOD_INIT(unread),
    METHOD_INIT(write),
    METHOD_INIT(writeln),

#undef  METHOD_CLASS
#define METHOD_CLASS  Environment

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT
  
    METHOD_INIT(at),
    METHOD_INIT(ate),
    METHOD_INIT(atput),

#undef  METHOD_CLASS
#define METHOD_CLASS  Exception

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT
  
    METHOD_INIT(new),
    METHOD_INIT(reraise),

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_INST_METHODS_DICT

    METHOD_INIT(raise),
    
#undef  METHOD_CLASS
#define METHOD_CLASS  System

#undef  METHOD_INIT_DICT_OFS
#define METHOD_INIT_DICT_OFS  CL_OFS_CL_METHODS_DICT

    METHOD_INIT(exit),
    METHOD_INIT(abort),
    METHOD_INIT(assert)

#ifndef NDEBUG
    ,
    METHOD_INIT(collect)
#endif
};

static void classes_init(ovm_thread_t th)
{
    ovm_inst_t work = ovm_stack_alloc(th, 4);

    /* Pass 0: Create Metaclass */
  
    class_alloc(&work[-3]);
    _ovm_obj_assign_nolock_norelease(&ovm_consts.Metaclass, ovm_inst_classval_nochk(&work[-3])->base);

    /* Pass 1: Create classes, as instances of Metaclass */
  
    unsigned i;
    for (i = 1; i < ARRAY_SIZE(cl_init_tbl); ++i) {
        class_alloc(&work[-3]);
        _ovm_obj_assign_nolock_norelease(cl_init_tbl[i].dst, ovm_inst_classval_nochk(&work[-3])->base);
    }

    /* Pass 2: Assign class names, parents, handlers and dictionaries */
  
    for (i = 0; i < ARRAY_SIZE(cl_init_tbl); ++i) {
        ovm_obj_class_t cl = ovm_obj_class(*cl_init_tbl[i].dst);
        str_newc(&work[-3], cl_init_tbl[i].name->size, cl_init_tbl[i].name->data);
        _ovm_obj_assign_nolock_norelease(&cl->name, work[-3].objval);
        if (cl_init_tbl[i].parent) {
            _ovm_obj_assign_nolock_norelease(&cl->parent, *cl_init_tbl[i].parent);
        }
        cl->mark    = cl_init_tbl[i].mark;
        cl->cleanup = cl_init_tbl[i].cleanup;
        cl->free    = cl_init_tbl[i].free;
        _ovm_obj_assign_nolock_norelease(&cl->cl_vars, set_newc(&work[-3], OVM_CL_DICTIONARY, CL_VARS_DICT_SIZE)->base);
        _ovm_obj_assign_nolock_norelease(&cl->cl_methods, set_newc(&work[-3], OVM_CL_DICTIONARY, CL_METHOD_DICT_SIZE)->base);
        _ovm_obj_assign_nolock_norelease(&cl->inst_methods, set_newc(&work[-3], OVM_CL_DICTIONARY, CL_METHOD_DICT_SIZE)->base);
    }

    /* Pass 3: Add methods to classes */
  
    for (i = 0; i < ARRAY_SIZE(method_init_tbl); ++i) {
        ovm_codemethod_newc(&work[-3], method_init_tbl[i].func);
        dict_ats_put(th, cl_dict(ovm_obj_class(*method_init_tbl[i].cl), method_init_tbl[i].dict_ofs), method_init_tbl[i].sel->size, method_init_tbl[i].sel->data, str_hashc(method_init_tbl[i].sel->size, method_init_tbl[i].sel->data), &work[-3]);
    }

    /* Pass 4: Create main namespace */

    ovm_obj_set_t main_dict = set_newc(&work[-4], OVM_CL_DICTIONARY, 64);
    ovm_obj_str_t s = str_newc(&work[-3], OVM_STR_CONST(main));
    ovm_obj_ns_t ns = ns_new(th, &work[-2], s, str_inst_hash(&work[-3]), main_dict, 0);
    dict_ats_put(th, main_dict, s->size, s->data, str_hashc(s->size, s->data), &work[-2]);
    _ovm_obj_assign_nolock_norelease(&ns_main, ns->base);
  
    /* Pass 5: Assign all classes to main module */
  
    for (i = 0; i < ARRAY_SIZE(cl_init_tbl); ++i) {
        _ovm_obj_assign_nolock_norelease(&ovm_obj_class(*cl_init_tbl[i].dst)->ns, ns_main);
        _ovm_inst_assign_obj_nolock(&work[-1], (*cl_init_tbl[i].dst));
        dict_ats_put(th, main_dict, cl_init_tbl[i].name->size, cl_init_tbl[i].name->data, str_hashc(cl_init_tbl[i].name->size, cl_init_tbl[i].name->data), &work[-1]);
    }

    /* Pass 6: Run class init hooks */
  
    struct ovm_frame *fr = frame_ns_push(th, ovm_obj_ns(ns_main));
  
    for (i = 0; i < ARRAY_SIZE(cl_init_tbl); ++i) {
        if (cl_init_tbl[i].init == 0)  continue;
        (*cl_init_tbl[i].init)(th, ovm_obj_class(*cl_init_tbl[i].dst));
    }

    frame_pop(th, fr);
  
    ovm_stack_unwind(th, work);
}

/***************************************************************************/

ovm_thread_t ovm_init(unsigned stack_size, unsigned frame_stack_size)
{
    ovm_thread_t th;

    mem_init();
    objs_init();
    th = threading_init(stack_size, frame_stack_size);
    classes_init(th);

    return (th);
}

ovm_thread_t ovm_thread_self(void)
{
    return ((ovm_thread_t) pthread_getspecific(pthread_key_self));
}

static void run_entry_method(ovm_thread_t th, ovm_inst_t dst, ovm_obj_ns_t entry_ns, ovm_obj_class_t entry_cl, ovm_inst_t entry_method, unsigned argc, char **argv)
{
    ovm_stack_alloc(th, 1 + argc);
    ovm_inst_assign_obj(th->sp, entry_ns->base);
    ovm_inst_t p;
    unsigned n;
    for (p = &th->sp[1], n = argc; n > 0; --n, ++p, ++argv)  str_newc1(p, *argv);

    method_run(th, dst, entry_ns, entry_cl, entry_method, 1 + argc, th->sp);
}

int ovm_run(ovm_thread_t th, ovm_inst_t dst, char *entry_module, char *entry_cl, char *entry_method, int argc, char **argv)
{
    ovm_inst_t work = ovm_stack_alloc(th, 6);
    
    ovm_obj_str_t modname = str_newc1(&work[-1], entry_module);
    unsigned modname_hash = str_inst_hash(&work[-1]);
    
    if (!module_file_chk_path_unsafe(th, &work[-2], modname, &work[-3])) {
        fprintf(stderr, "Error: entry module %s not found\n", entry_module);
        return (-2);
    }

    char mesg[132];
    
    if (!module_load_unsafe(th, &work[-4], modname, modname_hash, ovm_inst_strval_nochk(&work[-2]), ovm_inst_strval_nochk(&work[-3]), ovm_obj_ns(ns_main), sizeof(mesg), mesg)) {
        fprintf(stderr, "Error: entry module %s %s\n", entry_module, mesg);
        return (-3);
    }

    ovm_obj_list_t li = str_splitc_unsafe(th, &work[-5], strlen(entry_cl) + 1, entry_cl, _OVM_STR_CONST(".")), next;
    ovm_obj_str_t s;
    for (;;) {
        s = ovm_inst_strval_nochk(li->item);
        next = ovm_list_next(li);
        if (next == 0)  break;
        if (!ovm_is_subclass_of(ovm_inst_of_raw(&work[-4]), OVM_CL_NAMESPACE)
            || !ns_ats(&work[-4], ovm_inst_nsval_nochk(&work[-4]), s->size, s->data, str_hash(s))
            ) {
            fprintf(stderr, "Error: entry namespace %s not found\n", s->data);
            return (-4);
        }
        ovm_inst_assign(&work[-4], ovm_inst_pairval_nochk(&work[-4])->second);
        li = next;
    }
    if (!ovm_is_subclass_of(ovm_inst_of_raw(&work[-4]), OVM_CL_NAMESPACE)
        || !ns_ats(&work[-5], ovm_inst_nsval_nochk(&work[-4]), s->size, s->data, str_hash(s))
        ) {
    no_entry_cl:
        fprintf(stderr, "Error: entry class %s not found\n", entry_cl);
        return (-5);
    }
    ovm_inst_assign(&work[-5], ovm_inst_pairval_nochk(&work[-5])->second);
    if (ovm_inst_of_raw(&work[-5]) != OVM_METACLASS)  goto no_entry_cl;
    if (!dict_ats(&work[-6], ovm_obj_set(ovm_inst_classval_nochk(&work[-5])->cl_methods), strlen(entry_method) + 1, entry_method, str_hashc(strlen(entry_method) + 1, entry_method))) {
    method_not_found:
        fprintf(stderr, "Error: entry method %s not found\n", entry_method);
        return (-6);
    }

    ovm_inst_assign(&work[-6], ovm_inst_pairval_nochk(&work[-6])->second);
    switch (work[-6].type) {
    case OVM_INST_TYPE_METHOD:
    case OVM_INST_TYPE_CODEMETHOD:
        break;
    default:
        goto method_not_found;
    }

    run_entry_method(th, dst, 0, ovm_inst_classval_nochk(&work[-5]), &work[-6], argc, argv);

    ovm_stack_unwind(th, work);

    return (0);
}

void ovm_run_static(ovm_thread_t th, ovm_inst_t dst, ovm_codemethod_t init, ovm_codemethod_t entry, int argc, char **argv)
{
    ovm_inst_t work = ovm_stack_alloc(th, 1);

    ovm_inst_assign_obj(&work[-1], ns_main);
    (*init)(th, &work[-1], 0, 0);

    ovm_codemethod_newc(&work[-1], entry);
    run_entry_method(th, dst, ovm_obj_ns(ns_main), 0, &work[-1], argc, argv);

    ovm_stack_unwind(th, work);
}

#ifndef NDEBUG

void ovm_debug_inst_print(ovm_thread_t th, ovm_inst_t inst)
{
    ovm_stack_push(th, inst);
    ovm_method_callsch(th, th->sp, OVM_STR_CONST_HASH(write), 1);
    printf("%s\n", ovm_inst_strval(th, th->sp)->data);
    ovm_stack_free(th, 1);
}
 
void ovm_debug_obj_print(ovm_thread_t th, ovm_obj_t obj)
{
    ovm_stack_push_obj(th, obj);
    ovm_method_callsch(th, th->sp, OVM_STR_CONST_HASH(write), 1);
    printf("%s\n", ovm_inst_strval(th, th->sp)->data);
    ovm_stack_free(th, 1);
}
 
void ovm_debug_stk_frame(ovm_thread_t th)
{
    struct ovm_frame_mc *mcfp = th->mcfp;
    if (mcfp == 0)  return;
    ovm_inst_t p = th->sp, q = mcfp->ap + mcfp->argc;
    unsigned i;

    ovm_inst_t work = ovm_stack_alloc(th, 1);

    puts("");
    for (i = 0; p < q; ++p, ++i) {
        ovm_inst_assign(&work[-1], p);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 1);
        printf("%4u: %4s %s\n", i, p == mcfp->ap ? "ap->" : (p == mcfp->bp ? "bp->" : ""), ovm_inst_strval(th, &work[-1])->data);
    }

    ovm_stack_unwind(th, work);
}

void ovm_debug_stk_dump(ovm_thread_t th)
{
    ovm_inst_t p = th->sp, ap = 0, bp = 0;
    struct ovm_frame_mc *mcfp = th->mcfp;
    if (mcfp != 0) {
        ap = mcfp->ap;
        bp = mcfp->bp;
    }
    printf("\nDepth = %lu\n", (long unsigned)(th->stack_top - th->sp));
    unsigned i;

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    
    for (i = 0; p < th->stack_top; ++p, ++i) {
        ovm_inst_assign(&work[-1], p);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(write), 1);
        printf("%4u: %4s %s\n", i, p == ap ? "ap->" : (p == bp ? "bp->" : ""), ovm_inst_strval(th, &work[-1])->data);
    }

    ovm_stack_unwind(th, work);
}

void ovm_debug_backtrace(ovm_thread_t th)
{
    backtrace(th);
}

void ovm_debug_obj_chk(void)
{
    struct ovm_dllist *p;
    for (p = ovm_dllist_first(obj_list_white); p != ovm_dllist_end(obj_list_white); p = ovm_dllist_next(p)) {
        ovm_obj_t obj = FIELD_PTR_TO_STRUCT_PTR(p, struct ovm_obj, list_node);
        if (obj->ref_cnt == 0) {
            printf("Object %p ref_cnt == 0!\n", obj);
        }
    }
}

void ovm_debug_set_print(ovm_thread_t th, ovm_obj_set_t s)
{
    unsigned i;
    ovm_obj_t *p;
    for (p = &s->data[i = 0]; i < s->size; ++i, ++p) {
        printf("%3d: ", i);
        ovm_debug_obj_print(th, *p);
    }
}

#endif

/*
Local Variables:
c-basic-offset: 4
End:
*/
