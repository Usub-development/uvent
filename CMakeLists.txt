
cmake_minimum_required(VERSION 3.22..3.29)
project(uvent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -rdynamic -fPIC" CACHE STRING "Debug flags" FORCE)
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -rdynamic -fPIC" CACHE STRING "Debug flags" FORCE)

option(UVENT_ENABLE_REUSEADDR "Use SO_REUSEADDR (portable)" ON)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

file(GLOB_RECURSE UVENT_HEADERS include/*.h include/*.hpp)
file(GLOB_RECURSE UVENT_SOURCES src/*.cpp src/*.c)

foreach (header ${UVENT_HEADERS})
    if (header MATCHES "${CMAKE_BINARY_DIR}")
        list(REMOVE_ITEM UVENT_HEADERS ${header})
    endif ()
endforeach ()

add_definitions(-DUVENT_PIN_THREADS)

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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/uvent>
)

foreach (header ${UVENT_HEADERS})
    get_filename_component(header_path ${header} DIRECTORY)
    file(RELATIVE_PATH header_relative_path "${CMAKE_CURRENT_SOURCE_DIR}/include" ${header_path})
    install(FILES ${header} DESTINATION include/server/${header_relative_path})
endforeach ()

install(EXPORT uventTargets
        FILE uventConfig.cmake
        NAMESPACE usub::
        DESTINATION lib/cmake/uvent
)

option(BUILD_MAIN "Buld main executable for testing" OFF)
option(ENABLE_SANITIZERS "Buld main executable for testing" OFF)

if (BUILD_MAIN)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Debug mode enabled")
        include(FetchContent)
        FetchContent_Declare(
                spdlog
                GIT_REPOSITORY https://github.com/gabime/spdlog.git
                GIT_TAG v1.14.1
        )
        FetchContent_MakeAvailable(spdlog)
    endif ()

    if (ENABLE_SANITIZERS)
        add_executable(uvent_asan_ubsan ${UVENT_SOURCES} examples/main.cpp)
        target_compile_definitions(uvent_asan_ubsan PRIVATE
                $<$<CONFIG:Debug>:UVENT_DEBUG>
        )
        target_include_directories(uvent_asan_ubsan
                PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(uvent_asan_ubsan PRIVATE $<$<CONFIG:Debug>:spdlog::spdlog>)
        target_compile_options(uvent_asan_ubsan PRIVATE
                -fsanitize=address,undefined
                -fno-sanitize-recover=all
                -fno-omit-frame-pointer
                -g
        )
        target_link_options(uvent_asan_ubsan PRIVATE
                -fsanitize=address,undefined
        )

        add_executable(uvent_tsan ${UVENT_SOURCES} examples/main.cpp)
        target_compile_definitions(uvent_tsan PRIVATE
                $<$<CONFIG:Debug>:UVENT_DEBUG>
        )
        target_include_directories(uvent_tsan
                PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(uvent_tsan PRIVATE $<$<CONFIG:Debug>:spdlog::spdlog>)
        target_compile_options(uvent_tsan PRIVATE -fsanitize=thread -g)
        target_link_options(uvent_tsan PRIVATE -fsanitize=thread)


        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_executable(uvent_msan_ubsan ${UVENT_SOURCES} examples/main.cpp)
            target_compile_definitions(uvent_msan_ubsan PRIVATE
                    $<$<CONFIG:Debug>:UVENT_DEBUG>
            )
            target_include_directories(uvent_msan_ubsan
                    PRIVATE
                    ${CMAKE_CURRENT_SOURCE_DIR}/include
            )
            target_link_libraries(uvent_msan_ubsan PRIVATE $<$<CONFIG:Debug>:spdlog::spdlog>)
            target_compile_options(uvent_msan_ubsan PRIVATE
                    -fsanitize=memory,undefined
                    -fsanitize-memory-track-origins=2
                    -fno-sanitize-recover=all
                    -fno-omit-frame-pointer
                    -g
            )
            target_link_options(uvent_msan_ubsan PRIVATE
                    -fsanitize=memory,undefined
            )
        endif ()
    endif ()

    add_executable(uvent_exe ${UVENT_SOURCES} examples/main.cpp)
    target_compile_definitions(uvent_exe PRIVATE
            $<$<CONFIG:Debug>:UVENT_DEBUG>
    )
    target_compile_definitions(uvent_exe PUBLIC
            UVENT_ENABLE_REUSEADDR
    )
    target_include_directories(uvent_exe
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(uvent_exe PRIVATE $<$<CONFIG:Debug>:spdlog::spdlog>)

endif ()
