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

    // GET /conversations → list all conversations of the current user
    ADD_METHOD_TO(ConversationController::listConversations,
                  "/conversations", drogon::Get);

    // GET /conversations/{id} → conversation details by ID
    ADD_METHOD_TO(ConversationController::getConversation,
                  "/conversations/{id}", drogon::Get);

    // PATCH /conversations/{id} → update conversation (only name if you're owner )
    ADD_METHOD_TO(ConversationController::updateConversation,
              "/conversations/{id}", drogon::Patch);

    //  DELETE /conversations/{id} → delete conversation (only if you're owner)
    ADD_METHOD_TO(ConversationController::deleteConversation,
    "/conversations/{id}", drogon::Delete);

    // POST /conversations/{id}/members → add a user as member
    ADD_METHOD_TO(ConversationController::addMember,
                  "/conversations/{id}/members", drogon::Post);

    // GET /conversations/{id}/members → list members of a conversation
    ADD_METHOD_TO(ConversationController::listMembers,
                  "/conversations/{id}/members", drogon::Get);

    // PATCH /conversations/{id}/members/{userId} → update member role
    ADD_METHOD_TO(ConversationController::updateMemberRole,
                  "/conversations/{id}/members/{userId}", drogon::Patch);


    // DELETE /conversations/{id}/members/{userId} → remove a member from a conversation
    ADD_METHOD_TO(ConversationController::deleteMember,
                  "/conversations/{id}/members/{userId}", drogon::Delete);

    METHOD_LIST_END

    void createConversation(const drogon::HttpRequestPtr& req,
                            std::function<void (const drogon::HttpResponsePtr &)> &&cb) const;

    void listConversations(const drogon::HttpRequestPtr& req,
                       std::function<void (const drogon::HttpResponsePtr &)> &&cb) const;

    void getConversation(const drogon::HttpRequestPtr& req,
                         std::function<void (const drogon::HttpResponsePtr &)> &&cb,
                         const std::string& conversationId) const;

    void updateConversation(const drogon::HttpRequestPtr& req,
                        std::function<void (const drogon::HttpResponsePtr &)> &&cb,
                        const std::string& conversationId) const;

    void deleteConversation(const drogon::HttpRequestPtr& req,
                        std::function<void (const drogon::HttpResponsePtr &)> &&cb,
                        const std::string& conversationId) const;

    void addMember(const drogon::HttpRequestPtr& req,
                   std::function<void (const drogon::HttpResponsePtr &)> &&cb,
                   const std::string& conversationId) const;

    void listMembers(const drogon::HttpRequestPtr& req,
                     std::function<void (const drogon::HttpResponsePtr &)> &&cb,
                     const std::string& conversationId) const;

    void updateMemberRole(const drogon::HttpRequestPtr& req,
                      std::function<void (const drogon::HttpResponsePtr &)> &&cb,
                      const std::string& conversationId,
                      const std::string& userId) const;

    void deleteMember(const drogon::HttpRequestPtr& req,
                  std::function<void (const drogon::HttpResponsePtr &)> &&cb,
                  const std::string& conversationId,
                  const std::string& userId) const;
};

#endif //SECURE_CLOUD_CONVERSATIONCONTROLLER_H