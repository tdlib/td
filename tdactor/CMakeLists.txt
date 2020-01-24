cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()

#SOURCE SETS
set(TDACTOR_SOURCE
  td/actor/impl/ConcurrentScheduler.cpp
  td/actor/impl/Scheduler.cpp
  td/actor/MultiPromise.cpp
  td/actor/Timeout.cpp

  td/actor/impl/Actor-decl.h
  td/actor/impl/Actor.h
  td/actor/impl/ActorId-decl.h
  td/actor/impl/ActorId.h
  td/actor/impl/ActorInfo-decl.h
  td/actor/impl/ActorInfo.h
  td/actor/impl/EventFull-decl.h
  td/actor/impl/EventFull.h
  td/actor/impl/ConcurrentScheduler.h
  td/actor/impl/Event.h
  td/actor/impl/Scheduler-decl.h
  td/actor/impl/Scheduler.h
  td/actor/MultiPromise.h
  td/actor/PromiseFuture.h
  td/actor/SchedulerLocalStorage.h
  td/actor/SignalSlot.h
  td/actor/SleepActor.h
  td/actor/Timeout.h
  td/actor/actor.h
)

set(TDACTOR_TEST_SOURCE
  ${CMAKE_CURRENT_SOURCE_DIR}/test/actors_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/actors_simple.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/actors_workers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/actors_bugs.cpp
  PARENT_SCOPE
)

#RULES

#LIBRARIES

add_library(tdactor STATIC ${TDACTOR_SOURCE})
target_include_directories(tdactor PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(tdactor PUBLIC tdutils)

if (NOT CMAKE_CROSSCOMPILING)
  add_executable(example example/example.cpp)
  target_link_libraries(example PRIVATE tdactor)
endif()

install(TARGETS tdactor EXPORT TdTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
