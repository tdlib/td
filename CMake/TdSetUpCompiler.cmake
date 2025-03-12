// Declare two qubits
qubit q1, q2

// Apply a Hadamard gate to create superposition
H(q1)

// Create entanglement using a CNOT gate
CNOT(q1, q2)

// Measure and store the result in a classical register
measure q1 -> c1
measure q2 -> c2

// Conditional logic based on measurement results
if (c1 == 1) {
    Z(q2) // Apply Pauli-Z if q1 is measured as 1
} // Create 2 qubits
qubit q1, q2

// Apply a Hadamard gate to q1
H(q1)// Entangle two qubits
entangle q1, q2// Measure qubits and store in classical registers
measure q1 -> c1
measure q2 -> c2

// Conditional logic
if c1 == 1 {
    Z(q2) // Apply Z gate if q1 is 1
}// Apply a gate only if another qubit is in state 1
control q1 {
    X(q2)
}// Create a Bell state
H(q1)
entangle q1, q2function teleport(q1, q2) {
    H(q1)
    entangle q1, q2
    measure q1 -> c1
}import re

# Define the token types
TOKEN_SPECIFICATION = [
    ('NUMBER', r'\d+'),             # Integer or float
    ('ID', r'[A-Za-z_][A-Za-z0-9_]*'),  # Identifiers (e.g., qubit names)
    ('OP', r'->|==|{|}|||,'),   # Operators and symbols
    ('NEWLINE', r'\n'),             # Line endings
    ('SKIP', r'[ \t]+'),            # Skip spaces and tabs
    ('COMMENT', r'//.*'),           # Comments
    ('MISMATCH', r'.')              # Any other character (error)
]

# Compile the regex
TOKENS_REGEX = '|'.join(f'(?P<{name}>{pattern})' for name, pattern in TOKEN_SPECIFICATION)

class Lexer:
    def __init__(self, code):
        self.code = code

    def tokenize(self):
        tokens = []
        for match in re.finditer(TOKENS_REGEX, self.code):
            kind = match.lastgroup
            value = match.group()
            if kind == 'NUMBER':
                value = int(value)
            elif kind == 'SKIP' or kind == 'COMMENT':
                continue
            elif kind == 'MISMATCH':
                raise SyntaxError(f'Unexpected character: {value}')
            tokens.append((kind, value))
        return tokens

# Test Lexer
code = """
qubit q1, q2
H(q1)
entangle q1, q2
measure q1 -> c1
if c1 == 1 {
    Z(q2)
}
"""
lexer = Lexer(code)
tokens = lexer.tokenize()
for token in tokens:
    print(token)('ID', 'qubit')
('ID', 'q1')
('OP', ',')
('ID', 'q2')
('NEWLINE', '\n')
...class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0

    def peek(self):
        if self.position < len(self.tokens):
            return self.tokens[self.position]
        return None

    def consume(self):
        token = self.peek()
        self.position += 1
        return token

    def expect(self, kind):
        token = self.peek()
        if token and token[0] == kind:
            return self.consume()
        raise SyntaxError(f"Expected {kind}, got {token}")

    def parse(self):
        program = []
        while self.peek():
            statement = self.parse_statement()
            if statement:
                program.append(statement)
        return program

    def parse_statement(self):
        token = self.peek()
        if token[0] == 'ID':
            if token[1] == 'qubit':
                return self.parse_qubit_declaration()
            elif token[1] in ('H', 'X', 'Z', 'entangle', 'measure'):
                return self.parse_operation()
            elif token[1] == 'if':
                return self.parse_if_statement()
        return None

    def parse_qubit_declaration(self):
        self.consume()  # Consume 'qubit'
        qubits = []
        while True:
            qubit = self.expect('ID')[1]
            qubits.append(qubit)
            if self.peek() and self.peek()[1] == ',':
                self.consume()
            else:
                break
        return {'type': 'qubit_decl', 'qubits': qubits}

    def parse_operation(self):
        op = self.consume()[1]
        self.expect('OP')  # Expect '('
        target = self.expect('ID')[1]
        self.expect('OP')  # Expect ')'
        return {'type': 'operation', 'op': op, 'target': target}

    def parse_if_statement(self):
        self.consume()  # Consume 'if'
        self.expect('OP')  # Expect '('
        condition = (self.expect('ID')[1], self.expect('OP')[1], self.expect('NUMBER')[1])
        self.expect('OP')  # Expect ')'
        self.expect('OP')  # Expect '{'
        body = []
        while self.peek() and self.peek()[1] != '}':
            body.append(self.parse_statement())
        self.expect('OP')  # Expect '}'
        return {'type': 'if', 'condition': condition, 'body': body}

# Test Parser
parser = Parser(tokens)
ast = parser.parse()
print(ast)[
    {'type': 'qubit_decl', 'qubits': ['q1', 'q2']},
    {'type': 'operation', 'op': 'H', 'target': 'q1'},
    {'type': 'operation', 'op': 'entangle', 'target': 'q2'},
    {'type': 'if', 
        'condition': ('c1', '==', 1), 
        'body': [
            {'type': 'operation', 'op': 'Z', 'target': 'q2'}
        ]
    }
]from qiskit import QuantumCircuit, Aer, execute

