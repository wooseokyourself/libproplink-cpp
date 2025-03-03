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

# protobuf 정적 링킹을 위한 설정
set(protobuf_USE_STATIC_LIBS ON)
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")

# 필요한 패키지 찾기
find_package(protobuf CONFIG REQUIRED)
find_package(ZeroMQ CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)

# 패키지 정보 출력
message(STATUS "Protobuf library: ${protobuf_LIBRARIES}")
message(STATUS "ZeroMQ found: ${ZeroMQ_FOUND}")

# Protocol Buffer files
set(PROTO_FILES property.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

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
    include/core.h
    include/property.pb.h
)

set(SERVER_HEADERS
    include/server.h
    ${COMMON_HEADERS}
)

set(CLIENT_HEADERS
    include/client.h
    ${COMMON_HEADERS}
)

# Build Mode - 기본값은 정적 라이브러리, 옵션으로 변경 가능
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# 라이브러리 타입에 따라 적절한 추가 플래그 설정
if(BUILD_SHARED_LIBS)
    message(STATUS "Building proplink as SHARED libraries")
else()
    message(STATUS "Building proplink as STATIC libraries")
endif()

# Create library targets with the selected type (STATIC or SHARED based on BUILD_SHARED_LIBS)
add_library(proplink_server ${SERVER_SOURCES} ${SERVER_HEADERS})
add_library(proplink_client ${CLIENT_SOURCES} ${CLIENT_HEADERS})

# 라이브러리 속성 설정
set_target_properties(proplink_server PROPERTIES
    DEBUG_POSTFIX "d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    POSITION_INDEPENDENT_CODE ON
)

set_target_properties(proplink_client PROPERTIES
    DEBUG_POSTFIX "d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    POSITION_INDEPENDENT_CODE ON
)

# 공유 라이브러리일 경우 Windows에서 DLL 내보내기 심볼 설정
if(BUILD_SHARED_LIBS AND WIN32)
    set_target_properties(proplink_server PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set_target_properties(proplink_client PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Set include directories and link dependencies for server
target_include_directories(proplink_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Set include directories and link dependencies for client
target_include_directories(proplink_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Link libraries - ZeroMQ는 동적, Protobuf는 정적
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

# Windows에서 정적 링킹을 위한 런타임 설정
if(MSVC AND NOT BUILD_SHARED_LIBS)
    # 정적 라이브러리용 정적 런타임 링킹 (/MT 또는 /MTd)
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

# Windows에서 예제 실행 파일도 정적 런타임 링킹 설정
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
install(FILES 
    ${SERVER_HEADERS}
    ${CLIENT_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
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