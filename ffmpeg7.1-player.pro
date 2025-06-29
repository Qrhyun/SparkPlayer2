TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        audiooutput.cpp \
        avframequeue.cpp \
        avpacketqueue.cpp \
        decodethread.cpp \
        demuxthread.cpp \
        main.cpp \
        thread.cpp \
        videooutput.cpp


win32 {

FFMPEG_PATH = $$PWD\ffmpeg-n7.1-latest-win64-gpl-shared-7.1

INCLUDEPATH += $$FFMPEG_PATH\include
LIBS += -L$$FFMPEG_PATH\lib -lavutil -lavcodec -lswresample -lavformat



INCLUDEPATH += $$PWD/SDL2-2.0.10/include
LIBS += $$PWD/SDL2-2.0.10/lib/x64/SDL2.lib
}

HEADERS += \
    audiooutput.h \
    avframequeue.h \
    avpacketqueue.h \
    avsync.h \
    decodethread.h \
    demuxthread.h \
    queue.h \
    test.h \
    thread.h \
    videooutput.h
