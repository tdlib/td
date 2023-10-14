include(CMakeFindDependencyMacro)
#TODO: write all external dependencies
include("${CMAKE_CURRENT_LIST_DIR}/TdTargets.cmake")
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/TdStaticTargets.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/TdStaticTargets.cmake")
endif()
