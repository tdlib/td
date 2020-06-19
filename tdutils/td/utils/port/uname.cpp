//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/uname.h"

#include "td/utils/port/config.h"

#include "td/utils/common.h"
#include "td/utils/filesystem.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Stat.h"

#if TD_PORT_POSIX

#if TD_ANDROID
#include <sys/system_properties.h>
#else
#if TD_DARWIN
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#include <sys/utsname.h>
#endif

#endif

namespace td {

#if TD_DARWIN || TD_LINUX
static string read_os_name(CSlice os_version_file_path, CSlice prefix, CSlice suffix) {
  auto r_stat = stat(os_version_file_path);
  if (r_stat.is_ok() && r_stat.ok().is_reg_ && r_stat.ok().size_ < (1 << 16)) {
    auto r_file = read_file_str(os_version_file_path, r_stat.ok().size_);
    if (r_file.is_ok()) {
      auto begin_pos = r_file.ok().find(prefix.c_str());
      if (begin_pos != string::npos) {
        begin_pos += prefix.size();
        auto end_pos = r_file.ok().find(suffix.c_str(), begin_pos);
        if (end_pos != string::npos) {
          auto os_version = trim(r_file.ok().substr(begin_pos, end_pos - begin_pos));
          if (os_version.find("\n") == string::npos) {
            return os_version;
          }
        }
      }
    }
  }
  return string();
}
#endif

Slice get_operating_system_version() {
  static string result = []() -> string {
#if TD_DARWIN
    char version[256];
    size_t size = sizeof(version);
    string os_version;
    if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) == 0) {
      os_version = trim(string(version, size));
    }
    if (os_version.empty()) {
      os_version = read_os_name("/System/Library/CoreServices/SystemVersion.plist",
                                "<key>ProductUserVisibleVersion</key>\n\t<string>", "</string>\n");
    }
    if (!os_version.empty()) {
      os_version = " " + os_version;
    }

#if TD_DARWIN_IOS
    return "iOS" + os_version;
#elif TD_DARWIN_TV_OS
    return "tvOS" + os_version;
#elif TD_DARWIN_WATCH_OS
    return "watchOS" + os_version;
#elif TD_DARWIN_MAC
    return "macOS" + os_version;
#else
    return "Darwin" + os_version;
#endif
#elif TD_PORT_POSIX
#if TD_ANDROID
    char version[PROP_VALUE_MAX + 1];
    int length = __system_property_get("ro.build.version.release", version);
    if (length > 0) {
      return "Android " + string(version, length);
    }
#else
#if TD_LINUX
    auto os_name = read_os_name("/etc/os-release", "PRETTY_NAME=\"", "\"\n");
    if (!os_name.empty()) {
      return os_name;
    }
#endif

    utsname name;
    int err = uname(&name);
    if (err == 0) {
      auto os_name = trim(PSTRING() << name.sysname << " " << name.release);
      if (!os_name.empty()) {
        return os_name;
      }
    }
#endif
    LOG(ERROR) << "Failed to identify OS name; use generic one";

#if TD_ANDROID
    return "Android";
#elif TD_TIZEN
    return "Tizen";
#elif TD_LINUX
    return "Linux";
#elif TD_FREEBSD
    return "FreeBSD";
#elif TD_OPENBSD
    return "OpenBSD";
#elif TD_NETBSD
    return "NetBSD";
#elif TD_CYGWIN
    return "Cygwin";
#elif TD_EMSCRIPTEN
    return "Emscripten";
#else
    return "Unix";
#endif

#else

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)
    auto handle = GetModuleHandle(L"ntdll.dll");
    if (handle != nullptr) {
      using RtlGetVersionPtr = LONG(WINAPI *)(PRTL_OSVERSIONINFOEXW);
      RtlGetVersionPtr RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(handle, "RtlGetVersion"));
      if (RtlGetVersion != nullptr) {
        RTL_OSVERSIONINFOEXW os_version_info = {};
        os_version_info.dwOSVersionInfoSize = sizeof(os_version_info);
        if (RtlGetVersion(&os_version_info) == 0) {
          auto major = os_version_info.dwMajorVersion;
          auto minor = os_version_info.dwMinorVersion;
          bool is_server = os_version_info.wProductType != VER_NT_WORKSTATION;

          if (major == 10 && minor >= 0) {
            if (is_server) {
              return os_version_info.dwBuildNumber >= 17623 ? "Windows Server 2019" : "Windows Server 2016";
            }
            return "Windows 10";
          }
          if (major == 6 && minor == 3) {
            return is_server ? "Windows Server 2012 R2" : "Windows 8.1";
          }
          if (major == 6 && minor == 2) {
            return is_server ? "Windows Server 2012" : "Windows 8";
          }
          if (major == 6 && minor == 1) {
            return is_server ? "Windows Server 2008 R2" : "Windows 7";
          }
          if (major == 6 && minor == 0) {
            return is_server ? "Windows Server 2008" : "Windows Vista";
          }
          return is_server ? "Windows Server" : "Windows";
        }
      }
    }
#elif TD_WINRT
    return "Windows 10";
#endif

    LOG(ERROR) << "Failed to identify OS name; use generic one";
    return "Windows";
#endif
  }();
  return result;
}

}  // namespace td
