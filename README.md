# Telegram CLI for Linux

📦 **Telegram CLI** — это терминальный клиент Telegram, разработанный на C++ с использованием [TDLib (Telegram Database Library)](https://core.telegram.org/tdlib).  
Работает полностью в Linux-терминале и позволяет взаимодействовать с Telegram без графического интерфейса.

## 🚀 Возможности

- Аутентификация через номер телефона
- Поддержка кода подтверждения и двухфакторной авторизации
- Использование официальной Telegram TDLib
- Работа в чистом CLI (Command Line Interface)

## 🔧 Зависимости

- C++17 компилятор (например, `g++`)
- CMake ≥ 3.10
- TDLib
- OpenSSL (для TDLib)

### Установка TDLib

```bash
git clone https://github.com/tdlib/td.git
cd td
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target tdjson
sudo make install
