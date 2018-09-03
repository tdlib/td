<?php

require_once 'TlDocumentationGenerator.php';

class DotnetTlDocumentationGenerator extends TlDocumentationGenerator
{
    protected function escapeDocumentation($doc)
    {
        $doc = htmlspecialchars($doc, ENT_XML1);
        $doc = str_replace('*/', '*&#47;', $doc);
        $doc = preg_replace_callback('/_([A-Za-z])/', function ($matches) {return strtoupper($matches[1]);}, $doc);
        return $doc;
    }

    protected function getFieldName($name, $class_name)
    {
        $name = ucfirst($this->getParameterName($name, $class_name));
        if ($name === $class_name) {
          $name .= 'Value';
        }
        return $name;
    }

    protected function getParameterName($name, $class_name)
    {
        if (substr($name, 0, 6) === 'param_') {
            $name = substr($name, 6);
        }
        $name = preg_replace_callback('/_([A-Za-z])/', function ($matches) {return strtoupper($matches[1]);}, trim($name));
        return $name;
    }

    protected function getClassName($type)
    {
        return implode(array_map('ucfirst', explode('.', trim($type, "\r\n ;"))));
    }

    protected function getTypeName($type)
    {
        switch ($type) {
            case 'Bool':
                return 'bool';
            case 'int32':
                return 'int32';
            case 'int53':
            case 'int64':
                return 'int64';
            case 'double':
                return 'float64';
            case 'string':
                return 'String^';
            case 'bytes':
                return 'Array<byte>^';
            case 'bool':
            case 'int':
            case 'long':
            case 'Int':
            case 'Long':
            case 'Int32':
            case 'Int53':
            case 'Int64':
            case 'Double':
            case 'String':
            case 'Bytes':
                $this->printError("Wrong type $type");
                return '';
            default:
                if (substr($type, 0, 6) === 'vector') {
                    if ($type[6] !== '<' || $type[strlen($type) - 1] !== '>') {
                        $this->printError("Wrong vector subtype in $type");
                        return '';
                    }
                    return 'Array<'.$this->getTypeName(substr($type, 7, -1)).'>^';
                }

                if (preg_match('/[^A-Za-z0-9.]/', $type)) {
                    $this->printError("Wrong type $type");
                    return '';
                }
                return $this->getClassName($type).'^';
        }
    }

    protected function getBaseClassName($is_function)
    {
        return $is_function ? 'Function' : 'Object';
    }

    protected function needRemoveLine($line)
    {
        return strpos(trim($line), '///') === 0;
    }

    protected function needSkipLine($line)
    {
        $line = trim($line);
        return !$line || $line === 'public:' || $line === 'private:' || $line[0] === '}' ||
            strpos($line, 'Unmanaged') > 0 || strpos($line, 'PrivateField') > 0 || strpos($line, 'get()') > 0 ||
            strpos($line, 'void set(') === 0 || preg_match('/^[a-z]* class .*;/', $line) ||
            strpos($line, 'namespace ') === 0 || strpos($line, '#include ') === 0;
    }

    protected function isHeaderLine($line)
    {
        return false;
    }

    protected function extractClassName($line)
    {
        if (strpos($line, 'public ref class ') !== false || strpos($line, 'public interface class ') !== false) {
            return explode(' ', $line)[3];
        }
        return '';
    }

    protected function fixLine($line)
    {
        return $line;
    }

    protected function addGlobalDocumentation()
    {
        $this->addDocumentation('public interface class Object : BaseObject {', <<<EOT
/// <summary>
/// This class is a base class for all TDLib interface classes.
/// </summary>
EOT
);

        $this->addDocumentation('  virtual String^ ToString() override;', <<<EOT
  /// <summary>
  /// Returns string representation of the object.
  /// </summary>
  /// <returns>Returns string representation of the object.</returns>
EOT
);

        $this->addDocumentation('public interface class Function : BaseObject {', <<<EOT
/// <summary>
/// This class is a base class for all TDLib interface function-classes.
/// </summary>
EOT
);
    }

    protected function addAbstractClassDocumentation($class_name, $documentation)
    {
        $this->addDocumentation("public interface class $class_name : Object {", <<<EOT
/// <summary>
/// This class is an abstract base class.
/// $documentation
/// </summary>
EOT
);
    }

    protected function addClassDocumentation($class_name, $base_class_name, $description, $return_type)
    {
        $return_type_description = $return_type ? "\r\n/// <para>Returns <see cref=\"".substr($return_type, 0, -1).'"/>.</para>' : '';

        $this->addDocumentation("public ref class $class_name sealed : $base_class_name {", <<<EOT
/// <summary>
/// $description$return_type_description
/// </summary>
EOT
);
    }

    protected function addFieldDocumentation($class_name, $field_name, $type_name, $field_info, $may_be_null)
    {
        $end = ';';
        if (substr($type_name, 0, strlen($field_name)) === $field_name) {
            $type_name = '::Telegram::Td::Api::'.$type_name;
            $end = ' {';
        }
        $full_line = $class_name."  property $type_name $field_name$end";
        $this->addDocumentation($full_line, <<<EOT
  /// <summary>
  /// $field_info
  /// </summary>
EOT
);
    }

    protected function addDefaultConstructorDocumentation($class_name)
    {
        $this->addDocumentation("  $class_name();", <<<EOT
  /// <summary>
  /// Default constructor.
  /// </summary>
EOT
);
    }

    protected function addFullConstructorDocumentation($class_name, $known_fields, $info)
    {
        $full_constructor = "  $class_name(";
        $colon = '';
        foreach ($known_fields as $name => $type) {
            $field_type = $this->getTypeName($type);
            if (substr($field_type, 0, 5) !== 'Array' && substr($field_type, 0, 6) !== 'String' &&
                ucfirst($field_type) === $field_type) {
                $field_type = '::Telegram::Td::Api::'.$field_type;
            }
            $full_constructor .= $colon.$field_type.' '.$this->getParameterName($name, $class_name);
            $colon = ', ';
        }
        $full_constructor .= ');';

        $full_doc = <<<EOT
  /// <summary>
  /// Constructor for initialization of all fields.
  /// </summary>
EOT;
        foreach ($known_fields as $name => $type) {
            $full_doc .= "\r\n  /// <param name=\"".$this->getParameterName($name, $class_name).'">'.$info[$name]."</param>";
        }
        $this->addDocumentation($full_constructor, $full_doc);
    }
}

$generator = new DotnetTlDocumentationGenerator();
$generator->generate($argv[1], $argv[2]);
