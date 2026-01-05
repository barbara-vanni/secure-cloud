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

std::string makeDirectKey(const std::string& a, const std::string& b) {
    // UUID strings compare is deterministic; we only need consistent ordering
    if (a <= b) return a + ":" + b;
    return b + ":" + a;
}

bool fetchDirectConversationByKey(const SupabaseEnv& env,
                                 const std::string& directKey,
                                 nlohmann::json& outConv,
                                 ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversations"
        "?select=*"
        "&type=eq.direct"
        "&direct_key=eq." + directKey +
        "&deleted_at=is.null"
        "&limit=1";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (fetchDirectConversationByKey)");
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
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (fetchDirectConversationByKey)");
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
        errOut = makeError(500, "Cannot parse direct conversation search response");
        return false;
    }

    if (j.is_array() && !j.empty()) {
        outConv = j[0];
        return true;
    }

    outConv = nlohmann::json::object();
    return true; // no error, just not found
}

bool createConversationRowWithDirectKey(const SupabaseEnv& env,
                                       const std::string& type,
                                       const std::optional<std::string>& name,
                                       const std::string& profileId,
                                       const std::optional<std::string>& directKey,
                                       nlohmann::json& convObj,
                                       long& convHttpCode,
                                       ConversationService::Result& errOut) {
    std::string convUrl = env.base + "/rest/v1/conversations";

    nlohmann::json convPayload;
    if (name.has_value() && !name->empty()) {
        convPayload["name"] = *name;
    }
    if (directKey.has_value() && !directKey->empty()) {
        convPayload["direct_key"] = *directKey;
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
              ? nlohmann::json::array()
              : nlohmann::json::parse(convResp, nullptr, false);
    if (jc.is_discarded()) {
        errOut = makeError(500, "Cannot parse conversation response from Supabase");
        return false;
    }

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

bool fetchOtherParticipantId(const SupabaseEnv& env,
                             const std::string& conversationId,
                             const std::string& callerProfileId,
                             std::string& outOtherProfileId,
                             ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversation_members"
        "?select=user_id"
        "&conversation_id=eq." + conversationId +
        "&left_at=is.null"
        "&user_id=neq." + callerProfileId +
        "&limit=1";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (fetchOtherParticipantId)");
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
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (fetchOtherParticipantId)");
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
    if (j.is_discarded() || !j.is_array() || j.empty() || !j[0].contains("user_id")) {
        errOut = makeError(404, "Direct conversation other participant not found");
        return false;
    }

    outOtherProfileId = j[0]["user_id"].get<std::string>();
    return true;
}

bool fetchProfileDisplayName(const SupabaseEnv& env,
                             const std::string& profileId,
                             std::string& outName,
                             ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/profiles?select=first_name,last_name&id=eq." + profileId + "&limit=1";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (fetchProfileDisplayName)");
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
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (fetchProfileDisplayName)");
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
        errOut = makeError(404, "Profile not found for display name");
        return false;
    }

    const auto& p = j[0];
    std::string first = p.contains("first_name") && p["first_name"].is_string() ? p["first_name"].get<std::string>() : "";
    std::string last  = p.contains("last_name") && p["last_name"].is_string() ? p["last_name"].get<std::string>() : "";

    if (!first.empty() && !last.empty()) outName = first + " " + last;
    else if (!first.empty()) outName = first;
    else if (!last.empty()) outName = last;
    else outName = "Utilisateur";

    return true;
}

