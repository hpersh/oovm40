#!/usr/bin/python

# Compiler pass 5 - Generate C code file from VM code

import sys
import xml.etree.ElementTree as et

bp_used = False

def gen_src_dst(src_dst):
    if src_dst == 'dst':
        return 'dst'
    if src_dst[0:2] == 'sp':
        return '&th->{}'.format(src_dst)
    if src_dst[0:2] == 'bp':
        global bp_used
        bp_used = True
        return '&__{}'.format(src_dst)
    if src_dst[0:2] == 'ap':
        return '&argv{}'.format(src_dst[2:])
    assert(False)

def gen_stack_alloc(outf, nd):
    outf.write('ovm_stack_alloc(th, {});\n'.format(nd.get('size')))

def gen_stack_free(outf, nd):
    outf.write('ovm_stack_free(th, {});\n'.format(nd.get('size')))

def gen_stack_free_alloc(outf, nd):
    outf.write('ovm_stack_free_alloc(th, {}, {});\n'.format(nd.get('size_free'), nd.get('size_alloc')))

def gen_method_call(outf, nd):
    outf.write('ovm_method_callsch(th, {}, _OVM_STR_CONST_HASH("{}"), {});\n'.format(gen_src_dst(nd.get('dst')), nd.get('sel'), nd.get('argc')))

def gen_nil_assign(outf, nd):
    outf.write('ovm_inst_assign_obj({}, 0);\n'.format(gen_src_dst(nd.get('dst'))))

def gen_nil_push(outf, nd):
    outf.write('ovm_stack_push_obj(th, 0);\n')

def gen_inst_assign(outf, nd):
    outf.write('ovm_inst_assign({}, {});\n'.format(gen_src_dst(nd.get('dst')), gen_src_dst(nd.get('src'))))
    
def gen_stack_push(outf, nd):
    outf.write('ovm_stack_push(th, {});\n'.format(gen_src_dst(nd.get('src'))))
    
def gen_bool_newc(outf, nd):
    outf.write('ovm_bool_newc({}, {});\n'.format(gen_src_dst(nd.get('dst')), 1 if nd.get('val') == '#true' else 0))

def gen_bool_pushc(outf, nd):
    outf.write('ovm_bool_pushc(th, {});\n'.format(1 if nd.get('val') == '#true' else 0))

def gen_int_newc(outf, nd):
    outf.write('ovm_int_newc({}, {});\n'.format(gen_src_dst(nd.get('dst')), nd.get('val')))

def gen_int_pushc(outf, nd):
    outf.write('ovm_int_pushc(th, {});\n'.format(nd.get('val')))

def gen_float_newc(outf, nd):
    outf.write('ovm_float_newc({}, {});\n'.format(gen_src_dst(nd.get('dst')), nd.get('val')))

def gen_float_pushc(outf, nd):
    outf.write('ovm_float_pushc(th, {});\n'.format(nd.get('val')))

def gen_codemethod_newc(outf, nd):
    outf.write('ovm_codemethod_newc({}, {});\n'.format(gen_src_dst(nd.get('dst')), nd.get('func')))
    
def gen_codemethod_pushc(outf, nd):
    outf.write('ovm_codemethod_pushc(th, {});\n'.format(nd.get('func')))

def gen_str_newc(outf, nd):
    outf.write('ovm_str_newc({}, _OVM_STR_CONST("{}"));\n'.format(gen_src_dst(nd.get('dst')), nd.get('val')))

def gen_str_pushc(outf, nd):
    outf.write('ovm_str_pushc(th, _OVM_STR_CONST("{}"));\n'.format(nd.get('val')))

def gen_str_newch(outf, nd):
    outf.write('ovm_str_newch({}, _OVM_STR_CONST_HASH("{}"));\n'.format(gen_src_dst(nd.get('dst')), nd.get('val')))

def gen_str_pushch(outf, nd):
    outf.write('ovm_str_pushch(th, _OVM_STR_CONST_HASH("{}"));\n'.format(nd.get('val')))

def gen_label(outf, nd):
    outf.write('{}: ;\n'.format(nd.get('name')))
    
