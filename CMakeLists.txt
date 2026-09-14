cmake_minimum_required(VERSION 4.1)
project(Bimodal_Content_Defined_Chunking_for_Backup_Streams)

set(CMAKE_CXX_STANDARD 20)

add_executable(Bimodal_Content_Defined_Chunking_for_Backup_Streams
        src/Baseline.cpp
        src/DataAndMethod.h
        src/DataAndMethod.cpp
        src/ChunkStore.cpp)
