CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)
INCLUDE(GNUInstallDirs)
PROJECT(hiredis)

OPTION(HIREDIS_SSL "Link against OpenSSL" ON)

# Get the version numbers
MACRO(getVersionBit name)

EXECUTE_PROCESS(
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMAND grep "${name}" hiredis.h COMMAND awk "{print $3}"
    OUTPUT_VARIABLE "${name}"
    OUTPUT_STRIP_TRAILING_WHITESPACE)
ENDMACRO(getVersionBit)

getVersionBit(HIREDIS_MAJOR)
getVersionBit(HIREDIS_MINOR)
getVersionBit(HIREDIS_PATCH)

MESSAGE("Detected version: ${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

ADD_LIBRARY(hiredis SHARED
    async.c
    dict.c
    hiredis.c
    net.c
    read.c
    sds.c
    sslio.c)

SET_TARGET_PROPERTIES(hiredis
    PROPERTIES
    VERSION "${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")

INSTALL(TARGETS hiredis
    DESTINATION "${CMAKE_INSTALL_LIBDIR}")

INSTALL(FILES hiredis.h read.h sds.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)

IF(HIREDIS_SSL)
    IF (NOT OPENSSL_ROOT_DIR)
        IF (APPLE)
            SET(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
        ENDIF()
    ENDIF()
    FIND_PACKAGE(OpenSSL REQUIRED)
    ADD_DEFINITIONS(-DHIREDIS_SSL)
    INCLUDE_DIRECTORIES("${OPENSSL_INCLUDE_DIR}")
    TARGET_LINK_LIBRARIES(hiredis ${OPENSSL_LIBRARIES})
ENDIF()

# Add tests: Currently, I don't know how to make the tests actually run
# without hanging!
ENABLE_TESTING()
ADD_EXECUTABLE(hiredis-test test.c)


TARGET_LINK_LIBRARIES(hiredis-test hiredis)

# Add examples
ADD_SUBDIRECTORY(examples)