def gen_popjt(outf, nd):
    outf.write('if (ovm_bool_if(th))  goto {};\n'.format(nd.get('label')))

def gen_popjf(outf, nd):
    outf.write('if (!ovm_bool_if(th))  goto {};\n'.format(nd.get('label')))

def gen_jt(outf, nd):
    outf.write('if (ovm_inst_boolval(th, {}))  goto {};\n'.format(gen_src_dst(nd.get('src')), nd.get('label')))
    
def gen_jf(outf, nd):
    outf.write('if (!ovm_inst_boolval(th, {}))  goto {};\n'.format(gen_src_dst(nd.get('src')), nd.get('label')))
    
def gen_jmp(outf, nd):
    outf.write('goto {};\n'.format(nd.get('label')))

def gen_environ_at(outf, nd):
    outf.write('ovm_environ_atc(th, {}, _OVM_STR_CONST_HASH("{}"));\n'.format(gen_src_dst(nd.get('dst')), nd.get('name')))

def gen_environ_at_push(outf, nd):
    outf.write('ovm_environ_atc_push(th, _OVM_STR_CONST_HASH("{}"));\n'.format(nd.get('name')))

def gen_except_push(outf, nd):
    outf.write('setjmp(ovm_frame_except_push(th, {}));\n'.format(gen_src_dst(nd.get('var'))))

def gen_except_pop(outf, nd):
    outf.write('ovm_frame_except_pop(th, {});\n'.format(nd.get('cnt')))

def gen_jx(outf, nd):
    outf.write('if (ovm_except_chk(th))  goto {};\n'.format(nd.get('label')))

def gen_except_raise(outf, nd):
    outf.write('ovm_except_raise(th, {});\n'.format(gen_src_dst(nd.get('src'))))
 
def gen_except_reraise(outf, nd):
    outf.write('ovm_except_reraise(th);\n')

def gen_ret(outf, nd):
    outf.write('return;\n')

def gen_retd(outf, nd):
    outf.write('ovm_inst_assign(dst, &argv[0]);\n')
    outf.write('return;\n')

def gen_class_add(outf, nd):
    outf.write('ovm_user_class_new(th, _OVM_STR_CONST_HASH("{}"));\n'.format(nd.get('name')))

def gen_method_add(outf, nd):
    outf.write('ovm_method_add(th, _OVM_STR_CONST_HASH("{}"), OVM_INST_TYPE_CODEMETHOD, {});\n'.format(nd.get('name'), nd.get('func')))
   
def gen_classmethod_add(outf, nd):
    outf.write('ovm_classmethod_add(th, _OVM_STR_CONST_HASH("{}"), OVM_INST_TYPE_CODEMETHOD, {});\n'.format(nd.get('name'), nd.get('func')))
   
def gen_node(outf, nd):    
    exec('gen_{}(outf, nd)'.format(nd.tag))

def func_decl(outf, f):
    outf.write('void {}(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)'.format(f.get('name')))

def process_file(infile):
    r = et.parse(open(infile)).getroot()
    sys.stdout.write('#include "oovm.h"\n')
    for f in r:
        if f.get('visibility') == 'private':
            func_decl(sys.stdout, f)
            sys.stdout.write(';\n')
    for f in r:
        global bp_used
        bp_used = False
        # Scan for bp used
        with open('/dev/null', 'r+') as outf_null:
            for s in f:
                gen_node(outf_null, s)
        func_decl(sys.stdout, f)
        sys.stdout.write('\n{\n')
        if bp_used:
            sys.stdout.write('ovm_inst_t __bp = th->sp;\n');
        argc = f.get('argc')
        if f.get('arrayarg') is None:
            sys.stdout.write('ovm_method_argc_chk_exact(th, {});\n'.format(argc))
        else:
            sys.stdout.write('ovm_method_array_arg_push(th, {});\n'.format(int(argc) - 1))
        for s in f:
            gen_node(sys.stdout, s)
        sys.stdout.write('}\n')

def main():
    process_file(sys.argv[1])

if __name__ == '__main__':
    main()
