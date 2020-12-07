DIST_ROOT     = $(dir $(lastword $(MAKEFILE_LIST)))

# ----------------------------------------------------------------------------
# DBUS_GMAIN
# ----------------------------------------------------------------------------

DBUS_GMAIN_DIR := $(DIST_ROOT)/dbus-gmain

# The dbus-gmain submodule contains sources that have
# issues and do not compile cleanly. As the content is
# what it is, silence warnings etc when compiling source
# files from there...

DBUS_GMAIN_CPPFLAGS += -I$(DIST_ROOT)
DBUS_GMAIN_CFLAGS   += -Wno-unused-parameter
DBUS_GMAIN_CFLAGS   += -Wno-cast-function-type
DBUS_GMAIN_CFLAGS   += -Wno-missing-field-initializers

$(DBUS_GMAIN_DIR)/%.o : CPPFLAGS += $(DBUS_GMAIN_CPPFLAGS)
$(DBUS_GMAIN_DIR)/%.o : CFLAGS   += $(DBUS_GMAIN_CFLAGS)