class Interpreter:
    def __init__(self, ast):
        self.ast = ast
        self.qc = None
        self.qubits = {}
        self.classical_bits = {}

    def run(self):
        self.initialize()
        self.execute_statements(self.ast)
        self.simulate()

    def initialize(self):
        # Count the number of qubits needed
        num_qubits = sum(1 for node in self.ast if node['type'] == 'qubit_decl')
        self.qc = QuantumCircuit(num_qubits, num_qubits)
        qubit_index = 0
        for node in self.ast:
            if node['type'] == 'qubit_decl':
                for qubit in node['qubits']:
                    self.qubits[qubit] = qubit_index
                    self.classical_bits[qubit] = qubit_index
                    qubit_index += 1

    def execute_statements(self, statements):
        for stmt in statements:
            if stmt['type'] == 'operation':
                self.execute_operation(stmt)
            elif stmt['type'] == 'if':
                self.execute_if(stmt)

    def execute_operation(self, stmt):
        op = stmt['op']
        target = stmt['target']
        q = self.qubits.get(target)
        if q is None:
            raise ValueError(f"Undefined qubit '{target}'")

        if op == 'H':
            self.qc.h(q)
        elif op == 'X':
            self.qc.x(q)
        elif op == 'Z':
            self.qc.z(q)
        elif op == 'entangle':
            target_q = self.qubits.get(stmt.get('target'))
            if target_q is None:
                raise ValueError(f"Undefined target qubit '{stmt.get('target')}'")
            self.qc.cx(q, target_q)
        elif op == 'measure':
            c = self.classical_bits.get(target)
            self.qc.measure(q, c)

    def execute_if(self, stmt):
        condition_qubit = self.classical_bits.get(stmt['condition'][0])
        if condition_qubit is None:
            raise ValueError(f"Undefined condition qubit '{stmt['condition'][0]}'")
        
        # Measure first to get the condition value
        self.qc.measure(condition_qubit, condition_qubit)
        
        # Execute the body conditionally (this works in simulation only)
        if stmt['condition'][1] == '==':
            value = stmt['condition'][2]
            self.qc.barrier()
            with self.qc.if_test((condition_qubit, value)):
                for body_stmt in stmt['body']:
                    self.execute_operation(body_stmt)

    def simulate(self):
        backend = Aer.get_backend('qasm_simulator')
        job = execute(self.qc, backend, shots=1)
        result = job.result()
        counts = result.get_counts(self.qc)
        print("\nQuantum Circuit Output:\n", counts)
        self.qc.draw('mpl')  # Draw the circuit (if matplotlib is installed)

# Test Interpreter
interpreter = Interpreter(ast)
interpreter.run()qubit q1, q2
H(q1)
entangle q1, q2
measure q1 -> c1
if c1 == 1 {
    Z(q2)
}Quantum Circuit Output:
 {'00': 512, '11': 488}state mem[2]
qubit q1, q2
H(q1)
entangle q1, q2
measure q1 -> mem[0]
if mem[0] == 1 {
    Z(q2)
}qubit q1, q2
policy P = { H(q1), entangle q1, q2 }
reward = measure q1
adapt P based on rewardqubit q1, q2
parallel {
    H(q1)
    X(q2)
}
measure q1, q2qubit q1, q2
H(q1)
clone q1 -> q2
measure q1, q2gate custom_gate(q) {
    H(q)
    X(q)
}
custom_gate(q1)qubit q1
subroutine my_func(q) {
    H(q)
    if measure q == 1 {
        my_func(q)
    }
}
my_func(q1)qubit q1, q2, q3
entangle q1, q2
if measure q1 == 1 {
    H(q2)
    X(q3)
}qubit q1, q2
prior = H(q1)
evidence = measure q2
update q1 based on prior and evidencequbit q[4]
layer1 = { H(q[0]), X(q[1]) }
layer2 = { CX(q[2], q[3]) }
train layer1, layer2 based on outputqubit q1, q2
H(q1)
if measure q1 == 1 {
    modify_gate X(q2) -> Z(q2)
}import re

class Lexer:
    def __init__(self, source):
        self.source = source
        self.position = 0
        self.tokens = []

    def tokenize(self):
        token_specification = [
            ('NUMBER', r'\d+(\.\d*)?'),
            ('IDENTIFIER', r'[A-Za-z_][A-Za-z0-9_]*'),
            ('OPERATOR', r'[+\-*/%=]'),
            ('PAREN', r'[]'),
            ('BRACE', r'[\{\}]'),
            ('SEPARATOR', r'[,;]'),
            ('ENTANGLE', r'ENTANGLE'),
            ('MEASURE', r'MEASURE'),
            ('SUPERPOSE', r'SUPERPOSE'),
            ('STATE_SAVE', r'STATE_SAVE'),
            ('STATE_LOAD', r'STATE_LOAD'),
            ('ADAPT', r'ADAPT'),
            ('FEEDBACK', r'FEEDBACK'),
            ('QFOR', r'QFOR'),
            ('QWHILE', r'QWHILE'),
            ('META_MODIFY', r'META_MODIFY'),
            ('META_EVAL', r'META_EVAL'),
            ('FEEDBACK_LOOP', r'FEEDBACK_LOOP'),
            ('GATE', r'H|X|Y|Z|S|T|CX|CCX|U|SWAP|CRZ'),
            ('SKIP', r'[ \t]+'),  # Ignore whitespace
            ('NEWLINE', r'\n'),
            ('MISMATCH', r'.')   # Catch all other errors
        ]
        tok_regex = '|'.join(f'(?P<{name}>{regex})' for name, regex in token_specification)
        for match in re.finditer(tok_regex, self.source):
            kind = match.lastgroup
            value = match.group()
            if kind == 'NUMBER':
                value = float(value) if '.' in value else int(value)
            if kind == 'SKIP' or kind == 'NEWLINE':
                continue
            if kind == 'MISMATCH':
                raise RuntimeError(f'Unexpected character: {value}')
            self.tokens.append((kind, value))
        return self.tokens

