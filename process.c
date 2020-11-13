#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "oovm.h"

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

static ovm_obj_t my_class;

struct ovm_obj_proc {
  struct ovm_obj base[1];
  ovm_obj_t      argv, _stdin, _stdout, _stderr;
  int            pid;
  bool           waitedf;
  int            status;
};
typedef struct ovm_obj_proc *ovm_obj_proc_t;

static inline ovm_obj_proc_t ovm_obj_proc(ovm_obj_t obj)
{
  return ((ovm_obj_proc_t) obj);
}

static inline ovm_obj_proc_t ovm_inst_procval_nochk(ovm_inst_t inst)
{
  return (ovm_obj_proc(inst->objval));
}

static ovm_obj_proc_t ovm_inst_procval(ovm_thread_t th, ovm_inst_t inst)
{
  if (ovm_inst_of_raw(inst) != ovm_obj_class(my_class))  ovm_except_inv_value(th, inst);
  return (ovm_inst_procval_nochk(inst));
}

static void process_walk(ovm_obj_t obj, void (*func)(ovm_obj_t))
{
  ovm_obj_proc_t p = ovm_obj_proc(obj);
  
  (*func)(p->argv);
  (*func)(p->_stdin);
  (*func)(p->_stdout);
  (*func)(p->_stderr);
}

static void process_mark(ovm_obj_t obj)
{
  process_walk(obj, ovm_obj_mark);
}

static void process_cleanup(ovm_obj_t obj)
{
  ovm_obj_proc_t p = ovm_obj_proc(obj);
  if (!p->waitedf)  waitpid(p->pid, 0, 0);
}

static void process_free(ovm_obj_t obj)
{
  process_walk(obj, ovm_obj_release);
  process_cleanup(obj);
}

static void process_init(ovm_obj_t obj, va_list ap)
{
  ovm_obj_proc_t proc = ovm_obj_proc(obj);
  memset(obj + 1, 0, sizeof(*proc) - sizeof(*obj));
  proc->pid = va_arg(ap, int);
}

static ovm_obj_proc_t process_newc(ovm_inst_t dst, int pid)
{
  return (ovm_obj_proc(ovm_obj_alloc(dst, sizeof(struct ovm_obj_proc), ovm_obj_class(my_class), true, process_init, pid)));
}

#define METHOD_MODULE  process
#define METHOD_CLASS   Process

CM_DECL(new)
{
  ovm_method_argc_chk_exact(th, 2);

  ovm_inst_t work = ovm_stack_alloc(th, 4);

  ovm_inst_assign(&work[-4], &argv[1]);
  ovm_method_callsch(th, &work[-3], OVM_STR_CONST_HASH(size), 1);
  ovm_intval_t n = ovm_inst_intval(th, &work[-3]);
  ovm_inst_assign_obj(&work[-4], ovm_consts.Array);
  ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(new), 2);

  char *proc_argv[n + 1], **r;
  ovm_intval_t i;
  for (r = proc_argv, i = 0; i < n; ++i, ++r) {
    ovm_inst_assign(&work[-4], &argv[1]);
    ovm_int_newc(&work[-3], i);
    ovm_method_callsch(th, &work[-4], OVM_STR_CONST_HASH(at), 2);
    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(String), 1);
    ovm_inst_assign(&work[-4], &work[-1]);
    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(atput), 3);
    *r = ovm_inst_strval(th, &work[-2])->data;
  }
  *r = 0;

  int fd[6] = { -1, -1, -1, -1, -1, -1 }, pid;
  if (pipe(&fd[0]) != 0 || pipe(&fd[2]) != 0 || pipe(&fd[4]) != 0 || (pid = fork()) < 0) {
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(fd); ++i) {
      if (fd[i] >= 0)  close(fd[i]);
    }

    ovm_inst_assign_obj(dst, 0);
  }
  if (pid == 0) {
    /* Child */

    do {
      close(0);
      if (dup(fd[0]) < 0)  break;
      close(1);
      if (dup(fd[3]) < 0)  break;
      close(2);
      if (dup(fd[5]) < 0)  break;
      unsigned i;
      for (i = 0; i < ARRAY_SIZE(fd); ++i)  close(fd[i]);

      execv(proc_argv[0], &proc_argv[1]);
    } while (0);

    exit(errno);
  }

  /* Parent */

  ovm_obj_proc_t proc = process_newc(&work[-2], pid);
  close(fd[0]);
  close(fd[3]);
  close(fd[5]);

  ovm_obj_assign(&proc->argv, work[-1].objval);
  ovm_file_newc(th, &work[-1], _OVM_STR_CONST("__stdin__"), OVM_STR_CONST(w), fdopen(fd[1], "w"));
  ovm_obj_assign(&proc->_stdin, work[-1].objval);
  ovm_file_newc(th, &work[-1], _OVM_STR_CONST("__stdout__"), OVM_STR_CONST(r), fdopen(fd[2], "r"));
  ovm_obj_assign(&proc->_stdout, work[-1].objval);
  ovm_file_newc(th, &work[-1], _OVM_STR_CONST("__stderr__"), OVM_STR_CONST(r), fdopen(fd[4], "r"));
  ovm_obj_assign(&proc->_stderr, work[-1].objval);

  ovm_inst_assign(dst, &work[-2]);
}

