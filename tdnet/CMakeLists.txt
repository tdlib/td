cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()

if (NOT OPENSSL_FOUND)
  find_package(OpenSSL REQUIRED)
  find_package(ZLIB REQUIRED)
endif()

#SOURCE SETS
set(TDNET_SOURCE
  td/net/GetHostByNameActor.cpp
  td/net/HttpChunkedByteFlow.cpp
  td/net/HttpConnectionBase.cpp
  td/net/HttpContentLengthByteFlow.cpp
  td/net/HttpFile.cpp
  td/net/HttpInboundConnection.cpp
  td/net/HttpOutboundConnection.cpp
  td/net/HttpProxy.cpp
  td/net/HttpQuery.cpp
  td/net/HttpReader.cpp
  td/net/Socks5.cpp
  td/net/SslStream.cpp
  td/net/TcpListener.cpp
  td/net/TransparentProxy.cpp
  td/net/Wget.cpp

  td/net/GetHostByNameActor.h
  td/net/HttpChunkedByteFlow.h
  td/net/HttpConnectionBase.h
  td/net/HttpContentLengthByteFlow.h
  td/net/HttpFile.h
  td/net/HttpHeaderCreator.h
  td/net/HttpInboundConnection.h
  td/net/HttpOutboundConnection.h
  td/net/HttpProxy.h
  td/net/HttpQuery.h
  td/net/HttpReader.h
  td/net/NetStats.h
  td/net/Socks5.h
  td/net/SslStream.h
  td/net/TcpListener.h
  td/net/TransparentProxy.h
  td/net/Wget.h
)

#RULES
#LIBRARIES

add_library(tdnet STATIC ${TDNET_SOURCE})
target_include_directories(tdnet PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(tdnet SYSTEM PRIVATE $<BUILD_INTERFACE:${OPENSSL_INCLUDE_DIR}>)
target_link_libraries(tdnet PUBLIC tdutils tdactor)
if (NOT EMSCRIPTEN)
  target_link_libraries(tdnet PRIVATE ${OPENSSL_SSL_LIBRARY})
endif()
target_link_libraries(tdnet PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES})

if (WIN32)
  if (MINGW)
    target_link_libraries(tdnet PRIVATE ws2_32 mswsock crypt32)
  else()
    target_link_libraries(tdnet PRIVATE ws2_32 Mswsock Crypt32)
  endif()
endif()

install(TARGETS tdnet EXPORT TdTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
