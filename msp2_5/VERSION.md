# MeteoStationPrototype (msp2_5) — Historique des versions

## v2.12 (2026-03)
- Unification : utilisation des libs partagees n3_data, n3_battery, n3_sleep, n3_display, n3_defaults
- Correction bug batterie (double lecture ADC dans moyenne mobile)
- Correction detection OLED (flag displayOk avec verification I2C)
- Correction email batterie faible (comparaison enableEmailChecked == "checked")
- Ajout log HTTP dans datatobdd
- Remplacement httpGETRequest/datatobdd par n3DataGet/n3DataPost
- Constantes communes via n3_defaults.h

## v2.11 (2026-03)
- Harmonisation format version (`FIRMWARE_VERSION` dans main.cpp)
- Externalisation des credentials dans `credentials.h`

## v2.8 (2026-02)
- Credentials externalises dans credentials.h
- OTA HTTP distant via n3_common

## v2.6 (2026-01)
- Credentials externalises, OTA HTTP distant via n3_common
