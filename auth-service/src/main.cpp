//
// Created by drvba on 22/09/2025.
//

#include "main.h"

#include <iostream>
#include "../include/auth.h"

int main() {
    std::cout << "Démarrage du service d'authentification..." << std::endl;
    AuthService auth;
    auth.run();
    return 0;
}
