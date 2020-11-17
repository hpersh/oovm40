#!/usr/bin/python

# Compiler pass 2 - Optimize parse tree
#
# - Flatten operations like add, sub, etc.
# - Collect constants in arithmetic expressions
# - Highlight assignments to local variables

import sys
import copy
import xml.etree.ElementTree as et

outf = None

line_num = 0

def copy_node(nd):
    return et.Element(nd.tag, attrib = nd.attrib)

def node_is_num(nd):
    return nd.tag in ('int', 'float')

def node_intval(nd):
    return int(nd.get('val'))

def node_floatval(nd):
    return float(nd.get('val'))

def num_from_node(nd):
    if nd.tag == 'int':
        return node_intval(nd)
    if nd.tag == 'float':
        return node_floatval(nd)
    assert(False)

def int_node(val):
    return et.Element('int', attrib = {'val': str(val), 'line': line_num})

def float_node(val):
    return et.Element('float', attrib = {'val': str(val), 'line': line_num})

def node_from_num(val):
    t = type(val)
    if t == int:
        return int_node(val)
    if t == float:
        return float_node(val)
    assert(False)

def num_node_eq(nd, val):
    t = type(val)
    if t == int:
        return nd.tag == 'int' and node_intval(nd) == val
    if t == float:
        return nd.tag == 'float' and node_floatval(nd) == val
    assert(False)

def num_node_op(op, arg):
    return node_from_num(op(num_from_node(arg)))
    
def num_node_binop(op, arg1, arg2):
    return node_from_num(op(num_from_node(arg1), num_from_node(arg2)))
    
def simp_minus(nd):
    ch = simp_node(nd[0])
    if node_is_num(ch):
        return num_node_op(lambda x: -x, ch)
    if ch.tag == 'minus':
        return ch[0]
    return nd

def flatten(nd):
    ch = [simp_node(x) for x in nd]
    result = copy_node(nd)
    for c in ch:
        if c.tag == nd.tag:
            for cc in c:
                result.append(cc)
        else:
            result.append(c)
    return result
    
def nums_collect(nd, func_combine, func_test):
    num = None
    result = copy_node(nd)
    for c in nd:
        if node_is_num(c):
            if num is None:
                num = c
            else:
                num = num_node_binop(func_combine, num, c)
        else:
            result.append(c)
    if num is not None and func_test(num):
        result.append(num)
    return result

def simp_add(nd):
    temp = nums_collect(flatten(nd), lambda x,y: x + y, lambda x: not (num_node_eq(x, 0) or num_node_eq(x, 0.0)))
    ch = list(temp)
    result = copy_node(temp)
    last = ch[-1]
    if node_is_num(last):
        num = last
        ch = ch[:-1]
    else:
        num = int_node(0)
    for c in ch:
        if c.tag != 'sub':
            result.append(c)
            continue
        last = c[-1]
        if not node_is_num(last):
            result.append(c)
            continue
        num = num_node_binop(lambda x,y: x - y, num, last)
        c.remove(last)
        if len(c) == 1:
            result.append(c[0])
            continue
        result.append(c)
    if not (num_node_eq(num, 0) or num_node_eq(num, 0.0)):
        if len(result) == 0:
            return num
        result.append(num)
    return result

def simp_sub(nd):
    a = nd[0]
    b = nd[1]
    if node_is_num(a) and node_is_num(b):
        return node_from_num(num_from_node(a) - num_from_node(b))
    if num_node_eq(b, 0) or num_node_eq(b, 0.0):
        return a
    if num_node_eq(a, 0) or num_node_eq(a, 0.0):
        if node_is_num(b):
            return node_from_num(-num_from_node(b))
        return et.Element('minus', attrib={'line': line_num}).append(b)
    return nd

def simp_mul(nd):
    result = nums_collect(flatten(nd), lambda x,y: x * y, lambda x: not (num_node_eq(x, 1) or num_node_eq(x, 1.0)))
    last = result[-1]
    if num_node_eq(last, 0) or num_node_eq(last, 0.0):
        return last
    return result

def simp_div(nd):
    a = nd[0]
    b = nd[1]
    if (num_node_eq(a, 0) or num_node_eq(a, 0.0)) and not (num_node_eq(b, 0) or num_node_eq(b, 0.0)):
        return a
    return nd

def simp_land(nd):
    return flatten(nd)

def simp_lor(nd):
    return flatten(nd)

def simp_band(nd):
    return flatten(nd)

def simp_bor(nd):
    return flatten(nd)

def simp_bxor(nd):
    return flatten(nd)

def simp_anon(nd):
    x = et.Element('anon', attrib = nd.attrib)
    x.append(nd[0])
    parse_node(x, nd[1])
    return x

def simp_func(nd):
    x = et.Element('func', attrib = nd.attrib)
    x.append(nd[0])
    parse_node(x, nd[1])
    return x

def simp_default(nd):
    return nd

def simp_node(nd):
    f = 'simp_' + nd.tag
    return (globals().get(f, simp_default))(nd)

def parse_minus(parent, nd):
    parent.append(simp_mius(nd))
    
def parse_add(parent, nd):
    parent.append(simp_add(nd))

def parse_sub(parent, nd):
    parent.append(simp_sub(nd))

def parse_mul(parent, nd):
    parent.append(simp_mul(nd))

def parse_land(parent, nd):
    parent.append(simp_land(nd))

def parse_lor(parent, nd):
    parent.append(simp_lor(nd))

def parse_band(parent, nd):
    parent.append(simp_band(nd))

def parse_bor(parent, nd):
    parent.append(simp_bor(nd))

def parse_bxor(parent, nd):
    parent.append(simp_bxor(nd))

def parse_assign(parent, nd):
    lhs = nd[0]
    rhs = simp_node(nd[1])
    if lhs.tag != 'obj1':
        parse_node_default(parent, nd)
        return
    if rhs.tag in ['nil', 'bool', 'int', 'float', 'str']:
        t = 'assign1c'
    elif rhs.tag == 'obj1':
        t = 'assign11'
    else:
        t = 'assign1'
    x = et.Element(t, attrib = nd.attrib)
    x.append(lhs)
    x.append(rhs)
    parent.append(x)

def parse_module(parent, nd):
    global outf
    outf = copy_node(nd)
    for c in nd:
        parse_node(outf, c)

def parse_node_default(parent, nd):
    nd2 = copy_node(nd)
    for c in nd:
        parse_node(nd2, c)
    parent.append(nd2)

def parse_node(parent, nd):
    line_num_ = nd.get('line')
    if line_num_ is not None:
        global line_num
        line_num = line_num_
    f = 'parse_' + nd.tag
    (globals().get(f, parse_node_default))(parent, nd)

def process_file(infile):
    parse_node(None, et.parse(open(infile)).getroot())
    et.ElementTree(outf).write(sys.stdout)

def main():
    process_file(sys.argv[1])

if __name__ == '__main__':
    main()