void enrichDisplayNameIfDirect(const SupabaseEnv& env,
                               const std::string& callerProfileId,
                               nlohmann::json& membershipRow,
                               ConversationService::Result& errOut) {
    if (!membershipRow.is_object() || !membershipRow.contains("conversation")) return;
    auto& conv = membershipRow["conversation"];
    if (!conv.is_object() || !conv.contains("type") || !conv["type"].is_string()) return;

    const std::string type = conv["type"].get<std::string>();
    if (type != "direct") {
        // Pour group, si tu veux unifier : display_name = name (fallback)
        if (!conv.contains("display_name")) {
            if (conv.contains("name") && conv["name"].is_string() && !conv["name"].get<std::string>().empty())
                conv["display_name"] = conv["name"].get<std::string>();
            else
                conv["display_name"] = "Groupe";
        }
        return;
    }

    if (!conv.contains("id") || !conv["id"].is_string()) return;
    const std::string conversationId = conv["id"].get<std::string>();

    std::string otherId;
    if (!fetchOtherParticipantId(env, conversationId, callerProfileId, otherId, errOut)) {
        return; // errOut déjà rempli si nécessaire
    }

    std::string display;
    if (!fetchProfileDisplayName(env, otherId, display, errOut)) {
        return;
    }

    conv["display_name"] = display;
    conv["other_user_id"] = otherId;
}

// ---------- Helper 4a : insert a member into conversation_members ----------
bool insertMemberWithRole(const SupabaseEnv& env,
                          const std::string& conversationId,
                          const std::string& profileId,
                          const std::string& role,
                          nlohmann::json* outRow,
                          ConversationService::Result& errOut) {
    std::string memberUrl = env.base + "/rest/v1/conversation_members";

    json memberPayload;
    memberPayload["conversation_id"] = conversationId;
    memberPayload["user_id"]         = profileId;
    memberPayload["role"]            = role;

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

    if (outRow) {
        if (!memResp.empty()) {
            auto j = json::parse(memResp, nullptr, false);
            if (!j.is_discarded()) {
                *outRow = j;
                if (j.is_array() && !j.empty()) {
                    *outRow = j[0];
                }
            } else {
                *outRow = json::object();
            }
        } else {
            *outRow = json::object();
        }
    }

    return true;
}

// ---------- Helper 4b : insert the creator as owner ----------
bool insertOwnerMember(const SupabaseEnv& env,
                       const std::string& conversationId,
                       const std::string& profileId,
                       ConversationService::Result& errOut) {
    nlohmann::json dummy;
    return insertMemberWithRole(env, conversationId, profileId, "owner", &dummy, errOut);
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

// ---------- Helper 10: ensure a profile exists by id ----------
bool ensureProfileExists(const SupabaseEnv& env,
                         const std::string& profileId,
                         ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/profiles?select=id&id=eq." + profileId + "&limit=1";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (ensureProfileExists)");
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
        errOut = makeError(500, "curl perform failed (ensureProfileExists)");
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
        errOut = makeError(404, "Target profile not found");
        return false;
    }

    return true;
}

// ---------- Helper 11: ensure caller can view the conversation (is a member) ----------
bool ensureCanViewConversation(const SupabaseEnv& env,
                               const std::string& profileId,
                               const std::string& conversationId,
                               ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversation_members"
        "?select=id"
        "&conversation_id=eq." + conversationId +
        "&user_id=eq." + profileId +
        "&left_at=is.null";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (ensureCanViewConversation)");
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
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (ensureCanViewConversation)");
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
        errOut = makeError(403, "You are not a member of this conversation");
        return false;
    }

    return true;
}

