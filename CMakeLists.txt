cmake_minimum_required(VERSION 3.5)
project(svase)

set(CMAKE_CXX_STANDARD 20)

option(CMAKE_BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Handle local prefix (TODO: parameterize)
set(DEP_INSTALL_DIR deps/install)
set(CMAKE_PREFIX_PATH ${DEP_INSTALL_DIR})
link_directories(${DEP_INSTALL_DIR}/lib64)
link_directories(${DEP_INSTALL_DIR}/lib)
include_directories(${DEP_INSTALL_DIR}/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_BUILD_TYPE Debug)

# Dependencies
find_package(slang REQUIRED)
find_package(fmt REQUIRED)

set(CLI11_TESTING OFF)
set(CLI11_PRECOMPILED ON)
add_subdirectory(deps/CLI11)


# Build and link
add_executable(svase
        src/design.cpp
        src/rewrite.cpp
        src/driver.cpp
        src/diag.cpp)
target_include_directories(svase PRIVATE include)
target_link_libraries(svase PRIVATE CLI11::CLI11)

if (CMAKE_BUILD_SHARED_LIBS)
    target_link_libraries(svase PRIVATE svlang fmt pthread)
else()
    target_link_libraries(svase PRIVATE svlang fmt pthread -static)
endif()

