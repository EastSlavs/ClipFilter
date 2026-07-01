cmake_minimum_required(VERSION 3.10)
project(ClipFilter)

set(CMAKE_EXE_LINKER_FLAGS "-static")

add_executable(ClipFilter WIN32 main.cpp resource.rc)