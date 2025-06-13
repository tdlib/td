cmake_minimum_required(VERSION 3.7)
project(linux-cli)

set(CMAKE_CXX_STANDARD 17)

find_package(Td REQUIRED)

add_executable(linux-cli src/main.cpp)
target_link_libraries(linux-cli Td::tdjson Td::tdclient Td::tdcore Td::tdapi Td::tdutils)
