
cmake_minimum_required(VERSION 3.22..3.29)
project(uvent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -rdynamic -fPIC" CACHE STRING "Debug flags" FORCE)
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -rdynamic -fPIC" CACHE STRING "Debug flags" FORCE)

include_directories(${uvent_SOURCE_DIR})

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

file(GLOB_RECURSE UVENT_HEADERS include/*.h include/*.hpp)
file(GLOB_RECURSE UVENT_SOURCES src/*.cpp src/*.c)

foreach(header ${UVENT_HEADERS})
    if (header MATCHES "${CMAKE_BINARY_DIR}")
        list(REMOVE_ITEM UVENT_HEADERS ${header})
    endif()
endforeach()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug mode enabled")
    add_definitions(-DUVENT_DEBUG)

    # include(FetchContent)
    # FetchContent_Declare(
    #         spdlog
    #         GIT_REPOSITORY https://github.com/gabime/spdlog.git
    #         GIT_TAG v1.14.1
    # )
    # FetchContent_MakeAvailable(spdlog)
endif ()

add_definitions(-DUVENT_PIN_THREDS)

add_library(uvent ${UVENT_SOURCES})
add_library(usub::uvent ALIAS uvent)

configure_package_config_file(
    cmake/uventConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/uventConfig.cmake
    INSTALL_DESTINATION lib/cmake/uvent
)

install(TARGETS uvent
    EXPORT uventTargets
    #LIBRARY DESTINATION lib
    #ARCHIVE DESTINATION lib
    #RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uvent
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uvent
)

target_include_directories(uvent 
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/uvent>
)

foreach(header ${UVENT_HEADERS})
    get_filename_component(header_path ${header} DIRECTORY)
    file(RELATIVE_PATH header_relative_path "${CMAKE_CURRENT_SOURCE_DIR}/include" ${header_path})
    install(FILES ${header} DESTINATION include/server/${header_relative_path})
endforeach()

install(EXPORT uventTargets
    FILE uventConfig.cmake
    NAMESPACE usub::
    DESTINATION lib/cmake/uvent
)

if(BUILD_MAIN)
    add_executable(uvent_exe examples/main.cpp)
    target_link_libraries(uvent_exe usub::uvent)
endif()
