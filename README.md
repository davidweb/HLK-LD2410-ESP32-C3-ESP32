# Module Surveillance HLK-LD2410-ESP32-C3-ESP32

Système de surveillance intelligent basé sur des capteurs radar HLK-LD2410, conçu pour détecter la présence humaine, surveiller les postures (debout, assis, couché) et alerter en cas de chute. Chaque module capteur est piloté par un ESP32-C3, tandis qu'un ESP32-WROOM-32 centralise les données et gère les alertes.

## 🧠 Objectif du projet

Fournir une solution de surveillance non intrusive pour assurer la sécurité des personnes, notamment des personnes âgées, en détectant les chutes et en surveillant les activités dans différentes pièces.

## 🧩 Architecture du système

* **Modules capteurs (ESP32-C3)** :

  * Connectés à des capteurs radar HLK-LD2410 via UART.
  * Mesurent la distance et la posture des individus.
  * Envoient les données via Wi-Fi au module maître.

* **Module maître (ESP32-WROOM-32)** :

  * Reçoit les données des modules capteurs.
  * Fusionne les informations pour déterminer la position et la posture.
  * Détecte les chutes et envoie des alertes via MQTT ou autres moyens.

## 🔧 Matériel requis

* 2 × ESP32-C3 (modules capteurs)
* 1 × ESP32-WROOM-32 (module maître)
* 2 × Capteurs radar HLK-LD2410
* Alimentation 5V pour chaque module
* Connexion Wi-Fi stable

## 🛠️ Installation et configuration

1. **Cloner le dépôt** :

   ```bash
   git clone https://github.com/davidweb/HLK-LD2410-ESP32-C3-ESP32.git
   ```

2. **Configurer les modules capteurs** :

   * Flasher le firmware sur chaque ESP32-C3.
   * Connecter les capteurs HLK-LD2410 via UART.
   * Configurer les paramètres Wi-Fi.

3. **Configurer le module maître** :

   * Flasher le firmware sur l'ESP32-WROOM-32.
   * Configurer les paramètres Wi-Fi et MQTT.

4. **Démarrer le système** :

   * Alimenter les modules.
   * Vérifier la communication entre les modules capteurs et le maître.
   * Surveiller les alertes via le broker MQTT ou l'interface prévue.

## 📁 Structure du dépôt

```
Module-surveillance-v1-/
├── master_firmware
│   ├── main/
│   │   ├── main.c
│   │   └── CMakeLists.txt
│   └── test/
│   │   ├── CMakeLists.txt
│   │   ├── test_alert_manager.c
│   │   ├── test_fall_detector.c
│   │   ├── test_fusion_engine.c
│   │   └── test_main.c
├── slave_firmware/
│   ├── main/
│   │   ├── main.c
│   │   └── CMakeLists.txt
│   └── test/
│   │   ├── CMakeLists.txt
│   │   ├── test_main.c
│   │   ├── test_mqtt_utils.c
│   │   └── test_radar_utils.c
├── docs/
│   │   ├── CONFIGURATION_GUIDE.md
│   │   ├── DEPLOYMENT_MAINTENANCE_GUIDE.md
│   │   ├── FUTURE_IMPROVEMENTS.md
│   │   ├── TESTING_SCENARIOS.md
│   │   └── WIRING_DIAGRAM.md
├── README.md
```



## 📜 Licence

Ce projet est sous licence MIT. Voir le fichier [LICENSE](LICENSE) pour plus d'informations.

## 🤝 Contribuer

Les contributions sont les bienvenues !

1. Forkez le dépôt.
2. Créez une branche pour votre fonctionnalité (`git checkout -b feature/ma-fonctionnalité`).
3. Commitez vos modifications (`git commit -am 'Ajout de ma fonctionnalité'`).
4. Poussez la branche (`git push origin feature/ma-fonctionnalité`).
5. Créez une Pull Request.([GitHub][1])

## 📬 Contact

Pour toute question ou suggestion, veuillez ouvrir une issue sur le dépôt GitHub ou contacter [Neorak](https://github.com/davidweb).


