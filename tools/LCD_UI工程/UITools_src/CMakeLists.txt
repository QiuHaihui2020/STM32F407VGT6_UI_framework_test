#
# UITools —— 点阵屏 UI 工具链的 Qt 工程
#
# 不锁死某个 Qt 版本：Qt5 5.9 以上、Qt6 都能编。
# 想要单文件无外部依赖的形态，用 32 位 mingw + 静态 Qt。
#
# 构建（MinGW）:
#     cmake -S . -B build -G "Ninja" -DCMAKE_PREFIX_PATH=<Qt根>/5.15.2/mingw81_64
#     cmake --build build
# 构建（MSVC）:
#     cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt根>/5.15.2/msvc2019_64
#     cmake --build build --config Release
#
# 运行:
#     ./UITools --tools-root ../..            # 指向 UITools 目录，读 control/control.json
#     ./UITools --sty-dump ../../../JL/JL.sty # 只解析 .sty，不开界面
#
cmake_minimum_required(VERSION 3.16)
project(UITools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt5 优先（原始程序就是 Qt5）；找不到就退到 Qt6
find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
message(STATUS "UITools: 使用 Qt${QT_VERSION_MAJOR} (${Qt${QT_VERSION_MAJOR}_VERSION})")

# ---- 手写实现（承载真实行为）------------------------------------------
set(UITOOLS_CORE
    src/core/ProjectModel.cpp
    src/core/ControlLibrary.cpp
    src/core/StyFile.cpp
)
set(UITOOLS_UI
    src/ui/EditorOps.cpp
    src/ui/Preview.cpp
    src/ui/Forms.cpp
    src/ui/Canvas.cpp
    src/ui/Docks.cpp
    src/ui/Property.cpp
    src/ui/AppIcon.cpp
    src/ui/UiTheme.cpp
    src/ui/MainWindow.cpp
    src/ui/ProjectDialog.cpp
)

# ---- 对话框与小部件 --------------------------------------------------------
file(GLOB UITOOLS_GEN CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/*.cpp")

# ---- 头文件必须显式列进来 ----------------------------------------------
# 本工程的头在 include/、实现在 src/*/，两者不同目录。AUTOMOC 只会自动认
# "同目录同基名" 的头，跨目录的认不出来，结果就是 moc_*.cpp 一个都不生成，
# 链接时报一片 qt_metacall / qt_metacast / metaObject 未解析。把头加进
# 目标源列表即可让 AUTOMOC 扫到。
file(GLOB UITOOLS_HDRS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h")

# exe 在资源管理器/任务栏里的图标。Windows 下靠一个 .rc 资源提供，
# 和窗口图标（QApplication::setWindowIcon 读 :/icons/app.png）是两回事，
# 两个都要有。图标本身由 resources/make_icons.py 生成。
if (WIN32)
    set(UITOOLS_WIN_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
else()
    set(UITOOLS_WIN_RC "")
endif()

# 编辑器也要读多国语言 xls（"文字列表"属性的选择对话框），
# 复用 ResBuilder 那边写的 BIFF8 读取器
add_executable(UITools WIN32
    ${UITOOLS_WIN_RC}
    src/main.cpp
    src/res/XlsReader.cpp
    # 内容预览复用生成资源那条链：同一个 GDI 光栅化器、同一套透明色判定，
    # 画布上看到的就是烧进设备的那份点阵
    src/res/TextRaster.cpp
    src/res/ResConfig.cpp
    # 工具栏上的「资源导出」直接嵌 QtToolBin 那一页 —— 两个目标共用同一份
    # 源码，导出走的就是 step2 那条链，不会两边各跑一套逻辑跑出两种产物
    src/qttoolbin/ToolBinWindow.cpp
    src/qttoolbin/ToolBinWindow.h
    src/qttoolbin/FeatureDialog.cpp
    src/qttoolbin/FeatureDialog.h
    src/qttoolbin/ResbuilderOptions.cpp
    src/qttoolbin/ResbuilderOptions.h
    src/sty/StyBuilder.cpp
    ${UITOOLS_CORE}
    ${UITOOLS_UI}
    ${UITOOLS_GEN}
    ${UITOOLS_HDRS}
    resources/uitools.qrc
)

target_include_directories(UITools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/res
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sty
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qttoolbin
)

target_link_libraries(UITools PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
if(WIN32)
    # TextRaster 在 Windows 上走 GDI（CreateFontIndirectW + TextOutW）
    target_link_libraries(UITools PRIVATE gdi32 user32)
endif()

# ---- 中文路径下必须加这个 ----------------------------------------------
# 本工程就放在 tools/LCD_UI工程/ 下，路径里带中文。moc 生成 moc_*.cpp 时会把
# 头文件的**绝对路径**写进 #include，而它落盘时按本地 8 位编码转换，中文转不出来，
# 结果写成 "LCD_UI??/..."，编译直接 C1083 找不到文件（实测 Qt 5.15.2 + ACP 936）。
# moc 的 -p 选项用来指定 #include 里的路径前缀：给一个 "."，
# 生成的就是 #include "ActionList.h"，纯 ASCII，再靠下面的 -I 去找就行。
set_target_properties(UITools PROPERTIES AUTOMOC_MOC_OPTIONS "-p;.")

if(MSVC)
    # 源码是 UTF-8（大量中文注释），不加这个 MSVC 会按本地代码页解释而报 C4819
    target_compile_options(UITools PRIVATE /utf-8)
endif()

# =======================================================================
#  ResBuilder —— 资源描述 + 位图 + xls -> 资源二进制与索引头文件
#  只依赖 Qt Core（Windows 上还用 gdi32 做文字光栅化，见 src/res/TextRaster.cpp）
# =======================================================================
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui)

set(RES_CORE
    src/res/ResFormat.cpp
    src/res/ImageMono.cpp
    src/res/TextRaster.cpp
    src/res/XlsReader.cpp
    src/res/ResConfig.cpp
    src/res/ResFontDat.cpp
    src/res/ResBuilderCore.cpp
)

add_executable(ResBuilder src/resbuilder/main.cpp ${UITOOLS_WIN_RC} ${RES_CORE})
target_include_directories(ResBuilder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/res)
# 需要 QImage 读 bmp/png，所以带上 Gui；界面部分用不到
target_link_libraries(ResBuilder PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)
if(WIN32)
    target_link_libraries(ResBuilder PRIVATE gdi32 user32)
endif()
if(MSVC)
    target_compile_options(ResBuilder PRIVATE /utf-8)
endif()

# =======================================================================
#  QtToolBin —— 工程 json -> 布局数据与资源描述
#  工程 json -> project.bin(.sty) / ename.h / Resbuilder.xml / debug.txt
# =======================================================================
# QtToolBin 是弹窗工具，所以这里要 Widgets；
# 不带参数弹界面，带生成类选项走命令行（见 src/qttoolbin/main.cpp 抬头）。
add_executable(QtToolBin WIN32
    ${UITOOLS_WIN_RC}
    src/qttoolbin/main.cpp
    src/qttoolbin/ToolBinWindow.cpp
    src/qttoolbin/ToolBinWindow.h
    src/qttoolbin/FeatureDialog.cpp
    src/qttoolbin/FeatureDialog.h
    src/qttoolbin/ResbuilderOptions.cpp
    src/qttoolbin/ResbuilderOptions.h
    src/res/ResConfig.cpp
    src/res/ResConfig.h
    src/sty/StyBuilder.cpp
)
target_include_directories(QtToolBin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sty
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qttoolbin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/res
)
target_link_libraries(QtToolBin PRIVATE
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets)
set_target_properties(QtToolBin PROPERTIES AUTOMOC_MOC_OPTIONS "-p;.")
if(MSVC)
    target_compile_options(QtToolBin PRIVATE /utf-8)
endif()
