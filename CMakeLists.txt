cmake_minimum_required(VERSION 3.20)
project(secure-cloud)

# Inclusion de tous les microservices
add_subdirectory(auth-service)
add_subdirectory(files-service)
add_subdirectory(audit-service)
add_subdirectory(messaging-service)
