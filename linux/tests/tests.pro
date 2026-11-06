QT += core gui widgets dbus concurrent testlib
CONFIG += c++20 testcase release warn_on
TEMPLATE = app
TARGET = insomnia-tests
INCLUDEPATH += ../src
SOURCES += test_insomnia.cpp ../src/core.cpp ../src/platform.cpp ../src/guardian.cpp ../src/service.cpp ../src/window.cpp
HEADERS += ../src/core.h ../src/platform.h ../src/guardian.h ../src/service.h ../src/window.h
QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic

LIBS += -lrt
