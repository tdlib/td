//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

import Foundation

// TDLib Client Swift binding
class TdClient {
    typealias Client = UnsafeMutableRawPointer
    var client = td_json_client_create()!
    let tdlibMainLoop = DispatchQueue(label: "TDLib")
    let tdlibQueryQueue = DispatchQueue(label: "TDLibQuery")
    var queryF = Dictionary<Int64, (Dictionary<String,Any>)->()>()
    var updateF: ((Dictionary<String,Any>)->())?
    var queryId: Int64 = 0

    func queryAsync(query: [String: Any], f: ((Dictionary<String,Any>)->())? = nil) {
        tdlibQueryQueue.async {
            var newQuery = query

            if f != nil {
                let nextQueryId = self.queryId + 1
                newQuery["@extra"] = nextQueryId
                self.queryF[nextQueryId] = f
                self.queryId = nextQueryId
            }
            td_json_client_send(self.client, to_json(newQuery))
        }
    }

    func querySync(query: [String: Any]) -> Dictionary<String,Any> {
        let semaphore = DispatchSemaphore(value:0)
        var result = Dictionary<String,Any>()
        queryAsync(query: query) {
            result = $0
            semaphore.signal()
        }
        semaphore.wait()
        return result
    }

    init() {
    }

    deinit {
        td_json_client_destroy(client)
    }

    func run(updateHandler: @escaping (Dictionary<String,Any>)->()) {
        updateF = updateHandler
        tdlibMainLoop.async { [weak self] in
            while (true) {
                if let s = self {
                    if let res = td_json_client_receive(s.client, 10) {
                        let event = String(cString: res)
                        s.queryResultAsync(event)
                    }
                } else {
                    break
                }
            }
        }
    }

    private func queryResultAsync(_ result: String) {
        tdlibQueryQueue.async {
            let json = try? JSONSerialization.jsonObject(with: result.data(using: .utf8)!, options:[])
            if let dictionary = json as? [String:Any] {
                if let extra = dictionary["@extra"] as? Int64 {
                    let index = self.queryF.index(forKey: extra)!
                    self.queryF[index].value(dictionary)
                    self.queryF.remove(at: index)
                } else {
                    self.updateF!(dictionary)
                }
            }
        }
    }
}

func to_json(_ obj: Any) -> String {
    do {
        let obj = try JSONSerialization.data(withJSONObject: obj)
        return String(data: obj, encoding: .utf8)!
    } catch {
        return ""
    }
}


// An example of usage
td_set_log_verbosity_level(1);

var client = TdClient()

func myReadLine() -> String {
    while (true) {
        if let line = readLine() {
            return line
        }
    }
}

func updateAuthorizationState(authorizationState: Dictionary<String, Any>) {
    switch(authorizationState["@type"] as! String) {
        case "authorizationStateWaitTdlibParameters":
            client.queryAsync(query:[
                "@type":"setTdlibParameters",
                "parameters":[
                    "database_directory":"tdlib",
                    "use_message_database":true,
                    "use_secret_chats":true,
                    "api_id":94575,
                    "api_hash":"a3406de8d171bb422bb6ddf3bbd800e2",
                    "system_language_code":"en",
                    "device_model":"Desktop",
                    "system_version":"Unknown",
                    "application_version":"1.0",
                    "enable_storage_optimizer":true
                ]
            ]);

        case "authorizationStateWaitEncryptionKey":
            client.queryAsync(query: ["@type":"checkDatabaseEncryptionKey", "key":"cucumber"])

        case "authorizationStateWaitPhoneNumber":
            print("Enter your phone: ")
            let phone = myReadLine()
            client.queryAsync(query:["@type":"setAuthenticationPhoneNumber", "phone_number":phone], f:checkAuthenticationError)

        case "authorizationStateWaitCode":
            var first_name: String = ""
            var last_name: String = ""
            var code: String = ""
            if let is_registered = authorizationState["is_registered"] as? Bool, is_registered {
            } else {
                print("Enter your first name: ")
                first_name = myReadLine()
                print("Enter your last name: ")
                last_name = myReadLine()
            }
            print("Enter (SMS) code: ")
            code = myReadLine()
            client.queryAsync(query:["@type":"checkAuthenticationCode", "code":code, "first_name":first_name, "last_name":last_name], f:checkAuthenticationError)

        case "authorizationStateWaitPassword":
            print("Enter password: ")
            let password = myReadLine()
            client.queryAsync(query:["@type":"checkAuthenticationPassword", "password":password], f:checkAuthenticationError)

        case "authorizationStateReady":
            ()

        default:
            assert(false, "TODO: Unknown authorization state");
    }
}

func checkAuthenticationError(error: Dictionary<String, Any>) {
    if (error["@type"] as! String == "error") {
        client.queryAsync(query:["@type":"getAuthorizationState"], f:updateAuthorizationState)
    }
}

client.run {
    let update = $0
    print(update)
    if update["@type"] as! String == "updateAuthorizationState" {
        updateAuthorizationState(authorizationState: update["authorization_state"] as! Dictionary<String, Any>)
    }
}

while true {
    sleep(1)
}
