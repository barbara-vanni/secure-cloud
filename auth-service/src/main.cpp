//
// Created by drvba on 22/09/2025.
//

// #include <iostream>
// #include "../include/auth.h"
// #include <drogon/drogon.h>
// #include "AuthController.cpp"

// int main() {
//     std::cout << "Démarrage du service d'authentification..." << std::endl;
//     AuthService auth;
//     auth.run();
//     return 0;
// }

// #include "../include/AuthController.h"
//
// int main() {
//     drogon::app()
//       .registerHandler(
//         "/health",
//         [](const drogon::HttpRequestPtr&,
//            std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
//             Json::Value j;
//             j["status"] = "ok";
//             auto r = drogon::HttpResponse::newHttpJsonResponse(j);
//             cb(r);
//         },
//         {drogon::Get}
//       )
//       .addListener("0.0.0.0", std::getenv("PORT") ? std::stoi(std::getenv("PORT")) : 8080)
//       .setLogLevel(trantor::Logger::kInfo)
//       .run();
// }

#include "../include/AuthController.h"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>

void loadEnvFile(const std::string& path = ".env") {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[WARN] Impossible d’ouvrir le fichier .env : " << path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Nettoyage espaces
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
        value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());

#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }

    std::cout << "[INFO] Variables .env chargées depuis " << path << std::endl;
}

int main() {
    // 🟥 Charger automatiquement le .env au démarrage
    loadEnvFile(".env");

    // Vérification rapide
    std::cout << "SUPABASE_URL=" << (std::getenv("SUPABASE_URL") ? std::getenv("SUPABASE_URL") : "non défini") << std::endl;

    drogon::app()
        .registerHandler(
            "/health",
            [](const drogon::HttpRequestPtr&,
               std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
                Json::Value j;
                j["status"] = "ok";
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                cb(r);
            },
            {drogon::Get}
        )
        .addListener("0.0.0.0", std::getenv("PORT") ? std::stoi(std::getenv("PORT")) : 8080)
        .setLogLevel(trantor::Logger::kInfo)
        .run();
}
