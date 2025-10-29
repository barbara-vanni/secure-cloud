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

// Envoie le payload déjà construit (incluant "data" si présent) vers Supabase
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

// --- Handler /auth/register ---
void AuthController::registerUser(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    try {
        // Drogon fournit un JsonCPP (Json::Value)
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

        // Construit le payload Supabase : email/password (+ meta optionnelles dans "data")
        json signupPayload = {
            {"email", email},
            {"password", password}
        };

        // Champs profil optionnels à faire copier par le trigger depuis raw_user_meta_data
        json meta;
        if (body->isMember("first_name")) meta["first_name"] = (*body)["first_name"].asString();
        if (body->isMember("last_name"))  meta["last_name"]  = (*body)["last_name"].asString();
        if (body->isMember("state"))      meta["state"]      = (*body)["state"].asString();
        if (!meta.empty()) {
            signupPayload["data"] = meta;  // => ira dans auth.users.raw_user_meta_data
        }

        // Appel Supabase
        const auto sup = supabaseSignup(signupPayload);

        // Réponse brute Supabase
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