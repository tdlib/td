//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include <jni.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace td {
namespace td_api {
class keyboardButton;
class inlineKeyboardButton;
}  // namespace td_api

namespace jni {

extern thread_local bool parse_error;

extern jclass ArrayKeyboardButtonClass;
extern jclass ArrayInlineKeyboardButtonClass;
extern jmethodID GetConstructorID;
extern jmethodID BooleanGetValueMethodID;
extern jmethodID IntegerGetValueMethodID;
extern jmethodID LongGetValueMethodID;
extern jmethodID DoubleGetValueMethodID;

std::string fetch_string(JNIEnv *env, jobject o, jfieldID id);

inline jobject fetch_object(JNIEnv *env, const jobject &o, const jfieldID &id) {
  // null return object is implicitly allowed
  return env->GetObjectField(o, id);
}

inline bool have_parse_error() {
  return parse_error;
}

inline void reset_parse_error() {
  parse_error = false;
}

std::string from_jstring(JNIEnv *env, jstring s);

jstring to_jstring(JNIEnv *env, const std::string &s);

std::string from_bytes(JNIEnv *env, jbyteArray arr);

jbyteArray to_bytes(JNIEnv *env, const std::string &b);

jclass get_jclass(JNIEnv *env, const char *class_name);

jmethodID get_method_id(JNIEnv *env, jclass clazz, const char *name, const char *sig);

jfieldID get_field_id(JNIEnv *env, jclass clazz, const char *name, const char *sig);

bool init_vars(JNIEnv *env, const char *td_api_java_package);

jintArray store_vector(JNIEnv *env, const std::vector<std::int32_t> &v);

jlongArray store_vector(JNIEnv *env, const std::vector<std::int64_t> &v);

jdoubleArray store_vector(JNIEnv *env, const std::vector<double> &v);

jobjectArray store_vector(JNIEnv *env, const std::vector<std::string> &v);

template <class T>
jobjectArray store_vector(JNIEnv *env, const std::vector<T> &v) {
  jint length = static_cast<jint>(v.size());
  jobjectArray arr = env->NewObjectArray(length, T::element_type::Class, jobject());
  if (arr) {
    for (jint i = 0; i < length; i++) {
      if (v[i] != nullptr) {
        jobject stored_object;
        v[i]->store(env, stored_object);
        if (stored_object) {
          env->SetObjectArrayElement(arr, i, stored_object);
          env->DeleteLocalRef(stored_object);
        }
      }
    }
  }
  return arr;
}

template <class T>
class get_array_class {
  static jclass get();
};

template <>
class get_array_class<td_api::keyboardButton> {
 public:
  static jclass get() {
    return ArrayKeyboardButtonClass;
  }
};

template <>
class get_array_class<td_api::inlineKeyboardButton> {
 public:
  static jclass get() {
    return ArrayInlineKeyboardButtonClass;
  }
};

template <class T>
jobjectArray store_vector(JNIEnv *env, const std::vector<std::vector<T>> &v) {
  jint length = static_cast<jint>(v.size());
  jobjectArray arr = env->NewObjectArray(length, get_array_class<typename T::element_type>::get(), 0);
  if (arr) {
    for (jint i = 0; i < length; i++) {
      auto stored_array = store_vector(env, v[i]);
      if (stored_array) {
        env->SetObjectArrayElement(arr, i, stored_array);
        env->DeleteLocalRef(stored_array);
      }
    }
  }
  return arr;
}

}  // namespace jni
}  // namespace td
