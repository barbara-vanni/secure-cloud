# Version minimale de CMake requise
cmake_minimum_required(VERSION 3.18)

# Nom du projet et version
project(files-service VERSION 1.0.0 LANGUAGES CXX)

# Standard C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_ROOT_DIR "C:/vcpkg/installed/x64-windows")
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")

find_package(OpenSSL REQUIRED)
#find_package(PostgreSQL REQUIRED)
#find_package(ZLIB REQUIRED)

# Ajouter l'exécutable
add_executable(files-service
        src/main.cpp
        src/files.cpp
)

# Inclure les répertoires
target_include_directories(files-service PRIVATE include)
# Lier les bibliothèques
target_link_libraries(files-service
        PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
#        PQ::PQ
#        ZLIB::ZLIB
)

# Installation (optionnel)
install(TARGETS files-service DESTINATION bin)