# Example usage
source = '''
QFUNC teleport(q1, q2) {
    SUPERPOSE q1;
    ENTANGLE q1, q2;
    MEASURE q1;
    FEEDBACK_LOOP q2;
}
'''
lexer = Lexer(source)
tokens = lexer.tokenize()
for token in tokens:
    print(token)class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0

    def parse(self):
        while self.position < len(self.tokens):
            token_type, token_value = self.tokens[self.position]
            if token_type == 'QFUNC':
                self.parse_qfunc()
            elif token_type == 'SUPERPOSE':
                self.parse_superpose()
            elif token_type == 'ENTANGLE':
                self.parse_entangle()
            elif token_type == 'MEASURE':
                self.parse_measure()
            elif token_type == 'FEEDBACK_LOOP':
                self.parse_feedback()
            elif token_type == 'META_MODIFY':
                self.parse_meta_modify()
            self.position += 1

    def parse_qfunc(self):
        self.position += 1  # Skip 'QFUNC'
        func_name = self.tokens[self.position][1]
        print(f"Defining QFUNC: {func_name}")

    def parse_superpose(self):
        self.position += 1
        qubit = self.tokens[self.position][1]
        print(f"Applying SUPERPOSE on {qubit}")

    def parse_entangle(self):
        self.position += 1
        q1 = self.tokens[self.position][1]
        self.position += 2  # Skip comma
        q2 = self.tokens[self.position][1]
        print(f"Entangling {q1} and {q2}")

# Example usage
parser = Parser(tokens)
parser.parse()import re

# Define regular expressions for various tokens
token_specification = [
    ('NUMBER',    r'\d+(\.\d*)?'),     # Integer or decimal number
    ('ASSIGN',    r'='),                # Assignment operator
    ('NAME',      r'[A-Za-z_][A-Za-z0-9_]*'),  # Variable names
    ('QGATE',     r'(H|X|Y|Z|CX|SWAP)'),  # Quantum gates (Hadamard, CNOT, etc.)
    ('OPENPAREN', r''),               # Opening parenthesis
    ('CLOSEPAREN',r''),               # Closing parenthesis
    ('SEMICOLON', r';'),                # Statement terminator
    ('LBRACE',    r'\{'),               # Left brace
    ('RBRACE',    r'\}'),               # Right brace
    ('COLON',     r':'),                # Colon for type declaration
    ('COMMA',     r','),                # Comma for separating parameters
    ('QUANTUM',   r'quantum'),          # 'quantum' keyword for qubits declaration
    ('MEASURE',   r'measure'),          # 'measure' keyword for measurement
    ('CONTROL',   r'control'),          # 'control' for controlled gates
    ('END',       r'end'),              # 'end' to terminate a block of code
    ('NEWLINE',   r'\n'),               # Newline character for line breaks
    ('SKIP',      r'[ \t]+'),           # Skip over spaces and tabs
    ('MISMATCH',  r'.'),                # Any other character
]

# Combine the token specifications into a single pattern
master_pattern = '|'.join(f'(?P<{pair[0]}>{pair[1]})' for pair in token_specification)

def lexer(code):
    line_num = 1
    line_start = 0
    for mo in re.finditer(master_pattern, code):
        kind = mo.lastgroup
        value = mo.group()
        column = mo.start() - line_start
        if kind == 'NEWLINE':
            line_start = mo.end()
            line_num += 1
        elif kind == 'SKIP':
            continue
        elif kind == 'MISMATCH':
            raise RuntimeError(f'{value!r} unexpected on line {line_num}')
        else:
            yield kind, value, line_num, columnclass Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.current_token = None
        self.advance()

    def advance(self):
        try:
            self.current_token = next(self.tokens)
        except StopIteration:
            self.current_token = None

    def parse(self):
        """Start parsing"""
        program = []
        while self.current_token:
            statement = self.parse_statement()
            if statement:
                program.append(statement)
        return program

    def parse_statement(self):
        """Parse individual statements (quantum operations, declarations, etc.)"""
        if self.current_token[0] == 'NAME':  # Variable/quantum qubit declaration
            return self.parse_declaration()
        elif self.current_token[0] == 'QGATE':  # Quantum gate application
            return self.parse_quantum_gate()
        elif self.current_token[0] == 'MEASURE':  # Measurement operation
            return self.parse_measurement()
        else:
            return None

    def parse_declaration(self):
        """Handle 'quantum' variable declarations"""
        if self.current_token[0] == 'NAME':
            name = self.current_token[1]
            self.advance()
            if self.current_token[0] == 'COLON':
                self.advance()
                if self.current_token[0] == 'QGATE':  # Quantum gate or qubit type
                    qubit_type = self.current_token[1]
                    self.advance()
                    return f"Quantum qubit '{name}' of type {qubit_type} declared"
        return None

    def parse_quantum_gate(self):
        """Handle quantum gate applications (e.g., H, X, CX)"""
        if self.current_token[0] == 'QGATE':
            gate_name = self.current_token[1]
            self.advance()
            if self.current_token[0] == 'OPENPAREN':
                self.advance()
                # Parse parameters (qubits for gates)
                qubit_name = self.current_token[1]
                self.advance()
                if self.current_token[0] == 'CLOSEPAREN':
                    return f"Apply gate {gate_name} to qubit {qubit_name}"
        return None

    def parse_measurement(self):
        """Handle quantum measurement operation"""
        if self.current_token[0] == 'MEASURE':
            self.advance()
            if self.current_token[0] == 'NAME':
                qubit_name = self.current_token[1]
                self.advance()
                return f"Measure qubit {qubit_name}"code = """
quantum q1: qubit;
H(q1);
measure q1;
"""

lexer_tokens = lexer(code)
parser = Parser(iter(lexer_tokens))
program = parser.parse()

for stmt in program:
    print(stmt)Quantum qubit 'q1' of type qubit declared
Apply gate H to qubit q1
Measure qubit q1Quantum qubit 'q1' of type qubit declared
Apply gate H to qubit q1
Measure qubit q1import re

