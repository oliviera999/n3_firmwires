#!/usr/bin/env python3
"""
Script de test pour valider la fonctionnalité OTA filesystem
"""

import json
import os
import hashlib
import requests
from pathlib import Path

def calculate_md5(filepath):
    """Calcule le MD5 d'un fichier"""
    hash_md5 = hashlib.md5()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def create_test_filesystem():
    """Crée un filesystem de test avec des pages web simples"""
    print("📁 Création du filesystem de test...")
    
    # Créer le dossier de test
    test_dir = Path("test_filesystem")
    test_dir.mkdir(exist_ok=True)
    
    # Créer index.html
    index_html = """<!DOCTYPE html>
<html>
<head>
    <title>Test OTA Filesystem</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .header { color: #2c3e50; border-bottom: 2px solid #3498db; }
        .info { background: #ecf0f1; padding: 20px; border-radius: 5px; }
    </style>
</head>
<body>
    <h1 class="header">🚀 Test OTA Filesystem</h1>
    <div class="info">
        <p><strong>Version:</strong> 10.12-OTA-TEST</p>
        <p><strong>Fonctionnalité:</strong> Mise à jour des pages web via OTA</p>
        <p><strong>Timestamp:</strong> """ + str(int(__import__('time').time())) + """</p>
    </div>
    <p>Cette page a été mise à jour via OTA filesystem !</p>
</body>
</html>"""
    
    with open(test_dir / "index.html", "w", encoding="utf-8") as f:
        f.write(index_html)
    
    # Créer dashboard.css
    dashboard_css = """/* Dashboard CSS - Version OTA Test */
.dashboard {
    max-width: 1200px;
    margin: 0 auto;
    padding: 20px;
}

.card {
    background: #fff;
    border-radius: 8px;
    box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    padding: 20px;
    margin: 10px 0;
}

.ota-success {
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    text-align: center;
    padding: 30px;
    border-radius: 10px;
}

.ota-success h2 {
    margin: 0 0 10px 0;
    font-size: 2em;
}

.ota-success p {
    margin: 5px 0;
    opacity: 0.9;
}"""
    
    with open(test_dir / "dashboard.css", "w", encoding="utf-8") as f:
        f.write(dashboard_css)
    
    # Créer dashboard.js
    dashboard_js = """// Dashboard JS - Version OTA Test
console.log('🚀 Dashboard OTA Test chargé !');

document.addEventListener('DOMContentLoaded', function() {
    // Afficher la version OTA
    const versionElement = document.getElementById('ota-version');
    if (versionElement) {
        versionElement.textContent = '10.12-OTA-TEST';
    }
    
    // Animation de chargement
    const cards = document.querySelectorAll('.card');
    cards.forEach((card, index) => {
        card.style.opacity = '0';
        card.style.transform = 'translateY(20px)';
        
        setTimeout(() => {
            card.style.transition = 'all 0.3s ease';
            card.style.opacity = '1';
            card.style.transform = 'translateY(0)';
        }, index * 100);
    });
    
    console.log('✅ Dashboard OTA Test initialisé');
});"""
    
    with open(test_dir / "dashboard.js", "w", encoding="utf-8") as f:
        f.write(dashboard_js)
    
    print(f"✅ Filesystem de test créé dans {test_dir}")
    return test_dir

