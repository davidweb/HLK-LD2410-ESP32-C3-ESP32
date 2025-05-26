# Diagramme de Câblage du Système de Détection de Chute

## 1. Introduction

Ce document décrit les connexions matérielles de base nécessaires pour assembler le système de détection de chute. Il est important de noter que les numéros de broches GPIO spécifiques peuvent varier légèrement en fonction des cartes de développement ESP32 exactes utilisées (par exemple, ESP32-C3 Mini, ESP32 DevKitC). Il est recommandé de toujours vérifier le brochage (pinout) de votre carte de développement spécifique. Les connexions décrites ici se basent sur les configurations logicielles actuelles (par exemple, les broches UART définies dans `slave_firmware/main/main.c`).

## 2. Module Esclave (ESP32-C3 avec HLK-LD2410)

Chaque module esclave est composé d'un microcontrôleur ESP32-C3 et d'un capteur radar HLK-LD2410. Répétez ce câblage pour les deux modules esclaves prévus (Module 1 et Module 2).

### 2.1. Alimentation de l'ESP32-C3

*   **`ESP32-C3 5V` ou `VUSB`** (broche d'entrée 5V) -> **Source d'alimentation 5V DC**.
    *   *Note :* Cette source doit être capable de fournir au moins 500 mA pour assurer un fonctionnement stable de l'ESP32-C3 et du capteur radar.
*   **`ESP32-C3 GND`** (broche de masse) -> **Masse commune (GND)** de la source d'alimentation 5V.

### 2.2. Connexion du HLK-LD2410 à l'ESP32-C3

Le capteur HLK-LD2410 communique via UART.

*   **`HLK-LD2410 VCC`** (Broche 1) -> **`ESP32-C3 3.3V`** (broche de sortie 3.3V du régulateur de l'ESP32-C3).
    *   *Note :* Le HLK-LD2410 fonctionne en 3.3V.
*   **`HLK-LD2410 GND`** (Broche 2) -> **`ESP32-C3 GND`** (connecter à la même masse que l'ESP32-C3).
*   **`HLK-LD2410 OUT`** (Broche 3 - UART_TX du capteur) -> **`ESP32-C3 RXD`** (broche de réception UART).
    *   Dans `slave_firmware/main/main.c`, cette broche est configurée comme `RADAR_RXD_PIN` (GPIO20) pour `UART_NUM_1`.
*   **`HLK-LD2410 CLT`** (Broche 4 - UART_RX du capteur) -> **`ESP32-C3 TXD`** (broche de transmission UART).
    *   Dans `slave_firmware/main/main.c`, cette broche est configurée comme `RADAR_TXD_PIN` (GPIO21) pour `UART_NUM_1`.
*   **`HLK-LD2410 EN`** (Broche 5 - Enable/Chip Select) :
    *   Le code actuel (`slave_firmware/main/main.c`) configure et communique avec le module via UART, sans utiliser de contrôle matériel explicite via la broche EN.
    *   Pour une activation permanente du capteur HLK-LD2410, cette broche peut être :
        *   **Tirée à VCC (3.3V)** : Connectez la broche `EN` à la sortie 3.3V de l'ESP32-C3.
        *   **Laissée non connectée (flottante)** : Certains modules s'activent par défaut si EN n'est pas tirée à la masse. Vérifiez la datasheet du HLK-LD2410 pour le comportement par défaut. Par précaution, un tirage à VCC est plus sûr pour garantir l'activation.
    *   Si un contrôle d'alimentation plus fin du capteur est souhaité à l'avenir, cette broche pourrait être connectée à une broche GPIO de l'ESP32-C3.

### 2.3. Note sur les Niveaux Logiques UART

*   Le capteur radar HLK-LD2410 fonctionne avec des niveaux logiques UART de 3.3V.
*   Les broches GPIO de l'ESP32-C3 fonctionnent également en logique 3.3V.
*   Par conséquent, une connexion directe des broches UART est possible sans nécessiter de convertisseur de niveau logique.

## 3. Module Maître (ESP32-WROOM-32)

Le module maître est basé sur un ESP32-WROOM-32.

### 3.1. Alimentation de l'ESP32-WROOM-32

*   **`ESP32-WROOM-32 5V` ou `VUSB`** (broche d'entrée 5V) -> **Source d'alimentation 5V DC**.
    *   *Note :* Cette source doit être capable de fournir au moins 1A, surtout si des périphériques supplémentaires (comme un buzzer puissant) sont envisagés.
*   **`ESP32-WROOM-32 GND`** (broche de masse) -> **Masse commune (GND)** de la source d'alimentation 5V.

### 3.2. (Optionnel) Connexions pour Alertes Physiques

Le code actuel (`master_firmware/main/main.c`) contient des commentaires `TODO` pour l'activation d'alertes physiques. Si ces fonctionnalités sont implémentées, voici des exemples de connexions :

*   **Buzzer Actif (exemple)**:
    *   **`Buzzer +`** (borne positive) -> **`ESP32-WROOM-32 GPIO`** (par exemple, `GPIO25`).
        *   *Note :* Si le buzzer consomme plus de courant que ce qu'une broche GPIO peut fournir directement (généralement ~12mA pour l'ESP32), un transistor (par exemple, NPN comme le 2N2222 ou un MOSFET de niveau logique) doit être utilisé comme interrupteur, commandé par la broche GPIO.
    *   **`Buzzer -`** (borne négative) -> **`ESP32-WROOM-32 GND`**.

*   **LED d'Alerte (exemple)**:
    *   **`LED Anode (+)`** (borne la plus longue) -> **Résistance de limitation de courant** (par exemple, 220-330 Ohms pour une LED standard) -> **`ESP32-WROOM-32 GPIO`** (par exemple, `GPIO26`).
    *   **`LED Cathode (-)`** (borne la plus courte, côté plat) -> **`ESP32-WROOM-32 GND`**.

*   **Rappel**: Ces composants sont optionnels. Leur contrôle effectif (activation/désactivation) nécessite une implémentation logicielle dans `AlertManager_task` ou une fonction dédiée.

## 4. Considérations Générales

*   **Masse Commune (GND)**: Si vous utilisez des sources d'alimentation distinctes pour différents modules (par exemple, un ESP32-C3 alimenté séparément de l'ESP32-WROOM-32), il est crucial de connecter toutes les masses (GND) des différents circuits ensemble. Cela garantit des niveaux de référence communs pour les signaux logiques. Le cahier des charges suggère des alimentations dédiées par module, donc cette interconnexion des masses est importante.
*   **Qualité des Câbles**: Utilisez des câbles de bonne qualité, en particulier pour les connexions d'alimentation (VCC/5V et GND), afin de minimiser les chutes de tension et d'assurer une alimentation stable. Des câbles Dupont courts et de section adéquate sont recommandés pour les prototypes.
*   **Brochages (Pinouts)**: Référez-vous toujours aux schémas de brochage spécifiques de vos cartes de développement ESP32-C3 et ESP32-WROOM-32, car l'emplacement des broches 5V, 3.3V, GND et des GPIO peut varier.
*   **Débogage UART**: Pour le débogage initial, les broches TXD0/RXD0 par défaut de chaque ESP32 (généralement connectées à l'interface USB-Série de la carte de développement) seront utilisées pour la journalisation (`ESP_LOGx`). Assurez-vous que les pilotes USB sont installés sur votre ordinateur.

Ce guide devrait faciliter le câblage initial du système. Procédez avec prudence et vérifiez vos connexions avant de mettre sous tension.
