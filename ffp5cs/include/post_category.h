/**
 * Catégories de POST pour le pool NetRequest (Phase 2 - v12.42)
 * Priorité 3 > 2 > 1. Un slot réservé par catégorie pour isolation.
 */
#pragma once

#include <cstdint>

namespace AppTasks {
enum class PostCategory : uint8_t {
  Periodic = 1,   // Données périodiques 30s, inflexion marée
  EventAck = 2,   // Ack/événements (nourrissage, pompe, config)
  Replay = 3,     // Rattrapage (file _dataQueue, SD, NVS)
};
}
