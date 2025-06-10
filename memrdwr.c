#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#define MEMORY_SIZE (1UL * 1024 * 1024)  // 1MB
#define PAGE_SIZE 4096

int main() {
    // Allocate memory
    char *memory = mmap(NULL, MEMORY_SIZE, 
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    
    if (memory == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    printf("PID: %d\n", getpid());
    printf("Memory allocated at %p\n", (void*)memory);

    // Touch each page to ensure physical allocation
    for (size_t i = 0; i < MEMORY_SIZE; i += PAGE_SIZE) {
        memory[i] = 'A';
    }

    // Read and write operations
    while(1) {
        // Write pattern
        for (size_t i = 0; i < MEMORY_SIZE; i += PAGE_SIZE) {
            memory[i] = (char)(i % 256);
        }

        // Read pattern
        volatile char sum = 0;  // prevent optimization
        for (size_t i = 0; i < MEMORY_SIZE; i += PAGE_SIZE) {
            sum += memory[i];
        }

        sleep(1);  // Sleep to prevent CPU hogging
    }

    munmap(memory, MEMORY_SIZE);
    return 0;
}