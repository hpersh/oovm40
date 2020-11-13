#!/usr/bin/python

# Compiler pass 3 - Generate VM code

import sys
import copy
import xml.etree.ElementTree as et

anon = et.Element('anon')
body = et.Element('body')
init = None


class Struct:
    def __init__(self, d = {}):
        self.__dict__ = d

    def __repr__(self):
        return str(self.__dict__)
    

cstack = []
cstack_lvl = 0

def cstack_top():
    return None if len(cstack) == 0 else cstack[0]
    
def cstack_push(x):
    global cstack
    global cstack_lvl
    x.lvl = cstack_lvl
    cstack = [x] + cstack
    cstack_lvl += 1
    return x

def _cstack_pop():
    global cstack
    global cstack_lvl
    cstack = cstack[1:]
    cstack_lvl -= 1

def cstack_pop(fr):
    assert(cstack_top() is fr)
    _cstack_pop()
    
def cstack_popn(fr):
    while True:
        t = cstack_top()
        assert(t is not None)
        _cstack_pop()
        if t is fr:
            break

CSTACK_TYPE_BLOCK  = 0
CSTACK_TYPE_METHOD = 1
CSTACK_TYPE_CLASS  = 2
CSTACK_TYPE_NS     = 3
CSTACK_TYPE_EXCEPT = 4
CSTACK_TYPE_BREAK  = 5
CSTACK_TYPE_LOOP   = 6

def block_current():
    for x in cstack:
        if x.type == CSTACK_TYPE_BLOCK:
            return x
        if x.type == CSTACK_TYPE_METHOD:
            break
    return None

def block_push():
    b = block_current()
    return cstack_push(Struct({'type': CSTACK_TYPE_BLOCK,
                               'ofs': 0 if b is None else b.ofs - b.size,
                               'size': 0,
                               'vars': {}
                               }
                              ) 
                       )

def block_pop(outf, fr, noclean=False):
    cstack_pop(fr)
    if noclean:
        return
    gen_stack_free(outf, fr.size)

def _block_var_add(outf, nm, _pass):
    b = block_current()
    b.size += 1
    ofs = b.ofs - b.size
    b.vars[nm] = Struct({'ofs': ofs, '_pass': _pass})
    outf.append(et.Comment(' {}: {} '.format(nm, ofs)))
    return ofs

def _block_var_find(nm):
    for x in cstack:
        if x.type == CSTACK_TYPE_BLOCK and nm in x.vars:
            return x.vars[nm]
        if x.type == CSTACK_TYPE_METHOD:
            break
    return None

def block_var_add_soft(outf, nm):
    if _block_var_find(nm) is None:
        _block_var_add(outf, nm, 1)
        return True
    return False

def block_var_add_hard(outf, nm):
    _block_var_add(outf, nm, 2)

def block_var_mark_defined(nm):
    v = _block_var_find(nm)
    v._pass = 2
    return v.ofs
    
def block_var_find(nm, _pass = 2):
    v = _block_var_find(nm)
    return None if v is None or v._pass < _pass else v.ofs

def method_current():
    for x in cstack:
        if x.type == CSTACK_TYPE_METHOD:
            return x
    return None

def method_push(outf, nm):
    return cstack_push(Struct({'type': CSTACK_TYPE_METHOD, 'funcname': nm}))
    
def class_current():
    for x in cstack:
        if x.type == CSTACK_TYPE_CLASS:
            return x.node
    return None

def class_push(nd):
    return cstack_push(Struct({'type': CSTACK_TYPE_CLASS, 'node': nd}))

def ns_push(nd):
    return cstack_push(Struct({'type': CSTACK_TYPE_NS, 'node': nd}))
    
def ns_current():
    for x in cstack:
        if x.type == CSTACK_TYPE_NS:
            return x.node
    return None

def except_push():
    return cstack_push(Struct({'type': CSTACK_TYPE_EXCEPT}))

def break_push(subtype):
    return cstack_push(Struct({'type': CSTACK_TYPE_BREAK, 'subtype': subtype, 'exit_label': label_new(), 'exit_label_used': False}))

def break_pop(outf, fr):
    cstack_pop(fr)
    if fr.exit_label_used:
        gen_label(outf, fr.exit_label)

def loop_push(subtype, continue_label):
    return cstack_push(Struct({'type': CSTACK_TYPE_LOOP, 'subtype': subtype, 'continue_label': continue_label, 'continue_label_used': False}))
    
        
modname = ''

progname = sys.argv[0]

err_cnt = 0

def fini():
    sys.exit(err_cnt)

def error(nd, mesg, stop = False):
    sys.stderr.write('{}: Error, line {}: {}\n'.format(progname, nd.get('line'), mesg))
    global err_cnt
    err_cnt += 1
    if stop:
        fini()

