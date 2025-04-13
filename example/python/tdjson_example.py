#!/usr/bin/env python3
# Copyright Aliaksei Levin, Arseny Smirnov, Pellegrino Prevete  2014–2025
# Distributed under the Boost Software License, Version 1.0.

import json
import os
import sys
from ctypes import CDLL, CFUNCTYPE, c_char_p, c_double, c_int
from ctypes.util import find_library
from typing import Any, Dict, Optional


class TdExample:
    """Simplified Python wrapper for TDLib (Telegram Database Library)."""

    def __init__(self, api_id: int = None, api_hash: str = None):
        self.api_id = api_id
        self.api_hash = api_hash
        self._load_library()
        self._setup_functions()
        self._setup_logging()
        self.client_id = self._td_create_client_id()

    def _load_library(self) -> None:
        tdjson_path = find_library('tdjson')
        if tdjson_path is None:
            tdjson_path = os.path.join(os.path.dirname(__file__), 'tdjson.dll') if os.name == 'nt' else None
            if not tdjson_path or not os.path.exists(tdjson_path):
                sys.exit("Oops! Couldn't find TDLib (tdjson). Make sure it's properly installed or in the project folder.")
        try:
            self.tdjson = CDLL(tdjson_path)
        except Exception as e:
            sys.exit(f"Failed to load TDLib: {e}")

    def _setup_functions(self) -> None:
        self._td_create_client_id = self.tdjson.td_create_client_id
        self._td_create_client_id.restype = c_int

        self._td_receive = self.tdjson.td_receive
        self._td_receive.restype = c_char_p
        self._td_receive.argtypes = [c_double]

        self._td_send = self.tdjson.td_send
        self._td_send.argtypes = [c_int, c_char_p]

        self._td_execute = self.tdjson.td_execute
        self._td_execute.restype = c_char_p
        self._td_execute.argtypes = [c_char_p]

        self.log_message_callback_type = CFUNCTYPE(None, c_int, c_char_p)
        self._td_set_log_message_callback = self.tdjson.td_set_log_message_callback
        self._td_set_log_message_callback.argtypes = [c_int, self.log_message_callback_type]

    def _setup_logging(self, verbosity_level: int = 1) -> None:
        @self.log_message_callback_type
        def on_log_message_callback(level, message):
            if level == 0:
                sys.exit(f"TDLib critical error: {message.decode()}")

        self._td_set_log_message_callback(2, on_log_message_callback)
        self.execute({'@type': 'setLogVerbosityLevel', 'new_verbosity_level': verbosity_level})

    def execute(self, query: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        query_json = json.dumps(query).encode('utf-8')
        result = self._td_execute(query_json)
        return json.loads(result.decode()) if result else None

    def send(self, query: Dict[str, Any]) -> None:
        self._td_send(self.client_id, json.dumps(query).encode())

    def receive(self, timeout: float = 1.0) -> Optional[Dict[str, Any]]:
        result = self._td_receive(timeout)
        return json.loads(result.decode()) if result else None

    def login(self) -> None:
        self.send({'@type': 'getOption', 'name': 'version'})
        print("\nStarting login process... (Press Ctrl+C to quit)")
        try:
            self._handle_authentication()
        except KeyboardInterrupt:
            print("\nLogin canceled by user.")
            sys.exit(0)

    def _handle_authentication(self) -> None:
        while True:
            event = self.receive()
            if not event:
                continue

            if event.get('@type') != 'updateAuthorizationState':
                print(f"[Received] {json.dumps(event, indent=2)}")

            if event.get('@type') == 'updateAuthorizationState':
                auth_state = event['authorization_state']
                auth_type = auth_state.get('@type')

                if auth_type == 'authorizationStateClosed':
                    print("Session ended. Goodbye!")
                    break

                elif auth_type == 'authorizationStateWaitTdlibParameters':
                    if not self.api_id or not self.api_hash:
                        print("\nTo continue, you need your own API ID and Hash from https://my.telegram.org")
                        self.api_id = int(input("Enter your API ID: "))
                        self.api_hash = input("Enter your API Hash: ")

                    print("Sending TDLib parameters...")
                    self.send({
                        '@type': 'setTdlibParameters',
                        'database_directory': 'tdlib_data',
                        'use_message_database': True,
                        'use_secret_chats': True,
                        'api_id': self.api_id,
                        'api_hash': self.api_hash,
                        'system_language_code': 'en',
                        'device_model': 'TDLib Python',
                        'application_version': '1.0',
                    })

                elif auth_type == 'authorizationStateWaitPhoneNumber':
                    self.send({
                        '@type': 'setAuthenticationPhoneNumber',
                        'phone_number': input("Enter your phone number: ")
                    })

                elif auth_type == 'authorizationStateWaitEmailAddress':
                    self.send({
                        '@type': 'setAuthenticationEmailAddress',
                        'email_address': input("Enter your email address: ")
                    })

                elif auth_type == 'authorizationStateWaitEmailCode':
                    code = input("Enter the code sent to your email: ")
                    self.send({
                        '@type': 'checkAuthenticationEmailCode',
                        'code': {'@type': 'emailAddressAuthenticationCode', 'code': code}
                    })

                elif auth_type == 'authorizationStateWaitCode':
                    self.send({'@type': 'checkAuthenticationCode', 'code': input("Enter the code you received: ")})

                elif auth_type == 'authorizationStateWaitRegistration':
                    self.send({
                        '@type': 'registerUser',
                        'first_name': input("Your first name: "),
                        'last_name': input("Your last name: ")
                    })

                elif auth_type == 'authorizationStateWaitPassword':
                    self.send({'@type': 'checkAuthenticationPassword', 'password': input("Enter your password: ")})

                elif auth_type == 'authorizationStateReady':
                    print("You're in! Login successful.")
                    return


def main():
    DEFAULT_API_ID = 94575
    DEFAULT_API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2"

    print("Welcome to the TDLib Python Client")
    print("-"*20)
    print("Note: You need your own API ID and Hash from https://my.telegram.org")
    print("      (Default credentials are for testing only.)\n")

    use_default = input("Use default credentials for testing? (y/n): ").strip().lower() == 'y'
    client = TdExample(DEFAULT_API_ID, DEFAULT_API_HASH) if use_default else TdExample()

    print("\nTesting a simple TDLib method (getTextEntities)...")
    result = client.execute({
        '@type': 'getTextEntities',
        'text': '@telegram /test_command https://telegram.org telegram.me'
    })
    print(f"Result:\n{json.dumps(result, indent=2)}")

    client.login()

    print("\nListening for incoming updates... (Ctrl+C to stop)\n")
    try:
        while True:
            event = client.receive()
            if event:
                print(json.dumps(event, indent=2))
    except KeyboardInterrupt:
        print("\nSession terminated by user.")


if __name__ == "__main__":
    main()
