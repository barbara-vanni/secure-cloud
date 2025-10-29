//
// Created by walid on 26/10/2025.
//

#include "../include/AuthController.h"
#include <curl/curl.h>
#include <json/json.h>
#include <cstdlib>
#include <stdexcept>
#include <string>

using nlohmann::json;

namespace {
size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

struct HttpResult { long code{0}; std::string body; };

// Send the payload already built (include 'data' if present)  to Supabase
HttpResult supabaseSignup(const json& payload) {
    const char* url = std::getenv("SUPABASE_URL");
    const char* anonKey = std::getenv("SUPABASE_ANON_KEY");
    if (!url || !anonKey) throw std::runtime_error("Missing SUPABASE_URL/ANON_KEY");

    std::string endpoint = std::string(url) + "/auth/v1/signup";

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl init failed");

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("apikey: " + std::string(anonKey)).c_str());
    headers = curl_slist_append(headers, ("Authorization: Bearer " + std::string(anonKey)).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    const auto body = payload.dump();

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    auto res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) throw std::runtime_error("curl perform failed");
    return {code, response};
}
} // namespace


// --- Common helpers ---
static std::string getBearerToken(const drogon::HttpRequestPtr& req) {
    auto h = req->getHeader("authorization");
    if (h.empty()) h = req->getHeader("Authorization");
    if (h.rfind("Bearer ", 0) == 0) return h.substr(7);
    return {};
}

static drogon::HttpResponsePtr makeJson(int code, const nlohmann::json& j) {
    auto r = drogon::HttpResponse::newHttpResponse();
    r->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
    r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    r->setBody(j.dump());
    return r;
}

// --- Handler /auth/register ---
void AuthController::registerUser(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    try {
        // Drogon gives a JsonCPP (Json::Value)
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("email") || !body->isMember("password")) {
            Json::Value err;
            err["error"] = "email and password are required";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err);
            r->setStatusCode(drogon::k400BadRequest);
            return cb(r);
        }

        const auto email = (*body)["email"].asString();
        const auto password = (*body)["password"].asString();

        // Build Supabase payload : email/password (+ optional meta in "data")
        json signupPayload = {
            {"email", email},
            {"password", password}
        };

        // Optional profiles fields to be copied by trigger from raw_user_meta_data
        json meta;
        if (body->isMember("first_name")) meta["first_name"] = (*body)["first_name"].asString();
        if (body->isMember("last_name"))  meta["last_name"]  = (*body)["last_name"].asString();
        if (body->isMember("state"))      meta["state"]      = (*body)["state"].asString();
        if (!meta.empty()) {
            signupPayload["data"] = meta;  // => ira dans auth.users.raw_user_meta_data
        }

        // Supabase signup
        const auto sup = supabaseSignup(signupPayload);

        // Supabase response to client
        auto r = drogon::HttpResponse::newHttpResponse();
        r->setStatusCode(
            (sup.code >= 200 && sup.code < 300)
                ? drogon::k201Created
                : drogon::HttpStatusCode(sup.code)
        );
        r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        r->setBody(sup.body.empty() ? "{}" : sup.body);
        return cb(r);

    } catch (const std::exception& e) {
        Json::Value err;
        err["error"] = e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        return cb(r);
    }
}

// Get current user info
void AuthController::getUser(const drogon::HttpRequestPtr& req,
                             std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    try {
        const auto token = getBearerToken(req);
        if (token.empty()) {
            Json::Value err; err["error"] = "Missing Bearer access token";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err);
            r->setStatusCode(drogon::k401Unauthorized);
            return cb(r);
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) throw std::runtime_error("Missing SUPABASE_URL/ANON_KEY");

        std::string url = std::string(base) + "/auth/v1/user";

        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl init failed");

        std::string response;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("apikey: " + std::string(anon)).c_str());
        headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        auto res = curl_easy_perform(curl);
        long code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers); curl_easy_cleanup(curl);
        if (res != CURLE_OK) throw std::runtime_error("curl perform failed");

        auto r = drogon::HttpResponse::newHttpResponse();
        r->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
        r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        r->setBody(response.empty() ? "{}" : response);
        return cb(r);
    } catch (const std::exception& e) {
        Json::Value err; err["error"] = e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        return cb(r);
    }
}

