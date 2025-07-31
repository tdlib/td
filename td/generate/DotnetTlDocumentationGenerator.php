<?php

require_once 'TlDocumentationGenerator.php';

class DotnetTlDocumentationGenerator extends TlDocumentationGenerator
{
    private $cpp_cli = false;

    private function getArrayType($type_name)
    {
        if ($this->cpp_cli) {
            return $type_name.'[]';
        }

        return 'System.Collections.Generic.IList{'.$type_name.'}';
    }

    protected function isStandaloneFile()
    {
        return true;
    }

    protected function getDocumentationBegin()
    {
        $documentation = <<<EOT
<?xml version="1.0"?>
<doc>
    <assembly>
        "Telegram.Td"
    </assembly>
    <members>
        <member name="M:Telegram.Td.Client.Create(Telegram.Td.ClientResultHandler)">
            <summary>
Creates new Client.
</summary>
            <param name="updateHandler">Handler for incoming updates.</param>
            <returns>Returns created Client.</returns>
        </member>
        <member name="M:Telegram.Td.Client.Run">
            <summary>
Launches a cycle which will fetch all results of queries to TDLib and incoming updates from TDLib.
Must be called once on a separate dedicated thread on which all updates and query results from all Clients will be handled.
Never returns.
</summary>
        </member>
        <member name="M:Telegram.Td.Client.Execute(Telegram.Td.Api.Function)">
            <summary>
Synchronously executes a TDLib request. Only a few marked accordingly requests can be executed synchronously.
</summary>
            <param name="function">Object representing a query to the TDLib.</param>
            <returns>Returns request result.</returns>
            <exception cref="T:System.NullReferenceException">Thrown when query is null.</exception>
        </member>
        <member name="M:Telegram.Td.Client.Send(Telegram.Td.Api.Function,Telegram.Td.ClientResultHandler)">
            <summary>
Sends a request to the TDLib.
</summary>
            <param name="function">Object representing a query to the TDLib.</param>
            <param name="handler">Result handler with OnResult method which will be called with result
of the query or with Telegram.Td.Api.Error as parameter. If it is null, nothing will be called.</param>
            <exception cref="T:System.NullReferenceException">Thrown when query is null.</exception>
        </member>
        <member name="T:Telegram.Td.Client">
            <summary>
Main class for interaction with the TDLib.
</summary>
        </member>
        <member name="M:Telegram.Td.ClientResultHandler.OnResult(Telegram.Td.Api.BaseObject)">
            <summary>
Callback called on result of query to TDLib or incoming update from TDLib.
</summary>
            <param name="object">Result of query or update of type Telegram.Td.Api.Update about new events.</param>
        </member>
        <member name="T:Telegram.Td.ClientResultHandler">
            <summary>
Interface for handler for results of queries to TDLib and incoming updates from TDLib.
</summary>
        </member>
EOT;

        if ($this->cpp_cli) {
            return $documentation;
        }

        $documentation .= <<<EOT
        <member name="M:Telegram.Td.Client.SetLogMessageCallback(System.Int32,Telegram.Td.LogMessageCallback)">
            <summary>
Sets the callback that will be called when a message is added to the internal TDLib log.
None of the TDLib methods can be called from the callback.
</summary>
            <param name="max_verbosity_level">The maximum verbosity level of messages for which the callback will be called.</param>
            <param name="callback">Callback that will be called when a message is added to the internal TDLib log.
Pass null to remove the callback.</param>
        </member>
        <member name="T:Telegram.Td.LogMessageCallback">
            <summary>
A type of callback function that will be called when a message is added to the internal TDLib log.
</summary>
            <param name="verbosityLevel">Log verbosity level with which the message was added from -1 up to 1024.
If 0, then TDLib will crash as soon as the callback returns.
None of the TDLib methods can be called from the callback.</param>
            <param name="message">The message added to the log.</param>
        </member>
EOT;

        return $documentation;
    }

    protected function getDocumentationEnd()
    {
        return <<<EOT
    </members>
</doc>
EOT;
    }

