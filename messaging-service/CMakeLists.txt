cmake_minimum_required(VERSION 3.18)

project(messaging-service VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_ROOT_DIR "C:/vcpkg/installed/x64-windows")
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")

find_package(Drogon CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(messaging-service
        src/main.cpp
        src/ConversationController.cpp
        src/ConversationService.cpp
        include/ConversationController.h
        include/ConversationService.h
)

target_include_directories(messaging-service PRIVATE include)
target_link_libraries(messaging-service
        PRIVATE
        Drogon::Drogon
        CURL::libcurl
        nlohmann_json::nlohmann_json
        OpenSSL::SSL OpenSSL::Crypto
)

# Installation (optionnel)
install(TARGETS messaging-service DESTINATION bin)