# TDLib-delphi

This Example was developed by [*Ruan Diego Lacerda Menezes*](https://github.com/dieletro/)
for use and consumption of the Official [*Telegram TDLib API*](https://core.telegram.org/tdlib) for use in Delphi

# Developer Support Group:
 * **[*Telegram*](https://t.me/TinjectTelegram)**

## Description

Client for TDLib (Telegram database library) with Delphi through the json interface.

## Requirements

* Delphi 2010 or latter
* [x-superobject](https://github.com/onryldz/x-superobject) Files 

## Install

Add to your Delphi project:

    XSuperJSON.pas
    XSuperObject.inc
    XSuperObject.pas
    
Declare in your Uses:

    XSuperJSON,
    XSuperObject;
    
## Configuration
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

* **Precompiled binaries included in the example project**
> - [X] Windows 64.
> - [X] Windows 32.
> - [X] Linux64.

## Creating your Telegram Application
In order to obtain an API id and develop your own application using the Telegram API you need to do the following:

Sign up for Telegram using any application.

Log in to your Telegram core: https://my.telegram.org.

Go to API development tools and fill out the form.

You will get basic addresses as well as the api_id and api_hash parameters required for user authorization.

For the moment each number can only have one api_id connected to it.

These values must be set in Telegram.API property of Telegram component. In order to authenticate, you can authenticate as an user or as a bot, there are 2 properties which you can set to login to Telegram:

PhoneNumber: if you login as an user, you must set your phone number (with international code), example: +5521997196000

DatabaseDirectory: allows to specify where is the tdlib database. Leave empty and will take the default configuration.

## The following parameters can be configured:

* ApplicationVersion: application version, example: 1.0
* DeviceModel: device model, example: desktop
* LanguageCode: user language code, example: en or pt, es, ...
* SystemVersion: verison of operating system, example: windows.

![Fundo3](https://user-images.githubusercontent.com/11804577/79387409-14196880-7f42-11ea-8e7f-cb2d3270c74c.png)

# Sample Code

```Pascal

type
  MyPCharType = PAnsiChar;
  MyPVoid = IntPtr;

  //Definition of the Session
  TtgSession = record
    Name: String;
    ID: Integer;
    Client : MyPVoid;
  end;

var
  //Variable that receives the dll pointer
  tdjsonDll: THandle;

const
  //DLL name associated with the test project
  {$IFDEF MSWINDOWS}
    tdjsonDllName : String =
        {$IFDEF WIN32} 'tdjson.dll' {$ENDIF}
        {$IFDEF WIN64} 'tdjson-x64.dll' {$ENDIF} {+ SharedSuffix};   //TDLib.dll
  {$ELSE}
    tdjsonDllName : String = 'libtdjson.so' {+ SharedSuffix};
  {$ENDIF}

  //Setting the Receiver Timeout
  WAIT_TIMEOUT : double = 1.0; //1 seconds

var
  // should be set to 1, when updateAuthorizationState with authorizationStateClosed is received
  is_closed : integer  = 1;

  //Client Control
  FClient : MyPVoid;

  //Control Session...
  client_session : TtgSession;

type

  //internal delegate void Callback(IntPtr ptr);
  Ttd_log_fatal_error_callback_ptr = procedure (error_message: MyPCharType);

var

  client_create: function(): MyPVoid; cdecl;
  client_destroy: procedure(handle: MyPVoid); cdecl;
  client_send: procedure(handle: MyPVoid; data : MyPCharType); cdecl;
  client_receive: function(handle: MyPVoid; t: double ): MyPCharType; cdecl;
  client_execute: function(handle: MyPVoid; data : MyPCharType): MyPCharType; cdecl;
    set_log_file_path: function(path: MyPVoid): Int32; cdecl;   //Deprecated
    set_log_max_file_size: procedure(size: Int64); cdecl;       //Deprecated
  set_log_verbosity_level: procedure(level: Int32); cdecl;
  set_log_fatal_error_callback: procedure(callback : Ttd_log_fatal_error_callback_ptr); cdecl;
```

## Declare a function:

```Pascal
function DLLInitialize: Boolean;
var
  dllFilePath: String;
begin
  Result := False;
  dllFilePath := ExtractFilePath(Application.ExeName)+tdjsonDllName;
  if tdjsonDll = 0 then
    tdjsonDll := SafeLoadLibrary(tdjsonDllName);

    if tdjsonDll <> 0 then
    begin
      @client_create := GetProcAddress(tdjsonDll, 'td_json_client_create');
      if not Assigned(client_create) then
        Exit;
      @client_destroy := GetProcAddress(tdjsonDll, 'td_json_client_destroy');
      if not Assigned(client_destroy) then
        Exit;
      @client_send := GetProcAddress(tdjsonDll, 'td_json_client_send');
      if not Assigned(client_send) then
        Exit;
      @client_receive := GetProcAddress(tdjsonDll, 'td_json_client_receive');
      if not Assigned(client_receive) then
        Exit;
      @client_execute := GetProcAddress(tdjsonDll, 'td_json_client_execute');
      if not Assigned(client_execute) then
        Exit;
          //Deprecated
          @set_log_file_path := GetProcAddress(tdjsonDll, 'td_set_log_file_path');
          if not Assigned(set_log_file_path) then
            Exit;
          //Deprecated
          @set_log_max_file_size := GetProcAddress(tdjsonDll, 'td_set_log_max_file_size');
          if not Assigned(set_log_max_file_size) then
            Exit;
      @set_log_verbosity_level := GetProcAddress(tdjsonDll, 'td_set_log_verbosity_level');
      if not Assigned(set_log_verbosity_level) then
        Exit;
      @set_log_fatal_error_callback := GetProcAddress(tdjsonDll, 'td_set_log_fatal_error_callback');
      if not Assigned(set_log_fatal_error_callback) then
        Exit;
    end;

  Result := tdjsonDll <> 0;
end;
```

## Creating a Client:

```Pascal
  if (Pointer(FClient) = Nil) or (IntPtr(FClient) = 0) then
  Begin
    FClient := client_create;

    with client_session do
    Begin
      Client := FClient;
      ID := MyPVoid(FClient);
      Name := 'Section Desktop TDLib TInjectTelegram';
    End;
    
    //This is a TMemo to display data
    with memSend.Lines do
    Begin
      Add('Name : '+client_session.Name);
      Add('ID : '+client_session.ID.ToString);
      Add('*******Section Initialized********');
    end;

  End
  Else
    Showmessage('There is already a Created Customer!');
```

## Receiving Data 

```Pascal
function TForm1.td_receive(): String;
var
  ReturnStr, SDebug:  String;
  X, XParam, TLAuthState,TLEvent: ISuperObject;
  JsonAnsiStr: AnsiString;
begin
  {$REGION 'IMPLEMENTATION'}
  ReturnStr := client_receive(FClient, WAIT_TIMEOUT);

  TLEvent := SO(ReturnStr);

  if TLEvent <> NIl then
  Begin
    {$IFDEF DEBUG}
      SDebug := TLEvent.AsJSON;
    {$ENDIF}

    //# process authorization states
    if TLEvent.S['@type'] = 'updateAuthorizationState' then
    Begin
      TLAuthState := TLEvent.O['authorization_state'];

      //# if client is closed, we need to destroy it and create new client
      if TLAuthState.S['@type'] = 'authorizationStateClosed' then
        Exit;
    //    break;

    //  # set TDLib parameters
    //  # you MUST obtain your own api_id and api_hash at https://my.telegram.org
    //  # and use them in the setTdlibParameters call
      if TLAuthState.S['@type'] = 'authorizationStateWaitTdlibParameters' then
      Begin
        X := nil;
        X := SO;
        X.S['@type'] := 'setTdlibParameters';
        X.O['parameters'] := SO;
        XParam := X.O['parameters'];
          XParam.B['use_test_dc'] := False;
          XParam.S['database_directory'] := 'tdlib';
          XParam.S['files_directory'] := 'myfiles';
          XParam.B['use_file_database'] := True;
          XParam.B['use_chat_info_database'] := True;
          XParam.B['use_message_database'] := True;
          XParam.B['use_secret_chats'] := true;

          JsonAnsiStr := '';
          JsonAnsiStr := txtAPI_ID.Text;
          XParam.I['api_id'] := StrToInt(JsonAnsiStr);

          JsonAnsiStr := '';
          JsonAnsiStr := txtAPI_HASH.Text;
          XParam.S['api_hash'] := JsonAnsiStr;

          XParam.S['system_language_code'] := 'pt';
          XParam.S['device_model'] := 'TInjectTDLibTelegram';
          {$IFDEF WIN32}
            XParam.S['system_version'] := 'WIN32';
          {$ENDIF}
          {$IFDEF WIN64}
            XParam.S['system_version'] := 'WIN64';
          {$ENDIF}
          XParam.S['application_version'] := '1.0';
          XParam.B['enable_storage_optimizer'] := True;
          XParam.B['ignore_file_names'] := False;

          //Send Request
          ReturnStr := td_send(X.AsJSON);
      End;

      //# set an encryption key for database to let know TDLib how to open the database
      if TLAuthState.S['@type'] = 'authorizationStateWaitEncryptionKey' then
      Begin

        X := nil;
        X := SO;
        X.S['@type'] := 'checkDatabaseEncryptionKey';
        X.S['encryption_key'] := '';

        //Send Request
        ReturnStr := td_send(X.AsJSON);
      End;

      //# enter phone number to log in
      if TLAuthState.S['@type'] = 'authorizationStateWaitPhoneNumber' then
      Begin
        //Clear Variable
        JsonAnsiStr:='';

        //Convert String to AnsiString Type
        if chkLoginBot.Checked then
          JsonAnsiStr := txtBotToken.Text
        Else
          JsonAnsiStr := txtPhoneNumber.Text;
//
        X := nil;
        X := SO;
        X.S['@type'] := 'setAuthenticationPhoneNumber';

        if chkLoginBot.Checked then
          X.I['token'] := StrToInt(JsonAnsiStr)
        else
          X.S['phone_number'] := JsonAnsiStr;


        //Send Request
        ReturnStr := td_send(X.AsJSON);
      End;

      //# wait for authorization code
      if TLAuthState.S['@type'] = 'authorizationStateWaitCode' then
      Begin
        //Clear Variable
        JsonAnsiStr:='';

        //Convert String to AnsiString Type
        JsonAnsiStr := InputBox('User Authorization', 'Enter the authorization code', '');

        X := nil;
        X := SO;
        X.S['@type'] := 'checkAuthenticationCode';
        X.S['code'] := JsonAnsiStr;

        //Send Request
        ReturnStr := td_send(X.AsJSON);
      End;

      //# wait for first and last name for new users
      if TLAuthState.S['@type'] = 'authorizationStateWaitRegistration' then
      Begin
        X := nil;
        X := SO;
        X.S['@type'] := 'registerUser';
        X.S['first_name'] := 'Ruan Diego';
        X.S['last_name'] := 'Lacerda Menezes';

        //send request
        ReturnStr := td_send(X.AsJSON);
      End;

      //# wait for password if present
      if TLAuthState.S['@type'] = 'authorizationStateWaitPassword' then
      Begin
        //Clear Variable
        JsonAnsiStr := '';

        //Convert String to AnsiString Type
        JsonAnsiStr := InputBox('User Authentication ',' Enter the access code', '');

        X := nil;
        X := SO;
        X.S['@type'] := 'checkAuthenticationPassword';
        X.S['password'] := JsonAnsiStr;

        //Send Request
        ReturnStr := td_send(X.AsJSON);
      End;

    End;

    if TLEvent.S['@type'] = 'error' then
    Begin
      //if an error is found, stop the process
      if is_Closed = 0 then
         is_Closed := 1
      else
          is_Closed := 0;

      Showmessage('An error was found:'+ #10#13 +
                  'code : ' + TLEvent.S['code'] + #10#13 +
                  'message : '+TLEvent.S['message']);
    end;

    //# handle an incoming update or an answer to a previously sent request
    if TLEvent.AsJSON() <> '{}' then
      Result := 'RECEIVING : '+ TLEvent.AsJSON;

  End
  Else
  //# destroy client when it is closed and isn't needed anymore
  Client_destroy(FClient);
  ```

## Starting a Service

```Pascal
  if (Pointer(FClient) = Nil) or (IntPtr(FClient) = 0) then
  Begin
    Showmessage('Create a client to start the service');
  end
  Else
  Begin
    is_closed := 0;

    TThread.CreateAnonymousThread(
    procedure
    begin
      while is_closed = 0 do
      Begin
        //This is a TMemo to display data
        memReceiver.Lines.Add(td_receive);
      End
    end).Start;
    
    //This is a TMemo to display data
    memSend.Lines.Add('Service Started!!!');
  end;
```

### Stop Service

```Pascal
  if is_closed = 1 then
    Showmessage('No active service to stop!')
  Else
  begin
    is_closed := 1;
    memSend.Lines.Add('Service Paused!!!');
  end;
```

## Set Log Verbosy

```Pascal
procedure TForm1.Button4Click(Sender: TObject);
var
 X: ISuperObject;
 JSonAnsiStr: AnsiString;
begin
  //# setting TDLib log verbosity level to 1 (errors)
  X := SO;
  X.S['@type'] := 'setLogVerbosityLevel';
  X.I['new_verbosity_level'] := 1;
  X.F['@extra'] := 1.01234;

  //Convert String to AnsiString Type
  JSonAnsiStr := X.AsJSon;

  memSend.Lines.Add('SENDING : '+JSonAnsiStr);
  memSend.Lines.Add('');

  memReceiver.Lines.Add('RECEIVING : '+td_execute(JSonAnsiStr));
  memReceiver.Lines.Add('');

end;
```

## Send

```Pascal
procedure TForm1.btnSendClick(Sender: TObject);
var
  X: ISuperObject;
  JSonAnsiStr: AnsiString;
begin
  if is_closed = 1 then
    Showmessage('No active service to send!')
  Else
  begin
    X := SO;
    X.S['@type'] := 'getAuthorizationState';
    X.F['@extra'] := 1.01234;

    JSonAnsiStr := X.AsJSon;

    memSend.Lines.Add('SENDING : '+X.AsJSon);
    memSend.Lines.Add('');

    td_send(JSonAnsiStr);
  end;

end;
```
---
> **Ruan Diego Lacerda Menezes (dieletro).**
1. Contatos
    * **[whatsapp](https://web.whatsapp.com/send?phone=5521997196000&text=Olá#13gostaria#13de#13saber#13mais#13sobre#13o#13Projeto#13TinjectTelegram)** 
    * **[telegram](https://t.me/diegolacerdamenezes)**  
    * **[email](https://mail.google.com/mail/u/0/?view=cm&fs=1&tf=1&source=mailto&to=diegolacerdamenezes@gmail.com)**
    * **[instagram](https://www.instagram.com/lacerdamenezes/?hl=pt-br)**
---

## License

[MIT](https://github.com/dieletro/tdlib-delphi/blob/master/LICENSE.txt)

## Authors

The tdlib-delphi is designed by [Ruan Diego Lacerda Menezes](https://github.com/dieletro)
