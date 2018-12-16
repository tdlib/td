local tdlua = require "tdlua"
local serpent = require "serpent"
local function vardump(wut)
    print(serpent.block(wut, {comment=false}))
end
tdlua.setLogLevel(2)
local client = tdlua()
client:send(
    (
        {["@type"] = "getAuthorizationState", ["@extra"] = 1.01234}
    )
)

local api_id = "6"
local api_hash = "eb06d4abfb49dc3eeb1aeb98ae0f581e"
local dbpassword = ""
while true do
    local res = client:receive(1)
    if res then
        vardump(res)
        if type(res) ~= "table" then
            goto continue
        end
        if res["@type"] == "updateAuthorizationState" then
            res = res.authorization_state
        end
        if res["@type"] == "authorizationStateClosed" then
            print("exiting")
            break
        elseif res["@type"] == "authorizationStateWaitTdlibParameters" then
            client:send(
                 {
                    ["@type"] = "setTdlibParameters",
                    parameters = {
                        ["@type"] = "setTdlibParameters",
                        use_message_database = true,
                        api_id = api_id,
                        api_hash = api_hash,
                        system_language_code = "en",
                        device_model = "tdlua",
                        system_version = "unk",
                        application_version = "0.1",
                        enable_storage_optimizer = true,
                        use_pfs = true,
                        database_directory = "./tdlua"
                    }
                }
            )
        elseif res["@type"] == "authorizationStateWaitEncryptionKey" then
            client:send(
                {
                    ["@type"] = "checkDatabaseEncryptionKey",
                    encryption_key = dbpassword
                }
            )
        elseif res["@type"] == "authorizationStateWaitPhoneNumber" then
            print("Do you want to login as a Bot or as an User? [U/b]")
            if io.read() == 'b' then
                print("Enter bot token: ")
                local token = io.read()
                client:send(
                    {
                        ["@type"] = "checkAuthenticationBotToken",
                        token = token
                    }
                )
            else
                print("Enter phone: ")
                local phone = io.read()
                client:send(
                    {
                        ["@type"] = "setAuthenticationPhoneNumber",
                        phone_number = phone
                    }
                )
            end
        elseif res["@type"] == "authorizationStateWaitCode" then
            print("Enter code: ")
            local code = io.read()
            client:send(
                {
                    ["@type"] = "checkAuthenticationCode",
                    code = code
                }
            )
        elseif res["@type"] == "authorizationStateWaitPassword" then
            print("Enter password: ")
            local password = io.read()
            client:send(
                {
                    ["@type"] = "checkAuthenticationPassword",
                    password = password
                }
            )
        elseif res["@type"] == "authorizationStateReady" then
            print("LOGGED IN")
            --os.exit(0)
        end
        ::continue::
    end
end
