TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    video_muxing.cpp

QMAKE_CXXFLAGS += -std=c++11
QMAKE_LIBS += -lavformat -lswscale -lavcodec -lavutil
