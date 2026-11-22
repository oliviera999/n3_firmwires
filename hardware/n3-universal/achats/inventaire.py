"""Inventaire des composants à poser / acheter par profil, n3-universal rev 0.3.0.

Rev 0.3 : plus de relais ni d'alimentation 230 V sur la carte. Les relais sont des MODULES du
commerce actifs à l'état haut, câblés sur les borniers J39..J44 ; le profil ffp5cs 230 V
s'alimente par un bloc 5 V externe, comme n3pp.

Source : BOM.csv (carte), consignes de pose README / COMPOSANTS_FFP5CS.csv / ALIMENTATION.md,
firmwares msp / n3pp / ffp5cs (PINMAP_UNIVERSAL). Produit, pour chaque profil, la liste des
articles (id catalogue) avec quantité, références carte, rôle, statut et remarque.
"""
import csv, math, re
from collections import OrderedDict, defaultdict
from pathlib import Path

BOM = Path(__file__).resolve().parent.parent / "BOM.csv"

# Repères de la BOM posés sur AUCUN des quatre profils (site S3 et option microSD) :
# toute autre référence de la BOM doit appartenir à au moins un profil (garde --check).
NEVER = {"A2", "J35", "JP2", "JP3", "JP4", "JP11"}

PROFILES = OrderedDict([
    ("msp1", "msp1 — station météo + tracker (18650 + solaire)"),
    ("n3pp", "n3pp — serre (chargeur 5 V)"),
    ("ffp_230", "ffp5cs — aquaponie 230 V (bloc 5 V externe, modules relais)"),
    ("ffp_12v", "ffp5cs — aquaponie 12 V (AGM + PWM solaire)"),
])
SHEET = {"msp1": "msp1", "n3pp": "n3pp", "ffp_230": "ffp5cs 230V", "ffp_12v": "ffp5cs 12V"}


def rng(prefix, *nums):
    return {f"{prefix}{n}" for n in nums}


# ---------------------------------------------------------------- refs posées par profil
CORE = (
    {"A1", "Q12", "R57", "D12", "C1", "D5", "JP22", "C14", "Q7", "Q8", "R34", "R35", "R48", "U1", "C11", "C12",
     "C3", "C4", "R25", "R26", "J13", "J17", "J18", "J19", "J20", "JP1", "J1", "R23", "J10",
     "J12", "R54", "C10"}
    | rng("TP", 1, 2, 3, 4) | rng("H", 1, 2, 3, 4, 6, 7) | rng("JP", *range(13, 22))
)
VBAT = {"J25", "R38", "R39", "R58", "JP12", "R49", "C13", "D11"}
SERVO = {"J15", "J16", "R20", "R21", "C2"}
ADC4 = rng("J", 31, 32, 33, 34) | rng("R", 50, 51, 52, 53) | rng("C", 6, 7, 8, 9)
# Canaux de commande de modules relais (rev 0.3) : Rs 470, rappel 10k, bornier S/+5V/GND, sélecteur
RELAY1 = {"R1", "R5", "J39", "JP5"}
RELAY6 = (rng("R", 1, 2, 3, 4, 28, 31) | rng("R", 5, 6, 7, 8, 29, 32)
          | rng("J", *range(39, 45)) | rng("JP", *range(5, 11)))
FFP = (RELAY6 | SERVO | rng("J", 7, 8, 9) | rng("R", 14, 15, 16, 17, 18, 19) | {"J11", "R24", "R27", "J14",
       "LED5", "R13"})

POP = {
    "msp1": CORE | VBAT | SERVO | ADC4 | rng("R", 43, 44, 45, 46)
            | {"J11", "R24", "J29", "J30", "R47", "J21"},
    "n3pp": CORE | VBAT | ADC4 | RELAY1 | {"R27", "J2", "J21"},
    "ffp_230": CORE | FFP | {"J2"},
    "ffp_12v": CORE | FFP | VBAT | {"J26", "Q11", "D9", "R40", "D8", "C5", "J36", "J37", "J21", "J22", "J28"},
}