// ---------- Helper 12: get member role and count owners for a conversation ----------
bool fetchMemberRoleAndOwnerCount(const SupabaseEnv& env,
                                  const std::string& conversationId,
                                  const std::string& userId,
                                  std::string& outMemberRole,
                                  int& outOwnerCount,
                                  ConversationService::Result& errOut) {
    std::string url = env.base +
        "/rest/v1/conversation_members"
        "?select=user_id,role"
        "&conversation_id=eq." + conversationId +
        "&left_at=is.null";

    CURL* c = curl_easy_init();
    if (!c) {
        errOut = makeError(500, "curl init failed (fetchMemberRoleAndOwnerCount)");
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
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    auto res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        errOut = makeError(500, "curl perform failed (fetchMemberRoleAndOwnerCount)");
        return false;
    }

    if (httpCode != 200) {
        ConversationService::Result r;
        r.statusCode = static_cast<int>(httpCode);
        r.body = resp.empty()
            ? nlohmann::json::object()
            : nlohmann::json::parse(resp, nullptr, false);
        if (r.body.is_discarded()) r.body = nlohmann::json::object();
        errOut = r;
        return false;
    }

    nlohmann::json j = nlohmann::json::array();
    if (!resp.empty()) {
        j = nlohmann::json::parse(resp, nullptr, false);
        if (j.is_discarded()) {
            j = nlohmann::json::array();
        }
    }

    outOwnerCount = 0;
    bool found = false;
    outMemberRole.clear();

    if (j.is_array()) {
        for (const auto& row : j) {
            if (!row.is_object()) continue;
            const auto itUser = row.find("user_id");
            const auto itRole = row.find("role");
            if (itRole != row.end() && itRole->is_string()) {
                if (itRole->get<std::string>() == "owner") {
                    ++outOwnerCount;
                }
            }
            if (itUser != row.end() && itUser->is_string() &&
                itUser->get<std::string>() == userId &&
                itRole != row.end() && itRole->is_string()) {
                outMemberRole = itRole->get<std::string>();
                found = true;
            }
        }
    }

    if (!found) {
        errOut = makeError(404, "Member not found in this conversation");
        return false;
    }

    return true;
}

} // namespace


