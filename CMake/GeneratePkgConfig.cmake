function(get_relative_link OUTPUT PATH)
  if (PATH MATCHES "^[$]<[$]<CONFIG:DEBUG>:")
    set(${OUTPUT} "" PARENT_SCOPE)
    return()
  endif()
  string(REGEX REPLACE "^[$]<[$]<NOT:[$]<CONFIG:DEBUG>>:(.*)>$" "\\1" PATH "${PATH}")

  get_filename_component(NAME "${PATH}" NAME_WE)
  if (IS_ABSOLUTE ${PATH})
    get_filename_component(DIRECTORY_NAME "${PATH}" DIRECTORY)
    if (WIN32)
      # On Windows, library files can be libname.lib or name.lib
      get_filename_component(FULL_NAME "${PATH}" NAME)
      # Extract library name without lib prefix and extension
      string(REGEX REPLACE "^lib(.+)\\.[^.]+$" "\\1" LIB_NAME "${FULL_NAME}")
      # If the regex didn't match (no lib prefix), use the name without extension
      if ("${LIB_NAME}" STREQUAL "${FULL_NAME}")
        set(LIB_NAME "${NAME}")
      endif()
      set(${OUTPUT} "-L\"${DIRECTORY_NAME}\" -l${LIB_NAME}" PARENT_SCOPE)
    else()
      get_filename_component(FULL_NAME "${PATH}" NAME)
      # Extract library name without lib prefix and extension for all platforms
      string(REGEX REPLACE "^lib(.+)\\.[^.]+$" "\\1" LIB_NAME "${FULL_NAME}")
      set(${OUTPUT} "-L\"${DIRECTORY_NAME}\" -l${LIB_NAME}" PARENT_SCOPE)
    endif()
    return()
  endif()

  if (NOT WIN32 AND NAME MATCHES "^lib")
    string(REGEX REPLACE "^lib" "-l" LINK "${NAME}")
  elseif (NAME MATCHES "^-")
    set(LINK "${NAME}")
  else()
    string(CONCAT LINK "-l" "${NAME}")
  endif()
  set(${OUTPUT} "${LINK}" PARENT_SCOPE)
endfunction()

