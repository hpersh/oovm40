# Building

CFLAGS_COMMON	= -Wall
CFLAGS_DEBUG	= $(CFLAGS_COMMON) -g
CFLAGS_OPT	= $(CFLAGS_COMMON) -DNDEBUG -O3
CFLAGS_PROFILE	= $(CFLAGS_OPT) -pg
CFLAGS	= $(CFLAGS_OPT)

# Installing

INSTALL_ROOT	= /usr/local
