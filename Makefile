include config.mk

BISONFLAGS = -d
FLEXFLAGS  = -d
CC	= gcc
CPP	= $(CC) -E
LT_CC	= libtool --tag=CC --mode=compile $(CC) -c
RPATH	= /usr/local/lib
LT_LD	= libtool --tag=CC --mode=link $(CC) -rpath $(RPATH) -rdynamic
CPLUSPLUS = g++ -std=c++11
M4	= m4 -P
CHK	= splint

BINS = oovm oovm_hash ovmc1 
CLIBS = math thread regexp socket process dns datetime
OVMS = lib test2 perf2 http thread_test regexp_test socket_test process_test dns_test datetime_test math_test xml ctype

.PHONY: all clean check doc uninstall builddeps

all: oovm_hash .libs/liboovm.so oovm ovmc1 $(foreach f,$(CLIBS) $(OVMS),.libs/liboovm$(f).so)

clean:
	rm -fr $(BINS) $(foreach f,$(BINS),$(f).exe) *.so *.o grammar.tab.* lex.yy.* oovm_ovm*.c oovm.s $(foreach f,$(CLIBS),$(f)_ovm*.c $(f).s) $(foreach f,$(OVMS),$(f)*.xml $(f)*.[cs]) gmon.out *.la *.lo .libs

.PRECIOUS: %.c

OOVM_INCLUDES	= oovm.h oovm_internal.h oovm_types.h oovm_dllist.h oovm_thread.h

ovmc1: scanner.l grammar.y ovmc1.cc ovmc.h
	bison $(BISONFLAGS) -t grammar.y
	flex $(FLEXFLAGS) scanner.l
	$(CC) $(CFLAGS) -Wno-unused-function -I /usr/include/libxml2 -c grammar.tab.c lex.yy.c
	$(CPLUSPLUS) $(CFLAGS) -I /usr/include/libxml2 ovmc1.cc grammar.tab.o lex.yy.o -o ovmc1 -lxml2

oovm: oovm_main.c $(OOVM_INCLUDES)
	$(CC) $(CFLAGS) oovm_main.c -rdynamic -o oovm -L.libs -loovm -lz -lpthread -ldl

oovm_hash: oovm_hash.c oovm_hash.h
	$(CC) $(CFLAGS) oovm_hash.c -o oovm_hash -lz

.libs/liboovm.so: oovm.c  $(OOVM_INCLUDES)
	$(CPP) oovm.c >oovm_ovm1.c
	$(M4) oovmpp.m4 oovm_ovm1.c >oovm_ovm2.c
	$(LT_CC) $(CFLAGS) -Wa,-ahls=oovm.s oovm_ovm2.c -o oovm.o
	$(LT_LD) -o liboovm.la oovm.lo -lz -lpthread -ldl

.libs/liboovmmath.so: math.c $(OOVM_INCLUDES) oovmpp.m4 oovm_hash
	$(CPP) $< >math_ovm1.c
	$(M4) oovmpp.m4 math_ovm1.c >math_ovm2.c
	$(LT_CC) $(CFLAGS) math_ovm2.c -o math.o
	$(LT_LD) -o liboovmmath.la math.lo -L.libs -loovm -lm

.libs/liboovm%.so: %.c $(OOVM_INCLUDES) oovmpp.m4 oovm_hash
	$(CPP) $< >$*_ovm1.c
	$(M4) oovmpp.m4 $*_ovm1.c >$*_ovm2.c
	$(LT_CC) $(CFLAGS) -Wa,-ahls=$*.s $*_ovm2.c -o $*.o
	$(LT_LD) -o liboovm$*.la $*.lo -L.libs -loovm

%.c: %.ovm
	./ovmc1 $< > $*.xml
	./ovmc2.py $*.xml > $*_2.xml
	./ovmc3.py $*_2.xml > $*_3.xml
	./ovmc4.py $*_3.xml > $*_4.xml
	./ovmc5.py $*_4.xml > $*.c

MONOLITHIC_MODULE	= perf2

