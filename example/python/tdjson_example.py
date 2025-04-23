#!/usr/bin/env python3
# Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com),
# Pellegrino Prevete (pellegrinoprevete@gmail.com)  2014-2025
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import json
import os
import sys
from ctypes import CDLL, CFUNCTYPE, c_char_p, c_double, c_int
from ctypes.util import find_library
from typing import Any, Dict, Optional


class TdExample:
    """A Python client for the Telegram API using TDLib."""

    def __init__(self, api_id: int = None, api_hash: str = None):
        """Initialize a Telegram client.

        Args:
            api_id: Telegram API ID (get from https://my.telegram.org)
            api_hash: Telegram API hash (get from https://my.telegram.org)
        """
        self.api_id = api_id
        self.api_hash = api_hash
        self._load_library()
        self._setup_functions()
        self._setup_logging()
        self.client_id = self._td_create_client_id()

    def _load_library(self) -> None:
        """Load the TDLib shared library."""
        tdjson_path = find_library("tdjson")
        if tdjson_path is None:
            if os.name == "nt":
                tdjson_path = os.path.join(os.path.dirname(__file__), "tdjson.dll")
            else:
                sys.exit(
                    "Error: Can't find 'tdjson' library. Make sure it's installed correctly."
                )

        try:
            self.tdjson = CDLL(tdjson_path)
        except Exception as e:
            sys.exit(f"Error loading TDLib: {e}")

    def _setup_functions(self) -> None:
        """Set up function signatures for TDLib calls."""
        # Create client ID
        self._td_create_client_id = self.tdjson.td_create_client_id
        self._td_create_client_id.restype = c_int
        self._td_create_client_id.argtypes = []

        # Receive updates
        self._td_receive = self.tdjson.td_receive
        self._td_receive.restype = c_char_p
        self._td_receive.argtypes = [c_double]

        # Send requests
        self._td_send = self.tdjson.td_send
        self._td_send.restype = None
        self._td_send.argtypes = [c_int, c_char_p]

        # Execute synchronous requests
        self._td_execute = self.tdjson.td_execute
        self._td_execute.restype = c_char_p
        self._td_execute.argtypes = [c_char_p]

        # Set log callback
        self.log_message_callback_type = CFUNCTYPE(None, c_int, c_char_p)
        self._td_set_log_message_callback = self.tdjson.td_set_log_message_callback
        self._td_set_log_message_callback.restype = None
        self._td_set_log_message_callback.argtypes = [
            c_int,
            self.log_message_callback_type,
        ]

    def _setup_logging(self, verbosity_level: int = 1) -> None:
        """Configure TDLib logging.

        Args:
            verbosity_level: 0-fatal, 1-errors, 2-warnings, 3+-debug
        """

        @self.log_message_callback_type
        def on_log_message_callback(verbosity_level, message):
            if verbosity_level == 0:
                sys.exit(f"TDLib fatal error: {message.decode('utf-8')}")

        self._td_set_log_message_callback(2, on_log_message_callback)
        self.execute(
            {"@type": "setLogVerbosityLevel", "new_verbosity_level": verbosity_level}
        )

    def execute(self, query: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        """Execute a synchronous TDLib request.

        Args:
            query: The request to execute

        Returns:
            Response from TDLib or None
        """
        query_json = json.dumps(query).encode("utf-8")
        result = self._td_execute(query_json)
        if result:
            return json.loads(result.decode("utf-8"))
        return None

    def send(self, query: Dict[str, Any]) -> None:
        """Send an asynchronous request to TDLib.

        Args:
            query: The request to send
        """
        query_json = json.dumps(query).encode("utf-8")
        self._td_send(self.client_id, query_json)

    def receive(self, timeout: float = 1.0) -> Optional[Dict[str, Any]]:
        """Receive a response or update from TDLib.

        Args:
            timeout: Maximum number of seconds to wait

        Returns:
            An update or response from TDLib, or None if nothing received
        """
        result = self._td_receive(timeout)
        if result:
            return json.loads(result.decode("utf-8"))
        return None

    def login(self) -> None:
        """Start the authentication process."""
        self.send({"@type": "getOption", "name": "version"})

        print("Starting Telegram authentication flow...")
        print("Press Ctrl+C to cancel at any time.")

        try:
            self._handle_authentication()
        except KeyboardInterrupt:
            print("\nAuthentication canceled by user.")
            sys.exit(0)

    def _handle_authentication(self) -> None:
        """Handle the TDLib authentication flow."""
        while True:
            event = self.receive()
            if not event:
                continue

            # Print all updates for debugging
            event_type = event["@type"]
            if event_type != "updateAuthorizationState":
                print(f"Receive: {json.dumps(event, indent=2)}")

            # Process authorization states
            if event_type == "updateAuthorizationState":
                auth_state = event["authorization_state"]
                auth_type = auth_state["@type"]

                if auth_type == "authorizationStateClosed":
                    print("Authorization state closed.")
                    break

                elif auth_type == "authorizationStateWaitTdlibParameters":
                    if not self.api_id or not self.api_hash:
                        print(
                            "\nYou MUST obtain your own api_id and api_hash at https://my.telegram.org"
                        )
                        self.api_id = int(input("Please enter your API ID: "))
                        self.api_hash = input("Please enter your API hash: ")

                    print("Setting TDLib parameters...")
                    self.send(
                        {
                            "@type": "setTdlibParameters",
                            "database_directory": "tdlib_data",
                            "use_message_database": True,
                            "use_secret_chats": True,
                            "api_id": self.api_id,
                            "api_hash": self.api_hash,
                            "system_language_code": "en",
                            "device_model": "Python TDLib Client",
                            "application_version": "1.1",
                        }
                    )

                elif auth_type == "authorizationStateWaitPhoneNumber":
                    phone_number = input(
                        "Please enter your phone number (international format): "
                    )
                    self.send(
                        {
                            "@type": "setAuthenticationPhoneNumber",
                            "phone_number": phone_number,
                        }
                    )

                elif auth_type == "authorizationStateWaitPremiumPurchase":
                    print("Telegram Premium subscription is required.")
                    return

                elif auth_type == "authorizationStateWaitEmailAddress":
                    email_address = input("Please enter your email address: ")
                    self.send(
                        {
                            "@type": "setAuthenticationEmailAddress",
                            "email_address": email_address,
                        }
                    )

                elif auth_type == "authorizationStateWaitEmailCode":
                    code = input(
                        "Please enter the email authentication code you received: "
                    )
                    self.send(
                        {
                            "@type": "checkAuthenticationEmailCode",
                            "code": {
                                "@type": "emailAddressAuthenticationCode",
                                "code": code,
                            },
                        }
                    )

                elif auth_type == "authorizationStateWaitCode":
                    code = input("Please enter the authentication code you received: ")
                    self.send({"@type": "checkAuthenticationCode", "code": code})

                elif auth_type == "authorizationStateWaitRegistration":
                    first_name = input("Please enter your first name: ")
                    last_name = input("Please enter your last name: ")
                    self.send(
                        {
                            "@type": "registerUser",
                            "first_name": first_name,
                            "last_name": last_name,
                        }
                    )

                elif auth_type == "authorizationStateWaitPassword":
                    password = input("Please enter your password: ")
                    self.send(
                        {"@type": "checkAuthenticationPassword", "password": password}
                    )

                elif auth_type == "authorizationStateReady":
                    print("Authorization complete! You are now logged in.")
                    return


def main():
    """Main function to demonstrate client usage."""
    # Example API credentials - DO NOT USE THESE
    # Get your own from https://my.telegram.org
    DEFAULT_API_ID = 94575
    DEFAULT_API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2"

    print("TDLib Python Client")
    print("===================")
    print(
        "IMPORTANT: You should obtain your own api_id and api_hash at https://my.telegram.org"
    )
    print("         The default values are for demonstration only.\n")

    use_default = (
        input("Use default API credentials for testing? (y/n): ").lower() == "y"
    )

    if use_default:
        client = TdExample(DEFAULT_API_ID, DEFAULT_API_HASH)
    else:
        client = TdExample()

    # Test execute method
    print("\nTesting TDLib execute method...")
    result = client.execute(
        {
            "@type": "getTextEntities",
            "text": "@telegram /test_command https://telegram.org telegram.me",
        }
    )
    print(f"Text entities: {json.dumps(result, indent=2)}")

    # Start login process
    client.login()

    # Main event loop
    print("\nEntering main event loop. Press Ctrl+C to exit.")
    try:
        while True:
            event = client.receive()
            if event:
                print(json.dumps(event, indent=2))
    except KeyboardInterrupt:
        print("\nExiting...")


if __name__ == "__main__":
    main()
