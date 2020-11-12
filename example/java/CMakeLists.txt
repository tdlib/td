cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

project(TdJavaExample VERSION 1.0 LANGUAGES CXX)

if (POLICY CMP0054)
  # do not expand quoted arguments
  cmake_policy(SET CMP0054 NEW)
endif()

find_package(Td REQUIRED)

if (NOT JNI_FOUND)
  find_package(JNI REQUIRED)
endif()
message(STATUS "Found JNI: ${JNI_INCLUDE_DIRS} ${JNI_LIBRARIES}")

if (NOT Java_FOUND)
  find_package(Java REQUIRED)
endif()
message(STATUS "Found Java: ${Java_JAVAC_EXECUTABLE} ${Java_JAVADOC_EXECUTABLE}")

# Generating TdApi.java
find_program(PHP_EXECUTABLE php)
if ((CMAKE_SYSTEM_NAME MATCHES "FreeBSD") AND (CMAKE_SYSTEM_VERSION MATCHES "HBSD"))
  set(PHP_EXECUTABLE "PHP_EXECUTABLE-NOTFOUND")
endif()

set(TD_API_JAVA_PACKAGE "org/drinkless/tdlib")
set(TD_API_JAVA_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(TD_API_TLO_PATH ${CMAKE_CURRENT_SOURCE_DIR}/td/bin/td/generate/scheme/td_api.tlo)
set(TD_API_TL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/td/bin/td/generate/scheme/td_api.tl)
set(JAVADOC_TL_DOCUMENTATION_GENERATOR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/td/bin/td/generate/JavadocTlDocumentationGenerator.php)
set(GENERATE_JAVA_API_CMD ${CMAKE_CURRENT_SOURCE_DIR}/td/bin/td_generate_java_api TdApi ${TD_API_TLO_PATH} ${TD_API_JAVA_PATH} ${TD_API_JAVA_PACKAGE})
if (PHP_EXECUTABLE)
  set(GENERATE_JAVA_API_CMD ${GENERATE_JAVA_API_CMD} && ${PHP_EXECUTABLE} ${JAVADOC_TL_DOCUMENTATION_GENERATOR_PATH} ${TD_API_TL_PATH} ${TD_API_JAVA_PATH}/${TD_API_JAVA_PACKAGE}/TdApi.java)
endif()

add_custom_target(td_generate_java_api
  COMMAND ${GENERATE_JAVA_API_CMD}
  COMMENT "Generating Java TDLib API source files"
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/td/bin/td_generate_java_api ${TD_API_TLO_PATH} ${TD_API_TL_PATH} ${JAVADOC_TL_DOCUMENTATION_GENERATOR_PATH}
)

set(JAVA_SOURCE_PATH "${TD_API_JAVA_PATH}/${TD_API_JAVA_PACKAGE}")
get_filename_component(JAVA_OUTPUT_DIRECTORY ${CMAKE_INSTALL_PREFIX}/bin REALPATH BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
file(MAKE_DIRECTORY ${JAVA_OUTPUT_DIRECTORY})
add_custom_target(build_java
  COMMAND ${Java_JAVAC_EXECUTABLE} -d ${JAVA_OUTPUT_DIRECTORY} ${JAVA_SOURCE_PATH}/example/Example.java ${JAVA_SOURCE_PATH}/Client.java ${JAVA_SOURCE_PATH}/Log.java ${JAVA_SOURCE_PATH}/TdApi.java
  COMMENT "Building Java code"
  DEPENDS td_generate_java_api
)

add_custom_target(generate_javadoc
  COMMAND ${Java_JAVADOC_EXECUTABLE} -d ${JAVA_OUTPUT_DIRECTORY}/../docs org.drinkless.tdlib
  WORKING_DIRECTORY ${TD_API_JAVA_PATH}
  COMMENT "Generating Javadoc documentation"
  DEPENDS td_generate_java_api
)

# Building shared library
add_library(tdjni SHARED
  td_jni.cpp
)
target_include_directories(tdjni PRIVATE ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
target_link_libraries(tdjni PRIVATE Td::TdStatic ${JAVA_JVM_LIBRARY})
target_compile_definitions(tdjni PRIVATE PACKAGE_NAME="${TD_API_JAVA_PACKAGE}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(GCC 1)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CLANG 1)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
  set(INTEL 1)
elseif (NOT MSVC)
  message(FATAL_ERROR "Compiler isn't supported")
endif()

include(CheckCXXCompilerFlag)

if (GCC OR CLANG OR INTEL)
  if (WIN32 AND INTEL)
    set(STD14_FLAG /Qstd=c++14)
  else()
    set(STD14_FLAG -std=c++14)
  endif()
  check_cxx_compiler_flag(${STD14_FLAG} HAVE_STD14)
  if (NOT HAVE_STD14)
    string(REPLACE "c++14" "c++1y" STD14_FLAG "${STD14_FLAG}")
    check_cxx_compiler_flag(${STD14_FLAG} HAVE_STD1Y)
    set(HAVE_STD14 ${HAVE_STD1Y})
  endif()

  target_compile_options(tdjni PRIVATE "${STD14_FLAG}")
elseif (MSVC)
  set(HAVE_STD14 MSVC_VERSION>=1900)
endif()

if (NOT HAVE_STD14)
  message(FATAL_ERROR "No C++14 support in the compiler. Please upgrade the compiler.")
endif()

add_dependencies(tdjni td_generate_java_api build_java generate_javadoc)

install(TARGETS tdjni
  LIBRARY DESTINATION bin
  RUNTIME DESTINATION bin
)
if (MSVC AND VCPKG_TOOLCHAIN)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/" DESTINATION bin FILES_MATCHING PATTERN "*.dll" PATTERN "*.pdb")
endif()