// Update user profile (profiles table)
void AuthController::updateUser(const drogon::HttpRequestPtr& req,
                                   std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    try {
        const auto token = getBearerToken(req);
        if (token.empty()) {
            Json::Value err; err["error"] = "Missing Bearer access token";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err);
            r->setStatusCode(drogon::k401Unauthorized);
            return cb(r);
        }
        const auto body = req->getJsonObject();
        if (!body) {
            Json::Value err; err["error"] = "Missing JSON body";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err);
            r->setStatusCode(drogon::k400BadRequest);
            return cb(r);
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) throw std::runtime_error("Missing SUPABASE_URL/ANON_KEY");

        // 1) Get current user to know its id
        {
            std::string url = std::string(base) + "/auth/v1/user";
            CURL* curl = curl_easy_init(); if (!curl) throw std::runtime_error("curl init failed");
            std::string resp; long code = 0;
            struct curl_slist* hdr = nullptr;
            hdr = curl_slist_append(hdr, ("apikey: " + std::string(anon)).c_str());
            hdr = curl_slist_append(hdr, ("Authorization: Bearer " + token).c_str());
            hdr = curl_slist_append(hdr, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
            auto res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            curl_slist_free_all(hdr); curl_easy_cleanup(curl);
            if (res != CURLE_OK) throw std::runtime_error("curl perform failed");
            if (code != 200) {
                auto r = drogon::HttpResponse::newHttpResponse();
                r->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
                r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                r->setBody(resp.empty() ? "{}" : resp);
                return cb(r);
            }
            auto j = nlohmann::json::parse(resp);
            if (!j.contains("id")) throw std::runtime_error("Cannot extract user id");
            const std::string userId = j["id"].get<std::string>();

            // 2) PATCH profiles
            std::string url2 = std::string(base) + "/rest/v1/profiles?auth_id=eq." + userId;
            nlohmann::json upd;

            if (body->isMember("first_name")) upd["first_name"] = (*body)["first_name"].asString();
            if (body->isMember("last_name"))  upd["last_name"]  = (*body)["last_name"].asString();
            if (body->isMember("state"))      upd["state"]      = (*body)["state"].asString();

            CURL* c2 = curl_easy_init(); if (!c2) throw std::runtime_error("curl init failed");
            std::string resp2; long code2 = 0;
            struct curl_slist* h2 = nullptr;
            h2 = curl_slist_append(h2, ("apikey: " + std::string(anon)).c_str());
            h2 = curl_slist_append(h2, ("Authorization: Bearer " + token).c_str());
            h2 = curl_slist_append(h2, "Content-Type: application/json");
            h2 = curl_slist_append(h2, "Prefer: return=representation");

            auto bodyJson = upd.dump();
            curl_easy_setopt(c2, CURLOPT_URL, url2.c_str());
            curl_easy_setopt(c2, CURLOPT_HTTPHEADER, h2);
            curl_easy_setopt(c2, CURLOPT_CUSTOMREQUEST, "PATCH"); // PUT externe, PATCH REST
            curl_easy_setopt(c2, CURLOPT_POSTFIELDS, bodyJson.c_str());
            curl_easy_setopt(c2, CURLOPT_WRITEFUNCTION, writeCb);
            curl_easy_setopt(c2, CURLOPT_WRITEDATA, &resp2);
            auto res2 = curl_easy_perform(c2);
            curl_easy_getinfo(c2, CURLINFO_RESPONSE_CODE, &code2);
            curl_slist_free_all(h2); curl_easy_cleanup(c2);
            if (res2 != CURLE_OK) throw std::runtime_error("curl perform failed");

            auto r = drogon::HttpResponse::newHttpResponse();
            r->setStatusCode(static_cast<drogon::HttpStatusCode>(code2));
            r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            r->setBody(resp2.empty() ? "{}" : resp2);
            return cb(r);
        }
    } catch (const std::exception& e) {
        Json::Value err; err["error"] = e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        return cb(r);
    }
}

