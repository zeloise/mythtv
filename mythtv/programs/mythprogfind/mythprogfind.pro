######################################################################
# Automatically generated by qmake (1.02a) Mon Jul 8 22:32:30 2002
######################################################################

include (../../settings.pro)

TEMPLATE = app
CONFIG += thread
target.path = $${PREFIX}/bin
INSTALLS = target

INCLUDEPATH += ../../libs/ ../../libs/libmyth
LIBS += -L../../libs/libmyth -L../../libs/libmythtv -L../../libs/libavcodec
LIBS += -lmythtv -lavcodec -lmyth-$$LIBVERSION -lXv -lXinerama -lmp3lame

DEPENDPATH += ../../libs/libmyth ../../libs/libmythtv

TARGETDEPS += ../../libs/libmythtv/libmythtv.a
TARGETDEPS += ../../libs/libavcodec/libavcodec.a

# Input
SOURCES += main.cpp
