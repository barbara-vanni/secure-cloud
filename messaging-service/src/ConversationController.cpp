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

} // namespace

void ConversationController::createConversation(
    const drogon::HttpRequestPtr& req,
    std::function<void (const drogon::HttpResponsePtr &)> &&cb) const {

    // 1) Récupérer et valider le body JSON
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

    // 2) Récupérer le token Bearer
    const auto token = getBearerToken(req);
    if (token.empty()) {
        return cb(makeJsonError(k401Unauthorized, "Missing Bearer access token"));
    }

    // 3) Appeler le service métier
    ConversationService service;
    auto result = service.createConversation(
        token,
        type,
        name
    );

    // 4) Construire la réponse HTTP à partir du résultat
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<HttpStatusCode>(result.statusCode));
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(result.body.dump());

    return cb(resp);
}