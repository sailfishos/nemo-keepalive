# -*- mode: sh -*-
TEMPLATE = subdirs

SUBDIRS += lib
SUBDIRS += plugin
SUBDIRS += examples/backgroundactivity_periodic
SUBDIRS += examples/backgroundactivity_linger
SUBDIRS += examples/displayblanking
SUBDIRS += tests

CONFIG += mer-qdoc-template
MER_QDOC.project = libkeepalive
MER_QDOC.config = doc/libkeepalive.qdocconf
MER_QDOC.style = offline
MER_QDOC.path = /usr/share/doc/libkeepalive
OTHER_FILES += doc/src/index.qdoc

examples.files = examples/qml/*.qml
examples.path = $$[QT_INSTALL_EXAMPLES]/keepalive

QT -= gui

CONFIG  += ordered

INSTALLS += examples
