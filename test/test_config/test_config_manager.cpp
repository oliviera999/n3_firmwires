/**
 * Tests unitaires pour ConfigManager
 * v11.166: Ajouté suite à l'audit général
 * 
 * Ces tests vérifient la logique de ConfigManager en isolation,
 * sans dépendance réelle à NVS (mock).
 */

#include <unity.h>
#include "../unit/test_mocks.h"
#include "nvs_mock.h"

// Simulation minimale de ConfigManager pour tests
// (évite d'inclure le vrai ConfigManager qui dépend de NVS réel)
// NOTE: forceWakeUp est géré par Automatism (SYSTEM::forceWakeUp), pas ConfigManager
class ConfigManagerTestable {
private:
    bool _bouffeMatinOk = false;
    bool _bouffeMidiOk = false;
    bool _bouffeSoirOk = false;
    int _lastJourBouf = -1;
    bool _pompeAquaLocked = false;
    bool _otaUpdateFlag = false;
    bool _remoteSendEnabled = true;
    bool _remoteRecvEnabled = true;
    
    // Cache
    bool _cachedBouffeMatinOk = false;
    bool _cachedBouffeMidiOk = false;
    bool _cachedBouffeSoirOk = false;
    int _cachedLastJourBouf = -1;
    bool _flagsChanged = false;
    
public:
    // Setters avec détection de changements
    void setBouffeMatinOk(bool value) {
        if (_bouffeMatinOk != value) {
            _bouffeMatinOk = value;
            _flagsChanged = true;
        }
    }
    
    void setBouffeMidiOk(bool value) {
        if (_bouffeMidiOk != value) {
            _bouffeMidiOk = value;
            _flagsChanged = true;
        }
    }
    
    void setBouffeSoirOk(bool value) {
        if (_bouffeSoirOk != value) {
            _bouffeSoirOk = value;
            _flagsChanged = true;
        }
    }
    
    void setLastJourBouf(int value) {
        if (_lastJourBouf != value) {
            _lastJourBouf = value;
            _flagsChanged = true;
        }
    }
    
    void setPompeAquaLocked(bool value) {
        _pompeAquaLocked = value;
    }
    
    void setOtaUpdateFlag(bool value) {
        _otaUpdateFlag = value;
    }
    
    void setRemoteSendEnabled(bool value) {
        _remoteSendEnabled = value;
    }
    
    void setRemoteRecvEnabled(bool value) {
        _remoteRecvEnabled = value;
    }
    
    // Getters
    bool getBouffeMatinOk() const { return _bouffeMatinOk; }
    bool getBouffeMidiOk() const { return _bouffeMidiOk; }
    bool getBouffeSoirOk() const { return _bouffeSoirOk; }
    int getLastJourBouf() const { return _lastJourBouf; }
    bool getPompeAquaLocked() const { return _pompeAquaLocked; }
    bool getOtaUpdateFlag() const { return _otaUpdateFlag; }
    bool isRemoteSendEnabled() const { return _remoteSendEnabled; }
    bool isRemoteRecvEnabled() const { return _remoteRecvEnabled; }
    
    // État
    bool hasChanges() const { return _flagsChanged; }
    void clearChanges() { _flagsChanged = false; }
    
    // Reset pour tests
    void resetBouffeFlags() {
        _bouffeMatinOk = false;
        _bouffeMidiOk = false;
        _bouffeSoirOk = false;
        _lastJourBouf = -1;
        _flagsChanged = true;
    }
    
    void updateCache() {
        _cachedBouffeMatinOk = _bouffeMatinOk;
        _cachedBouffeMidiOk = _bouffeMidiOk;
        _cachedBouffeSoirOk = _bouffeSoirOk;
        _cachedLastJourBouf = _lastJourBouf;
        _flagsChanged = false;
    }
};

// Instance globale pour tests
static ConfigManagerTestable config;

void setUp(void) {
    // Reset config avant chaque test
    config = ConfigManagerTestable();
}

void tearDown(void) {
    // Nettoyage après chaque test
}

// ============================================================================
// TESTS BOUFFE FLAGS
// ============================================================================

void test_bouffe_flags_initial_state() {
    TEST_ASSERT_FALSE(config.getBouffeMatinOk());
    TEST_ASSERT_FALSE(config.getBouffeMidiOk());
    TEST_ASSERT_FALSE(config.getBouffeSoirOk());
    TEST_ASSERT_EQUAL(-1, config.getLastJourBouf());
}

void test_bouffe_matin_toggle() {
    config.setBouffeMatinOk(true);
    TEST_ASSERT_TRUE(config.getBouffeMatinOk());
    TEST_ASSERT_TRUE(config.hasChanges());
    
    config.clearChanges();
    TEST_ASSERT_FALSE(config.hasChanges());
    
    // Même valeur ne déclenche pas de changement
    config.setBouffeMatinOk(true);
    TEST_ASSERT_FALSE(config.hasChanges());
    
    // Valeur différente déclenche changement
    config.setBouffeMatinOk(false);
    TEST_ASSERT_TRUE(config.hasChanges());
    TEST_ASSERT_FALSE(config.getBouffeMatinOk());
}

