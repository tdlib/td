<?php

function disjoint_set_find(&$parents, $x) {
    if ($parents[$x] !== $x) {
        return $parents[$x] = disjoint_set_find($parents, $parents[$x]);
    }
    return $x;
}

function disjoint_set_union(&$parents, $x, $y) {
    $x = disjoint_set_find($parents, $x);
    $y = disjoint_set_find($parents, $y);

    if ($x !== $y) {
        if (rand(0, 1) == 0) {
            $parents[$x] = $y;
        } else {
            $parents[$y] = $x;
        }
    }
}

function split_file($file, $chunks, $undo) {
    $cpp_name = "$file.cpp";

    echo "Processing file $cpp_name".PHP_EOL;

    $new_files = array();
    foreach (range(0, $chunks - 1) as $n) {
        $new_files[] = "$file$n.cpp";
    }

    $is_generated = (strpos($file, 'td/generate/') === 0);

    $cmake_file = $is_generated ? 'td/generate/CMakeLists.txt' : 'CMakeLists.txt';
    $cmake = file_get_contents($cmake_file);

    $cmake_cpp_name = $cpp_name;
    $cmake_new_files = $new_files;
    if ($is_generated) {
        foreach ($cmake_new_files as &$file_ref) {
            $file_ref = str_replace('td/generate/auto/td', '${TD_AUTO_INCLUDE_DIR}', $file_ref);
        }
        $cmake_cpp_name = str_replace('td/generate/auto/td', '${TD_AUTO_INCLUDE_DIR}', $cmake_cpp_name);
    }

    if ($undo) {
        foreach ($new_files as $file) {
            if (file_exists($file)) {
                echo "Unlinking ".$file.PHP_EOL;
                unlink($file);
            }
        }

        if (strpos($cmake, $cmake_cpp_name) === false) {
            $cmake = str_replace(implode(PHP_EOL.'  ', $cmake_new_files), $cmake_cpp_name, $cmake);
            file_put_contents($cmake_file, $cmake);
        }

        return;
    }

    if (strpos($cmake, $cmake_cpp_name) !== false) {
        $cmake = str_replace($cmake_cpp_name, implode(PHP_EOL.'  ', $cmake_new_files), $cmake);
        file_put_contents($cmake_file, $cmake);
    }

    if (!file_exists($cpp_name)) {
        echo "ERROR: skip unexisting file $cpp_name".PHP_EOL;
        return;
    }

    $lines = file($cpp_name);
    $depth = 0;
    $target_depth = 1 + $is_generated;
    $is_static = false;
    $in_define = false;
    $in_comment = false;
    $current = '';
    $common = '';
    $functions = array();
    $namespace_begin = '';
    $namespace_end = '';
    foreach ($lines as $line) {
        $add_depth = strpos($line, 'namespace ') === 0 ? 1 : (strpos($line, '}  // namespace') === 0 ? -1 : 0);
        if ($add_depth) {
            # namespace begin/end
            if ($add_depth > 0) {
              $depth += $add_depth;
            }
            if ($depth <= $target_depth) {
                if ($add_depth > 0) {
                    $namespace_begin .= $line;
                } else {
                    $namespace_end .= $line;
                }
            }
            if ($add_depth < 0) {
              $depth += $add_depth;
            }
            if ($is_static) {
                $common .= $current;
            } else {
                $functions[] = $current;
            }
            $common .= $line;
            $current = '';
            $is_static = false;
            $in_define = false;
            continue;
        }

        if (strpos($line, '#undef') === 0 && !trim($current)) {
            continue;
        }

        if ($in_comment && strpos($line, '*/') === 0) {
            $in_comment = false;
            continue;
        }
        if (strpos($line, '/*') === 0) {
            $in_comment = true;
        }
        if ($in_comment) {
            continue;
        }

        if ($depth !== $target_depth) {
            $common .= $line;
            continue;
        }

        if (strpos($line, 'static ') === 0 && $depth === $target_depth) {
            $is_static = true;
        }
        if (!trim($current) && strpos($line, '#define ') === 0) {
            $is_static = true;
            $in_define = true;
        }

        $current .= $line;
        if ((strpos($line, '}') === 0 || ($in_define && !trim($line)) || preg_match('/^[a-z].*;\s*$/i', $line)) && $depth === $target_depth) {
            # block end
            if ($is_static) {
                $common .= $current;
            } else {
                $functions[] = $current;
            }
            $current = '';
            $is_static = false;
            $in_define = false;
        }
    }
    $current = trim($current);
    if (!empty($current)) {
        fwrite(STDERR, "ERROR: $current".PHP_EOL);
        exit();
    }

    if (count($functions) < $chunks) {
        fwrite(STDERR, "ERROR: file is too small to be splitted more".PHP_EOL);
        return;
    }

    $deps = array();  // all functions from the same subarray must be in the same file
    $parents = array();
    foreach ($functions as $i => $f) {
        if (preg_match_all('/(?J)(create_handler|create_net_actor)<(?<name>[A-Z][A-Za-z]*)>|'.
                           '(?<name>[A-Z][A-Za-z]*) : public (Td::ResultHandler|NetActor|Request)|'.
                           '(CREATE_REQUEST|CREATE_NO_ARGS_REQUEST)[(](?<name>[A-Z][A-Za-z]*)|'.
                           '(?<name>complete_pending_preauthentication_requests)|'.
                           '(?<name>get_message_history_slice)|'.
                           '(Up|Down)load[a-zA-Z]*C(?<name>allback)|(up|down)load_[a-z_]*_c(?<name>allback)_|'.
                           '(?<name>lazy_to_json)|'.
                           '(?<name>LogEvent)[^sA]|'.
                           '(?<name>parse)[(]|'.
                           '(?<name>store)[(]/', $f, $matches, PREG_SET_ORDER)) {
            foreach ($matches as $match) {
                $name = $match['name'];
                if ($name === 'parse' || $name === 'store') {
                    if ($is_generated) {
                        continue;
                    }
                    $name = 'LogEvent';
                }
                $deps[$name][] = $i;
            }
        }
        $parents[$i] = $i;
    }

    foreach ($deps as $func_ids) {
        foreach ($func_ids as $func_id) {
            disjoint_set_union($parents, $func_ids[0], $func_id);
        }
    }
    $sets = array();
    $set_sizes = array();
    foreach ($functions as $i => $f) {
        $parent = disjoint_set_find($parents, $i);
        if (!isset($sets[$parent])) {
            $sets[$parent] = '';
            $set_sizes[$parent] = 0;
        }
        $sets[$parent] .= $f;
        $set_sizes[$parent] += preg_match('/Td::~?Td/', $f) ? 1000000 : strlen($f);
    }
    arsort($set_sizes);

    $files = array_fill(0, $chunks, '');
    $file_sizes = array_fill(0, $chunks, 0);
    foreach ($set_sizes as $parent => $size) {
        $file_id = array_search(min($file_sizes), $file_sizes);
        $files[$file_id] .= $sets[$parent];
        $file_sizes[$file_id] += $size;
    }

    foreach ($files as $n => $f) {
        $new_content = $common.$namespace_begin.$f.$namespace_end;

        $std_methods = array();
        preg_match_all('/std::[a-z_0-9]*/', $new_content, $std_methods);
        $std_methods = array_unique($std_methods[0]);

        $needed_std_headers = array();
        $type_headers = array(
            'std::move' => '',
            'std::vector' => '',
            'std::string' => '',
            'std::uint32_t' => '',
            'std::int32_t' => '',
            'std::int64_t' => '',
            'std::fill' => 'algorithm',
            'std::find' => 'algorithm',
            'std::max' => 'algorithm',
            'std::min' => 'algorithm',
            'std::remove' => 'algorithm',
            'std::reverse' => 'algorithm',
            'std::rotate' => 'algorithm',
            'std::sort' => 'algorithm',
            'std::abs' => 'cmath',
            'std::numeric_limits' => 'limits',
            'std::make_shared' => 'memory',
            'std::shared_ptr' => 'memory',
            'std::tie' => 'tuple',
            'std::tuple' => 'tuple',
            'std::decay_t' => 'type_traits',
            'std::is_same' => 'type_traits',
            'std::make_pair' => 'utility',
            'std::pair' => 'utility',
            'std::swap' => 'utility',
            'std::unordered_map' => 'unordered_map',
            'std::unordered_set' => 'unordered_set');
        foreach ($type_headers as $type => $header) {
            if (in_array($type, $std_methods)) {
                $std_methods = array_diff($std_methods, array($type));
                if ($header && !in_array($header, $needed_std_headers)) {
                    $needed_std_headers[] = $header;
                }
            }
        }

        if (!$std_methods) { // know all needed std headers
            $new_content = preg_replace_callback(
                '/#include <([a-z_]*)>/',
                function ($matches) use ($needed_std_headers) {
                    if (in_array($matches[1], $needed_std_headers)) {
                        return $matches[0];
                    }
                    return '';
                },
                $new_content
            );
        }

        if (!preg_match('/Td::~?Td/', $new_content)) {  // destructor Td::~Td needs to see definitions of all forward-declared classes
            $td_methods = array(
                'animations_manager[_(-][^.]|AnimationsManager[^;>]' => "AnimationsManager",
                'audios_manager[_(-][^.]|AudiosManager' => "AudiosManager",
                'auth_manager[_(-][^.]|AuthManager' => 'AuthManager',
                'background_manager[_(-][^.]|BackgroundManager' => "BackgroundManager",
                'ConfigShared|shared_config[(]' => 'ConfigShared',
                'contacts_manager[_(-][^.]|ContactsManager([^ ;.]| [^*])' => 'ContactsManager',
                'country_info_manager[_(-][^.]|CountryInfoManager' => 'CountryInfoManager',
                'documents_manager[_(-][^.]|DocumentsManager' => "DocumentsManager",
                'file_reference_manager[_(-][^.]|FileReferenceManager|file_references[)]' => 'FileReferenceManager',
                'file_manager[_(-][^.]|FileManager([^ ;.]| [^*])|update_file[)]' => 'files/FileManager',
                'G[(][)]|Global[^A-Za-z]' => 'Global',
                'group_call_manager[_(-][^.]|GroupCallManager' => 'GroupCallManager',
                'HashtagHints' => 'HashtagHints',
                'inline_queries_manager[_(-][^.]|InlineQueriesManager' => 'InlineQueriesManager',
                'language_pack_manager[_(-][^.]|LanguagePackManager' => 'LanguagePackManager',
                'LogeventIdWithGeneration|add_log_event|delete_log_event|get_erase_log_event_promise|parse_time|store_time' => 'logevent/LogEventHelper',
                'MessageCopyOptions' => 'MessageCopyOptions',
                'messages_manager[_(-][^.]|MessagesManager' => 'MessagesManager',
                'notification_manager[_(-][^.]|NotificationManager|notifications[)]' => 'NotificationManager',
                'phone_number_manager[_(-][^.]|PhoneNumberManager' => "PhoneNumberManager",
                'poll_manager[_(-][^.]|PollManager' => "PollManager",
                'PublicDialogType|get_public_dialog_type' => 'PublicDialogType',
                'SecretChatActor' => 'SecretChatActor',
                'secret_chats_manager[_(-][^.]|SecretChatsManager' => 'SecretChatsManager',
                'stickers_manager[_(-][^.]|StickersManager' => 'StickersManager',
                '[>](td_db[(][)]|get_td_db_impl[(])|TdDb[^A-Za-z]' => 'TdDb',
                'TopDialogCategory|get_top_dialog_category' => 'TopDialogCategory',
                'top_dialog_manager[_(-][^.]|TopDialogManager' => 'TopDialogManager',
                'updates_manager[_(-][^.]|UpdatesManager|get_difference[)]|updateSentMessage|dummyUpdate' => 'UpdatesManager',
                'WebPageId(Hash)?' => 'WebPageId',
                'web_pages_manager[_(-][^.]|WebPagesManager' => 'WebPagesManager');

            foreach ($td_methods as $pattern => $header) {
                if (strpos($cpp_name, $header) !== false) {
                    continue;
                }

                $include_name = '#include "td/telegram/'.$header.'.h"';
                if (strpos($new_content, $include_name) !== false && preg_match('/'.$pattern.'/', str_replace($include_name, '', $new_content)) === 0) {
                    $new_content = str_replace($include_name, '', $new_content);
                }
            }
        } else {
            $new_content = preg_replace_callback(
                '|#include "[a-z_A-Z/0-9.]*"|',
                function ($matches) {
                    if (strpos($matches[0], "Manager") !== false || strpos($matches[0], "HashtagHints") !== false || strpos($matches[0], "Td.h") !== false) {
                        return $matches[0];
                    }
                    return '';
                },
                $new_content
            );
        }

        if (!file_exists($new_files[$n]) || file_get_contents($new_files[$n]) !== $new_content) {
            echo "Writing file ".$new_files[$n].PHP_EOL;
            file_put_contents($new_files[$n], $new_content);
        }
    }
}

if (in_array('--help', $argv) || in_array('-h', $argv)) {
    echo "Usage: php SplitSource.php [OPTION]...\n".
         "Splits some source files to reduce a maximum amount of RAM needed for compiling a single file.\n".
         "  -u, --undo Undo all source code changes.\n".
         "  -h, --help Show this help.\n";
    exit(2);
}

$undo = in_array('--undo', $argv) || in_array('-u', $argv);
$files = array('td/telegram/ContactsManager' => 20,
               'td/telegram/MessagesManager' => 50,
               'td/telegram/NotificationManager' => 10,
               'td/telegram/StickersManager' => 10,
               'td/telegram/Td' => 50,
               'td/generate/auto/td/telegram/td_api' => 10,
               'td/generate/auto/td/telegram/td_api_json' => 10,
               'td/generate/auto/td/telegram/telegram_api' => 10);

foreach ($files as $file => $chunks) {
    split_file($file, $chunks, $undo);
}
