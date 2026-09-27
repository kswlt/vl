cmake_minimum_required(VERSION 3.5)

project(xtsdk VERSION 2.1.8 LANGUAGES CXX)
string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S")

# 配置版本头文件
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ############################ WINDOWS LIBS PATH ##########################
if(WIN32)
    # set boost path
    set(BOOST_ROOT "D:/ws/xtsdk_cpp/libs/pcl_msvc/PCL 1.14.1/3rdParty/Boost/lib/cmake")

    # set OpenCV path
    set(OpenCV_DIR D:/ws/xtsdk_cpp/libs/opencv/build/x64/vc15/lib/)

    # set PCL path
    set(PCL_DIR "D:/ws/xtsdk_cpp/libs/pcl_msvc/PCL 1.14.1/cmake")
    find_package(PCL REQUIRED COMPONENTS common visualization)
    add_definitions(${PCL_DEFINITIONS})
endif(WIN32)

# ############################ Boost ####################################
find_package(Boost REQUIRED COMPONENTS system date_time regex)
find_package(OpenCV REQUIRED)
link_directories(${OpenCV_LIBRARY_DIRS})

# ############################# OpenMP ##################################
find_package(OpenMP REQUIRED)

if(OpenMP_FOUND)
    message(STATUS "found openmp")
    set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} ${OPENMP_C_FLAGS})
    set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} ${OPENMP_CXX_FLAGS})
    set(CMAKE_EXE_LINKER_FLAGS"${CMAKE_EXE_LINKER_FLAGS}${OpenMP_EXE_LINKER_FLAGS}")
else(OpenMP_FOUND)
    message(FATAL_ERROR "openmp not found!")
endif(OpenMP_FOUND)

configure_file(
    version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    @ONLY
)

add_library(xtsdk STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    xtsdk.h
    xtsdk.cpp
    xtdaemon.cpp
    communicationNet.cpp
    communicationUsb.cpp
    frame.cpp
    cartesianTransform.cpp
    baseFilter.cpp
    utils.cpp
    xtlogger.cpp
    xtsdkLibHandler.cpp
    xtpcl_filter.cpp
)

message(STATUS "__________CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}_________")

message(STATUS "__________SDK Version: ${PROJECT_VERSION}_________")

# add_definitions(-DPROJECT_DIR="${CMAKE_SOURCE_DIR}")
if(NOT DEFINED PROJECT_DIR)
    # 如果未定义，则定义PROJECT_DIR
    set(PROJECT_DIR ${CMAKE_SOURCE_DIR} CACHE STRING "Project directory")

    message(STATUS "PROJECT_DIR defined as: ${PROJECT_DIR}")
else()
    message(STATUS "PROJECT_DIR already defined as: ${PROJECT_DIR}")
endif()

add_definitions(-DPROJECT_DIR="${PROJECT_DIR}")

if(WIN32)
    target_include_directories(xtsdk PUBLIC ${Boost_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${PCL_INCLUDE_DIRS})
    target_link_libraries(xtsdk PRIVATE ${OpenCV_LIBS} ${PCL_LIBRARIES})

else()
    target_include_directories(xtsdk PUBLIC ${Boost_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})

    # Detect architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(ARCH "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(ARCH "aarch64")
    else()
        message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(LIB_DIR ${PROJECT_DIR}/xtsdk/lib/linux/${ARCH})

    # link_directories(LIB_DIR)
    # find_library(XTSDK_LIB NAMES xtsdk_shared
    # PATHS "${LIB_DIR}"
    # )

    # if(XTSDK_LIB)
    # message("*************XTSDK_LIB found  ${LIB_DIR}*************")
    # target_link_libraries(xtsdk ${XTSDK_LIB})
    # else()
    # message( "*************XTSDK_LIB not found: ${LIB_DIR}*************")
    # endif()
    add_definitions(-DLIB_DIR="${LIB_DIR}")
    target_link_libraries(xtsdk pthread boost_system boost_filesystem ${OpenCV_LIBS} dl)

    # target_link_libraries(xtsdk pthread boost_filesystem) #for ubuntu20.04
endif(WIN32)
