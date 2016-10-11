SRCDIR = ../../lib/
INCLUDEPATH += $$SRCDIR
DEPENDPATH = $$INCLUDEPATH
QT -= gui
QT += testlib
TEMPLATE = app
CONFIG -= app_bundle
LIBS += -L../../lib -lkeepalive

DEFINES += USE_VOLAND_TEST_INTERFACE

target.path = /opt/tests/nemo-keepalive
INSTALLS += target
