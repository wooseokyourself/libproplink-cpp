# - Try to find ZeroMQ
# Once done this will define
#  ZeroMQ_FOUND - System has ZeroMQ
#  ZeroMQ_INCLUDE_DIRS - The ZeroMQ include directories
#  ZeroMQ_LIBRARIES - The libraries needed to use ZeroMQ

find_path(ZeroMQ_INCLUDE_DIR zmq.h
    PATH_SUFFIXES include
    PATHS
    /usr
    /usr/local
    /opt/local
    ${ZeroMQ_ROOT}
)

# Look for the zmq library
find_library(ZeroMQ_LIBRARY
    NAMES zmq libzmq
    PATH_SUFFIXES lib lib64
    PATHS
    /usr
    /usr/local
    /opt/local
    ${ZeroMQ_ROOT}
)

# Look for the cppzmq headers
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp
    PATH_SUFFIXES include
    PATHS
    /usr
    /usr/local
    /opt/local
    ${ZeroMQ_ROOT}
)

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set ZeroMQ_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(ZeroMQ
    FOUND_VAR ZeroMQ_FOUND
    REQUIRED_VARS ZeroMQ_LIBRARY ZeroMQ_INCLUDE_DIR CPPZMQ_INCLUDE_DIR
)

if(ZeroMQ_FOUND)
    set(ZeroMQ_LIBRARIES ${ZeroMQ_LIBRARY})
    set(ZeroMQ_INCLUDE_DIRS ${ZeroMQ_INCLUDE_DIR} ${CPPZMQ_INCLUDE_DIR})
endif()

mark_as_advanced(ZeroMQ_INCLUDE_DIR ZeroMQ_LIBRARY CPPZMQ_INCLUDE_DIR)