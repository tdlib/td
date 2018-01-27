cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

if (NOT OPENSSL_FOUND)
  find_package(OpenSSL REQUIRED)
endif()

set(SQLITE_SOURCE
  sqlite/sqlite3.c

  sqlite/sqlite3.h
  sqlite/sqlite3ext.h
  sqlite/sqlite3session.h
)

add_library(tdsqlite STATIC ${SQLITE_SOURCE})
target_include_directories(tdsqlite PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(tdsqlite SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(tdsqlite PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
target_compile_definitions(tdsqlite PRIVATE
  -DSQLITE_DEFAULT_MEMSTATUS=0
  -DSQLITE_OMIT_LOAD_EXTENSION
  -DSQLITE_OMIT_DECLTYPE
  -DSQLITE_OMIT_PROGRESS_CALLBACK
  #-DSQLITE_OMIT_DEPRECATED  # SQLCipher uses deprecated sqlite3_profile
  #-DSQLITE_OMIT_SHARED_CACHE
)
target_compile_definitions(tdsqlite PRIVATE -DSQLITE_HAS_CODEC -DSQLITE_TEMP_STORE=2 -DSQLITE_ENABLE_FTS5 -DSQLITE_DISABLE_LFS)

if (NOT WIN32)
  target_link_libraries(tdsqlite PRIVATE z)
  target_compile_definitions(tdsqlite PRIVATE -DHAVE_USLEEP -DNDEBUG=1)
endif()

if (GCC OR CLANG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-const-variable -Wno-unused-function")
  if (CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-parentheses-equality -Wno-unused-value")
  endif()
elseif (MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4996")
endif()

if (UWP)
  target_compile_options(tdsqlite PRIVATE /DSQLITE_OS_WINRT=1)
endif()

install(TARGETS tdsqlite EXPORT TdTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)
