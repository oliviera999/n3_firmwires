# -*- coding: utf-8 -*-
import os
os.chdir(os.path.dirname(os.path.abspath(__file__)) + "/../..")

# 1. FFP5CS-R-gles-c-ur-de-projet.mdc - Documents line (index 36)
path1 = ".cursor/rules/FFP5CS-R-gles-c-ur-de-projet.mdc"
with open(path1, "r", encoding="utf-8") as f:
    lines = f.readlines()
lines[36] = "  - Ne pas créer de fichiers `.md` sans demande explicite de l'utilisateur. Les documentations liées à une tâche demandée (ex. doc d'API, contrat) sont autorisées lorsque l'utilisateur le demande.\n"
with open(path1, "w", encoding="utf-8") as f:
    f.writelines(lines)
print("OK:", path1)

# 2. FFP5CS-constantes-harmonisees-firmware-serveur.mdc - Contrat line (index 18)
path2 = ".cursor/rules/FFP5CS-constantes-harmonisees-firmware-serveur.mdc"
with open(path2, "r", encoding="utf-8") as f:
    lines = f.readlines()
lines[18] = "  - Clés de config (NVS / API), codes de statut, noms de champs JSON, endpoints, valeurs d'énumération partagées : **mêmes noms et mêmes valeurs** des deux côtés, **ou** un mapping unique documenté (ex. docs/technical/VARIABLE_NAMING.md, doc d'API) tenu à jour.\n"
with open(path2, "w", encoding="utf-8") as f:
    f.writelines(lines)
print("OK:", path2)

# 3. description-R-gles-interface-web-locale-FFP5CS.mdc - Robustesse line (index 26)
path3 = ".cursor/rules/description-R-gles-interface-web-locale-FFP5CS.mdc"
with open(path3, "r", encoding="utf-8") as f:
    lines = f.readlines()
lines[26] = "  - Gérer les déconnexions (messages d'erreur simples). Pas de retry sans backoff ni limite (ex. max 3 tentatives avec backoff autorisé).\n"
with open(path3, "w", encoding="utf-8") as f:
    f.writelines(lines)
print("OK:", path3)
