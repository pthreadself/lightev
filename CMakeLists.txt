cmake_minimum_required (VERSION 2.8)

project (lightev)
include_directories (./include)
aux_source_directory(./src LIB_SRCS)

add_library(lightev SHARED ${LIB_SRCS}) 