# ---------------------------------------------------------------- lecture BOM
bom = {}
for r in list(csv.reader(BOM.open(encoding="utf-8"), delimiter=";"))[1:]:
    refs = r[0].split()
    if not all(re.fullmatch(r"[A-Z]+\d+", x) for x in refs):
        continue
    for ref in refs:
        bom[ref] = dict(value=r[2], fp=r[3], desc=r[4])


def article_of(ref):
    """id d'article catalogue (et nombre de broches pour les barrettes)."""
    b = bom[ref]
    v, fp = b["value"], b["fp"]
    p = re.match(r"[A-Z]+", ref).group()
    if p == "R":
        return "R_" + v, 1
    if p == "C":
        return {"100n": "C_100n", "1000u/16V": "C_1000u16", "470u/16V": "C_470u16", "470u/25V": "C_470u25",
                "220u/16V": "C_220u16", "100u/16V": "C_100u16", "10u/25V": "C_10u25"}[v], 1
    if p == "D":
        return {"1N5822": "D_1N5822", "BAT85": "BAT85", "1.5KE6.8A": "TVS_6V8", "1.5KE18A": "TVS_18",
                "1N4744A": "Z15"}[v], 1
    if p == "LED":
        return ("LED_R" if v == "rouge" else "LED_V"), 1
    if p == "Q":
        return ("BC337" if v.startswith("BC337") else "IRF4905"), 1
    if p == "U":
        return "LD1117", 1
    if p == "TP":
        return "TP_FIL", 1
    if p == "H":
        return "VIS_M3", 1
    if p == "A":
        return "ESP32_DEVKIT", 1
    if p == "J" or p == "JP":
        m = re.search(r"bornier-(\d)", fp)
        if m:
            return f"BORNIER_{m.group(1)}P", 1
        if "BarrelJack" in fp:
            return "JACK_DC", 1
        n = int(re.search(r"1x0?(\d+)", fp).group(1))
        if fp.startswith("PinSocket"):
            return "BARRETTE_F", n
        return "BARRETTE_M", n
    raise KeyError(ref)


def short(desc):
    d = re.split(r" — | \(rev| ;", desc)[0]
    d = re.sub(r"\([^)]*\)", "", d).strip(" ,;")
    d = re.sub(r"\s{2,}", " ", d).replace(" :", " :")
    return d[:70]


def natkey(ref):
    m = re.match(r"([A-Z]+)(\d+)", ref)
    return (m.group(1), int(m.group(2)))


