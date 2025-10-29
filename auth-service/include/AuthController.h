//
// Created by walid on 26/10/2025.
//

#ifndef FILES_SERVICE_AUTHCONTROLLER_H
#define FILES_SERVICE_AUTHCONTROLLER_H

#pragma once
#include <drogon/HttpController.h>
#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

class AuthController : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
      ADD_METHOD_TO(AuthController::registerUser, "/auth/register", drogon::Post);
      ADD_METHOD_TO(AuthController::getUser, "/auth/user", drogon::Get);
      ADD_METHOD_TO(AuthController::updateUser, "/auth/user", drogon::Put);
      ADD_METHOD_TO(AuthController::deleteUser, "/auth/user", drogon::Delete);

    // This method handles user login. There is a link with other API AuthController methods but not directly.
      ADD_METHOD_TO(AuthController::loginUser, "/auth/login", drogon::Post);
    METHOD_LIST_END

    void registerUser(const drogon::HttpRequestPtr& req,
                      std::function<void (const drogon::HttpResponsePtr &)> &&cb);

    void getUser(const drogon::HttpRequestPtr& req,
             std::function<void (const drogon::HttpResponsePtr &)> &&cb);

    void updateUser(const drogon::HttpRequestPtr& req,
                       std::function<void (const drogon::HttpResponsePtr &)> &&cb);

    void deleteUser(const drogon::HttpRequestPtr& req,
                    std::function<void (const drogon::HttpResponsePtr &)> &&cb);

    void loginUser(const drogon::HttpRequestPtr& req,
               std::function<void (const drogon::HttpResponsePtr &)> &&cb);
};

#endif //FILES_SERVICE_AUTHCONTROLLER_H