    protected function escapeDocumentation($doc)
    {
        $doc = preg_replace_callback('/(?<!["A-Za-z_\/])[A-Za-z]+(_[A-Za-z]+)+/',
            function ($word_matches)
            {
                return ucfirst(preg_replace_callback('/_([A-Za-z])/', function ($matches) {return strtoupper($matches[1]);}, $word_matches[0]));
            }, $doc);
        $doc = htmlspecialchars($doc, ENT_XML1, 'UTF-8');
        $doc = str_replace('*/', '*&#47;', $doc);
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
                return 'System.Boolean';
            case 'int32':
                return 'System.Int32';
            case 'int53':
            case 'int64':
                return 'System.Int64';
            case 'double':
                return 'System.Double';
            case 'string':
                return 'System.String';
            case 'bytes':
                return $this->getArrayType('System.Byte');
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
                    return $this->getArrayType($this->getTypeName(substr($type, 7, -1)));
                }

                if (preg_match('/[^A-Za-z0-9.]/', $type)) {
                    $this->printError("Wrong type $type");
                    return '';
                }
                return $this->getClassName($type);
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
        $this->addDocumentation('T:Object', <<<EOT
        <member name="T:Telegram.Td.Api.Object">
            <summary>
This class is a base class for all TDLib interface classes.
</summary>
        </member>
EOT
);

        $this->addDocumentation('T:Function', <<<EOT
        <member name="T:Telegram.Td.Api.Function">
            <summary>
This class is a base class for all TDLib interface function-classes.
</summary>
        </member>
EOT
);
    }

    protected function addAbstractClassDocumentation($class_name, $documentation)
    {
        $this->addDocumentation("T:$class_name", <<<EOT
        <member name="T:Telegram.Td.Api.$class_name">
            <summary>
This class is an abstract base class.
$documentation
</summary>
        </member>
EOT
);
    }

    protected function getFunctionReturnTypeDescription($return_type, $for_constructor)
    {
        return "\r\n            <para>Returns <see cref=\"T:Telegram.Td.Api.$return_type\"/>.</para>";
    }

    protected function addClassDocumentation($class_name, $base_class_name, $return_type, $description)
    {
        $this->addDocumentation("T:$class_name", <<<EOT
        <member name="T:Telegram.Td.Api.$class_name">
            <summary>
$description
</summary>
        </member>
EOT
);
    }

    protected function addFieldDocumentation($class_name, $field_name, $type_name, $field_info, $may_be_null)
    {
        $this->addDocumentation("P:$class_name.$field_name", <<<EOT
        <member name="P:Telegram.Td.Api.$class_name.$field_name">
            <summary>
$field_info
</summary>
        </member>
EOT
);
    }

    protected function addDefaultConstructorDocumentation($class_name, $class_description)
    {
        $this->addDocumentation("M:$class_name.#ctor", <<<EOT
        <member name="M:Telegram.Td.Api.$class_name.ToString">
            <summary>
Returns string representation of the object.
</summary>
            <returns>Returns string representation of the object.</returns>
        </member>
        <member name="M:Telegram.Td.Api.$class_name.#ctor">
            <summary>
$class_description
</summary>
        </member>
EOT
);
    }

    protected function addFullConstructorDocumentation($class_name, $class_description, $known_fields, $info)
    {
        $full_constructor = "";
        $colon = '';
        foreach ($known_fields as $name => $type) {
            $field_type = $this->getTypeName($type);
            $pos = 0;
            while (substr($field_type, $pos, 33) === 'System.Collections.Generic.IList{') {
                $pos += 33;
            }
            if (substr($field_type, $pos, 7) !== 'System.') {
                $field_type = substr($field_type, 0, $pos).'Telegram.Td.Api.'.substr($field_type, $pos);
            }
            $full_constructor .= $colon.$field_type;
            $colon = ',';
        }

        $full_doc = <<<EOT
        <member name="M:Telegram.Td.Api.$class_name.#ctor($full_constructor)">
            <summary>
$class_description
</summary>
EOT;
        foreach ($known_fields as $name => $type) {
            $full_doc .= "\r\n            <param name=\"".$this->getParameterName($name, $class_name).'">'.$info[$name]."</param>";
        }
        $full_doc .= "\r\n        </member>";
        $this->addDocumentation("M:$class_name.#ctor($full_constructor)", $full_doc);
    }

    public function __construct($flavor_name) {
        $this->cpp_cli = $flavor_name !== 'CX';
    }
}

$flavor_name = isset($argv[3]) ? $argv[3] : 'Windows';

$generator = new DotnetTlDocumentationGenerator($flavor_name);
$generator->generate($argv[1], $argv[2]);