function(generate_pkgconfig TARGET DESCRIPTION)
  # message("Generating pkg-config for ${TARGET}")
  get_filename_component(PREFIX "${CMAKE_INSTALL_PREFIX}" REALPATH)

  # Get the target type to handle interface libraries differently
  get_target_property(LIBRARY_TYPE "${TARGET}" TYPE)
  # For interface libraries, use INTERFACE_LINK_LIBRARIES instead of LINK_LIBRARIES
  if ("${LIBRARY_TYPE}" STREQUAL "INTERFACE_LIBRARY")
    get_target_property(LIST "${TARGET}" INTERFACE_LINK_LIBRARIES)
  else()
    get_target_property(LIST "${TARGET}" LINK_LIBRARIES)
  endif()

  # Handle the case when no libraries are found
  if ("${LIST}" STREQUAL "LIST-NOTFOUND")
    set(LIST "")
  endif()

  # Special handling for tdcore interface library
  if ("${TARGET}" STREQUAL "tdcore" AND "${LIBRARY_TYPE}" STREQUAL "INTERFACE_LIBRARY")
    # For tdcore interface library, we need to link to the actual part libraries
    # instead of the non-existent tdcore library
    set(TDCORE_LIBS "")
    set(COMBINED_REQS "")
    set(COMBINED_LIBS "")
    
    foreach (PART_LIB ${LIST})
      if (TARGET "${PART_LIB}" AND "${PART_LIB}" MATCHES "^tdcore_part[0-9]+$")
        # Add the actual part library to link against
        list(APPEND TDCORE_LIBS "-l${PART_LIB}")
        
        # Collect dependencies from the parts
        get_target_property(PART_LIST "${PART_LIB}" LINK_LIBRARIES)
        if (NOT "${PART_LIST}" STREQUAL "PART_LIST-NOTFOUND")
          foreach (PART_DEP ${PART_LIST})
            if (TARGET "${PART_DEP}")
              list(APPEND COMBINED_REQS "${PART_DEP}")
            else()
              list(APPEND COMBINED_LIBS "${PART_DEP}")
            endif()
          endforeach()
        endif()
      elseif (TARGET "${PART_LIB}")
        list(APPEND COMBINED_REQS "${PART_LIB}")
      else()
        list(APPEND COMBINED_LIBS "${PART_LIB}")
      endif()
    endforeach()

    # Remove duplicates
    if (COMBINED_REQS)
      list(REMOVE_DUPLICATES COMBINED_REQS)
    endif()
    if (COMBINED_LIBS)
      list(REMOVE_DUPLICATES COMBINED_LIBS)
    endif()
    if (TDCORE_LIBS)
      list(REMOVE_DUPLICATES TDCORE_LIBS)
    endif()

    set(LIST "")
    list(APPEND LIST ${COMBINED_REQS})
    list(APPEND LIST ${COMBINED_LIBS})

    # Set a flag to use different Libs line for tdcore
    set(USE_TDCORE_PARTS TRUE)
  else()
    set(USE_TDCORE_PARTS FALSE)
  endif()

  set(REQS "")
  set(LIBS "")
  foreach (LIB ${LIST})
    if (TARGET "${LIB}")
      # Skip internal tdcore parts as they don't have their own .pc files
      if (NOT "${LIB}" MATCHES "^tdcore_part[0-9]+$")
        set(HAS_REQS 1)
        list(APPEND REQS "${LIB}")
      endif()
    else()
      set(HAS_LIBS 1)
      get_relative_link(LINK "${LIB}")
      if (NOT "${LINK}" STREQUAL "")
        list(APPEND LIBS "${LINK}")
      endif()
    endif()
  endforeach()

  if (HAS_REQS)
    set(REQUIRES "")
    foreach (REQ ${REQS})
      set(REQUIRES "${REQUIRES} ${REQ}")
    endforeach()
    set(REQUIRES "Requires.private:${REQUIRES}\n")
  endif()
  if (HAS_LIBS)
    set(LIBRARIES "")
    list(REVERSE LIBS)
    list(REMOVE_DUPLICATES LIBS)
    foreach (LIB ${LIBS})
      set(LIBRARIES " ${LIB}${LIBRARIES}")
    endforeach()
    set(LIBRARIES "Libs.private:${LIBRARIES}\n")
  endif()

  if (IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(PKGCONFIG_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
  else()
    set(PKGCONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
  endif()

  if (IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(PKGCONFIG_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
  else()
    set(PKGCONFIG_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
  endif()

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig")
  
  # Generate the correct Libs line based on library type
  if (USE_TDCORE_PARTS)
    # For tdcore interface library, link to the actual part libraries
    set(LIBS_LINE "")
    foreach (PART_LIB ${TDCORE_LIBS})
      set(LIBS_LINE "${LIBS_LINE} ${PART_LIB}")
    endforeach()
    set(LIBS_LINE "Libs: -L\"${PKGCONFIG_LIBDIR}\"${LIBS_LINE}")
  else()
    set(LIBS_LINE "Libs: -L\"${PKGCONFIG_LIBDIR}\" -l${TARGET}")
  endif()
  
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig/${TARGET}.pc" CONTENT
"prefix=${PREFIX}

Name: ${TARGET}
Description: ${DESCRIPTION}
Version: ${PROJECT_VERSION}

CFlags: -I\"${PKGCONFIG_INCLUDEDIR}\"
${LIBS_LINE}
${REQUIRES}${LIBRARIES}")

  if ("${LIBRARY_TYPE}" STREQUAL "STATIC_LIBRARY" OR "${LIBRARY_TYPE}" STREQUAL "SHARED_LIBRARY")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig/${TARGET}.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  elseif ("${LIBRARY_TYPE}" STREQUAL "INTERFACE_LIBRARY")
    # Interface libraries are also supported, install the .pc file
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig/${TARGET}.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  else()
    message(FATAL_ERROR "Don't know how to handle ${TARGET} of type ${LIBRARY_TYPE}")
  endif()
endfunction()
