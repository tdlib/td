# TDLib-lazarus

This Example was developed by [*Ruan Diego Lacerda Menezes*](https://github.com/dieletro/)
for use and consumption of the Official [*Telegram TDLib API*](https://core.telegram.org/tdlib) for use in Lazarus

# Developer Support Group:
 * **[*Telegram*](https://t.me/TinjectTelegram)**

## Description

Client for TDLib (Telegram database library) with Lazarus through the json interface.

## Requirements

* Lazarus 2.0.10 or latter
* [superobject](https://github.com/hgourvest/superobject) Files 

## Install

Add to your Lazarus project:

    superdate.pas
    superobject.pas
    supertimezone.pas
    supertypes.pas
    superxmlparser.pas
    
Declare in your Uses:

    superobject;
    
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

### Linux64

Deploy the library libtdjson.so to your device and set the library path calling the method SetTDJsonPath.

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
  //Global Types Definitions
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
  tdjsonDll: TLibHandle = dynlibs.NilHandle;

const
  //DLL name associated with the test project
  {$IFDEF WINDOWS}
    {$IFDEF WIN32}                          //SharedSuffix = dll
       tdjsonDllName : String = 'tdjson.' + SharedSuffix;
    {$ENDIF}
    {$IFDEF WIN64}
       tdjsonDllName : String = 'tdjson-x64.' + SharedSuffix;
    {$ENDIF}
  {$ELSE}
    tdjsonDllName : String = 'libtdjson.so';
  {$ENDIF}

  //Setting the Receiver Timeout
  WAIT_TIMEOUT : double = 1.0; //1 seconds

var
  //should be set to 1, when updateAuthorizationState with authorizationStateClosed is received
  is_closed : integer  = 1;

  //Client Control
  FClient : MyPVoid;

  //Control Session...
  client_session : TtgSession;  
```

## Declare dll functions and procedures

```Pascal
  //Defining DLL methods and functions
  function client_create: MyPVoid; stdcall; external 'tdjson-x64.dll' name 'td_json_client_create';
  procedure client_destroy(handle: MyPVoid); stdcall; external 'tdjson-x64.dll' name 'td_json_client_destroy';
  procedure client_send(handle: MyPVoid; data : MyPCharType); stdcall; external 'tdjson-x64.dll' name 'td_json_client_send';
  function client_receive(handle: MyPVoid; t: double ): MyPCharType; stdcall; external 'tdjson-x64.dll' name 'td_json_client_receive';
  function client_execute(handle: MyPVoid; data : MyPCharType): MyPCharType; stdcall; external 'tdjson-x64.dll' name 'td_json_client_execute';
  procedure set_log_verbosity_level(level: Int32); stdcall; external 'tdjson-x64.dll' name 'td_json_client_set_log_verbosity_level';
  procedure set_log_fatal_error_callback(callback : fatal_error_callback_type); stdcall; external 'tdjson-x64.dll' name 'td_json_client_set_log_fatal_error_callback';

var
  frmLazteste: TfrmLazteste;

implementation

{$R *.lfm}

{ TfrmLazteste } 
```
## Declare this functions:

```Pascal
  public
    //Treatment of the necessary methods and functions
    function td_execute(JsonUTF8: String): String;
    function td_send(JsonUTF8: String): String;
    function td_receive: String;
    function DLLInitialize: Boolean;
 ```

## Implement this functions and procedures:

```Pascal
function TfrmLazteste.td_execute(JsonUTF8: String): String;
var
  JSonAnsiStr: AnsiString;
begin
  JSonAnsiStr := JsonUTF8;
  Result := client_execute(0, MyPCharType(JSonAnsiStr));
end;  

function TfrmLazteste.td_send(JsonUTF8: String): String;
var
  JsonAnsiStr: AnsiString;
begin
  JsonAnsiStr := JsonUTF8;
  client_send(FClient, MyPCharType(JsonAnsiStr));
  Result := JsonAnsiStr;
end; 

function TfrmLazteste.td_receive: String;
var
  ReturnStr, SDebug:  String;
  X, XParam, TLAuthState, TLEvent: ISuperObject;
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

      //# set TDLib parameters
      //# you MUST obtain your own api_id and api_hash at https://my.telegram.org
      //# and use them in the setTdlibParameters call
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
        JsonAnsiStr := txtPhoneNumber.Text;

        X := nil;
        X := SO;
        X.S['@type'] := 'setAuthenticationPhoneNumber';
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
    if TLEvent.AsJSON() <> '' then
    Begin
      Result := 'RECEIVING : '+ TLEvent.AsString;
    End;

  End
  Else
  //# destroy client when it is closed and isn't needed anymore
  Client_destroy(FClient);

  {$ENDREGION 'IMPLEMENTATION'}
end; 

function DLLInitialize: Boolean;
begin
  //I initialize the Function Result to false
  Result := False;

  //Assigning the dll to the variable tdjsonDll
  tdjsonDll := SafeLoadLibrary(tdjsonDllName);

  //Checking if the variable was loaded with the DLL's Pointer
  if tdjsonDll = dynlibs.NilHandle then //DLL was not loaded successfully
  Begin
    raise Exception.CreateFmt('Cannot load Library "%s"'//+ sLineBreak
                             , [tdjsonDllName, GetLoadErrorStr]); //GetLoadErrorStr //GetLastError
    Exit;
  end;

  //Function return definition
  Result := tdjsonDll <> 0; 
end;
```

## Creating a Client:

```Pascal
procedure TfrmLazteste.btnCreateClientClick(Sender: TObject);
begin

  if (Pointer(FClient) = Nil) or (IntPtr(FClient) = 0) then
  Begin
    FClient := client_create;

    with client_session do
    Begin
      Client := FClient;
      ID := MyPVoid(FClient);
      Name := 'Section Desktop TDLib TInjectTelegram';
    End;

    with memSend.Lines do
    Begin
      Add('Name : '+client_session.Name);
      Add('ID : '+client_session.ID.ToString);
      Add('*******Section Initialized********');
    end;

  End
  Else
    Showmessage('There is already a Created Customer!');

end; 
```

## Starting a Service

```Pascal
procedure TfrmLazteste.btnStartClick(Sender: TObject);

  procedure Receiver;
  Begin
    while is_closed = 0 do
    Begin
       with frmLazteste do
         memReceiver.Lines.Add(td_receive);
    End;
  end;

begin

  if (Pointer(FClient) = Nil) or (IntPtr(FClient) = 0) then
  Begin
    Showmessage('Create a client to start the service');
  end
  Else
  Begin
    is_Closed := 0;

      with TThread.CreateAnonymousThread(TProcedure(@Receiver)) do
        begin
          FreeOnTerminate := True;
          Start;
        end;

    memSend.Lines.Add('Service Started!!!');

  end;

end;
```

### Stop Service

```Pascal
procedure TfrmLazteste.btnStopClick(Sender: TObject);
begin
  if is_closed = 1 then
    Showmessage('No active service to stop!')
  Else
  begin
    is_closed := 1;
    memSend.Lines.Add('Service Paused!!!');
  end;
end; 
```

## Set Log Verbosy

```Pascal
procedure TfrmLazteste.btnLogVerbosyClick(Sender: TObject);
var
  JSonAnsiStr: AnsiString;
  X: ISuperObject;
begin
  //# setting TDLib log verbosity level to 1 (errors)
  X := SO;
  X.S['@type'] := 'setLogVerbosityLevel';
  X.I['new_verbosity_level'] := 1;
  X.D['@extra'] := 1.01234;

  //Convert String to AnsiString Type
  JSonAnsiStr := X.AsJSon;

  memSend.Lines.Add('SENDING : '+X.AsJSon);
  memSend.Lines.Add('');

  memReceiver.Lines.Add('RECEIVING : '+td_execute(JSonAnsiStr));
  memReceiver.Lines.Add('');

end; 
```

## Send

```Pascal
procedure TfrmLazteste.btnSendClick(Sender: TObject);
var
  JSonAnsiStr : AnsiString;
  X: ISuperObject;
begin
  if is_closed = 1 then
    Showmessage('No active service to send!')
  Else
  begin
    X := SO;
    X.S['@type'] := 'getAuthorizationState';
    X.D['@extra'] := 1.01234;

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

[MIT](https://github.com/dieletro/tdlib-lazarus/blob/master/LICENSE.txt)

## Authors

The tdlib-lazarus is designed by [Ruan Diego Lacerda Menezes](https://github.com/dieletro)
