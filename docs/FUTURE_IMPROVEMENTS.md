# Améliorations Futures et Points en Suspens

## 1. Introduction

*   **But du document**: Ce document a pour objectif d'identifier et de décrire les axes d'amélioration potentiels pour le système de détection de chute. Ces améliorations visent à rendre le système plus robuste, configurable, sécurisé, et fonctionnellement complet, en se basant sur l'état actuel du projet et les meilleures pratiques de développement pour les systèmes embarqués.

## 2. Triangulation et Positionnement

*   **Implémentation d'un algorithme de triangulation robuste**:
    *   Le stub actuel (`calculate_xy_position`) doit être remplacé par un algorithme de triangulation mathématiquement correct (par exemple, basé sur la loi des cosinus ou des méthodes de trilatération si plus de deux capteurs sont envisagés).
    *   Gestion des cas ambigus ou impossibles (par exemple, si les cercles de distance ne se croisent pas ou sont tangents).
    *   Intégration de la gestion des erreurs de mesure des capteurs (par exemple, en utilisant des techniques de filtrage comme Kalman sur les distances avant la triangulation, ou des approches probabilistes pour la position).
*   **Calibration dynamique de la position des capteurs**:
    *   Au lieu de coder en dur les positions des capteurs dans le firmware maître, permettre leur configuration via une interface dédiée (UART, BLE, petite interface web) et les stocker en NVS.
    *   Explorer des méthodes de calibration semi-automatiques où le système pourrait aider à déterminer les positions relatives des capteurs.
*   **Extension à la 3D**:
    *   Si les capteurs radar utilisés (ou de futurs capteurs) fournissent des informations d'élévation (coordonnée Z), étendre l'algorithme de positionnement à la 3D.
    *   Évaluer la pertinence de la 3D pour améliorer la distinction entre postures (par exemple, assis sur une chaise vs. assis au sol) et la robustesse de la détection de chute.

## 3. Logique de Fusion et Détection

*   **Affinement de la logique de fusion des postures**:
    *   La logique actuelle pour `final_posture` est basée sur une priorisation simple. Explorer des approches plus nuancées, potentiellement basées sur la confiance (signal radar), l'historique des postures, ou des modèles d'états plus complexes.
    *   Définir clairement les états de posture possibles et leur hiérarchie (par exemple, `STANDING`, `SITTING_CHAIR`, `SITTING_FLOOR`, `LYING`, `MOVING`, `STILL`).
*   **Gestion complète des timeouts de capteurs dans `FusionEngine_task`**:
    *   Le `TODO` pour invalider les données d'un capteur si l'autre ne rapporte plus pendant une période prolongée doit être implémenté pour éviter que `FusionEngine_task` ne se bloque ou n'utilise des données excessivement obsolètes d'un capteur.
*   **Utilisation de la force du signal radar**:
    *   Intégrer la valeur `signal` (intensité du signal) des messages radar comme un indicateur de confiance. Des mesures avec un signal faible pourraient être moins pondérées ou nécessiter une confirmation supplémentaire.
*   **Filtres pour stabiliser les mesures**:
    *   Appliquer des filtres (par exemple, moyenne mobile, filtre médian, ou filtre de Kalman simple) sur les données de distance et potentiellement sur l'état de posture brut de chaque capteur avant la fusion pour lisser les fluctuations et réduire les fausses détections.

## 4. Configuration et Sécurité

*   **Mécanismes de provisionnement sécurisés et dynamiques**:
    *   Pour Wi-Fi et les credentials MQTT (URL du broker, identifiants client), remplacer les constantes codées en dur par des méthodes de configuration dynamiques et sécurisées :
        *   **Portail Captif**: Au premier démarrage, l'ESP32 agit comme un point d'accès Wi-Fi, servant une page web pour entrer les identifiants du réseau local et les détails MQTT.
        *   **Configuration via BLE (Bluetooth Low Energy)**: Utiliser une application mobile pour envoyer les configurations de manière sécurisée.
        *   Les configurations seraient ensuite stockées en NVS (idéalement NVS chiffré).
