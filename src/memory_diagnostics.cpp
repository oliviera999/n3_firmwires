#include "memory_diagnostics.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

#if MEM_DIAG_ENABLED

HeapSnapshot MemoryDiagnostics::_snapshots[MemoryDiagnostics::MAX_SNAPSHOTS];
size_t MemoryDiagnostics::_snapshotCount = 0;

HeapSnapshot MemoryDiagnostics::captureSnapshot(const char* label) {
    HeapSnapshot snapshot;
    snapshot.label = label;
    snapshot.timestamp = millis();
    snapshot.freeHeap = ESP.getFreeHeap();
    snapshot.minFreeHeap = ESP.getMinFreeHeap();
    snapshot.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    snapshot.heapSize = ESP.getHeapSize();
    snapshot.allocatedHeap = snapshot.heapSize - snapshot.freeHeap;
    
    // Calcul de la fragmentation
    if (snapshot.freeHeap > 0) {
        snapshot.fragmentation = ((snapshot.freeHeap - snapshot.largestBlock) * 100) / snapshot.freeHeap;
    } else {
        snapshot.fragmentation = 0;
    }
    
    // Stocker le snapshot si on a de la place
    if (_snapshotCount < MAX_SNAPSHOTS) {
        _snapshots[_snapshotCount++] = snapshot;
    }
    
    // Afficher le snapshot
    printSnapshot(snapshot);
    
    return snapshot;
}

void MemoryDiagnostics::printSnapshot(const HeapSnapshot& snapshot) {
    Serial.printf("[MEM_DIAG] Snapshot: %s at %u ms\n", snapshot.label, snapshot.timestamp);
    Serial.printf("[MEM_DIAG]   Heap: free=%u bytes, min=%u bytes, size=%u bytes\n",
                  snapshot.freeHeap, snapshot.minFreeHeap, snapshot.heapSize);
    Serial.printf("[MEM_DIAG]   Largest block: %u bytes\n", snapshot.largestBlock);
    Serial.printf("[MEM_DIAG]   Fragmentation: %u%%\n", snapshot.fragmentation);
    Serial.printf("[MEM_DIAG]   Allocated: %u bytes\n", snapshot.allocatedHeap);
}

void MemoryDiagnostics::compareSnapshots(const HeapSnapshot& before, const HeapSnapshot& after) {
    Serial.printf("[MEM_DIAG] Comparison: %s -> %s\n", before.label, after.label);
    
    int32_t heapDelta = static_cast<int32_t>(after.freeHeap) - static_cast<int32_t>(before.freeHeap);
    int32_t largestDelta = static_cast<int32_t>(after.largestBlock) - static_cast<int32_t>(before.largestBlock);
    int32_t fragDelta = static_cast<int32_t>(after.fragmentation) - static_cast<int32_t>(before.fragmentation);
    int32_t allocatedDelta = static_cast<int32_t>(after.allocatedHeap) - static_cast<int32_t>(before.allocatedHeap);
    
    Serial.printf("[MEM_DIAG]   Heap delta: %d bytes (%s)\n",
                  heapDelta, heapDelta < 0 ? "decrease" : heapDelta > 0 ? "increase" : "unchanged");
    Serial.printf("[MEM_DIAG]   Largest block delta: %d bytes\n", largestDelta);
    Serial.printf("[MEM_DIAG]   Fragmentation change: %u%% -> %u%% (%d%%)\n",
                  before.fragmentation, after.fragmentation, fragDelta);
    Serial.printf("[MEM_DIAG]   Allocated delta: %d bytes\n", allocatedDelta);
    Serial.printf("[MEM_DIAG]   Time delta: %u ms\n", after.timestamp - before.timestamp);
    
    // Avertissement si fragmentation augmente significativement
    if (fragDelta > 5) {
        Serial.printf("[MEM_DIAG]   ⚠️ Fragmentation increased significantly (+%d%%)\n", fragDelta);
    }
    
    // Avertissement si plus grand bloc diminue significativement
    if (largestDelta < -5000) {
        Serial.printf("[MEM_DIAG]   ⚠️ Largest block decreased significantly (%d bytes)\n", largestDelta);
    }
}

void MemoryDiagnostics::printDetailedHeapInfo() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = ESP.getMinFreeHeap();
    uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t heapSize = ESP.getHeapSize();
    uint32_t fragmentation = (freeHeap > 0) ? ((freeHeap - largestBlock) * 100 / freeHeap) : 0;
    
    Serial.println(F("[MEM_DIAG] === Detailed Heap Information ==="));
    Serial.printf("[MEM_DIAG] Total heap size: %u bytes (%u KB)\n", heapSize, heapSize / 1024);
    Serial.printf("[MEM_DIAG] Free heap: %u bytes (%u KB)\n", freeHeap, freeHeap / 1024);
    Serial.printf("[MEM_DIAG] Min free heap: %u bytes (%u KB)\n", minFreeHeap, minFreeHeap / 1024);
    Serial.printf("[MEM_DIAG] Largest free block: %u bytes (%u KB)\n", largestBlock, largestBlock / 1024);
    Serial.printf("[MEM_DIAG] Fragmentation: %u%%\n", fragmentation);
    Serial.printf("[MEM_DIAG] Allocated: %u bytes (%u KB)\n", 
                  heapSize - freeHeap, (heapSize - freeHeap) / 1024);
    
    // Informations par capacité
    uint32_t free8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t free32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    Serial.printf("[MEM_DIAG] Free 8-bit: %u bytes\n", free8bit);
    Serial.printf("[MEM_DIAG] Free 32-bit: %u bytes\n", free32bit);
    
    Serial.println(F("[MEM_DIAG] =================================="));
}

void MemoryDiagnostics::analyzeFragmentation() {
    if (_snapshotCount < 2) {
        Serial.println(F("[MEM_DIAG] Not enough snapshots for analysis"));
        return;
    }
    
    Serial.println(F("[MEM_DIAG] === Fragmentation Analysis ==="));
    
    // Trouver le snapshot avec la fragmentation la plus élevée
    const HeapSnapshot* maxFragSnapshot = nullptr;
    uint32_t maxFrag = 0;
    
    for (size_t i = 0; i < _snapshotCount; i++) {
        if (_snapshots[i].fragmentation > maxFrag) {
            maxFrag = _snapshots[i].fragmentation;
            maxFragSnapshot = &_snapshots[i];
        }
    }
    
    if (maxFragSnapshot) {
        Serial.printf("[MEM_DIAG] Maximum fragmentation: %u%% at snapshot '%s'\n",
                      maxFrag, maxFragSnapshot->label);
    }
    
    // Analyser l'évolution
    Serial.println(F("[MEM_DIAG] Evolution:"));
    for (size_t i = 1; i < _snapshotCount; i++) {
        const HeapSnapshot& prev = _snapshots[i - 1];
        const HeapSnapshot& curr = _snapshots[i];
        int32_t fragDelta = static_cast<int32_t>(curr.fragmentation) - static_cast<int32_t>(prev.fragmentation);
        
        if (fragDelta > 0) {
            Serial.printf("[MEM_DIAG]   %s -> %s: fragmentation +%d%% (heap -%u bytes, largest -%u bytes)\n",
                          prev.label, curr.label, fragDelta,
                          prev.freeHeap - curr.freeHeap,
                          prev.largestBlock - curr.largestBlock);
        }
    }
    
    // Snapshot actuel
    HeapSnapshot current = captureSnapshot("current_analysis");
    
    Serial.println(F("[MEM_DIAG] ==============================="));
}

#endif // MEM_DIAG_ENABLED
