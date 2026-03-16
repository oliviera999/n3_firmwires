"""
Pre-build script (pioarduino / ESP-IDF 5.x) : assure que CMake peut lire la révision Git.
1) Projet : en submodule / detached HEAD, on crée refs/heads/pio-build et on pointe HEAD dessus.
2) Package framework-espidf : pas de .git (tarball) → on crée un .git minimal pour éviter
   l'erreur grabRef.cmake "file failed to open for reading head-ref".
"""
import subprocess
from pathlib import Path

Import("env")

PIO_BUILD_REF = "refs/heads/pio-build"
BACKUP_HEAD = ".pio_git_head_backup"
IDF_FAKE_REV = "v5.5.2-pio"

def _ensure_build_dir_head_ref(e=None):
    """Crée head-ref dans le build dir pour grabRef.cmake (toujours, même après clean)."""
    try:
        e = e if e is not None else env
        build_dir = e.subst("$BUILD_DIR") if hasattr(e, "subst") else None
        if not build_dir:
            build_dir = e.get("PROJECT_BUILD_DIR")
        if not build_dir:
            build_dir = Path(e.get("PROJECT_DIR", ".")) / ".pio" / "build" / e.get("PIOENV", "wroom-test")
        git_data = Path(build_dir) / "CMakeFiles" / "git-data"
        git_data.mkdir(parents=True, exist_ok=True)
        head_ref_build = git_data / "head-ref"
        head_ref_build.write_text(IDF_FAKE_REV + "\n", encoding="utf-8")
        print("[pre-script] head-ref créé dans build dir (fallback CMake):", git_data)
    except Exception as ex:
        print("[pre-script] _ensure_build_dir_head_ref:", ex)


def ensure_espidf_fake_git():
    """Crée un .git minimal dans le package framework-espidf (sans .git) pour CMake IDF.
    Crée aussi head-ref dans le build dir pour éviter l'erreur grabRef si configure_file échoue."""
    try:
        pio = env.PioPlatform()
        for pkg_name in ("framework-espidf", "tool-framework-espidf"):
            try:
                pkg_dir = Path(pio.get_package_dir(pkg_name))
            except Exception:
                continue
            if not pkg_dir.exists():
                continue
            git_dir = pkg_dir / ".git"
            if not git_dir.exists():
                head_ref = git_dir / "refs" / "heads" / "main"
                head_ref.parent.mkdir(parents=True, exist_ok=True)
                head_ref.write_text(IDF_FAKE_REV + "\n", encoding="utf-8")
                head_file = git_dir / "HEAD"
                head_file.write_text("ref: refs/heads/main\n", encoding="utf-8")
                print("[pre-script] .git minimal créé pour", pkg_name, "(CMake IDF)")
            break
        _ensure_build_dir_head_ref()
    except Exception as e:
        print("[pre-script] ensure_espidf_fake_git:", e)
        _ensure_build_dir_head_ref()

def ensure_git_data():
    try:
        proj_dir = Path(env.get("PROJECT_DIR", ".")).resolve()
        rev_run = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=proj_dir,
            capture_output=True,
            text=True,
            timeout=5,
        )
        if rev_run.returncode != 0 or not rev_run.stdout.strip():
            return
        rev_hex = rev_run.stdout.strip()
        gd_run = subprocess.run(
            ["git", "rev-parse", "--git-dir"],
            cwd=proj_dir,
            capture_output=True,
            text=True,
            timeout=5,
        )
        if gd_run.returncode != 0 or not gd_run.stdout.strip():
            return
        git_dir_s = gd_run.stdout.strip()
        git_dir = (proj_dir / git_dir_s).resolve() if not Path(git_dir_s).is_absolute() else Path(git_dir_s).resolve()
        head_file = git_dir / "HEAD"
        if not head_file.exists():
            return

        # Restaurer HEAD si une run précédente l'avait modifié
        backup = proj_dir / BACKUP_HEAD
        if backup.exists():
            try:
                head_file.write_text(backup.read_text(encoding="utf-8"), encoding="utf-8")
                backup.unlink()
                print("[pre-script] HEAD restauré depuis", BACKUP_HEAD)
            except Exception:
                pass

        ref_file = git_dir / PIO_BUILD_REF
        ref_file.parent.mkdir(parents=True, exist_ok=True)
        ref_file.write_text(rev_hex + "\n", encoding="utf-8")

        head_content = head_file.read_text(encoding="utf-8")
        if head_content.strip() == "ref: " + PIO_BUILD_REF:
            return
        backup.write_text(head_content, encoding="utf-8")
        head_file.write_text("ref: " + PIO_BUILD_REF + "\n", encoding="utf-8")
        print("[pre-script] HEAD -> refs/heads/pio-build pour CMake (revision:", rev_hex[:12] + ", restauré au prochain build)")
    except Exception as e:
        print("[pre-script] pio_ensure_git_data:", e)

def _write_head_ref_to_build_dir(target=None, source=None, env=None):
    """Écrit head-ref dans le build dir (CMake grabRef). Signature compatible PreAction SCons (target, source, env)."""
    e = env if env is not None else globals().get("env")
    if e is None:
        return
    try:
        build_dir = e.subst("$BUILD_DIR") if hasattr(e, "subst") else e.get("PROJECT_BUILD_DIR")
        if not build_dir:
            build_dir = Path(e.get("PROJECT_DIR", ".")) / ".pio" / "build" / e.get("PIOENV", "wroom-test")
        git_data = Path(build_dir) / "CMakeFiles" / "git-data"
        git_data.mkdir(parents=True, exist_ok=True)
        (git_data / "head-ref").write_text(IDF_FAKE_REV + "\n", encoding="utf-8")
    except Exception:
        pass

ensure_espidf_fake_git()
ensure_git_data()

# Recréer head-ref en fin de pre (build dir peut venir d'ensure_build_subdirs) + PreAction avant build
_ensure_build_dir_head_ref()
for alias in ("buildprog", "buildelf", "build"):
    try:
        env.AddPreAction(alias, _write_head_ref_to_build_dir)
        break
    except Exception:
        continue
