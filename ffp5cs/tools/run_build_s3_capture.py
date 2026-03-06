#!/usr/bin/env python3
"""Lance pio run -e wroom-s3-test et capture toute la sortie dans build_capture.log."""
import subprocess
import sys
import os

os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
log_path = os.path.join(os.getcwd(), "build_capture.log")
with open(log_path, "w", encoding="utf-8") as log:
    p = subprocess.Popen(
        [sys.executable, "-m", "platformio", "run", "-e", "wroom-s3-test"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, cwd=os.getcwd()
    )
    for line in p.stdout:
        print(line, end="")
        log.write(line)
        log.flush()
    p.wait()
    sys.exit(p.returncode)
