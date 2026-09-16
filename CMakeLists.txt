cmake_minimum_required(VERSION 4.1)
project(Bimodal_Content_Defined_Chunking_for_Backup_Streams)

set(CMAKE_CXX_STANDARD 20)

# Homebrew 的 openssl 是 keg-only，未必在默认搜索路径；显式给出候选根目录。
if(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR)
    foreach(_openssl_candidate /opt/homebrew/opt/openssl /usr/local/opt/openssl)
        if(EXISTS "${_openssl_candidate}/include/openssl/evp.h")
            set(OPENSSL_ROOT_DIR "${_openssl_candidate}" CACHE PATH "OpenSSL install prefix")
        endif()
    endforeach()
endif()

find_package(OpenSSL REQUIRED)

add_executable(Bimodal_Content_Defined_Chunking_for_Backup_Streams
        src/Baseline.cpp
        src/DataAndMethod.h
        src/DataAndMethod.cpp
        src/ChunkStore.cpp
        src/Hash.h
        src/Hash.cpp)

target_link_libraries(Bimodal_Content_Defined_Chunking_for_Backup_Streams
        PRIVATE OpenSSL::Crypto)
