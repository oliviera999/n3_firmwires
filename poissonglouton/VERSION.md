# Poissonglouton - Historique versions

## 0.1.2 - 2026-06-02

- Heartbeat serveur `POST /pgl/heartbeat` (flag `PGL_ENABLE_SERVER_HEARTBEAT`, intervalle 2 min + piggyback apres upload).

## 0.1.0 - 2026-05-19

- Initialisation du firmware poissonglouton (display + headless).
- Comptage IR / ultrason / tandem avec anti-double-compte.
- Audio DFPlayer, UI LVGL ludique, upload batch vers serveur.
- Deep sleep optimise pour alimentation solaire.
