# Guide de Déploiement et de Maintenance du Système de Détection de Chute

## 1. Introduction

*   **Objectif du guide**: Ce guide a pour but d'assister les utilisateurs dans le déploiement initial, la configuration, et la maintenance continue du système de détection de chute.
*   **Rappel des composants**: Le système est composé de :
    *   Deux (2) modules esclaves, chacun comprenant un microcontrôleur ESP32-C3 et un capteur radar HLK-LD2410.
    *   Un (1) module maître, basé sur un microcontrôleur ESP32-WROOM-32.

## 2. Prérequis Logiciels

Avant de commencer, assurez-vous de disposer des outils suivants :

*   **Environnement de développement ESP-IDF**: Une installation fonctionnelle d'ESP-IDF (version 5.x recommandée) est nécessaire pour compiler et flasher les firmwares.
    *   Site officiel : [https://docs.espressif.com/projects/esp-idf/](https://docs.espressif.com/projects/esp-idf/)
*   **Outils de build**: CMake et Ninja sont utilisés par ESP-IDF. Ils sont généralement installés lors de la configuration d'ESP-IDF.
*   **Python**: Python (version 3.6+) est requis pour exécuter le script de calibration (`scripts/calibration_setup.py`).
*   **Client MQTT**: Un client MQTT graphique ou en ligne de commande (par exemple, MQTT Explorer, mosquitto_sub) est très utile pour tester et déboguer les communications MQTT.
*   **Terminal Série**: Un programme de terminal série (par exemple, PuTTY, Minicom, le moniteur série intégré à ESP-IDF) est indispensable pour visualiser les logs des modules ESP32 et pour le débogage.

## 3. Déploiement Initial

Cette section décrit les étapes pour mettre en service le système.

### 3.1. Compilation des Firmwares

*   **Firmware Esclave (`slave_firmware/`)**:
    1.  Ouvrez un terminal dans le répertoire `slave_firmware/`.
    2.  Configurez la cible sur ESP32-C3 : `idf.py set-target esp32c3`
    3.  Compilez le projet : `idf.py build`
    4.  Répétez cette opération pour chaque module esclave. Si des configurations spécifiques par ID sont nécessaires au niveau du code (non recommandé pour la compilation, voir section 4 du `CONFIGURATION_GUIDE.md`), assurez-vous de les appliquer.

*   **Firmware Maître (`master_firmware/`)**:
    1.  Ouvrez un terminal dans le répertoire `master_firmware/`.
    2.  Configurez la cible sur ESP32 : `idf.py set-target esp32`
    3.  Compilez le projet : `idf.py build`

### 3.2. Configuration Initiale (avant flashage)

Consultez le document `docs/CONFIGURATION_GUIDE.md` pour les détails sur la configuration :
*   **Wi-Fi**: Modifiez les constantes `EXAMPLE_ESP_WIFI_SSID` / `EXAMPLE_ESP_WIFI_PASS` (esclave) et `MASTER_ESP_WIFI_SSID` / `MASTER_ESP_WIFI_PASS` (maître) dans les fichiers `main.c` respectifs.
*   **MQTT**: Vérifiez et ajustez `CONFIG_BROKER_URL` (esclave) et `MASTER_CONFIG_BROKER_URL` (maître), ainsi que les topics si nécessaire.
*   **ID du Module Esclave**: Pour chaque module esclave, assurez-vous que la constante `RADAR_MODULE_ID` dans `slave_firmware/main/main.c` est unique (par exemple, 1 pour le premier esclave, 2 pour le second). Cette valeur est utilisée dans les messages JSON et pour la logique de fusion du maître.

### 3.3. Flashage des Firmwares

Connectez chaque module ESP32 à votre ordinateur via USB. Identifiez le port série de chaque module (par exemple, `/dev/ttyUSB0` sur Linux, `COM3` sur Windows).

*   **Pour chaque Module Esclave (ESP32-C3)**:
    *   Depuis le répertoire `slave_firmware/` :
        `idf.py -p /dev/ttyUSB_ESCLAVE_X flash monitor`
        (Remplacez `/dev/ttyUSB_ESCLAVE_X` par le port série correct de l'esclave).
*   **Pour le Module Maître (ESP32-WROOM-32)**:
    *   Depuis le répertoire `master_firmware/` :
        `idf.py -p /dev/ttyUSB_MAITRE flash monitor`
        (Remplacez `/dev/ttyUSB_MAITRE` par le port série correct du maître).

### 3.4. Calibration

Cette étape est cruciale pour la précision de la détection de position et de chute.

1.  **Exécuter le Script de Calibration**:
    *   Sur votre ordinateur de développement, naviguez vers le répertoire `scripts/`.
    *   Exécutez le script : `python calibration_setup.py`
    *   Répondez aux questions pour définir les positions des capteurs (X, Y pour chaque radar), les seuils de détection de chute, et les seuils du watchdog.
2.  **Modifier le Firmware Maître**:
    *   Le script affichera des extraits de code C. Ouvrez `master_firmware/main/main.c`.
    *   Copiez et collez les `#define` pour `FALL_TRANSITION_MAX_MS`, `LYING_CONFIRMATION_DURATION_S`, `WATCHDOG_CHECK_INTERVAL_S`, et `SLAVE_MODULE_TIMEOUT_S` en haut du fichier, en remplaçant les valeurs existantes si nécessaire.
    *   Localisez la fonction `calculate_xy_position`. Vous devrez y intégrer les positions des capteurs (par exemple, en définissant des constantes statiques comme `SENSOR1_X`, `SENSOR1_Y`, etc., et en utilisant ces constantes dans votre logique de triangulation). Le script fournit des exemples de ces constantes.
3.  **Recompiler et Reflasher le Maître**:
    *   Retournez dans le répertoire `master_firmware/`.
    *   Recompilez : `idf.py build`
    *   Reflashez le module maître : `idf.py -p /dev/ttyUSB_MAITRE flash monitor`

### 3.5. Placement Physique des Capteurs

*   **Couverture et Triangulation**: Pour une détection de position efficace par triangulation, les deux capteurs radar (modules esclaves) doivent être placés de manière à couvrir la zone d'intérêt sous des angles différents.
    *   **Espacement**: Évitez de les placer trop près l'un de l'autre ou sur la même ligne de visée par rapport à la zone de surveillance principale. Un espacement de quelques mètres (par exemple, sur des murs opposés ou adjacents) est généralement préférable.
    *   **Hauteur**: Une hauteur typique pourrait être entre 1 et 1.5 mètres, mais cela peut dépendre de la portée verticale du capteur et de la hauteur des plafonds. L'objectif est de détecter les postures debout, assise et couchée.
    *   **Orientation**: Orientez les capteurs vers la zone centrale où la personne est susceptible de se trouver. Évitez les obstructions directes.
*   **Mesure Précise**: Après le placement, mesurez précisément les coordonnées (x, y) de chaque capteur par rapport à un point d'origine commun dans la pièce. Ces mesures sont celles que vous utiliserez dans le script de calibration. La précision de ces mesures affecte directement la précision de la localisation X,Y calculée.

### 3.6. Vérification Initiale

Après le flashage et la calibration :

*   **Logs Série des Esclaves**:
    *   Vérifiez que chaque module esclave se connecte au Wi-Fi.
    *   Vérifiez que le `RadarTask_task` démarre et (simule) la lecture des données radar.
    *   Vérifiez que le `WiFiTask_task` démarre et (simule) la publication MQTT des données.
*   **Logs Série du Maître**:
    *   Vérifiez la connexion Wi-Fi et MQTT du `NetworkManager_task`.
    *   Surveillez les logs de `NetworkManager_task` pour la réception des messages MQTT (topic `home/+/radar+`).
    *   Vérifiez les logs de `FusionEngine_task` pour s'assurer que les données sont reçues, parsées, et que la fusion (même simulée pour la position XY) est tentée.
    *   Vérifiez les logs du `Watchdog_task` pour s'assurer qu'il démarre et (après le délai initial) qu'il ne signale pas de modules hors ligne si les esclaves fonctionnent.
*   **Client MQTT Externe**:
    *   Abonnez-vous au topic `home/+/radar/#` (ou plus spécifiquement, aux topics des esclaves, ex: `home/room1/radar1`) pour voir les données brutes (simulées) des capteurs.
    *   Abonnez-vous au topic `home/room1/alert` pour voir les alertes publiées par le maître (par exemple, si vous simulez une chute ou déconnectez un esclave).

## 4. Maintenance et Dépannage

### 4.1. Surveillance Régulière

*   **Logs**: Si un accès physique ou distant aux logs série est possible, une vérification périodique peut aider à identifier des problèmes latents.
*   **Broker MQTT**: Assurez-vous que le broker MQTT est toujours opérationnel et accessible.
*   **Alertes du Watchdog**: Soyez attentif aux alertes `MODULE_OFFLINE` qui indiquent un problème avec un module esclave.

### 4.2. Mises à Jour du Firmware

*   **Processus Standard**:
    1.  Effectuez les modifications de code souhaitées.
    2.  Recompilez le firmware concerné (`idf.py build`).
    3.  Reflashez le module via UART (`idf.py -p <PORT> flash monitor`).
*   **OTA (Over-The-Air Updates)**:
    *   Actuellement, le système ne supporte pas les mises à jour OTA.
    *   L'implémentation de l'OTA est une amélioration majeure qui permettrait de mettre à jour les firmwares à distance sans connexion physique, ce qui est très utile pour les systèmes déployés. ESP-IDF fournit un support robuste pour l'OTA.

### 4.3. Dépannage Courant

*   **Un module esclave est hors ligne (alerte `MODULE_OFFLINE`)**:
    1.  **Alimentation**: Vérifiez que le module esclave est correctement alimenté.
    2.  **Connexion Wi-Fi**: Accédez aux logs série de l'esclave (si possible) pour vérifier les erreurs de connexion Wi-Fi. Vérifiez que le SSID/mot de passe sont corrects et que le point d'accès fonctionne.
    3.  **Capteur HLK-LD2410**: Vérifiez le câblage entre l'ESP32-C3 et le capteur. Assurez-vous que le capteur est alimenté (VCC/GND).
    4.  **Logs du Maître**: Les logs du `Watchdog_task` peuvent indiquer depuis quand le module est hors ligne.

*   **Pas de données de position / fusion incorrecte (`FusionEngine_task`)**:
    1.  **Vérifiez les Esclaves**: Assurez-vous via un client MQTT que les *deux* modules esclaves publient activement leurs données radar.
    2.  **Calibration des Positions**: Revérifiez l'exactitude des positions des capteurs (X, Y) entrées lors de la calibration et mises à jour dans `master_firmware/main/main.c`. Des erreurs ici mèneront à une triangulation incorrecte.
    3.  **Logs de `FusionEngine_task`**: Examinez les logs pour des erreurs de parsing JSON (si les données sont malformées), des problèmes de synchronisation des timestamps, ou des valeurs de distance inattendues.
    4.  **Logs du `NetworkManager_task`**: Vérifiez que les messages MQTT des esclaves sont bien reçus.

*   **Fausses alertes de chute / non-détection de chutes réelles (`FallDetector_task`)**:
    1.  **Ajuster les Seuils**:
        *   Si des chutes réelles ne sont pas détectées : `FALL_TRANSITION_MAX_MS` est peut-être trop court (la chute prend plus de temps) ou `LYING_CONFIRMATION_DURATION_S` est trop long (la personne est aidée avant confirmation).
        *   Si de fausses alertes sont générées : `FALL_TRANSITION_MAX_MS` est peut-être trop long (des mouvements normaux sont interprétés comme des chutes) ou `LYING_CONFIRMATION_DURATION_S` est trop court.
        *   Utilisez `scripts/calibration_setup.py` pour générer de nouvelles valeurs, modifiez `master_firmware/main/main.c`, recompilez et reflashez le maître.
    2.  **Placement des Capteurs**: Un mauvais placement peut entraîner des données radar imprécises ou des "angles morts", affectant la détection de posture et de position. Revoyez les conseils de la section 3.5.
    3.  **Logs de `FallDetector_task`**: Ces logs sont cruciaux pour comprendre comment les transitions de posture sont interprétées et pourquoi une alerte a été (ou n'a pas été) générée.

*   **Problèmes de connexion MQTT (Maître ou Esclaves)**:
    1.  **Broker MQTT**: Vérifiez que le broker MQTT est en ligne et accessible sur le réseau.
    2.  **Configuration de l'URL**: Assurez-vous que `CONFIG_BROKER_URL` (esclave) et `MASTER_CONFIG_BROKER_URL` (maître) sont corrects dans les firmwares.
    3.  **Connectivité Réseau**: Vérifiez que les modules ESP32 ont une bonne connectivité Wi-Fi et qu'il n'y a pas de pare-feu réseau bloquant l'accès au broker MQTT sur le port utilisé (généralement 1883 pour MQTT non sécurisé, 8883 pour MQTT sur TLS).
    4.  **Logs MQTT**: Les logs des tâches `WiFiTask_task` (esclave) et `NetworkManager_task` (maître) fournissent des informations détaillées sur les tentatives de connexion MQTT et les erreurs (voir `mqtt_event_handler`).

Ce guide sert de point de départ pour le déploiement et la maintenance. Chaque environnement de déploiement peut présenter des défis uniques.
