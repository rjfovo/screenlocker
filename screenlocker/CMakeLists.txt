set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 查找libcutefish mpris库
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB_LIBS REQUIRED xcb)

# 设置libcutefish库路径
set(LIBCUTEFISH_LIBRARIES cutefishmpris_qmlplugins)

# 添加libcutefish库的搜索路径
set(LIBCUTEFISH_LIB_DIRS 
    ${CMAKE_SOURCE_DIR}/../../../libcutefish/obj-x86_64-linux-gnu/mpris
)

# 添加库搜索路径
link_directories(${LIBCUTEFISH_LIB_DIRS})

set(PROJECT_SOURCES
    main.cpp
    application.cpp
    authenticator.cpp
    kcheckpass-enums.h
    fixx11h.h
    qml.qrc
)

add_executable(cutefish-screenlocker
    ${PROJECT_SOURCES}
)

target_link_libraries(cutefish-screenlocker
    PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
    Qt6::Quick
    ${LIBCUTEFISH_LIBRARIES}
    ${X11_LIBRARIES}
)

# 注意：Qt6中移除了X11Extras模块，相关功能需要直接使用X11库

install(TARGETS cutefish-screenlocker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
