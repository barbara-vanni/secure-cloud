//
// Created by drvba on 22/09/2025.
//

#ifndef SECURE_CLOUD_AUTH_H
#define SECURE_CLOUD_AUTH_H


#pragma once
#include <string>

class AuthService {
public:
    void run();
    bool login(const std::string &username, const std::string &password);
};



#endif //SECURE_CLOUD_AUTH_H