CM_DECL(argv)
{
  ovm_method_argc_chk_exact(th, 1);
  ovm_inst_assign_obj(dst, ovm_inst_procval(th, &argv[0])->argv);  
}

CM_DECL(_stdin)
{
  ovm_method_argc_chk_exact(th, 1);
  ovm_inst_assign_obj(dst, ovm_inst_procval(th, &argv[0])->_stdin);
}

CM_DECL(_stdout)
{
  ovm_method_argc_chk_exact(th, 1);
  ovm_inst_assign_obj(dst, ovm_inst_procval(th, &argv[0])->_stdout);
}

CM_DECL(_stderr)
{
  ovm_method_argc_chk_exact(th, 1);
  ovm_inst_assign_obj(dst, ovm_inst_procval(th, &argv[0])->_stderr);
}

CM_DECL(pid)
{
  ovm_method_argc_chk_exact(th, 1);
  ovm_int_newc(dst, ovm_inst_procval(th, &argv[0])->pid);
}

CM_DECL(kill)
{
  ovm_method_argc_chk_exact(th, 1);
  ovm_obj_proc_t p = ovm_inst_procval(th, &argv[0]);
  kill(p->pid, ovm_inst_intval(th, &argv[1]));
  ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(wait)
{
  ovm_method_argc_chk_exact(th, 1);
  ovm_obj_proc_t p = ovm_inst_procval(th, &argv[0]);
  if (!p->waitedf) {
    waitpid(p->pid, &p->status, 0);
    p->waitedf = true;
  }
  ovm_int_newc(dst, p->status);
}

CM_DECL(write)
{
  ovm_method_argc_chk_exact(th, 1);

  ovm_obj_proc_t p = ovm_inst_procval(th, &argv[0]);

  ovm_inst_t work = ovm_stack_alloc(th, 2);
  struct ovm_clist cl[1];
  ovm_clist_init(cl);

  ovm_inst_assign_obj(&work[-2], ovm_consts.Object);
  ovm_str_newc(&work[-1], OVM_STR_CONST(write));
  ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(method), 2);
  ovm_inst_assign(&work[-1], &argv[0]);
  ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(call), 2);
  ovm_clist_append_str(th, cl, ovm_inst_strval(th, &work[-2]));

  ovm_clist_appendc(cl, _OVM_STR_CONST("{argv: "));
  ovm_inst_assign_obj(&work[-2], p->argv);
  ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(write), 1);
  ovm_clist_append_str(th, cl, ovm_inst_strval(th, &work[-2]));
  
  ovm_clist_appendc(cl, _OVM_STR_CONST(", pid: "));
  ovm_int_newc(&work[-2], p->pid);
  ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(write), 1);
  ovm_clist_append_str(th, cl, ovm_inst_strval(th, &work[-2]));

  ovm_clist_append_char(cl, '}');

  ovm_str_clist(dst, cl);

  ovm_clist_fini(cl);
}

void __process_init__(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
  ovm_inst_t old = th->sp;

  ovm_stack_push(th, &argv[0]);

  ovm_stack_push_obj(th, ovm_consts.Object);
  ovm_class_new(th, OVM_STR_CONST_HASH(Process), process_mark, process_free, process_cleanup);
  my_class = th->sp->objval;

  ovm_classmethod_add(th, OVM_STR_CONST_HASH(new), METHOD_NAME(new));
  ovm_method_add(th, _OVM_STR_CONST_HASH("argv"), METHOD_NAME(argv));
  ovm_method_add(th, _OVM_STR_CONST_HASH("stdin"), METHOD_NAME(_stdin));
  ovm_method_add(th, _OVM_STR_CONST_HASH("stdout"), METHOD_NAME(_stdout));
  ovm_method_add(th, _OVM_STR_CONST_HASH("stderr"), METHOD_NAME(_stderr));
  ovm_method_add(th, OVM_STR_CONST_HASH(pid), METHOD_NAME(pid));
  ovm_method_add(th, OVM_STR_CONST_HASH(kill), METHOD_NAME(kill));
  ovm_method_add(th, OVM_STR_CONST_HASH(wait), METHOD_NAME(wait));
  ovm_method_add(th, OVM_STR_CONST_HASH(write), METHOD_NAME(write));
  ovm_method_add(th, OVM_STR_CONST_HASH(String), METHOD_NAME(write));

  ovm_stack_unwind(th, old);
}
