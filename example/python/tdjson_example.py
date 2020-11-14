#
# Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com),
# Pellegrino Prevete (pellegrinoprevete@gmail.com)  2014-2020
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
from ctypes.util import find_library
from ctypes import *
import json
import sys

# load shared library
tdjson_path = find_library('tdjson') or 'tdjson.dll'
if tdjson_path is None:
    print('can\'t find tdjson library')
    quit()
tdjson = CDLL(tdjson_path)

# load TDLib functions from shared library
_td_create_client_id = tdjson.td_create_client_id
_td_create_client_id.restype = c_int
_td_create_client_id.argtypes = []

_td_receive = tdjson.td_receive
_td_receive.restype = c_char_p
_td_receive.argtypes = [c_double]

_td_send = tdjson.td_send
_td_send.restype = None
_td_send.argtypes = [c_int, c_char_p]

_td_execute = tdjson.td_execute
_td_execute.restype = c_char_p
_td_execute.argtypes = [c_char_p]

fatal_error_callback_type = CFUNCTYPE(None, c_char_p)

_td_set_log_fatal_error_callback = tdjson.td_set_log_fatal_error_callback
_td_set_log_fatal_error_callback.restype = None
_td_set_log_fatal_error_callback.argtypes = [fatal_error_callback_type]

# initialize TDLib log with desired parameters
def on_fatal_error_callback(error_message):
    print('TDLib fatal error: ', error_message)
    sys.stdout.flush()

def td_execute(query):
    query = json.dumps(query).encode('utf-8')
    result = _td_execute(query)
    if result:
        result = json.loads(result.decode('utf-8'))
    return result

c_on_fatal_error_callback = fatal_error_callback_type(on_fatal_error_callback)
_td_set_log_fatal_error_callback(c_on_fatal_error_callback)

# setting TDLib log verbosity level to 1 (errors)
print(str(td_execute({'@type': 'setLogVerbosityLevel', 'new_verbosity_level': 1, '@extra': 1.01234})).encode('utf-8'))


# create client
client_id = _td_create_client_id()

# simple wrappers for client usage
def td_send(query):
    query = json.dumps(query).encode('utf-8')
    _td_send(client_id, query)

def td_receive():
    result = _td_receive(1.0)
    if result:
        result = json.loads(result.decode('utf-8'))
    return result

# another test for TDLib execute method
print(str(td_execute({'@type': 'getTextEntities', 'text': '@telegram /test_command https://telegram.org telegram.me', '@extra': ['5', 7.0, 'ä']})).encode('utf-8'))

# start the client by sending request to it
td_send({'@type': 'getAuthorizationState', '@extra': 1.01234})

# main events cycle
while True:
    event = td_receive()
    if event:
        # process authorization states
        if event['@type'] == 'updateAuthorizationState':
            auth_state = event['authorization_state']

            # if client is closed, we need to destroy it and create new client
            if auth_state['@type'] == 'authorizationStateClosed':
                break

            # set TDLib parameters
            # you MUST obtain your own api_id and api_hash at https://my.telegram.org
            # and use them in the setTdlibParameters call
            if auth_state['@type'] == 'authorizationStateWaitTdlibParameters':
                td_send({'@type': 'setTdlibParameters', 'parameters': {
                                                       'database_directory': 'tdlib',
                                                       'use_message_database': True,
                                                       'use_secret_chats': True,
                                                       'api_id': 94575,
                                                       'api_hash': 'a3406de8d171bb422bb6ddf3bbd800e2',
                                                       'system_language_code': 'en',
                                                       'device_model': 'Desktop',
                                                       'application_version': '1.0',
                                                       'enable_storage_optimizer': True}})

            # set an encryption key for database to let know TDLib how to open the database
            if auth_state['@type'] == 'authorizationStateWaitEncryptionKey':
                td_send({'@type': 'checkDatabaseEncryptionKey', 'encryption_key': ''})

            # enter phone number to log in
            if auth_state['@type'] == 'authorizationStateWaitPhoneNumber':
                phone_number = input('Please enter your phone number: ')
                td_send({'@type': 'setAuthenticationPhoneNumber', 'phone_number': phone_number})

            # wait for authorization code
            if auth_state['@type'] == 'authorizationStateWaitCode':
                code = input('Please enter the authentication code you received: ')
                td_send({'@type': 'checkAuthenticationCode', 'code': code})

            # wait for first and last name for new users
            if auth_state['@type'] == 'authorizationStateWaitRegistration':
                first_name = input('Please enter your first name: ')
                last_name = input('Please enter your last name: ')
                td_send({'@type': 'registerUser', 'first_name': first_name, 'last_name': last_name})

            # wait for password if present
            if auth_state['@type'] == 'authorizationStateWaitPassword':
                password = input('Please enter your password: ')
                td_send({'@type': 'checkAuthenticationPassword', 'password': password})

        # handle an incoming update or an answer to a previously sent request
        print(str(event).encode('utf-8'))
        sys.stdout.flush()
