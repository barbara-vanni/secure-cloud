//
// Created by walid on 02/12/2025.
//

#ifndef SECURE_CLOUD_CONVERSATIONCONTROLLER_H
#define SECURE_CLOUD_CONVERSATIONCONTROLLER_H

#pragma once
#include <drogon/HttpController.h>
#include <drogon/drogon.h>

class ConversationController final
    : public drogon::HttpController<ConversationController> {
public:
    METHOD_LIST_BEGIN
      // POST /conversations
      ADD_METHOD_TO(ConversationController::createConversation,
                    "/conversations", drogon::Post);
    METHOD_LIST_END

    void createConversation(const drogon::HttpRequestPtr& req,
                            std::function<void (const drogon::HttpResponsePtr &)> &&cb) const;
};

#endif //SECURE_CLOUD_CONVERSATIONCONTROLLER_H