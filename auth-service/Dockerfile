FROM ubuntu:22.04

# Installe les dépendances nécessaires
RUN apt-get update && \
    apt-get install -y libstdc++6 && \
    rm -rf /var/lib/apt/lists/*

# Crée le répertoire de travail
WORKDIR /app

# Copie le binaire depuis le build local ( à faire avec WSL)
COPY build/auth-service /app/

# Donne les permissions d'exécution
RUN chmod +x /app/auth-service

# Utilisateur root (ou un utilisateur spécifique si nécessaire)
USER root

# Variables d'environnement (à externaliser// .env dans le repertoire docker !)
ENV JWT_SECRET=default_secret
ENV DB_HOST=postgres
ENV DB_USER=securecloud
ENV DB_PASSWORD=password
ENV REDIS_HOST=redis

# Commande de démarrage
CMD ["./auth-service"]
