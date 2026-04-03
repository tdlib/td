---
description: C++ best practices and code style guidelines for consistent, high-quality code generation aligned with our conventions, stack, and best practices.
applyTo: **/*.cpp, **/*.h, **/*.hpp
---

# GitHub Copilot Instructions

These instructions define how GitHub Copilot should assist with this project. The goal is to ensure consistent, high-quality code generation aligned with our conventions, stack, and best practices.

## 🧠 Context

- **Project Type**: System Library / Game Engine / Cross-Platform App / CLI Tool
- **Language**: C++
- **Framework / Libraries**: STL / Boost / Qt / CMake / GoogleTest
- **Architecture**: Modular / Layered / ECS / RAII / OOP / Component-Based

## 🔧 General Guidelines

- Use modern C++ (C++17 or C++20 where supported).
- Prefer RAII for resource management.
- Favor smart pointers (`std::unique_ptr`, `std::shared_ptr`) over raw pointers.
- Use `const`, `constexpr`, and `noexcept` to express intent.
- Keep headers clean—avoid logic in header files.
- Use `clang-format` or `.editorconfig` for consistent formatting.
- Favor readability, modularity, and exception safety.

## 📁 File Structure

Use this structure as a guide when creating or updating files:

```text
src/
  core/
  engine/
  components/
  systems/
  utils/
  config/
include/
  project_name/
tests/
  unit/
  integration/
build/
```

## 🧶 Patterns

### ✅ Patterns to Follow

- Use RAII for all resource ownership (memory, file handles, etc.).
- Encapsulate with classes and clean header/implementation separation.
- Favor the Rule of 5 or Rule of 0 when managing custom types.
- Use interfaces (`abstract class`) for testability and polymorphism.
- Prefer `enum class` over unscoped enums.
- Use assertions (`assert()`) and logging macros for debug checks.
- Use CMake targets and `target_include_directories` for modular build config.
- Use `std::optional` or `std::variant` for nullable/union-like types.

### 🚫 Patterns to Avoid

- Don’t use raw pointers for ownership unless performance-critical and documented.
- Avoid macros for constants—prefer `constexpr` or `inline` `const`.
- Don’t put implementation logic in header files unless using templates.
- Avoid global variables unless necessary and wrapped in a namespace or singleton.
- Don’t overuse inheritance; prefer composition.
- Avoid manual memory management unless absolutely required.

## 🧪 Testing Guidelines

- Use `GoogleTest` or `Catch2` for unit testing.
- Isolate dependencies using mock interfaces or adapters.
- Test core logic in isolation from I/O or rendering subsystems.
- Use `CMake` to register and run tests via `ctest`.
- Test constructors, copy/move semantics, and edge cases.

## 🧩 Example Prompts

- `Copilot, create a C++ class for a thread-safe queue using std::mutex and std::condition_variable.`
- `Copilot, implement an abstract class Drawable with a draw() method and two subclasses.`
- `Copilot, write a CMakeLists.txt file that builds all .cpp files in src and links Boost.`
- `Copilot, write a GoogleTest unit test for the MathUtils::Factorial function.`
- `Copilot, implement a RAII wrapper for a FILE* handle using std::unique_ptr and a custom deleter.`

## 🔁 Iteration & Review

- Always review Copilot-generated code for memory safety and correctness.
- Use comments to explain complex logic and guide Copilot suggestions.
- Refactor output to match the project's code conventions.
- Run `clang-tidy`, `cppcheck`, or sanitizers on generated code for static analysis.

## 📚 References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Modern C++ Features (C++17/20)](https://github.com/AnthonyCalandra/modern-cpp-features)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [CMake Documentation](https://cmake.org/documentation/)
- [GoogleTest Documentation](https://github.com/google/googletest)
- [Catch2 Testing Framework](https://github.com/catchorg/Catch2)
- [Clang-Tidy Checks](https://clang.llvm.org/extra/clang-tidy/)
- [CppReference STL Docs](https://en.cppreference.com/w/cpp)