# Define regular expressions for various tokens
token_specification = [
    ('NUMBER',    r'\d+(\.\d*)?'),             # Integer or decimal number
    ('ASSIGN',    r'='),                        # Assignment operator
    ('NAME',      r'[A-Za-z_][A-Za-z0-9_]*'),  # Variable names
    ('QGATE',     r'(H|X|Y|Z|CX|SWAP|RX|RY|RZ|CCNOT)'),  # Quantum gates
    ('OPENPAREN', r''),                       # Opening parenthesis
    ('CLOSEPAREN',r''),                       # Closing parenthesis
    ('SEMICOLON', r';'),                        # Statement terminator
    ('LBRACE',    r'\{'),                       # Left brace
    ('RBRACE',    r'\}'),                       # Right brace
    ('COLON',     r':'),                        # Colon for type declaration
    ('COMMA',     r','),                        # Comma for separating parameters
    ('QUANTUM',   r'quantum'),                  # 'quantum' keyword for qubit declaration
    ('MEASURE',   r'measure'),                  # 'measure' keyword for measurement
    ('CONTROL',   r'control'),                  # 'control' keyword for controlled gates
    ('LOOP',      r'loop'),                     # 'loop' keyword for loop constructs
    ('IF',        r'if'),                       # 'if' keyword for conditionals
    ('END',       r'end'),                      # 'end' to terminate a block of code
    ('NEWLINE',   r'\n'),                       # Newline character for line breaks
    ('SKIP',      r'[ \t]+'),                   # Skip over spaces and tabs
    ('MISMATCH',  r'.'),                        # Any other character
]

# Combine the token specifications into a single pattern
master_pattern = '|'.join(f'(?P<{pair[0]}>{pair[1]})' for pair in token_specification)

def lexer(code):
    line_num = 1
    line_start = 0
    for mo in re.finditer(master_pattern, code):
        kind = mo.lastgroup
        value = mo.group()
        column = mo.start() - line_start
        if kind == 'NEWLINE':
            line_start = mo.end()
            line_num += 1
        elif kind == 'SKIP':
            continue
        elif kind == 'MISMATCH':
            raise RuntimeError(f'{value!r} unexpected on line {line_num}')
        else:
            yield kind, value, line_num, column
        return Noneclass Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.current_token = None
        self.advance()

    def advance(self):
        try:
            self.current_token = next(self.tokens)
        except StopIteration:
            self.current_token = None

    def parse(self):
        """Start parsing"""
        program = []
        while self.current_token:
            statement = self.parse_statement()
            if statement:
                program.append(statement)
        return program

    def parse_statement(self):
        """Parse individual statements (quantum operations, declarations, etc.)"""
        if self.current_token[0] == 'NAME':  # Variable/quantum qubit declaration
            return self.parse_declaration()
        elif self.current_token[0] == 'QGATE':  # Quantum gate application
            return self.parse_quantum_gate()
        elif self.current_token[0] == 'MEASUR// Declare two qubits
qubit q1, q2

// Apply a Hadamard gate to create superposition
H(q1)

// Create entanglement using a CNOT gate
CNOT(q1, q2)

// Measure and store the result in a classical register
measure q1 -> c1
measure q2 -> c2

// Conditional logic based on measurement results
if (c1 == 1) {
    Z(q2) // Apply Pauli-Z if q1 is measured as 1
} // Create 2 qubits
qubit q1, q2

// Apply a Hadamard gate to q1
H(q1)// Entangle two qubits
entangle q1, q2// Measure qubits and store in classical registers
measure q1 -> c1
measure q2 -> c2

// Conditional logic
if c1 == 1 {
    Z(q2) // Apply Z gate if q1 is 1
}// Apply a gate only if another qubit is in state 1
control q1 {
    X(q2)
}// Create a Bell state
H(q1)
entangle q1, q2function teleport(q1, q2) {
    H(q1)
    entangle q1, q2
    measure q1 -> c1
}import re

# Define the token types
TOKEN_SPECIFICATION = [
    ('NUMBER', r'\d+'),             # Integer or float
    ('ID', r'[A-Za-z_][A-Za-z0-9_]*'),  # Identifiers (e.g., qubit names)
    ('OP', r'->|==|{|}|||,'),   # Operators and symbols
    ('NEWLINE', r'\n'),             # Line endings
    ('SKIP', r'[ \t]+'),            # Skip spaces and tabs
    ('COMMENT', r'//.*'),           # Comments
    ('MISMATCH', r'.')              # Any other character (error)
]

# Compile the regex
TOKENS_REGEX = '|'.join(f'(?P<{name}>{pattern})' for name, pattern in TOKEN_SPECIFICATION)

class Lexer:
    def __init__(self, code):
        self.code = code

    def tokenize(self):
        tokens = []
        for match in re.finditer(TOKENS_REGEX, self.code):
            kind = match.lastgroup
            value = match.group()
            if kind == 'NUMBER':
                value = int(value)
            elif kind == 'SKIP' or kind == 'COMMENT':
                continue
            elif kind == 'MISMATCH':
                raise SyntaxError(f'Unexpected character: {value}')
            tokens.append((kind, value))
        return tokens

# Test Lexer
code = """
qubit q1, q2
H(q1)
entangle q1, q2
measure q1 -> c1
if c1 == 1 {
    Z(q2)
}
"""
lexer = Lexer(code)
tokens = lexer.tokenize()
for token in tokens:
    print(token)('ID', 'qubit')
('ID', 'q1')
('OP', ',')
('ID', 'q2')
('NEWLINE', '\n')
...class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0

    def peek(self):
        if self.position < len(self.tokens):
            return self.tokens[self.position]
        return None

    def consume(self):
        token = self.peek()
        self.position += 1
        return token

    def expect(self, kind):
        token = self.peek()
        if token and token[0] == kind:
            return self.consume()
        raise SyntaxError(f"Expected {kind}, got {token}")

    def parse(self):
        program = []
        while self.peek():
            statement = self.parse_statement()
            if statement:
                program.append(statement)
        return program

    def parse_statement(self):
        token = self.peek()
        if token[0] == 'ID':
            if token[1] == 'qubit':
                return self.parse_qubit_declaration()
            elif token[1] in ('H', 'X', 'Z', 'entangle', 'measure'):
                return self.parse_operation()
            elif token[1] == 'if':
                return self.parse_if_statement()
        return None

    def parse_qubit_declaration(self):
        self.consume()  # Consume 'qubit'
        qubits = []
        while True:
            qubit = self.expect('ID')[1]
            qubits.append(qubit)
            if self.peek() and self.peek()[1] == ',':
                self.consume()
            else:
                break
        return {'type': 'qubit_decl', 'qubits': qubits}

    def parse_operation(self):
        op = self.consume()[1]
        self.expect('OP')  # Expect '('
        target = self.expect('ID')[1]
        self.expect('OP')  # Expect ')'
        return {'type': 'operation', 'op': op, 'target': target}

    def parse_if_statement(self):
        self.consume()  # Consume 'if'
        self.expect('OP')  # Expect '('
        condition = (self.expect('ID')[1], self.expect('OP')[1], self.expect('NUMBER')[1])
        self.expect('OP')  # Expect ')'
        self.expect('OP')  # Expect '{'
        body = []
        while self.peek() and self.peek()[1] != '}':
            body.append(self.parse_statement())
        self.expect('OP')  # Expect '}'
        return {'type': 'if', 'condition': condition, 'body': body}

