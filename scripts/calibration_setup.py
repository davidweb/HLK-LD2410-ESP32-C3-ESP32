# scripts/calibration_setup.py

"""
Script de Calibration Conceptuel pour le Système de Détection de Chute.

Ce script simule un processus de calibration où l'utilisateur peut définir:
1. Les positions des capteurs radar dans la pièce.
2. Les seuils pour la détection de chute.
3. Les seuils pour le mécanisme de watchdog des modules esclaves.

Le script génère ensuite des extraits de code C que l'utilisateur devrait
copier manuellement dans le firmware du maître (master_firmware/main/main.c).

NOTE: Ce script est purement conceptuel et n'interagit avec aucun matériel.
Les fonctions input() sont utilisées pour simuler l'interaction utilisateur.
"""

import json

def get_float_input(prompt, default_value):
    """
    Simule la récupération d'une entrée flottante de l'utilisateur.
    Renvoie default_value si l'entrée est vide.
    """
    try:
        value_str = input(f"{prompt} (par défaut: {default_value}): ")
        return float(value_str) if value_str else default_value
    except ValueError:
        print(f"Entrée invalide. Utilisation de la valeur par défaut: {default_value}")
        return default_value

def get_int_input(prompt, default_value):
    """
    Simule la récupération d'une entrée entière de l'utilisateur.
    Renvoie default_value si l'entrée est vide.
    """
    try:
        value_str = input(f"{prompt} (par défaut: {default_value}): ")
        return int(value_str) if value_str else default_value
    except ValueError:
        print(f"Entrée invalide. Utilisation de la valeur par défaut: {default_value}")
        return default_value

def configure_sensor_positions():
    """
    Permet à l'utilisateur de configurer les positions des capteurs.
    Retourne un dictionnaire avec les positions des capteurs.
    """
    print("\n--- Configuration des Positions des Capteurs ---")
    print("Veuillez entrer les coordonnées (x, y) en mètres pour chaque capteur.")
    
    sensor1_x = get_float_input("Position X du Capteur 1", 0.0)
    sensor1_y = get_float_input("Position Y du Capteur 1", 0.5)
    sensor2_x = get_float_input("Position X du Capteur 2", 3.0)
    sensor2_y = get_float_input("Position Y du Capteur 2", 0.5)

    return {
        "sensor1_pos": (sensor1_x, sensor1_y),
        "sensor2_pos": (sensor2_x, sensor2_y)
    }

def configure_fall_detection_thresholds():
    """
    Permet à l'utilisateur de configurer les seuils de détection de chute.
    Retourne un dictionnaire avec les seuils.
    """
    print("\n--- Configuration des Seuils de Détection de Chute ---")
    fall_transition_max_ms = get_int_input(
        "Temps max de transition pour une chute (debout à couché) en ms", 1000
    )
    lying_confirmation_duration_s = get_int_input(
        "Durée en position allongée pour confirmer la chute en secondes", 20
    )
    return {
        "fall_transition_max_ms": fall_transition_max_ms,
        "lying_confirmation_duration_s": lying_confirmation_duration_s
    }

def configure_watchdog_thresholds():
    """
    Permet à l'utilisateur de configurer les seuils du watchdog.
    Retourne un dictionnaire avec les seuils.
    """
    print("\n--- Configuration des Seuils du Watchdog des Modules Esclaves ---")
    watchdog_check_interval_s = get_int_input(
        "Intervalle de vérification du watchdog en secondes", 2
    )
    slave_module_timeout_s = get_int_input(
        "Délai d'attente avant de considérer un module esclave comme hors ligne en secondes", 5
    )
    return {
        "watchdog_check_interval_s": watchdog_check_interval_s,
        "slave_module_timeout_s": slave_module_timeout_s
    }

