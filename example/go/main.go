package main

// #cgo LDFLAGS: -ltdjson
// #include <td/telegram/td_json_client.h>
import "C"
import (
	"log"
	"unsafe"
)

func td_send(client unsafe.Pointer, query *C.char) {
	C.td_json_client_send(client, query)
}

func td_receive(client unsafe.Pointer) string {
	return C.GoString(C.td_json_client_receive(client, 1.0))
}

func main() {
	var client unsafe.Pointer
	client = C.td_json_client_create()

	query := C.CString(`{"@type": "getAuthorizationState"}`)
	td_send(client, query)
	log.Println(td_receive(client))
}
