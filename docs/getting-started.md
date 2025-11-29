# Getting Started

This guide will help you set up **Uvent** and run your first asynchronous server.  
This library doesn't have any dependencies.

---

## Installation

Clone the repository and build with CMake.

### Backend selection

Uvent automatically selects the optimal backend for your platform:

| Platform    | Backend                                        |
|-------------|------------------------------------------------|
| Linux       | `epoll` (default) or **`io_uring`** (optional) |
| macOS / BSD | `kqueue`                                       |
| Windows     | **IOCP** (enabled automatically)               |

### Enabling Linux io_uring backend

To build with the io_uring engine instead of epoll:

```bash
cmake -DUVENT_ENABLE_IO_URING=ON ..
make -j
```

Or via FetchContent:

```cmake
set(UVENT_ENABLE_IO_URING ON)
```

Requires a kernel with io_uring support (5.6+ recommended).

---

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

```cmake
FetchContent_Declare(uvent
        GIT_REPOSITORY https://github.com/Usub-development/uvent.git
        GIT_TAG v2.0.0
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
```

---

## 2. Manual installation

```bash
git clone https://github.com/Usub-development/uvent.git
cd uvent
mkdir build && cd build
cmake ..
make -j
```

To enable `io_uring` during manual build:

```bash
cmake -DUVENT_ENABLE_IO_URING=ON ..
make -j
```

---

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