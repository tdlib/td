if ((CMAKE_MAJOR_VERSION LESS 3) OR (CMAKE_VERSION VERSION_LESS "3.10"))
  message(FATAL_ERROR "CMake >= 3.10 is required")
endif()

# Generates files for MIME type <-> extension conversions
# DEPENDS ON: gperf grep

if (NOT TDUTILS_MIME_TYPE)
  return()
endif()

file(MAKE_DIRECTORY auto)

set(TDMIME_SOURCE
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/mime_type_to_extension.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/extension_to_mime_type.cpp
)
set(TDMIME_AUTO
  ${TDMIME_SOURCE}
  PARENT_SCOPE
)

add_custom_target(tdmime_auto DEPENDS ${TDMIME_SOURCE})

if (NOT CMAKE_CROSSCOMPILING)
  function(tdutils_disable_sanitizers_for_generator target_name)
    if ((CMAKE_C_FLAGS MATCHES "(^| )-fsanitize=") OR
        (CMAKE_CXX_FLAGS MATCHES "(^| )-fsanitize=") OR
        (CMAKE_EXE_LINKER_FLAGS MATCHES "(^| )-fsanitize="))
      target_compile_options(${target_name} PRIVATE -fno-sanitize=all)
      if (COMMAND target_link_options)
        target_link_options(${target_name} PRIVATE -fno-sanitize=all)
      else()
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY LINK_FLAGS " -fno-sanitize=all")
      endif()
    endif()
  endfunction()

  find_program(GPERF_EXECUTABLE gperf)
  if (NOT GPERF_EXECUTABLE)
    message(FATAL_ERROR "Could NOT find gperf. Add path to gperf executable to PATH environment variable or specify it manually using GPERF_EXECUTABLE option, i.e. 'cmake -DGPERF_EXECUTABLE:FILEPATH=\"<path to gperf executable>\"'.")
  endif()

  set(GPERF_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/auto/mime_type_to_extension.gperf
    ${CMAKE_CURRENT_SOURCE_DIR}/auto/extension_to_mime_type.gperf
  )

  set(GPERF_GEN_SOURCE generate_mime_types_gperf.cpp)

  add_executable(generate_mime_types_gperf ${GPERF_GEN_SOURCE})
  tdutils_disable_sanitizers_for_generator(generate_mime_types_gperf)

  add_custom_command(
    OUTPUT ${GPERF_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND generate_mime_types_gperf mime_types.txt ${GPERF_FILES}
    DEPENDS generate_mime_types_gperf mime_types.txt
  )

  set(MIME_TYPE_TO_EXTENSION_CMD ${GPERF_EXECUTABLE} -m100 --output-file=auto/mime_type_to_extension.cpp auto/mime_type_to_extension.gperf)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/auto/mime_type_to_extension.cpp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${MIME_TYPE_TO_EXTENSION_CMD}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/auto/mime_type_to_extension.gperf
  )

  set(EXTENSION_TO_MIME_TYPE_CMD ${GPERF_EXECUTABLE} -m100 --output-file=auto/extension_to_mime_type.cpp auto/extension_to_mime_type.gperf)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/auto/extension_to_mime_type.cpp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${EXTENSION_TO_MIME_TYPE_CMD}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/auto/extension_to_mime_type.gperf
  )
endif()
