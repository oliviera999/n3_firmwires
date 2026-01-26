#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdint>

#if defined(PROFILE_TEST)
#define MEM_DIAG_ENABLED 1
#else
#define MEM_DIAG_ENABLED 0
#endif

/**
 * Snapshot de l'état du heap à un moment donné
 */
struct HeapSnapshot {
    const char* label;
    uint32_t timestamp;
    uint32_t freeHeap;
    uint32_t minFreeHeap;
    uint32_t largestBlock;
    uint32_t fragmentation;
    uint32_t heapSize;
    uint32_t allocatedHeap;
    
    HeapSnapshot() 
        : label(nullptr)
        , timestamp(0)
        , freeHeap(0)
        , minFreeHeap(0)
        , largestBlock(0)
        , fragmentation(0)
        , heapSize(0)
        , allocatedHeap(0) {}
};

/**
 * Système de diagnostic de fragmentation mémoire
 * 
 * Permet de capturer des snapshots du heap à différents moments
 * et d'analyser l'évolution de la fragmentation.
 */
class MemoryDiagnostics {
public:
    /**
     * Capture un snapshot du heap avec un label
     * @param label Label descriptif du snapshot
     * @return Snapshot capturé
     */
    static HeapSnapshot captureSnapshot(const char* label);
    
    /**
     * Compare deux snapshots et affiche les différences
     * @param before Snapshot avant
     * @param after Snapshot après
     */
    static void compareSnapshots(const HeapSnapshot& before, const HeapSnapshot& after);
    
    /**
     * Affiche des informations détaillées sur le heap
     */
    static void printDetailedHeapInfo();
    
    /**
     * Analyse la fragmentation et identifie les causes probables
     */
    static void analyzeFragmentation();
    
    /**
     * Affiche un snapshot de manière formatée
     * @param snapshot Snapshot à afficher
     */
    static void printSnapshot(const HeapSnapshot& snapshot);
    
private:
    static constexpr size_t MAX_SNAPSHOTS = 20;
    static HeapSnapshot _snapshots[MAX_SNAPSHOTS];
    static size_t _snapshotCount;
};

#if MEM_DIAG_ENABLED
#define MEM_DIAG_SNAPSHOT(label) MemoryDiagnostics::captureSnapshot(label)
#define MEM_DIAG_COMPARE(before, after) MemoryDiagnostics::compareSnapshots(before, after)
#define MEM_DIAG_ANALYZE() MemoryDiagnostics::analyzeFragmentation()
#define MEM_DIAG_DETAILED() MemoryDiagnostics::printDetailedHeapInfo()
#else
#define MEM_DIAG_SNAPSHOT(label) HeapSnapshot()
#define MEM_DIAG_COMPARE(before, after) ((void)0)
#define MEM_DIAG_ANALYZE() ((void)0)
#define MEM_DIAG_DETAILED() ((void)0)
#endif
