cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(TdExample VERSION 1.0 LANGUAGES CXX)

if (NOT Td_DIR)
  set(Td_DIR "${CMAKE_CURRENT_SOURCE_DIR}/td/lib/cmake/Td")
endif()

find_package(Td 1.8.63 REQUIRED)

add_executable(tdjson_example tdjson_example.cpp)
target_link_libraries(tdjson_example PRIVATE Td::TdJson)
target_include_directories(tdjson_example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_property(TARGET tdjson_example PROPERTY CXX_STANDARD 11)

add_executable(td_example td_example.cpp)
target_link_libraries(td_example PRIVATE Td::TdStatic)
target_include_directories(td_example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_property(TARGET td_example PROPERTY CXX_STANDARD 14)
