import "dart:ffi" as ffi;
import '../ffi/cstring.dart';

typedef td_json_client_create_t = ffi.Pointer Function();

typedef td_json_client_receive_t = CString Function(ffi.Pointer, ffi.Double);

typedef td_json_client_send_t = ffi.Void Function(ffi.Pointer, CString);

typedef td_json_client_execute_t = CString Function(ffi.Pointer, CString);

typedef td_json_client_destroy_t = ffi.Void Function(ffi.Pointer);
