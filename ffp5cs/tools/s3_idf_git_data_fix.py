"""
Pre-script FFP5CS : crée CMakeFiles/git-data/head-ref pour le build ESP32-S3.

Quand framework-espidf est installé via tarball (platform_packages), il n'y a pas
de .git : GetGitRevisionDescription échoue et le fichier head-ref n'est pas créé.
Ce script crée le répertoire et un head-ref factice pour débloquer la config CMake.

À utiliser uniquement pour les envs wroom-s3-* (plateforme pioarduino + espidf tarball).
"""

import os
Import("env")

PIOENV = env.get("PIOENV", "")
if not PIOENV.startswith("wroom-s3"):
    # Pas un env S3 : ne rien faire
    pass
else:
    # BUILD_DIR = .pio/build/<env> (créé par PlatformIO avant les pre scripts)
    try:
        build_dir = env.subst("$BUILD_DIR")
    except Exception:
        proj = env.subst("$PROJECT_DIR")
        build_dir = os.path.join(proj, ".pio", "build", PIOENV)

    git_data_dir = os.path.join(build_dir, "CMakeFiles", "git-data")
    head_ref_path = os.path.join(git_data_dir, "head-ref")

    # Révision factice (IDF en tarball n'a pas de hash ; évite l'erreur CMake)
    idf_rev = "v5.5.2-tarball"

    try:
        os.makedirs(git_data_dir, exist_ok=True)
        with open(head_ref_path, "w") as f:
            f.write(idf_rev)
        print("FFP5CS S3: git-data/head-ref créé (IDF tarball) -> %s" % idf_rev)
    except OSError as e:
        print("FFP5CS S3: WARN impossible de créer head-ref: %s" % e)
