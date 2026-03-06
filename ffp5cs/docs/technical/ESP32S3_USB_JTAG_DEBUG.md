# ESP32-S3 : USB Serial/JTAG et debug JTAG (FFP5CS)

Ce document décrit l’utilisation du **USB Serial/JTAG** (flash + moniteur série sans pont UART) et du **debug JTAG** intégré sur ESP32-S3, avec l’environnement `wroom-s3-test-usb` du projet.

## 1. Prérequis matériel

- Une carte **ESP32-S3** dont le connecteur USB est câblé sur **GPIO19 (D-)** et **GPIO20 (D+)** (USB Serial/JTAG intégré au SoC).
- Exemples de cartes compatibles : **ESP32-S3-DevKitC-1**, **ESP32-S3-DevKitM-1**, ou toute carte exposant un port USB pour le SoC (pas seulement un pont USB-UART).
- La carte **4D Systems GEN4-ESP32 16MB** utilisée en S3 dans le projet peut ne pas exposer ce USB : dans ce cas, utiliser un **ESP32-S3-DevKitC-1** (ou équivalent) pour profiter du USB/JTAG, ou garder l’env `wroom-s3-test` avec câble UART.

## 2. Point 1 : Flash et moniteur série via USB (sans pont UART)

### 2.1 Activer l’environnement USB

Dans PlatformIO, sélectionner l’environnement **`wroom-s3-test-usb`** (menu d’environnement en bas de l’IDE, ou `platformio.ini` → env par défaut).

Cet environnement :

- Utilise **`upload_protocol = esp-builtin`** : le flash se fait via le contrôleur USB-JTAG intégré.
- Utilise **`ARDUINO_USB_CDC_ON_BOOT=1`** : au démarrage, `Serial` est redirigé vers le port USB natif (vous voyez les logs sur le même port USB que le flash).

### 2.2 Pilotes Windows (USB JTAG)

Sous Windows, le périphérique USB-JTAG peut ne pas être reconnu. Deux options :

- **Option A (recommandée)** : installer les outils **ESP-IDF** (ou le package “Espressif” dans l’installateur) et cocher **“Espressif – WinUSB support for JTAG (ESP32-C3/S3)”** pour installer le pilote.
- **Option B** : utiliser **Zadig** pour installer le pilote **WinUSB** (ou **libusb**) sur le périphérique “USB JTAG/serial debug unit” quand la carte est branchée.

Sans pilote correct, vous pouvez avoir `LIBUSB_ERROR_NOT_FOUND` ou “could not find or open device” lors du flash ou du debug.

### 2.3 Flasher le firmware

- **Depuis l’IDE** : bouton **Upload** (flèche vers la droite) avec l’env **wroom-s3-test-usb** sélectionné.
- **En ligne de commande** :
  ```bash
  pio run -e wroom-s3-test-usb -t upload
  ```
  Le port est en général choisi automatiquement (périphérique USB JTAG/serial). Si vous avez plusieurs ports, indiquer le bon avec `-p COMx` (Windows) ou `-p /dev/ttyACMx` (Linux).

### 2.4 Flasher le système de fichiers (LittleFS)

- **Depuis l’IDE** : tâche **Upload Filesystem image** avec l’env **wroom-s3-test-usb**.
- **En ligne de commande** :
  ```bash
  pio run -e wroom-s3-test-usb -t uploadfs
  ```

### 2.5 Ouvrir le moniteur série (logs sur USB)

- **Depuis l’IDE** : bouton **Monitor** (icône prise série).
- **En ligne de commande** :
  ```bash
  pio device monitor -e wroom-s3-test-usb
  ```
  Si plusieurs ports existent, préciser le port du USB CDC : `-p COMx` ou `-p /dev/ttyACMx`.

Avec `ARDUINO_USB_CDC_ON_BOOT=1`, les `Serial.printf` / `Serial.println` du firmware s’affichent sur ce port USB (pas sur un UART externe).

---

## 3. Point 2 : Debug JTAG (breakpoints, pas à pas)

Le même câble USB sert au **debug JTAG** via OpenOCD et le contrôleur intégré (`debug_tool = esp-builtin`).

### 3.1 Build (et éventuellement flash) avant debug

- Compiler (et si besoin flasher) avec l’env **wroom-s3-test-usb** :
  ```bash
  pio run -e wroom-s3-test-usb
  pio run -e wroom-s3-test-usb -t upload   # si besoin
  ```

### 3.2 Lancer une session de debug dans VS Code / Cursor

1. Ouvrir l’onglet **Run and Debug** (icône play + insecte).
2. Dans la liste déroulante des configurations, choisir :
   - **PIO Debug S3 (USB-JTAG)** : build + upload puis attache le debugger (breakpoints, pas à pas).
   - **PIO Debug S3 (USB-JTAG, no upload)** : attache le debugger **sans** reflasher (utiliser après un build/upload manuel).
3. Mettre des **breakpoints** dans le code (clic dans la marge à gauche des numéros de ligne).
4. Lancer le debug (F5 ou bouton vert). Le firmware s’exécute jusqu’à un breakpoint ; vous pouvez inspecter variables, pile, pas à pas (F10/F11).

### 3.3 Si le debugger ne trouve pas la carte

- Vérifier que le **pilote USB JTAG** est bien installé (voir § 2.2).
- Vérifier qu’**un seul** périphérique USB de la carte est branché (le port “USB-JTAG/serial” du SoC), ou que le bon port est utilisé.
- Sous Linux : ajouter les règles udev pour OpenOCD (ex. `60-openocd.rules` dans `/etc/udev/rules.d/`) et se reconnecter.

### 3.4 Toolchain (executable / toolchainBinDir)

Les configurations de launch pointent vers l’exécutable `.pio/build/wroom-s3-test-usb/firmware.elf` et le toolchain **xtensa-esp32s3** (par ex. `~/.platformio/packages/toolchain-xtensa-esp32s3/bin`). Si votre installation PlatformIO est ailleurs, adapter dans `.vscode/launch.json` les champs `executable` et `toolchainBinDir` (ou utiliser les variables `${workspaceFolder}` / `${userHome}` déjà présentes).

---

## 4. Résumé des envs S3

| Env                  | Série / Flash      | Usage typique                          |
|----------------------|--------------------|----------------------------------------|
| `wroom-s3-test`      | UART (pont USB-UART) | Carte 4D ou S3 sans USB natif exposé  |
| `wroom-s3-test-usb`  | USB Serial/JTAG    | Flash + monitor + debug JTAG sur un câble USB |

Pour le **développement avec breakpoints et inspection**, utiliser **`wroom-s3-test-usb`** sur une carte qui expose le USB-JTAG (ex. DevKitC-1). Pour la prod ou une carte 4D sans USB natif, rester sur **`wroom-s3-test`** (ou `wroom-s3-prod`) avec câble UART.
