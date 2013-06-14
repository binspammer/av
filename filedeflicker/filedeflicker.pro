TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    filedeflicker.cpp \
    testfiledeflicker.cpp

HEADERS += \
    filedeflicker.h

linux-g++-64: {
  QMAKE_CXXFLAGS += -std=c++11
  QMAKE_LIBS += -lavformat -lswscale -lavcodec -lavutil
}

win32: {
  DEV = "C:/Dev"

  INCLUDEPATH +=   $$DEV/include
  DEPENDPATH +=   $$DEV/include

  QMAKE_CXXFLAGS_RELEASE += -Gm -MD
  QMAKE_CXXFLAGS_DEBUG += -Od -Gm -MDd
  QMAKE_LFLAGS_DEBUG += /INCREMENTAL:NO

  LIBS += -L$$DEV/lib
  CONFIG(release, debug|release): LIBS += -lavformat -lswscale -lavcodec -lavutil
  else:CONFIG(debug, debug|release): LIBS += -lavformat -lswscale -lavcodec -lavutil

}
