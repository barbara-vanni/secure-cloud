//
// Created by walid on 02/12/2025.
//

#include "../include/ConversationController.h"
#include "../include/ConversationService.h"

#include <json/json.h>
#include <optional>
#include <string>

using namespace drogon;

namespace {

std::string getBearerToken(const HttpRequestPtr& req) {
    auto h = req->getHeader("authorization");
    if (h.empty()) h = req->getHeader("Authorization");
    if (h.rfind("Bearer ", 0) == 0) return h.substr(7);
    return {};
}

HttpResponsePtr makeJsonError(HttpStatusCode code, const std::string& msg) {
    Json::Value j;
    j["error"] = msg;
    auto r = HttpResponse::newHttpJsonResponse(j);
    r->setStatusCode(code);
    return r;
}

HttpResponsePtr badRequest(const std::string& msg) {
    return makeJsonError(drogon::k400BadRequest, msg);
}

HttpResponsePtr unauthorized(const std::string& msg) {
    return makeJsonError(drogon::k401Unauthorized, msg);
}

} // namespace

void ConversationController::createConversation(
    const drogon::HttpRequestPtr& req,
    std::function<void (const drogon::HttpResponsePtr &)> &&cb) const {

    // 1) Receive and validate the JSON body
    const auto body = req->getJsonObject();
    if (!body) {
        return cb(makeJsonError(k400BadRequest, "Body must be JSON"));
    }

    if (!body->isMember("type") || !(*body)["type"].isString()) {
        return cb(makeJsonError(
            k400BadRequest,
            "Field 'type' is required and must be a string ('direct'|'group')"
        ));
    }

    const std::string type = (*body)["type"].asString();
    if (type != "direct" && type != "group") {
        return cb(makeJsonError(
            k400BadRequest,
            "Field 'type' must be 'direct' or 'group'"
        ));
    }

    std::optional<std::string> name;
    if (body->isMember("name") && (*body)["name"].isString()) {
        name = (*body)["name"].asString();
    }

    // 2) Take the Bearer token from the Authorization header
    const auto token = getBearerToken(req);
    if (token.empty()) {
        return cb(makeJsonError(k401Unauthorized, "Missing Bearer access token"));
    }

    // 3) Call the business service
    ConversationService service;
    auto result = service.createConversation(
        token,
        type,
        name
    );

    // 4) Build the HTTP response from the result
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<HttpStatusCode>(result.statusCode));
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(result.body.dump());

    return cb(resp);
}

void ConversationController::listConversations(
    const drogon::HttpRequestPtr& req,
    std::function<void (const drogon::HttpResponsePtr &)> &&cb) const {

    const auto token = getBearerToken(req);
    if (token.empty()) {
        return cb(makeJsonError(k401Unauthorized, "Missing Bearer access token"));
    }

    ConversationService service;
    auto result = service.listMyConversations(token);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<HttpStatusCode>(result.statusCode));
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(result.body.dump());
    return cb(resp);
}

void ConversationController::getConversation(
    const drogon::HttpRequestPtr& req,
    std::function<void (const drogon::HttpResponsePtr &)> &&cb,
    const std::string& conversationId) const {

    const auto token = getBearerToken(req);
    if (token.empty()) {
        return cb(makeJsonError(k401Unauthorized, "Missing Bearer access token"));
    }

    if (conversationId.empty()) {
        return cb(makeJsonError(k400BadRequest, "Missing conversation id"));
    }

    ConversationService service;
    auto result = service.getConversationById(token, conversationId);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<HttpStatusCode>(result.statusCode));
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(result.body.dump());
    return cb(resp);
}

void ConversationController::updateConversation(
    const drogon::HttpRequestPtr& req,
    std::function<void (const drogon::HttpResponsePtr &)> &&cb,
    const std::string& conversationId) const {

    const auto token = getBearerToken(req);
    if (token.empty()) {
        return cb(unauthorized("Missing Bearer access token"));
    }

    if (conversationId.empty()) {
        return cb(badRequest("Missing conversation id"));
    }

    const auto body = req->getJsonObject();
    if (!body) {
        return cb(badRequest("Body must be JSON"));
    }

    std::optional<std::string> name;
    if (body->isMember("name")) {
        if (!(*body)["name"].isString()) {
            return cb(badRequest("Field 'name' must be a string"));
        }
        name = (*body)["name"].asString();
    }

    if (!name.has_value()) {
        return cb(badRequest("Nothing to update (expecting at least 'name')"));
    }

    ConversationService service;
    auto result = service.updateConversation(token, conversationId, name);

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.statusCode));
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(result.body.dump());
    return cb(resp);
}

void ConversationController::deleteConversation(
    const drogon::HttpRequestPtr& req,
    std::function<void (const drogon::HttpResponsePtr &)> &&cb,
    const std::string& conversationId) const {

    const auto token = getBearerToken(req);
    if (token.empty()) {
        return cb(unauthorized("Missing Bearer access token"));
    }

    if (conversationId.empty()) {
        return cb(badRequest("Missing conversation id"));
    }

    ConversationService service;
    auto result = service.deleteConversation(token, conversationId);

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.statusCode));
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(result.body.dump());
    return cb(resp);
}