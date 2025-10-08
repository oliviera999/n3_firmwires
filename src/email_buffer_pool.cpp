#include "email_buffer_pool.h"

// Définition des variables statiques
char EmailBufferPool::buffer[EmailBufferPool::BUFFER_SIZE];
bool EmailBufferPool::inUse = false;
unsigned long EmailBufferPool::lastUsedMs = 0;