*   **Implémentation complète de TLS/mTLS**:
    *   Activer et configurer MQTT sur TLS (`mqtts://`) comme décrit conceptuellement dans `CONFIGURATION_GUIDE.md`.
    *   Mettre en place une infrastructure pour la gestion des certificats (CA du serveur, certificats et clés clients pour mTLS si nécessaire).
    *   Assurer le stockage sécurisé des clés privées sur les dispositifs (par exemple, en utilisant la flash encryption de l'ESP32).
*   **Revue de sécurité générale**:
    *   Analyser les flux de données pour identifier les vulnérabilités potentielles.
    *   Sécuriser l'accès physique aux dispositifs si possible.
    *   Considérer la sécurité des mises à jour OTA (signature du firmware).

## 5. Robustesse et Fiabilité

*   **Persistance d'état critique en NVS**:
    *   Sauvegarder en NVS certains états critiques pour permettre une reprise correcte après un redémarrage inattendu du module maître. Par exemple :
        *   L'état `in_potential_fall_state` et `potential_fall_start_time_ms` du `FallDetector_task`.
        *   Les derniers timestamps connus des modules esclaves (`last_received_timestamp_ms`) pour le `Watchdog_task`.
*   **Mise en place d'un framework de tests unitaires et d'intégration sur cible**:
    *   Utiliser Unity (fourni avec ESP-IDF) pour écrire des tests unitaires exécutables sur les ESP32. Cela nécessitera de refactoriser le code pour rendre les fonctions plus testables (par exemple, en évitant les fonctions statiques pour les unités sous test, en utilisant l'injection de dépendances).
    *   Développer des scénarios de test d'intégration qui peuvent être exécutés sur le matériel assemblé.
*   **Stratégies de watchdog matériel**:
    *   Configurer les watchdogs matériels (Task Watchdog Timer - TWDT, et Interrupt Watchdog Timer - IWDT) de l'ESP32 pour détecter les blocages de tâches ou les interruptions critiques et redémarrer le système si nécessaire.
    *   Le `Watchdog_task` actuel est un watchdog logiciel pour les modules esclaves ; il ne remplace pas les watchdogs matériels pour la stabilité du module maître lui-même.
*   **Gestion d'erreurs plus détaillée et mécanismes de récupération**:
    *   Améliorer la journalisation des erreurs (`esp_err_t` codes, etc.).
    *   Implémenter des stratégies de récupération pour certaines erreurs (par exemple, redémarrage contrôlé de sous-systèmes comme MQTT ou Wi-Fi si des erreurs répétées sont détectées, plutôt que de simplement suspendre une tâche).

## 6. Fonctionnalités Additionnelles

*   **Implémentation complète des alertes physiques**:
    *   Activer le code pour le buzzer et/ou la LED dans `AlertManager_task` en utilisant des broches GPIO configurables.
*   **Implémentation des notifications externes**:
    *   Ajouter la logique pour envoyer des requêtes HTTP POST à un serveur distant ou envoyer des e-mails en cas d'alerte (via des services tiers comme IFTTT, SendGrid, ou un serveur SMTP direct si possible). Cela nécessiterait des bibliothèques HTTP et/ou SMTP.
*   **Développement d'une interface utilisateur ou d'un dashboard**:
    *   Une application mobile ou une interface web pourrait afficher l'état du système, la position estimée de la personne, l'historique des alertes, et permettre la configuration.
*   **Support complet de l'OTA (Over-The-Air update)**:
    *   Implémenter la mise à jour OTA pour les firmwares des modules maître et esclaves. ESP-IDF fournit des exemples robustes pour cela. Cela simplifierait grandement la maintenance et le déploiement de nouvelles versions.

## 7. Intégration Continue et Déploiement Continu (CI/CD)

*   **Mise en place de pipelines (ex: GitHub Actions)** pour automatiser :
    *   **Compilation automatique**: Compiler les firmwares pour chaque commit ou pull request pour détecter les erreurs tôt.
    *   **Tests automatisés**:
        *   Exécuter des linters (par exemple, `clang-format`, `cppcheck`) pour la qualité du code.
        *   Exécuter des tests unitaires (ceux écrits avec Unity pourraient être exécutés sur des cibles simulées ou réelles si une infrastructure de test sur cible est disponible).
    *   **Déploiement automatisé (optionnel)**:
        *   Déployer automatiquement les firmwares compilés vers un environnement de test (par exemple, via OTA vers des dispositifs de test désignés) après des tests réussis.

Ces améliorations contribueraient de manière significative à la maturité et à la valeur du projet. Elles devraient être priorisées en fonction des besoins des utilisateurs finaux et des ressources disponibles.
