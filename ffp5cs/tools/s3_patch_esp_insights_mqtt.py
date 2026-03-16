"""
Force le composant esp_insights à utiliser le certificat MQTT (évite bug chemin .S HTTPS).
Patch le CMakeLists du composant pour la branche MQTT uniquement (build S3).
Exécuté en pre ET en PreAction avant le build pour survivre à une éventuelle synchro composants.
"""

import os
Import("env")

def _patch_esp_insights_cmake():
    PROJECT_DIR = env.subst("$PROJECT_DIR")
    cmake_path = os.path.join(PROJECT_DIR, "managed_components", "espressif__esp_insights", "CMakeLists.txt")
    if not os.path.isfile(cmake_path):
        return False
    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()

    old_block = """if(CONFIG_ESP_INSIGHTS_TRANSPORT_MQTT)
    target_add_binary_data(${COMPONENT_TARGET} "server_certs/mqtt_server.crt" TEXT)
    target_sources(${COMPONENT_LIB} PRIVATE "src/transport/esp_insights_mqtt.c")
else()
    target_add_binary_data(${COMPONENT_TARGET} "server_certs/https_server.crt" TEXT)
    idf_component_get_property(http_client_lib esp_http_client COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PRIVATE ${http_client_lib})
    target_sources(${COMPONENT_LIB} PRIVATE "src/transport/esp_insights_https.c")
endif()"""

    new_block = """# FFP5CS S3: forcer MQTT (évite bug chemin .S dupliqué avec https_server.crt)
    target_add_binary_data(${COMPONENT_TARGET} "server_certs/mqtt_server.crt" TEXT)
    target_sources(${COMPONENT_LIB} PRIVATE "src/transport/esp_insights_mqtt.c")"""

    if new_block in content:
        return False  # déjà patché (ne pas invalider le cache)
    if old_block not in content:
        print("FFP5CS S3: WARN esp_insights CMakeLists non patché (bloc introuvable)")
        return False
    content = content.replace(old_block, new_block)
    with open(cmake_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("FFP5CS S3: esp_insights patché (transport MQTT forcé)")
    return True  # patch appliqué cette fois → invalider cache

def _invalidate_cmake_cache():
    """Supprime cache CMake pour forcer reconfig (uniquement en pre, pas en PreAction)."""
    build_dir = env.subst("$BUILD_DIR")
    for name in ("CMakeCache.txt", "build.ninja"):
        p = os.path.join(build_dir, name)
        if os.path.isfile(p):
            try:
                os.remove(p)
                print("FFP5CS S3: supprimé %s pour forcer reconfig" % name)
            except OSError:
                pass

PIOENV = env.get("PIOENV", "")
# Appliquer le patch pour S3 et WROOM (pioarduino) : bug chemin .S dupliqué avec https_server.crt
if not (PIOENV.startswith("wroom-s3") or PIOENV.startswith("wroom-")):
    pass
else:
    # Désactiver le component manager pour ce build : évite qu'il réécrive
    # managed_components après notre patch (ordre: pre -> "Compile Arduino IDF libs" -> CMake).
    os.environ["IDF_COMPONENT_MANAGER"] = "0"
    patched = _patch_esp_insights_cmake()
    if patched:
        _invalidate_cmake_cache()
    env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", lambda source, target, env: _patch_esp_insights_cmake())
