cmake_minimum_required (VERSION 2.8)

project (lightev)

aux_source_directory(. LIB_SRCS)

add_library(lightev SHARED ${LIB_SRCS}) 
