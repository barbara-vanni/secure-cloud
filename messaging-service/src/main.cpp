//
// Created by drvba on 23/09/2025.
//

#include <iostream>
#include "../include/messaging.h"


int main () {
    std::cout << "Service de messagerie activée!\n";
    MessagingService message;
    message.run();
    return 0;
}