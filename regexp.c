#include <sys/types.h>
#include <regex.h>

#include "oovm.h"

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

static ovm_obj_t my_class;

struct ovm_obj_regexp {
  struct ovm_obj  base[1];
  int             rc;
  regex_t         re[1];
};
typedef struct ovm_obj_regexp *ovm_obj_regexp_t;

static inline ovm_obj_regexp_t ovm_obj_regexp(ovm_obj_t obj)
{
  return ((ovm_obj_regexp_t) obj);
}

static inline ovm_obj_regexp_t ovm_inst_reval_nochk(ovm_inst_t inst)
{
  return (ovm_obj_regexp(inst->objval));
}

static ovm_obj_regexp_t ovm_inst_reval(ovm_thread_t th, ovm_inst_t inst)
{
  if (ovm_inst_of_raw(inst) != ovm_obj_class(my_class))  ovm_except_inv_value(th, inst);
  return (ovm_inst_reval_nochk(inst));
}

static void regexp_cleanup(ovm_obj_t obj)
{
  ovm_obj_regexp_t re = ovm_obj_regexp(obj);
  if (re->rc == 0)  regfree(re->re);
}

static void regexp_init(ovm_obj_t obj, va_list ap)
{
  char *pat = va_arg(ap, char *);
  int  flags = va_arg(ap, int);
  ovm_obj_regexp_t re = ovm_obj_regexp(obj);
  re->rc = regcomp(re->re, pat, flags);
}

static ovm_obj_regexp_t regexp_newc(ovm_inst_t dst, const char *pat, int flags)
{
  return (ovm_obj_regexp(ovm_obj_alloc(dst, sizeof(struct ovm_obj_regexp), ovm_obj_class(my_class), 2, regexp_init, pat, flags)));
}

#define METHOD_MODULE  regexp
#define METHOD_CLASS   Regexp

CM_DECL(new)
{
  ovm_method_argc_chk_range(th, 2, 3);
  ovm_obj_str_t s = ovm_inst_strval(th, &argv[1]);
  ovm_intval_t  i = (argc == 3) ? ovm_inst_intval(th, &argv[2]) : 0;

  ovm_inst_t work = ovm_stack_alloc(th, 1);

  ovm_obj_regexp_t re = regexp_newc(&work[-1], s->data, i);
  if (re->rc != 0) {
    ovm_int_newc(dst, re->rc);
    return;
  }
  
  ovm_inst_assign(dst, &work[-1]);
}

CM_DECL(match)
{
  ovm_method_argc_chk_range(th, 2, 3);
  ovm_obj_regexp_t re = ovm_inst_reval(th, &argv[0]);
  ovm_obj_str_t s = ovm_inst_strval(th, &argv[1]);
  ovm_intval_t n = 0;
  if (argc == 3) {
    n  = ovm_inst_intval(th, &argv[2]);
    if (n < 0)  ovm_except_inv_value(th, &argv[2]);
  }

  regmatch_t m[n];
  if (regexec(re->re, s->data, n, m, 0) != 0) {
    ovm_inst_assign_obj(dst, 0);
    return;
  }
  
  unsigned i, k;
  for (i = 0; i < n && m[i].rm_so >= 0; ++i);
  k = i;
  
  ovm_inst_t work = ovm_stack_alloc(th, 3);
  
  ovm_inst_assign_obj(&work[-3], ovm_consts.Array);
  ovm_int_newc(&work[-2], k);
  ovm_method_callsch(th, &work[-3], OVM_STR_CONST_HASH(new), 2);
  for (i = 0; i < k; ++i) {
    ovm_int_newc(&work[-2], i);
    ovm_str_newc(&work[-1], m[i].rm_eo + 1 - m[i].rm_so, s->data + m[i].rm_so);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);
  }
  
  ovm_inst_assign(dst, &work[-3]);
}

CM_DECL(match1)
{
  ovm_method_argc_chk_exact(th, 3);

  ovm_inst_t work = ovm_stack_alloc(th, 2);

  ovm_inst_assign_obj(&work[-2], my_class);
  ovm_inst_assign(&work[-1], &argv[1]);
  ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(new), 2);
  ovm_inst_assign(&work[-1], &argv[2]);
  ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(match), 2);
}

static struct {
  unsigned   nm_size;
  const char *nm;
  unsigned   hash;
  int        val;
} class_vars[] = {
#define CLASS_VAR_INIT(_nm)  { OVM_STR_CONST_HASH(_nm), _nm }
    { _OVM_STR_CONST_HASH("#REG_EXTENDED"), REG_EXTENDED },
    { _OVM_STR_CONST_HASH("#REG_ICASE"), REG_ICASE },
    { _OVM_STR_CONST_HASH("#REG_NEWLINE"), REG_NEWLINE },
    { _OVM_STR_CONST_HASH("#REG_NOTBOL"), REG_NOTBOL },
    { _OVM_STR_CONST_HASH("#REG_NOTEOL"), REG_NOTEOL },
    { _OVM_STR_CONST_HASH("#REG_BADBR"), REG_BADBR },
    { _OVM_STR_CONST_HASH("#REG_BADPAT"), REG_BADPAT },
    { _OVM_STR_CONST_HASH("#REG_BADRPT"), REG_BADRPT },
    { _OVM_STR_CONST_HASH("#REG_EBRACE"), REG_EBRACE },
    { _OVM_STR_CONST_HASH("#REG_EBRACK"), REG_EBRACK },
    { _OVM_STR_CONST_HASH("#REG_ECOLLATE"), REG_ECOLLATE },
    { _OVM_STR_CONST_HASH("#REG_ECTYPE"), REG_ECTYPE },
    { _OVM_STR_CONST_HASH("#REG_EESCAPE"), REG_EESCAPE },
    { _OVM_STR_CONST_HASH("#REG_EPAREN"), REG_EPAREN },
    { _OVM_STR_CONST_HASH("#REG_ERANGE"), REG_ERANGE },
    { _OVM_STR_CONST_HASH("#REG_ESPACE"), REG_ESPACE },
    { _OVM_STR_CONST_HASH("#REG_ESUBREG"), REG_ESUBREG }
#if 0
    ,
    { _OVM_STR_CONST_HASH("#REG_EEND"), REG_EEND },
    { _OVM_STR_CONST_HASH("#REG_ESIZ"), REG_ESIZ },
#endif
};

void __regexp_init__(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
  ovm_inst_t old = th->sp;

  ovm_stack_push(th, &argv[0]);

  ovm_stack_push_obj(th, ovm_consts.Object);
  ovm_class_new(th, OVM_STR_CONST_HASH(Regexp), 0, regexp_cleanup, regexp_cleanup);
  my_class = th->sp->objval;

  ovm_inst_t work = ovm_stack_alloc(th, 3);
  
  ovm_inst_assign(&work[-3], &work[0]);
  unsigned i;
  for (i = 0; i < ARRAY_SIZE(class_vars); ++i) {
    ovm_str_newch(&work[-2], class_vars[i].nm_size, class_vars[i].nm, class_vars[i].hash);
    ovm_int_newc(&work[-1], class_vars[i].val);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);
  }

  ovm_stack_unwind(th, work);

  ovm_classmethod_add(th, OVM_STR_CONST_HASH(new), OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(new));
  ovm_classmethod_add(th, OVM_STR_CONST_HASH(match), OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(match1));
  ovm_method_add(th, OVM_STR_CONST_HASH(match), OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(match));

  ovm_stack_unwind(th, old);
}