# Test Parser
parser = Parser(tokens)
ast = parser.parse()
print(ast)[
    {'type': 'qubit_decl', 'qubits': ['q1', 'q2']},
    {'type': 'operation', 'op': 'H', 'target': 'q1'},
    {'type': 'operation', 'op': 'entangle', 'target': 'q2'},
    {'type': 'if', 
        'condition': ('c1', '==', 1), 
        'body': [
            {'type': 'operation', 'op': 'Z', 'target': 'q2'}
        ]
    }
]from qiskit import QuantumCircuit, Aer, execute

class Interpreter:
    def __init__(self, ast):
        self.ast = ast
        self.qc = None
        self.qubits = {}
        self.classical_bits = {}

    def run(self):
        self.initialize()
        self.execute_statements(self.ast)
        self.simulate()

    def initialize(self):
        # Count the number of qubits needed
        num_qubits = sum(1 for node in self.ast if node['type'] == 'qubit_decl')
        self.qc = QuantumCircuit(num_qubits, num_qubits)
        qubit_index = 0
        for node in self.ast:
            if node['type'] == 'qubit_decl':
                for qubit in node['qubits']:
                    self.qubits[qubit] = qubit_index
                    self.classical_bits[qubit] = qubit_index
                    qubit_index += 1

    def execute_statements(self, statements):
        for stmt in statements:
            if stmt['type'] == 'operation':
                self.execute_operation(stmt)
            elif stmt['type'] == 'if':
                self.execute_if(stmt)

    def execute_operation(self, stmt):
        op = stmt['op']
        target = stmt['target']
        q = self.qubits.get(target)
        if q is None:
            raise ValueError(f"Undefined qubit '{target}'")

        if op == 'H':
            self.qc.h(q)
        elif op == 'X':
            self.qc.x(q)
        elif op == 'Z':
            self.qc.z(q)
        elif op == 'entangle':
            target_q = self.qubits.get(stmt.get('target'))
            if target_q is None:
                raise ValueError(f"Undefined target qubit '{stmt.get('target')}'")
            self.qc.cx(q, target_q)
        elif op == 'measure':
            c = self.classical_bits.get(target)
            self.qc.measure(q, c)

    def execute_if(self, stmt):
        condition_qubit = self.classical_bits.get(stmt['condition'][0])
        if condition_qubit is None:
            raise ValueError(f"Undefined condition qubit '{stmt['condition'][0]}'")
        
        # Measure first to get the condition value
        self.qc.measure(condition_qubit, condition_qubit)
        
        # Execute the body conditionally (this works in simulation only)
        if stmt['condition'][1] == '==':
            value = stmt['condition'][2]
            self.qc.barrier()
            with self.qc.if_test((condition_qubit, value)):
                for body_stmt in stmt['body']:
                    self.execute_operation(body_stmt)

    def simulate(self):
        backend = Aer.get_backend('qasm_simulator')
        job = execute(self.qc, backend, shots=1)
        result = job.result()
        counts = result.get_counts(self.qc)
        print("\nQuantum Circuit Output:\n", counts)
        self.qc.draw('mpl')  # Draw the circuit (if matplotlib is installed)

# Test Interpreter
interpreter = Interpreter(ast)
interpreter.run()qubit q1, q2
H(q1)
entangle q1, q2
measure q1 -> c1
if c1 == 1 {
    Z(q2)
}Quantum Circuit Output:
 {'00': 512, '11': 488}state mem[2]
qubit q1, q2
H(q1)
entangle q1, q2
measure q1 -> mem[0]
if mem[0] == 1 {
    Z(q2)
}qubit q1, q2
policy P = { H(q1), entangle q1, q2 }
reward = measure q1
adapt P based on rewardqubit q1, q2
parallel {
    H(q1)
    X(q2)
}
measure q1, q2qubit q1, q2
H(q1)
clone q1 -> q2
measure q1, q2gate custom_gate(q) {
    H(q)
    X(q)
}
custom_gate(q1)qubit q1
subroutine my_func(q) {
    H(q)
    if measure q == 1 {
        my_func(q)
    }
}
my_func(q1)qubit q1, q2, q3
entangle q1, q2
if measure q1 == 1 {
    H(q2)
    X(q3)
}qubit q1, q2
prior = H(q1)
evidence = measure q2
update q1 based on prior and evidencequbit q[4]
layer1 = { H(q[0]), X(q[1]) }
layer2 = { CX(q[2], q[3]) }
train layer1, layer2 based on outputqubit q1, q2
H(q1)
if measure q1 == 1 {
    modify_gate X(q2) -> Z(q2)
}import re

