QT += core gui widgets dbus concurrent
CONFIG += c++20 release warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = evereye
SOURCES += src/main.cpp src/core.cpp src/platform.cpp src/guardian.cpp src/service.cpp src/window.cpp
HEADERS += src/core.h src/platform.h src/guardian.h src/service.h src/window.h
QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
isEmpty(PREFIX): PREFIX = /usr
target.path = $$PREFIX/bin
desktop.files = packaging/io.github.ProfBilal.EverEye.desktop
desktop.path = $$PREFIX/share/applications
icon.files = assets/io.github.ProfBilal.EverEye.svg
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
service.files = packaging/systemd/evereye.service
service.path = $$PREFIX/lib/systemd/user
INSTALLS += target desktop icon service

metainfo.files = packaging/io.github.ProfBilal.EverEye.metainfo.xml
metainfo.path = $$PREFIX/share/metainfo
INSTALLS += metainfo

LIBS += -lrt
