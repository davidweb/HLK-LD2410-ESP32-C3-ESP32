# Guide de Configuration du Système de Détection de Chute

## 1. Introduction

Ce document fournit un guide pour configurer les différents aspects du système de détection de chute, incluant le firmware des modules esclaves (ESP32-C3) et du module maître (ESP32-WROOM-32).

## 2. Configuration Wi-Fi

La connexion Wi-Fi est essentielle pour la communication MQTT entre les modules.

### 2.1. Module Esclave (ESP32-C3)

*   **Configuration Actuelle**:
    *   Les identifiants Wi-Fi (SSID et mot de passe) sont actuellement codés en dur dans `slave_firmware/main/main.c` via les constantes suivantes :
        *   `EXAMPLE_ESP_WIFI_SSID`
        *   `EXAMPLE_ESP_WIFI_PASS`
    *   Ces valeurs doivent être modifiées directement dans le code source avant la compilation et le flashage.

*   **Améliorations Possibles**:
    *   **NVS (Non-Volatile Storage)**: Pour une configuration plus dynamique sans recompiler, les identifiants Wi-Fi pourraient être stockés en NVS. Le firmware lirait ces valeurs au démarrage. Une interface (par exemple, UART ou BLE) pourrait être utilisée pour initialiser ou mettre à jour ces valeurs en NVS.
    *   **Provisionnement Wi-Fi**: Des mécanismes plus avancés comme SmartConfig, WPS, ou une configuration via une interface web ou BLE pourraient être implémentés pour permettre à l'utilisateur de configurer le Wi-Fi après le flashage initial.

### 2.2. Module Maître (ESP32-WROOM-32)

*   **Configuration Actuelle**:
    *   De même, les identifiants Wi-Fi sont codés en dur dans `master_firmware/main/main.c` :
        *   `MASTER_ESP_WIFI_SSID`
        *   `MASTER_ESP_WIFI_PASS`
    *   Ces valeurs doivent être modifiées directement dans le code source.

*   **Améliorations Possibles**:
    *   Les mêmes améliorations que pour le module esclave (NVS, mécanismes de provisionnement) sont applicables ici pour une configuration Wi-Fi plus flexible.

## 3. Configuration MQTT

MQTT est utilisé pour la communication des données radar des esclaves vers le maître, et pour la publication des alertes par le maître.

### 3.1. Broker MQTT

*   **Module Esclave**:
    *   L'URL du broker MQTT est définie par la constante `CONFIG_BROKER_URL` dans `slave_firmware/main/main.c`.
    *   Exemple: `mqtt://192.168.1.100`
*   **Module Maître**:
    *   L'URL du broker MQTT est définie par la constante `MASTER_CONFIG_BROKER_URL` dans `master_firmware/main/main.c`.
    *   Exemple: `mqtt://192.168.1.100`

