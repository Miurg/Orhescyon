cmake_minimum_required(VERSION 3.21)
project(Orhescyon VERSION 0.1.0 LANGUAGES CXX)

add_library(Orhescyon INTERFACE) 

target_include_directories(Orhescyon INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(Orhescyon INTERFACE cxx_std_20)