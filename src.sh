#!/bin/bash
git ls-tree -r HEAD --name-only benchmark example memprof td tdactor tddb tdnet tdtl tdutils test tg_http_client | grep -E "\.cpp$|\.h$|\.hpp$"