*   **Remarques**:
    *   Ces URLs doivent pointer vers l'adresse IP (ou nom d'hôte) et le port de votre broker MQTT.
    *   Si le module maître doit héberger le broker MQTT (ce qui n'est pas implémenté actuellement), cette URL pointerait vers l'adresse IP du maître lui-même sur le réseau local.
    *   Pour des tests, un broker MQTT public ou local (ex: Mosquitto) peut être utilisé.

### 3.2. Topics MQTT

*   **Modules Esclaves**:
    *   Le topic de publication des données radar est construit dynamiquement.
    *   La base du topic (ex: `home/room1/radar%d`) est définie par `MQTT_TOPIC_RADAR_DATA` dans `slave_firmware/main/main.c` (bien que le code actuel utilise `MQTT_TOPIC_RADAR_DATA` comme un topic fixe "home/room1/radar1" dans `WiFiTask_task` pour les données *dummy*. La logique de `format_radar_json` dans `RadarTask_task` n'est pas encore utilisée pour construire dynamiquement le topic pour la publication MQTT).
    *   L'`id_module` (voir section 4) est destiné à être ajouté dynamiquement pour différencier les esclaves (par exemple, `home/room1/radar1`, `home/room1/radar2`).

*   **Module Maître**:
    *   **Souscription**: Le maître souscrit au topic wildcard `HOME_MQTT_TOPIC_WILDCARD` (actuellement `"home/+/radar+"`) défini dans `master_firmware/main/main.c`. Cela lui permet de recevoir les données de tous les modules radar sous le chemin `home/*/radar*`.
    *   **Publication des Alertes**: Les alertes (chutes, modules hors ligne) sont publiées sur le topic `ALERT_TOPIC` (actuellement `"home/room1/alert"`) défini dans `master_firmware/main/main.c`.

*   **Améliorations Possibles**:
    *   Les chaînes de base des topics et les topics d'alerte pourraient être rendus configurables, par exemple via NVS ou intégrés dans le processus du script de calibration, pour une plus grande flexibilité de déploiement.

## 4. Configuration des Identifiants des Modules Esclaves

*   **Configuration Actuelle**:
    *   L'`id_module` (par exemple, 1 ou 2) est actuellement codé en dur dans `slave_firmware/main/main.c` au sein de la fonction `format_radar_json` (`RADAR_MODULE_ID`).
    *   Cette valeur est cruciale pour que le module maître puisse distinguer les données provenant de différents capteurs esclaves.

*   **Améliorations Recommandées**:
    *   **Au Moment du Flashage**: L'ID du module pourrait être défini comme une constante unique au moment de la compilation pour chaque firmware esclave.
    *   **Lecture d'une Broche GPIO**: Une ou plusieurs broches GPIO pourraient être utilisées pour définir l'ID. Par exemple, des résistances de tirage (pull-up/pull-down) sur certaines broches pourraient être lues au démarrage pour déterminer un ID binaire.
    *   **Configuration NVS**: Stocker un ID unique en NVS pour chaque module.

## 5. Configuration TLS pour MQTT (Conceptuelle)

Le cahier des charges mentionne l'utilisation de TLS pour sécuriser les communications MQTT. L'implémentation actuelle n'inclut pas TLS, mais voici comment cela pourrait être abordé avec ESP-IDF.

*   **Support ESP-IDF**: La structure `esp_mqtt_client_config_t` dans ESP-IDF supporte la configuration TLS via les champs suivants :
    *   `cert_pem`: Pointeur vers une chaîne de caractères contenant le certificat du serveur CA (Certificate Authority) pour valider le serveur MQTT.
    *   `client_cert_pem` (pour mTLS - Mutual TLS): Pointeur vers le certificat du client MQTT.
    *   `client_key_pem` (pour mTLS): Pointeur vers la clé privée du client MQTT.

*   **Implémentation Réelle**:
    1.  **Génération des Certificats**:
        *   Obtenez un certificat de CA pour votre broker MQTT.
        *   Si mTLS est requis, générez une paire clé privée/certificat pour chaque client MQTT (chaque module ESP32).
        *   Ces certificats peuvent être obtenus d'une CA reconnue, ou auto-signés pour des environnements de test/privés.
    2.  **Stockage des Certificats**:
        *   **Embarqués dans le Firmware**: Les certificats (sous forme de chaînes PEM) peuvent être directement inclus dans le code source comme des chaînes de caractères ( `const char* my_ca_cert = "-----BEGIN CERTIFICATE-----\n...";`).
        *   **Système de Fichiers**: Pour plus de flexibilité, les certificats peuvent être stockés sur un système de fichiers (SPIFFS ou LittleFS) sur la flash de l'ESP32 et chargés au démarrage. Cela permet de les mettre à jour sans recompiler tout le firmware.
    3.  **Configuration du Client MQTT**:
        *   Dans `master_mqtt_app_start()` (pour le maître) et une fonction similaire dans l'esclave, la structure `esp_mqtt_client_config_t` serait mise à jour pour inclure les pointeurs vers les certificats chargés. Par exemple :
            ```c
            // const char *server_ca_pem = "---BEGIN CERTIFICATE---..."; // Chargé ou défini
            // const char *client_cert_pem_str = "---BEGIN CERTIFICATE---..."; // Chargé ou défini
            // const char *client_key_pem_str = "---BEGIN PRIVATE KEY---..."; // Chargé ou défini
            
            esp_mqtt_client_config_t mqtt_cfg = {
                .broker.address.uri = "mqtts://your_broker_url:8883", // Notez mqtts et le port sécurisé
                .broker.verification.certificate = server_ca_pem,
            // Pour mTLS:
            //  .credentials.authentication.certificate = client_cert_pem_str,
            //  .credentials.authentication.key = client_key_pem_str,
            };
            ```
    4.  **Gestion Sécurisée**: La protection des clés privées est cruciale. Si elles sont stockées sur un système de fichiers, celui-ci devrait être chiffré si possible (par exemple, avec NVS chiffré ou des fonctionnalités de flash encryption de l'ESP32).

*   **Note**: L'utilisation de TLS augmente la consommation de mémoire et le temps de connexion. Les ressources de l'ESP32-C3 et de l'ESP32-WROOM-32 sont généralement suffisantes pour TLS.

## 6. Utilisation du Script de Calibration

*   **Référence**: Le script `scripts/calibration_setup.py` est fourni pour aider à déterminer les valeurs de configuration pour le module maître.
*   **Fonctionnement**:
    *   Ce script Python est exécuté sur un ordinateur de développement.
    *   Il pose des questions à l'utilisateur (ou utilise des valeurs par défaut) pour définir :
        *   Les positions X, Y des deux capteurs radar.
        *   Les seuils de détection de chute (`FALL_TRANSITION_MAX_MS`, `LYING_CONFIRMATION_DURATION_S`).
        *   Les seuils du watchdog (`WATCHDOG_CHECK_INTERVAL_S`, `SLAVE_MODULE_TIMEOUT_S`).
    *   **Sortie**: Le script affiche des extraits de code C (`static const float` pour les positions, `#define` pour les seuils).
*   **Action Requise**: L'utilisateur doit **manuellement copier** ces extraits de code C générés et les **coller aux endroits appropriés** dans le fichier `master_firmware/main/main.c`.
*   **Recompilation**: Après avoir modifié `master_firmware/main/main.c` avec les nouvelles valeurs, le firmware du module maître doit être recompilé et reflashé pour que les changements prennent effet.

Ce guide devrait aider à la configuration et à la personnalisation du système. Pour des déploiements en production, il est fortement recommandé d'implémenter des mécanismes de configuration plus dynamiques et sécurisés que les constantes codées en dur, en particulier pour les informations sensibles comme les identifiants Wi-Fi et les certificats.
