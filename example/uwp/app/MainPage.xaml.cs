//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
using System;
using System.IO;
using Td = Telegram.Td;
using TdApi = Telegram.Td.Api;
using Windows.UI.Core;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace TdApp
{
    public sealed partial class MainPage : Page
    {
        public System.Collections.ObjectModel.ObservableCollection<string> Items { get; set; }

        private MyClientResultHandler _handler;

        public MainPage()
        {
            InitializeComponent();

            Items = new System.Collections.ObjectModel.ObservableCollection<string>();
            _handler = new MyClientResultHandler(this);

            Td.Client.Execute(new TdApi.SetLogVerbosityLevel(0));
            Td.Client.Execute(new TdApi.SetLogStream(new TdApi.LogStreamFile(Path.Combine(Windows.Storage.ApplicationData.Current.LocalFolder.Path, "log"), 1 << 27, false)));

            System.Threading.Tasks.Task.Run(() =>
            {
                try
                {
                    _client = Td.Client.Create(_handler);
                    var parameters = new TdApi.TdlibParameters();
                    parameters.DatabaseDirectory = Windows.Storage.ApplicationData.Current.LocalFolder.Path;
                    parameters.UseSecretChats = true;
                    parameters.UseMessageDatabase = true;
                    parameters.ApiId = 94575;
                    parameters.ApiHash = "a3406de8d171bb422bb6ddf3bbd800e2";
                    parameters.SystemLanguageCode = "en";
                    parameters.DeviceModel = "Desktop";
                    parameters.ApplicationVersion = "1.0.0";
                    _client.Send(new TdApi.SetTdlibParameters(parameters), null);
                    _client.Send(new TdApi.CheckDatabaseEncryptionKey(), null);
                    _client.Run();
                }
                catch (Exception ex)
                {
                    Print(ex.ToString());
                }
            });
        }

        public void Print(String str)
        {
            var delayTask = Dispatcher.RunAsync(CoreDispatcherPriority.Normal, () =>
            {
                Items.Insert(0, str.Substring(0, Math.Min(1024, str.Length)));
            });
        }

        private Td.Client _client;

        private void AcceptCommand(String command)
        {
            Input.Text = string.Empty;
            Items.Insert(0, string.Format(">>{0}", command));
        }
        private void Button_Click(object sender, RoutedEventArgs e)
        {
            var command = Input.Text;

            if (command.StartsWith("DESTROY"))
            {
                AcceptCommand("Destroy");
                _client.Send(new TdApi.Destroy(), _handler);
            }
            else if (command.StartsWith("lo"))
            {
                AcceptCommand("LogOut");
                _client.Send(new TdApi.LogOut(), _handler);
            }
            else if (command.StartsWith("gas"))
            {
                AcceptCommand(command);
                _client.Send(new TdApi.GetAuthorizationState(), _handler);
            }
            else if (command.StartsWith("sap"))
            {
                var args = command.Split(" ".ToCharArray(), 2);
                AcceptCommand(command);
                _client.Send(new TdApi.SetAuthenticationPhoneNumber(args[1], null), _handler);
            }
            else if (command.StartsWith("cac"))
            {
                var args = command.Split(" ".ToCharArray(), 2);
                AcceptCommand(command);
                _client.Send(new TdApi.CheckAuthenticationCode(args[1]), _handler);
            }
            else if (command.StartsWith("cap"))
            {
                var args = command.Split(" ".ToCharArray(), 2);
                AcceptCommand(command);
                _client.Send(new TdApi.CheckAuthenticationPassword(args[1]), _handler);
            }
            else if (command.StartsWith("gco"))
            {
                var args = command.Split(" ".ToCharArray(), 2);
                AcceptCommand(command);
                _client.Send(new TdApi.SearchContacts(), _handler);
            }
            else if (command.StartsWith("df"))
            {
                var args = command.Split(" ".ToCharArray(), 2);
                AcceptCommand(command);
                _client.Send(new TdApi.DownloadFile(Int32.Parse(args[1]), 1, 0, 0, false), _handler);
            }
            else if (command.StartsWith("bench"))
            {
                var args = command.Split(" ".ToCharArray(), 2);
                AcceptCommand(command);
                var cnt = Int32.Parse(args[1]);
                var handler = new BenchSimpleHandler(this, cnt);
                for (int i = 0; i < cnt; i++)
                {
                    _client.Send(new TdApi.TestSquareInt(123), handler);
                }
            }
        }
    }

    class MyClientResultHandler : Td.ClientResultHandler
    {
        private MainPage _page;

        public MyClientResultHandler(MainPage page)
        {
            _page = page;
        }

        public void OnResult(TdApi.BaseObject obj)
        {
            var str = obj.ToString();
            _page.Print(str);
        }
    }

    class BenchSimpleHandler : Td.ClientResultHandler
    {
        private MainPage _page;
        private int _cnt;

        public BenchSimpleHandler(MainPage page, int cnt)
        {
            _page = page;
            _cnt = cnt;
        }

        public void OnResult(TdApi.BaseObject obj)
        {
            _cnt--;
            if (_cnt == 0)
            {
                _page.Print("DONE");
            }
        }
    }
}
