cmake_minimum_required(VERSION 3.16)

project(light_whisper_server VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Network Core)

add_library(server server.h server.cpp network/protocol.h network/protocol.cpp)

target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(server PUBLIC Qt6::Core Qt6::Network)

add_executable(light_whisper_server main.cpp)

target_link_libraries(light_whisper_server PRIVATE server)
