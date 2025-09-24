FROM ubuntu:latest
LABEL authors="drvba"

# Installe les dépendances nécessaires
RUN apt-get update && \
    apt-get install -y libstdc++6 libssl-dev libpq-dev zlib1g && \
    rm -rf /var/lib/apt/lists/*

# Crée le répertoire de travail
WORKDIR /app

# Copie le binaire depuis le build local ( à faire avec WSL)
COPY build/files-service /app/

# Donne les permissions d'exécution
RUN chmod +x /app/files-service

# Utilisateur root (ou un utilisateur spécifique si nécessaire)
USER root

# Variables d'environnement (à externaliser// .env dans le repertoire docker !)
ENV AUTH_SERVICE_URL=http://auth:8080
ENV MINIO_ENDPOINT=minio:9000
ENV MINIO_ACCESS_KEY=default_key
ENV MINIO_SECRET_KEY=default_secret

# Commande de démarrage
CMD ["./files-service"]

