#include <iostream>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <chrono>

// Configuration constants
const size_t PAGE_SIZE = 4096;
const size_t RAM_SIZE = 8 * PAGE_SIZE;   // 32KB RAM (reduced to force swapping)
const size_t DISK_SIZE = 64 * PAGE_SIZE; // 256KB Disk
const size_t SWAP_SIZE = 32 * PAGE_SIZE; // 128KB Swap space
const size_t VIRTUAL_ADDR_SPACE = 32 * PAGE_SIZE; // 128KB virtual space

using pfn_t = uint32_t;
using vpn_t = uint32_t;
using swap_slot_t = uint32_t;
using timestamp_t = uint64_t;

// Page metadata structure
struct PageMetadata {
    bool present;           // Is page in RAM?
    bool dirty;            // Has page been modified?
    bool accessed;         // Recently accessed (for LRU)
    bool file_backed;      // Is this a file-backed mapping?
    bool swapped;          // Is page in swap space?
    pfn_t physical_page;   // Physical page frame number
    pfn_t disk_page;       // Disk page number (for file-backed)
    swap_slot_t swap_slot; // Swap slot number (for swapped pages)
    timestamp_t last_access; // For LRU algorithm
    vpn_t virtual_page;    // Back reference to virtual page
    
    PageMetadata() : present(false), dirty(false), accessed(false), 
                    file_backed(false), swapped(false), physical_page(0), 
                    disk_page(0), swap_slot(0), last_access(0), virtual_page(0) {}
};

class SwapSpace {
private:
    std::vector<char> swap_storage;
    std::vector<bool> allocated_slots;
    
public:
    SwapSpace() : swap_storage(SWAP_SIZE, 0), allocated_slots(SWAP_SIZE / PAGE_SIZE, false) {
        std::cout << "Swap space initialized: " << SWAP_SIZE << " bytes ("
                  << SWAP_SIZE / PAGE_SIZE << " slots)\n";
    }
    
    swap_slot_t allocate_slot() {
        for (size_t i = 0; i < allocated_slots.size(); i++) {
            if (!allocated_slots[i]) {
                allocated_slots[i] = true;
                std::cout << "Swap allocated: slot " << i << "\n";
                return i;
            }
        }
        return -1; // No free slots
    }
    
    void free_slot(swap_slot_t slot) {
        if (slot < allocated_slots.size()) {
            allocated_slots[slot] = false;
            std::cout << "Swap freed: slot " << slot << "\n";
        }
    }
    
    void write_page(swap_slot_t slot, const char* data) {
        if (slot < allocated_slots.size()) {
            size_t offset = slot * PAGE_SIZE;
            std::memcpy(&swap_storage[offset], data, PAGE_SIZE);
            std::cout << "Swap write: slot " << slot << "\n";
        }
    }
    
    void read_page(swap_slot_t slot, char* buffer) {
        if (slot < allocated_slots.size()) {
            size_t offset = slot * PAGE_SIZE;
            std::memcpy(buffer, &swap_storage[offset], PAGE_SIZE);
            std::cout << "Swap read: slot " << slot << "\n";
        }
    }
};

class Disk {
private:
    std::vector<char> storage;
    
public:
    Disk() : storage(DISK_SIZE, 0) {
        std::cout << "Disk initialized: " << DISK_SIZE << " bytes\n";
    }
    
    void read_page(pfn_t page_num, char* buffer) {
        size_t offset = page_num * PAGE_SIZE;
        if (offset + PAGE_SIZE <= DISK_SIZE) {
            std::memcpy(buffer, &storage[offset], PAGE_SIZE);
            std::cout << "Disk read: page " << page_num << "\n";
        }
    }
    
    void write_page(pfn_t page_num, const char* buffer) {
        size_t offset = page_num * PAGE_SIZE;
        if (offset + PAGE_SIZE <= DISK_SIZE) {
            std::memcpy(&storage[offset], buffer, PAGE_SIZE);
            std::cout << "Disk write: page " << page_num << "\n";
        }
    }
    
    void write_file(const std::string& filename, const char* data, size_t size) {
        size_t pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (size_t i = 0; i < pages_needed && i * PAGE_SIZE < DISK_SIZE; i++) {
            size_t copy_size = std::min(PAGE_SIZE, size - i * PAGE_SIZE);
            std::memcpy(&storage[i * PAGE_SIZE], data + i * PAGE_SIZE, copy_size);
        }
        std::cout << "File '" << filename << "' written to disk (" << size << " bytes)\n";
    }
};

