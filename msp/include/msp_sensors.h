/* MeteoStationPrototype (msp1) — Capteurs
 * LectureCapteurs, batterie, Light_val (tracker solaire)
 */
#pragma once

void LectureCapteurs();
void batterie();
void Light_val();

// Calibration LDR (égalisation des sensibilités, clé serveur 113)
void mspTrackerLoadCalibration();   // charge les gains NVS au boot
bool mspTrackerCalibrate();         // balayage de référence + calcul des gains (false = à retenter)
void mspTrackerResetCalibration();  // gains neutres + effacement NVS
