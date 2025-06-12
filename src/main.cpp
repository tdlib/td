#include <td/telegram/td_json_client.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

void send_query(void* client, const std::string& query) {
    td_json_client_send(client, query.c_str());
}

std::string receive_response(void* client) {
    const double timeout = 1.0;
    const char* result = td_json_client_receive(client, timeout);
    return result ? result : "";
}

int main() {
    void* client = td_json_client_create();

    send_query(client, R"({
        "@type": "setLogVerbosityLevel",
        "new_verbosity_level": 1
    })");

    send_query(client, R"({
        "@type": "getAuthorizationState"
    })");

    while (true) {
        std::string result = receive_response(client);
        if (!result.empty()) {
            std::cout << "Received: " << result << std::endl;
            if (result.find("authorizationStateWaitPhoneNumber") != std::string::npos) {
                send_query(client, R"({
                    "@type": "setAuthenticationPhoneNumber",
                    "phone_number": "+79991234567"
                })");
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    td_json_client_destroy(client);
    return 0;
}

