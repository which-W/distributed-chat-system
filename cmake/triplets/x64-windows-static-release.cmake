set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# MySQL's optional ABI check probes wsl.exe during Windows configuration and can
# hang when WSL is unavailable or its system disk is full. It is not required to
# build the client library.
if(PORT STREQUAL "libmysql")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DWSL_EXECUTABLE=")
endif()
