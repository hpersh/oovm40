#!/usr/bin/python

# Compiler pass 4 - Optimize VM code
#
# - Remove dead code
# - Collapse stack alloc/free

import sys
import xml.etree.ElementTree as et

outf = None

def copy(nd):
    return et.Element(nd.tag, attrib=nd.attrib)

def optim(m):
    mm = copy(m)
    outf.append(mm)
    last = None
    dead = False
    for s in m:
        if s.tag == 'label':
            dead = False
        if dead:
            continue
        if (s.tag == 'stack_alloc' and last is not None and last.tag == 'stack_alloc'
            or s.tag == 'stack_free' and last is not None and last.tag == 'stack_free'
            ):
            last.set('size', str(int(last.get('size')) + int(s.get('size'))))
            continue
        if s.tag == 'stack_alloc' and last is not None and last.tag == 'stack_free':
            last = et.Element('stack_free_alloc', attrib={'size_free': last.get('size'), 'size_alloc': s.get('size')})
            continue
        if s.tag == 'stack_alloc' and last is not None and last.tag == 'stack_free_alloc':
            last.set('size_alloc', str(int(last.get('size_alloc')) + int(s.get('size'))))
            continue
        if s.tag == 'ret' and last is not None and last.tag == 'stack_free':
            last = None
        if s.tag in ['jmp', 'ret']:
            dead = True
        if last is not None:
            mm.append(last)
        last = s
    if last is not None:
        mm.append(last)
        
def process_file(infile):
    r = et.parse(open(infile)).getroot()
    global outf
    outf = copy(r)
    for m in r:
        optim(m)
    et.ElementTree(outf).write(sys.stdout)

def main():
    process_file(sys.argv[1])

if __name__ == '__main__':
    main()
