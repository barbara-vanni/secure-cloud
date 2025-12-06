//
// Created by walid on 03/12/2025.
//

#include "../include/ConversationService.h"

#include <curl/curl.h>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

using nlohmann::json;

namespace {

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

ConversationService::Result makeError(int status, const std::string& msg) {
    ConversationService::Result r;
    r.statusCode = status;
    r.body = json{{"error", msg}};
    return r;
}

// Struct to easily pass Supabase config to helpers
struct SupabaseEnv {
    std::string base;
    std::string anonKey;
    std::string accessToken;
};

// ---------- Helper 1 : receive the authUserId via /auth/v1/user ----------
bool fetchAuthUserId(const SupabaseEnv& env,
                     std::string& authUserId,
                     ConversationService::Result& errOut) {
    std::string meUrl = env.base + "/auth/v1/user";

    CURL* curl = curl_easy_init();
    if (!curl) {
        errOut = makeError(500, "curl init failed (/auth/v1/user)");
        return false;
    }

    std::string response;
    long code = 0;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("apikey: " + env.anonKey).c_str());
    headers = curl_slist_append(headers, ("Authorization: Bearer " + env.accessToken).c_str());
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
        errOut = makeError(500, "curl perform failed (/auth/v1/user)");
        return false;
    }
    if (code != 200) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(code);
        r.body = response.empty() ? json::object() : json::parse(response, nullptr, false);
        if (r.body.is_discarded()) r.body = json::object();
        errOut = r;
        return false;
    }

    auto j = json::parse(response, nullptr, false);
    if (j.is_discarded() || !j.contains("id")) {
        errOut = makeError(500, "Cannot extract user id from Supabase response");
        return false;
    }

    authUserId = j["id"].get<std::string>();
    return true;
}

// ---------- Helper 2 : receive the profileId via /rest/v1/profiles ----------
bool fetchProfileId(const SupabaseEnv& env,
                    const std::string& authUserId,
                    std::string& profileId,
                    ConversationService::Result& errOut) {
    std::string profileUrl =
        env.base +
        "/rest/v1/profiles?select=id&auth_id=eq." + authUserId + "&limit=1";

    CURL* cProf = curl_easy_init();
    if (!cProf) {
        errOut = makeError(500, "curl init failed (profiles)");
        return false;
    }

    std::string profResp;
    long profCode = 0;
    struct curl_slist* hProf = nullptr;
    hProf = curl_slist_append(hProf, ("apikey: " + env.anonKey).c_str());
    hProf = curl_slist_append(hProf, ("Authorization: Bearer " + env.accessToken).c_str());
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
        errOut = makeError(500, "curl perform failed (profiles)");
        return false;
    }
    if (profCode != 200) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(profCode);
        r.body = profResp.empty() ? json::object() : json::parse(profResp, nullptr, false);
        if (r.body.is_discarded()) r.body = json::object();
        errOut = r;
        return false;
    }

    auto jp = json::parse(profResp, nullptr, false);
    if (jp.is_discarded() || !jp.is_array() || jp.empty() || !jp[0].contains("id")) {
        errOut = makeError(400, "No profile found for the current authenticated user");
        return false;
    }

    profileId = jp[0]["id"].get<std::string>();
    return true;
}

