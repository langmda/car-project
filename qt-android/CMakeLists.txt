cmake_minimum_required(VERSION 3.16)

project(home VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
)

# 安卓适配 - 添加编译选项
if(ANDROID)
    # 关键：指向ZIP文件，不是目录
    file(WRITE "${CMAKE_BINARY_DIR}/android-build-home/gradle/wrapper/gradle-wrapper.properties"
        "distributionBase=GRADLE_USER_HOME\n"
        "distributionPath=wrapper/dists\n"
        "distributionUrl=file\\:///C:/Users/Administrator/.gradle/wrapper/dists/gradle-8.14.3-bin.zip\n"  # 必须是.zip
        "zipStoreBase=GRADLE_USER_HOME\n"
        "zipStorePath=wrapper/dists\n"
    )

    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(home
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        source.qrc
    )
    # Define target properties for Android with Qt 6 as:
    #    set_property(TARGET home APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
    #                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
    # For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(home SHARED
            ${PROJECT_SOURCES}
        )
        # Define properties for Android with Qt 5 after find_package() calls as:
        #    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(home
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# 链接 Qt 库
target_link_libraries(home PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

# 安卓适配 - 链接 Android 库（必须在目标创建后调用）
if(ANDROID)
    # 明确链接 C++ 库
    find_library(log-lib log)
    find_library(c++-lib c++_shared)

    target_link_libraries(home PRIVATE
        ${log-lib}
        ${c++-lib}
    )

    # 设置 Android 属性
    set_target_properties(home PROPERTIES
        ANDROID_STL "c++_shared"
        # 如果需要，添加包源目录
        # QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.home)
endif()
set_target_properties(home PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS home
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(home)
endif()
