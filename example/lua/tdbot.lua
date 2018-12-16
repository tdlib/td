--[[
    tdbot / tdcli retrocompatibility script
    put this file into your bot folder and append in the end of the main bot file
    require "tdbot"

    examle:
    file bot.lua:
        function tdbot_update_callback(data)
            ...
        end
    become
        function tdbot_update_callback(data)
            ...
        end
        require "tdbot"
    the script will start a loop and register the function callback, it will handle everything (if it work) throw tdlua
--]]
local tdlua = require "tdlua"
local serpent = require "serpent"
local function vardump(wut)
    print(serpent.block(wut, {comment=false}))
end
local api_id = "6"
local api_hash = "eb06d4abfb49dc3eeb1aeb98ae0f581e"
local dbpassword = ""
tdlua.setLogLevel(0)
local client = tdlua()
client:send(
    (
        {["@type"] = "getAuthorizationState"}
    )
)
local ready = false

local function oldtonew(t)
  if type(t) ~= "table" then return t end
  for k, v in pairs(t) do
    if type(v) == "table" then
      oldtonew(v)
    end
  end
  if not t["@type"] then
    t["@type"] = t._
  end
  return t
end

local function newtoold(t)
  if type(t) ~= "table" then return t end
  for k, v in pairs(t) do
    if type(v) == "table" then
      newtoold(v)
    end
  end
  if t["@type"] then
    t._ = t["@type"]
  end
  return t
end

local function authstate(state)
    if state["@type"] == "authorizationStateClosed" then
        os.exit(0)
    elseif state["@type"] == "authorizationStateWaitTdlibParameters" then
        client:send({
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
    elseif state["@type"] == "authorizationStateWaitEncryptionKey" then
        client:send({
                ["@type"] = "checkDatabaseEncryptionKey",
                encryption_key = dbpassword
            }
        )
    elseif state["@type"] == "authorizationStateWaitPhoneNumber" then
        print("Do you want to login as a Bot or as an User? [U/b]")
        if io.read() == 'b' then
            print("Enter bot token: ")
            local token = io.read()
            client:send({
                    ["@type"] = "checkAuthenticationBotToken",
                    token = token
                }
            )
        else
            print("Enter phone: ")
            local phone = io.read()
            client:send({
                    ["@type"] = "setAuthenticationPhoneNumber",
                    phone_number = phone
                }
            )
        end
    elseif state["@type"] == "authorizationStateWaitCode" then
        print("Enter code: ")
        local code = io.read()
        client:send({
                ["@type"] = "checkAuthenticationCode",
                code = code
            }
        )
    elseif state["@type"] == "authorizationStateWaitPassword" then
        print("Enter password: ")
        local password = io.read()
        client:send({
                ["@type"] = "checkAuthenticationPassword",
                password = password
            }
        )
    elseif state["@type"] == "authorizationStateReady" then
        ready = true
        print("ready")
        --os.exit(0)
    end
end

local function err(e)
  return e .. " " .. debug.traceback()
end

local function _call(params, cb, extra)
    local res = client:execute(params)
    if type(cb) == "function" then
        if type(res) == "table" then
            local ok, rres = xpcall(cb, err, extra, res)
            if not ok then
                print("Result cb failed", rres, debug.traceback())
                --vardump(res)
                return false
            end
            return ok
        end
    end
end

tdbot_function = _call
tdcli_function = _call

client = setmetatable({_client = client,}, {__index = function(self, call, ...) return function(self, params) return newtoold(self._client[call](self._client, type(params) == "table" and oldtonew(params) or params)) end end})

while true do
    local callback = tdbot_update_callback or tdcli_update_callback or vardump
    local res = client:receive(1)
    if res then
        if type(res) ~= "table" then
            goto continue
        end
        if not ready or res["@type"] == "updateAuthorizationState" then
            authstate(res.authorization_state and res.authorization_state or res)
            goto continue
        end
        if res["@type"] == "connectionStateUpdating" then
          goto continue
        end
        local ok, rres = xpcall(callback, err, res)
        if not ok then
          print("Update cb failed", rres)
          --vardump(res)
        end
        ::continue::
    end
end
