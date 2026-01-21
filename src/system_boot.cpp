#include "system_boot.h"
#include "bootstrap_storage.h"
#include "bootstrap_network.h"
#include "bootstrap_services.h"
#include <Arduino.h>

// Implémentation du namespace SystemBoot qui délègue aux namespaces existants
namespace SystemBoot {

void setupHostname(char* buffer, size_t bufferSize) {
    BootstrapNetwork::setupHostname(buffer, bufferSize);
}

void initializeStorage(AppContext& ctx, unsigned long& lastDigestMs, uint32_t& lastDigestSeq) {
    BootstrapStorage::initialize(ctx, lastDigestMs, lastDigestSeq);
}

void validatePendingOta(OtaState& state) {
    // Adapter l'OtaState SystemBoot vers BootstrapNetwork::OtaState
    BootstrapNetwork::OtaState netState{
        state.justUpdated,
        state.previousVersion,
        state.lastCheck
    };
    BootstrapNetwork::validatePendingOta(netState);
}

void initializeTimekeeping(AppContext& ctx) {
    BootstrapServices::initializeTimekeeping(ctx);
}

bool initializeDisplay(AppContext& ctx) {
    return BootstrapServices::initializeDisplay(ctx);
}

void initializePeripherals(AppContext& ctx) {
    BootstrapServices::initializePeripherals(ctx);
}

void loadConfiguration(AppContext& ctx) {
    BootstrapServices::loadConfiguration(ctx);
}

void finalizeDisplay(AppContext& ctx) {
    BootstrapServices::finalizeDisplay(ctx);
}

bool connectWifi(AppContext& ctx, const char* hostname) {
    return BootstrapNetwork::connect(ctx, hostname);
}

void checkForOtaUpdate(AppContext& ctx) {
    BootstrapNetwork::checkForOtaUpdate(ctx);
}

void onWifiReady(AppContext& ctx, const char* hostname, OtaState& state) {
    BootstrapNetwork::OtaState netState{
        state.justUpdated,
        state.previousVersion,
        state.lastCheck
    };
    BootstrapNetwork::onWifiReady(ctx, hostname, netState);
}

void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state) {
    BootstrapNetwork::OtaState netState{
        state.justUpdated,
        state.previousVersion,
        state.lastCheck
    };
    BootstrapNetwork::postConfiguration(ctx, hostname, netState);
}

} // namespace SystemBoot