// ---------- Helper 3 : create the conversation via /rest/v1/conversations ----------
bool createConversationRow(const SupabaseEnv& env,
                           const std::string& type,
                           const std::optional<std::string>& name,
                           const std::string& profileId,
                           json& convObj,
                           long& convHttpCode,
                           ConversationService::Result& errOut) {
    std::string convUrl = env.base + "/rest/v1/conversations";

    json convPayload;
    if (name.has_value() && !name->empty()) {
        convPayload["name"] = *name;
    }
    convPayload["type"]       = type;
    convPayload["created_by"] = profileId;

    const std::string convBody = convPayload.dump();

    CURL* cConv = curl_easy_init();
    if (!cConv) {
        errOut = makeError(500, "curl init failed (conversations)");
        return false;
    }

    std::string convResp;
    struct curl_slist* hConv = nullptr;
    hConv = curl_slist_append(hConv, ("apikey: " + env.anonKey).c_str());
    hConv = curl_slist_append(hConv, ("Authorization: Bearer " + env.accessToken).c_str());
    hConv = curl_slist_append(hConv, "Content-Type: application/json");
    hConv = curl_slist_append(hConv, "Prefer: return=representation");

    curl_easy_setopt(cConv, CURLOPT_URL, convUrl.c_str());
    curl_easy_setopt(cConv, CURLOPT_HTTPHEADER, hConv);
    curl_easy_setopt(cConv, CURLOPT_POST, 1L);
    curl_easy_setopt(cConv, CURLOPT_POSTFIELDS, convBody.c_str());
    curl_easy_setopt(cConv, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(cConv, CURLOPT_WRITEDATA, &convResp);

    auto resConv = curl_easy_perform(cConv);
    curl_easy_getinfo(cConv, CURLINFO_RESPONSE_CODE, &convHttpCode);
    curl_slist_free_all(hConv);
    curl_easy_cleanup(cConv);

    if (resConv != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (create conversation)");
        return false;
    }

    auto jc = convResp.empty()
              ? json::array()
              : json::parse(convResp, nullptr, false);
    if (jc.is_discarded()) {
        errOut = makeError(500, "Cannot parse conversation response from Supabase");
        return false;
    }

    // Supabase may return an array; we take the first object
    convObj = jc;
    if (jc.is_array() && !jc.empty()) {
        convObj = jc[0];
    }

    if (!convObj.contains("id")) {
        errOut = makeError(500, "Conversation created but id missing in response");
        return false;
    }

    return true;
}

// ---------- Helper 4 : insert the creator into conversation_members ----------
bool insertOwnerMember(const SupabaseEnv& env,
                       const std::string& conversationId,
                       const std::string& profileId,
                       ConversationService::Result& errOut) {
    std::string memberUrl = env.base + "/rest/v1/conversation_members";

    json memberPayload;
    memberPayload["conversation_id"] = conversationId;
    memberPayload["user_id"]         = profileId;
    memberPayload["role"]            = "owner";

    const std::string memberBody = memberPayload.dump();

    CURL* cMem = curl_easy_init();
    if (!cMem) {
        errOut = makeError(500, "curl init failed (conversation_members)");
        return false;
    }

    std::string memResp;
    long memCode = 0;
    struct curl_slist* hMem = nullptr;
    hMem = curl_slist_append(hMem, ("apikey: " + env.anonKey).c_str());
    hMem = curl_slist_append(hMem, ("Authorization: Bearer " + env.accessToken).c_str());
    hMem = curl_slist_append(hMem, "Content-Type: application/json");
    hMem = curl_slist_append(hMem, "Prefer: return=representation");

    curl_easy_setopt(cMem, CURLOPT_URL, memberUrl.c_str());
    curl_easy_setopt(cMem, CURLOPT_HTTPHEADER, hMem);
    curl_easy_setopt(cMem, CURLOPT_POST, 1L);
    curl_easy_setopt(cMem, CURLOPT_POSTFIELDS, memberBody.c_str());
    curl_easy_setopt(cMem, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(cMem, CURLOPT_WRITEDATA, &memResp);

    auto resMem = curl_easy_perform(cMem);
    curl_easy_getinfo(cMem, CURLINFO_RESPONSE_CODE, &memCode);
    curl_slist_free_all(hMem);
    curl_easy_cleanup(cMem);

    if (resMem != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (conversation_members)");
        return false;
    }

    if (memCode != 200 && memCode != 201) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(memCode);
        r.body = memResp.empty() ? json::object() : json::parse(memResp, nullptr, false);
        if (r.body.is_discarded()) r.body = json::object();
        errOut = r;
        return false;
    }

    return true;
}

// ---------- Helper 5: List all conversations of a profile ----------
bool fetchMyConversations(const SupabaseEnv& env,
                          const std::string& profileId,
                          nlohmann::json& out,
                          ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversation_members"
    "?select=conversation:conversations!inner(*),role,joined_at,left_at"
        "&user_id=eq." + profileId +
        "&left_at=is.null"
        "&conversation.deleted_at=is.null";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (list conversations)");
        return false;
    }

    std::string resp;
    long httpCode = 0;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
    h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
    h = curl_slist_append(h, "Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (list conversations)");
        return false;
    }

    if (httpCode != 200) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(httpCode);
        r.body = resp.empty() ? nlohmann::json::object() : nlohmann::json::parse(resp, nullptr, false);
        if (r.body.is_discarded()) r.body = nlohmann::json::object();
        errOut = r;
        return false;
    }

    auto j = nlohmann::json::parse(resp, nullptr, false);
    if (j.is_discarded()) {
        errOut = makeError(500, "Cannot parse conversations list from Supabase");
        return false;
    }

    out = j;
    return true;
}

