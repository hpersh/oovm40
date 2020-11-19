#!/usr/bin/python

# Compiler pass 5 - Generate VM instructions

import sys
import xml.etree.ElementTree as et
import zlib


def byte(x, n):
    return int((x >> (8 * n)) & 0xff)

def gen_int(n, force=None, isize=5):
    sh = isize - 1
    b = 0xe0
    for i in xrange(6):
        r = i + 1
        if force == r or force is None and n >= (-1 << sh) and n < (1 << sh):
            b = i << 5
            break
        sh += 8
    if b == 0xe0:
        return [0xe0] + [byte(n, i) for i in reversed(range(8))]
    li = [byte(n, i) for i in reversed(range(r))]
    li[0] = (li[0] & ((1 << isize) - 1)) | b
    return li

def gen_uint(n, force=None):
    m = 1 << 5
    b = 0xe0
    for i in xrange(6):
        r = i + 1
        if force == r or force is None and n < m:
            b = i << 5
            break
        m <<= 8
    if b == 0xe0:
        return [0xe0] + [byte(n, i) for i in reversed(range(8))]
    li = [byte(n, i) for i in reversed(range(r))]
    li[0] |= b
    return li

def gen_uint32(n):
    return [byte(n, i) for i in reversed(range(4))]

def str_to_bytes(s):
    result = []
    n = len(s)
    i = 0
    while i < n:
        c = s[i]
        if c == '\\':
            i += 1
            c = s[i]
            if c == 'n':
                c = '\n'
            elif c == 'r':
                c = '\r'
            elif c == 't':
                c = '\t'
        result.append(ord(c))
        i += 1
    result.append(0)
    return result

def gen_str(s):
    li = str_to_bytes(s)
    return gen_uint(len(li)) + li

def gen_str_hash(s):
    return gen_str(s) + gen_uint32(zlib.crc32(s))

src_dst_base_regs = {'sp': 0, 'bp': 1 << 3, 'ap': 2 << 3}

def gen_src_dst(src_dst):
    if src_dst == 'dst':
        return [0x18]
    li = gen_int(int(src_dst[3:-1]), None, 3)
    li[0] |= src_dst_base_regs[src_dst[0:2]]
    return li

code = []
cur_loc = 0
symbols_dict = {}
refs_dict = {}

def code_write(ofs, li):
    n = len(li)
    assert (ofs + n) <= len(code)
    for i in xrange(n):
        code[ofs + i] = li[i]

def code_append(nd, li):
    global code
    code += li
    n = len(li)
    global cur_loc
    nd.set('len', str(n))
    cur_loc += n

def code_delete(ofs, n):
    global code
    code = code[0:ofs] + code[(ofs + n):]
    
def gen_ref_ofs(to, from_):
    n = 1
    while True:
        ofs = gen_int(to - (from_ + n))
        if len(ofs) == n:
            break
        n = (n + 1) if n < 7 else 9
    return ofs

def symbol_add(nm):
    assert nm not in symbols_dict
    symbols_dict[nm] = cur_loc

def symbol_ref_add(nd, li, s):
    r = cur_loc + len(li)
    ofs = gen_ref_ofs(symbols_dict[s], r) if s in symbols_dict else 9 * [0]
    refs_dict[r] = [s, len(ofs), nd]
    return li + ofs

def symbols_dump():
    d = {}
    for r, li in refs_dict.items():
        d.setdefault(li[0], []).append(r)
    for s, ofs in sorted(symbols_dict.items(), key=lambda x: x[1]):
        print '{}: 0x{:08x}'.format(s, ofs)
        print '\trefs: {}'.format(', '.join(['0x{:08x}'.format(r) for r in sorted(d.get(s, []))]))
        
def gen_stack_free(nd):
    code_append(nd, [0x01] + gen_uint(int(nd.get('size'))))

def gen_stack_alloc(nd):
    code_append(nd, [0x02] + gen_uint(int(nd.get('size'))))

