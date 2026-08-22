find_package(unofficial-mysql-connector-cpp CONFIG QUIET)

if(TARGET unofficial::mysql-connector-cpp::connector-jdbc)
    add_library(MySQLConnectorCPP::jdbc ALIAS unofficial::mysql-connector-cpp::connector-jdbc)
    set(MySQLConnectorCPP_FOUND TRUE)
    return()
endif()

find_path(MySQLConnectorCPP_INCLUDE_DIR
    NAMES mysql_driver.h
    PATH_SUFFIXES jdbc mysql-cppconn/jdbc mysql-connector-c++/jdbc)

find_library(MySQLConnectorCPP_LIBRARY
    NAMES mysqlcppconn mysqlcppconn8)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MySQLConnectorCPP
    REQUIRED_VARS MySQLConnectorCPP_INCLUDE_DIR MySQLConnectorCPP_LIBRARY)

if(MySQLConnectorCPP_FOUND AND NOT TARGET MySQLConnectorCPP::jdbc)
    add_library(MySQLConnectorCPP::jdbc UNKNOWN IMPORTED)
    set_target_properties(MySQLConnectorCPP::jdbc PROPERTIES
        IMPORTED_LOCATION "${MySQLConnectorCPP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MySQLConnectorCPP_INCLUDE_DIR}")
endif()

mark_as_advanced(MySQLConnectorCPP_INCLUDE_DIR MySQLConnectorCPP_LIBRARY)
