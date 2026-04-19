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

# tl-parser is a build-time generator invoked while building other targets.
# When CI enables global sanitizer flags for runtime binaries, instrumenting
# this generator can make source-generation fail before tests even start.
if ((CMAKE_C_FLAGS MATCHES "(^| )-fsanitize=") OR (CMAKE_EXE_LINKER_FLAGS MATCHES "(^| )-fsanitize="))
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-sanitize=all)
  if (COMMAND target_link_options)
    target_link_options(${PROJECT_NAME} PRIVATE -fno-sanitize=all)
  else()
    set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS " -fno-sanitize=all")
  endif()
endif()

if (NOT WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()
