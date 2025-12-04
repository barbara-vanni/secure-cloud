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
        int statusCode;              // ex: 201, 400, 401, 500...
        nlohmann::json body;         // payload JSON à renvoyer
    };

    // Crée une conversation :
    // - récupère l'utilisateur courant via /auth/v1/user
    // - récupère le profil via /rest/v1/profiles
    // - crée la conversation via /rest/v1/conversations
    Result createConversation(
        const std::string& accessToken,
        const std::string& type,
        const std::optional<std::string>& name
    );
};

#endif //SECURE_CLOUD_CONVERSATIONSERVICE_H