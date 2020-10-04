# TDLib-delphi

This Example was developed for use and consumption of the Official [*Telegram TDLib API*](https://core.telegram.org/tdlib) for use in Delphi

# For More Info and Source Files project, visit:
 * **[*tdlib-delphi*](https://github.com/dieletro/tdlib-delphi)**

## Description

Client for TDLib (Telegram database library) with Delphi through the json interface.

## Requirements

* Delphi 2010 or higher
* [x-superobject](https://github.com/onryldz/x-superobject) Files 

### Install

Add to your Delphi project:

    XSuperJSON.pas
    XSuperObject.inc
    XSuperObject.pas
    
Declare in your Uses:

    XSuperJSON,
    XSuperObject;
    
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

### OSX64

Deploy the library libtdjson.dylib to your device and you can set where is the library using SetTDJsonPath, example:
if you deploy to "Contents\MacOS\", you must set the path in System.SysUtils.GetCurrentDir folder.

### Linux64

Deploy the library libtdjson.so to your device and set the library path calling the method SetTDJsonPath.

### Android

Deploy the library libtdjsonandroid.so to your device. Example: if you deploy an Android64 library, set RemotePath in Project/Deployment to "library\lib\arm64-v8a\". If is Android32, set RemotePath to "library\lib\armeabi-v7a\"

### iOS64

Copy the library libtdjson.a to these directories:
C:\Program Files (x86)\Embarcadero\Studio\<IDE Version>\lib\iosDevice64\debug
C:\Program Files (x86)\Embarcadero\Studio\<IDE Version>\lib\iosDevice64\release

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
