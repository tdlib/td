import "dart:ffi" as ffi;
import 'package:ffi/ffi.dart';

typedef td_json_client_create_t = ffi.Pointer Function();

typedef td_json_client_receive_t = ffi.Pointer<Utf8> Function(ffi.Pointer, ffi.Double);

typedef td_json_client_send_t = ffi.Void Function(ffi.Pointer, ffi.Pointer<Utf8>);

typedef td_json_client_execute_t = ffi.Pointer<Utf8> Function(ffi.Pointer, ffi.Pointer<Utf8>);

typedef td_json_client_destroy_t = ffi.Void Function(ffi.Pointer);
