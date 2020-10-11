cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

if (NOT OPENSSL_FOUND)
  find_package(OpenSSL REQUIRED)
  find_package(ZLIB REQUIRED)
endif()

#TODO: all benchmarks in one file
add_executable(bench_crypto bench_crypto.cpp)
target_link_libraries(bench_crypto PRIVATE tdutils ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES})
if (WIN32)
  if (MINGW)
    target_link_libraries(bench_crypto PRIVATE ws2_32 mswsock crypt32)
  else()
    target_link_libraries(bench_crypto PRIVATE ws2_32 Mswsock Crypt32)
  endif()
endif()
target_include_directories(bench_crypto SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})

add_executable(bench_actor bench_actor.cpp)
target_link_libraries(bench_actor PRIVATE tdactor tdutils)

add_executable(bench_http bench_http.cpp)
target_link_libraries(bench_http PRIVATE tdnet tdutils)

add_executable(bench_http_server bench_http_server.cpp)
target_link_libraries(bench_http_server PRIVATE tdnet tdutils)

add_executable(bench_http_server_cheat bench_http_server_cheat.cpp)
target_link_libraries(bench_http_server_cheat PRIVATE tdnet tdutils)

add_executable(bench_http_server_fast bench_http_server_fast.cpp)
target_link_libraries(bench_http_server_fast PRIVATE tdnet tdutils)

add_executable(bench_http_reader bench_http_reader.cpp)
target_link_libraries(bench_http_reader PRIVATE tdnet tdutils)

add_executable(bench_handshake bench_handshake.cpp)
target_link_libraries(bench_handshake PRIVATE tdcore tdutils)

add_executable(bench_db bench_db.cpp)
target_link_libraries(bench_db PRIVATE tdactor tddb tdutils)

add_executable(bench_tddb bench_tddb.cpp)
target_link_libraries(bench_tddb PRIVATE tdcore tddb tdutils)

add_executable(bench_misc bench_misc.cpp)
target_link_libraries(bench_misc PRIVATE tdcore tdutils)

add_executable(check_proxy check_proxy.cpp)
target_link_libraries(check_proxy PRIVATE tdclient tdutils)

add_executable(check_tls check_tls.cpp)
target_link_libraries(check_tls PRIVATE tdutils)

add_executable(rmdir rmdir.cpp)
target_link_libraries(rmdir PRIVATE tdutils)

add_executable(wget wget.cpp)
target_link_libraries(wget PRIVATE tdnet tdutils)

add_executable(bench_empty bench_empty.cpp)
target_link_libraries(bench_empty PRIVATE tdutils)

if (NOT WIN32 AND NOT CYGWIN)
  add_executable(bench_log bench_log.cpp)
  target_link_libraries(bench_log PRIVATE tdutils)

  set_source_files_properties(bench_queue.cpp PROPERTIES COMPILE_FLAGS -Wno-deprecated-declarations)
  add_executable(bench_queue bench_queue.cpp)
  target_link_libraries(bench_queue PRIVATE tdutils)
endif()