label_num = 0

def label_new():
    global label_num
    label_num += 1
    result = 'label{}'.format(label_num)
    return result


# Valid forms for 'dst'
# 'ap[x]'
# 'bp[x]'
# 'sp[x]'
# 'push'
# 'dst'
# None
        
def dst_from_ofs(ofs):
    return '{}[{}]'.format('ap' if ofs >= 0 else 'bp', ofs)

def dst_adj(dst, n):
    return 'sp[{}]'.format(int(dst[3:-1]) + n) if dst is not None and dst[0:2] == 'sp' else dst

def gen_stack_alloc(outf, n):
    if n == 0:
        return
    et.SubElement(outf, 'stack_alloc', attrib={'size': str(n)})
    
def gen_stack_free(outf, n):
    if n == 0:
        return
    et.SubElement(outf, 'stack_free', attrib={'size': str(n)})

def gen_stack_push(outf, src):
    et.SubElement(outf, 'stack_push', attrib={'src': src})

def gen_nil(outf, dst):
    if dst == 'push':
        et.SubElement(outf, 'nil_push')
        return
    et.SubElement(outf, 'nil_assign', attrib={'dst': dst})    

def gen_bool_newc(outf, dst, val):
    if dst == 'push':
        et.SubElement(outf, 'bool_pushc', attrib={'val': str(val)})    
        return
    et.SubElement(outf, 'bool_newc', attrib={'dst': dst, 'val': str(val)})    

def gen_int_newc(outf, dst, val):
    if dst == 'push':
        et.SubElement(outf, 'int_pushc', attrib={'val': str(val)})    
        return
    et.SubElement(outf, 'int_newc', attrib={'dst': dst, 'val': str(val)})    

def gen_float_newc(outf, dst, val):
    if dst == 'push':
        et.SubElement(outf, 'float_pushc', attrib={'val': str(val)})    
        return
    et.SubElement(outf, 'float_newc', attrib={'dst': dst, 'val': str(val)})    

def gen_method_newc(outf, dst, funcname):
    if dst == 'push':
        et.SubElement(outf, 'method_pushc', attrib={'func': funcname})    
        return
    et.SubElement(outf, 'method_newc', attrib={'dst': dst, 'func': funcname})    

def gen_str_newc(outf, dst, val):
    if dst == 'push':
        et.SubElement(outf, 'str_pushc', attrib={'val': val})    
        return
    et.SubElement(outf, 'str_newc', attrib={'dst': dst, 'val': val})    

def gen_str_newch(outf, dst, val):
    if dst == 'push':
        et.SubElement(outf, 'str_pushch', attrib={'val': val})    
        return
    et.SubElement(outf, 'str_newch', attrib={'dst': dst, 'val': val})    

def gen_inst_assign(outf, dst, src):
    et.SubElement(outf, 'inst_assign', attrib={'dst': dst, 'src': src})

def gen_method_call(outf, dst, sel, argc):
    et.SubElement(outf, 'method_call', attrib={'dst': dst, 'sel': sel, 'argc': str(argc)})

def gen_environ_at(outf, dst, nm):
    if dst == 'push':
        et.SubElement(outf, 'environ_at_push', attrib={'name': nm})
        return
    et.SubElement(outf, 'environ_at', attrib={'dst': dst, 'name': nm})

def gen_label(outf, label):
    et.SubElement(outf, 'label', attrib={'name': label})
    
def gen_jmp(outf, label):
    et.SubElement(outf, 'jmp', attrib={'label': label})

def gen_jt(outf, src, label):
    et.SubElement(outf, 'jt', attrib={'src': src, 'label': label})
    
def gen_jf(outf, src, label):
    et.SubElement(outf, 'jf', attrib={'src': src, 'label': label})
    
def gen_jx(outf, label):
    et.SubElement(outf, 'jx', attrib={'label': label})
    
def gen_popjt(outf, label):
    et.SubElement(outf, 'popjt', attrib={'label': label})
    
def gen_popjf(outf, label):
    et.SubElement(outf, 'popjf', attrib={'label': label})
    
def gen_return(outf):
    et.SubElement(outf, 'ret')

def gen_retd(outf):
    et.SubElement(outf, 'retd')

def gen_except_raise(outf, src):
    et.SubElement(outf, 'except_raise', attrib={'src': src})

def gen_except_push(outf, var):
    et.SubElement(outf, 'except_push', attrib={'var': var})
    
def gen_except_pop(outf, cnt):
    if cnt == 0:
        return
    et.SubElement(outf, 'except_pop', attrib={'cnt': str(cnt)})
    
def gen_except_reraise(outf):
    et.SubElement(outf, 'except_reraise')

def parse_nil(outf, dst, nd):
    gen_nil(outf, dst)