ConversationService::Result ConversationService::createConversation(
    const std::string& accessToken,
    const std::string& type,
    const std::optional<std::string>& name,
    const std::optional<std::string>& targetUserId
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }

        if (type != "direct" && type != "group") {
            return makeError(400, "Field 'type' must be 'direct' or 'group'");
        }

        const char* base = std::getenv("SUPABASE_URL");
        const char* anon = std::getenv("SUPABASE_ANON_KEY");
        if (!base || !anon) {
            return makeError(500, "Missing SUPABASE_URL/ANON_KEY");
        }

        SupabaseEnv env{ std::string(base), std::string(anon), accessToken };
        ConversationService::Result err;

        // 1) authUserId
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) caller profileId
        std::string callerProfileId;
        if (!fetchProfileId(env, authUserId, callerProfileId, err)) {
            return err;
        }

        // GROUP: comportement actuel
        if (type == "group") {
            nlohmann::json convObj;
            long convHttpCode = 0;

            if (!createConversationRowWithDirectKey(env, type, name, callerProfileId, std::nullopt, convObj, convHttpCode, err)) {
                return err;
            }

            const std::string conversationId = convObj["id"].get<std::string>();
            if (!insertOwnerMember(env, conversationId, callerProfileId, err)) {
                return err;
            }

            ConversationService::Result r;
            r.statusCode = 201;
            r.body = convObj;
            return r;
        }

        // DIRECT: target_user_id obligatoire
        if (!targetUserId.has_value() || targetUserId->empty()) {
            return makeError(400, "Field 'target_user_id' is required for type='direct'");
        }

        const std::string targetProfileId = *targetUserId;

        if (targetProfileId == callerProfileId) {
            return makeError(400, "Cannot create a direct conversation with yourself");
        }

        // vérifier que le profil cible existe
        if (!ensureProfileExists(env, targetProfileId, err)) {
            return err;
        }

        // 3) direct_key
        const std::string directKey = makeDirectKey(callerProfileId, targetProfileId);

        // 4) si existe déjà → retourner (200)
        nlohmann::json existing;
        if (!fetchDirectConversationByKey(env, directKey, existing, err)) {
            return err;
        }

        if (existing.is_object() && existing.contains("id")) {
            ConversationService::Result r;
            r.statusCode = 200;
            r.body = existing;
            return r;
        }

        // 5) créer la conversation direct (name ignoré)
        nlohmann::json convObj;
        long convHttpCode = 0;

        if (!createConversationRowWithDirectKey(env, "direct", std::nullopt, callerProfileId, directKey, convObj, convHttpCode, err)) {
            return err;
        }

        const std::string conversationId = convObj["id"].get<std::string>();

        // 6) ajouter les 2 membres : caller owner, target member
        if (!insertOwnerMember(env, conversationId, callerProfileId, err)) {
            return err;
        }

        nlohmann::json dummy;
        if (!insertMemberWithRole(env, conversationId, targetProfileId, "owner", &dummy, err)) {
            return err;
        }

        ConversationService::Result r;
        r.statusCode = 201;
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

        SupabaseEnv env{ std::string(base), std::string(anon), accessToken };
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

        // 4) enrich display_name
        if (list.is_array()) {
            for (auto& row : list) {
                enrichDisplayNameIfDirect(env, profileId, row, err);
                // si err est rempli ici, on ne casse pas toute la liste : on continue
                err.statusCode = 0;
                err.body = nlohmann::json::object();
            }
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

        SupabaseEnv env{ std::string(base), std::string(anon), accessToken };
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
        nlohmann::json convRow;
        if (!fetchConversationById(env, profileId, conversationId, convRow, err)) {
            return err;
        }

        // 4) enrich display_name
        enrichDisplayNameIfDirect(env, profileId, convRow, err);

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = convRow;
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
            return makeError(400, "Missing 'name' field");
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

        // 1) Auth user
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) Caller profile
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) Vérifier droits (owner/admin)
        std::string callerRole;
        if (!checkConversationUpdateRights(env, profileId, conversationId, callerRole, err)) {
            return err;
        }

        // 4) Lire la conversation pour connaître son type
        nlohmann::json convRow;
        if (!fetchConversationById(env, profileId, conversationId, convRow, err)) {
            return err;
        }

        if (!convRow.is_object() || !convRow.contains("conversation") || !convRow["conversation"].is_object()) {
            return makeError(500, "Unexpected conversation read format");
        }

        const auto& conv = convRow["conversation"];
        if (!conv.contains("type") || !conv["type"].is_string()) {
            return makeError(500, "Conversation type missing");
        }

        const std::string type = conv["type"].get<std::string>();
        if (type == "direct") {
            // Interdit : cohérence option B (nom dynamique)
            return makeError(409, "Direct conversations cannot be renamed");
        }

        // 5) Update name (group only)
        std::string url = env.base +
            "/rest/v1/conversations?id=eq." + conversationId;

        nlohmann::json payload;
        payload["name"] = *name;
        const std::string body = payload.dump();

        CURL* c = curl_easy_init();
        if (!c) {
            return makeError(500, "curl init failed (updateConversation)");
        }

        std::string resp;
        long httpCode = 0;
        struct curl_slist* h = nullptr;
        h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
        h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
        h = curl_slist_append(h, "Content-Type: application/json");
        h = curl_slist_append(h, "Prefer: return=representation");

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
            return makeError(500, "curl perform failed (updateConversation)");
        }

        if (httpCode != 200) {
            ConversationService::Result r;
            r.statusCode = static_cast<int>(httpCode);
            r.body = resp.empty()
                ? nlohmann::json::object()
                : nlohmann::json::parse(resp, nullptr, false);
            if (r.body.is_discarded()) r.body = nlohmann::json::object();
            return r;
        }

        nlohmann::json j;
        if (!resp.empty()) {
            j = nlohmann::json::parse(resp, nullptr, false);
            if (j.is_discarded()) {
                j = nlohmann::json::array();
            }
        } else {
            j = nlohmann::json::array();
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = j;
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

ConversationService::Result ConversationService::addMember(
    const std::string& accessToken,
    const std::string& conversationId,
    const std::string& userId
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }
        if (conversationId.empty()) {
            return makeError(400, "Missing conversation id");
        }
        if (userId.empty()) {
            return makeError(400, "Missing user id");
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

        // 2) Caller's profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) Check rights (owner/admin)
        std::string role;
        if (!checkConversationUpdateRights(env, profileId, conversationId, role, err)) {
            return err;
        }

        // 4) Check that the target profile exists
        if (!ensureProfileExists(env, userId, err)) {
            return err;
        }

        // 5) Insert the member with role 'member'
        nlohmann::json inserted;
        if (!insertMemberWithRole(env, conversationId, userId, "member", &inserted, err)) {
            return err;
        }

        ConversationService::Result r;
        r.statusCode = 201;
        r.body = inserted;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}

ConversationService::Result ConversationService::listMembers(
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

        // 1) Recover the authenticated user (authUserId)
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) Recover the caller's profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) check rights (is member)
        if (!ensureCanViewConversation(env, profileId, conversationId, err)) {
            return err;
        }

        // 4) Recover the list of active members of the conversation
        std::string url = env.base +
            "/rest/v1/conversation_members"
            "?select=id,conversation_id,user_id,role,joined_at,left_at"
            "&conversation_id=eq." + conversationId +
            "&left_at=is.null";

        CURL* c = curl_easy_init();
        if (!c) {
            return makeError(500, "curl init failed (listMembers)");
        }

        std::string resp;
        long httpCode = 0;
        struct curl_slist* h = nullptr;
        h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
        h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
        h = curl_slist_append(h, "Content-Type: application/json");

        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

        auto res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_slist_free_all(h);
        curl_easy_cleanup(c);

        if (res != CURLE_OK) {
            return makeError(500, "curl perform failed (listMembers)");
        }

        if (httpCode != 200) {
            ConversationService::Result r;
            r.statusCode = static_cast<int>(httpCode);
            r.body = resp.empty() ? nlohmann::json::object() : nlohmann::json::parse(resp, nullptr, false);
            if (r.body.is_discarded()) r.body = nlohmann::json::object();
            return r;
        }

        nlohmann::json j;
        if (!resp.empty()) {
            j = nlohmann::json::parse(resp, nullptr, false);
            if (j.is_discarded()) {
                j = nlohmann::json::array();
            }
        } else {
            j = nlohmann::json::array();
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = j;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}

ConversationService::Result ConversationService::updateMemberRole(
    const std::string& accessToken,
    const std::string& conversationId,
    const std::string& userId,
    const std::string& role
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }
        if (conversationId.empty()) {
            return makeError(400, "Missing conversation id");
        }
        if (userId.empty()) {
            return makeError(400, "Missing user id");
        }
        if (role != "owner" && role != "member") {
            return makeError(400, "Role must be either 'owner' or 'member'");
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

        // 1) Recover the authenticated user (authUserId)
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) Recover the caller's profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        // 3) Recover the caller's role
        std::string callerRole;
        if (!checkConversationUpdateRights(env, profileId, conversationId, callerRole, err)) {
            return err;
        }

        // 4) Check that the target profile exists
        if (!ensureProfileExists(env, userId, err)) {
            return err;
        }

        // 5) Recover the target member's current role + number of owners
        std::string currentMemberRole;
        int ownerCount = 0;
        if (!fetchMemberRoleAndOwnerCount(env, conversationId, userId,
                                          currentMemberRole, ownerCount, err)) {
            return err;
        }

        // 6) Cannot downgrade the last owner to member
        if (currentMemberRole == "owner" && role == "member" && ownerCount <= 1) {
            // 409 = Conflict
            return makeError(409, "Cannot downgrade the last owner of the conversation");
        }

        // 7) Update the member's role (only if left_at IS NULL)
        std::string url = env.base +
            "/rest/v1/conversation_members"
            "?conversation_id=eq." + conversationId +
            "&user_id=eq." + userId +
            "&left_at=is.null";

        nlohmann::json payload;
        payload["role"] = role;
        const std::string body = payload.dump();

        CURL* c = curl_easy_init();
        if (!c) {
            return makeError(500, "curl init failed (updateMemberRole)");
        }

        std::string resp;
        long httpCode = 0;
        struct curl_slist* h = nullptr;
        h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
        h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
        h = curl_slist_append(h, "Content-Type: application/json");
        h = curl_slist_append(h, "Prefer: return=representation");

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
            return makeError(500, "curl perform failed (updateMemberRole)");
        }

        if (httpCode != 200) {
            ConversationService::Result r;
            r.statusCode = static_cast<int>(httpCode);
            r.body = resp.empty()
                ? nlohmann::json::object()
                : nlohmann::json::parse(resp, nullptr, false);
            if (r.body.is_discarded()) r.body = nlohmann::json::object();
            return r;
        }

        nlohmann::json j;
        if (!resp.empty()) {
            j = nlohmann::json::parse(resp, nullptr, false);
            if (j.is_discarded()) {
                j = nlohmann::json::array();
            }
        } else {
            j = nlohmann::json::array();
        }

        // If nothing was updated → the member does not exist or has already left the conversation
        if (j.is_array() && j.empty()) {
            return makeError(404, "Member not found in this conversation or already left");
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = j;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}

