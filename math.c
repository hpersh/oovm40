/***************************************************************************
 *
 * math module
 * 
 * - Add methods for math functions and constants to Float class
 *
 ***************************************************************************/

#include "oovm.h"

#include <math.h>

#define METHOD_MODULE  math
#define METHOD_CLASS   Float

#define MF(f) \
  ovm_method_argc_chk_exact(th, 1); \
  ovm_float_newc(dst, f(ovm_inst_floatval(th, &argv[0])))

CM_DECL(acos)
{
  MF(acosl);
}

CM_DECL(asin)
{
  MF(asinl);
}

CM_DECL(atan)
{
  MF(atanl);
}

CM_DECL(atan2)
{
  ovm_method_argc_chk_exact(th, 2);
  ovm_float_newc(dst, atan2l(ovm_inst_floatval(th, &argv[0]), ovm_inst_floatval(th, &argv[1])));
}

CM_DECL(cos)
{
  MF(cosl);
}

CM_DECL(sin)
{
  MF(sinl);
}

CM_DECL(tan)
{
  MF(tanl);
}

CM_DECL(exp)
{
  MF(expl);
}

CM_DECL(exp10)
{
  MF(exp10l);
}

CM_DECL(log)
{
  MF(logl);
}

CM_DECL(log10)
{
  MF(log10l);
}

CM_DECL(sqrt)
{
  MF(sqrtl);
}

void __math_init__(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
  ovm_inst_t old = th->sp;

  ovm_stack_push_obj(th, ovm_consts.Float);

  ovm_method_add(th, OVM_STR_CONST_HASH(acos),  OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(acos));
  ovm_method_add(th, OVM_STR_CONST_HASH(asin),  OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(asin));
  ovm_method_add(th, OVM_STR_CONST_HASH(atan),  OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(atan));
  ovm_method_add(th, OVM_STR_CONST_HASH(atan2), OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(atan2));
  ovm_method_add(th, OVM_STR_CONST_HASH(cos),   OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(cos));
  ovm_method_add(th, OVM_STR_CONST_HASH(sin),   OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(sin));
  ovm_method_add(th, OVM_STR_CONST_HASH(tan),   OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(tan));
  ovm_method_add(th, OVM_STR_CONST_HASH(exp),   OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(exp));
  ovm_method_add(th, OVM_STR_CONST_HASH(exp10), OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(exp10));
  ovm_method_add(th, OVM_STR_CONST_HASH(log),   OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(log));
  ovm_method_add(th, OVM_STR_CONST_HASH(log10), OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(log10));
  ovm_method_add(th, OVM_STR_CONST_HASH(sqrt),  OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(sqrt));

  ovm_stack_alloc(th, 2);
  ovm_inst_t w = th->sp;

  ovm_inst_assign(&w[0], &w[2]);
  ovm_str_newch(&w[1], OVM_STR_CONST_HASH(pi));
  ovm_float_newc(&w[2], M_PIl);
  ovm_method_callsch(th, &w[2], OVM_STR_CONST_HASH(atput), 3);
  ovm_str_newch(&w[1], OVM_STR_CONST_HASH(e));
  ovm_float_newc(&w[2], M_El);
  ovm_method_callsch(th, &w[2], OVM_STR_CONST_HASH(atput), 3);
  
  ovm_stack_unwind(th, old);
}

void __math_fini__(void)
{
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(acos));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(asin));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(atan));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(atan2));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(cos));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(sin));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(tan));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(exp));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(exp10));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(log));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(log10));
  ovm_method_del(OVM_CL_FLOAT, OVM_STR_CONST_HASH(sqrt));
}