def parse_bool(outf, dst, nd):
    gen_bool_newc(outf, dst, nd.get('val'))

def parse_int(outf, dst, nd):
    gen_int_newc(outf, dst, nd.get('val'))

def parse_float(outf, dst, nd):
    gen_float_newc(outf, dst, nd.get('val'))
    
def parse_str(outf, dst, nd):
    gen_str_newc(outf, dst, nd.get('val'))

def parse_atmodule(outf, dst, nd):
    gen_str_newc(outf, dst, modname)
    
def parse_atns(outf, dst, nd):
    ns = ns_current()
    gen_str_newc(outf, dst, modname if ns is None else ns[0].get('val'))
    
def parse_atclass(outf, dst, nd):
    cl = class_current()
    if cl is None:
        error(nd, '@class expression not within class')
        s = ''
    else:
        s = cl[0].get('val')
    gen_str_newc(outf, dst, s)

def parse_atmethod(outf, dst, nd):
    m = method_current()
    if m is None:
        error(nd, '@method expression not within method')
        s = ''
    else:
        s = m.funcname.split('$')[-1]
    gen_str_newc(outf, dst, s)
        
def parse_pair(outf, dst, nd):
    if dst is None:
        return
    gen_stack_alloc(outf, 3)
    gen_environ_at(outf, 'sp[0]', '#Pair')
    parse_node(outf, 'sp[1]', nd[0])
    parse_node(outf, 'sp[2]', nd[1])
    gen_method_call(outf, 'sp[2]' if dst == 'push' else dst_adj(dst, 3), 'new', 3)
    gen_stack_free(outf, 2 if dst == 'push' else 3)

def parse_list(outf, dst, nd):
    if dst is None:
        return
    ch = nd[0]
    n = len(ch)
    if n == 0:
        gen_nil(outf, dst)
        return
    gen_stack_alloc(outf, 2)
    for c in ch:
        parse_node(outf, 'sp[1]', c)
        gen_method_call(outf, 'sp[0]', 'cons', 2)
    gen_method_call(outf, 'sp[1]' if dst == 'push' else dst_adj(dst, 2), 'reverse', 1)
    gen_stack_free(outf, 1 if dst == 'push' else 2)

def parse_array(outf, dst, nd):
    if dst is None:
        return
    n = len(nd[0])
    gen_int_newc(outf, 'push', n)
    gen_environ_at(outf, 'push', '#Array')
    gen_method_call(outf, 'sp[0]', 'new', 2)
    gen_stack_push(outf, 'sp[0]')
    i = 0
    for c in nd[0]:
        gen_int_newc(outf, 'sp[1]', i)
        parse_node(outf, 'sp[2]', c)
        gen_method_call(outf, 'sp[2]', 'atput', 3)
        i += 1
    if dst == 'push':
        gen_inst_assign(outf, 'sp[2]', 'sp[0]')
        gen_stack_free(outf, 2)
        return
    gen_inst_assign(outf, dst_adj(dst, 3), 'sp[0]')
    gen_stack_free(outf, 3)

def parse_set(outf, dst, nd):
    if dst is None:
        return
    gen_environ_at(outf, 'push', '#Set')
    gen_method_call(outf, 'sp[0]', 'new', 1)
    gen_stack_push(outf, 'sp[0]')
    for c in nd[0]:
        parse_node(outf, 'sp[1]', c)
        gen_method_call(outf, 'sp[1]', 'put', 2)
    if dst == 'push':
        gen_inst_assign(outf, 'sp[1]', 'sp[0]')
        gen_stack_free(outf, 1)
        return
    gen_inst_assign(outf, dst_adj(dst, 2), 'sp[0]')
    gen_stack_free(outf, 2)

def parse_dict(outf, dst, nd):
    if dst is None:
        return
    gen_environ_at(outf, 'push', '#Dictionary')
    gen_method_call(outf, 'sp[0]', 'new', 1)
    gen_stack_alloc(outf, 2)
    gen_inst_assign(outf, 'sp[0]', 'sp[2]')
    for c in nd[0]:
        k = c[0]
        if k.tag == 'str':
            gen_str_newch(outf, 'sp[1]', k.get('val'))
        else:
            parse_node(outf, 'sp[1]', k)
        parse_node(outf, 'sp[2]', c[1])
        gen_method_call(outf, 'sp[2]', 'atput', 3)
    if dst == 'push':
        gen_inst_assign(outf, 'sp[2]', 'sp[0]')
        gen_stack_free(outf, 2)
        return
    gen_inst_assign(outf, dst_adj(dst, 3), 'sp[0]')
    gen_stack_free(outf, 3)