monolithic: oovm_main.c oovm.c $(OOVM_INCLUDES) oovmpp.m4 oovm_hash $(MONOLITHIC_MODULE)_ovm2.c
	$(CPP) -DMONOLITHIC oovm.c >oovm_ovm1.c
	$(M4) oovmpp.m4 oovm_ovm1.c >oovm_ovm2.c
	$(CC) $(CFLAGS) -Wa,-ahls=monolithic.s -rdynamic oovm_main.c oovm_ovm2.c $(MONOLITHIC_MODULE)_ovm2.c -o monolithic -lz -lpthread -ldl

check:
	# $(CHK) -preproc -warnposix oovm.c $(foreach f,$(CLIBS),$(f).c) 
	LD_LIBRARY_PATH=.libs OVM_MODULE_PATH=.libs ./oovm test2

ARCH	= $(shell gcc -dumpmachine)

INSTALL_LIBS	= liboovm liboovmmath liboovmthread liboovmregexp liboovmsocket liboovmprocess liboovmdns liboovmdatetime liboovmhttp liboovmxml liboovmctype
INSTALL_LIBS_DIR	= $(INSTALL_ROOT)/lib/$(ARCH)

INSTALL_BINS_LIB	= ovmc1 ovmc2.py ovmc3.py ovmc4.py ovmc5.py oovm_hash
INSTALL_BINS_LIB_DIR	= $(INSTALL_ROOT)/libexec/$(ARCH)

INSTALL_SHARES	= oovmpp.m4
INSTALL_SHARES_DIR	= $(INSTALL_ROOT)/share/oovm

INSTALL_BINS	= ovmc oovm
INSTALL_BINS_DIR	= $(INSTALL_ROOT)/bin

INSTALL_INCLUDES	= $(OOVM_INCLUDES)
INSTALL_INCLUDES_DIR	= $(INSTALL_ROOT)/include/oovm

uninstall:
	for f in $(INSTALL_LIBS); do \
	    rm -f $(INSTALL_LIBS_DIR)/$$f.*; \
	done; \
	for f in $(INSTALL_BINS_LIB); do \
	    rm -f $(INSTALL_BINS_LIB_DIR)/$$f; \
	done; \
	rm -fr $(INSTALL_SHARES_DIR)
	for f in $(INSTALL_BINS); do \
	    rm -f $(INSTALL_BINS_DIR)/$$f; \
	done; \
	rm -fr $(INSTALL_INCLUDES_DIR)

install: all uninstall
	mkdir -p $(INSTALL_LIBS_DIR); \
	for f in $(foreach f,$(INSTALL_LIBS),.libs/$(f).so* .libs/$(f).a); do \
	    if [ -L $$f ]; then \
	        ln -s $(INSTALL_LIBS_DIR)/$$(basename $$(readlink -f $$f)) $(INSTALL_LIBS_DIR)/$$(basename $$f); \
	    else \
		install -m 644 $$f $(INSTALL_LIBS_DIR); \
	    fi; \
	done; \
	ldconfig; \
	mkdir -p $(INSTALL_BINS_LIB_DIR); \
	install -m 755 $(INSTALL_BINS_LIB) $(INSTALL_BINS_LIB_DIR); \
	mkdir -p $(INSTALL_SHARES_DIR); \
	install -m 644 $(INSTALL_SHARES) $(INSTALL_SHARES_DIR); \
	sed -i "s/\.\/oovm_hash/$$(echo $(INSTALL_BINS_LIB_DIR) | sed 's/\//\\\//g')\/oovm_hash/g" $(INSTALL_SHARES_DIR)/oovmpp.m4; \
	mkdir -p $(INSTALL_BINS_DIR); \
	install -m 755 $(INSTALL_BINS) $(INSTALL_BINS_DIR); \
	mkdir -p $(INSTALL_INCLUDES_DIR); \
	install -m 644 $(INSTALL_INCLUDES) $(INSTALL_INCLUDES_DIR)

doc:
	doxygen

builddeps:
	apt-get install autoconf automake libtool libtool-bin gcc g++ python flex bison m4 libz-dev libxml2-dev doxygen
