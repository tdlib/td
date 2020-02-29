cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

if (NOT DEFINED CMAKE_INSTALL_BINDIR)
  set(CMAKE_INSTALL_BINDIR "bin")
endif()

file(MAKE_DIRECTORY auto/td/telegram)
file(MAKE_DIRECTORY auto/td/mtproto)

set(TL_TD_AUTO_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/auto PARENT_SCOPE)

set(TD_AUTO_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/auto/td)

set(TL_TD_AUTO_SOURCE
  ${TD_AUTO_INCLUDE_DIR}/mtproto/mtproto_api.cpp
  ${TD_AUTO_INCLUDE_DIR}/mtproto/mtproto_api.h
  ${TD_AUTO_INCLUDE_DIR}/mtproto/mtproto_api.hpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/telegram_api.cpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/telegram_api.h
  ${TD_AUTO_INCLUDE_DIR}/telegram/telegram_api.hpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/secret_api.cpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/secret_api.h
  ${TD_AUTO_INCLUDE_DIR}/telegram/secret_api.hpp
  PARENT_SCOPE
)

set(TL_TD_API_AUTO_SOURCE
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_api.cpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_api.h
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_api.hpp
  PARENT_SCOPE
)

set(TL_TD_JSON_AUTO_SOURCE
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_api_json.cpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_api_json.h
  PARENT_SCOPE
)

set(TL_TD_API_TLO ${CMAKE_CURRENT_SOURCE_DIR}/scheme/td_api.tlo)
set(TL_TD_API_TLO ${TL_TD_API_TLO} PARENT_SCOPE)

set(TL_C_AUTO_SOURCE
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_tdc_api.cpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_tdc_api.h
  ${TD_AUTO_INCLUDE_DIR}/telegram/td_tdc_api_inner.h
  PARENT_SCOPE
)

set(TL_DOTNET_AUTO_SOURCE
  ${TD_AUTO_INCLUDE_DIR}/telegram/TdDotNetApi.cpp
  ${TD_AUTO_INCLUDE_DIR}/telegram/TdDotNetApi.h
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

  if ((CMAKE_SYSTEM_NAME MATCHES "FreeBSD") AND (CMAKE_SYSTEM_VERSION MATCHES "HBSD"))
    set(PHP_EXECUTABLE "PHP_EXECUTABLE-NOTFOUND")
  endif()

  if (PHP_EXECUTABLE AND NOT TD_ENABLE_DOTNET)
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
    DEPENDS generate_common scheme/mtproto_api.tlo scheme/telegram_api.tlo scheme/secret_api.tlo ${TL_TD_API_TLO} DoxygenTlDocumentationGenerator.php
  )
  if (TD_ENABLE_JNI)
    target_compile_definitions(generate_common PRIVATE TD_ENABLE_JNI=1)
  endif()
  if (TD_ENABLE_DOTNET)
    target_compile_definitions(generate_common PRIVATE DISABLE_HPP_DOCUMENTATION=1)
  endif()

  add_executable(generate_c ${TL_GENERATE_C_SOURCE})
  target_link_libraries(generate_c PRIVATE tdtl)
  add_custom_target(tl_generate_c
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND generate_c
    COMMENT "Generate C tl source files"
    DEPENDS generate_c ${TL_TD_API_TLO}
  )

  add_executable(td_generate_java_api ${TL_GENERATE_JAVA_SOURCE})
  target_link_libraries(td_generate_java_api PRIVATE tdtl)

  add_executable(generate_json ${TL_GENERATE_JSON_SOURCE})
  target_link_libraries(generate_json PRIVATE tdtl tdutils)
  add_custom_target(tl_generate_json
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND generate_json
    COMMENT "Generate JSON tl source files"
    DEPENDS generate_json ${TL_TD_API_TLO}
  )

  if (TD_ENABLE_JNI)
    install(TARGETS td_generate_java_api RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES JavadocTlDocumentationGenerator.php TlDocumentationGenerator.php DESTINATION "${CMAKE_INSTALL_BINDIR}/td/generate")
    install(FILES scheme/td_api.tlo scheme/td_api.tl DESTINATION "${CMAKE_INSTALL_BINDIR}/td/generate/scheme")
  endif()

  if (TD_ENABLE_DOTNET)
    if (PHP_EXECUTABLE)
      set(GENERATE_DOTNET_API_CMD td_generate_dotnet_api ${TL_TD_API_TLO} && ${PHP_EXECUTABLE} DotnetTlDocumentationGenerator.php scheme/td_api.tl auto/td/telegram/TdDotNetApi.h)
    else()
      set(GENERATE_DOTNET_API_CMD td_generate_dotnet_api ${TL_TD_API_TLO})
    endif()

    add_executable(td_generate_dotnet_api generate_dotnet.cpp tl_writer_dotnet.h)
    target_link_libraries(td_generate_dotnet_api PRIVATE tdtl)
    add_custom_target(generate_dotnet_api
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMAND ${GENERATE_DOTNET_API_CMD} ${TL_TD_API_TLO}
      COMMENT "Generate .NET API files"
      DEPENDS td_generate_dotnet_api ${TL_TD_API_TLO} DotnetTlDocumentationGenerator.php
    )
  endif()

  add_executable(remove_documentation remove_documentation.cpp)
  target_link_libraries(remove_documentation PRIVATE tdtl)
endif()