def parse_multiop(outf, dst, nd, op):
    gen_stack_alloc(outf, 2)
    wdst = 'sp[1]' if dst is None or dst == 'push' else dst_adj(dst, 2)
    parse_node(outf, 'sp[0]', nd[0])
    n = len(nd) - 1
    i = 1
    while n > 0:
        parse_node(outf, 'sp[1]', nd[i])
        gen_method_call(outf, 'sp[0]' if n > 1 else wdst, op, 2)
        i += 1
        n -= 1
    gen_stack_free(outf, 1 if dst == 'push' else 2)

def parse_add(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'add')

def parse_sub(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'sub')

def parse_mul(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'mul')

def parse_div(outf, dst, nd):
    parse_node(outf, 'push', nd[1])
    parse_node(outf, 'push', nd[0])
    gen_method_call(outf, 'sp[1]' if dst is None or dst == 'push' else dst_adj(dst, 2), 'div', 2)
    gen_stack_free(outf, 1 if dst == 'push' else 2)
    
def parse_band(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'band')

def parse_bor(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'bor')

def parse_land_lor(outf, dst, nd, f):
    label_done = label_new()
    gen_stack_alloc(outf, 1)
    n = len(nd)
    i = 0
    while n > 0:
        parse_node(outf, 'sp[0]', nd[i])
        if n > 1:
            f(outf, 'sp[0]', label_done)
        i += 1
        n -= 1
    gen_label(outf, label_done)
    if dst is None:
        gen_stack_free(outf, 1)
        return
    if dst == 'push':
        return
    gen_inst_assign(outf, dst_adj(dst, 1), 'sp[0]')
    gen_stack_free(outf, 1)

def parse_land(outf, dst, nd):
    parse_land_lor(outf, dst, nd, gen_jf)

def parse_lor(outf, dst, nd):
    parse_land_lor(outf, dst, nd, gen_jt)

def parse_lnot(outf, dst, nd):
    parse_node(outf, 'push', nd[0])
    gen_method_call(outf, 'sp[0]' if dst is None or dst == 'push' else dst_adj(dst, 1), 'not', 1)
    if dst != 'push':
        gen_stack_free(outf, 1)
    
def parse_equal(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'equal')

def parse_notequal(outf, dst, nd):
    gen_stack_alloc(outf, 2)
    parse_node(outf, 'sp[0]', nd[0])
    parse_node(outf, 'sp[1]', nd[1])
    gen_method_call(outf, 'sp[1]', 'equal', 2)
    gen_stack_free(outf, 1)
    gen_method_call(outf, 'sp[0]' if dst is None or dst == 'push' else dst_adj(dst, 1), 'not', 1)
    gen_stack_free(outf, 0 if dst == 'push' else 1)

def parse_lt(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'lt')    

def parse_gt(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'gt')    

def parse_le(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'le')    

def parse_ge(outf, dst, nd):
    parse_multiop(outf, dst, nd, 'ge')    

def parse_obj1(outf, dst, nd):
    if dst is None:
        return
    nm = nd[0].get('val')
    ofs = block_var_find(nm)
    if ofs is None:
        gen_environ_at(outf, dst, nm)
        return
    src = dst_from_ofs(ofs)
    if dst == 'push':
        gen_stack_push(outf, src)
        return
    gen_inst_assign(outf, dst, src)

def parse_obj2(outf, dst, nd):
    if dst is None or dst == 'push':
        gen_stack_alloc(outf, 1)
        wdst = 'sp[0]'
    else:
        wdst = dst
    k = nd[1]
    if k.tag == 'str':
        gen_str_newch(outf, 'push', k.get('val'))
    else:
        parse_node(outf, 'push', k)
    parse_node(outf, 'push', nd[0])
    gen_method_call(outf, dst_adj(wdst, 2), 'at', 2)
    gen_stack_free(outf, 2)
    if dst is None:
        gen_stack_free(outf, 1)        
    
def parse_obj2e(outf, dst, nd):
    if dst is None or dst == 'push':
        gen_stack_alloc(outf, 1)
        wdst = 'sp[0]'
    else:
        wdst = dst
    gen_str_newch(outf, 'push', nd[1].get('val'))
    parse_node(outf, 'push', nd[0])
    gen_method_call(outf, dst_adj(wdst, 2), 'ate', 2)
    gen_stack_free(outf, 2)
    if dst is None:
        gen_stack_free(outf, 1)        
    
def parse_module_or_class_assign(outf, dst, nd):
    parse_node(outf, 'push', nd[1])
    lvar = nd[0][0].get('val')
    gen_str_newch(outf, 'push', lvar)
    gen_stack_push(outf, 'sp[2]')
    gen_method_call(outf, 'sp[2]', 'atput', 3)
    gen_stack_free(outf, 3)