class RAM {
private:
    std::vector<char> memory;
    std::vector<PageMetadata*> frame_to_page; // Track which page is in each frame
    std::vector<bool> allocated;
    
public:
    RAM() : memory(RAM_SIZE, 0), frame_to_page(RAM_SIZE / PAGE_SIZE, nullptr),
            allocated(RAM_SIZE / PAGE_SIZE, false) {
        std::cout << "RAM initialized: " << RAM_SIZE << " bytes (" 
                  << RAM_SIZE / PAGE_SIZE << " pages)\n";
    }
    
    pfn_t allocate_page(PageMetadata* page_meta = nullptr) {
        for (size_t i = 0; i < allocated.size(); i++) {
            if (!allocated[i]) {
                allocated[i] = true;
                frame_to_page[i] = page_meta;
                std::cout << "RAM allocated: physical page " << i << "\n";
                return i;
            }
        }
        return -1; // No free pages
    }
    
    void free_page(pfn_t page_num) {
        if (page_num < allocated.size()) {
            allocated[page_num] = false;
            frame_to_page[page_num] = nullptr;
            std::cout << "RAM freed: physical page " << page_num << "\n";
        }
    }
    
    char* get_page_ptr(pfn_t page_num) {
        if (page_num < allocated.size()) {
            return &memory[page_num * PAGE_SIZE];
        }
        return nullptr;
    }
    
    void read_page(pfn_t page_num, char* buffer) {
        char* page_ptr = get_page_ptr(page_num);
        if (page_ptr) {
            std::memcpy(buffer, page_ptr, PAGE_SIZE);
        }
    }
    
    void write_page(pfn_t page_num, const char* buffer) {
        char* page_ptr = get_page_ptr(page_num);
        if (page_ptr) {
            std::memcpy(page_ptr, buffer, PAGE_SIZE);
        }
    }
    
    // Get page metadata for a physical frame
    PageMetadata* get_page_metadata(pfn_t frame_num) {
        if (frame_num < frame_to_page.size()) {
            return frame_to_page[frame_num];
        }
        return nullptr;
    }
    
    // Find LRU page for eviction
    pfn_t find_lru_page() {
        timestamp_t oldest_time = UINT64_MAX;
        pfn_t lru_frame = -1;
        
        for (size_t i = 0; i < frame_to_page.size(); i++) {
            if (allocated[i] && frame_to_page[i]) {
                if (frame_to_page[i]->last_access < oldest_time) {
                    oldest_time = frame_to_page[i]->last_access;
                    lru_frame = i;
                }
            }
        }
        return lru_frame;
    }
    
    size_t get_free_frames() {
        size_t count = 0;
        for (bool alloc : allocated) {
            if (!alloc) count++;
        }
        return count;
    }
};

class MMU {
private:
    std::unordered_map<vpn_t, PageMetadata> page_table;
    RAM& ram;
    Disk& disk;
    SwapSpace& swap_space;
    pfn_t next_disk_page;
    timestamp_t current_time;
    
    // Page replacement algorithm
    pfn_t evict_page() {
        std::cout << "RAM full! Evicting LRU page...\n";
        
        pfn_t victim_frame = ram.find_lru_page();
        if (victim_frame == (pfn_t)-1) {
            std::cout << "ERROR: No page to evict!\n";
            return -1;
        }
        
        PageMetadata* victim_meta = ram.get_page_metadata(victim_frame);
        if (!victim_meta) {
            std::cout << "ERROR: Invalid victim page metadata!\n";
            return -1;
        }
        
        std::cout << "Evicting virtual page " << victim_meta->virtual_page 
                  << " from physical frame " << victim_frame << "\n";
        
        // If page is dirty, write it to appropriate storage
        if (victim_meta->dirty) {
            char buffer[PAGE_SIZE];
            ram.read_page(victim_frame, buffer);
            
            if (victim_meta->file_backed) {
                // Write back to original file
                disk.write_page(victim_meta->disk_page, buffer);
            } else {
                // Write to swap space
                if (!victim_meta->swapped) {
                    victim_meta->swap_slot = swap_space.allocate_slot();
                    victim_meta->swapped = true;
                }
                swap_space.write_page(victim_meta->swap_slot, buffer);
            }
        } else if (!victim_meta->file_backed && !victim_meta->swapped) {
            // Clean anonymous page needs swap slot for future access
            victim_meta->swap_slot = swap_space.allocate_slot();
            victim_meta->swapped = true;
            char buffer[PAGE_SIZE];
            ram.read_page(victim_frame, buffer);
            swap_space.write_page(victim_meta->swap_slot, buffer);
        }
        
        // Update page metadata
        victim_meta->present = false;
        victim_meta->dirty = false;
        victim_meta->physical_page = 0;
        
        // Free the physical frame
        ram.free_page(victim_frame);
        
        return victim_frame;
    }
    
public:
    MMU(RAM& r, Disk& d, SwapSpace& s) : ram(r), disk(d), swap_space(s), 
                                         next_disk_page(0), current_time(0) {
        std::cout << "MMU initialized\n";
    }
    
