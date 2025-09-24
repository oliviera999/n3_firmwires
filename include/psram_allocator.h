#ifndef PSRAM_ALLOCATOR_H
#define PSRAM_ALLOCATOR_H

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <map>
#include <vector>

// PSRAM allocation configuration
#define PSRAM_MIN_ALLOCATION 1024      // Minimum size to allocate in PSRAM
#define PSRAM_SAFETY_MARGIN 32768      // Keep 32KB free in PSRAM
#define PSRAM_POOL_SIZE 10              // Number of pre-allocated pools

// PSRAM allocator with memory pools
class PSRAMAllocator {
private:
    struct MemoryBlock {
        void* ptr;
        size_t size;
        bool inUse;
        unsigned long allocTime;
        const char* tag;
    };
    
    struct MemoryPool {
        size_t blockSize;
        size_t blockCount;
        std::vector<MemoryBlock> blocks;
        size_t usedBlocks;
    };
    
    std::map<size_t, MemoryPool> pools;
    std::map<void*, MemoryBlock*> allocations;
    SemaphoreHandle_t mutex;
    
    // Statistics
    size_t totalAllocated;
    size_t peakAllocated;
    size_t allocationCount;
    size_t failureCount;
    
    static PSRAMAllocator* instance;
    static const char* TAG;
    
    PSRAMAllocator() : totalAllocated(0), peakAllocated(0), 
                      allocationCount(0), failureCount(0) {
        mutex = xSemaphoreCreateMutex();
        initializePools();
    }
    
    void initializePools() {
        // Create pools for common sizes
        size_t poolSizes[] = {512, 1024, 2048, 4096, 8192, 16384};
        size_t poolCounts[] = {20, 15, 10, 8, 5, 3};
        
        for (int i = 0; i < 6; i++) {
            createPool(poolSizes[i], poolCounts[i]);
        }
        
        ESP_LOGI(TAG, "PSRAM pools initialized");
    }
    
    void createPool(size_t blockSize, size_t blockCount) {
        if (!psramFound()) {
            ESP_LOGW(TAG, "PSRAM not found, pool creation skipped");
            return;
        }
        
        MemoryPool pool;
        pool.blockSize = blockSize;
        pool.blockCount = blockCount;
        pool.usedBlocks = 0;
        
        for (size_t i = 0; i < blockCount; i++) {
            void* ptr = heap_caps_malloc(blockSize, MALLOC_CAP_SPIRAM);
            if (ptr) {
                MemoryBlock block = {
                    .ptr = ptr,
                    .size = blockSize,
                    .inUse = false,
                    .allocTime = 0,
                    .tag = nullptr
                };
                pool.blocks.push_back(block);
            } else {
                ESP_LOGW(TAG, "Failed to allocate PSRAM block %d of size %d", i, blockSize);
                break;
            }
        }
        
        pools[blockSize] = pool;
        ESP_LOGI(TAG, "Created pool: %d blocks of %d bytes", pool.blocks.size(), blockSize);
    }
    
public:
    static PSRAMAllocator* getInstance() {
        if (!instance) {
            instance = new PSRAMAllocator();
        }
        return instance;
    }
    