def parse_assign11(outf, dst, nd):
    if method_current() is None:
        parse_module_or_class_assign(outf, dst, nd)
        return
    rvar = nd[1][0].get('val')
    lvar = nd[0][0].get('val')
    rofs = block_var_find(rvar)
    ldst = dst_from_ofs(block_var_mark_defined(lvar))
    if rofs is None:
        gen_environ_at(outf, ldst, rvar)
        return
    gen_inst_assign(outf, ldst, dst_from_ofs(rofs))

def parse_assign1c(outf, dst, nd):
    if method_current() is None:
        parse_module_or_class_assign(outf, dst, nd)
        return
    rhs = nd[1]
    rval = rhs.get('val')
    ldst = dst_from_ofs(block_var_mark_defined(nd[0][0].get('val')))
    rtag = rhs.tag
    if rtag == 'nil':
        gen_nil(outf, ldst)
    elif rtag == 'bool':
        gen_bool_newc(outf, ldst, rval)
    elif rtag == 'int':
        gen_int_newc(outf, ldst, rval)
    elif rtag == 'float':
        gen_float_newc(outf, ldst, rval)
    elif rtag == 'str':
        gen_str_newc(outf, ldst, rval)
    else:
        assert(False)

def parse_assign1(outf, dst, nd):
    if method_current() is None:
        parse_module_or_class_assign(outf, dst, nd)
        return
    rhs = nd[1]
    lvar = nd[0][0].get('val')
    lofs = block_var_find(lvar, 1)
    parse_node(outf, dst_from_ofs(lofs), rhs)
    block_var_mark_defined(lvar)

def parse_assign(outf, dst, nd):
    assert(nd[0].tag in ['obj2', 'obj2e'])
    parse_node(outf, 'push', nd[1])
    if nd[0].tag == 'obj2':
        parse_node(outf, 'push', nd[0][1])
    else:
        gen_str_newch(outf, 'push', nd[0][1].get('val'))
    parse_node(outf, 'push', nd[0][0])
    gen_method_call(outf, 'sp[2]', 'atput', 3)
    gen_stack_free(outf, 3)

def parse_methodcall(outf, dst, nd):
    args = nd[2]
    argc = 1 + len(args)
    if dst is None or dst == 'push':
        gen_stack_alloc(outf, 1);
        wdst = 'sp[0]'
    else:
        wdst = dst
    gen_stack_alloc(outf, argc)
    wdst = dst_adj(wdst, argc)
    parse_node(outf, 'sp[0]', nd[0])
    i = 1
    for a in args:
        parse_node(outf, 'sp[{}]'.format(i), a)
        i += 1
    gen_method_call(outf, wdst, nd[1].get('val'), argc)
    gen_stack_free(outf, argc)
    if dst is None:
        gen_stack_free(outf, 1)

def block_scan(outf, nd):
    for s in nd:
        if s.tag[0:7] == 'assign1':
            lvar = s[0][0].get('val')
            block_var_add_soft(outf, lvar)
            continue
        if s.tag == 'var':
            for c in s:
                if c.tag == 'sym':
                    block_var_add_hard(outf, c.get('val'))
                    continue
                if c.tag[0:7] == 'assign1':
                    block_var_add_hard(outf, c[0][0].get('val'))
                    continue
                assert(False)
            continue
        if s.tag == 'for' or s.tag[0:3] == 'try':
            block_var_add_soft(outf, s[0].get('val'))
            continue

def parse_break(outf, dst, nd):
    n = int(nd.get('val'))
    if n > 0:
        stack_cleanup = 0
        except_cleanup = 0
        for c in cstack:
            if c.type == CSTACK_TYPE_METHOD:
                break
            if c.type == CSTACK_TYPE_BLOCK:
                stack_cleanup += c.size
                continue
            if c.type == CSTACK_TYPE_EXCEPT:
                except_cleanup += 1
                stack_cleanup = 0
                continue
            if c.type == CSTACK_TYPE_BREAK:
                n -= 1
                if n == 0:
                    gen_except_pop(outf, except_cleanup)
                    gen_stack_free(outf, stack_cleanup)
                    gen_jmp(outf, c.exit_label)
                    c.exit_label_used = True
                    return
                continue
    error(nd, 'Invalid break count')

def parse_condexpr(outf, dst, nd):
    label_false = label_new()
    label_done = label_new()
    parse_node(outf, 'push', nd[0])
    gen_popjf(outf, label_false)
    parse_node(outf, dst, nd[1])
    gen_jmp(outf, label_done)
    gen_label(outf, label_false)
    parse_node(outf, dst, nd[2])
    gen_label(outf, label_done)

def _parse_if(outf, dst, nd, f):
    has_else = (len(nd) > 2)
    label_else = label_new()
    label_end  = label_new()
    parse_node(outf, 'push', nd[0])
    f(outf, label_else if has_else else label_end)
    parse_node(outf, None, nd[1])
    if has_else:
        gen_jmp(outf, label_end)
        gen_label(outf, label_else)
        parse_node(outf, None, nd[2])
    gen_label(outf, label_end)

