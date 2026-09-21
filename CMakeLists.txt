cmake_minimum_required(VERSION 3.18)
project(CrashHandler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "Source/*")
add_executable(CrashHandler ${SOURCE_FILES})

if(MSVC)
    target_compile_options(CrashHandler PRIVATE /utf-8)
endif()

# Call:
# cmake . -A x64 -B Build
