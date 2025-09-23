//
// Created by drvba on 23/09/2025.
//

#include <iostream>
#include <ostream>

#include "../include/files.h"

int main () {
    std::cout<<"Service d'échange de files"<<std::endl;
    FilesService files;
    files.run();
    return 0;
}