def parse_if(outf, dst, nd):
    _parse_if(outf, dst, nd, gen_popjf)

def parse_ifnot(outf, dst, nd):
    _parse_if(outf, dst, nd, gen_popjt)

def parse_cond(outf, dst, nd):
    fr = break_push('cond')
    parse_node(outf, dst, nd[0])
    break_pop(outf, fr)

def parse_for(outf, dst, nd): 
    fr_break = break_push('for')
    fr_block = block_push()
    label_continue = label_new()
    fr_loop = loop_push('for', label_continue)
    parse_node(outf, 'push', nd[1])
    gen_method_call(outf, 'sp[0]', 'List', 1)
    fr_block.size = 1
    var = nd[0].get('val')
    if block_var_add_soft(outf, var):
        get_stack_push(outf, 'sp[0]')
    vofs = block_var_mark_defined(var)
    vdst = dst_from_ofs(vofs)
    label_loop = label_new()
    label_done = label_new()
    gen_label(outf, label_loop)
    gen_stack_push(outf, 'sp[0]')
    gen_method_call(outf, 'sp[0]', 'isnil', 1)
    gen_popjt(outf, label_done)
    gen_method_call(outf, vdst, 'car', 1)
    parse_node(outf, None, nd[2])
    if fr_loop.continue_label_used:
        gen_label(outf, label_continue)
    gen_method_call(outf, 'sp[0]', 'cdr', 1)
    gen_jmp(outf, label_loop)
    gen_label(outf, label_done)
    cstack_pop(fr_loop)
    block_pop(outf, fr_block)
    break_pop(outf, fr_break)

def parse_loop(outf, dst, nd):
    fr_break = break_push('loop')
    label_loop = label_new()
    fr_loop = loop_push('loop', label_loop)
    gen_label(outf, label_loop)
    parse_node(outf, dst, nd[0])
    gen_jmp(outf, label_loop)
    cstack_pop(fr_loop)
    break_pop(outf, fr_break)
    
def parse_while(outf, dst, nd):
    fr_break = break_push('while')
    label_begin = label_new()
    label_loop = label_new()
    fr_loop = loop_push('while', label_begin)
    gen_jmp(outf, label_begin)
    gen_label(outf, label_loop)
    parse_node(outf, dst, nd[1])
    gen_label(outf, label_begin)
    parse_node(outf, 'push', nd[0])
    gen_popjt(outf, label_loop)
    cstack_pop(fr_loop)
    break_pop(outf, fr_break)

def parse_until(outf, dst, nd):
    fr_break = break_push('while')
    label_begin = label_new()
    label_loop = label_new()
    fr_loop = loop_push('while', label_begin)
    gen_jmp(outf, label_begin)
    gen_label(outf, label_loop)
    parse_node(outf, dst, nd[1])
    gen_label(outf, label_begin)
    parse_node(outf, 'push', nd[0])
    gen_popjf(outf, label_loop)
    cstack_pop(fr_loop)
    break_pop(outf, fr_break)

def parse_continue(outf, dst, nd):
    stack_cleanup = 0
    except_cleanup = 0
    for c in cstack:
        if c.type == CSTACK_TYPE_METHOD:
            break
        if c.type == CSTACK_TYPE_BLOCK:
            stack_cleanup += c.size
            continue
        if c.type == CSTACK_TYPE_EXCEPT:
            except_cleanup += 1
            stack_cleanup = 0
            continue
        if c.type == CSTACK_TYPE_LOOP:
            gen_except_pop(outf, except_cleanup)
            gen_stack_free(outf, stack_cleanup)
            c.continue_label_used = True
            gen_jmp(outf, c.continue_label)
            return
    error(nd, 'continue not within for/while/until/loop')

def parse_return(outf, dst, nd):
    if len(nd) == 0:
        gen_retd(outf)
        return
    parse_node(outf, 'dst', nd[0])
    gen_return(outf)

def parse_var(outf, dst, nd):
    for c in nd:
        if c.tag[0:7] == 'assign1':
            parse_node(outf, dst, c)

def parse_block(outf, dst, nd, noclean=False):
    fr = block_push()
    block_scan(outf, nd)
    gen_stack_alloc(outf, block_current().size)
    for s in nd:
        parse_node(outf, dst, s)
    block_pop(outf, fr, noclean)

