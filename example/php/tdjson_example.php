<?php

class TDLibClient {
    private $ffi;
    private $client;
    private $authorization_state;
    private $are_authorized = false;
    private $need_restart = false;
    private $current_query_id = 0;
    private $authentication_query_id = 0;

    private $handlers = [];
    private $users = [];
    private $chat_titles = [];

    public function __construct() {
        $this->initialize_tdlib();

        $this->send_query([
            '@type' => 'setLogVerbosityLevel',
            'new_verbosity_level' => 1
        ]);
    }

    private function initialize_tdlib() {
        $library_path = $this->get_library_path();

        if (!file_exists($library_path)) {
            throw new Exception("TDLib library not found at: $library_path. Please compile TDLib first.");
        }

        $ffi_def = "
            void* td_json_client_create();
            void td_json_client_send(void* client, const char* request);
            const char* td_json_client_receive(void* client, double timeout);
            void td_json_client_destroy(void* client);
        ";

        $this->ffi = FFI::cdef($ffi_def, $library_path);

        $this->client = $this->ffi->td_json_client_create();

        if ($this->client === null) {
            throw new Exception("Failed to create TDLib client");
        }

        echo "TDLib client initialized successfully" . PHP_EOL;
    }

    private function get_library_path() {
        if (strtoupper(substr(PHP_OS, 0, 3)) === 'WIN') {
            return 'tdjson.dll';
        } elseif (PHP_OS === 'Darwin') {
            return 'libtdjson.dylib';
        } else {
            return 'libtdjson.so';
        }
    }

    public function loop() {
        while (true) {
            $this->process_incoming_messages();

            if ($this->need_restart) {
                $this->restart();
                continue;
            }

            if (!$this->are_authorized) {
                usleep(100000);
                continue;
            }

            $this->handle_user_input();
        }
    }

    private function process_incoming_messages() {
        while (true) {
            $response = $this->receive(0);
            if (!$response) {
                break;
            }
            $this->process_response($response);
        }
    }

    private function handle_user_input() {
        $read = [STDIN];
        $write = [];
        $except = [];

        $changed = stream_select($read, $write, $except, 0, 100000);

        if ($changed > 0) {
            echo "Enter action [q] quit [u] check for updates [c] show chats [m <chat_id> <text>] send message [me] show self [l] logout: " . PHP_EOL;

            $line = trim(fgets(STDIN));
            if (empty($line)) {
                return;
            }

            $parts = explode(' ', $line, 2);
            $action = $parts[0];

            switch ($action) {
                case 'q':
                    exit(0);

                case 'u':
                    echo "Checking for updates..." . PHP_EOL;
                    break;

                case 'close':
                    echo "Closing..." . PHP_EOL;
                    $this->send_query(['@type' => 'close']);
                    break;

                case 'me':
                    $this->send_query(['@type' => 'getMe'], function($object) {
                        if (isset($object['@type']) && $object['@type'] === 'error') {
                            echo "Error: " . $object['message'] . PHP_EOL;
                        } elseif (isset($object['@type']) && $object['@type'] === 'user') {
                            echo "User info: " . json_encode($object, JSON_PRETTY_PRINT) . PHP_EOL;
                        } else {
                            echo "Unexpected response: " . json_encode($object) . PHP_EOL;
                        }
                    });
                    break;

                case 'l':
                    echo "Logging out..." . PHP_EOL;
                    $this->send_query(['@type' => 'logOut']);
                    break;

                case 'm':
                    if (isset($parts[1])) {
                        $this->send_message($parts[1]);
                    } else {
                        echo "Usage: m <chat_id> <text>" . PHP_EOL;
                    }
                    break;

                case 'c':
                    echo "Loading chat list..." . PHP_EOL;
                    $this->get_chats();
                    break;

                default:
                    echo "Unknown action: $action" . PHP_EOL;
            }
        }
    }

    private function send_message($input) {
        $sub_parts = explode(' ', $input, 2);
        if (count($sub_parts) >= 2) {
            $chat_id = (int)$sub_parts[0];
            $text = $sub_parts[1];

            echo "Sending message to chat $chat_id..." . PHP_EOL;

            $send_message = [
                '@type' => 'sendMessage',
                'chat_id' => $chat_id,
                'input_message_content' => [
                    '@type' => 'inputMessageText',
                    'text' => [
                        '@type' => 'formattedText',
                        'text' => $text,
                        'entities' => []
                    ]
                ]
            ];

            $this->send_query($send_message, function($object) use ($chat_id) {
                if (isset($object['@type']) && $object['@type'] == 'error') {
                    echo "Failed to send message to chat $chat_id: " . $object['message'] . PHP_EOL;
                } else {
                    echo "Message sent successfully to chat $chat_id" . PHP_EOL;
                }
            });
        } else {
            echo "Usage: m <chat_id> <text>" . PHP_EOL;
        }
    }

