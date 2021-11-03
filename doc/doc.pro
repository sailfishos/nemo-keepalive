TEMPLATE=aux

CONFIG += sailfish-qdoc-template
SAILFISH_QDOC.project = libkeepalive
SAILFISH_QDOC.config = libkeepalive.qdocconf
SAILFISH_QDOC.style = offline
SAILFISH_QDOC.path = /usr/share/doc/libkeepalive

OTHER_FILES += src/index.qdoc