# ---------------------------------------------------------------- articles hors carte
# (profil -> liste de (id, qté, statut, rôle, remarque))
A, O, P = "À acheter", "Option", "Déjà possédé"
OFF = {
    "msp1": [
        ("DHT11", 2, A, "Température/humidité air intérieure (J10) et extérieure (J30)",
         "J30 exige JP21 FERMÉ (pull-up R47). Un BME280 détecté remplace le DHT (option ci-dessous)."),
        ("BME280", 2, O, "Remplace les DHT11 : 0x76 = intérieur, 0x77 = extérieur (I2C J21 / J14)",
         "Le firmware le détecte au démarrage. Adresse 0x77 : pont SDO à souder sur le module."),
        ("DS18B20", 1, A, "Température du sol (« TempEau » côté serveur) sur J11", "Version étanche, câble 1 m."),
        ("LDR", 4, A, "Tracker solaire : 4 photorésistances sur J31-J34",
         "JP16-JP19 FERMÉS (bas de pont R43-R46 10k). Prévoir un séparateur en croix entre les 4 LDR."),
        ("PLUIE", 1, A, "Détection de pluie (sortie DO) sur J29",
         "Ne PAS poser R17 (sinon « mouillé » permanent). Alimenté par +3V3_SW."),
        ("SOL_CAPA", 1, A, "Humidité du sol (« HumiditeSol », sortie AO) sur J12",
         "R27 NON posé, JP20 OUVERT. Module 3 fils : prendre le GND sur un bornier voisin (J12 n'a que 3V3/ADC)."),
        ("SG90", 2, A, "Tracker : servo gauche-droite (J15) et haut-bas (J16)",
         "Coupés pendant le sommeil (JP1 ôté). Gros servo plus tard : fil +5V en étoile, voir ALIMENTATION.md."),
        ("OLED", 1, A, "Affichage local sur J13", "Alimenté par le rail commuté : éteint en sommeil, 0 fuite."),
        ("TP4056", 1, P, "Chargeur 1S entre panneau et 18650", "Décision N5 : conservé (déjà en stock). Modèle AVEC protection (DW01)."),
        ("BOOST", 1, A, "Élève 3,0–4,2 V de la 18650 à 5,2 V vers J1",
         "RÉGLER À 5,2 V au multimètre AVANT de brancher la carte (rev 0.3 : la diode D5 prend 0,35 V). MT3608 : ~1,5 mA à vide, acceptable."),
        ("CELL_18650", 1, A, "Stockage d'énergie (~2,3 Wh/jour consommés)",
         "Cellule de marque (Samsung/LG/Panasonic/Molicel), pas de « 9900 mAh ». 2e cellule en parallèle = option."),
        ("SUPPORT_18650", 1, A, "Logement de la cellule", "À l'ombre, sous le panneau : jamais au soleil (> 45 °C)."),
        ("PANNEAU_6V", 1, A, "Recharge solaire", "5–6 W / 6 V : un 2 W est déficitaire en hiver (ALIMENTATION.md §3)."),
        ("BOITIER", 1, A, "Boîtier étanche extérieur", "Ventilé par le bas / à l'ombre."),
        ("PRESSE_ETOUPE", 6, A, "Passage des câbles capteurs/servos/alim", "PG7 pour les sondes, PG9 pour l'alim."),
    ],
    "n3pp": [
        ("DHT11", 1, A, "Température/humidité de la serre sur J10", "BME280 en option (détecté automatiquement)."),
        ("BME280", 1, O, "Remplace le DHT11 (0x76, I2C J21)", "Ajoute la pression atmosphérique."),
        ("SOL_CAPA", 4, A, "Humidité du sol 1 à 4 sur J31-J34",
         "JP16-JP19 OUVERTS, R43-R46 NON posés. Capacitif v1.2 : ne rouille pas comme les sondes résistives."),
        ("LDR", 1, A, "Luminosité de la serre sur J12", "R27 posé, JP20 FERMÉ."),
        ("OLED", 1, A, "Affichage local sur J13", ""),
        ("CHARGEUR_5V", 1, A, "Alimentation de la carte", "5 V 2 A minimum, de marque (≥ 4,8 V sur J1/J2 sous charge). Cavalier JP22 POSÉ sur cette unité : jamais d'USB du DevKit en même temps."),
        ("MODULE_RELAIS", 1, A, "Relais de la pompe d'arrosage, câblé sur J39 (S / +5V / GND)",
         "ACTIF A L'ÉTAT HAUT uniquement (type KY-019). Essai sur table avant achat : collage franc en 3,3 V à travers 1 kΩ."),
        ("CABLE_USB_JACK", 1, A, "Du chargeur au jack J2", "Le plus COURT et le plus épais possible (≥ 0,5 mm²)."),
        ("1N4007_EXT", 1, A, "Roue libre aux bornes de la pompe si pompe DC",
         "Cathode côté +. Inutile pour une pompe 230 V."),
        ("BOITIER", 1, A, "Boîtier étanche (serre humide)", "Isolant si la pompe est en 230 V : le 230 V ne passe que par le module relais."),
        ("PRESSE_ETOUPE", 6, A, "Passage des câbles sondes / pompe / alim", ""),
    ],
    "ffp_230": [
        ("DHT11", 1, A, "Température/humidité de l'air sur J10", "DHT22 possible (flag -DUSE_DHT22)."),
        ("DS18B20", 1, A, "Température de l'eau (régule le chauffage) sur J11",
         "GPIO2 = broche de démarrage : ôter JP1 pour flasher par USB (carte HORS secteur)."),
        ("LDR", 1, A, "Luminosité ambiante sur J12", "R27 posé, JP20 FERMÉ."),
        ("HCSR04", 3, A, "Niveaux d'eau aquarium (J7), réservoir (J8), potager (J9)",
         "JP13-JP15 FERMÉS + R17-R19 posés (écho 5 V ramené à 3,3 V) : OBLIGATOIRE."),
        ("SG90", 2, A, "Nourrisseurs gros (J15) et petits poissons (J16)", "JP1 fermé : alimentés en permanence."),
        ("OLED", 1, A, "Affichage local sur J13", ""),
        ("DS3231", 1, A, "Horloge hors ligne sur J14", "Dessouder la résistance/diode de charge du module (pile non rechargeable)."),
        ("CR2032", 1, A, "Pile du DS3231", ""),
        ("MICROSD", 1, O, "Journal sur carte SD (J35)",
         "Seulement avec un ESP32-S3 (site A2) ou après efuse VDD_SDIO=3V3 sur WROOM. PAS de module Catalex."),
        ("MODULE_RELAIS", 4, A, "Relais pompe aquarium (J39), pompe réservoir (J40), chauffage (J41), lumière (J42)",
         "ACTIFS A L'ÉTAT HAUT uniquement (type KY-019, ou module 8 canaux H/L cavalier sur H). Regarder le dessous d'un module avant d'en acheter plusieurs : pas d'écart visible entre pistes 230 V et basse tension = pas d'achat."),
        ("MODULE_RELAIS", 2, O, "Relais auxiliaires AUX1 (J43) et AUX2 (J44)", "Seulement si une charge y est raccordée."),
        ("BLOC_5V", 1, A, "Alimentation de la carte et des modules relais, sur J2 (jack) ou J1 (bornier)",
         "5 V ≥ 2 A, mieux 3 A. Mesurer ≥ 5,1 V en charge, ou modèle réglable mis à 5,2 V. PAS un bloc 12 V (même fiche) : il détruirait la carte."),
        ("PROTECTION_AMONT", 1, A, "Fusible ou disjoncteur en amont des charges 230 V",
         "OBLIGATOIRE : la carte ne protège pas les charges. Raccordement sous supervision adulte, coffret fermé."),
        ("BOITIER", 1, A, "Coffret isolant OBLIGATOIRE pour les modules relais et leurs charges 230 V", "Plastique, pas de boîtier métallique. Modules au-dessus de la carte (les fils de commande sortent par le bord haut)."),
        ("PRESSE_ETOUPE", 8, A, "Passage cordon secteur, charges 230 V, sondes", ""),
        ("FIL_15", 5, A, "Câblage des charges 230 V (pompes, chauffage, lumière), côté modules relais", "Au mètre, 1,5 mm² souple + embouts. Jamais sur la carte."),
    ],
    "ffp_12v": [
        ("DHT11", 1, A, "Température/humidité de l'air sur J10", "DHT22 possible (flag -DUSE_DHT22)."),
        ("DS18B20", 1, A, "Température de l'eau sur J11", "Chauffage interdit sur batterie (firmware) : surplus solaire seulement."),
        ("LDR", 1, A, "Luminosité ambiante sur J12", "R27 posé, JP20 FERMÉ."),
        ("HCSR04", 3, A, "Niveaux d'eau aquarium (J7), réservoir (J8), potager (J9)",
         "JP13-JP15 FERMÉS + R17-R19 posés : OBLIGATOIRE."),
        ("SG90", 2, A, "Nourrisseurs gros (J15) et petits poissons (J16)", "JP1 fermé : alimentés en permanence."),
        ("OLED", 1, A, "Affichage local sur J13", ""),
        ("DS3231", 1, A, "Horloge hors ligne sur J14", "Dessouder le circuit de charge du module."),
        ("CR2032", 1, A, "Pile du DS3231", ""),
        ("INA226", 1, A, "Mesure tension/courant batterie (délestage AGM, n3_power) sur J21",
         "Le shunt R100 du module plafonne à 0,8 A : shunt externe 5–10 mΩ câblé en Kelvin pour mesurer les pompes."),
        ("INA226", 2, O, "2e et 3e INA226 : panneau (0x40) et charges (0x44) sur J22/J28", "Adresses à régler par ponts A0/A1."),
        ("MODULE_RELAIS", 3, A, "Relais pompe aquarium (J39), pompe réservoir (J40), lumière (J42)",
         "ACTIFS A L'ÉTAT HAUT uniquement (type KY-019). Bobines alimentées en 5 V par le bornier de chaque canal."),
        ("MODULE_RELAIS", 3, O, "Relais chauffage (J41, interdit sur batterie par le firmware), AUX1 (J43), AUX2 (J44)", "Seulement si une charge y est raccordée."),
        ("BUCK", 1, A, "12 V → 5,2 V pour la carte (J36 → J37)",
         "RÉGLER À 5,2 V AVANT de raccorder J37 (rev 0.3 : la diode D5 prend 0,35 V). Sur entretoises au-dessus de la carte."),
        ("FUSIBLE_LAME", 1, A, "Fusible 7,5–10 A en ligne, au plus près de la batterie, avant J26", "OBLIGATOIRE."),
        ("1N4007_EXT", 2, A, "Roue libre aux bornes des pompes 12 V (aquarium, réservoir)",
         "Cathode côté +. Pompe > 1 A : préférer une 1N5408 (3 A)."),
        ("AGM_12V", 1, P, "Batterie du bus 12 V", "Brancher la carte sur les bornes BATTERIE, pas sur la sortie « charge » du régulateur."),
        ("PWM_SOLAIRE", 1, P, "Charge l'AGM depuis le panneau (14,5 V mesurés en absorption)", "Déjà en place."),
        ("PANNEAU_12V", 1, P, "Production solaire du bus 12 V", "Déjà en place."),
        ("BOITIER", 1, A, "Boîtier étanche", ""),
        ("PRESSE_ETOUPE", 8, A, "Passage bus 12 V, pompes, sondes", ""),
        ("FIL_15", 5, A, "Câblage bus 12 V et pompes", "Au mètre ; 2,5 mm² entre batterie et fusible si pompes > 5 A."),
    ],
}
for k in OFF:  # câblage commun à chaque unité
    OFF[k].append(("FIL_05", 5, A, "Câblage des capteurs (au mètre)", "Souple 0,25–0,5 mm² ; embouts sur chaque fil."))