    bool handle_page_fault(vpn_t virtual_page) {
        std::cout << "Page fault: virtual page " << virtual_page << "\n";
        
        auto it = page_table.find(virtual_page);
        if (it == page_table.end()) {
            std::cout << "Invalid page access!\n";
            return false;
        }
        
        PageMetadata& pte = it->second;
        
        // Try to allocate physical page
        pfn_t phys_page = ram.allocate_page(&pte);
        
        // If allocation failed, evict a page
        if (phys_page == (pfn_t)-1) {
            phys_page = evict_page();
            if (phys_page == (pfn_t)-1) {
                return false;
            }
            // Now allocate the freed page
            ram.free_page(phys_page); // Make sure it's marked free
            phys_page = ram.allocate_page(&pte);
        }
        
        // Load page content
        char buffer[PAGE_SIZE];
        
        if (pte.swapped) {
            // Load from swap space
            swap_space.read_page(pte.swap_slot, buffer);
            swap_space.free_slot(pte.swap_slot);
            pte.swapped = false;
        } else if (pte.file_backed) {
            // Load from file
            disk.read_page(pte.disk_page, buffer);
        } else {
            // Zero-fill anonymous page
            std::memset(buffer, 0, PAGE_SIZE);
        }
        
        ram.write_page(phys_page, buffer);
        
        // Update page metadata
        pte.physical_page = phys_page;
        pte.present = true;
        pte.last_access = ++current_time;
        
        return true;
    }
    
    char* translate_address(void* virtual_addr, bool write_access = false) {
        uintptr_t vaddr = reinterpret_cast<uintptr_t>(virtual_addr);
        vpn_t virtual_page = vaddr / PAGE_SIZE;
        size_t page_offset = vaddr % PAGE_SIZE;
        
        auto it = page_table.find(virtual_page);
        if (it == page_table.end()) {
            std::cout << "Invalid virtual address: " << virtual_addr << "\n";
            return nullptr;
        }
        
        PageMetadata& pte = it->second;
        
        // Handle page fault
        if (!pte.present) {
            if (!handle_page_fault(virtual_page)) {
                return nullptr;
            }
        }
        
        // Update access information
        pte.last_access = ++current_time;
        pte.accessed = true;
        if (write_access) {
            pte.dirty = true;
        }
        
        char* phys_addr = ram.get_page_ptr(pte.physical_page);
        return phys_addr + page_offset;
    }
    
    bool map_pages(vpn_t start_page, size_t num_pages, bool file_backed = false, pfn_t disk_start = 0) {
        for (size_t i = 0; i < num_pages; i++) {
            vpn_t virtual_page = start_page + i;
            PageMetadata pte;
            pte.file_backed = file_backed;
            pte.virtual_page = virtual_page;
            if (file_backed) {
                pte.disk_page = disk_start + i;
            }
            page_table[virtual_page] = pte;
        }
        std::cout << "Mapped " << num_pages << " virtual pages starting at " << start_page << "\n";
        return true;
    }
    
    bool unmap_pages(vpn_t start_page, size_t num_pages) {
        for (size_t i = 0; i < num_pages; i++) {
            vpn_t virtual_page = start_page + i;
            auto it = page_table.find(virtual_page);
            if (it != page_table.end()) {
                PageMetadata& pte = it->second;
                if (pte.present) {
                    // Write back if dirty and file-backed
                    if (pte.dirty && pte.file_backed) {
                        char buffer[PAGE_SIZE];
                        ram.read_page(pte.physical_page, buffer);
                        disk.write_page(pte.disk_page, buffer);
                    }
                    ram.free_page(pte.physical_page);
                }
                if (pte.swapped) {
                    swap_space.free_slot(pte.swap_slot);
                }
                page_table.erase(it);
            }
        }
        std::cout << "Unmapped " << num_pages << " virtual pages starting at " << start_page << "\n";
        return true;
    }
    
    void print_memory_status() {
        std::cout << "\n=== Memory Status ===\n";
        std::cout << "RAM free frames: " << ram.get_free_frames() << "/" << (RAM_SIZE / PAGE_SIZE) << "\n";
        std::cout << "Page table entries: " << page_table.size() << "\n";
        
        std::cout << "\n=== Page Table ===\n";
        for (const auto& entry : page_table) {
            const auto& pte = entry.second;
            std::cout << "VPN " << entry.first << " -> ";
            if (pte.present) {
                std::cout << "PFN " << pte.physical_page;
                if (pte.dirty) std::cout << " [DIRTY]";
                if (pte.accessed) std::cout << " [ACCESSED]";
            } else if (pte.swapped) {
                std::cout << "SWAP slot " << pte.swap_slot;
            } else {
                std::cout << "Not loaded";
            }
            if (pte.file_backed) {
                std::cout << " (file-backed, disk page " << pte.disk_page << ")";
            }
            std::cout << " [LRU: " << pte.last_access << "]\n";
        }
        std::cout << "==================\n\n";
    }
};

