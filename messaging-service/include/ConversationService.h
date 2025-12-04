//
// Created by walid on 03/12/2025.
//

#ifndef SECURE_CLOUD_CONVERSATIONSERVICE_H
#define SECURE_CLOUD_CONVERSATIONSERVICE_H

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

class ConversationService {
public:
    struct Result {
        int statusCode;
        nlohmann::json body;
    };

    // Create a new conversation
    Result createConversation(
        const std::string& accessToken,
        const std::string& type,
        const std::optional<std::string>& name
    );

    // List all conversations where the current user is a member
    Result listMyConversations(
        const std::string& accessToken
    );

    // take the conversationId as parameter
    Result getConversationById(
        const std::string& accessToken,
        const std::string& conversationId
    );
};

#endif //SECURE_CLOUD_CONVERSATIONSERVICE_H