def _parse_try(outf, dst, nd, anyc = None, nonec = None):
    fr_block = block_push()
    var = nd[0].get('val')
    ofs = block_var_mark_defined(var)
    label_ex = label_new()
    label_cleanup = label_new()
    label_done = label_new()
    gen_except_push(outf, dst_from_ofs(ofs))
    gen_jx(outf, label_ex)
    fr_except = except_push()
    parse_node(outf, None, nd[1])
    gen_except_pop(outf, 1)
    if nonec is not None:
        parse_node(outf, None, nonec)
    gen_jmp(outf, label_done)
    gen_label(outf, label_ex)
    for c in nd[2]:
        if len(c) == 1:
            parse_node(outf, None, c[0])
            gen_jmp(outf, label_cleanup)
            continue
        label_catch_no = label_new()
        parse_node(outf, 'push', c[0])
        gen_popjf(outf, label_catch_no)
        parse_node(outf, None, c[1])
        gen_jmp(outf, label_cleanup)
        gen_label(outf, label_catch_no)
    gen_except_reraise(outf)
    gen_label(outf, label_cleanup)
    gen_except_pop(outf, 1)
    cstack_pop(fr_except)
    if anyc is not None:
        parse_node(outf, None, anyc)
    gen_label(outf, label_done)
    block_pop(outf, fr_block)

def parse_try(outf, dst, nd):
    _parse_try(outf, dst, nd)

def parse_trynone(outf, dst, nd):
    _parse_try(outf, dst, nd, None, nd[3])

def parse_tryany(outf, dst, nd):
    _parse_try(outf, dst, nd, nd[3])

def parse_tryanynone(outf, dst, nd):
    _parse_try(outf, dst, nd, nd[3], nd[4])

def parse_raise(outf, dst, nd):
    parse_node(outf, 'push', nd[0])
    gen_except_raise(outf, 'sp[0]')
    
def parse_reraise(outf, dst, nd):
    gen_except_reraise(outf)
    
def method_func_name(nm):
    result = nm
    for fr in cstack:
        x = None
        if fr.type in (CSTACK_TYPE_CLASS, CSTACK_TYPE_NS):
            x = fr.node[0].get('val')
        if fr.type == CSTACK_TYPE_METHOD:
            x = fr.funcname
        if x:
            result = '{}{}{}'.format(x, '$' if result else '', result)
        if fr.type == CSTACK_TYPE_METHOD:
            return result
    return modname + '$' + result

def __parse_method(outf, dst, nm, funcname, args, body):
    x = et.SubElement(outf, 'func', attrib={'name': funcname, 'argc': str(len(args)), 'visibility': 'private'})
    method_fr = method_push(x, funcname)
    block_fr = block_push()
    v = {}
    ofs = 0
    for a in args:
        if a.tag == 'sym':
            argname = a.get('val')
            v[argname] = Struct({'ofs': ofs, '_pass': 2})
            x.append(et.Comment(' {}: {} '.format(argname, ofs)))
            ofs += 1
        elif a.tag == 'methodarrayarg':
            argname = a[0].get('val')
            ofs = -1
            v[argname] = Struct({'ofs': ofs, '_pass': 2})
            block_fr.size = 1
            x.append(et.Comment(' {}: {} '.format(argname, ofs)))
            x.set('arrayarg', '1')
    block_fr.vars = v
    parse_block(x, None, body, True)
    block_pop(x, block_fr, True)
    gen_retd(x)
    cstack_pop(method_fr)

def _parse_method(outf, dst, nd):
    nm = nd[0][0].get('val')
    funcname = method_func_name(nm)
    __parse_method(outf, dst, nm, funcname, nd[0][1], nd[1])
    gen_stack_alloc(init, 3)
    gen_inst_assign(init, 'sp[0]', 'sp[3]')
    gen_method_call(init, 'sp[0]', 'classmethods' if nd.tag == 'clmethod' else 'methods', 1)
    gen_str_newch(init, 'sp[1]', nm)
    gen_method_newc(init, 'sp[2]', funcname)
    gen_method_call(init, 'sp[2]', 'atput', 3)
    gen_stack_free(init, 3)

def parse_method(outf, dst, nd):
    _parse_method(body, dst, nd)

def parse_clmethod(outf, dst, nd):
    _parse_method(body, dst, nd)

anon_num = 0
    
def parse_anon(outf, dst, nd):
    global anon_num
    anon_num += 1
    funcname = '{}$__anon__${}'.format(modname, anon_num)
    __parse_method(anon, dst, funcname, funcname, nd[0], nd[1])
    gen_codemethod_newc(outf, dst, funcname)

def parse_func(outf, dst, nd):
    funcname = method_func_name(nd[0][0].get('val'))
    __parse_method(anon, dst, funcname, funcname, nd[0][1], nd[1])
    gen_codemethod_newc(outf, dst, funcname)

def parse_recurse(outf, dst, nd):
    gen_codemethod_newc(outf, dst, method_func_name(''))    
    
ifaces = {}