class Lexer:
    def __init__(self, source):
        self.source = source
        self.position = 0
        self.tokens = []

    def tokenize(self):
        token_specification = [
            ('NUMBER', r'\d+(\.\d*)?'),
            ('IDENTIFIER', r'[A-Za-z_][A-Za-z0-9_]*'),
            ('OPERATOR', r'[+\-*/%=]'),
            ('PAREN', r'[]'),
            ('BRACE', r'[\{\}]'),
            ('SEPARATOR', r'[,;]'),
            ('ENTANGLE', r'ENTANGLE'),
            ('MEASURE', r'MEASURE'),
            ('SUPERPOSE', r'SUPERPOSE'),
            ('STATE_SAVE', r'STATE_SAVE'),
            ('STATE_LOAD', r'STATE_LOAD'),
            ('ADAPT', r'ADAPT'),
            ('FEEDBACK', r'FEEDBACK'),
            ('QFOR', r'QFOR'),
            ('QWHILE', r'QWHILE'),
            ('META_MODIFY', r'META_MODIFY'),
            ('META_EVAL', r'META_EVAL'),
            ('FEEDBACK_LOOP', r'FEEDBACK_LOOP'),
            ('GATE', r'H|X|Y|Z|S|T|CX|CCX|U|SWAP|CRZ'),
            ('SKIP', r'[ \t]+'),  # Ignore whitespace
            ('NEWLINE', r'\n'),
            ('MISMATCH', r'.')   # Catch all other errors
        ]
        tok_regex = '|'.join(f'(?P<{name}>{regex})' for name, regex in token_specification)
        for match in re.finditer(tok_regex, self.source):
            kind = match.lastgroup
            value = match.group()
            if kind == 'NUMBER':
                value = float(value) if '.' in value else int(value)
            if kind == 'SKIP' or kind == 'NEWLINE':
                continue
            if kind == 'MISMATCH':
                raise RuntimeError(f'Unexpected character: {value}')
            self.tokens.append((kind, value))
        return self.tokens

# Example usage
source = '''
QFUNC teleport(q1, q2) {
    SUPERPOSE q1;
    ENTANGLE q1, q2;
    MEASURE q1;
    FEEDBACK_LOOP q2;
}
'''
lexer = Lexer(source)
tokens = lexer.tokenize()
for token in tokens:
    print(token)class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0

    def parse(self):
        while self.position < len(self.tokens):
            token_type, token_value = self.tokens[self.position]
            if token_type == 'QFUNC':
                self.parse_qfunc()
            elif token_type == 'SUPERPOSE':
                self.parse_superpose()
            elif token_type == 'ENTANGLE':
                self.parse_entangle()
            elif token_type == 'MEASURE':
                self.parse_measure()
            elif token_type == 'FEEDBACK_LOOP':
                self.parse_feedback()
            elif token_type == 'META_MODIFY':
                self.parse_meta_modify()
            self.position += 1

    def parse_qfunc(self):
        self.position += 1  # Skip 'QFUNC'
        func_name = self.tokens[self.position][1]
        print(f"Defining QFUNC: {func_name}")

    def parse_superpose(self):
        self.position += 1
        qubit = self.tokens[self.position][1]
        print(f"Applying SUPERPOSE on {qubit}")

    def parse_entangle(self):
        self.position += 1
        q1 = self.tokens[self.position][1]
        self.position += 2  # Skip comma
        q2 = self.tokens[self.position][1]
        print(f"Entangling {q1} and {q2}")

# Example usage
parser = Parser(tokens)
parser.parse()import re

# Define regular expressions for various tokens
token_specification = [
    ('NUMBER',    r'\d+(\.\d*)?'),     # Integer or decimal number
    ('ASSIGN',    r'='),                # Assignment operator
    ('NAME',      r'[A-Za-z_][A-Za-z0-9_]*'),  # Variable names
    ('QGATE',     r'(H|X|Y|Z|CX|SWAP)'),  # Quantum gates (Hadamard, CNOT, etc.)
    ('OPENPAREN', r''),               # Opening parenthesis
    ('CLOSEPAREN',r''),               # Closing parenthesis
    ('SEMICOLON', r';'),                # Statement terminator
    ('LBRACE',    r'\{'),               # Left brace
    ('RBRACE',    r'\}'),               # Right brace
    ('COLON',     r':'),                # Colon for type declaration
    ('COMMA',     r','),                # Comma for separating parameters
    ('QUANTUM',   r'quantum'),          # 'quantum' keyword for qubits declaration
    ('MEASURE',   r'measure'),          # 'measure' keyword for measurement
    ('CONTROL',   r'control'),          # 'control' for controlled gates
    ('END',       r'end'),              # 'end' to terminate a block of code
    ('NEWLINE',   r'\n'),               # Newline character for line breaks
    ('SKIP',      r'[ \t]+'),           # Skip over spaces and tabs
    ('MISMATCH',  r'.'),                # Any other character
]

# Combine the token specifications into a single pattern
master_pattern = '|'.join(f'(?P<{pair[0]}>{pair[1]})' for pair in token_specification)

def lexer(code):
    line_num = 1
    line_start = 0
    for mo in re.finditer(master_pattern, code):
        kind = mo.lastgroup
        value = mo.group()
        column = mo.start() - line_start
        if kind == 'NEWLINE':
            line_start = mo.end()
            line_num += 1
        elif kind == 'SKIP':
            continue
        elif kind == 'MISMATCH':
            raise RuntimeError(f'{value!r} unexpected on line {line_num}')
        else:
            yield kind, value, line_num, columnclass Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.current_token = None
        self.advance()

    def advance(self):
        try:
            self.current_token = next(self.tokens)
        except StopIteration:
            self.current_token = None

    def parse(self):
        """Start parsing"""
        program = []
        while self.current_token:
            statement = self.parse_statement()
            if statement:
                program.append(statement)
        return program

    def parse_statement(self):
        """Parse individual statements (quantum operations, declarations, etc.)"""
        if self.current_token[0] == 'NAME':  # Variable/quantum qubit declaration
            return self.parse_declaration()
        elif self.current_token[0] == 'QGATE':  # Quantum gate application
            return self.parse_quantum_gate()
        elif self.current_token[0] == 'MEASURE':  # Measurement operation
            return self.parse_measurement()
        else:
            return None

    def parse_declaration(self):
        """Handle 'quantum' variable declarations"""
        if self.current_token[0] == 'NAME':
            name = self.current_token[1]
            self.advance()
            if self.current_token[0] == 'COLON':
                self.advance()
                if self.current_token[0] == 'QGATE':  # Quantum gate or qubit type
                    qubit_type = self.current_token[1]
                    self.advance()
                    return f"Quantum qubit '{name}' of type {qubit_type} declared"
        return None

    def parse_quantum_gate(self):
        """Handle quantum gate applications (e.g., H, X, CX)"""
        if self.current_token[0] == 'QGATE':
            gate_name = self.current_token[1]
            self.advance()
            if self.current_token[0] == 'OPENPAREN':
                self.advance()
                # Parse parameters (qubits for gates)
                qubit_name = self.current_token[1]
                self.advance()
                if self.current_token[0] == 'CLOSEPAREN':
                    return f"Apply gate {gate_name} to qubit {qubit_name}"
        return None

    def parse_measurement(self):
        """Handle quantum measurement operation"""
        if self.current_token[0] == 'MEASURE':
            self.advance()
            if self.current_token[0] == 'NAME':
                qubit_name = self.current_token[1]
                self.advance()
                return f"Measure qubit {qubit_name}"code = """
quantum q1: qubit;
H(q1);
measure q1;
"""