    private function get_chats() {
        $this->send_query([
            '@type' => 'getChats',
            'chat_list' => ['@type' => 'chatListMain'],
            'limit' => 20
        ], function($object) {
            if (isset($object['@type']) && $object['@type'] == 'error') {
                echo "Error getting chats: " . $object['message'] . PHP_EOL;
                return;
            }

            if (isset($object['chat_ids'])) {
                foreach ($object['chat_ids'] as $chat_id) {
                    $title = $this->chat_titles[$chat_id] ?? 'unknown chat';
                    echo "[chat_id:$chat_id] [title:$title]" . PHP_EOL;
                }

                if (count($object['chat_ids']) === 0) {
                    echo "No chats found" . PHP_EOL;
                }
            }
        });
    }

    private function restart() {
        echo "Restarting TDLib..." . PHP_EOL;
        $this->__destruct();
        sleep(1);
        $this->__construct();
    }

    private function send_query($query, $handler = null) {
        $query_id = ++$this->current_query_id;

        if ($handler) {
            $this->handlers[$query_id] = $handler;
        }

        $query['@extra'] = ['request_id' => $query_id];
        $json = json_encode($query);

        if (json_last_error() !== JSON_ERROR_NONE) {
            echo "JSON encode error: " . json_last_error_msg() . PHP_EOL;
            return;
        }

        $this->ffi->td_json_client_send($this->client, $json);
    }

    private function receive($timeout) {
        try {
            $response_ptr = $this->ffi->td_json_client_receive($this->client, $timeout);

            if ($response_ptr === null) {
                return null;
            }

            if (is_string($response_ptr)) {
                $response_json = $response_ptr;
            } elseif ($response_ptr instanceof FFI\CData) {
                $response_json = FFI::string($response_ptr);
            } else {
                return null;
            }

            if (!empty($response_json)) {
                $response = json_decode($response_json, true);

                if (json_last_error() === JSON_ERROR_NONE) {
                    return $response;
                }
            }
        } catch (Throwable $e) {}

        return null;
    }

    private function process_response($response) {
        if (!$response || !is_array($response)) {
            return;
        }

        if (isset($response['@type']) && $response['@type'] === 'error') {
            $this->handle_error($response);
            return;
        }

        if (isset($response['@extra']['request_id'])) {
            $request_id = $response['@extra']['request_id'];
            if (isset($this->handlers[$request_id])) {
                $this->handlers[$request_id]($response);
                unset($this->handlers[$request_id]);
                return;
            }
        }

        $this->process_update($response);
    }

    private function handle_error($error) {
        if (!isset($error['message'])) {
            return;
        }

        $message = $error['message'];

        if (strpos($message, 'Failed to repair server unread count') !== false ||
            strpos($message, 'receive read_inbox_max_message_id') !== false) {
            return;
        }

        echo "Error: " . $message . " (code: " . ($error['code'] ?? 'unknown') . ")" . PHP_EOL;
    }

    private function get_user_name($user_id) {
        if (!isset($this->users[$user_id])) {
            return 'unknown user';
        }

        $user = $this->users[$user_id];
        $name = $user['first_name'] ?? '';
        if (isset($user['last_name'])) {
            $name .= ' ' . $user['last_name'];
        }
        return trim($name);
    }

    private function get_chat_title($chat_id) {
        return $this->chat_titles[$chat_id] ?? 'unknown chat';
    }

    private function process_update($update) {
        if (!isset($update['@type'])) {
            return;
        }

        switch ($update['@type']) {
            case 'updateAuthorizationState':
                $this->authorization_state = $update['authorization_state'];
                $this->on_authorization_state_update();
                break;

            case 'updateNewChat':
                if (isset($update['chat']['id'], $update['chat']['title'])) {
                    $this->chat_titles[$update['chat']['id']] = $update['chat']['title'];
                }
                break;

            case 'updateChatTitle':
                if (isset($update['chat_id'], $update['title'])) {
                    $this->chat_titles[$update['chat_id']] = $update['title'];
                }
                break;

            case 'updateUser':
                if (isset($update['user']['id'])) {
                    $user_id = $update['user']['id'];
                    $this->users[$user_id] = $update['user'];
                }
                break;

            case 'updateNewMessage':
                $this->process_new_message($update);
                break;

            case 'updateConnectionState':
                if (isset($update['state']['@type'])) {
                    $state = str_replace('connectionState', '', $update['state']['@type']);
                    echo "Connection state: $state" . PHP_EOL;
                }
                break;

            case 'updateServiceNotification':
                break;
        }
    }

