# Version minimale de CMake requise
cmake_minimum_required(VERSION 3.18)

# Nom du projet et version
project(messaging-service VERSION 1.0.0 LANGUAGES CXX)

# Standard C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_ROOT_DIR "C:/vcpkg/installed/x64-windows")
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")

find_package(OpenSSL REQUIRED)
#find_package(PostgreSQL REQUIRED)
#find_package(ixWebSocket REQUIRED)

# Ajouter l'exécutable
add_executable(messaging-service
        src/main.cpp
        src/messaging.cpp
)

# Inclure les répertoires
target_include_directories(messaging-service PRIVATE include)
# Lier les bibliothèques
target_link_libraries(messaging-service
        PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
#        PQ::PQ
#        ixWebSocket::ixWebSocket
)

# Installation (optionnel)
install(TARGETS messaging-service DESTINATION bin)
