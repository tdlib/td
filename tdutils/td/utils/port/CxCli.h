//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#undef small

#if TD_WINRT

#include "td/utils/misc.h"  // for narrow_cast
#include "td/utils/port/wstring_convert.h"

#include "collection.h"

#include <map>
#include <mutex>
#include <queue>

#define REF_NEW ref new
#define CLRCALL
#define CXCONST

namespace CxCli {
using Platform::String;
using Windows::Foundation::Collections::IVector;
#define Array IVector
using Platform::Collections::Vector;

template <class Key, class Value> class Dictionary {
public:
  bool TryGetValue(Key key, Value &value) {
    auto it = impl_.find(key);
    if (it == impl_.end()) {
      return false;
    }
    value = it->second;
    return true;
  }
  void Remove(Key value) {
    impl_.erase(value);
  }
  Value &operator [] (Key key) {
    return impl_[key];
  }
private:
  std::map<Key, Value> impl_;
};
template <class Value> class ConcurrentQueue {
public:
  void Enqueue(Value value) {
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(value);
  }
  bool TryDequeue(Value &value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty()) {
      return false;
    }
    value = queue.front();
    queue.pop();
    return true;
  }
private:
  std::mutex mutex;
  std::queue<Value> queue;
};

inline std::string string_to_unmanaged(String^ string) {
  return td::from_wstring(string->Data(), string->Length()).ok();
}
inline String^ string_from_unmanaged(const std::string &from) {
  auto tmp = td::to_wstring(from).ok();
  return REF_NEW String(tmp.c_str(), td::narrow_cast<unsigned>(tmp.size()));
}
} // namespace CxCli
#elif TD_CLI
#include <msclr\lock.h>
#include <msclr\marshal_cppstd.h>
#define REF_NEW gcnew
#define CLRCALL __clrcall
#define CXCONST

#define Array array
#define Vector array

namespace CxCli {
using uint8 = td::uint8;
using int32 = td::int32;
using int64 = td::int64;
using float64 = double;

using System::String;
using System::Collections::Concurrent::ConcurrentQueue;
using System::Collections::Generic::Dictionary;
using msclr::lock;
using msclr::interop::marshal_as;

inline std::string string_to_unmanaged(String^ string) {
  return marshal_as<std::string>(string);
}

inline String^ string_from_unmanaged(const std::string &from) {
  return marshal_as<String^>(from);
}
} // namespace CxCli
#endif
