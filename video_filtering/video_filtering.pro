TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    video_filtering.cpp

QMAKE_CXXFLAGS += -std=c++11
#QMAKE_LIBS += -lavcodec -lavfilter -lavformat -lavutil -lswscale

unix:!macx: LIBS += -L/usr/local/lib/ -lavcodec -lavfilter -lavformat -lavutil -lswscale

INCLUDEPATH += /usr/local/include
DEPENDPATH += /usr/local/include
