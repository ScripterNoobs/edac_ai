cmake_minimum_required(VERSION 3.16)
project(edac_ai_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(edac_api main.cpp)

# Enable sensible warnings on both GCC/Clang and MSVC.
if(MSVC)
  target_compile_options(edac_api PRIVATE /W4 /permissive-)
else()
  target_compile_options(edac_api PRIVATE -Wall -Wextra -Wpedantic)
endif()

