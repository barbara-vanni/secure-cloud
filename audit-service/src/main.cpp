//
// Created by drvba on 23/09/2025.
//

#include "../include/audit.h"
#include <iostream>
#include <ostream>

int main () {
    std::cout<<"Service d'audit demarré"<<std::endl;
    AuditService::run();
    AuditService::read();
    return 0;
}