def gen_stack_free_alloc(nd):
    code_append(nd, [0x03] + gen_uint(int(nd.get('size_free'))) + gen_uint(int(nd.get('size_alloc'))))

def gen_stack_clear(nd):
    code_append(nd, [0x04] + gen_uint(int(nd.get('size'))))

def gen_inst_assign(nd):
    code_append(nd, [0x05] + gen_src_dst(nd.get('dst')) + gen_src_dst(nd.get('src')))
    
def gen_stack_push(nd):
    code_append(nd, [0x06] + gen_src_dst(nd.get('src')))
    
def gen_method_call(nd):
    code_append(nd, [0x10] + gen_src_dst(nd.get('dst')) + gen_str_hash(nd.get('sel')) + gen_uint(int(nd.get('argc'))))

def gen_ret(nd):
    code_append(nd, [0x11])

def gen_retd(nd):
    code_append(nd, [0x12])

def gen_except_push(nd):
    code_append(nd, [0x20] + gen_src_dst(nd.get('var')))

def gen_except_raise(nd):
    code_append(nd, [0x21] + gen_src_dst(nd.get('src')))
 
def gen_except_reraise(nd):
    code_append(nd, [0x22])

def gen_except_pop(nd):
    n = int(nd.get('cnt'))
    if n == 1:
        code_append(nd, [0x23])
        return
    code_append(nd, [0x24] + gen_uint(n))

def gen_jf(nd):
    code_append(nd, symbol_ref_add(nd, [0x30], nd.get('label')))

def gen_jt(nd):
    code_append(nd, symbol_ref_add(nd, [0x31], nd.get('label')))

def gen_popjf(nd):
    code_append(nd, symbol_ref_add(nd, [0x32], nd.get('label')))

def gen_popjt(nd):
    code_append(nd, symbol_ref_add(nd, [0x33], nd.get('label')))

def gen_jx(nd):
    code_append(nd, symbol_ref_add(nd, [0x34], nd.get('label')))

def gen_jmp(nd):
    code_append(nd, symbol_ref_add(nd, [0x35], nd.get('label')))

def gen_environ_at(nd):
    code_append(nd, [0x40] + gen_src_dst(nd.get('dst')) + gen_str_hash(nd.get('name')))

def gen_environ_at_push(nd):
    code_append(nd, [0x41] + gen_str_hash(nd.get('name')))

def gen_nil_assign(nd):
    code_append(nd, [0x50] + gen_src_dst(nd.get('dst')))

def gen_nil_push(nd):
    code_append(nd, [0x51])

def gen_bool_newc(nd):
    code_append(nd, [0x53 if nd.get('val') == '#true' else  0x52] + gen_src_dst(nd.get('dst')))

def gen_bool_pushc(nd):
    code_append(nd, [0x55 if nd.get('val') == '#true' else 0x54])

str_to_int_ldrs_to_base = {'0x': 16, '0b': 2}
    
def gen_int_newc(nd):
    code_append(nd, [0x56] + gen_src_dst(nd.get('dst')) + gen_int(int(nd.get('val'), 0)))

def gen_int_pushc(nd):
    code_append(nd, [0x57] + gen_int(int(nd.get('val'), 0)))

def gen_float_newc(nd):
    code_append(nd, [0x58] + gen_src_dst(nd.get('dst')) + gen_str(float(nd.get('val')).hex()))

def gen_float_pushc(nd):
    code_append(nd, [0x59] + gen_str(float(nd.get('val')).hex()))

def gen_method_newc(nd):
    code_append(nd, symbol_ref_add(nd, [0x5a] + gen_src_dst(nd.get('dst')), nd.get('func')))

def gen_method_pushc(nd):
    code_append(nd, symbol_ref_add(nd, [0x5b], nd.get('func')))
    
def gen_str_newc(nd):
    code_append(nd, [0x5c] + gen_src_dst(nd.get('dst')) + gen_str(nd.get('val')))

def gen_str_pushc(nd):
    code_append(nd, [0x5d] + gen_str(nd.get('val')))

