cmake_minimum_required(VERSION 3.20)
project(udet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(udet
    src/main.cpp

    src/core/MarshalReader.cpp
    src/engines/rpgmaker/RxdataParser.cpp
    src/engines/rpgmaker/RpgMakerAdapter.cpp
)

target_include_directories(udet PRIVATE
    src
)

target_include_directories(udet PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)