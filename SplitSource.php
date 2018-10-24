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
            $file_ref = str_replace('td/generate', '${CMAKE_CURRENT_SOURCE_DIR}', $file_ref);
        }
        $cmake_cpp_name = str_replace('td/generate', '${CMAKE_CURRENT_SOURCE_DIR}', $cmake_cpp_name);
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

    $lines = file($cpp_name);
    $depth = 0;
    $target_depth = 1 + $is_generated;
    $is_static = false;
    $in_define = false;
    $current = '';
    $common = '';
    $functions = array();
    $namespace_begin = '';
    $namespace_end = '';
    foreach ($lines as $line) {
        $add_depth = strpos($line, 'namespace ') === 0 ? 1 : (strpos($line, '}  // namespace') === 0 ? -1 : 0);
        if ($add_depth) {
            # namespace begin/end
            $depth += $add_depth;
            if ($depth <= $target_depth) {
                if ($add_depth > 0) {
                    $namespace_begin .= $line;
                } else {
                    $namespace_end .= $line;
                }
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
    if (!empty(trim($current))) {
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
                           '(Up|Down)load[a-zA-Z]*C(?<name>allback)|(up|down)load_[a-z_]*_c(?<name>allback)_|'.
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
        $set_sizes[$parent] += strlen($f);
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
        if (!file_exists($new_files[$n]) || file_get_contents($new_files[$n]) !== $new_content) {
            echo "Writing file ".$new_files[$n].PHP_EOL;
            file_put_contents($new_files[$n], $new_content);
        }
    }
}

if (in_array('--help', $argv) || in_array('-h', $argv)) {
    echo "Usage: php SplitSource.php [OPTION]...\nSplits some source files to reduce maximum RAM needed for compiling a single file.\n  -u, --undo Undo all source code changes.\n  -h, --help Show this help.\n";
    exit(2);
}

$undo = in_array('--undo', $argv) || in_array('-u', $argv);
$files = array('td/telegram/ContactsManager' => 10,
               'td/telegram/MessagesManager' => 20,
               'td/telegram/Td' => 20,
               'td/telegram/StickersManager' => 10,
               'td/generate/auto/td/telegram/td_api' => 10,
               'td/generate/auto/td/telegram/td_api_json' => 10,
               'td/generate/auto/td/telegram/telegram_api' => 10);

foreach ($files as $file => $chunks) {
    split_file($file, $chunks, $undo);
}
