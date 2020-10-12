cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

#SOURCE SETS
set(TD_TEST_SOURCE
  ${CMAKE_CURRENT_SOURCE_DIR}/db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mtproto.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/message_entities.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/poll.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/secret.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/secure_storage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/set_with_position.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/string_cleaning.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tdclient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tqueue.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/data.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/data.h

  ${TDUTILS_TEST_SOURCE}
  ${TDACTOR_TEST_SOURCE}
)
set(TD_TEST_SOURCE ${TD_TEST_SOURCE} PARENT_SCOPE)

set(TESTS_MAIN
  main.cpp
)

#add_library(all_tests STATIC ${TD_TEST_SOURCE})
#target_include_directories(all_tests PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
#target_link_libraries(all_tests PRIVATE tdcore tdclient)

if (NOT CMAKE_CROSSCOMPILING OR EMSCRIPTEN)
  #Tests
  add_executable(test-tdutils EXCLUDE_FROM_ALL ${TESTS_MAIN} ${TDUTILS_TEST_SOURCE})
  add_executable(run_all_tests ${TESTS_MAIN} ${TD_TEST_SOURCE})
  if (CLANG AND NOT CYGWIN AND NOT EMSCRIPTEN AND NOT (CMAKE_HOST_SYSTEM_NAME MATCHES "OpenBSD"))
    target_compile_options(test-tdutils PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(run_all_tests PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined -fno-sanitize=vptr")
  endif()
  target_include_directories(run_all_tests PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_include_directories(test-tdutils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_link_libraries(test-tdutils PRIVATE tdutils)
  target_link_libraries(run_all_tests PRIVATE tdcore tdclient)

  if (CLANG)
#    add_executable(fuzz_url fuzz_url.cpp)
#    target_link_libraries(fuzz_url PRIVATE tdcore)
#    target_compile_options(fuzz_url PRIVATE "-fsanitize-coverage=trace-pc-guard")
  endif()

  add_test(run_all_tests run_all_tests)
endif()
