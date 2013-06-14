TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    video_muxing.cpp

linux-g++-64: {
  QMAKE_CXXFLAGS += -std=c++11
#  QMAKE_LIBS += -lavformat -lswscale -lavcodec -lavutil

  unix:!macx: LIBS += -L/usr/local/lib/ -lavcodec -lavfilter -lavformat -lavutil -lswscale

  INCLUDEPATH += /usr/local/include
  DEPENDPATH += /usr/local/include
}