def parse_iface(outf, dst, nd):
    ifaces[nd[0].get('val')] = nd

def class_scan(nd):
    methods = {}
    clmethods = {}
    clvars = []
    for c in nd[3]:
        if c.tag == 'method':
            methods[c[0][0].get('val')] = c[0]
            continue
        if c.tag == 'clmethod':
            clmethods[c[0][0].get('val')] = c[0]
            continue
        if c.tag[0:7] == 'assign1':
            clvars.append(c[0][0].get('val'))
    return Struct({'methods': methods, 'clmethods': clmethods, 'clvars': clvars})

def method_def_match(nd1, nd2):
    if nd1.tag != nd2.tag:
        return False
    a1 = copy.copy(nd1.attrib)
    del a1['line']
    if nd1.tag == 'sym':
        del a1['val']
    a2 = copy.copy(nd2.attrib)
    del a2['line']
    if nd2.tag == 'sym':
        del a2['val']
    if a1 != a2:
        return False
    ch1 = list(nd1)
    ch2 = list(nd2)
    n1 = len(ch1)
    if n1 != len(ch2):
        return False
    i = 0
    while i < n1:
        if not method_def_match(ch1[i], ch2[i]):
            return False
        i += 1
    return True

def method_def_check(clinfo, md):
    method_nm = md[0].get('val')
    d = clinfo.clmethods if md.tag == 'clmethoddecl' else clinfo.methods
    if method_nm not in d:
        return False
    return method_def_match(d[method_nm], md)

def class_implements_iface(nd, clinfo, iface_nm):
    clnm = nd[0].get('val')
    if iface_nm not in ifaces:
        error(nd, 'Undefined interface {}'.format(iface_nm))
        return False
    iface = ifaces[iface_nm]
    result = True
    for c in iface[1]:
        if not class_implements(nd, clinfo, c.get('val')):
            result = False
    for c in iface[2]:
        if c.tag in ('methoddecl', 'clmethoddecl'):
            if not method_def_check(clinfo, c):
                error(nd, 'Class {} does not implement {} {}'.format(clnm, 'method' if c.tag == 'method' else 'class method', c[0].get('val')))
                result = False
            continue
        if c.tag == 'clvars':
            for s in c[0]:
                clvar = s.get('val')
                if clvar not in clinfo.clvars:
                    error(nd, 'Class {} does not define class variable {}'.format(clnm, clvar))
                    result = False
            continue
        assert(False)
    if not result:
        error(nd, 'Class {} does not implement interface {}'.format(clnm, iface_nm))
    return result

def parse_parent(outf, dst, nd):
    parse_node(outf, dst, class_current()[1])

def parse_class(outf, dst, nd):
    nm = nd[0].get('val')
    if len(nd[2]) > 0:
        clinfo = class_scan(nd)
        for c in nd[2]:
            class_implements_iface(nd, clinfo, c.get('val'))
    gen_stack_alloc(init, 5)
    gen_environ_at(init, 'sp[0]', '#Metaclass')
    gen_str_newch(init, 'sp[1]', nm)
    parse_node(init, 'sp[2]', nd[1])
    gen_inst_assign(init, 'sp[3]', 'ap[0]')
    gen_method_call(init, 'sp[4]', 'new', 4)
    gen_stack_free(init, 4)
    fr = class_push(nd)
    for c in nd[3]:
        parse_node(outf, dst, c)
    cstack_pop(fr)
    gen_stack_free(init, 1)

def parse_namespace(outf, dst, nd):
    fr = ns_push(nd)
    gen_str_newc(init, 'push', nd[0].get('val'))
    gen_environ_at(init, 'push', '#Namespace')
    gen_method_call(init, 'sp[1]', 'new', 3)
    gen_stack_free(init, 1)
    for c in nd[1]:
        parse_node(outf, dst, c)
    gen_stack_free(init, 1)
    cstack_pop(fr)

def parse_module(outf, dst, nd):
    global modname
    modname = nd.get('val')
    global init
    init = et.Element('func', attrib={'name': '__{}_init__'.format(modname), 'argc': '1', 'visibility': 'public'})
    for c in nd:
        parse_node(init, dst, c)
    gen_retd(init)

def parse_node(outf, dst, nd):
    outf.append(et.Comment(' Line {} '.format(nd.get('line'))))
    exec('parse_' + nd.tag + '(outf, dst, nd)')    

def process_file(infile):
    parse_node(body, None, et.parse(open(infile)).getroot())
    r = et.Element('module', attrib={'name': modname})
    for x in [anon, body]:
        for c in x:
            r.append(c)
    r.append(init)
    et.ElementTree(r).write(sys.stdout)

def main():
    process_file(sys.argv[1])
    fini()
    
if __name__ == "__main__":
    main()
    