// ---------- Helper 6: Get conversation by ID ----------
bool fetchConversationById(const SupabaseEnv& env,
                           const std::string& profileId,
                           const std::string& conversationId,
                           nlohmann::json& out,
                           ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversation_members"
    "?select=conversation:conversations!inner(*),role,joined_at,left_at"
        "&user_id=eq." + profileId +
        "&conversation_id=eq." + conversationId +
        "&left_at=is.null"
        "&conversation.deleted_at=is.null";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (get conversation)");
        return false;
    }

    std::string resp;
    long httpCode = 0;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
    h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
    h = curl_slist_append(h, "Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (get conversation)");
        return false;
    }

    if (httpCode != 200) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(httpCode);
        r.body = resp.empty() ? nlohmann::json::object() : nlohmann::json::parse(resp, nullptr, false);
        if (r.body.is_discarded()) r.body = nlohmann::json::object();
        errOut = r;
        return false;
    }

    auto j = nlohmann::json::parse(resp, nullptr, false);
    if (j.is_discarded()) {
        errOut = makeError(500, "Cannot parse conversation from Supabase");
        return false;
    }

    if (!j.is_array() || j.empty()) {
        errOut = makeError(404, "Conversation not found or user is not a member");
        return false;
    }

    out = j[0];
    return true;
}

// ------------- Helper 7: Check update rights ----------
bool checkConversationUpdateRights(const SupabaseEnv& env,
                                   const std::string& profileId,
                                   const std::string& conversationId,
                                   std::string& outRole,
                                   ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversation_members"
        "?select=role"
        "&user_id=eq." + profileId +
        "&conversation_id=eq." + conversationId +
        "&left_at=is.null"
        "&limit=1";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (checkConversationUpdateRights)");
        return false;
    }

    std::string resp;
    long httpCode = 0;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
    h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
    h = curl_slist_append(h, "Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (checkConversationUpdateRights)");
        return false;
    }

    if (httpCode != 200) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(httpCode);
        r.body = resp.empty() ? nlohmann::json::object() : nlohmann::json::parse(resp, nullptr, false);
        if (r.body.is_discarded()) r.body = nlohmann::json::object();
        errOut = r;
        return false;
    }

    auto j = nlohmann::json::parse(resp, nullptr, false);
    if (j.is_discarded() || !j.is_array() || j.empty()) {
        errOut = makeError(404, "Conversation not found or user is not a member");
        return false;
    }

    auto role = j[0].value("role", std::string{});
    if (role != "owner" && role != "admin") {
        errOut = makeError(403, "User is not allowed to update this conversation");
        return false;
    }

    outRole = role;
    return true;
}

// ------------- Helper 8: Update conversation name ----------
    bool patchConversationRow(const SupabaseEnv& env,
                          const std::string& conversationId,
                          const nlohmann::json& payload,
                          nlohmann::json& out,
                          ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversations?id=eq." + conversationId;

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (patchConversationRow)");
        return false;
    }

    std::string resp;
    long httpCode = 0;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
    h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Prefer: return=representation");

    const std::string body = payload.dump();

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (patchConversationRow)");
        return false;
    }

    if (httpCode != 200 && httpCode != 204) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(httpCode);
        r.body = resp.empty() ? nlohmann::json::object() : nlohmann::json::parse(resp, nullptr, false);
        if (r.body.is_discarded()) r.body = nlohmann::json::object();
        errOut = r;
        return false;
    }

    if (resp.empty()) {
        out = nlohmann::json::object();
        return true;
    }

    auto j = nlohmann::json::parse(resp, nullptr, false);
    if (j.is_discarded()) {
        errOut = makeError(500, "Cannot parse updated conversation from Supabase");
        return false;
    }

    out = j;
    if (j.is_array() && !j.empty()) {
        out = j[0];
    }

    return true;
}

