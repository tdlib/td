import 'dart:ffi' as ffi;
import 'package:ffi/ffi.dart';
import 'signatures.dart';

class _TDJsonBindings {

  ffi.Pointer Function() client_create;

  ffi.Pointer<Utf8> Function(ffi.Pointer, double) client_receive;

  void Function(ffi.Pointer, ffi.Pointer<Utf8>) client_send;

  ffi.Pointer<Utf8> Function(ffi.Pointer, ffi.Pointer<Utf8>) client_execute;

  void Function(ffi.Pointer) client_destroy;

  _TDJsonBindings({String path = './libs/'}) {

    final libtdjson = ffi.DynamicLibrary.open(path + 'libtdjson.so');

    client_create = libtdjson
      .lookup<ffi.NativeFunction<td_json_client_create_t>>('td_json_client_create')
      .asFunction();

    client_receive = libtdjson
      .lookup<ffi.NativeFunction<td_json_client_receive_t>>('td_json_client_receive')
      .asFunction();

    client_send = libtdjson
      .lookup<ffi.NativeFunction<td_json_client_send_t>>('td_json_client_send')
      .asFunction();

    client_execute = libtdjson
      .lookup<ffi.NativeFunction<td_json_client_execute_t>>('td_json_client_execute')
      .asFunction();

    client_destroy = libtdjson
      .lookup<ffi.NativeFunction<td_json_client_destroy_t>>('td_json_client_destroy')
      .asFunction();

  }

}

_TDJsonBindings _cachedBindings;
_TDJsonBindings get td_json => _cachedBindings ??= _TDJsonBindings();