    private function process_new_message($update) {
        if (!isset($update['message']['chat_id'])) {
            return;
        }

        $chat_id = $update['message']['chat_id'];
        $sender_name = 'unknown';

        if (isset($update['message']['sender_id']['@type'])) {
            $sender_type = $update['message']['sender_id']['@type'];
            if ($sender_type == 'messageSenderUser' && isset($update['message']['sender_id']['user_id'])) {
                $sender_name = $this->get_user_name($update['message']['sender_id']['user_id']);
            } elseif ($sender_type == 'messageSenderChat' && isset($update['message']['sender_id']['chat_id'])) {
                $sender_name = $this->get_chat_title($update['message']['sender_id']['chat_id']);
            }
        }

        $text = '';
        if (isset($update['message']['content']['@type'])) {
            $content_type = $update['message']['content']['@type'];
            if ($content_type == 'messageText' && isset($update['message']['content']['text']['text'])) {
                $text = $update['message']['content']['text']['text'];
            } else {
                $text = "[$content_type]";
            }
        }

        echo "New message: [chat_id:$chat_id] [from:$sender_name] [$text]" . PHP_EOL;
    }

    private function create_authentication_query_handler() {
        $current_id = $this->authentication_query_id;
        return function($object) use ($current_id) {
            if ($current_id == $this->authentication_query_id) {
                $this->check_authentication_error($object);
            }
        };
    }

    private function on_authorization_state_update() {
        $this->authentication_query_id++;

        if (!isset($this->authorization_state['@type'])) {
            return;
        }

        $handler = $this->create_authentication_query_handler();

        switch ($this->authorization_state['@type']) {
            case 'authorizationStateReady':
                $this->are_authorized = true;
                echo "Authorization is completed" . PHP_EOL;
                break;

            case 'authorizationStateLoggingOut':
                $this->are_authorized = false;
                echo "Logging out..." . PHP_EOL;
                break;

            case 'authorizationStateClosing':
                echo "Closing..." . PHP_EOL;
                break;

            case 'authorizationStateClosed':
                $this->are_authorized = false;
                $this->need_restart = true;
                echo "TDLib terminated" . PHP_EOL;
                break;

            case 'authorizationStateWaitPhoneNumber':
                echo "Enter phone number (international format, e.g., +1234567890): ";
                $phone_number = trim(fgets(STDIN));
                $this->send_query([
                    '@type' => 'setAuthenticationPhoneNumber',
                    'phone_number' => $phone_number
                ], $handler);
                break;

            case 'authorizationStateWaitCode':
                echo "Enter authentication code: ";
                $code = trim(fgets(STDIN));
                $this->send_query([
                    '@type' => 'checkAuthenticationCode',
                    'code' => $code
                ], $handler);
                break;

            case 'authorizationStateWaitPassword':
                echo "Enter authentication password: ";
                $password = trim(fgets(STDIN));
                $this->send_query([
                    '@type' => 'checkAuthenticationPassword',
                    'password' => $password
                ], $handler);
                break;

            case 'authorizationStateWaitTdlibParameters':
                $this->send_query([
                    '@type' => 'setTdlibParameters',
                    'database_directory' => 'tdlib_db',
                    'use_file_database' => true,
                    'use_chat_info_database' => true,
                    'use_message_database' => true,
                    'use_secret_chats' => true,
                    'api_id' => 94575,
                    'api_hash' => 'a3406de8d171bb422bb6ddf3bbd800e2',
                    'system_language_code' => 'en',
                    'device_model' => 'PHP Client',
                    'system_version' => PHP_OS,
                    'application_version' => '1.0',
                    'enable_storage_optimizer' => true
                ], $handler);
                break;

            default:
                echo "Unknown authorization state: " . $this->authorization_state['@type'] . PHP_EOL;
        }
    }

    private function check_authentication_error($object) {
        if (isset($object['@type']) && $object['@type'] == 'error') {
            echo "Authentication error: " . $object['message'] . PHP_EOL;
            $this->on_authorization_state_update();
        }
    }

    public function __destruct() {
        if ($this->client !== null) {
            $this->ffi->td_json_client_destroy($this->client);
            $this->client = null;
            echo "TDLib client destroyed" . PHP_EOL;
        }
    }
}

if (!extension_loaded('ffi')) {
    echo "FFI extension is required. Please install it or enable in php.ini" . PHP_EOL;
    exit(1);
}

try {
    $example = new TDLibClient();
    $example->loop();
} catch (Exception $e) {
    echo "Error: " . $e->getMessage() . PHP_EOL;
    exit(1);
}
