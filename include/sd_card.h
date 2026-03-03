#pragma once

/**
 * @file sd_card.h
 * @brief Module optionnel carte SD (SPI) pour ESP32-S3.
 *
 * Détection au boot, init conditionnelle, isPresent() pour usage futur.
 * Compilé uniquement pour BOARD_S3 ; sur WROOM, isPresent() retourne false.
 */

namespace SdCard {

/** Initialise le bus SPI et tente de monter la carte SD. Test minimal après montage. Retourne true si OK. */
bool init();

/** Retourne true si la carte a été détectée et testée avec succès au boot. */
bool isPresent();

}  // namespace SdCard
