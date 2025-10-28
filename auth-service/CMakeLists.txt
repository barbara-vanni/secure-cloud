cmake_minimum_required(VERSION 3.18)

project(auth-service VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ne PAS définir CMAKE_TOOLCHAIN_FILE ni OPENSSL_ROOT_DIR ici.
# Ça vient du profil CMake de CLion (ligne de commande), sinon CMake ne le prend pas au 1er tour.

find_package(Drogon CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(auth-service
        src/main.cpp
        src/AuthController.cpp
        include/AuthController.h
)

target_include_directories(auth-service PRIVATE include)
target_link_libraries(auth-service
        PRIVATE
        Drogon::Drogon
        CURL::libcurl
        nlohmann_json::nlohmann_json
        OpenSSL::SSL OpenSSL::Crypto
)

install(TARGETS auth-service DESTINATION bin)
