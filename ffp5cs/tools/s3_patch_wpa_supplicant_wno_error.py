"""
FFP5CS : patch IDF (wpa_supplicant + mbedtls) pour -Wno-error sur tous les envs wroom-*.
Avec pioarduino des options C++ fuient vers les .c → warnings traités en erreurs (-Werror).
"""
import os
Import("env")

def _get_espidf_pkg_dir():
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-"):
        return None
    try:
        platform = env.PioPlatform()
        # Noms possibles du package IDF (S3 = platformio/espressif32, WROOM = pioarduino)
        for name in ("framework-espidf", "framework-espidf@src-1e666e6a68efaa9903d5a0984e93801f"):
            try:
                pkg_dir = platform.get_package_dir(name)
                if pkg_dir and os.path.isdir(pkg_dir):
                    wpa_cmake = os.path.join(pkg_dir, "components", "wpa_supplicant", "CMakeLists.txt")
                    if os.path.isfile(wpa_cmake):
                        return pkg_dir
            except Exception:
                continue
        home = os.environ.get("USERPROFILE") or os.environ.get("HOME") or ""
        base = os.path.join(home, ".platformio", "packages")
        if os.path.isdir(base):
            for n in os.listdir(base):
                pkg_dir = os.path.join(base, n)
                wpa_cmake = os.path.join(pkg_dir, "components", "wpa_supplicant", "CMakeLists.txt")
                if os.path.isfile(wpa_cmake):
                    return pkg_dir
    except Exception:
        pass
    return None

def _patch_file(path, old_line, new_line, label):
    if not os.path.isfile(path):
        return False
    try:
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        if new_line in content:
            return False
        if old_line not in content:
            return False
        content = content.replace(old_line, new_line)
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print("FFP5CS S3: %s patché (-Werror → -Wno-error)" % label)
        return True
    except Exception as e:
        print("FFP5CS S3: WARN patch %s: %s" % (label, e))
        return False

def _patch_idf_wno_error():
    pkg_dir = _get_espidf_pkg_dir()
    if not pkg_dir:
        print("FFP5CS S3: WARN framework-espidf introuvable, patch IDF ignoré")
        return
    # wpa_supplicant
    _patch_file(
        os.path.join(pkg_dir, "components", "wpa_supplicant", "CMakeLists.txt"),
        "target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-strict-aliasing -Wno-write-strings -Werror)",
        "target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-strict-aliasing -Wno-write-strings -Wno-error)",
        "wpa_supplicant",
    )
    # mbedtls (sous-projet mbedtls/CMakeLists.txt)
    _patch_file(
        os.path.join(pkg_dir, "components", "mbedtls", "mbedtls", "CMakeLists.txt"),
        "        set(CMAKE_C_FLAGS \"${CMAKE_C_FLAGS} -Werror\")",
        "        set(CMAKE_C_FLAGS \"${CMAKE_C_FLAGS} -Wno-error\")",
        "mbedtls",
    )

_patch_idf_wno_error()
