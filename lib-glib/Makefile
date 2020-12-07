# ----------------------------------------------------------- -*- mode: makefile -*-
# List of targets to build
# ----------------------------------------------------------------------------

NAME     ?= libkeepalive-glib
ROOT     ?= /tmp/test/$(NAME)

# Needed for local builds with make; rpm build will set it via spec file
VERS     ?= $(shell awk '/^Version:/ { print $$2 }' ../rpm/keepalive.spec)
VERS     := $(VERS)

# Note: library versioning != package versioning
SO_VERS  ?= .so.1.0.0
SO_NAME  ?= .so.1

TARGETS  += libkeepalive-glib$(SO_VERS)
TARGETS  += keepalive-glib.pc

# ----------------------------------------------------------------------------
# Standard install directories
# ----------------------------------------------------------------------------

_PREFIX         ?= /usr#                         # /usr
_INCLUDEDIR     ?= $(_PREFIX)/include#           # /usr/include
_EXEC_PREFIX    ?= $(_PREFIX)#                   # /usr
_BINDIR         ?= $(_EXEC_PREFIX)/bin#          # /usr/bin
_SBINDIR        ?= $(_EXEC_PREFIX)/sbin#         # /usr/sbin
_LIBEXECDIR     ?= $(_EXEC_PREFIX)/libexec#      # /usr/libexec
_LIBDIR         ?= $(_EXEC_PREFIX)/lib#          # /usr/lib
_SYSCONFDIR     ?= /etc#                         # /etc
_DATADIR        ?= $(_PREFIX)/share#             # /usr/share
_MANDIR         ?= $(_DATADIR)/man#              # /usr/share/man
_INFODIR        ?= $(_DATADIR)/info#             # /usr/share/info
_DEFAULTDOCDIR  ?= $(_DATADIR)/doc#              # /usr/share/doc
_LOCALSTATEDIR  ?= /var#                         # /var
_UNITDIR        ?= /lib/systemd/system#
_TESTSDIR       ?= /opt/tests#                   # /opt/tests

# ----------------------------------------------------------------------------
# Top level targets
# ----------------------------------------------------------------------------

.PHONY: build install clean distclean mostlyclean

build:: $(TARGETS)

install:: build

clean:: mostlyclean
	$(RM) $(TARGETS)

distclean:: clean

mostlyclean::
	$(RM) *.o *~ *.bak

# ----------------------------------------------------------------------------
# Default flags
# ----------------------------------------------------------------------------

CPPFLAGS += -D_GNU_SOURCE
CPPFLAGS += -D_FILE_OFFSET_BITS=64

COMMON   += -Wall
COMMON   += -Wextra
COMMON   += -Wmissing-declarations
COMMON   += -Os
COMMON   += -g

CFLAGS   += $(COMMON)
CFLAGS   += -std=c99
CFLAGS   += -Wstrict-prototypes
CFLAGS   += -Wmissing-prototypes

CXXFLAGS += $(COMMON)

LDFLAGS  += -g

LDLIBS   += -Wl,--as-needed

# ----------------------------------------------------------------------------
# Flags from pkg-config
# ----------------------------------------------------------------------------

PKG_NAMES += glib-2.0
PKG_NAMES += dbus-1
PKG_NAMES += libiphb
PKG_NAMES += mce

maintenance  = normalize clean distclean mostlyclean doc protos update
intersection = $(strip $(foreach w,$1, $(filter $w,$2)))
ifneq ($(call intersection,$(maintenance),$(MAKECMDGOALS)),)
PKG_CONFIG   ?= true
endif

ifneq ($(strip $(PKG_NAMES)),)
PKG_CONFIG   ?= pkg-config
PKG_CFLAGS   := $(shell $(PKG_CONFIG) --cflags $(PKG_NAMES))
PKG_LDLIBS   := $(shell $(PKG_CONFIG) --libs   $(PKG_NAMES))
PKG_CPPFLAGS := $(filter -D%,$(PKG_CFLAGS)) $(filter -I%,$(PKG_CFLAGS))
PKG_CFLAGS   := $(filter-out -I%, $(filter-out -D%, $(PKG_CFLAGS)))
endif

CPPFLAGS += $(PKG_CPPFLAGS)
CFLAGS   += $(PKG_CFLAGS)
LDLIBS   += $(PKG_LDLIBS)

# ----------------------------------------------------------------------------
# Implicit rules
# ----------------------------------------------------------------------------

%$(SO_VERS) : LDFLAGS += -shared -Wl,-soname,$*$(SO_NAME)

%$(SO_VERS) :
	$(CC) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	ln -sf  $@ $(NAME).so

clean::
	$(RM) $(NAME).so

%.o : %.c
	$(CC) -o $@ -c $< $(CPPFLAGS) $(CFLAGS)

%.pic.o : CFLAGS += -fPIC
%.pic.o : CFLAGS += -fvisibility=hidden

%.pic.o : %.c
	$(CC) -o $@ -c $< $(CPPFLAGS) $(CFLAGS)

%.q : %.c
	$(CC) -o $@ -E $^ $(CPPFLAGS)

clean::
	$(RM) *.q

TEMPLATE_OPS += -e 's:{{PREFIX}}:$(_PREFIX):g'
TEMPLATE_OPS += -e 's:{{EXEC_PREFIX}}:$(_EXEC_PREFIX):g'
TEMPLATE_OPS += -e 's:{{INCLUDEDIR}}:$(_INCLUDEDIR):g'
TEMPLATE_OPS += -e 's:{{LIBDIR}}:$(_LIBDIR):g'
TEMPLATE_OPS += -e 's:{{VERS}}:$(VERS):g'

