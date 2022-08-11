<?php
    if ($argc !== 2) {
        exit();
    }
    $file = file_get_contents($argv[1]);

    if (strpos($file, 'androidx.annotation.IntDef') !== false) {
        exit();
    }

    $file = str_replace('import androidx.annotation.Nullable;', 'import androidx.annotation.IntDef;'.PHP_EOL.
                                                                'import androidx.annotation.Nullable;'.PHP_EOL.
                                                                PHP_EOL.
                                                                'import java.lang.annotation.Retention;'.PHP_EOL.
                                                                'import java.lang.annotation.RetentionPolicy;', $file);

    preg_match_all('/public static class ([A-Za-z0-9]+) extends ([A-Za-z0-9]+)/', $file, $matches, PREG_SET_ORDER);
    $children = [];
    foreach ($matches as $val) {
        if ($val[2] === 'Object') {
            continue;
        }

        $children[$val[2]][] = PHP_EOL.'            '.$val[1].'.CONSTRUCTOR';
    }

    $file = preg_replace_callback('/public abstract static class ([A-Za-z0-9]+)(<R extends Object>)? extends Object [{]/',
        function ($val) use ($children) {
            return $val[0].PHP_EOL.'        @Retention(RetentionPolicy.SOURCE)'.PHP_EOL.'        @IntDef({'.implode(',', $children[$val[1]]).<<<'EOL'

        })
        public @interface Constructors {}

        /**
         * @return identifier uniquely determining type of the object.
         */
        @Constructors
        @Override
        public abstract int getConstructor();
EOL;
        },
        $file);

    file_put_contents($argv[1], $file);
