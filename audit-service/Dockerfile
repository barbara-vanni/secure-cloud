FROM ubuntu:22.04

# Installe les dépendances nécessaires
RUN apt-get update && \
    apt-get install -y libstdc++6 && \
    rm -rf /var/lib/apt/lists/*

# Crée le répertoire de travail
WORKDIR /app

# Copie le binaire depuis le build local ( à faire avec WSL)
COPY build/audit-service /app/

# Variables d'environnement (à externaliser// .env dans le repertoire docker !)
ENV DB_HOST=postgres

# Commande de démarrage
CMD ["./audit-service"]