def gen_str_newch(nd):
    code_append(nd, [0x5e] + gen_src_dst(nd.get('dst')) + gen_str_hash(nd.get('val')))

def gen_str_pushch(nd):
    code_append(nd, [0x5f] + gen_str_hash(nd.get('val')))

def gen_argc_chk(nd):
    code_append(nd, [0x70] + gen_uint(int(nd.get('argc'))))

def gen_array_arg_push(nd):
    code_append(nd, [0x71] + gen_uint(int(nd.get('argc')) - 1))
    
def gen_label(nd):
    symbol_add(nd.get('name'))
    
def gen_node(nd):    
    exec('gen_{}(nd)'.format(nd.tag))

def func_decl(f):
    symbol_add(f.get('name'))

def refs_fixup():
    againf = True
    while againf:
        againf = False
        for r in reversed(sorted(refs_dict.keys())):
            s, rsize, nd = refs_dict[r]
            ofs = gen_ref_ofs(symbols_dict[s], r)
            n = len(ofs)
            if code[r:(r + n)] != ofs:
                code_write(r, ofs)
            xs = rsize - n
            if xs == 0:
                continue
            code_delete(r + n, xs)
            refs_dict[r][1] = n
            nd.set('len', str(int(nd.get('len')) - xs))
            againf = True
            # Adjust all symbols above shuffle point
            for s, ofs in symbols_dict.items():
                if ofs < r:
                    continue
                symbols_dict[s] = ofs - xs
            # Adjust all references above shuffle point
            for rr in sorted(filter(lambda x: x > r, refs_dict.keys())):
                s, rsize, nd = refs_dict[rr]
                del refs_dict[rr]
                refs_dict[rr - xs] = [s, rsize, nd]

listing_ofs = 0
                
def listing_node(nd):
    t = nd.tag
    if t == 'label':
        sys.stdout.write('{}:\n'.format(nd.get('name')))
        return
    if t == 'func':
        sys.stdout.write('\n{}:'.format(nd.get('name')))
    else:
        sys.stdout.write('\t{}'.format(t))
        sep = '\t'
        for k in ['dst', 'src', 'sel', 'argc', 'name', 'label', 'func', 'val', 'size', 'size_free', 'size_alloc']:
            v = nd.attrib.get(k)
            if v is None:
                continue
            sys.stdout.write('{}{}'.format(sep, v))
            sep = ','
    n = int(nd.get('len'))
    global listing_ofs
    sys.stdout.write('\n{:08x} '.format(listing_ofs))
    while n > 0:
        sys.stdout.write('{:02x} '.format(code[listing_ofs]))
        listing_ofs += 1
        n -= 1
    sys.stdout.write('\n')
    
def listing_dump(nd):
    for f in nd:
        listing_node(f)
        for s in f:
            listing_node(s)
    
def output_write(nd):
    sys.stdout.write('const unsigned char __{}_code__[] = '.format(nd.get('name')))
    sys.stdout.write('{')
    i = 0
    k = 0
    for b in code:
        if k == 0:
            sys.stdout.write('\n/* 0x{:08x} */ '.format(i))
        sys.stdout.write('0x{:02x}, '.format(b))
        i += 1
        k = (k + 1) & 0x07;
    sys.stdout.write('\n};\n')
    sys.stdout.write('/*\nListing\n')
    listing_dump(nd)
    sys.stdout.write('*/\n')
    sys.stdout.write('/*\nSymbol table\n\n')
    symbols_dump()
    sys.stdout.write('*/\n')
    
def process_file(infile):
    r = et.parse(open(infile)).getroot()
    for f in r:
        func_decl(f)
        if f.get('arrayarg') is None:
            gen_argc_chk(f)
        else:
            gen_array_arg_push(f)
        for s in f:
            gen_node(s)
    refs_fixup()
    output_write(r)
    

def main():
    process_file(sys.argv[1])

if __name__ == '__main__':
    main()
