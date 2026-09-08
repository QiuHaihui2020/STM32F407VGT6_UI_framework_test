# qmake 工程（给习惯 Qt Creator / 原厂那套 Qt 5.9 mingw 工具链的人）
#
#   qmake UITools.pro && mingw32-make
#
# 想复刻原始二进制的形态：32 位 mingw + 静态 Qt 5.9.3，
# 原程序就是这么出的（.text 12.5 MB，无外部 Qt DLL 依赖）。

QT += core gui widgets
CONFIG += c++11
TARGET = UITools
TEMPLATE = app

INCLUDEPATH += include src/core

SOURCES += \
    src/main.cpp \
    src/core/ProjectModel.cpp \
    src/core/ControlLibrary.cpp \
    src/core/StyFile.cpp \
    src/ui/Forms.cpp \
    src/ui/Canvas.cpp \
    src/ui/Docks.cpp \
    src/ui/MainWindow.cpp \
    src/ui/ProjectDialog.cpp \
    $$files(src/gen/*.cpp)

# include/ 下既有手写头(Forms.h/Canvas.h/Docks.h/MainWindow.h/ProjectDialog.h)，
# 也有 gen_classes.py 生成的 24 个类头，一把收进来即可 —— 不要再单列，
# 重复列会让 moc 对同一个头跑两遍，链接时符号打架。
HEADERS += \
    $$files(include/*.h) \
    src/core/ProjectModel.h \
    src/core/ControlLibrary.h \
    src/core/StyFile.h

RESOURCES += resources/uitools.qrc

win32-msvc*: QMAKE_CXXFLAGS += /utf-8
