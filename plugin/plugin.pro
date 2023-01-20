TEMPLATE = lib
TARGET   = keepaliveplugin
TARGET   = $$qtLibraryTarget($$TARGET)

MODULENAME = Nemo/KeepAlive
TARGETPATH = $$[QT_INSTALL_QML]/$$MODULENAME

QT          -= gui
QT          += qml
CONFIG      += plugin
INCLUDEPATH += ../lib
LIBS        += -L../lib -lkeepalive

import.files = qmldir *.qml plugins.qmltypes
import.path  = $$TARGETPATH
target.path  = $$TARGETPATH

OTHER_FILES += qmldir
OTHER_FILES += *.qml

SOURCES += plugin.cpp

SOURCES += declarativebackgroundactivity.cpp
HEADERS += declarativebackgroundactivity.h

INSTALLS += target import

qmltypes.commands = qmlplugindump -nonrelocatable Nemo.KeepAlive 1.2 \
    |sed --file=$$PWD/plugins.qmltypes.sed > $$PWD/plugins.qmltypes
QMAKE_EXTRA_TARGETS += qmltypes
