# -*- mode: sh -*-
TEMPLATE = subdirs
CONFIG += no_keywords
SUBDIRS += lib
SUBDIRS += plugin
SUBDIRS += examples/backgroundactivity_periodic
SUBDIRS += examples/backgroundactivity_linger
SUBDIRS += examples/displayblanking
SUBDIRS += tests
SUBDIRS += doc

examples.files = examples/qml/*.qml
examples.path = $$[QT_INSTALL_EXAMPLES]/keepalive

QT -= gui

CONFIG  += ordered

INSTALLS += examples