// Delete user account
void AuthController::deleteUser(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    try {
        const char* base = std::getenv("SUPABASE_URL");
        const char* svc  = std::getenv("SUPABASE_SERVICE_ROLE");
        if (!base || !svc) throw std::runtime_error("Missing SUPABASE_URL/SERVICE_ROLE");

        // 1) Recover user id (either way)
        std::string userId;
        const auto body = req->getJsonObject();
        if (body && body->isMember("id")) {
            userId = (*body)["id"].asString();
        } else {
            // si pas d'id fourni, essayer via access token
            const auto token = getBearerToken(req);
            if (token.empty()) {
                Json::Value err; err["error"] = "Provide user id in body or Bearer token";
                auto r = drogon::HttpResponse::newHttpJsonResponse(err);
                r->setStatusCode(drogon::k400BadRequest);
                return cb(r);
            }
            // /auth/v1/user to get user id
            std::string meUrl = std::string(base) + "/auth/v1/user";
            CURL* c1 = curl_easy_init(); if (!c1) throw std::runtime_error("curl init failed");
            std::string me; long c = 0;
            struct curl_slist* h1 = nullptr;
            h1 = curl_slist_append(h1, ("apikey: " + std::string(svc)).c_str()); // ok avec service role
            h1 = curl_slist_append(h1, ("Authorization: Bearer " + token).c_str());
            h1 = curl_slist_append(h1, "Content-Type: application/json");
            curl_easy_setopt(c1, CURLOPT_URL, meUrl.c_str());
            curl_easy_setopt(c1, CURLOPT_HTTPHEADER, h1);
            curl_easy_setopt(c1, CURLOPT_WRITEFUNCTION, writeCb);
            curl_easy_setopt(c1, CURLOPT_WRITEDATA, &me);
            auto res1 = curl_easy_perform(c1);
            curl_easy_getinfo(c1, CURLINFO_RESPONSE_CODE, &c);
            curl_slist_free_all(h1); curl_easy_cleanup(c1);
            if (res1 != CURLE_OK || c != 200) throw std::runtime_error("Cannot resolve user id");
            auto j = nlohmann::json::parse(me);
            userId = j.value<std::string>("id", "");
            if (userId.empty()) throw std::runtime_error("Empty user id");
        }

        // 2) DELETE admin
        std::string delUrl = std::string(base) + "/auth/v1/admin/users/" + userId;
        CURL* curl = curl_easy_init(); if (!curl) throw std::runtime_error("curl init failed");
        std::string response; long code = 0;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("apikey: " + std::string(svc)).c_str());
        headers = curl_slist_append(headers, ("Authorization: Bearer " + std::string(svc)).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, delUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        auto res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers); curl_easy_cleanup(curl);
        if (res != CURLE_OK) throw std::runtime_error("curl perform failed");

        // 204 No Content on success
        auto r = drogon::HttpResponse::newHttpResponse();
        r->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
        r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        r->setBody(response.empty() ? "{}" : response);
        return cb(r);
    } catch (const std::exception& e) {
        Json::Value err; err["error"] = e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        return cb(r);
    }
}

// This method handles user login. There is a link with other API AuthController methods but not directly.
void AuthController::loginUser(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    try {
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("email") || !body->isMember("password")) {
            Json::Value err;
            err["error"] = "email and password are required";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err);
            r->setStatusCode(drogon::k400BadRequest);
            return cb(r);
        }

        const auto email = (*body)["email"].asString();
        const auto password = (*body)["password"].asString();

        const char* url = std::getenv("SUPABASE_URL");
        const char* anonKey = std::getenv("SUPABASE_ANON_KEY");
        if (!url || !anonKey) throw std::runtime_error("Missing SUPABASE_URL/ANON_KEY");

        std::string endpoint = std::string(url) + "/auth/v1/token?grant_type=password";
        nlohmann::json payload = {{"email", email}, {"password", password}};

        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl init failed");

        std::string response;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("apikey: " + std::string(anonKey)).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        const auto bodyJson = payload.dump();
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyJson.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        auto res = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) throw std::runtime_error("curl perform failed");

        auto r = drogon::HttpResponse::newHttpResponse();
        r->setStatusCode(drogon::HttpStatusCode(code));
        r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        r->setBody(response.empty() ? "{}" : response);
        return cb(r);

    } catch (const std::exception& e) {
        Json::Value err;
        err["error"] = e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        return cb(r);
    }
}