ConversationService::Result ConversationService::deleteMember(
    const std::string& accessToken,
    const std::string& conversationId,
    const std::string& userId
) {
    try {
        if (accessToken.empty()) {
            return makeError(401, "Missing Bearer access token");
        }
        if (conversationId.empty()) {
            return makeError(400, "Missing conversation id");
        }
        if (userId.empty()) {
            return makeError(400, "Missing user id");
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

        // 1) Recover the authenticated user (authUserId)
        std::string authUserId;
        if (!fetchAuthUserId(env, authUserId, err)) {
            return err;
        }

        // 2) Recover the caller's profileId
        std::string profileId;
        if (!fetchProfileId(env, authUserId, profileId, err)) {
            return err;
        }

        const bool isSelf = (profileId == userId);

        if (!isSelf) {
            // The caller is removing another member -> they must be owner/admin
            std::string callerRole;
            if (!checkConversationUpdateRights(env, profileId, conversationId, callerRole, err)) {
                return err;
            }
        } else {
            // The user is removing themselves -> they must at least be a member of the conversation
            if (!ensureCanViewConversation(env, profileId, conversationId, err)) {
                return err;
            }
        }

        // 3) Check that the target profile exists
        if (!ensureProfileExists(env, userId, err)) {
            return err;
        }

        // 4) Recover the target member's role + owner count
        std::string memberRole;
        int ownerCount = 0;
        if (!fetchMemberRoleAndOwnerCount(env, conversationId, userId,
                                          memberRole, ownerCount, err)) {
            return err; // include 404 if member not found
        }

        // 5) Empêcher de supprimer le dernier owner
        if (memberRole == "owner" && ownerCount <= 1) {
            return makeError(409, "Cannot remove the last owner of the conversation");
        }

        // 6) DELETE on conversation_members (hard delete)
        std::string url = env.base +
            "/rest/v1/conversation_members"
            "?conversation_id=eq." + conversationId +
            "&user_id=eq." + userId;

        CURL* c = curl_easy_init();
        if (!c) {
            return makeError(500, "curl init failed (deleteMember)");
        }

        std::string resp;
        long httpCode = 0;
        struct curl_slist* h = nullptr;
        h = curl_slist_append(h, ("apikey: " + env.anonKey).c_str());
        h = curl_slist_append(h, ("Authorization: Bearer " + env.accessToken).c_str());
        h = curl_slist_append(h, "Content-Type: application/json");
        h = curl_slist_append(h, "Prefer: return=representation");

        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

        auto res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_slist_free_all(h);
        curl_easy_cleanup(c);

        if (res != CURLE_OK) {
            return makeError(500, "curl perform failed (deleteMember)");
        }

        if (httpCode != 200 && httpCode != 204) {
            ConversationService::Result r;
            r.statusCode = static_cast<int>(httpCode);
            r.body = resp.empty()
                ? nlohmann::json::object()
                : nlohmann::json::parse(resp, nullptr, false);
            if (r.body.is_discarded()) r.body = nlohmann::json::object();
            return r;
        }

        nlohmann::json j;
        if (!resp.empty()) {
            j = nlohmann::json::parse(resp, nullptr, false);
            if (j.is_discarded()) {
                j = nlohmann::json::array();
            }
        } else {
            j = nlohmann::json::array();
        }

        // Si aucune ligne supprimée → le membre n'était pas dans cette conversation
        if (j.is_array() && j.empty()) {
            return makeError(404, "Member not found in this conversation");
        }

        ConversationService::Result r;
        r.statusCode = 200;
        r.body = j;
        return r;

    } catch (const std::exception& e) {
        return makeError(500, e.what());
    }
}