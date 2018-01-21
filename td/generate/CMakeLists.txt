cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

file(MAKE_DIRECTORY auto/td/telegram)
file(MAKE_DIRECTORY auto/td/mtproto)

set(TL_TD_AUTO_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/auto PARENT_SCOPE)

set(TL_TD_AUTO
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/mtproto/mtproto_api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/mtproto/mtproto_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/mtproto/mtproto_api.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_api.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/telegram_api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/telegram_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/telegram_api.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/secret_api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/secret_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/secret_api.hpp
  PARENT_SCOPE
)

set(TL_TD_JSON_AUTO
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_api_json.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_api_json.h
  PARENT_SCOPE
)

set(TL_TD_API_TLO ${CMAKE_CURRENT_SOURCE_DIR}/scheme/td_api.tlo PARENT_SCOPE)

set(TL_C_AUTO
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_tdc_api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_tdc_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/td/telegram/td_tdc_api_inner.h
  PARENT_SCOPE
)

set(TL_GENERATE_COMMON_SOURCE
  generate_common.cpp

  tl_writer_cpp.cpp
  tl_writer_h.cpp
  tl_writer_hpp.cpp
  tl_writer_jni_cpp.cpp
  tl_writer_jni_h.cpp
  tl_writer_td.cpp

  tl_writer_cpp.h
  tl_writer_h.h
  tl_writer_hpp.h
  tl_writer_jni_cpp.h
  tl_writer_jni_h.h
  tl_writer_td.h
)

set(TL_GENERATE_C_SOURCE
  generate_c.cpp

  tl_writer_c.h
)

set(TL_GENERATE_JAVA_SOURCE
  generate_java.cpp

  tl_writer_java.cpp

  tl_writer_java.h
)

set(TL_GENERATE_JSON_SOURCE
  generate_json.cpp

  tl_json_converter.cpp

  tl_json_converter.h
)

if (NOT CMAKE_CROSSCOMPILING)
  find_program(PHP_EXECUTABLE php)

  if (PHP_EXECUTABLE)
    set(GENERATE_COMMON_CMD generate_common && ${PHP_EXECUTABLE} DoxygenTlDocumentationGenerator.php scheme/td_api.tl auto/td/telegram/td_api.h)
  else()
    set(GENERATE_COMMON_CMD generate_common)
  endif()

  add_executable(generate_common ${TL_GENERATE_COMMON_SOURCE})
  target_link_libraries(generate_common PRIVATE tdtl)
  add_custom_target(tl_generate_common
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${GENERATE_COMMON_CMD}
    COMMENT "Generate common tl source files"
    DEPENDS generate_common scheme/mtproto_api.tlo scheme/telegram_api.tlo scheme/secret_api.tlo scheme/td_api.tlo
  )
  if (TD_ENABLE_JNI)
    target_compile_definitions(generate_common PRIVATE TD_ENABLE_JNI=1)
  endif()

  add_executable(generate_c ${TL_GENERATE_C_SOURCE})
  target_link_libraries(generate_c PRIVATE tdtl)
  add_custom_target(tl_generate_c
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND generate_c
    COMMENT "Generate C tl source files"
    DEPENDS generate_c scheme/td_api.tlo
  )

  add_executable(td_generate_java_api ${TL_GENERATE_JAVA_SOURCE})
  target_link_libraries(td_generate_java_api PRIVATE tdtl)

  add_executable(generate_json ${TL_GENERATE_JSON_SOURCE})
  target_link_libraries(generate_json PRIVATE tdtl tdutils)
  add_custom_target(tl_generate_json
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND generate_json
    COMMENT "Generate JSON tl source files"
    DEPENDS generate_json scheme/td_api.tlo
  )

  if (TD_ENABLE_JNI)
    install(TARGETS td_generate_java_api RUNTIME DESTINATION bin)
    install(FILES JavadocTlDocumentationGenerator.php TlDocumentationGenerator.php DESTINATION bin/td/generate)
    install(FILES scheme/td_api.tlo scheme/td_api.tl DESTINATION bin/td/generate/scheme)
  endif()
endif()