void test_bouffe_midi_toggle() {
    config.setBouffeMidiOk(true);
    TEST_ASSERT_TRUE(config.getBouffeMidiOk());
    TEST_ASSERT_TRUE(config.hasChanges());
}

void test_bouffe_soir_toggle() {
    config.setBouffeSoirOk(true);
    TEST_ASSERT_TRUE(config.getBouffeSoirOk());
    TEST_ASSERT_TRUE(config.hasChanges());
}

void test_last_jour_bouf() {
    config.setLastJourBouf(15);
    TEST_ASSERT_EQUAL(15, config.getLastJourBouf());
    TEST_ASSERT_TRUE(config.hasChanges());
    
    config.clearChanges();
    config.setLastJourBouf(15);  // Même valeur
    TEST_ASSERT_FALSE(config.hasChanges());
    
    config.setLastJourBouf(16);  // Nouvelle valeur
    TEST_ASSERT_TRUE(config.hasChanges());
    TEST_ASSERT_EQUAL(16, config.getLastJourBouf());
}

void test_reset_bouffe_flags() {
    config.setBouffeMatinOk(true);
    config.setBouffeMidiOk(true);
    config.setBouffeSoirOk(true);
    config.setLastJourBouf(20);
    config.clearChanges();
    
    config.resetBouffeFlags();
    
    TEST_ASSERT_FALSE(config.getBouffeMatinOk());
    TEST_ASSERT_FALSE(config.getBouffeMidiOk());
    TEST_ASSERT_FALSE(config.getBouffeSoirOk());
    TEST_ASSERT_EQUAL(-1, config.getLastJourBouf());
    TEST_ASSERT_TRUE(config.hasChanges());
}

// ============================================================================
// TESTS NETWORK FLAGS
// ============================================================================

void test_network_flags_initial_state() {
    TEST_ASSERT_TRUE(config.isRemoteSendEnabled());
    TEST_ASSERT_TRUE(config.isRemoteRecvEnabled());
}

void test_remote_send_toggle() {
    config.setRemoteSendEnabled(false);
    TEST_ASSERT_FALSE(config.isRemoteSendEnabled());
    
    config.setRemoteSendEnabled(true);
    TEST_ASSERT_TRUE(config.isRemoteSendEnabled());
}

void test_remote_recv_toggle() {
    config.setRemoteRecvEnabled(false);
    TEST_ASSERT_FALSE(config.isRemoteRecvEnabled());
    
    config.setRemoteRecvEnabled(true);
    TEST_ASSERT_TRUE(config.isRemoteRecvEnabled());
}

// ============================================================================
// TESTS AUTRES FLAGS
// ============================================================================

void test_pompe_aqua_locked() {
    TEST_ASSERT_FALSE(config.getPompeAquaLocked());
    
    config.setPompeAquaLocked(true);
    TEST_ASSERT_TRUE(config.getPompeAquaLocked());
    
    config.setPompeAquaLocked(false);
    TEST_ASSERT_FALSE(config.getPompeAquaLocked());
}

// NOTE: test_force_wakeup supprimé - forceWakeUp est géré par Automatism

void test_ota_update_flag() {
    TEST_ASSERT_FALSE(config.getOtaUpdateFlag());
    
    config.setOtaUpdateFlag(true);
    TEST_ASSERT_TRUE(config.getOtaUpdateFlag());
    
    config.setOtaUpdateFlag(false);
    TEST_ASSERT_FALSE(config.getOtaUpdateFlag());
}

// ============================================================================
// TESTS CACHE
// ============================================================================

void test_cache_update() {
    config.setBouffeMatinOk(true);
    config.setBouffeMidiOk(true);
    config.setLastJourBouf(25);
    
    TEST_ASSERT_TRUE(config.hasChanges());
    
    config.updateCache();
    
    TEST_ASSERT_FALSE(config.hasChanges());
    
    // Les valeurs restent inchangées
    TEST_ASSERT_TRUE(config.getBouffeMatinOk());
    TEST_ASSERT_TRUE(config.getBouffeMidiOk());
    TEST_ASSERT_EQUAL(25, config.getLastJourBouf());
}

// ============================================================================
// RUN TESTS
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Tests bouffe flags
    RUN_TEST(test_bouffe_flags_initial_state);
    RUN_TEST(test_bouffe_matin_toggle);
    RUN_TEST(test_bouffe_midi_toggle);
    RUN_TEST(test_bouffe_soir_toggle);
    RUN_TEST(test_last_jour_bouf);
    RUN_TEST(test_reset_bouffe_flags);
    
    // Tests network flags
    RUN_TEST(test_network_flags_initial_state);
    RUN_TEST(test_remote_send_toggle);
    RUN_TEST(test_remote_recv_toggle);
    
    // Tests autres flags
    RUN_TEST(test_pompe_aqua_locked);
    // NOTE: test_force_wakeup supprimé - forceWakeUp est géré par Automatism
    RUN_TEST(test_ota_update_flag);
    
    // Tests cache
    RUN_TEST(test_cache_update);
    
    return UNITY_END();
}