    ~PSRAMAllocator() {
        // Free all pools
        for (auto& [size, pool] : pools) {
            for (auto& block : pool.blocks) {
                if (block.ptr) {
                    heap_caps_free(block.ptr);
                }
            }
        }
        
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    // Allocate memory from PSRAM
    void* allocate(size_t size, const char* tag = "unknown") {
        if (!psramFound()) {
            ESP_LOGE(TAG, "PSRAM not available");
            return nullptr;
        }
        
        // Check if PSRAM has enough free space
        size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (freePSRAM < size + PSRAM_SAFETY_MARGIN) {
            ESP_LOGW(TAG, "Insufficient PSRAM: requested %d, free %d", size, freePSRAM);
            failureCount++;
            return nullptr;
        }
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        void* ptr = nullptr;
        
        // Try to allocate from pool first
        for (auto& [poolSize, pool] : pools) {
            if (poolSize >= size && pool.usedBlocks < pool.blockCount) {
                // Find free block
                for (auto& block : pool.blocks) {
                    if (!block.inUse) {
                        block.inUse = true;
                        block.allocTime = millis();
                        block.tag = tag;
                        pool.usedBlocks++;
                        
                        ptr = block.ptr;
                        allocations[ptr] = &block;
                        
                        ESP_LOGD(TAG, "Allocated %d bytes from pool for %s", poolSize, tag);
                        break;
                    }
                }
                if (ptr) break;
            }
        }
        
        // If no pool block available, allocate directly
        if (!ptr) {
            ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
            if (ptr) {
                MemoryBlock* block = new MemoryBlock{
                    .ptr = ptr,
                    .size = size,
                    .inUse = true,
                    .allocTime = millis(),
                    .tag = tag
                };
                allocations[ptr] = block;
                
                ESP_LOGD(TAG, "Allocated %d bytes directly from PSRAM for %s", size, tag);
            } else {
                failureCount++;
                ESP_LOGE(TAG, "Failed to allocate %d bytes from PSRAM", size);
            }
        }
        
        if (ptr) {
            totalAllocated += size;
            if (totalAllocated > peakAllocated) {
                peakAllocated = totalAllocated;
            }
            allocationCount++;
        }
        
        xSemaphoreGive(mutex);
        return ptr;
    }
    
    // Free PSRAM memory
    void free(void* ptr) {
        if (!ptr) return;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        auto it = allocations.find(ptr);
        if (it != allocations.end()) {
            MemoryBlock* block = it->second;
            
            // Check if it's from a pool
            bool fromPool = false;
            for (auto& [poolSize, pool] : pools) {
                for (auto& poolBlock : pool.blocks) {
                    if (&poolBlock == block) {
                        // Return to pool
                        poolBlock.inUse = false;
                        poolBlock.tag = nullptr;
                        pool.usedBlocks--;
                        fromPool = true;
                        
                        ESP_LOGD(TAG, "Returned %d bytes to pool", poolSize);
                        break;
                    }
                }
                if (fromPool) break;
            }
            
            if (!fromPool) {
                // Direct allocation, free it
                heap_caps_free(ptr);
                totalAllocated -= block->size;
                delete block;
                
                ESP_LOGD(TAG, "Freed %d bytes from PSRAM", block->size);
            } else {
                totalAllocated -= block->size;
            }
            
            allocations.erase(it);
        } else {
            ESP_LOGW(TAG, "Attempted to free unknown pointer");
        }
        
        xSemaphoreGive(mutex);
    }
    
    // Reallocate memory
    void* reallocate(void* ptr, size_t newSize, const char* tag = "unknown") {
        if (!ptr) {
            return allocate(newSize, tag);
        }
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        auto it = allocations.find(ptr);
        if (it == allocations.end()) {
            xSemaphoreGive(mutex);
            ESP_LOGE(TAG, "Attempted to reallocate unknown pointer");
            return nullptr;
        }
        
        MemoryBlock* oldBlock = it->second;
        size_t oldSize = oldBlock->size;
        
        xSemaphoreGive(mutex);
        
        // Allocate new block
        void* newPtr = allocate(newSize, tag);
        if (newPtr) {
            // Copy data
            memcpy(newPtr, ptr, min(oldSize, newSize));
            // Free old block
            free(ptr);
        }
        
        return newPtr;
    }
    
    // Get statistics
    void getStats(size_t& total, size_t& peak, size_t& count, size_t& failures) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        total = totalAllocated;
        peak = peakAllocated;
        count = allocationCount;
        failures = failureCount;
        
        xSemaphoreGive(mutex);
    }
    
    // Print memory map
    void printMemoryMap() {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        ESP_LOGI(TAG, "=== PSRAM Memory Map ===");
        ESP_LOGI(TAG, "Total PSRAM: %d KB", heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024);
        ESP_LOGI(TAG, "Free PSRAM: %d KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        ESP_LOGI(TAG, "Allocated: %d KB", totalAllocated / 1024);
        ESP_LOGI(TAG, "Peak: %d KB", peakAllocated / 1024);
        
        ESP_LOGI(TAG, "Pools:");
        for (const auto& [size, pool] : pools) {
            ESP_LOGI(TAG, "  %d bytes: %d/%d blocks used", 
                    size, pool.usedBlocks, pool.blockCount);
        }
        
        ESP_LOGI(TAG, "Active allocations:");
        for (const auto& [ptr, block] : allocations) {
            if (block->inUse) {
                unsigned long age = millis() - block->allocTime;
                ESP_LOGI(TAG, "  %p: %d bytes, tag=%s, age=%lu ms", 
                        ptr, block->size, block->tag, age);
            }
        }
        
        ESP_LOGI(TAG, "Statistics:");
        ESP_LOGI(TAG, "  Total allocations: %d", allocationCount);
        ESP_LOGI(TAG, "  Failed allocations: %d", failureCount);
        ESP_LOGI(TAG, "========================");
        
        xSemaphoreGive(mutex);
    }
};

PSRAMAllocator* PSRAMAllocator::instance = nullptr;
const char* PSRAMAllocator::TAG = "PSRAM";

// C-style wrapper functions for easy use
inline void* psram_malloc(size_t size, const char* tag = "unknown") {
    return PSRAMAllocator::getInstance()->allocate(size, tag);
}

inline void psram_free(void* ptr) {
    PSRAMAllocator::getInstance()->free(ptr);
}

inline void* psram_realloc(void* ptr, size_t size, const char* tag = "unknown") {
    return PSRAMAllocator::getInstance()->reallocate(ptr, size, tag);
}

// Template for PSRAM-allocated objects
template<typename T>
class PSRAMObject {
private:
    T* obj;
    
public:
    PSRAMObject() : obj(nullptr) {
        void* mem = psram_malloc(sizeof(T), typeid(T).name());
        if (mem) {
            obj = new(mem) T();
        }
    }
    
    template<typename... Args>
    PSRAMObject(Args&&... args) : obj(nullptr) {
        void* mem = psram_malloc(sizeof(T), typeid(T).name());
        if (mem) {
            obj = new(mem) T(std::forward<Args>(args)...);
        }
    }
    
    ~PSRAMObject() {
        if (obj) {
            obj->~T();
            psram_free(obj);
        }
    }
    
    T* operator->() { return obj; }
    const T* operator->() const { return obj; }
    T& operator*() { return *obj; }
    const T& operator*() const { return *obj; }
    
    bool isValid() const { return obj != nullptr; }
};

#endif // PSRAM_ALLOCATOR_H