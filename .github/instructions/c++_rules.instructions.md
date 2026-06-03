---
name: cpp-coding-standards
description: "C++ coding standards based on the C++ Core Guidelines (isocpp.github.io). Use when writing, reviewing, or refactoring C++ code to enforce modern, safe, and idiomatic practices."
origin: ECC
applyTo: **/*.cpp, **/*.h, **/*.hpp, **/*.cxx, **/*.hxx, CMakeLists.txt
---

# C++ Coding Standards

Comprehensive coding standards for modern C++ (C++17/20/23) derived from the C++ Core Guidelines. Optimize for type safety, resource safety, explicit ownership, and clear intent.

Prefer repository-local conventions when they are stricter or more specific than a generic guideline. Match the surrounding subsystem's naming, error-handling, logging, and boundary types unless the task explicitly asks for a broader refactor.

## When to Use

- Writing new C++ code, templates, classes, tests, or CMake that shapes C++ targets
- Reviewing or refactoring existing C++ code
- Making architectural decisions in C++ projects
- Enforcing consistent style and ownership semantics across a C++ codebase
- Choosing between language features such as `enum` versus `enum class`, raw pointer versus smart pointer, or reference versus value semantics

## When Not to Use

- Non-C++ projects
- C-only code that cannot adopt modern C++ features
- Constrained embedded or platform surfaces where a specific rule conflicts with hardware, ABI, or toolchain requirements; adapt selectively and document the tradeoff

## Repository Adaptation Rules

- Preserve existing subsystem conventions when editing in place. Do not rename unrelated symbols or replace stable local abstractions just to satisfy a generic style preference.
- Prefer established repository boundary types such as local slice/span/status/result abstractions when those are the canonical interface for the touched subsystem.
- Do not introduce exception-based control flow into exception-free surfaces or targets compiled with `-fno-exceptions`; use the existing explicit status or result pattern there.
- Prefer existing logging and diagnostics facilities over ad hoc `printf` or `std::cout` output.
- In `CMakeLists.txt`, prefer target-scoped properties, compile features, include directories, definitions, and options over global flags.

## Cross-Cutting Principles

1. RAII everywhere (P.8, R.1, E.6, CP.20): bind resource lifetime to object lifetime.
2. Immutability by default (P.10, Con.1-5, ES.25): start with `const` and `constexpr`; mutability is the exception.
3. Type safety (P.4, I.4, ES.46-49, Enum.3): use the type system to make misuse hard.
4. Express intent (P.3, F.1, NL.1-2, T.10): names, types, and contracts should communicate purpose.
5. Minimize complexity (F.2-3, ES.5, Per.4-5): prefer simple, local, testable code.
6. Value semantics over pointer semantics (C.10, R.3-5, F.20, CP.31): prefer scoped objects and return values.

## Philosophy and Interfaces (P.*, I.*)

### Key Rules

- P.1: Express ideas directly in code.
- P.3: Express intent.
- P.4: Prefer static type safety.
- P.5: Prefer compile-time checking to run-time checking.
- P.8: Do not leak resources.
- P.10: Prefer immutable data to mutable data.
- I.1: Make interfaces explicit.
- I.2: Avoid non-const global variables.
- I.4: Make interfaces precisely and strongly typed.
- I.11: Never transfer ownership by raw pointer or reference.
- I.23: Keep function argument counts low.

### Preferred Pattern

```cpp
struct Temperature {
    double kelvin;
};

Temperature boil(const Temperature &water);
```

### Avoid

```cpp
double boil(double *temp);

int g_counter = 0;
```

## Functions (F.*)

### Key Rules

- F.1: Package meaningful operations as carefully named functions.
- F.2: A function should perform a single logical operation.
- F.3: Keep functions short and locally understandable.
- F.4: If a function may be evaluated at compile time, consider `constexpr`.
- F.6: Mark functions `noexcept` when throwing is impossible or unacceptable.
- F.8: Prefer pure functions where practical.
- F.16: For input parameters, pass cheap types by value and other types by `const&`.
- F.20: Prefer return values to output parameters.
- F.21: Return a struct for multiple outputs.
- F.24: Use `std::span` or the repository's established span type for contiguous views.
- F.43: Never return a pointer or reference to a local object.

### Parameter and Return Guidance

```cpp
void print(int x);
void analyze(const std::string &data);
void transform(std::string data);

struct ParseResult {
    std::string token;
    int position;
};

ParseResult parse(std::string_view input);
```

### Avoid

- Returning `T&&` from functions.
- C-style variadics such as `va_arg`.
- Reference captures in lambdas that outlive the current thread or scope.
- Returning `const T` by value.
- Output parameters that exist only to smuggle results back to the caller.

## Classes and Hierarchies (C.*)

### Key Rules

- C.2: Use `class` when an invariant matters; use `struct` for passive aggregates.
- C.9: Minimize exposure of members.
- C.20: Prefer the Rule of Zero.
- C.21: If you define or `=delete` one copy, move, or destruction operation, address the full set.
- C.35: Base class destructors should be public virtual or protected non-virtual.
- C.41: Constructors should produce fully initialized objects.
- C.45: Prefer in-class member initializers.
- C.46: Make single-argument constructors `explicit`.
- C.67: Polymorphic classes should suppress public copy and move.
- C.128: Use exactly one of `virtual`, `override`, or `final`.

### Preferred Pattern

```cpp
class Shape {
 public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
};

class Circle final : public Shape {
 public:
    explicit Circle(double radius) : radius_(radius) {
    }

    double area() const override {
        return 3.14159 * radius_ * radius_;
    }

 private:
    double radius_{};
};
```

### Avoid

- Calling virtual functions from constructors or destructors.
- Using `memset` or `memcpy` on non-trivial types.
- Different default arguments on a virtual function and its overrider.
- Unnecessary `const` or reference data members that suppress sane move semantics.

## Resource Management (R.*)

### Key Rules

- R.1: Manage resources automatically using RAII.
- R.3: Treat raw pointers as non-owning.
- R.5: Prefer scoped objects; avoid heap allocation unless needed.
- R.10: Avoid `malloc()` and `free()`.
- R.11: Avoid explicit `new` and `delete`.
- R.13: Perform at most one explicit resource allocation in a single expression.
- R.20: Use `unique_ptr` or `shared_ptr` to represent ownership.
- R.21: Prefer `unique_ptr` over `shared_ptr` unless sharing is required.
- R.22: Use `make_shared()` to create `shared_ptr`s.
- R.30: Pass smart pointers only when the parameter itself expresses ownership semantics.

### Preferred Pattern

```cpp
auto widget = std::make_unique<Widget>("config");

void render(const Widget *widget_ptr) {
    if (widget_ptr != nullptr) {
        widget_ptr->draw();
    }
}
```

### Avoid

- Naked `new` or `delete`.
- `malloc()` or `free()` in C++ code.
- Passing `shared_ptr` by value when no ownership change is intended.
- Using raw owning pointers instead of explicit ownership wrappers.

## Expressions and Statements (ES.*)

### Key Rules

- ES.5: Keep scopes small.
- ES.20: Always initialize objects.
- ES.23: Prefer `{}` initialization.
- ES.25: Declare objects `const` or `constexpr` unless mutation is required.
- ES.28: Use lambdas for complex `const` initialization.
- ES.45: Avoid magic constants.
- ES.46: Avoid narrowing and lossy conversions.
- ES.47: Use `nullptr`, not `0` or `NULL`.
- ES.48: Avoid casts.
- ES.50: Do not cast away `const`.
- ES.100: Do not mix signed and unsigned arithmetic casually.

### Preferred Pattern

```cpp
const int max_retries{3};
const std::vector<int> primes{2, 3, 5, 7, 11};

const auto config = [&] {
    Config result;
    result.timeout = std::chrono::seconds{30};
    result.retries = max_retries;
    return result;
}();
```

### Avoid

- Uninitialized variables.
- C-style casts.
- Hidden narrowing conversions.
- Reusing names in nested scopes.

## Error Handling (E.*)

### Key Rules

- E.1: Decide on the error-handling strategy early.
- E.2: Use exceptions only where the subsystem and build configuration permit them.
- E.3: Do not use exceptions for normal control flow.
- E.6: Use RAII to prevent leaks on failure paths.
- E.12: Use `noexcept` when failure is impossible or unacceptable.
- E.14: Prefer purpose-designed exception types when exceptions are enabled.
- E.15: Throw by value and catch by reference.
- E.16: Destructors, deallocation, move operations, and `swap` must not fail.
- E.17: Do not catch every exception in every layer.
- E.28: Do not build error handling around ambient global state like `errno` when stronger alternatives exist.

### Repository Rule

For recoverable errors, prefer the error model already established by the touched subsystem. In exception-free code, use existing status, result, or boolean-plus-context patterns rather than introducing try/catch.

### Avoid

- Throwing built-in types or string literals.
- Catching by value.
- Empty catch blocks.
- Swallowing failures and continuing with partially valid state.

## Constants and Immutability (Con.*)

### Key Rules

- Con.1: Make objects immutable by default.
- Con.2: Mark member functions `const` when they do not mutate observable state.
- Con.3: Pass pointers and references to `const` by default.
- Con.4: Use `const` for values that do not change after construction.
- Con.5: Use `constexpr` for compile-time constants.

## Concurrency and Parallelism (CP.*)

### Key Rules

- CP.2: Avoid data races.
- CP.3: Minimize explicit sharing of writable data.
- CP.4: Think in tasks, not raw threads, when the subsystem allows it.
- CP.8: Do not use `volatile` for synchronization.
- CP.20: Use RAII for locks.
- CP.21: Use `std::scoped_lock` for multiple mutexes.
- CP.22: Never call unknown code while holding a lock.
- CP.42: Do not wait without a condition.
- CP.44: Name `lock_guard` and `unique_lock` objects.
- CP.50: Define the mutex together with the data it protects.
- CP.100: Avoid lock-free techniques unless the problem truly requires them.

### Preferred Pattern

```cpp
class ThreadSafeQueue {
 public:
    void push(int value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
        cv_.notify_one();
    }

    int pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        const int value = queue_.front();
        queue_.pop();
        return value;
    }

 private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<int> queue_;
};
```

### Avoid

- Detached threads without a clear ownership and shutdown model.
- Unnamed temporary lock guards.
- Sleeping for synchronization in tests or production coordination logic.
- Calling callbacks while holding internal locks.

## Templates and Generic Programming (T.*)

### Key Rules

- T.1: Use templates to raise the level of abstraction.
- T.2: Use templates to express reusable algorithms.
- T.10: Constrain templates when the minimum standard and local style permit it.
- T.11: Prefer standard concepts when available.
- T.13: Use shorthand concept notation for simple cases.
- T.43: Prefer `using` over `typedef`.
- T.120: Use template metaprogramming only when needed.
- T.144: Do not specialize function templates when an overload is clearer.

### Repository Rule

If the touched target is effectively C++17-only or does not already use concepts, prefer simple interfaces, `static_assert`, and readable traits over forced modernization. Do not introduce template machinery that obscures data flow.

## Standard Library (SL.*)

### Key Rules

- SL.1: Use libraries wherever practical.
- SL.2: Prefer the standard library to ad hoc replacements unless the repository already has the canonical abstraction.
- SL.con.1: Prefer `std::array` or `std::vector` over C arrays.
- SL.con.2: Prefer `std::vector` by default for dynamic sequences.
- SL.str.1: Use `std::string` to own text.
- SL.str.2: Use `std::string_view` for non-owning string parameters when lifetime is clear.
- SL.io.50: Avoid `std::endl`; use `'\n'`.

## Enumerations (Enum.*)

### Key Rules

- Enum.1: Prefer enumerations over macros.
- Enum.3: Prefer `enum class` over plain `enum`.
- Enum.5: Use ordinary identifier casing for enumerators, not ALL_CAPS.
- Enum.6: Avoid unnamed enumerations.

## Source Files, Naming, and Headers (SF.*, NL.*)

### Key Rules

- SF.7: Never write `using namespace` at global scope in a header.
- SF.8: Use include guards or the repository's accepted header protection pattern.
- SF.11: Headers must be self-contained.
- NL.5: Avoid Hungarian notation.
- NL.8: Use a consistent naming style.
- NL.9: Reserve ALL_CAPS for macros and similar preprocessor symbols.
- NL.10: Follow the surrounding subsystem's naming conventions consistently.

### Repository Rule

This repository mixes long-lived subsystem conventions. Match nearby code first. Do not perform incidental renames across a file or module unless the task explicitly includes that refactor.

## Performance (Per.*)

### Key Rules

- Per.1: Do not optimize without evidence.
- Per.2: Do not optimize prematurely.
- Per.4: Do not assume low-level code is automatically faster.
- Per.6: Do not make performance claims without measurement.
- Per.7: Design so optimization remains possible.
- Per.10: Rely on the static type system.
- Per.11: Move work from run time to compile time when it pays off.
- Per.19: Prefer predictable, contiguous data access.

## CMake Guidance for C++ Targets

- Prefer target-based CMake: `target_link_libraries`, `target_include_directories`, `target_compile_definitions`, and `target_compile_features`.
- Keep compile options, sanitizer flags, warnings, and definitions scoped to the smallest practical target set.
- Do not encode compiler behavior through global mutable variables when a target property or generator expression is sufficient.
- Preserve existing preset, sanitizer, and test wiring unless the task explicitly changes build orchestration.

## Quick Review Checklist

- No raw `new` or `delete`; use RAII or explicit owning types.
- Objects are initialized at declaration or construction.
- Variables are `const` or `constexpr` by default.
- Member functions are `const` when possible.
- `enum class` is used unless a plain enum is required for interoperability.
- `nullptr` is used instead of `0` or `NULL`.
- Narrowing conversions and casual signed/unsigned mixes are avoided.
- C-style casts are absent.
- Single-argument constructors are `explicit`.
- The Rule of Zero or Rule of Five is applied deliberately.
- Base class destructors have correct virtuality.
- Templates are constrained or kept intentionally simple for the target standard.
- Headers are self-contained and avoid `using namespace`.
- Locks use RAII and waits use predicates.
- Error handling matches subsystem policy and build constraints.
- `std::endl` is not used.
- Magic numbers are replaced with named constants where intent matters.
- Never wait without a condition.
- Never call unknown code while holding a lock.
- Avoid detached threads unless lifetime is externally owned and documented.
- Avoid lock-free programming unless it is necessary, measured, and understood.

## Templates and Generic Programming (T.*)

- Use templates to raise the level of abstraction, not to hide unclear contracts.
- Constrain templates with concepts or equivalent static requirements when the active language level supports them.
- Prefer standard concepts over custom ones when they express the contract.
- Prefer overloads to function-template specialization.
- Prefer `using` over `typedef`.
- Prefer `constexpr` and ordinary code over template metaprogramming when both solve the problem.
- Keep generic code readable and diagnostics comprehensible.

## Standard Library (SL.*)

- Prefer standard library containers, algorithms, and utilities over handwritten reinventions.
- Prefer `std::vector` by default and `std::array` for fixed-size owned storage.
- Use `std::string` to own text and `std::string_view` to observe it.
- Prefer `std::span` or the repository's equivalent borrowed-range type for contiguous views.
- Prefer algorithms and ranges when they improve clarity over manual loops.
- Use `\n` instead of `std::endl`.
- Avoid C arrays and manual length bookkeeping in new code.

## Enumerations (Enum.*)

- Prefer enumerations over macros.
- Prefer `enum class` over plain `enum`.
- Avoid unnamed enumerations.
- Reserve ALL_CAPS for macros and include guards, not enumerators.

## Source Files and Naming (SF.*, NL.*)

- Headers must be self-contained and safe to include in any order.
- Do not write `using namespace` at global scope in headers.
- Include what you use; do not rely on inclusion order.
- Preserve the naming convention already established in the touched module. Do not mass-rename to satisfy an abstract style preference.
- Avoid Hungarian notation and misleading type encodings in names.
- Use macro-style ALL_CAPS only for macros and include guards.

## Performance (Per.*)

- Do not optimize without a reason.
- Do not make performance claims without measurement.
- Prefer simple data flow and predictable memory access.
- Prefer contiguous storage when it fits the ownership and access pattern.
- Move work to compile time when it improves clarity and reduces runtime cost.
- Do not trade maintainability for speculative micro-optimizations.

## CMake Implications

- Prefer target-based CMake: `target_link_libraries`, `target_include_directories`, `target_compile_features`, and `target_compile_definitions`.
- Keep compile options, warnings, and definitions target-scoped.
- Express the language standard and required features explicitly.
- Add tests, tools, and benchmarks using the repository's existing CMake patterns instead of ad hoc local logic.

## Common Anti-Patterns

- Naked `new` or `delete`
- Owning raw pointers
- Output parameters used as hidden returns
- Implicit narrowing and signed/unsigned bugs
- C-style casts and cast-away-const
- Plain `enum` where `enum class` is viable
- Unnamed lock temporaries
- Detached threads without an explicit lifetime contract
- Swallowed errors or reliance on global error state
- Headers that depend on inclusion order
- `using namespace std;` in headers
- Magic numbers and sentinel integers instead of named types or constants

## Quick Review Checklist

Before marking C++ work complete, confirm that:

- ownership is explicit and leak-free
- all objects are initialized
- `const` and `constexpr` are used wherever practical
- interfaces return values instead of hidden output parameters
- no raw owning pointers or manual `new` or `delete` were introduced
- conversions are explicit and non-narrowing
- error handling matches the subsystem contract
- locks use RAII and wait predicates
- headers are self-contained
- templates are constrained or otherwise statically checked
- code follows surrounding repository conventions unless a deliberate improvement is justified