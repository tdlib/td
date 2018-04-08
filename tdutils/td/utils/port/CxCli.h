//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#include "td/utils/common.h"
#undef small

#if TD_WINRT

#include "td/utils/port/wstring_convert.h"

#include "collection.h"

#include <cstdint>
#include <map>
#include <mutex>

#define REF_NEW ref new
#define CLRCALL

namespace CxCli {

using Windows::Foundation::Collections::IVector;
#define Array IVector
using Platform::Collections::Vector;
#define ArraySize(arr) ((arr)->Size)
#define ArrayGet(arr, index) ((arr)->GetAt(index))
#define ArraySet(arr, index, value) ((arr)->SetAt((index), (value)))
#define ArrayIndexType unsigned

using Platform::String;

using Platform::NullReferenceException;

template <class Key, class Value> class ConcurrentDictionary {
public:
  bool TryGetValue(Key key, Value &value) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = impl_.find(key);
    if (it == impl_.end()) {
      return false;
    }
    value = it->second;
    return true;
  }
  bool TryRemove(Key key, Value &value) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = impl_.find(key);
    if (it == impl_.end()) {
      return false;
    }
    value = std::move(it->second);
    impl_.erase(it);
    return true;
  }
  Value &operator [] (Key key) {
    std::lock_guard<std::mutex> guard(mutex_);
    return impl_[key];
  }
private:
  std::mutex mutex_;
  std::map<Key, Value> impl_;
};

inline std::int64_t Increment(volatile std::int64_t &value) {
  return InterlockedIncrement64(&value);
}

inline std::string string_to_unmanaged(String^ str) {
  if (!str) {
    return std::string();
  }
  return td::from_wstring(str->Data(), str->Length()).ok();
}

inline String^ string_from_unmanaged(const std::string &from) {
  auto tmp = td::to_wstring(from).ok();
  return REF_NEW String(tmp.c_str(), static_cast<unsigned>(tmp.size()));
}

} // namespace CxCli

#elif TD_CLI

#include <msclr\marshal_cppstd.h>

#define REF_NEW gcnew
#define CLRCALL __clrcall

namespace CxCli {

using uint8 = td::uint8;
using int32 = td::int32;
using int64 = td::int64;
using float64 = double;

#define Array array
#define Vector array
#define ArraySize(arr) ((arr)->Length)
#define ArrayGet(arr, index) ((arr)[index])
#define ArraySet(arr, index, value) ((arr)[index] = (value))
#define ArrayIndexType int

using System::String;

using System::NullReferenceException;

using System::Collections::Concurrent::ConcurrentDictionary;

inline std::int64_t Increment(std::int64_t %value) {
  return System::Threading::Interlocked::Increment(value);
}

inline std::string string_to_unmanaged(String^ str) {
  if (!str) {
    return std::string();
  }
  return msclr::interop::marshal_as<std::string>(str);
}

inline String^ string_from_unmanaged(const std::string &from) {
  return msclr::interop::marshal_as<String^>(from);
}

} // namespace CxCli

#endif
