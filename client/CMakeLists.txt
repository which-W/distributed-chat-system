set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)

set(CHAT_CLIENT_CONFIG_FILE "${PROJECT_SOURCE_DIR}/config/client.ini" CACHE FILEPATH
    "Client INI copied beside the desktop executable")
if(NOT EXISTS "${CHAT_CLIENT_CONFIG_FILE}")
    message(FATAL_ERROR "CHAT_CLIENT_CONFIG_FILE does not exist: ${CHAT_CLIENT_CONFIG_FILE}")
endif()

if(WIN32 AND QT_VERSION_MAJOR LESS 6)
    message(WARNING
        "Qt 5 on Windows requires a matching OpenSSL runtime for QSslSocket. "
        "Run chat_tls_probe from the output directory before shipping; Qt 6 "
        "with its maintained Windows TLS backend is recommended for production.")
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
add_executable(chat_tls_probe tools/TlsProbe.cpp)
target_link_libraries(chat_tls_probe PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network)
target_include_directories(chat_client PRIVATE
    app core model network
    ui/auth ui/chat ui/common ui/contacts)
target_link_libraries(chat_client PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network)

add_custom_command(TARGET chat_client POST_BUILD
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
