"""
Post-build FFP5CS : écrit la version firmware dans BUILD_DIR/version.txt.

Permet au script publish_ota.ps1 de publier le numéro de version correspondant à chaque
firmware compilé (chaque env peut avoir été compilé à un moment différent).
Source : include/config_system.h (ou config.h en repli).
"""

import os
import re

Import("env")


def post_build_write_version(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    version = None
    for header_name in ("config_system.h", "config.h"):
        config_path = os.path.join(project_dir, "include", header_name)
        if not os.path.isfile(config_path):
            continue
        with open(config_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        m = re.search(r'VERSION\s*=\s*"([^"]+)"', content)
        if m:
            version = m.group(1)
            break
    if version is None:
        version = "0.0.0"
    version_file = os.path.join(build_dir, "version.txt")
    with open(version_file, "w", encoding="utf-8") as f:
        f.write(version.strip())
    print("FFP5CS: version %s -> %s" % (version, version_file))


# S'exécute après la génération du binaire firmware (alias buildprog / buildelf selon plateforme)
_post_attached = False
for alias in ("buildprog", "buildelf", "build"):
    try:
        env.AddPostAction(alias, post_build_write_version)
        _post_attached = True
        break
    except Exception:
        continue
if not _post_attached:
    print("FFP5CS: WARN post-build version.txt non attache (aucun alias trouve)")
