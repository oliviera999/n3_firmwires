# N3PhasmesProto (n3pp4_2) — Historique des versions

## v4.11 (2026-03)
- Unification : utilisation des libs partagees n3_data, n3_battery, n3_sleep, n3_display, n3_defaults
- Correction bug batterie (double lecture ADC dans moyenne mobile)
- Correction affichage OLED (digitalRead remplace par valeur variable)
- Remplacement httpGETRequest/datatobdd par n3DataGet/n3DataPost
- Constantes communes via n3_defaults.h

## v4.10 (2026-03)
- Harmonisation format version (`FIRMWARE_VERSION` dans main.cpp)
- Externalisation des credentials dans `credentials.h`

## v4.9 (2026-03)
- Correction lecture DHT, stabilisation envoi de donnees

## v4.6 (2026-02)
- OTA URL conditionnelle TEST_MODE

## v4.5 (2026-02)
- OTA HTTP distant via n3_common

## v4.3 (2026-01)
- Credentials externalises dans credentials.h
- OTA HTTP distant via n3_common
