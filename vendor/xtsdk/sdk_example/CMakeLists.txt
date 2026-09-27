cmake_minimum_required(VERSION 3.5)

project(xtsdk_example VERSION 0.1 LANGUAGES CXX)

set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/../../xtsdk_bin_${CMAKE_BUILD_TYPE})

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# pcl
# ############################ WINDOWS LIBS PATH ##########################
if(WIN32)
    set(PCL_ROOT "D:/ws/xtsdk_cpp/libs/pcl_msvc/PCL 1.14.1")
    set(PCL_DIR "${PCL_ROOT}/cmake")
endif(WIN32)

# ################################################################
find_package(PCL REQUIRED COMPONENTS io common visualization)
find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets)

if(QT_FOUND)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets)
endif()

add_definitions(${PCL_DEFINITIONS})

# ###############define global variables################
if(PCL_VERSION VERSION_LESS "1.12")
    message("*********************PCL version VERSION_LESS *********************")
    add_definitions(-DPCL_VERSION_LESS_112)
else()
    message("*********************PCL version VERSION_HIG *********************")
endif()

add_definitions(-DEXAMPLE_DIR="${CMAKE_SOURCE_DIR}")

if(QT_FOUND AND Qt${QT_VERSION_MAJOR}_FOUND)
    message("*********************QT_FOUND *********************")
    add_definitions(-DQT_FOUND)
endif()

# sdk example
add_executable(sdk_example sdk_example.cpp)
target_include_directories(sdk_example PUBLIC xtsdk ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sdk_example xtsdk)

# sdk example pcl
add_executable(sdk_example_pcl sdk_example_pcl.cpp pcl_render.cpp)
target_include_directories(sdk_example_pcl PUBLIC
    xtsdk
    ${PCL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# sdk example play
add_executable(sdk_example_play sdk_example_play.cpp pcl_render.cpp)
target_include_directories(sdk_example_play PUBLIC
    xtsdk
    ${PCL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# sdk example ufw
add_executable(sdk_example_ufw sdk_example_ufw.cpp)
target_include_directories(sdk_example_ufw PUBLIC xtsdk ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sdk_example_ufw xtsdk)

# sdk example record
add_executable(sdk_example_record sdk_example_record.cpp)
target_include_directories(sdk_example_record PUBLIC
    xtsdk
    ${PCL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sdk_example_record xtsdk)



if(QT_FOUND AND Qt${QT_VERSION_MAJOR}_FOUND)
    if(WIN32)
        target_link_libraries(sdk_example_pcl PRIVATE Qt${QT_VERSION_MAJOR}::Widgets xtsdk ${PCL_LIBRARIES})
    else()
        target_link_libraries(sdk_example_pcl PRIVATE Qt${QT_VERSION_MAJOR}::Widgets xtsdk pthread ${PCL_LIBRARIES})
    endif()
else()
    if(WIN32)
        target_link_libraries(sdk_example_pcl PRIVATE xtsdk ${PCL_LIBRARIES})
    else()
        target_link_libraries(sdk_example_pcl PRIVATE xtsdk pthread ${PCL_LIBRARIES})
    endif()
endif()





if(QT_FOUND AND Qt${QT_VERSION_MAJOR}_FOUND)
    if(WIN32)
        target_link_libraries(sdk_example_play PRIVATE Qt${QT_VERSION_MAJOR}::Widgets xtsdk ${PCL_LIBRARIES})
    else()
        target_link_libraries(sdk_example_play PRIVATE Qt${QT_VERSION_MAJOR}::Widgets xtsdk pthread ${PCL_LIBRARIES})
    endif()
else()
    if(WIN32)
        target_link_libraries(sdk_example_play PRIVATE xtsdk ${PCL_LIBRARIES})
    else()
        target_link_libraries(sdk_example_play PRIVATE xtsdk pthread ${PCL_LIBRARIES})
    endif()
endif()
