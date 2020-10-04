# TDLib-lazarus

### Description

Client for TDLib (Telegram database library) with Lazarus through the json interface.

### Requirements

* Lazarus 2.0.10 or latter
* [superobject](https://github.com/hgourvest/superobject) Files 

### For Code exemples and source files project visit:
* **[tdlib-lazarus](https://github.com/dieletro/tdlib-lazarus)**

### Install

Add to your Lazarus project:

    superdate.pas
    superobject.pas
    supertimezone.pas
    supertypes.pas
    superxmlparser.pas
    
Declare in your Uses:

    superobject;
    
### Configuration
-- Windows

TDLib requires other third-parties libraries: OpenSSL and ZLib. These libraries must be deployed with tdjson library.
* [OpenSSL](https://wiki.openssl.org/index.php/Binaries)
* [ZLib](http://www.winimage.com/zLibDll/)
* Windows versions requires VCRuntime which can be download from microsoft: https://www.microsoft.com/en-us/download/details.aspx?id=52685

Copy the following libraries in the same directory where is your application:

### Windows 64
    tdjson-x64.dll
    libcrypto-1_1-x64.dll
    libssl-1_1-x64.dll
    zlib1.dll

### Windows 32	
    tdjson.dll	
    libcrypto-1_1.dll	
    libssl-1_1.dll	
    zlib1.dll	

### Linux64

Deploy the library libtdjson.so to your device and set the library path calling the method SetTDJsonPath.

### Precompiled binaries included in the example project:
> - [X] Windows 64.
> - [X] Windows 32.
> - [X] Linux64.

### Creating your Telegram Application
In order to obtain an API id and develop your own application using the Telegram API you need to do the following:

Sign up for Telegram using any application.
Log in to your Telegram core: https://my.telegram.org.
Go to API development tools and fill out the form.
You will get basic addresses as well as the api_id and api_hash parameters required for user authorization.
For the moment each number can only have one api_id connected to it.
These values must be set in Telegram.API property of Telegram component. In order to authenticate, you can authenticate as an user or as a bot, there are 2 properties which you can set to login to Telegram:
PhoneNumber: if you login as an user, you must set your phone number (with international code), example: +5521997196000
DatabaseDirectory: allows to specify where is the tdlib database. Leave empty and will take the default configuration.

### The following parameters can be configured:

* ApplicationVersion: application version, example: 1.0
* DeviceModel: device model, example: desktop
* LanguageCode: user language code, example: en or pt, es, ...
* SystemVersion: verison of operating system, example: windows.
