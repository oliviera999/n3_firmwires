"""
Désactive ESP Insights pour le build S3 en le renommant temporairement.

Évite l'échec https_server.crt.S (chemin dupliqué). Le composant n'est pas utilisé
par le code FFP5CS.
- Env wroom-s3-* : avant build, renomme esp_insights -> esp_insights_disabled.
- Env non-S3 : si esp_insights_disabled existe, le restaure (état du dépôt).
"""

import os
import shutil
Import("env")

PIOENV = env.get("PIOENV", "")
PROJECT_DIR = env.subst("$PROJECT_DIR")
COMPONENT_DIR = os.path.join(PROJECT_DIR, "managed_components", "espressif__esp_insights")
DISABLED_DIR = os.path.join(PROJECT_DIR, "managed_components", "espressif__esp_insights_disabled")

def _disable():
    if os.path.isdir(COMPONENT_DIR) and not os.path.exists(DISABLED_DIR):
        try:
            os.rename(COMPONENT_DIR, DISABLED_DIR)
            print("FFP5CS S3: esp_insights désactivé (renommé -> esp_insights_disabled)")
        except OSError as e:
            print("FFP5CS S3: WARN impossible de désactiver esp_insights: %s" % e)

def _restore():
    if os.path.isdir(DISABLED_DIR) and not os.path.exists(COMPONENT_DIR):
        try:
            os.rename(DISABLED_DIR, COMPONENT_DIR)
            print("FFP5CS: esp_insights restauré")
        except OSError as e:
            print("FFP5CS: WARN impossible de restaurer esp_insights: %s" % e)

if PIOENV.startswith("wroom-s3"):
    _restore()  # remet en place si précédent build S3 l'avait renommé
    _disable()
    # Invalider le cache CMake pour que la reconfig ne trouve plus esp_insights
    build_dir = env.subst("$BUILD_DIR") if env.get("BUILD_DIR") else os.path.join(PROJECT_DIR, ".pio", "build", PIOENV)
    if os.path.isdir(build_dir):
        try:
            shutil.rmtree(build_dir)
            print("FFP5CS S3: cache build supprimé pour reconfig sans esp_insights")
        except OSError as e:
            print("FFP5CS S3: WARN impossible de supprimer le cache: %s" % e)
    # Recréer head-ref (supprimé avec le cache) pour la prochaine config CMake
    git_data_dir = os.path.join(build_dir, "CMakeFiles", "git-data")
    head_ref_path = os.path.join(git_data_dir, "head-ref")
    try:
        os.makedirs(git_data_dir, exist_ok=True)
        with open(head_ref_path, "w") as f:
            f.write("v5.5.2-tarball")
        print("FFP5CS S3: head-ref recréé après clean")
    except OSError as e:
        print("FFP5CS S3: WARN head-ref: %s" % e)
    # Parttool cherche partitions.csv à la racine : copie pour S3
    src_csv = os.path.join(PROJECT_DIR, "config", "partitions", "partitions_esp32_s3_test.csv")
    dst_csv = os.path.join(PROJECT_DIR, "partitions.csv")
    if os.path.isfile(src_csv):
        try:
            shutil.copy2(src_csv, dst_csv)
            print("FFP5CS S3: partitions.csv copié à la racine")
        except OSError as e:
            print("FFP5CS S3: WARN partitions.csv: %s" % e)
else:
    _restore()