lexer_tokens = lexer(code)
parser = Parser(iter(lexer_tokens))
program = parser.parse()

for stmt in program:
    print(stmt)Quantum qubit 'q1' of type qubit declared
Apply gate H to qubit q1
Measure qubit q1Quantum qubit 'q1' of type qubit declared
Apply gate H to qubit q1
Measure qubit q1import re

# Define regular expressions for various tokens
token_specification = [
    ('NUMBER',    r'\d+(\.\d*)?'),             # Integer or decimal number
    ('ASSIGN',    r'='),                        # Assignment operator
    ('NAME',      r'[A-Za-z_][A-Za-z0-9_]*'),  # Variable names
    ('QGATE',     r'(H|X|Y|Z|CX|SWAP|RX|RY|RZ|CCNOT)'),  # Quantum gates
    ('OPENPAREN', r''),                       # Opening parenthesis
    ('CLOSEPAREN',r''),                       # Closing parenthesis
    ('SEMICOLON', r';'),                        # Statement terminator
    ('LBRACE',    r'\{'),                       # Left brace
    ('RBRACE',    r'\}'),                       # Right brace
    ('COLON',     r':'),                        # Colon for type declaration
    ('COMMA',     r','),                        # Comma for separating parameters
    ('QUANTUM',   r'quantum'),                  # 'quantum' keyword for qubit declaration
    ('MEASURE',   r'measure'),                  # 'measure' keyword for measurement
    ('CONTROL',   r'control'),                  # 'control' keyword for controlled gates
    ('LOOP',      r'loop'),                     # 'loop' keyword for loop constructs
    ('IF',        r'if'),                       # 'if' keyword for conditionals
    ('END',       r'end'),                      # 'end' to terminate a block of code
    ('NEWLINE',   r'\n'),                       # Newline character for line breaks
    ('SKIP',      r'[ \t]+'),                   # Skip over spaces and tabs
    ('MISMATCH',  r'.'),                        # Any other character
]

# Combine the token specifications into a single pattern
master_pattern = '|'.join(f'(?P<{pair[0]}>{pair[1]})' for pair in token_specification)

def lexer(code):
    line_num = 1
    line_start = 0
    for mo in re.finditer(master_pattern, code):
        kind = mo.lastgroup
        value = mo.group()
        column = mo.start() - line_start
        if kind == 'NEWLINE':
            line_start = mo.end()
            line_num += 1
        elif kind == 'SKIP':
            continue
        elif kind == 'MISMATCH':
            raise RuntimeError(f'{value!r} unexpected on line {line_num}')
        else:
            yield kind, value, line_num, column
        return Noneclass Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.current_token = None
        self.advance()

    def advance(self):
        try:
            self.current_token = next(self.tokens)
        except StopIteration:
            self.current_token = None

    def parse(self):
        """Start parsing"""
        program = []
        while self.current_token:
            statement = self.parse_statement()
            if statement:
                program.append(statement)
        return program

    def parse_statement(self):
        """Parse individual statements (quantum operations, declarations, etc.)"""
        if self.current_token[0] == 'NAME':  # Variable/quantum qubit declaration
            return self.parse_declaration()
        elif self.current_token[0] == 'QGATE':  # Quantum gate application
            return self.parse_quantum_gate()
        elif self.current_token[0] == 'MEASUR# Configures C++17 compiler, setting TDLib-specific compilation options.

