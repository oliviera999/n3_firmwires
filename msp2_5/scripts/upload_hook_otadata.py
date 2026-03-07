# Hook PlatformIO : remplacer l'upload par write_flash + erase otadata + run
# pour que le prochain boot soit toujours sur app0 apres un flash USB.
Import("env")
import os
script = os.path.join(env["PROJECT_DIR"], "..", "scripts", "upload_esp32_otadata_reset.py")
env.Replace(
    UPLOADCMD='python "' + script + '" "${UPLOAD_PORT}" "${PROJECT_BUILD_DIR}"'
)
