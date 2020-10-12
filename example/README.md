# TDLib usage and build examples

This directory contains basic examples of TDLib usage from different programming languages and examples of library building for different platforms.
If you are looking for documentation of all available TDLib methods, see the [td_api.tl](https://github.com/tdlib/td/blob/master/td/generate/scheme/td_api.tl) scheme or the
automatically generated [HTML documentation](https://core.telegram.org/tdlib/docs/td__api_8h.html) for a list of all available TDLib
[methods](https://core.telegram.org/tdlib/docs/classtd_1_1td__api_1_1_function.html) and [classes](https://core.telegram.org/tdlib/docs/classtd_1_1td__api_1_1_object.html).
Also take a look at our [Getting Started](https://core.telegram.org/tdlib/getting-started) tutorial for a description of basic TDLib concepts.

TDLib can be easily used from almost any programming language on any platform. See a [TDLib build instructions generator](https://tdlib.github.io/td/build.html) for detailed instructions on how to build TDLib.
Choose your preferred programming language to see examples of usage and a detailed description:

- [Python](#python)
- [JavaScript](#javascript)
- [Go](#go)
- [Java](#java)
- [Kotlin](#kotlin)
- [C#](#csharp)
- [C++](#cxx)
- [Swift](#swift)
- [Objective-C](#objective-c)
- [Object Pascal](#object-pascal)
- [Dart](#dart)
- [Rust](#rust)
- [Erlang](#erlang)
- [PHP](#php)
- [Lua](#lua)
- [Ruby](#ruby)
- [Clojure](#clojure)
- [Emacs Lisp](#emacslisp)
- [D](#d)
- [Elixir](#elixir)
- [1С](#1s)
- [C](#c)
- [Other](#other)

<a name="python"></a>
## Using TDLib in Python projects

TDLib can be used from Python through the [JSON](https://github.com/tdlib/td#using-json) interface.

Convenient Python wrappers already exist for our JSON interface.

If you use modern Python >= 3.6, take a look at [python-telegram](https://github.com/alexander-akhmetov/python-telegram).
The wrapper uses the full power of asyncio, has a good documentation and has several examples. It can be installed through pip or used in a Docker container.
You can also try a fork [python-telegram](https://github.com/iTeam-co/python-telegram) of this library.

For older Python versions you can use [pytdlib](https://github.com/pytdlib/pytdlib) or [python-tdlib](https://github.com/andrew-ld/python-tdlib).
These wrappers contain generators for TDLib API classes and basic classes for interaction with TDLib.

You can also check out [example/python/tdjson_example.py](https://github.com/tdlib/td/tree/master/example/python/tdjson_example.py) and
[tdlib-python](https://github.com/JunaidBabu/tdlib-python) for some very basic examples of TDLib JSON interface integration with Python.

<a name="javascript"></a>
## Using TDLib in JavaScript projects

TDLib can be compiled to WebAssembly or asm.js and used in a browser from JavaScript. See [tdweb](https://github.com/tdlib/td/tree/master/example/web) as a convenient wrapper for TDLib in a browser
and [telegram-react](https://github.com/evgeny-nadymov/telegram-react) as an example of a TDLib-based Telegram client.

TDLib can be used from Node.js through the [JSON](https://github.com/tdlib/td#using-json) interface.

Convenient Node.js wrappers already exist for our JSON interface.
For example, take a look at [Airgram](https://github.com/airgram/airgram) – modern TDLib framework for TypeScript/JavaScript, or
at [tdl](https://github.com/Bannerets/tdl), which provides a convenient, fully-asynchronous interface for interaction with TDLib and contains a bunch of examples.

You can also see [tglib](https://github.com/nodegin/tglib), [node-tdlib](https://github.com/wfjsw/node-tdlib), [tdlnode](https://github.com/fonbah/tdlnode),
[Paper Plane](https://github.com/BlackSuited/paper-plane) and [node-tlg](https://github.com/dilongfa/node-tlg) for other examples of TDLib JSON interface integration with Node.js.

TDLib can be used also from NativeScript through the [JSON](https://github.com/tdlib/td#using-json) interface.
See [nativescript-tglib](https://github.com/arpit2438735/nativescript-tglib) as an example of a NativeScript library for building Telegram clients.

<a name="go"></a>
## Using TDLib in Go projects

TDLib can be used from the Go programming language through the [JSON](https://github.com/tdlib/td#using-json) interface and Cgo, and can be linked either statically or dynamically.

Convenient Go wrappers already exist for our JSON interface.
For example, take a look at [go-tdlib](https://github.com/zelenin/go-tdlib) or [go-tdlib](https://github.com/Arman92/go-tdlib), which provide a convenient TDLib client, a generator for TDLib API classes and contain many examples.

You can also see [go-tdjson](https://github.com/L11R/go-tdjson) for another example of TDLib JSON interface integration with Go.

<a name="java"></a>
## Using TDLib in Java projects

TDLib can be used from the Java programming language through native [JNI](https://github.com/tdlib/td#using-java) binding.

We provide a generator for JNI bridge methods and Java classes for all TDLib API methods and objects.
See [example/java](https://github.com/tdlib/td/tree/master/example/java) for an example of using TDLib from desktop Java along with detailed building and usage instructions.
To use TDLib to create Android Java applications, use our [prebuilt library for Android](https://core.telegram.org/tdlib/tdlib.zip).

You can also see [JTDLib](https://github.com/ErnyTech/JTDLib) for another example of Java wrapper for TDLib.

<a name="kotlin"></a>
## Using TDLib in Kotlin projects

TDLib can be used from the Kotlin/JVM programming language through same way as in [Java](#java).

You can also use [ktd](https://github.com/whyoleg/ktd) library with kotlin specific bindings.

<a name="csharp"></a>
## Using TDLib in C# projects

TDLib provides a native [.NET](https://github.com/tdlib/td#using-dotnet) interface through `C++/CLI` and `C++/CX`.
See [example/uwp](https://github.com/tdlib/td/tree/master/example/uwp) for an example of building TDLib SDK for the Universal Windows Platform and an example of its usage from C#.
See [example/csharp](https://github.com/tdlib/td/tree/master/example/csharp) for an example of building TDLib with `C++/CLI` support and an example of TDLib usage from C# on Windows.

If you want to write a cross-platform C# application using .NET Core, see [tdsharp](https://github.com/egramtel/tdsharp). It uses our [JSON](https://github.com/tdlib/td#using-json) interface,
provides an asynchronous interface for interaction with TDLib, automatically generated classes for TDLib API and has some examples.

Also see [Unigram](https://github.com/UnigramDev/Unigram), which is a full-featured client rewritten from scratch in C# using TDLib SDK for Universal Windows Platform in less than 2 months, or
[egram.tel](https://github.com/egramtel/egram.tel) – a cross-platform Telegram client written in C#, .NET Core, ReactiveUI and Avalonia.

<a name="cxx"></a>
## Using TDLib in C++ projects

TDLib has a simple and convenient C++11-interface for sending and receiving requests and can be statically linked to your application.

See [example/cpp](https://github.com/tdlib/td/tree/master/example/cpp) for an example of TDLib usage from C++.
[td_example.cpp](https://github.com/tdlib/td/tree/master/example/cpp/td_example.cpp) contains an example of authorization, processing new incoming messages, getting a list of chats and sending a text message.

See also the source code of [Depecher](https://github.com/blacksailer/depecher) – a Telegram app for Sailfish OS, and [TELEports](https://gitlab.com/ubports/apps/teleports) – a Qt-client for Ubuntu Touch, both of which are based on TDLib.

<a name="swift"></a>
## Using TDLib in Swift projects

TDLib can be used from the Swift programming language through the [JSON](https://github.com/tdlib/td#using-json) interface and can be linked statically or dynamically.

See [TDLib-iOS](https://github.com/leoMehlig/TDLib-iOS), which provide a convenient TDLib client with automatically generated and fully-documented classes for all TDLib API methods and objects.

See [example/swift](https://github.com/tdlib/td/tree/master/example/swift) for an example of a macOS Swift application.
See [example/ios](https://github.com/tdlib/td/tree/master/example/ios) for an example of building TDLib for iOS, watchOS, tvOS, and macOS.

<a name="objective-c"></a>
## Using TDLib in Objective-C projects

TDLib can be used from the Objective-C programming language through [JSON](https://github.com/tdlib/td#using-json) interface and can be linked statically or dynamically.

See [example/ios](https://github.com/tdlib/td/tree/master/example/ios) for an example of building TDLib for iOS, watchOS, tvOS, and macOS.

<a name="object-pascal"></a>
## Using TDLib in Object Pascal projects with Delphi and Lazarus

TDLib can be used from the Object Pascal programming language through the [JSON](https://github.com/tdlib/td#using-json).

See [tdlib-delphi](https://github.com/dieletro/tdlib-delphi) for an example of TDLib usage from Delphi.

See [tdlib-lazarus](https://github.com/dieletro/tdlib-lazarus) for an example of TDLib usage from Lazarus.

<a name="dart"></a>
## Using TDLib in Dart projects

TDLib can be used from the Dart programming language through the [JSON](https://github.com/tdlib/td#using-json) interface and a Dart Native Extension or Dart FFI.

See [dart_tdlib](https://github.com/periodicaidan/dart_tdlib) or [Dart wrapper for TDLib](https://github.com/tdlib/td/pull/708/commits/237060abd4c205768153180e9f814298d1aa9d49) for an example of a TDLib Dart bindings through FFI.

See [project.scarlet](https://github.com/aaugmentum/project.scarlet) or [tdlib-dart](https://github.com/triedcatched/tdlib-dart) for an example of using TDLib from Dart.

<a name="rust"></a>
## Using TDLib in Rust projects

TDLib can be used from the Rust programming language through the [JSON](https://github.com/tdlib/td#using-json) interface.

See [rtdlib](https://github.com/fewensa/rtdlib), [tdlib-rs](https://github.com/d653/tdlib-rs), [tdlib-futures](https://github.com/yuri91/tdlib-futures),
[rust-tdlib](https://github.com/lattenwald/rust-tdlib) or [tdjson-rs](https://github.com/mersinvald/tdjson-rs) for an example of TDLib Rust bindings.

<a name="erlang"></a>
## Using TDLib in Erlang projects

TDLib can be used from the Erlang programming language through the [JSON](https://github.com/tdlib/td#using-json) interface.

See [erl-tdlib](https://github.com/lattenwald/erl-tdlib) for an example of TDLib Erlang bindings.

<a name="php"></a>
## Using TDLib in PHP projects

If you use modern PHP >= 7.4, you can use TDLib via a PHP FFI extension. For example, take a look at [ffi-tdlib](https://github.com/aurimasniekis/php-ffi-tdlib) - an FFI-based TDLib wrapper.

See also [tdlib-schema](https://github.com/aurimasniekis/php-tdlib-schema) - a generator for TDLib API classes.

For older PHP versions you can use TDLib by wrapping its functionality in a PHP extension.

See [phptdlib](https://github.com/yaroslavche/phptdlib), [tdlib](https://github.com/aurimasniekis/php-ext-tdlib) or [PIF-TDPony](https://github.com/danog/pif-tdpony) for examples of such extensions which provide access to TDLib from PHP.

See [tdlib-bundle](https://github.com/yaroslavche/tdlib-bundle) – a Symfony bundle based on [phptdlib](https://github.com/yaroslavche/phptdlib).

<a name="lua"></a>
## Using TDLib in Lua projects

TDLib can be used from the Lua programming language through the [JSON](https://github.com/tdlib/td#using-json) interface.

See [tdlua](https://github.com/giuseppeM99/tdlua) for examples of TDLib Lua bindings and basic usage examples.

See also [tdbot](https://github.com/vysheng/tdbot), which makes all TDLib features available from Lua scripts.

<a name="d"></a>
## Using TDLib in D projects

TDLib can be used from the D programming language through the [JSON](https://github.com/tdlib/td#using-json) interface.

See [d-tdlib-service](https://github.com/Lord-Evil/d-tdlib-service) for an example of TDLib D bindings.

<a name="ruby"></a>
## Using TDLib in Ruby projects

TDLib can be used from the Ruby programming language through the [JSON](https://github.com/tdlib/td#using-json) interface.

See [tdlib-ruby](https://github.com/centosadmin/tdlib-ruby) for examples of Ruby bindings and a client for TDLib.

<a name="clojure"></a>
## Using TDLib in Clojure projects

TDLib can be used from the Clojure programming language through the [JSON](https://github.com/tdlib/td#using-json) interface.

See [clojure-tdlib-json-wrapper](https://github.com/MityaSaray/clojure-tdlib-json-wrapper) for an example of TDLib Clojure bindings.

<a name="emacslisp"></a>
## Using TDLib in Emacs Lisp projects

TDLib can be used from the Emacs Lisp programming language.

See [telega.el](https://github.com/zevlg/telega.el) for an example of a GNU Emacs Telegram client.

<a name="elixir"></a>
## Using TDLib in Elixir projects

TDLib can be used from the Elixir programming language.

See [Elixir TDLib](https://gitlab.com/Fnux/elixir-tdlib) or [Elixir TDLib](https://github.com/QuantLayer/elixir-tdlib) for examples of such usage and an Elixir client for TDLib.
This library contains automatically generated and fully-documented classes for all TDLib API methods and objects.

<a name="1s"></a>
## Using TDLib from 1С:Enterprise

TDLib can be used from the 1С programming language.

See [TDLib bindings for 1С:Enterprise](https://github.com/Infactum/telegram-native) for examples of such usage.

<a name="c"></a>
## Using TDLib in C projects

TDLib can be used from the C programming language through the [JSON](https://github.com/tdlib/td#using-json) interface and can be linked statically or dynamically.

You can also try to use our [C](https://github.com/tdlib/td/blob/master/td/telegram/td_c_client.h) client, which was used by the private TDLib-based version of [telegram-cli](https://github.com/vysheng/tg).

<a name="other"></a>
## Using TDLib from other programming languages

You can use TDLib from any other programming language using [tdbot](https://github.com/vysheng/tdbot) or [TDLib JSON CLI](https://github.com/oott123/tdlib-json-cli),
which provide a command line tool for interaction with TDLIb using the [JSON](https://github.com/tdlib/td#using-json) interface through stdin and stdout.
You can use this method to use TDLib, for example, from Brainfuck (unfortunately, we haven't seen examples of sending a Telegram message through TDLib on Brainfuck yet).

Alternatively, you can use the TDLib [JSON](https://github.com/tdlib/td#using-json) interface directly from your programming language.

Feel free to create an issue, if you have created a valuable TDLib binding or a TDLib client in some programming language and want it to be added to this list of examples.