def generate_c_code_snippets(sensor_positions, fall_thresholds, watchdog_thresholds):
    """
    Génère les extraits de code C basés sur les configurations fournies.
    """
    print("\n\n--- Extraits de Code C Générés ---")
    print("Veuillez copier et coller ces extraits dans `master_firmware/main/main.c` comme indiqué.")

    # Extrait pour les positions des capteurs
    print("\n// 1. Pour la fonction `calculate_xy_position` dans master_firmware/main/main.c:")
    print("//    (Remplacez les valeurs existantes ou ajoutez-les si elles n'existent pas)")
    print(f"// static const float SENSOR1_X = {sensor_positions['sensor1_pos'][0]:.2f}f;")
    print(f"// static const float SENSOR1_Y = {sensor_positions['sensor1_pos'][1]:.2f}f;")
    print(f"// static const float SENSOR2_X = {sensor_positions['sensor2_pos'][0]:.2f}f;")
    print(f"// static const float SENSOR2_Y = {sensor_positions['sensor2_pos'][1]:.2f}f;")
    print("// Assurez-vous que ces variables sont utilisées dans la logique de triangulation.")

    # Extrait pour les seuils de détection de chute
    print("\n// 2. Pour les définitions globales (en haut de master_firmware/main/main.c):")
    print(f"#define FALL_TRANSITION_MAX_MS {fall_thresholds['fall_transition_max_ms']}")
    print(f"#define LYING_CONFIRMATION_DURATION_S {fall_thresholds['lying_confirmation_duration_s']}")

    # Extrait pour les seuils du watchdog
    print("\n// 3. Pour les définitions globales (en haut de master_firmware/main/main.c):")
    print(f"#define WATCHDOG_CHECK_INTERVAL_S {watchdog_thresholds['watchdog_check_interval_s']}")
    print(f"#define SLAVE_MODULE_TIMEOUT_S {watchdog_thresholds['slave_module_timeout_s']}")
    
    print("\n--- Fin des Extraits de Code ---")
    print("\nNOTE IMPORTANTE: Après avoir copié ces valeurs, recompilez et reflashez le firmware maître.")


def generate_json_config(sensor_positions, fall_thresholds, watchdog_thresholds):
    """
    Alternativement, génère une configuration JSON.
    Ceci pourrait être utilisé si le firmware était conçu pour lire une configuration
    depuis NVS ou SPIFFS, ce qui n'est pas le cas actuellement.
    """
    config = {
        "sensor_positions": {
            "sensor1": {"x": sensor_positions['sensor1_pos'][0], "y": sensor_positions['sensor1_pos'][1]},
            "sensor2": {"x": sensor_positions['sensor2_pos'][0], "y": sensor_positions['sensor2_pos'][1]}
        },
        "fall_detection_thresholds": fall_thresholds,
        "watchdog_thresholds": watchdog_thresholds
    }
    
    print("\n\n--- Configuration JSON Alternative (pour référence) ---")
    print("Si le firmware supportait la lecture de configuration depuis un fichier JSON, cela pourrait ressembler à ceci:")
    print(json.dumps(config, indent=2))
    print("--- Fin de la Configuration JSON ---")


if __name__ == "__main__":
    print("Bienvenue dans le Script de Calibration Conceptuel.")
    print("Ce script vous aidera à générer des valeurs de configuration pour votre firmware.")
    print("Veuillez répondre aux questions suivantes. Appuyez sur Entrée pour utiliser la valeur par défaut.")

    sensor_config = configure_sensor_positions()
    fall_config = configure_fall_detection_thresholds()
    watchdog_config = configure_watchdog_thresholds()

    generate_c_code_snippets(sensor_config, fall_config, watchdog_config)
    
    # Optionnel: Montrer la sortie JSON comme alternative conceptuelle
    # generate_json_config(sensor_config, fall_config, watchdog_config)

    print("\nCalibration conceptuelle terminée.")
    print("N'oubliez pas de transférer manuellement les extraits de code C générés dans votre firmware.")
