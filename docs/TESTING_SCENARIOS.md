# Scénarios de Test pour le Système de Détection de Chute

## 1. Introduction

Ce document détaille une série de scénarios de test conçus pour valider la fonctionnalité et la robustesse du système de détection de chute. Ces scénarios sont basés sur l'analyse du code source actuel du firmware (pour `master_firmware` et `slave_firmware`) et sur les exigences du cahier des charges. Ils sont destinés à guider les futurs tests sur matériel réel, où les comportements décrits pourront être observés et validés, notamment grâce aux logs générés par le firmware.

Les constantes de temps mentionnées (par exemple, `FALL_TRANSITION_MAX_MS`) se réfèrent aux valeurs définies dans `master_firmware/main/main.c`.

## 2. Tests de Chute Scénarisés

Ces tests visent à vérifier la logique de détection de chute implémentée dans `FallDetector_task` sur le module maître.

### Scénario 2.1: Chute Confirmée

*   **Description**: Une personne initialement debout (`STANDING_POSTURE`) tombe rapidement en position couchée (`LYING_POSTURE`) et reste immobile dans cette position pendant une durée suffisante pour confirmer la chute.
*   **Séquence d'États/Postures (entrée de `FallDetector_task` via `fusion_output_queue`):**
    1.  `FusedData` avec `final_posture = STANDING_POSTURE`, `timestamp = T0`
    2.  `FusedData` avec `final_posture = LYING_POSTURE`, `timestamp = T1` (où `T1 - T0 < FALL_TRANSITION_MAX_MS`)
    3.  `FusedData` avec `final_posture = LYING_POSTURE`, `timestamp = T2` (où `T2 - T1 >= LYING_CONFIRMATION_DURATION_S * 1000`)