def create_metadata_json(firmware_size, firmware_md5, filesystem_size, filesystem_md5):
    """Crée un fichier metadata.json de test"""
    print("📄 Création du fichier metadata.json...")
    
    metadata = {
        "version": "10.12-OTA-TEST",
        "bin_url": "https://server.com/ota/esp32-s3/firmware.bin",
        "size": firmware_size,
        "md5": firmware_md5,
        "filesystem_url": "https://server.com/ota/esp32-s3/filesystem.bin",
        "filesystem_size": filesystem_size,
        "filesystem_md5": filesystem_md5,
        "channels": {
            "prod": {
                "esp32-s3": {
                    "bin_url": "https://server.com/ota/esp32-s3/firmware.bin",
                    "version": "10.12-OTA-TEST",
                    "size": firmware_size,
                    "md5": firmware_md5,
                    "filesystem_url": "https://server.com/ota/esp32-s3/filesystem.bin",
                    "filesystem_size": filesystem_size,
                    "filesystem_md5": filesystem_md5
                },
                "esp32-wroom": {
                    "bin_url": "https://server.com/ota/esp32-wroom/firmware.bin",
                    "version": "10.12-OTA-TEST",
                    "size": firmware_size,
                    "md5": firmware_md5,
                    "filesystem_url": "https://server.com/ota/esp32-wroom/filesystem.bin",
                    "filesystem_size": filesystem_size,
                    "filesystem_md5": filesystem_md5
                }
            },
            "test": {
                "esp32-s3": {
                    "bin_url": "https://server.com/ota/test/esp32-s3/firmware.bin",
                    "version": "10.12-OTA-TEST",
                    "size": firmware_size,
                    "md5": firmware_md5,
                    "filesystem_url": "https://server.com/ota/test/esp32-s3/filesystem.bin",
                    "filesystem_size": filesystem_size,
                    "filesystem_md5": filesystem_md5
                }
            }
        }
    }
    
    with open("metadata_test.json", "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2)
    
    print("✅ metadata_test.json créé")

def validate_metadata_structure():
    """Valide la structure du fichier metadata.json"""
    print("🔍 Validation de la structure metadata.json...")
    
    try:
        with open("metadata_test.json", "r", encoding="utf-8") as f:
            metadata = json.load(f)
        
        # Vérifier les champs obligatoires
        required_fields = ["version", "bin_url", "size", "md5"]
        optional_fields = ["filesystem_url", "filesystem_size", "filesystem_md5"]
        
        for field in required_fields:
            if field not in metadata:
                print(f"❌ Champ manquant: {field}")
                return False
        
        # Vérifier les champs filesystem
        has_filesystem = any(field in metadata for field in optional_fields)
        if has_filesystem:
            print("✅ Champs filesystem détectés")
            for field in optional_fields:
                if field in metadata:
                    print(f"  - {field}: {metadata[field]}")
        else:
            print("ℹ️ Aucun champ filesystem (mise à jour firmware uniquement)")
        
        # Vérifier la structure channels
        if "channels" in metadata:
            print("✅ Structure channels détectée")
            for env, models in metadata["channels"].items():
                print(f"  - Environnement: {env}")
                for model, config in models.items():
                    print(f"    - Modèle: {model}")
                    if "filesystem_url" in config:
                        print(f"      - Filesystem: {config['filesystem_size']} bytes")
        
        print("✅ Structure metadata.json valide")
        return True
        
    except Exception as e:
        print(f"❌ Erreur validation: {e}")
        return False

def simulate_ota_selection():
    """Simule la logique de sélection OTA"""
    print("🎯 Simulation de la sélection OTA...")
    
    try:
        with open("metadata_test.json", "r", encoding="utf-8") as f:
            metadata = json.load(f)
        
        # Simulation pour ESP32-S3 en mode test
        env = "test"
        model = "esp32-s3"
        
        print(f"🔎 Sélection pour env={env}, model={model}")
        
        # Logique de sélection (simplifiée)
        if "channels" in metadata and env in metadata["channels"]:
            if model in metadata["channels"][env]:
                config = metadata["channels"][env][model]
                print(f"✅ Configuration trouvée pour {env}/{model}")
                print(f"  - Firmware: {config.get('bin_url', 'N/A')}")
                print(f"  - Version: {config.get('version', 'N/A')}")
                print(f"  - Taille: {config.get('size', 0)} bytes")
                if "filesystem_url" in config:
                    print(f"  - Filesystem: {config.get('filesystem_url', 'N/A')}")
                    print(f"  - Taille FS: {config.get('filesystem_size', 0)} bytes")
                return True
        
        print(f"⚠️ Configuration non trouvée pour {env}/{model}")
        return False
        
    except Exception as e:
        print(f"❌ Erreur simulation: {e}")
        return False

def main():
    """Fonction principale"""
    print("🚀 Test de la fonctionnalité OTA Filesystem")
    print("=" * 50)
    
    # Créer le filesystem de test
    test_dir = create_test_filesystem()
    
    # Calculer les tailles et MD5 (simulés pour le test)
    firmware_size = 1678528  # Taille typique
    firmware_md5 = "a1b2c3d4e5f6789012345678901234567"
    
    # Calculer la taille réelle du filesystem
    filesystem_size = sum(f.stat().st_size for f in test_dir.rglob('*') if f.is_file())
    filesystem_md5 = "f6e5d4c3b2a1908765432109876543210"  # MD5 simulé
    
    print(f"📊 Tailles:")
    print(f"  - Firmware: {firmware_size:,} bytes")
    print(f"  - Filesystem: {filesystem_size:,} bytes")
    
    # Créer le metadata.json
    create_metadata_json(firmware_size, firmware_md5, filesystem_size, filesystem_md5)
    
    # Valider la structure
    if validate_metadata_structure():
        print("\n✅ Structure metadata.json valide")
    else:
        print("\n❌ Structure metadata.json invalide")
        return
    
    # Simuler la sélection OTA
    if simulate_ota_selection():
        print("\n✅ Simulation sélection OTA réussie")
    else:
        print("\n❌ Simulation sélection OTA échouée")
        return
    
    print("\n🎉 Test OTA Filesystem terminé avec succès !")
    print("\n📋 Prochaines étapes:")
    print("1. Utiliser mklittlefs pour créer le fichier filesystem.bin")
    print("2. Déployer les fichiers sur le serveur web")
    print("3. Tester la mise à jour OTA sur l'ESP32")

if __name__ == "__main__":
    main()
