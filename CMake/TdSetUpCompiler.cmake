# Configures C++23 compiler, setting TDLib-specific compilation options.

option(TD_STRICT_CI_WARNINGS "Treat selected warning classes as errors in CI-oriented builds." OFF)

# When ON, upgrades below-recommended compiler version warnings to fatal errors.
# Set this in CI to enforce the latest tested toolchain baseline; leave OFF for
# local developer builds where older-but-still-supported compilers are common.
option(TD_STRICT_COMPILER_VERSIONS "Fail the configure step when compiler is below the latest recommended version." OFF)

# Prefer lld where supported because it links large C++ targets much faster
# than GNU ld on most Linux hosts.
option(TD_ENABLE_LLD "Use -fuse-ld=lld when the compiler supports it." ON)

# Optional local optimization for developer builds; keep OFF in portable CI.
option(TD_ENABLE_NATIVE_ARCH "Enable -march=native/-mtune=native for local builds." OFF)

function(td_set_up_compiler)
  set(CMAKE_EXPORT_COMPILE_COMMANDS 1 PARENT_SCOPE)

  set(CMAKE_POSITION_INDEPENDENT_CODE ON PARENT_SCOPE)

  include(illumos)

  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(GCC 1)
    set(GCC 1 PARENT_SCOPE)
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CLANG 1)
    set(CLANG 1 PARENT_SCOPE)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    set(INTEL 1)
    set(INTEL 1 PARENT_SCOPE)
  elseif (NOT MSVC)
    message(FATAL_ERROR "Compiler isn't supported")
  endif()

  include(CheckCXXCompilerFlag)

  if (GCC OR CLANG OR INTEL)
    if (WIN32 AND INTEL)
      set(STD23_FLAG /Qstd=c++23)
    else()
      set(STD23_FLAG -std=c++23)
    endif()
    if (GCC AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0))
      message(FATAL_ERROR "No C++23 support in the compiler. Please upgrade the compiler to at least GCC 13.0.")
    endif()
    if (CLANG AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0))
      message(FATAL_ERROR "No C++23 support in the compiler. Please upgrade the compiler to at least clang 16.0.")
    endif()
    if (GCC AND (NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0) AND
      (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15.2))
      if (TD_STRICT_COMPILER_VERSIONS)
        message(FATAL_ERROR "TD_STRICT_COMPILER_VERSIONS is ON: GCC 15.2+ is required. Found ${CMAKE_CXX_COMPILER_VERSION}.")
      else()
        message(WARNING "GCC 15.2+ is recommended for the best C++23 support and diagnostics.")
      endif()
    endif()
    if (CLANG AND (NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0) AND
        (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22.1.3))
      if (TD_STRICT_COMPILER_VERSIONS)
        message(FATAL_ERROR "TD_STRICT_COMPILER_VERSIONS is ON: Clang 22.1.3+ is required. Found ${CMAKE_CXX_COMPILER_VERSION}.")
      else()
        message(WARNING "Clang 22.1.3+ is recommended for the best C++23 support and diagnostics.")
      endif()
    endif()
    check_cxx_compiler_flag(${STD23_FLAG} HAVE_STD23)
  elseif (MSVC)
    set(HAVE_STD23 MSVC_VERSION>=1936) # MSVC 2022 version 17.6
    if (MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_VERSION LESS 1940)
      if (TD_STRICT_COMPILER_VERSIONS)
        message(FATAL_ERROR "TD_STRICT_COMPILER_VERSIONS is ON: MSVC 19.40+ (VS 2022 17.10+) is required.")
      else()
        message(WARNING "MSVC 19.40+ (VS 2022 17.10+) is recommended for the best C++23 support and diagnostics.")
      endif()
    endif()
  endif()

  if (NOT HAVE_STD23)
    message(FATAL_ERROR "No C++23 support in the compiler. Please upgrade the compiler.")
  endif()

  if (MSVC)
    if (CMAKE_CXX_FLAGS_DEBUG MATCHES "/RTC1")
      string(REPLACE "/RTC1" " " CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    endif()
    add_definitions(-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /utf-8 /GR- /W4 /wd4100 /wd4127 /wd4324 /wd4505 /wd4814 /wd4702 /bigobj")
  elseif (CLANG OR GCC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${STD23_FLAG} -fno-omit-frame-pointer -fno-exceptions -fno-rtti")
    if (TD_ENABLE_NATIVE_ARCH AND NOT CMAKE_CROSSCOMPILING)
      check_cxx_compiler_flag("-march=native" TD_HAVE_MARCH_NATIVE)
      if (TD_HAVE_MARCH_NATIVE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
      endif()
      check_cxx_compiler_flag("-mtune=native" TD_HAVE_MTUNE_NATIVE)
      if (TD_HAVE_MTUNE_NATIVE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mtune=native")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mtune=native")
      endif()
    endif()
    if (APPLE)
      set(TD_LINKER_FLAGS "-Wl,-dead_strip")
      if (NOT CMAKE_BUILD_TYPE MATCHES "Deb")
        set(TD_LINKER_FLAGS "${TD_LINKER_FLAGS},-x,-S")
      endif()
    else()
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
      if (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
        set(TD_LINKER_FLAGS "-Wl,-z,ignore")
      elseif (EMSCRIPTEN)
        set(TD_LINKER_FLAGS "-Wl,--gc-sections")
      elseif (ANDROID)
        set(TD_LINKER_FLAGS "-Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--icf=safe")
        if (CMAKE_SIZEOF_VOID_P EQUAL 8)
          # Enable 16 KB ELF alignment
          set(TD_LINKER_FLAGS "${TD_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
        endif()
      else()
        set(TD_LINKER_FLAGS "-Wl,--gc-sections -Wl,--exclude-libs,ALL")
      endif()
    endif()

    if (TD_ENABLE_LLD AND NOT WIN32 AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
      check_cxx_compiler_flag("-fuse-ld=lld" TD_HAVE_FUSE_LD_LLD)
      if (TD_HAVE_FUSE_LD_LLD)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
      endif()
    endif()

    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${TD_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TD_LINKER_FLAGS}")

    if (WIN32 OR CYGWIN)
      if (GCC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj")
      endif()
    endif()
  elseif (INTEL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${STD23_FLAG}")
  endif()

  if (WIN32)
    add_definitions(-DNTDDI_VERSION=0x06020000 -DWINVER=0x0602 -D_WIN32_WINNT=0x0602 -DPSAPI_VERSION=1 -DNOMINMAX -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
  endif()
  if (CYGWIN)
    add_definitions(-D_DEFAULT_SOURCE=1 -DFD_SETSIZE=4096)
  endif()

  # _FILE_OFFSET_BITS is broken in Android NDK r15, r15b and r17 and doesn't work prior to Android 7.0
  add_definitions(-D_FILE_OFFSET_BITS=64)

  # _GNU_SOURCE might not be defined by g++
  add_definitions(-D_GNU_SOURCE)

  if (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lsocket -lnsl")
    if (ILLUMOS)
      add_definitions(-DTD_ILLUMOS=1)
    endif()
  endif()

  include(AddCXXCompilerFlag)
  if (NOT MSVC)
    add_cxx_compiler_flag("-Wall")
    add_cxx_compiler_flag("-Wextra")
  #  add_cxx_compiler_flag("-Werror")

    if (TD_STRICT_CI_WARNINGS)
      add_cxx_compiler_flag("-Werror=return-type")
      add_cxx_compiler_flag("-Werror=deprecated")
      add_cxx_compiler_flag("-Werror=format")
      add_cxx_compiler_flag("-Werror=implicit-fallthrough")
    endif()

    add_cxx_compiler_flag("-flto-odr-type-merging")
    add_cxx_compiler_flag("-Qunused-arguments")
    add_cxx_compiler_flag("-Walloc-zero")
    add_cxx_compiler_flag("-Wc++20-compat-pedantic")
    add_cxx_compiler_flag("-Wcast-qual")
    add_cxx_compiler_flag("-Wconversion")
    add_cxx_compiler_flag("-Wdeprecated")
    add_cxx_compiler_flag("-Wduplicated-branches")
    add_cxx_compiler_flag("-Wduplicated-cond")
    add_cxx_compiler_flag("-Wimplicit-fallthrough=2")
    add_cxx_compiler_flag("-Wlogical-op")
    add_cxx_compiler_flag("-Wno-psabi")
    add_cxx_compiler_flag("-Wno-sign-conversion")
    add_cxx_compiler_flag("-Wno-tautological-compare")
    add_cxx_compiler_flag("-Wno-unknown-warning-option")
    add_cxx_compiler_flag("-Wno-unused-command-line-argument")
    add_cxx_compiler_flag("-Wno-unused-parameter")
    add_cxx_compiler_flag("-Wnon-virtual-dtor")
    add_cxx_compiler_flag("-Wodr")
    add_cxx_compiler_flag("-Wpointer-arith")
    add_cxx_compiler_flag("-Wsign-compare")
    add_cxx_compiler_flag("-Wunused-member-function")
    add_cxx_compiler_flag("-Wunused-private-field")
    add_cxx_compiler_flag("-Wvla")

  #  add_cxx_compiler_flag("-Wcast-align")

  #std::int32_t <-> int and off_t <-> std::size_t/std::int64_t
  #  add_cxx_compiler_flag("-Wuseless-cast")

  #external headers like openssl
  #  add_cxx_compiler_flag("-Wzero-as-null-pointer-constant")
  endif()

  if (GCC)
    add_cxx_compiler_flag("-Wno-maybe-uninitialized")  # too many false positives
  endif()
  if (WIN32 AND GCC AND NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0))
    # warns about casts of function pointers returned by GetProcAddress
    add_cxx_compiler_flag("-Wno-cast-function-type")
  endif()
  if (GCC AND NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0))
    # warns about a lot of "return std::move", which are not redundant for compilers without proper fix for DR 1579, i.e. GCC 8
    # see http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#1579
    # see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80452
    add_cxx_compiler_flag("-Wno-redundant-move")
  endif()
  if (GCC)
    add_cxx_compiler_flag("-Wno-stringop-overflow")  # some false positives
  endif()
  if (GCC AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0))
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104030
    add_cxx_compiler_flag("-Wbidi-chars=none")
    add_cxx_compiler_flag("-Wno-bidirectional")
  endif()

  if (MINGW)
    add_cxx_compiler_flag("-ftrack-macro-expansion=0")
  endif()

  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem /usr/include/c++/v1")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" PARENT_SCOPE)
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}" PARENT_SCOPE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" PARENT_SCOPE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
endfunction()
