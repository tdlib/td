//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <td/telegram/Client.h>
#include <td/telegram/Log.h>
#include <td/telegram/td_api.h>

#include <td/tl/tl_jni_object.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

namespace td_jni {

static td::td_api::object_ptr<td::td_api::Function> fetch_function(JNIEnv *env, jobject function) {
  td::jni::reset_parse_error();
  auto result = td::td_api::Function::fetch(env, function);
  if (td::jni::have_parse_error()) {
    std::abort();
  }
  return result;
}

static td::Client *get_client(jlong client_id) {
  return reinterpret_cast<td::Client *>(static_cast<std::uintptr_t>(client_id));
}

static jlong Client_createNativeClient(JNIEnv *env, jclass clazz) {
  return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(new td::Client()));
}

static void Client_nativeClientSend(JNIEnv *env, jclass clazz, jlong client_id, jlong id, jobject function) {
  get_client(client_id)->send({static_cast<std::uint64_t>(id), fetch_function(env, function)});
}

static jint Client_nativeClientReceive(JNIEnv *env, jclass clazz, jlong client_id, jlongArray ids, jobjectArray events,
                                       jdouble timeout) {
  auto client = get_client(client_id);
  jsize events_size = env->GetArrayLength(ids);  // ids and events size must be of equal size
  if (events_size == 0) {
    return 0;
  }
  jsize result_size = 0;

  auto response = client->receive(timeout);
  while (response.object) {
    jlong result_id = static_cast<jlong>(response.id);
    env->SetLongArrayRegion(ids, result_size, 1, &result_id);

    jobject object;
    response.object->store(env, object);
    env->SetObjectArrayElement(events, result_size, object);
    env->DeleteLocalRef(object);

    result_size++;
    if (result_size == events_size) {
      break;
    }

    response = client->receive(0);
  }
  return result_size;
}

static jobject Client_nativeClientExecute(JNIEnv *env, jclass clazz, jobject function) {
  jobject result;
  td::Client::execute({0, fetch_function(env, function)}).object->store(env, result);
  return result;
}

static void Client_destroyNativeClient(JNIEnv *env, jclass clazz, jlong client_id) {
  delete get_client(client_id);
}

static void Log_setVerbosityLevel(JNIEnv *env, jclass clazz, jint new_log_verbosity_level) {
  td::Log::set_verbosity_level(static_cast<int>(new_log_verbosity_level));
}

static jboolean Log_setFilePath(JNIEnv *env, jclass clazz, jstring file_path) {
  return td::Log::set_file_path(td::jni::from_jstring(env, file_path)) ? JNI_TRUE : JNI_FALSE;
}

static void Log_setMaxFileSize(JNIEnv *env, jclass clazz, jlong max_file_size) {
  td::Log::set_max_file_size(max_file_size);
}

static jstring Object_toString(JNIEnv *env, jobject object) {
  return td::jni::to_jstring(env, to_string(td::td_api::Object::fetch(env, object)));
}

static jstring Function_toString(JNIEnv *env, jobject object) {
  return td::jni::to_jstring(env, to_string(td::td_api::Function::fetch(env, object)));
}

static constexpr jint JAVA_VERSION = JNI_VERSION_1_6;
static JavaVM *java_vm;
static jclass log_class;

static void on_fatal_error(const char *error_message) {
  auto env = td::jni::get_jni_env(java_vm, JAVA_VERSION);
  jmethodID on_fatal_error_method = env->GetStaticMethodID(log_class, "onFatalError", "(Ljava/lang/String;)V");
  if (env && on_fatal_error_method) {
    jstring error_str = td::jni::to_jstring(env.get(), error_message);
    env->CallStaticVoidMethod(log_class, on_fatal_error_method, error_str);
    if (error_str) {
      env->DeleteLocalRef(error_str);
    }
  }
}

static jint register_native(JavaVM *vm) {
  JNIEnv *env;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JAVA_VERSION) != JNI_OK) {
    return -1;
  }

  java_vm = vm;

  auto register_method = [env](jclass clazz, std::string name, std::string signature, auto function_ptr) {
    td::jni::register_native_method(env, clazz, std::move(name), std::move(signature),
                                    reinterpret_cast<void *>(function_ptr));
  };

  auto client_class = td::jni::get_jclass(env, PACKAGE_NAME "/Client");
  log_class = td::jni::get_jclass(env, PACKAGE_NAME "/Log");
  auto object_class = td::jni::get_jclass(env, PACKAGE_NAME "/TdApi$Object");
  auto function_class = td::jni::get_jclass(env, PACKAGE_NAME "/TdApi$Function");

#define TD_OBJECT "L" PACKAGE_NAME "/TdApi$Object;"
#define TD_FUNCTION "L" PACKAGE_NAME "/TdApi$Function;"
  register_method(client_class, "createNativeClient", "()J", Client_createNativeClient);
  register_method(client_class, "nativeClientSend", "(JJ" TD_FUNCTION ")V", Client_nativeClientSend);
  register_method(client_class, "nativeClientReceive", "(J[J[" TD_OBJECT "D)I", Client_nativeClientReceive);
  register_method(client_class, "nativeClientExecute", "(" TD_FUNCTION ")" TD_OBJECT, Client_nativeClientExecute);
  register_method(client_class, "destroyNativeClient", "(J)V", Client_destroyNativeClient);

  register_method(log_class, "setVerbosityLevel", "(I)V", Log_setVerbosityLevel);
  register_method(log_class, "setFilePath", "(Ljava/lang/String;)Z", Log_setFilePath);
  register_method(log_class, "setMaxFileSize", "(J)V", Log_setMaxFileSize);

  register_method(object_class, "toString", "()Ljava/lang/String;", Object_toString);

  register_method(function_class, "toString", "()Ljava/lang/String;", Function_toString);
#undef TD_FUNCTION
#undef TD_OBJECT

  td::jni::init_vars(env, PACKAGE_NAME);
  td::td_api::Object::init_jni_vars(env, PACKAGE_NAME);
  td::td_api::Function::init_jni_vars(env, PACKAGE_NAME);
  td::Log::set_fatal_error_callback(on_fatal_error);

  return JAVA_VERSION;
}

}  // namespace td_jni

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  static jint jni_version = td_jni::register_native(vm);  // call_once
  return jni_version;
}
