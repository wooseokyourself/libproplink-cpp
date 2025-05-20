cmake_minimum_required(VERSION 3.10)

# Set project name and version
project(proplink 
    VERSION 1.0.0
    LANGUAGES CXX
)

# Use C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories for all targets
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")

# Configure static linking for protobuf based on platform
if(WIN32)
    # Settings for static linking on Windows
    set(protobuf_USE_STATIC_LIBS ON)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

# Find required packages with platform-specific settings
if(WIN32)
    # On Windows, use CONFIG mode for packages from vcpkg
    find_package(protobuf CONFIG REQUIRED)
    find_package(ZeroMQ CONFIG REQUIRED)
    find_package(cppzmq CONFIG REQUIRED)
else()
    # On Linux/Ubuntu, use module mode for Protobuf
    find_package(Protobuf REQUIRED)
    
    # For Linux/Jetson, use pkg-config to find ZeroMQ and cppzmq
    # First make sure pkg-config is available
    find_package(PkgConfig REQUIRED)
    
    # Find ZeroMQ using pkg-config
    pkg_check_modules(ZeroMQ REQUIRED libzmq)
    
    # If cppzmq is not installed as a package, we might need to provide
    # the path to the cppzmq headers or build it from source
    # For now, assume the headers are in a standard location
    set(CPPZMQ_INCLUDE_DIRS "/usr/include" CACHE PATH "cppzmq include directories")
    
    # Additional Linux dependencies if needed
    find_package(Threads REQUIRED)
endif()

# Output package information
message(STATUS "Protobuf found: ${Protobuf_FOUND}")
if(WIN32)
    message(STATUS "ZeroMQ found: ${ZeroMQ_FOUND}")
else()
    message(STATUS "ZeroMQ found via pkg-config: ${ZeroMQ_FOUND}")
    message(STATUS "ZeroMQ libraries: ${ZeroMQ_LIBRARIES}")
    message(STATUS "ZeroMQ include dirs: ${ZeroMQ_INCLUDE_DIRS}")
endif()

# Protocol Buffer files
set(PROTO_FILES property.proto)
if(WIN32)
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
else()
    Protobuf_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
endif()

# Common source files for both server and client
set(COMMON_SOURCES
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# Server library source files
set(SERVER_SOURCES
    src/thread_pool.cpp
    src/server.cpp
    ${COMMON_SOURCES}
)

# Client library source files
set(CLIENT_SOURCES
    src/client.cpp
    ${COMMON_SOURCES}
)

# Header files
set(COMMON_HEADERS
    include/proplink/core.h
    include/proplink/property.pb.h
)

set(SERVER_HEADERS
    include/proplink/server.h
    ${COMMON_HEADERS}
)

set(CLIENT_HEADERS
    include/proplink/client.h
    ${COMMON_HEADERS}
)

# Build Mode - Default is static library, can be changed via option
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Set appropriate flags based on library type
if(BUILD_SHARED_LIBS)
    message(STATUS "Building proplink as SHARED libraries")
else()
    message(STATUS "Building proplink as STATIC libraries")
endif()

# Create library targets with the selected type (STATIC or SHARED based on BUILD_SHARED_LIBS)
add_library(proplink_server ${SERVER_SOURCES} ${SERVER_HEADERS})
add_library(proplink_client ${CLIENT_SOURCES} ${CLIENT_HEADERS})

# Set library properties
set_target_properties(proplink_server PROPERTIES
    DEBUG_POSTFIX "d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    POSITION_INDEPENDENT_CODE ON
)

set_target_properties(proplink_client PROPERTIES
    DEBUG_POSTFIX "d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    POSITION_INDEPENDENT_CODE ON
)

# Configure export symbols for shared libraries on Windows
if(BUILD_SHARED_LIBS AND WIN32)
    set_target_properties(proplink_server PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set_target_properties(proplink_client PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Set include directories and link dependencies for server
target_include_directories(proplink_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/proplink
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Set include directories and link dependencies for client
target_include_directories(proplink_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/proplink
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Platform-specific include directories and link libraries
if(WIN32)
    # Windows-specific linking
    target_link_libraries(proplink_server 
        PUBLIC 
            cppzmq
        PRIVATE
            protobuf::libprotobuf
    )

    target_link_libraries(proplink_client 
        PUBLIC 
            cppzmq
        PRIVATE
            protobuf::libprotobuf
    )
else()
    # Add ZeroMQ include directories for Linux
    target_include_directories(proplink_server
        PRIVATE
            ${ZeroMQ_INCLUDE_DIRS}
            ${CPPZMQ_INCLUDE_DIRS}
    )
    target_include_directories(proplink_client
        PRIVATE
            ${ZeroMQ_INCLUDE_DIRS}
            ${CPPZMQ_INCLUDE_DIRS}
    )

    # Linux/Ubuntu/Jetson-specific linking
    target_link_libraries(proplink_server 
        PRIVATE
            ${Protobuf_LIBRARIES}
            ${ZeroMQ_LIBRARIES}
            Threads::Threads
    )

    target_link_libraries(proplink_client 
        PRIVATE
            ${Protobuf_LIBRARIES}
            ${ZeroMQ_LIBRARIES}
            Threads::Threads
    )
endif()

# Configure static runtime linking for MSVC
if(MSVC AND NOT BUILD_SHARED_LIBS)
    # Static runtime linking for static libraries (/MT or /MTd)
    set_property(TARGET proplink_server PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET proplink_client PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Create example executables
add_executable(server_example src/server_example.cpp)
add_executable(client_example src/client_example.cpp)

# Set the example executable output directories
set_target_properties(server_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
)

set_target_properties(client_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
)

# Link examples with libraries and ensure correct build order
target_link_libraries(server_example PRIVATE proplink_server)
target_link_libraries(client_example PRIVATE proplink_client)

# Configure static runtime linking for MSVC example executables
if(MSVC AND NOT BUILD_SHARED_LIBS)
    set_property(TARGET server_example PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET client_example PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Installation settings
include(GNUInstallDirs)
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# Install libraries
install(TARGETS proplink_server proplink_client
    EXPORT ${PROJECT_NAME}Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install header files
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Install CMake targets
install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${INSTALL_CONFIGDIR}
)

# Generate and install Config files
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/proplinkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

# Generate version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)