% : %.tpl
	$(RM) $@
	sed < $< > $@ $(TEMPLATE_OPS)
	chmod a-w $@

# ----------------------------------------------------------------------------
# Explicit dependencies
# ----------------------------------------------------------------------------

# headers that are included in dev package
LIBRARY_HDR += keepalive.h
LIBRARY_HDR += keepalive-backgroundactivity.h
LIBRARY_HDR += keepalive-cpukeepalive.h
LIBRARY_HDR += keepalive-displaykeepalive.h
LIBRARY_HDR += keepalive-heartbeat.h
LIBRARY_HDR += keepalive-timeout.h

# headers that are used only during build time
PRIVATE_HDR += logging.h
PRIVATE_HDR += xdbus.h

# sources with exported functionality
LIBRARY_SRC += keepalive-backgroundactivity.c
LIBRARY_SRC += keepalive-cpukeepalive.c
LIBRARY_SRC += keepalive-displaykeepalive.c
LIBRARY_SRC += keepalive-heartbeat.c
LIBRARY_SRC += keepalive-object.c
LIBRARY_SRC += keepalive-timeout.c

# sources with internal functions only
LIBRARY_SRC += logging.c
LIBRARY_SRC += xdbus.c

LIBRARY_OBJ := $(patsubst %.c,%.pic.o,$(LIBRARY_SRC))

libkeepalive-glib$(SO_VERS): $(LIBRARY_OBJ)

# ----------------------------------------------------------------------------
# Documentation rules
# ----------------------------------------------------------------------------

.PHONY: doc
doc : Doxytag

Doxytag: Doxyfile $(wildcard keepalive*.h) | Doxyout
	doxygen > Doxylog
	touch $@

Doxyout:
	mkdir Doxyout

build:: doc

clean::
	$(RM) -r Doxyout Doxylog Doxytag

# ----------------------------------------------------------------------------
# Installation rules
# ----------------------------------------------------------------------------

install::
	install -m755 -d $(ROOT)$(_LIBDIR)/
	install -m644 libkeepalive-glib$(SO_VERS) $(ROOT)$(_LIBDIR)/
	ln -sf  libkeepalive-glib$(SO_VERS) $(ROOT)$(_LIBDIR)/libkeepalive-glib.so

	install -m755 -d $(ROOT)$(_INCLUDEDIR)/keepalive-glib/
	install -m644 $(LIBRARY_HDR) $(ROOT)$(_INCLUDEDIR)/keepalive-glib/

	install -m755 -d $(ROOT)$(_LIBDIR)/pkgconfig/
	install -m644 keepalive-glib.pc $(ROOT)$(_LIBDIR)/pkgconfig/

	install -m755 -d $(ROOT)/usr/share/doc/libkeepalive-glib/html
	tar cf - -C Doxyout html | tar xf - -C $(ROOT)/usr/share/doc/libkeepalive-glib

# ----------------------------------------------------------------------------
# Source code normalization
# ----------------------------------------------------------------------------

.PHONY: normalize
normalize::
	normalize_whitespace -M Makefile
	normalize_whitespace -a $(wildcard *.[ch] *.cc *.cpp *.pc.tpl *.py)

# ----------------------------------------------------------------------------
# Automatic header dependencies
# ----------------------------------------------------------------------------

.PHONY: depend
depend::
	@echo "Updating .depend"
	$(CC) -MM $(CPPFLAGS) *.c |\
	python3 ./depend_filter.py > .depend

ifneq ($(MAKECMDGOALS),depend) # not while: make depend
ifneq (,$(wildcard .depend))   # not if .depend does not exist
include .depend
endif
endif

# ----------------------------------------------------------------------------
# DEVELOPMENT TIME PROTOTYPE SCANNING
# ----------------------------------------------------------------------------

.SUFFIXES: %.q %.p %.g

CPROTOFLAGS += -D_Float32=float
CPROTOFLAGS += -D_Float64=double
CPROTOFLAGS += -D_Float128="long double"
CPROTOFLAGS += -D_Float32x=float
CPROTOFLAGS += -D_Float64x=double
CPROTOFLAGS += -D_Float128x="long double"

%.q : CPPFLAGS += $(CPROTOFLAGS)

%.q : %.c
	$(CC) -o $@ -E $< $(CPPFLAGS)
%.p : %.q ../prettyproto.groups
	cproto -s < $< | prettyproto.py | tee $@
%.g : %.q ../prettyproto.groups
	cproto < $< | prettyproto.py | tee $@

.PHONY: protos preprocess update
preprocess:: $(patsubst %.c,%.q,$(wildcard *.c))

protos_q = $(wildcard *.q )
protos_p = $(patsubst %.q,%.p,$(protos_q))
protos_g = $(patsubst %.q,%.g,$(protos_q))

protos:: $(protos_p) $(protos_g)

update_c += keepalive-backgroundactivity.c
update_c += keepalive-cpukeepalive.c
update_c += keepalive-displaykeepalive.c
update_c += keepalive-heartbeat.c
update_c += keepalive-object.c

update:: $(patsubst %.c,%.p,$(update_c))
	updateproto.py $(update_c)
clean::
	$(RM) *.[qpg]
