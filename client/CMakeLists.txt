set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6.3 REQUIRED
    COMPONENTS Core Gui Widgets Network)
set(QT_VERSION_MAJOR 6)

set(ELA_WIDGET_TOOLS_SOURCE_DIR
    "${PROJECT_SOURCE_DIR}/third_party/ElaWidgetTools/ElaWidgetTools")
if(NOT EXISTS "${ELA_WIDGET_TOOLS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "ElaWidgetTools is missing. Run: git submodule update --init --recursive")
endif()
set(ELAWIDGETTOOLS_BUILD_STATIC_LIB OFF CACHE BOOL "Build ElaWidgetTools shared" FORCE)
add_subdirectory("${ELA_WIDGET_TOOLS_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/third_party/ElaWidgetTools")

set(CHAT_CLIENT_CONFIG_FILE "${PROJECT_SOURCE_DIR}/config/client.ini" CACHE FILEPATH
    "复制到桌面客户端可执行文件旁的 INI 配置")
if(NOT EXISTS "${CHAT_CLIENT_CONFIG_FILE}")
    message(FATAL_ERROR "CHAT_CLIENT_CONFIG_FILE 指定的文件不存在：${CHAT_CLIENT_CONFIG_FILE}")
endif()

if(WIN32 AND QT_VERSION_MAJOR LESS 6)
    message(WARNING
        "Windows 上的 Qt 5 需要与 QSslSocket 匹配的 OpenSSL 运行库。"
        "发布前请从输出目录运行 chat_tls_probe；生产环境推荐使用 Qt 6 "
        "及其仍受维护的 Windows TLS 后端。")
endif()

file(GLOB_RECURSE CLIENT_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/model/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/model/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/network/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/network/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.qrc")

add_executable(chat_client WIN32 ${CLIENT_FILES})
set_property(TARGET chat_client PROPERTY AUTOGEN_ORIGIN_DEPENDS OFF)
add_executable(chat_tls_probe tools/TlsProbe.cpp)
target_link_libraries(chat_tls_probe PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network)
target_include_directories(chat_client PRIVATE
    app core model network
    ui/auth ui/chat ui/common ui/contacts)
target_link_libraries(chat_client PRIVATE
    Ela::WidgetTools
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network)

add_custom_command(TARGET chat_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ElaWidgetTools>"
        "$<TARGET_FILE_DIR:chat_client>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CHAT_CLIENT_CONFIG_FILE}"
        "$<TARGET_FILE_DIR:chat_client>/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/style"
        "$<TARGET_FILE_DIR:chat_client>/style"
    VERBATIM)

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${QT_BIN_DIR}"
        NO_DEFAULT_PATH
        REQUIRED)
    add_custom_command(TARGET chat_client POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --release --no-translations "$<TARGET_FILE:chat_client>"
        VERBATIM)
    add_custom_command(TARGET chat_tls_probe POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --release --no-translations "$<TARGET_FILE:chat_tls_probe>"
        VERBATIM)
endif()

install(TARGETS chat_client RUNTIME DESTINATION bin BUNDLE DESTINATION bin)
install(TARGETS chat_tls_probe RUNTIME DESTINATION bin)
install(FILES "${CHAT_CLIENT_CONFIG_FILE}" DESTINATION bin RENAME config.ini)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/ElaWidgetTools/LICENSE"
    DESTINATION licenses/ElaWidgetTools)

if(BUILD_TESTING)
    add_executable(client_ui_smoke tools/UiSmoke.cpp)
    target_link_libraries(client_ui_smoke PRIVATE
        Ela::WidgetTools
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets)
    add_test(NAME client_ui_smoke COMMAND client_ui_smoke)
    set_tests_properties(client_ui_smoke PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>"
        TIMEOUT 15)
endif()