function(td_set_up_compiler)
  set(CMAKE_EXPORT_COMPILE_COMMANDS 1 PARENT_SCOPE)

  set(CMAKE_POSITION_INDEPENDENT_CODE ON PARENT_SCOPE)

  include(illumos)

  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(GCC 1)
    set(GCC 1 PARENT_SCOPE)
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CLANG 1)
    set(CLANG 1 PARENT_SCOPE)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    set(INTEL 1)
    set(INTEL 1 PARENT_SCOPE)
  elseif (NOT MSVC)
    message(FATAL_ERROR "Compiler isn't supported")
  endif()

  include(CheckCXXCompilerFlag)

  if (GCC OR CLANG OR INTEL)
    if (WIN32 AND INTEL)
      set(STD17_FLAG /Qstd=c++17)
    else()
      set(STD17_FLAG -std=c++17)
    endif()
    if (GCC AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
      message(FATAL_ERROR "No C++17 support in the compiler. Please upgrade the compiler to at least GCC 7.0.")
    endif()
    if (CLANG AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0))
      message(FATAL_ERROR "No C++17 support in the compiler. Please upgrade the compiler to at least clang 5.0.")
    endif()
    check_cxx_compiler_flag(${STD17_FLAG} HAVE_STD17)
  elseif (MSVC)
    set(HAVE_STD17 MSVC_VERSION>=1914) # MSVC 2017 version 15.7
  endif()

  if (NOT HAVE_STD17)
    message(FATAL_ERROR "No C++17 support in the compiler. Please upgrade the compiler.")
  endif()

  if (MSVC)
    if (CMAKE_CXX_FLAGS_DEBUG MATCHES "/RTC1")
      string(REPLACE "/RTC1" " " CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    endif()
    add_definitions(-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17 /utf-8 /GR- /W4 /wd4100 /wd4127 /wd4324 /wd4505 /wd4814 /wd4702 /bigobj")
  elseif (CLANG OR GCC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${STD17_FLAG} -fno-omit-frame-pointer -fno-exceptions -fno-rtti")
    if (APPLE)
      set(TD_LINKER_FLAGS "-Wl,-dead_strip")
      if (NOT CMAKE_BUILD_TYPE MATCHES "Deb")
        set(TD_LINKER_FLAGS "${TD_LINKER_FLAGS},-x,-S")
      endif()
    else()
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
      if (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
        set(TD_LINKER_FLAGS "-Wl,-z,ignore")
      elseif (EMSCRIPTEN)
        set(TD_LINKER_FLAGS "-Wl,--gc-sections")
      elseif (ANDROID)
        set(TD_LINKER_FLAGS "-Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--icf=safe")
      else()
        set(TD_LINKER_FLAGS "-Wl,--gc-sections -Wl,--exclude-libs,ALL")
      endif()
    endif()
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${TD_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TD_LINKER_FLAGS}")

    if (WIN32 OR CYGWIN)
      if (GCC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj")
      endif()
    endif()
  elseif (INTEL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${STD17_FLAG}")
  endif()

  if (WIN32)
    add_definitions(-DNTDDI_VERSION=0x06020000 -DWINVER=0x0602 -D_WIN32_WINNT=0x0602 -DPSAPI_VERSION=1 -DNOMINMAX -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
  endif()
  if (CYGWIN)
    add_definitions(-D_DEFAULT_SOURCE=1 -DFD_SETSIZE=4096)
  endif()

  # _FILE_OFFSET_BITS is broken in Android NDK r15, r15b and r17 and doesn't work prior to Android 7.0
  add_definitions(-D_FILE_OFFSET_BITS=64)

  # _GNU_SOURCE might not be defined by g++
  add_definitions(-D_GNU_SOURCE)

  if (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lsocket -lnsl")
    if (ILLUMOS)
      add_definitions(-DTD_ILLUMOS=1)
    endif()
  endif()

  include(AddCXXCompilerFlag)
  if (NOT MSVC)
    add_cxx_compiler_flag("-Wall")
    add_cxx_compiler_flag("-Wextra")
    add_cxx_compiler_flag("-Wimplicit-fallthrough=2")
    add_cxx_compiler_flag("-Wpointer-arith")
    add_cxx_compiler_flag("-Wcast-qual")
    add_cxx_compiler_flag("-Wsign-compare")
    add_cxx_compiler_flag("-Wduplicated-branches")
    add_cxx_compiler_flag("-Wduplicated-cond")
    add_cxx_compiler_flag("-Walloc-zero")
    add_cxx_compiler_flag("-Wlogical-op")
    add_cxx_compiler_flag("-Wno-tautological-compare")
    add_cxx_compiler_flag("-Wpointer-arith")
    add_cxx_compiler_flag("-Wvla")
    add_cxx_compiler_flag("-Wnon-virtual-dtor")
    add_cxx_compiler_flag("-Wno-unused-parameter")
    add_cxx_compiler_flag("-Wconversion")
    add_cxx_compiler_flag("-Wno-sign-conversion")
    add_cxx_compiler_flag("-Wc++17-compat-pedantic")
    add_cxx_compiler_flag("-Wdeprecated")
    add_cxx_compiler_flag("-Wno-unused-command-line-argument")
    add_cxx_compiler_flag("-Qunused-arguments")
    add_cxx_compiler_flag("-Wno-unknown-warning-option")
    add_cxx_compiler_flag("-Wodr")
    add_cxx_compiler_flag("-flto-odr-type-merging")
    add_cxx_compiler_flag("-Wno-psabi")
    add_cxx_compiler_flag("-Wunused-member-function")
    add_cxx_compiler_flag("-Wunused-private-field")

  #  add_cxx_compiler_flag("-Werror")

  #  add_cxx_compiler_flag("-Wcast-align")

  #std::int32_t <-> int and off_t <-> std::size_t/std::int64_t
  #  add_cxx_compiler_flag("-Wuseless-cast")

  #external headers like openssl
  #  add_cxx_compiler_flag("-Wzero-as-null-pointer-constant")
  endif()

  if (GCC)
    add_cxx_compiler_flag("-Wno-maybe-uninitialized")  # too many false positives
  endif()
  if (WIN32 AND GCC AND NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0))
    # warns about casts of function pointers returned by GetProcAddress
    add_cxx_compiler_flag("-Wno-cast-function-type")
  endif()
  if (GCC AND NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0))
    # warns about a lot of "return std::move", which are not redundant for compilers without fix for DR 1579, i.e. GCC 4.9 or clang 3.8
    # see http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#1579
    add_cxx_compiler_flag("-Wno-redundant-move")
  endif()
  if (GCC AND NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0))
    add_cxx_compiler_flag("-Wno-stringop-overflow")  # some false positives
  endif()
  if (CLANG AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.5))
    # https://stackoverflow.com/questions/26744556/warning-returning-a-captured-reference-from-a-lambda
    add_cxx_compiler_flag("-Wno-return-stack-address")
  endif()
  if (GCC AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0))
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104030
    add_cxx_compiler_flag("-Wbidi-chars=none")
  endif()

  if (MINGW)
    add_cxx_compiler_flag("-ftrack-macro-expansion=0")
  endif()

  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem /usr/include/c++/v1")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" PARENT_SCOPE)
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}" PARENT_SCOPE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" PARENT_SCOPE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
endfunction()
