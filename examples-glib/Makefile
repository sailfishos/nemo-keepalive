# ----------------------------------------------------------- -*- mode: makefile -*-
# List of targets to build
# ----------------------------------------------------------------------------

include ../dbus-gmain.mk

TARGETS += block-suspend
TARGETS += keep-display-on
TARGETS += periodic-wakeup
TARGETS += simple-timer-wakeup

# ----------------------------------------------------------------------------
# Top level targets
# ----------------------------------------------------------------------------

.PHONY: build install clean distclean mostlyclean

$(TARGETS): $(DBUS_GMAIN_DIR)/dbus-gmain.o

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

CFLAGS   += -Wall
CFLAGS   += -Os
CFLAGS   += -std=c99
CFLAGS   += -g

LDFLAGS  += -g

LDLIBS   += -Wl,--as-needed

# ----------------------------------------------------------------------------
# Flags from pkg-config
# ----------------------------------------------------------------------------

PKG_NAMES  += glib-2.0
PKG_NAMES  += dbus-1
PKG_NAMES  += keepalive-glib

PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_NAMES))
PKG_LDLIBS := $(shell pkg-config --libs   $(PKG_NAMES))

CFLAGS     += $(PKG_CFLAGS)
LDLIBS     += $(PKG_LDLIBS)
