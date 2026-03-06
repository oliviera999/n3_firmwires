#!/usr/bin/env python3
"""
Script de minification des assets JS/CSS pour production ESP32
Réduit la taille des fichiers de ~35-40% pour optimiser la mémoire Flash
"""

import os
import sys
import re
import json
from pathlib import Path

def minify_js(content):
    """Minification JavaScript simple mais efficace"""
    # Supprimer commentaires multi-lignes
    content = re.sub(r'/\*[\s\S]*?\*/', '', content)
    
    # Supprimer commentaires simples (mais garder URLs)
    content = re.sub(r'(?<!:)//.*$', '', content, flags=re.MULTILINE)
    
    # Supprimer espaces multiples
    content = re.sub(r'\s+', ' ', content)
    
    # Supprimer espaces autour des opérateurs
    content = re.sub(r'\s*([=+\-*/<>!&|,;:{}()\[\]])\s*', r'\1', content)
    
    # Supprimer lignes vides
    content = re.sub(r'\n\s*\n', '\n', content)
    
    # Retirer espaces en début/fin
    content = content.strip()
    
    return content

def minify_css(content):
    """Minification CSS"""
    # Supprimer commentaires
    content = re.sub(r'/\*[\s\S]*?\*/', '', content)
    
    # Supprimer espaces multiples
    content = re.sub(r'\s+', ' ', content)
    
    # Supprimer espaces autour des opérateurs CSS
    content = re.sub(r'\s*([{}:;,])\s*', r'\1', content)
    
    # Supprimer dernier point-virgule avant }
    content = re.sub(r';\}', '}', content)
    
    # Supprimer lignes vides
    content = re.sub(r'\n\s*\n', '\n', content)
    
    return content.strip()

def minify_html(content):
    """Minification HTML simple"""
    # Supprimer commentaires HTML (sauf ceux avec conditions IE)
    content = re.sub(r'<!--(?!\[if).*?-->', '', content, flags=re.DOTALL)
    
    # Supprimer espaces entre tags
    content = re.sub(r'>\s+<', '><', content)
    
    # Supprimer espaces multiples
    content = re.sub(r'\s+', ' ', content)
    
    return content.strip()

def process_file(input_path, output_path, file_type):
    """Traiter un fichier selon son type"""
    print(f"📄 Processing {input_path.name}...", end=' ')
    
    with open(input_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    original_size = len(content)
    
    if file_type == 'js':
        minified = minify_js(content)
    elif file_type == 'css':
        minified = minify_css(content)
    elif file_type == 'html':
        minified = minify_html(content)
    else:
        minified = content
    
    minified_size = len(minified)
    reduction = ((original_size - minified_size) / original_size) * 100
    
    # Créer dossier de sortie si nécessaire
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(minified)
    
    print(f"✅ {original_size} → {minified_size} bytes (-{reduction:.1f}%)")
    
    return original_size, minified_size

def main():
    """Fonction principale"""
    print("=" * 60)
    print("🚀 Minification Assets pour Production ESP32")
    print("=" * 60)
    
    # Chemins
    project_root = Path(__file__).parent.parent
    data_dir = project_root / 'data'
    data_min_dir = project_root / 'data_minified'
    
    if not data_dir.exists():
        print(f"❌ Erreur: dossier {data_dir} introuvable")
        sys.exit(1)
    
    # Statistiques globales
    total_original = 0
    total_minified = 0
    files_processed = 0
    
    # Liste des fichiers à minifier
    files_to_process = [
        # JavaScript
        ('shared/common.js', 'js'),
        ('shared/websocket.js', 'js'),
        
        # CSS
        ('shared/common.css', 'css'),
        
        # HTML
        ('index.html', 'html'),
        ('pages/controles.html', 'html'),
        ('pages/wifi.html', 'html'),
    ]
    
    print(f"\n📁 Source: {data_dir}")
    print(f"📁 Destination: {data_min_dir}\n")
    
    for file_path, file_type in files_to_process:
        input_path = data_dir / file_path
        output_path = data_min_dir / file_path
        
        if input_path.exists():
            orig, mini = process_file(input_path, output_path, file_type)
            total_original += orig
            total_minified += mini
            files_processed += 1
        else:
            print(f"⚠️  Fichier ignoré (inexistant): {file_path}")
    
    # Copier les fichiers non minifiables (assets, images, etc.)
    print("\n📦 Copie des assets non-minifiables...")
    
    import shutil
    
    # Copier assets (déjà minifiés)
    if (data_dir / 'assets').exists():
        shutil.copytree(data_dir / 'assets', data_min_dir / 'assets', dirs_exist_ok=True)
        print("✅ assets/ copié")
    
    # Copier manifest.json
    if (data_dir / 'manifest.json').exists():
        shutil.copy2(data_dir / 'manifest.json', data_min_dir / 'manifest.json')
        print("✅ manifest.json copié")
    
    # Résumé
    print("\n" + "=" * 60)
    print("📊 RÉSUMÉ")
    print("=" * 60)
    print(f"Fichiers traités: {files_processed}")
    print(f"Taille originale: {total_original:,} bytes")
    print(f"Taille minifiée:  {total_minified:,} bytes")
    
    total_reduction = ((total_original - total_minified) / total_original) * 100
    bytes_saved = total_original - total_minified
    
    print(f"Réduction totale: -{total_reduction:.1f}% ({bytes_saved:,} bytes économisés)")
    print("\n✅ Minification terminée avec succès!")
    print(f"\n💡 Pour uploader: pio run --target uploadfs --upload-port COM_PORT")
    print(f"   (Remplacer data/ par data_minified/ dans platformio.ini)")

if __name__ == '__main__':
    main()

