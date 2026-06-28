cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(tl-parser LANGUAGES C)

set(SOURCES crc32.h crc32.c tlc.c tl-parser.c tl-parser.h tl-parser-tree.h tl-tl.h portable_endian.h)

if (WIN32)
  add_definitions("-D_CRT_SECURE_NO_WARNINGS")
  list(APPEND SOURCES wgetopt.c wgetopt.h)
  if (MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /utf-8 /wd4101 /wd4244 /wd4267")
  endif()
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

# Vendored wgetopt.c uses K&R declarations that are rejected by C23 (GCC 15+ default).
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set_target_properties(${PROJECT_NAME} PROPERTIES C_STANDARD 11)
endif()

if (NOT WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()