// ----------- Helper 9: Get current time in ISO 8601 UTC ----------
std::string nowIsoUtc() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}
} // namespace


ConversationService::Result ConversationService::createConversation(
    const std::string& accessToken,
    const std::string& type,
    const std::optional<std::string>& name
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) {
            return makeError(500, "Missing SUPABASE_URL/ANON_KEY");
        }

        SupabaseEnv env{
            std::string(base),
            std::string(anon),
            accessToken
        };

        ConversationService::Result err; // used by helpers

        // 1) authUserId
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) create the conversation
        json convObj;
        long convHttpCode = 0;
        if (!createConversationRow(env, type, name, profileId, convObj, convHttpCode, err)) {
            return err;
        }


        // 4) log the creator as owner in conversation_members
        const std::string conversationId = convObj["id"].get<std::string>();
        if (!insertOwnerMember(env, conversationId, profileId, err)) {
            return err;
        }

        // 5) Final response: same behavior as before
        ConversationService::Result r;
        r.statusCode = (convHttpCode == 200 || convHttpCode == 201)
                       ? 201
                       : static_cast<int>(convHttpCode);
        r.body = convObj;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}

ConversationService::Result ConversationService::listMyConversations(
    const std::string& accessToken
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) {
            return makeError(500, "Missing SUPABASE_URL/ANON_KEY");
        }

        SupabaseEnv env{
            std::string(base),
            std::string(anon),
            accessToken
        };

        ConversationService::Result err;

        // 1) authUserId
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) conversation lists
        nlohmann::json list;
        if (!fetchMyConversations(env, profileId, list, err)) {
            return err;
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = list;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}

ConversationService::Result ConversationService::getConversationById(
    const std::string& accessToken,
    const std::string& conversationId
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }
        if (conversationId.empty()) {
            return makeError(400, "Missing conversation id");
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) {
            return makeError(500, "Missing SUPABASE_URL/ANON_KEY");
        }

        SupabaseEnv env{
            std::string(base),
            std::string(anon),
            accessToken
        };

        ConversationService::Result err;

        // 1) authUserId
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) Conversation if the user is a member
        nlohmann::json conv;
        if (!fetchConversationById(env, profileId, conversationId, conv, err)) {
            return err;
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = conv;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}

ConversationService::Result ConversationService::updateConversation(
    const std::string& accessToken,
    const std::string& conversationId,
    const std::optional<std::string>& name
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }
        if (conversationId.empty()) {
            return makeError(400, "Missing conversation id");
        }
        if (!name.has_value()) {
            return makeError(400, "Nothing to update (expecting at least 'name')");
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) {
            return makeError(500, "Missing SUPABASE_URL/ANON_KEY");
        }

        SupabaseEnv env{
            std::string(base),
            std::string(anon),
            accessToken
        };

        ConversationService::Result err;

        // 1) authUserId
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) check rights (owner/admin)
        std::string role;
        if (!checkConversationUpdateRights(env, profileId, conversationId, role, err)) {
            return err;
        }

        // 4) build the update payload
        nlohmann::json payload;
        if (name.has_value()) {
            payload["name"] = *name;
        }

        // 5) PATCH on conversations
        nlohmann::json updated;
        if (!patchConversationRow(env, conversationId, payload, updated, err)) {
            return err;
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = updated;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}

ConversationService::Result ConversationService::deleteConversation(
    const std::string& accessToken,
    const std::string& conversationId
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }
        if (conversationId.empty()) {
            return makeError(400, "Missing conversation id");
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) {
            return makeError(500, "Missing SUPABASE_URL/ANON_KEY");
        }

        SupabaseEnv env{
            std::string(base),
            std::string(anon),
            accessToken
        };

        ConversationService::Result err;

        // 1) authUserId
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) Check rights (owner/admin)
        std::string role;
        if (!checkConversationUpdateRights(env, profileId, conversationId, role, err)) {
            return err;
        }

        // 4) Soft delete : put deleted_at (and updated_at) to now
        nlohmann::json payload;
        const auto ts = nowIsoUtc();
        payload["deleted_at"] = ts;
        payload["updated_at"] = ts;

        nlohmann::json updated;
        if (!patchConversationRow(env, conversationId, payload, updated, err)) {
            return err;
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = updated;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}