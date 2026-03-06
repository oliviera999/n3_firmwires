/**
 * Tests unitaires pour la validation NVS
 * v11.166: Ajouté suite à l'audit général
 * 
 * Teste la logique de validation des clés NVS (max 15 caractères)
 * et les opérations de base via mock.
 */

#include <unity.h>
#include "../unit/test_mocks.h"
#include "../test_config/nvs_mock.h"
#include <cstring>

// ============================================================================
// TESTS DE VALIDATION DE CLÉS NVS
// ============================================================================

// Reproduit la logique de validateKey() de nvs_manager.cpp
// Retourne true si la clé est valide (≤15 caractères, non vide, non null)
bool validateNVSKey(const char* key) {
    if (key == nullptr) return false;
    if (key[0] == '\0') return false;
    if (strlen(key) > 15) return false;
    return true;
}

void setUp(void) {
    NVSMock::reset();
}

void tearDown(void) {
    // Nettoyage après chaque test
}

// ============================================================================
// TESTS VALIDATION CLÉS
// ============================================================================

void test_key_validation_null() {
    TEST_ASSERT_FALSE(validateNVSKey(nullptr));
}

void test_key_validation_empty() {
    TEST_ASSERT_FALSE(validateNVSKey(""));
}

void test_key_validation_short() {
    TEST_ASSERT_TRUE(validateNVSKey("a"));
    TEST_ASSERT_TRUE(validateNVSKey("abc"));
    TEST_ASSERT_TRUE(validateNVSKey("key"));
}

void test_key_validation_max_length() {
    // Exactement 15 caractères (limite NVS)
    TEST_ASSERT_TRUE(validateNVSKey("123456789012345"));
}

void test_key_validation_too_long() {
    // 16 caractères (trop long)
    TEST_ASSERT_FALSE(validateNVSKey("1234567890123456"));
    
    // Beaucoup trop long
    TEST_ASSERT_FALSE(validateNVSKey("this_key_is_way_too_long_for_nvs"));
}

void test_key_validation_special_chars() {
    // Caractères spéciaux autorisés
    TEST_ASSERT_TRUE(validateNVSKey("key_with_under"));
    TEST_ASSERT_TRUE(validateNVSKey("key-with-dash"));
    TEST_ASSERT_TRUE(validateNVSKey("key.with.dots"));
}

void test_key_validation_boundary() {
    // Test exact de la limite 15 vs 16
    char key15[16] = "abcdefghijklmno";  // 15 chars
    char key16[17] = "abcdefghijklmnop"; // 16 chars
    
    TEST_ASSERT_EQUAL(15, strlen(key15));
    TEST_ASSERT_EQUAL(16, strlen(key16));
    
    TEST_ASSERT_TRUE(validateNVSKey(key15));
    TEST_ASSERT_FALSE(validateNVSKey(key16));
}

// ============================================================================
// TESTS MOCK NVS OPERATIONS
// ============================================================================

void test_nvs_mock_open_close() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("test_ns", NVS_READWRITE, &handle);
    
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_EQUAL(0, handle);
    
    nvs_close(handle);
}

void test_nvs_mock_set_get_string() {
    nvs_handle_t handle;
    nvs_open("test_ns", NVS_READWRITE, &handle);
    
    // Écrire une valeur
    esp_err_t err = nvs_set_str(handle, "test_key", "test_value");
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Lire la taille requise
    size_t required_size = 0;
    err = nvs_get_str(handle, "test_key", nullptr, &required_size);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(11, required_size);  // "test_value" + '\0'
    
    // Lire la valeur
    char buffer[32];
    size_t buffer_size = sizeof(buffer);
    err = nvs_get_str(handle, "test_key", buffer, &buffer_size);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("test_value", buffer);
    
    nvs_close(handle);
}

void test_nvs_mock_key_not_found() {
    nvs_handle_t handle;
    nvs_open("test_ns", NVS_READWRITE, &handle);
    
    size_t size = 0;
    esp_err_t err = nvs_get_str(handle, "nonexistent", nullptr, &size);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);
    
    nvs_close(handle);
}

void test_nvs_mock_separate_namespaces() {
    nvs_handle_t handle1, handle2;
    nvs_open("ns1", NVS_READWRITE, &handle1);
    nvs_open("ns2", NVS_READWRITE, &handle2);
    
    // Écrire dans ns1
    nvs_set_str(handle1, "key", "value1");
    
    // Écrire dans ns2
    nvs_set_str(handle2, "key", "value2");
    
    // Vérifier que les valeurs sont séparées
    char buffer[32];
    size_t size = sizeof(buffer);
    
    nvs_get_str(handle1, "key", buffer, &size);
    TEST_ASSERT_EQUAL_STRING("value1", buffer);
    
    size = sizeof(buffer);
    nvs_get_str(handle2, "key", buffer, &size);
    TEST_ASSERT_EQUAL_STRING("value2", buffer);
    
    nvs_close(handle1);
    nvs_close(handle2);
}

void test_nvs_mock_overwrite_value() {
    nvs_handle_t handle;
    nvs_open("test_ns", NVS_READWRITE, &handle);
    
    nvs_set_str(handle, "key", "original");
    nvs_set_str(handle, "key", "updated");
    
    char buffer[32];
    size_t size = sizeof(buffer);
    nvs_get_str(handle, "key", buffer, &size);
    TEST_ASSERT_EQUAL_STRING("updated", buffer);
    
    nvs_close(handle);
}

void test_nvs_mock_erase_key() {
    nvs_handle_t handle;
    nvs_open("test_ns", NVS_READWRITE, &handle);
    
    nvs_set_str(handle, "key", "value");
    nvs_erase_key(handle, "key");
    
    size_t size = 0;
    esp_err_t err = nvs_get_str(handle, "key", nullptr, &size);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);
    
    nvs_close(handle);
}

void test_nvs_mock_reset() {
    nvs_handle_t handle;
    nvs_open("test_ns", NVS_READWRITE, &handle);
    nvs_set_str(handle, "key", "value");
    nvs_close(handle);
    
    // Reset le mock
    NVSMock::reset();
    
    // Vérifier que les données sont effacées
    nvs_open("test_ns", NVS_READWRITE, &handle);
    size_t size = 0;
    esp_err_t err = nvs_get_str(handle, "key", nullptr, &size);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);
    nvs_close(handle);
}

// ============================================================================
// RUN TESTS
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Tests validation clés
    RUN_TEST(test_key_validation_null);
    RUN_TEST(test_key_validation_empty);
    RUN_TEST(test_key_validation_short);
    RUN_TEST(test_key_validation_max_length);
    RUN_TEST(test_key_validation_too_long);
    RUN_TEST(test_key_validation_special_chars);
    RUN_TEST(test_key_validation_boundary);
    
    // Tests mock NVS
    RUN_TEST(test_nvs_mock_open_close);
    RUN_TEST(test_nvs_mock_set_get_string);
    RUN_TEST(test_nvs_mock_key_not_found);
    RUN_TEST(test_nvs_mock_separate_namespaces);
    RUN_TEST(test_nvs_mock_overwrite_value);
    RUN_TEST(test_nvs_mock_erase_key);
    RUN_TEST(test_nvs_mock_reset);
    
    return UNITY_END();
}
