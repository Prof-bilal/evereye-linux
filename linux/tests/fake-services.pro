QT += core dbus
CONFIG += c++20 console release warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = insomnia-fake-services
contains(CONFIG, legacy_profiles) {
    DEFINES += EVEREYE_LEGACY_PROFILES
    TARGET = insomnia-fake-services-legacy
}
SOURCES += fake_services.cpp
QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
