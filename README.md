# secure-cloud

Messagerie sécurisée pour médecine sans frontières (MCF)


# CI/CD : 

- GitHub actions
- Docker et Docker Compose 
- SonarCloud 

### Github actions 

Intégration native avec Github sans besoin de d'outils externe
Configuration workflows personalisé 

### Docker et Docker Compose

Chaque microservice s'éxecute dans son propre conteneur. 
Toutes les configurations et dépendance sont figés dans les images docker

### SonarCloud 

Analyse de code statique avec détection des bugs, vulnérabilités et des "code smells"
Intégration à GitHub actions avec un workflow spécifique, résulat visible dans les pull request

## Workflow :

### ci-cd.yml workflow

Ce workflow construit et déploie les images Docker des microservices.
Étapes clés :

**Checkout du code** :
- Récupère le code depuis GitHub.

**Connexion à Docker Hub** :
- Utilise un token stocké dans les secrets GitHub.

**Build et push des images Docker** :
- Chaque microservice est construit séparément.
- Les images sont poussées vers Docker Hub avec des tags.

**Déploiement avec Docker Compose** :

- Les images sont tirées depuis Docker Hub.
- Les conteneurs sont démarrés avec : 
```
docker compose up -d.
```