class VirtualMemorySystem {
private:
    RAM ram;
    Disk disk;
    SwapSpace swap_space;
    MMU mmu;
    uintptr_t next_virtual_addr;
    
public:
    VirtualMemorySystem() : mmu(ram, disk, swap_space), next_virtual_addr(0x10000000) {
        std::cout << "Virtual Memory System with Swapping initialized\n\n";
    }
    
    void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
        size_t pages_needed = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        
        uintptr_t virtual_addr = next_virtual_addr;
        vpn_t start_page = virtual_addr / PAGE_SIZE;
        
        bool file_backed = (fd != -1);
        pfn_t disk_start = 0;
        
        if (file_backed) {
            disk_start = offset / PAGE_SIZE;
        }
        
        if (mmu.map_pages(start_page, pages_needed, file_backed, disk_start)) {
            next_virtual_addr += pages_needed * PAGE_SIZE;
            std::cout << "mmap returned: " << std::hex << virtual_addr << std::dec 
                      << " (" << length << " bytes, " << pages_needed << " pages)\n\n";
            return reinterpret_cast<void*>(virtual_addr);
        }
        
        return nullptr;
    }
    
    int munmap(void* addr, size_t length) {
        uintptr_t virtual_addr = reinterpret_cast<uintptr_t>(addr);
        vpn_t start_page = virtual_addr / PAGE_SIZE;
        size_t pages_needed = (length + PAGE_SIZE - 1) / PAGE_SIZE;
        
        if (mmu.unmap_pages(start_page, pages_needed)) {
            std::cout << "munmap successful\n\n";
            return 0;
        }
        return -1;
    }
    
    void write_memory(void* addr, const char* data, size_t size) {
        std::cout << "Writing " << size << " bytes to " << addr << "\n";
        char* phys_addr = mmu.translate_address(addr, true); // write_access = true
        if (phys_addr) {
            std::memcpy(phys_addr, data, size);
            std::cout << "Write successful\n";
        } else {
            std::cout << "Write failed - invalid address\n";
        }
        std::cout << "\n";
    }
    
    void read_memory(void* addr, char* buffer, size_t size) {
        std::cout << "Reading " << size << " bytes from " << addr << "\n";
        char* phys_addr = mmu.translate_address(addr, false); // write_access = false
        if (phys_addr) {
            std::memcpy(buffer, phys_addr, size);
            std::cout << "Read successful: '" << std::string(buffer, size) << "'\n";
        } else {
            std::cout << "Read failed - invalid address\n";
        }
        std::cout << "\n";
    }
    
    void print_status() {
        mmu.print_memory_status();
    }
    
    void create_file(const std::string& filename, const std::string& content) {
        disk.write_file(filename, content.c_str(), content.size());
    }
};

int main() {
    VirtualMemorySystem vm_system;
    
    vm_system.create_file("test.txt", "Hello from file! This content will be memory mapped.");
    
    std::cout << "\n=== Testing Memory Pressure and Swapping ===\n";
    
    // Allocate multiple regions to force swapping
    std::vector<void*> mappings;
    
    // Allocate enough memory to exceed RAM capacity
    for (int i = 0; i < 6; i++) {
        void* mem = vm_system.mmap(nullptr, 8192, 0, 0, -1, 0);
        mappings.push_back(mem);
        
        // Write different data to each mapping
        std::string data = "Data block " + std::to_string(i) + " - some test content here!";
        vm_system.write_memory(mem, data.c_str(), data.size());
        
        vm_system.print_status();
    }
    
    std::cout << "\n=== Testing LRU Access Patterns ===\n";
    
    // Access older mappings to test LRU
    char buffer[64] = {0};
    vm_system.read_memory(mappings[0], buffer, 32);
    vm_system.read_memory(mappings[2], buffer, 32);
    
    vm_system.print_status();
    
    // Allocate one more to force more swapping
    void* final_mem = vm_system.mmap(nullptr, 4096, 0, 0, -1, 0);
    vm_system.write_memory(final_mem, "Final allocation", 16);
    
    vm_system.print_status();
    
    // Clean up
    for (void* mem : mappings) {
        vm_system.munmap(mem, 8192);
    }
    vm_system.munmap(final_mem, 4096);
    
    vm_system.print_status();
    
    return 0;
}