m4_changecom(`@@@')
m4_define(`__LQ__', `m4_changequote(<,>)`m4_dnl'
m4_changequote`'')
m4_define(`__RQ__', `m4_changequote(<,>)m4_dnl`
'm4_changequote`'')

m4_define(`__str_hash__',  `m4_esyscmd(./oovm_hash `$1')')
m4_define(`__str_hash2__', `m4_esyscmd(./oovm_hash __RQ__()m4_patsubst(m4_patsubst($1, `^"'), `"$')`'__RQ__())')

