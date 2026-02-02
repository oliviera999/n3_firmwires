#pragma once

/**
 * Invalide le cache statique de la route /dbvars.
 * À appeler après application de la config distante (poll ou boot) pour que
 * l'UI embarquée reflète immédiatement la config sans attendre le TTL (30s).
 */
void invalidateDbvarsCache();
