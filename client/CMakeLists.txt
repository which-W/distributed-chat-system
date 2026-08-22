set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)

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
        "${PROJECT_SOURCE_DIR}/config/client.ini"
        "$<TARGET_FILE_DIR:chat_client>/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/style"
        "$<TARGET_FILE_DIR:chat_client>/style"
    VERBATIM)

install(TARGETS chat_client RUNTIME DESTINATION bin BUNDLE DESTINATION bin)
install(FILES "${PROJECT_SOURCE_DIR}/config/client.ini" DESTINATION bin RENAME config.ini)
