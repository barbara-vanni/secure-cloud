//
// Created by walid on 02/12/2025.
//

#include "../include/ConversationController.h"
#include <curl/curl.h>
#include <json/json.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <stdexcept>
#include <string>

using nlohmann::json;
using namespace drogon;

namespace {

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string getBearerToken(const HttpRequestPtr& req) {
    auto h = req->getHeader("authorization");
    if (h.empty()) h = req->getHeader("Authorization");
    if (h.rfind("Bearer ", 0) == 0) return h.substr(7);
    return {};
}

HttpResponsePtr badRequest(const std::string& msg) {
    Json::Value j; j["error"] = msg;
    auto r = HttpResponse::newHttpJsonResponse(j);
    r->setStatusCode(k400BadRequest);
    return r;
}

HttpResponsePtr unauthorized(const std::string& msg) {
    Json::Value j; j["error"] = msg;
    auto r = HttpResponse::newHttpJsonResponse(j);
    r->setStatusCode(k401Unauthorized);
    return r;
}

HttpResponsePtr serverError(const std::string& msg) {
    Json::Value j; j["error"] = msg;
    auto r = HttpResponse::newHttpJsonResponse(j);
    r->setStatusCode(k500InternalServerError);
    return r;
}

}

void ConversationController::createConversation(
    const drogon::HttpRequestPtr& req,
    std::function<void (const drogon::HttpResponsePtr &)> &&cb) const {

    try {
        // 1) Validation body JSON
        const auto body = req->getJsonObject();
        if (!body) {
            return cb(badRequest("Body must be JSON"));
        }
        if (!body->isMember("type") || !(*body)["type"].isString()) {
            return cb(badRequest("Field 'type' is required and must be a string ('direct'|'group')"));
        }

        const std::string type = (*body)["type"].asString();
        if (type != "direct" && type != "group") {
            return cb(badRequest("Field 'type' must be 'direct' or 'group'"));
        }

        std::string name;
        if (body->isMember("name") && (*body)["name"].isString()) {
            name = (*body)["name"].asString();
        }

        const auto token = getBearerToken(req);
        if (token.empty()) {
            return cb(unauthorized("Missing Bearer access token"));
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) {
            return cb(serverError("Missing SUPABASE_URL/ANON_KEY"));
        }

        std::string meUrl = std::string(base) + "/auth/v1/user";
        CURL* curl = curl_easy_init();
        if (!curl) return cb(serverError("curl init failed"));

        std::string response; long code = 0;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("apikey: " + std::string(anon)).c_str());
        headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, meUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        auto cres = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (cres != CURLE_OK) {
            return cb(serverError("curl perform failed (/auth/v1/user)"));
        }
        if (code != 200) {
            auto r = drogon::HttpResponse::newHttpResponse();
            r->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
            r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            r->setBody(response.empty() ? "{}" : response);
            return cb(r);
        }

        auto j = nlohmann::json::parse(response, nullptr, false);
        if (j.is_discarded() || !j.contains("id")) {
            return cb(serverError("Cannot extract user id from Supabase response"));
        }
        const std::string authUserId = j["id"].get<std::string>();

        std::string profileUrl =
            std::string(base) +
            "/rest/v1/profiles?select=id&auth_id=eq." + authUserId + "&limit=1";

        CURL* cProf = curl_easy_init();
        if (!cProf) return cb(serverError("curl init failed (profiles)"));

        std::string profResp; long profCode = 0;
        struct curl_slist* hProf = nullptr;
        hProf = curl_slist_append(hProf, ("apikey: " + std::string(anon)).c_str());
        hProf = curl_slist_append(hProf, ("Authorization: Bearer " + token).c_str());
        hProf = curl_slist_append(hProf, "Content-Type: application/json");

        curl_easy_setopt(cProf, CURLOPT_URL, profileUrl.c_str());
        curl_easy_setopt(cProf, CURLOPT_HTTPHEADER, hProf);
        curl_easy_setopt(cProf, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(cProf, CURLOPT_WRITEDATA, &profResp);

        auto resProf = curl_easy_perform(cProf);
        curl_easy_getinfo(cProf, CURLINFO_RESPONSE_CODE, &profCode);
        curl_slist_free_all(hProf);
        curl_easy_cleanup(cProf);

        if (resProf != CURLE_OK) {
            return cb(serverError("curl perform failed (profiles)"));
        }
        if (profCode != 200) {
            auto r = drogon::HttpResponse::newHttpResponse();
            r->setStatusCode(static_cast<drogon::HttpStatusCode>(profCode));
            r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            r->setBody(profResp.empty() ? "{}" : profResp);
            return cb(r);
        }

        auto jp = nlohmann::json::parse(profResp, nullptr, false);
        if (jp.is_discarded() || !jp.is_array() || jp.empty() || !jp[0].contains("id")) {
            return cb(badRequest("No profile found for the current authenticated user"));
        }
        const std::string profileId = jp[0]["id"].get<std::string>();

        std::string convUrl = std::string(base) + "/rest/v1/conversations";

        nlohmann::json convPayload;
        if (!name.empty()) convPayload["name"] = name;
        convPayload["type"]       = type;
        convPayload["created_by"] = profileId;

        const std::string convBody = convPayload.dump();

        CURL* cConv = curl_easy_init();
        if (!cConv) return cb(serverError("curl init failed (conversations)"));

        std::string convResp; long convCode = 0;
        struct curl_slist* hConv = nullptr;
        hConv = curl_slist_append(hConv, ("apikey: " + std::string(anon)).c_str());
        hConv = curl_slist_append(hConv, ("Authorization: Bearer " + token).c_str());
        hConv = curl_slist_append(hConv, "Content-Type: application/json");
        hConv = curl_slist_append(hConv, "Prefer: return=representation");

        curl_easy_setopt(cConv, CURLOPT_URL, convUrl.c_str());
        curl_easy_setopt(cConv, CURLOPT_HTTPHEADER, hConv);
        curl_easy_setopt(cConv, CURLOPT_POST, 1L);
        curl_easy_setopt(cConv, CURLOPT_POSTFIELDS, convBody.c_str());
        curl_easy_setopt(cConv, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(cConv, CURLOPT_WRITEDATA, &convResp);

        auto resConv = curl_easy_perform(cConv);
        curl_easy_getinfo(cConv, CURLINFO_RESPONSE_CODE, &convCode);
        curl_slist_free_all(hConv);
        curl_easy_cleanup(cConv);

        if (resConv != CURLE_OK) {
            return cb(serverError("curl perform failed (create conversation)"));
        }

        if (convCode != 201 && convCode != 200) {
            auto r = drogon::HttpResponse::newHttpResponse();
            r->setStatusCode(static_cast<drogon::HttpStatusCode>(convCode));
            r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            r->setBody(convResp.empty() ? "{}" : convResp);
            return cb(r);
        }

        auto jc = nlohmann::json::parse(convResp, nullptr, false);
        if (jc.is_discarded()) {
            return cb(serverError("Cannot parse conversation response from Supabase"));
        }

        nlohmann::json obj = jc;
        if (jc.is_array() && !jc.empty()) {
            obj = jc[0];
        }

        auto r = drogon::HttpResponse::newHttpResponse();
        r->setStatusCode(drogon::k201Created);
        r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        r->setBody(obj.dump());
        return cb(r);

    } catch (const std::exception& e) {
        return cb(serverError(e.what()));
    }
}