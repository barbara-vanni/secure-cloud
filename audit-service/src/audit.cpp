//
// Created by drvba on 23/09/2025.
//

#include "../include/audit.h"
#include <iostream>

void AuditService::run() {
    std::cout<<"Audit Service en cours d'execution"<<std::endl;
}

void AuditService::read() {
    std::cout<<"Utilisation de mongodb"<<std::endl;
    int a = 0;
    int b = 5;
    int c = a + b ;
    std::cout<< c <<std::endl;
}