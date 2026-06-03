"""
Pre-build FFP5CS : injecte FIRMWARE_BUILD_EPOCH (UTC) pour EPOCH_COMPILE_TIME dans config.h.
"""

from datetime import datetime, timezone

Import("env")

epoch = int(datetime.now(timezone.utc).timestamp())
env.Append(CPPDEFINES=[("FIRMWARE_BUILD_EPOCH", str(epoch))])
print("FFP5CS: FIRMWARE_BUILD_EPOCH=%d" % epoch)
