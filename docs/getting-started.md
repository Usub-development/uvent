# Getting Started

This guide will help you set up **Uvent** and run your first asynchronous server.
This library doesn't have any dependencies.
---

## Installation

Clone the repository and build with CMake:

## 1. CMake FetchContent (Recommended)
```cmake
include(FetchContent)

FetchContent_Declare(uvent
        GIT_REPOSITORY https://github.com/Usub-development/uvent.git
        GIT_TAG main
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(uvent)

target_link_libraries(${PROJECT_NAME} PRIVATE usub::uvent)
```

### Using a Specific Version
For production use, it's recommended to pin to a specific version:
```cmake
FetchContent_Declare(uvent
        GIT_REPOSITORY https://github.com/Usub-development/uvent.git
        GIT_TAG v2.0.0
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
```

## 2. Manual installation
```bash
git clone https://github.com/Usub-development/uvent.git
cd uvent
mkdir build && cd build
cmake ..
make -j
```

## Example Project Setup
### Complete CMakeLists.txt Example
```cmake 
cmake_minimum_required(VERSION 3.20)
project(example CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(uvent
        GIT_REPOSITORY https://github.com/Usub-development/uvent.git
        GIT_TAG main
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(uvent)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE usub::uvent)
```