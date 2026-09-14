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
#     ./UITools --tools-root ../..            # 指向工具目录，读 assets/widgets.json
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

# ---- 动态 Qt 还是静态 Qt --------------------------------------------------
# 两种都能编。静态那套（C:\Qt\5.15.2-static，自己 configure -static 编的）编出来
# 是**一个不依赖任何 dll 的 exe**，工具目录里就不用摆 Qt5*.dll 和插件目录了。
#
# 静态时要多做两件事：
#   1. MSVC 运行库也得静态（/MT）—— 那套 Qt 是 -static-runtime 编的，
#      我们这边还用默认的 /MD 会在链接期撞成一堆 LNK2038。
#   2. 平台插件要显式导入。动态时 Qt 运行期从 plugins 目录加载；静态时它是
#      一个 .lib，不 Q_IMPORT_PLUGIN 就没人引用它，链接器直接丢掉，
#      运行起来报 "could not find or load the Qt platform plugin windows"。
get_target_property(UITOOLS_QT_TYPE Qt${QT_VERSION_MAJOR}::Core TYPE)
if(UITOOLS_QT_TYPE STREQUAL "STATIC_LIBRARY")
    set(UITOOLS_QT_STATIC ON)
    message(STATUS "UITools: 静态 Qt —— 编出来的 exe 不依赖 Qt dll")
    if(MSVC)
        cmake_policy(SET CMP0091 NEW)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
else()
    set(UITOOLS_QT_STATIC OFF)
    message(STATUS "UITools: 动态 Qt —— 需要在 exe 旁边放 Qt5*.dll 和 runtime\\ 插件")
endif()

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
    src/ui/NewProjectDialog.cpp
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

# ---- 资源打包那一条链（原 ResBuilder）----------------------------------
# 需要 QImage 读 bmp/png，所以要 Gui；Windows 上还用 gdi32 做文字光栅化
# （见 src/res/TextRaster.cpp）。
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui)
set(RES_CORE
    src/res/ResFormat.cpp
    src/res/ImageMono.cpp
    # 内容预览复用生成资源那条链：同一个 GDI 光栅化器、同一套透明色判定，
    # 画布上看到的就是烧进设备的那份点阵
    src/res/TextRaster.cpp
    # 编辑器也要读多国语言 xls（"文字列表"属性的选择对话框）
    src/res/XlsReader.cpp
    src/res/ResConfig.cpp
    src/res/ResFontDat.cpp
    src/res/ResBuilderCore.cpp
)

# exe 在资源管理器/任务栏里的图标。Windows 下靠一个 .rc 资源提供，
# 和窗口图标（QApplication::setWindowIcon 读 :/icons/app.png）是两回事，
# 两个都要有。图标本身由 resources/make_icons.py 生成。
if (WIN32)
    set(UITOOLS_WIN_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
else()
    set(UITOOLS_WIN_RC "")
endif()

# ---- 一个 exe，三条入口 -------------------------------------------------
# 以前是三个可执行文件：编辑器、工程 json -> 布局数据、资源打包。合成一个之后
#   * 工具目录少两项，其中两个名字还和别处的工具重名
#   * 静态链接时 Qt 只打包一份（三个各链一份的话体积要翻两倍多）
#   * 「资源导出」和命令行走的仍然是同一份代码，不会两边跑出两种产物
# 子命令在 src/main.cpp 的 dispatchSubcommand() 里分发：
#   UITools --gen  ...   原 QtToolBin
#   UITools --pack ...   原 ResBuilder
add_executable(UITools WIN32
    ${UITOOLS_WIN_RC}
    src/main.cpp
    # 资源打包那一整条链（原 ResBuilder），--pack 进
    src/resbuilder/main.cpp
    ${RES_CORE}
    # 工程 json -> 布局数据（原 QtToolBin），--gen 进；
    # 工具栏上的「资源导出」嵌的也是这一页
    src/qttoolbin/main.cpp
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
    # 只读数据（控件库 / 类型码表）编进 exe，见 assets.qrc 抬头
    resources/assets.qrc
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

# ---- 静态链接时要把插件也拉进来 -------------------------------------------
# 平台插件不导入就起不来；图片格式插件不导入的话，画布背景图（jpg）和
# 「修改背景图片」里的缩略图会静默显示不出来 —— bmp/png 是编在 QtGui 里的，
# 只有 jpg/gif/ico 靠插件。
if(UITOOLS_QT_STATIC)
    qt5_import_plugins(UITools
        INCLUDE Qt5::QWindowsIntegrationPlugin
                Qt5::QJpegPlugin
                Qt5::QGifPlugin
                Qt5::QICOPlugin
    )
endif()

# 就一个 target 了。原来这儿还有 ResBuilder 和 QtToolBin 两个 add_executable，
# 现在它们的源码都进了上面那个 target，入口变成 --pack / --gen 两个子命令。
