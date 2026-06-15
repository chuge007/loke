#-------------------------------------------------
#
# Project created by QtCreator 2026-01-18T09:48:48
#
#-------------------------------------------------

QT       += core gui serialbus network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = Locke
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11 console

# 源码使用 UTF-8 编码（含中文注释、emoji），告诉 MSVC 按 UTF-8 解析，
# 否则 cl 会按系统代码页(GBK)解析，遇到尾字节为 0x5C 的多字节字符会
# 误吞大括号或分号，引发大量假语法错误。
msvc {
    QMAKE_CFLAGS   += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}


SOURCES += \
        main.cpp \
        mainwindow.cpp \
        scancontrolhuichuan.cpp \
        scancontroltaida.cpp

HEADERS += \
        mainwindow.h \
        modbusconfig.h \
        scancontrolabstract.h \
        scancontrolhuichuan.h \
        scancontroltaida.h

FORMS += \
        mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

INCLUDEPATH +=$$PWD/SDL3-3.2.16/include



LIBS += -L$$PWD/SDL3-3.2.16/lib/x64/ -lSDL3
