# qmake 工程（给习惯 Qt Creator / mingw 工具链的人）
#
#   qmake UITools.pro && mingw32-make
#
# 想要单文件、无外部 Qt DLL 依赖的形态：32 位 mingw + 静态 Qt。

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