*   **Comportement Attendu de `FallDetector_task`**:
    1.  À la réception de (1), `previous_data` est mis à jour. `in_potential_fall_state` est `false`.
    2.  À la réception de (2):
        *   Log: `Transition to LYING detected. Prev: STANDING, Curr: LYING, Time_diff: <valeur> ms`
        *   Log: `Potential fall detected! Transition time: <valeur> ms. Entering potential fall state.`
        *   `in_potential_fall_state` devient `true`.
        *   `potential_fall_start_time_ms` est mis à `T1`.
    3.  À la réception de (3) (et potentiellement d'autres messages `LYING` entre T1 et T2):
        *   Log: `In potential fall state, current posture: LYING. Lying duration: <valeur> ms.`
        *   Lorsque `(T2 - potential_fall_start_time_ms) >= LYING_CONFIRMATION_DURATION_S * 1000`:
            *   Log: `CHUTE CONFIRMÉE! Lying duration: <valeur> ms.`
            *   Un `AlertMessage` (type `ALERT_TYPE_FALL_DETECTED`, description "Chute détectée à <T2> (Pos: X.XX,Y.YY)") est envoyé à `alert_queue`.
            *   `in_potential_fall_state` redevient `false`.
            *   Log: `Fall state reset after confirmation.`
*   **Message MQTT Final Attendu (sur `home/room1/alert`)**:
    ```json
    {
      "alert_type": "FALL_DETECTED",
      "description": "Chute détectée à <T2> (Pos: 1.00,1.50)", // Position X,Y basée sur le stub actuel
      "timestamp": "<T2>" 
    }
    ```

### Scénario 2.2: Fausse Alerte - Annulation

*   **Description**: Une personne debout se baisse rapidement (simulant une transition vers `LYING_POSTURE`) mais se relève immédiatement en position debout (`STANDING_POSTURE`) avant la fin du délai de confirmation.
*   **Séquence d'États/Postures**:
    1.  `FusedData` avec `final_posture = STANDING_POSTURE`, `timestamp = T0`
    2.  `FusedData` avec `final_posture = LYING_POSTURE`, `timestamp = T1` (où `T1 - T0 < FALL_TRANSITION_MAX_MS`)
    3.  `FusedData` avec `final_posture = STANDING_POSTURE`, `timestamp = T2` (où `T2 - T1 < LYING_CONFIRMATION_DURATION_S * 1000`)
*   **Comportement Attendu de `FallDetector_task`**:
    1.  À la réception de (1), `previous_data` est mis à jour.
    2.  À la réception de (2):
        *   Log: `Transition to LYING detected...`
        *   Log: `Potential fall detected!... Entering potential fall state.`
        *   `in_potential_fall_state` devient `true`.
        *   `potential_fall_start_time_ms` est mis à `T1`.
    3.  À la réception de (3):
        *   Log: `Potential fall cancelled. Person no longer LYING. Current posture: STANDING`
        *   `in_potential_fall_state` redevient `false`.
        *   `potential_fall_start_time_ms` est réinitialisé à 0.
*   **Message MQTT Final Attendu**: Aucun.

### Scénario 2.3: Transition Lente - Pas de Chute

*   **Description**: Une personne debout s'allonge lentement, la transition de `STANDING_POSTURE` à `LYING_POSTURE` prend plus de temps que `FALL_TRANSITION_MAX_MS`.
*   **Séquence d'États/Postures**:
    1.  `FusedData` avec `final_posture = STANDING_POSTURE`, `timestamp = T0`
    2.  (Optionnel) `FusedData` avec `final_posture = SITTING_POSTURE` ou `MOVING_POSTURE`, `timestamp = T_intermediaire`
    3.  `FusedData` avec `final_posture = LYING_POSTURE`, `timestamp = T1` (où `T1 - T0 > FALL_TRANSITION_MAX_MS`)
*   **Comportement Attendu de `FallDetector_task`**:
    1.  À la réception de (1) et (2), `previous_data` est mis à jour.
    2.  À la réception de (3):
        *   Log: `Transition to LYING detected. Prev: <previous_posture>, Curr: LYING, Time_diff: <valeur> ms`
        *   Log: `Transition to LYING too slow (<valeur> ms), not considered a fall trigger.`
        *   `in_potential_fall_state` reste `false`.
*   **Message MQTT Final Attendu**: Aucun.

### Scénario 2.4: Position Assise vers Couché - Pas de Chute

*   **Description**: Une personne initialement assise (`SITTING_POSTURE`) s'allonge. Selon le cahier des charges, cela ne devrait pas déclencher d'alerte de chute.
*   **Séquence d'États/Postures**:
    1.  `FusedData` avec `final_posture = SITTING_POSTURE`, `timestamp = T0`
    2.  `FusedData` avec `final_posture = LYING_POSTURE`, `timestamp = T1` (la durée de transition `T1-T0` peut être rapide ou lente).
*   **Comportement Attendu de `FallDetector_task`**:
    *   La logique actuelle de `FallDetector_task` vérifie si `was_upright_or_moving` était vrai. `SITTING_POSTURE` est inclus dans cette condition.
    *   Si la transition `SITTING` -> `LYING` est rapide (`T1 - T0 < FALL_TRANSITION_MAX_MS`):
        *   Log: `Transition to LYING detected. Prev: SITTING, Curr: LYING, Time_diff: <valeur> ms`
        *   Log: `Potential fall detected! Transition time: <valeur> ms. Entering potential fall state.`
        *   `in_potential_fall_state` devient `true`.
        *   Si la personne reste `LYING` pendant `LYING_CONFIRMATION_DURATION_S`, une alerte SERA générée.
    *   **Note de Vérification**: La logique actuelle du code (`FallDetector_task`) considère `SITTING` comme une posture depuis laquelle une transition rapide vers `LYING` peut initier une détection de chute potentielle. Si le cahier des charges stipule explicitement "pas d'alerte depuis SITTING", alors la condition `was_upright_or_moving` dans `FallDetector_task` devrait être affinée pour exclure `SITTING_POSTURE` ou différencier le traitement. Actuellement, une chute sera détectée.
*   **Message MQTT Final Attendu (selon le code actuel)**: Si la transition est rapide et la position couchée maintenue, une alerte `FALL_DETECTED` sera générée. Si le CdC l'interdit, le code nécessite un ajustement.

## 3. Tests de Robustesse

Ces tests visent à évaluer la réaction du système face à des pannes ou des conditions réseau dégradées.

### Scénario 3.1: Perte de Connexion Wi-Fi du Maître

*   **Condition de Test**: Le module maître perd la connexion au point d'accès Wi-Fi.
*   **Comportement Attendu**:
    *   `master_wifi_event_handler` reçoit `WIFI_EVENT_STA_DISCONNECTED`.
    *   Log: `Disconnected from AP <SSID>`.
    *   `mqtt_connected_flag` passe à `false`.
    *   Le système tente de se reconnecter (`esp_wifi_connect()`) jusqu'à `MASTER_ESP_MAXIMUM_RETRY` fois.
        *   Log: `Retry Wi-Fi connection (X/Y)...`
    *   Si les tentatives échouent, `WIFI_FAIL_BIT` est positionné dans `wifi_event_group`.
        *   Log: `Failed to connect to Wi-Fi AP <SSID> after Y retries.`
    *   `NetworkManager_task` logue périodiquement `NetworkManager: MQTT connection lost or not established. Wi-Fi status: Disconnected`.
*   **Impact sur le Système Global**:
    *   Les alertes générées par `FallDetector_task` ou `Watchdog_task` sont envoyées à `alert_queue`.
    *   `AlertManager_task` reçoit ces alertes mais ne peut pas les publier via MQTT (log: `MQTT not connected. Alert not published via MQTT.`).
    *   Les alertes s'accumulent dans `alert_queue`. Si la file se remplit, les nouvelles alertes pourraient être perdues (selon le comportement de `xQueueSend` avec timeout 0 ou faible).
    *   La réception de données des modules esclaves via MQTT est interrompue.

### Scénario 3.2: Perte de Connexion Wi-Fi d'un Esclave

*   **Condition de Test**: Un module esclave (`slave_firmware`) perd la connexion Wi-Fi.
*   **Comportement Attendu (Esclave)**:
    *   Le `wifi_event_handler` de l'esclave tente des reconnexions (logique similaire à celle du maître).
    *   L'esclave ne peut pas publier ses données radar sur MQTT.
*   **Impact sur le Système Global (Maître)**:
    *   `FusionEngine_task` ne reçoit plus de données du module esclave concerné.
    *   `Watchdog_task` sur le maître détecte l'absence de messages du module esclave après `SLAVE_MODULE_TIMEOUT_S`.
        *   Log: `Module X timed out. Last seen Y ms ago.`
        *   Une `AlertMessage` (type `ALERT_TYPE_MODULE_OFFLINE`) est envoyée à `alert_queue`.
    *   `AlertManager_task` traite cette alerte et la publie via MQTT (si le maître est connecté).
        *   Message MQTT attendu sur `home/room1/alert`: `{"alert_type": "MODULE_OFFLINE", "description": "Module X offline. Last seen Y ms ago.", "timestamp": <timestamp_alerte>}`
    *   Si la triangulation est essentielle, la fusion de données pour la détection de position sera impactée.

### Scénario 3.3: Redémarrage d'un Module Esclave

*   **Condition de Test**: Un module esclave redémarre (simulé par une coupure de courant et un rétablissement).
*   **Comportement Attendu**:
    *   **Esclave**: Redémarre, initialise son Wi-Fi, se connecte au broker MQTT et recommence à envoyer des données radar.
    *   **Maître**:
        *   `FusionEngine_task` reçoit de nouvelles données du module esclave.
        *   `last_received_timestamp_ms` pour ce module est mis à jour.
        *   Si `module_offline_alerted` pour ce module était `true`, il est remis à `false`.
        *   Log: `Module X is back online.`
        *   Aucun message MQTT explicite "MODULE_ONLINE" n'est envoyé par défaut par `AlertManager_task` (mais la logique est commentée pour une future implémentation).
*   **Impact sur le Système Global**: Le système revient à son état de fonctionnement normal pour ce module. Si une alerte "MODULE_OFFLINE" était active pour cet esclave, elle est logiquement annulée (le flag `module_offline_alerted` est réinitialisé).

### Scénario 3.4: Redémarrage du Module Maître

*   **Condition de Test**: Le module maître redémarre.
*   **Comportement Attendu**:
    *   Toutes les tâches sur le maître sont réinitialisées.
    *   `app_main` est exécuté: NVS, files, tâches sont initialisées.
    *   `NetworkManager_task` initialise le Wi-Fi et tente de se connecter au broker MQTT.
    *   Les états internes (comme `in_potential_fall_state`, `sensor1_data_valid`, `last_received_timestamp_ms`) sont réinitialisés à leurs valeurs par défaut.
*   **Impact sur le Système Global**:
    *   Toute détection de chute potentielle en cours est perdue.
    *   L'historique des timestamps des modules esclaves est perdu, le `Watchdog_task` recommence sa surveillance (avec une période de grâce initiale si `last_received_timestamp_ms` est à 0).
    *   Le système reprend la surveillance des données des esclaves dès que la connexion MQTT est rétablie.
    *   C'est un comportement attendu pour un système sans persistance d'état avancée (ex: sauvegarde de l'état en NVS).

### Scénario 3.5: Un Module Esclave ne Répond Plus (Panne)

*   **Condition de Test**: Un module esclave cesse définitivement d'envoyer des données (panne matérielle, bug critique).
*   **Comportement Attendu**:
    *   `Watchdog_task` sur le maître détecte l'absence de messages du module esclave après `SLAVE_MODULE_TIMEOUT_S`.
        *   Log: `Module X timed out. Last seen Y ms ago.` (ou "Module X has never sent data..." si c'est le cas).
        *   Une `AlertMessage` (type `ALERT_TYPE_MODULE_OFFLINE`) est envoyée à `alert_queue`.
        *   `module_offline_alerted` pour ce module est mis à `true`.
    *   `AlertManager_task` traite cette alerte et la publie via MQTT (si le maître est connecté).
*   **Impact sur le Système Global**:
    *   Le système fonctionne en mode dégradé.
    *   `FusionEngine_task` ne pourra plus effectuer de fusion de données impliquant les deux capteurs. La logique actuelle attend les données des deux capteurs pour la triangulation et la posture combinée. Si un capteur est hors ligne, aucune donnée fusionnée ne sera envoyée à `FallDetector_task`.
    *   **Note**: La logique de `FusionEngine_task` pourrait être améliorée pour fonctionner avec un seul capteur (par exemple, en se basant sur la posture d'un seul capteur si l'autre est hors ligne), mais ce n'est pas le comportement actuel.

## 4. Conclusion

Les scénarios décrits ci-dessus fournissent une base pour les tests fonctionnels et de robustesse du système de détection de chute. Ils devront être exécutés sur du matériel réel pour une validation complète. Les messages de log (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`) implémentés dans le firmware seront des outils cruciaux pour observer et valider le comportement interne du système pendant ces tests. Des ajustements des seuils et de la logique pourront s'avérer nécessaires suite aux résultats des tests réels pour optimiser la fiabilité et la performance du système.
