//
// Created by drvba on 22/09/2025.
//

#include "../include/auth.h"

#include <iostream>

void AuthService::run() {
    std::cout << "Service d'authentification en cours d'exécution..." << std::endl;
}

bool AuthService::login(const std::string &username, const std::string &password) {
    return (username == "dr.dupont" && password == "password");
}