# Remarques de pose spécifiques profil (carte)
POSE_NOTE = {
    "msp1": {
        "R_2k": None,
        "JP": "Cavaliers posés : JP1 parqué (rail commuté), JP12 en 2-3 (1S), JP16-JP19 et JP21 FERMÉS. JP22 ÔTÉ (boost à 5,2 V).",
        "LED5": "LED5/R13 NON posées (3 mA permanents).",
        "J1": "Entrée du boost 5,2 V.",
        "J25": "Pont batterie 1S : J25 sur le + de la 18650, JP12 en 2-3. Veille d'urgence firmware ≥ 3,3 V.",
    },
    "n3pp": {
        "JP": "Cavaliers posés : JP1 parqué (rail commuté), JP12 en 2-3, JP20 FERMÉ, JP22 POSÉ (chargeur 5,0 V : jamais d'USB en même temps) ; JP5 SANS cavalier (AUTO).",
        "J25": "Pas de batterie : relier J25-1 au +5V (J18-1) par un fil, sinon le firmware lit une batterie vide et part en veille d'urgence (ou désactiver la clé serveur 112). À vérifier au 1er allumage.",
        "J1": "J2 (jack) pour le chargeur ; J1 en secours.",
    },
    "ffp_230": {
        "JP": "Cavaliers posés : JP1 FERMÉ 1-2, JP13-JP15 et JP20 FERMÉS ; JP5-JP10 SANS cavalier (AUTO) ; JP22 ÔTÉ si le bloc donne 5,2 V, POSÉ s'il ne dépasse pas 5,0 V.",
        "J1": "Bloc 5 V sur J2 (jack) OU sur J1 (bornier) : jamais deux sources.",
    },
    "ffp_12v": {
        "JP": "Cavaliers posés : JP1 FERMÉ, JP12 en 1-2 (18k), JP13-JP15 et JP20 FERMÉS ; JP5-JP10 SANS cavalier ; JP22 ÔTÉ (buck à 5,2 V).",
        "J1": "J1 seulement pour les essais sur table, buck débranché.",
        "J25": "Sonde bus 12 V (JP12 en 1-2, ratio 118/18) : facultatif, aucun firmware ne la lit aujourd'hui.",
    },
}
# Position des cavaliers par profil (source structurée : classeur, vues Fritzing, gardes).
# "1-2" / "2-3" = cavalier posé entre ces broches ; absent = sans cavalier (JP5-JP10 : AUTO ;
# JP13-JP21 : OUVERT ; JP22 : ÔTÉ = D5 en service). JP1 en "2-3" = cavalier parqué (broche 3 non
# câblée) = rail commuté. JP22 n'est posé que sur n3pp (chargeur 5,0 V, règle du 2026-11-19).
JUMPER_STATE = {
    "msp1": {"JP1": "2-3", "JP12": "2-3", "JP16": "1-2", "JP17": "1-2", "JP18": "1-2", "JP19": "1-2", "JP21": "1-2"},
    "n3pp": {"JP1": "2-3", "JP12": "2-3", "JP20": "1-2", "JP22": "1-2"},
    "ffp_230": {"JP1": "1-2", "JP13": "1-2", "JP14": "1-2", "JP15": "1-2", "JP20": "1-2"},
    "ffp_12v": {"JP1": "1-2", "JP12": "1-2", "JP13": "1-2", "JP14": "1-2", "JP15": "1-2", "JP20": "1-2"},
}
JUMPERS = {p: len(v) for p, v in JUMPER_STATE.items()}  # cavaliers posés par profil

