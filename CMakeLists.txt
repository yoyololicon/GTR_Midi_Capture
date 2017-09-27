cmake_minimum_required(VERSION 3.8)
project(midi_capture)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES main.cpp)
add_executable(midi_capture ${SOURCE_FILES})