# Valeur posée différente de la BOM selon le profil. C2 (réservoir des servos, derrière Q7) :
# 100 µF sur msp, où Q7 se ferme sur C2 vide à chaque réveil (simulation/SIMULATION.md §13 :
# 3V3 du DevKit à 3,30 V avec 100 µF, 2,9 V avec 470 µF) ; 470 µF sur ffp5cs (rail permanent).
VALUE_BY_PROFILE = {("msp1", "C2"): "C_100u16"}


def board_items(prof):
    """{id: dict(qty, refs, roles, pins)} pour la carte du profil."""
    agg = OrderedDict()
    pins = defaultdict(int)
    socket_pieces = 0
    for ref in sorted(POP[prof], key=natkey):
        aid, n = article_of(ref)
        aid = VALUE_BY_PROFILE.get((prof, ref), aid)
        if aid in ("BARRETTE_F", "BARRETTE_M"):
            k = 1
            if ref == "A1":
                pass
            pins[aid] += n
            if aid == "BARRETTE_F":
                socket_pieces += 1
        e = agg.setdefault(aid, dict(qty=0, refs=[], roles=[]))
        e["refs"].append(ref)
        e["qty"] += 1 if aid not in ("BARRETTE_F", "BARRETTE_M") else 0
        r = short(bom[ref]["desc"])
        if r not in e["roles"]:
            e["roles"].append(r)
    # A1 (rev 0.2.3, DevKitC 38 broches) : deux supports femelles 1x19 tirés d'une barrette femelle
    pins["BARRETTE_F"] += 38
    socket_pieces += 2
    agg.setdefault("BARRETTE_F", dict(qty=0, refs=[], roles=[]))["refs"].insert(0, "A1 (2×19)")
    agg["BARRETTE_F"]["roles"].insert(0, "Supports du DevKit (jamais soudé)")
    # barrettes 1x40 : une broche perdue à chaque coupe pour les femelles
    agg["BARRETTE_F"]["qty"] = math.ceil((pins["BARRETTE_F"] + socket_pieces) / 40)
    agg["BARRETTE_M"]["qty"] = math.ceil((pins["BARRETTE_M"] + 4) / 40)  # marge de casse
    agg["BARRETTE_F"]["pins"] = pins["BARRETTE_F"]
    agg["BARRETTE_M"]["pins"] = pins["BARRETTE_M"]
    # F1 : porte-fusible ; cavaliers
    agg["CAVALIER"] = dict(qty=JUMPERS[prof] + 2, refs=["JP…"], roles=["Cavaliers de configuration du profil (+2 de rechange)"])
    agg["ENTRETOISE_M3"] = dict(qty=6, refs=["H1-H4, H6, H7"], roles=["Entretoises de fixation de la carte"])
    agg["PCB"] = dict(qty=1, refs=["—"], roles=["Circuit imprimé n3-universal rev 0.3.0"])
    return agg


