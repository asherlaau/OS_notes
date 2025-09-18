#include <iostream>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstring>
#include <cassert>
#include <cstdint>  // For uintptr_t
// Configuration constants
const size_t PAGE_SIZE = 4096;
const size_t RAM_SIZE = 16 * PAGE_SIZE;  // 64KB RAM
const size_t DISK_SIZE = 64 * PAGE_SIZE; // 256KB Disk
const size_t VIRTUAL_ADDR_SPACE = 32 * PAGE_SIZE; // 128KB virtual space

// Page frame number type
using pfn_t = uint32_t;
using vpn_t = uint32_t;  // Virtual page number

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
    
    // Simulate file operations
    void write_file(const std::string& filename, const char* data, size_t size) {
        // Simplified: write to beginning of disk
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
    std::vector<bool> allocated;
    
public:
    RAM() : memory(RAM_SIZE, 0), allocated(RAM_SIZE / PAGE_SIZE, false) {
        std::cout << "RAM initialized: " << RAM_SIZE << " bytes (" 
                  << RAM_SIZE / PAGE_SIZE << " pages)\n";
    }
    
    pfn_t allocate_page() {
        for (size_t i = 0; i < allocated.size(); i++) {
            if (!allocated[i]) {
                allocated[i] = true;
                std::cout << "RAM allocated: physical page " << i << "\n";
                return i;
            }
        }
        return -1; // No free pages
    }
    
    void free_page(pfn_t page_num) {
        if (page_num < allocated.size()) {
            allocated[page_num] = false;
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
};

struct PageTableEntry {
    pfn_t physical_page;
    bool present;           // Is page in RAM?
    bool dirty;            // Has page been modified?
    bool file_backed;      // Is this a file-backed mapping?
    pfn_t disk_page;       // Which disk page backs this?
    
    PageTableEntry() : physical_page(0), present(false), dirty(false), 
                      file_backed(false), disk_page(0) {}
};

class MMU {
private:
    std::unordered_map<vpn_t, PageTableEntry> page_table;
    RAM& ram;
    Disk& disk;
    pfn_t next_disk_page;
    
public:
    MMU(RAM& r, Disk& d) : ram(r), disk(d), next_disk_page(0) {
        std::cout << "MMU initialized\n";
    }
    
    // Handle page fault - load page from disk to RAM
    bool handle_page_fault(vpn_t virtual_page) {
        std::cout << "Page fault: virtual page " << virtual_page << "\n";
        
        auto& pte = page_table[virtual_page];
        
        // Allocate physical page
        pfn_t phys_page = ram.allocate_page();
        if (phys_page == (pfn_t)-1) {
            std::cout << "Out of RAM! Need to implement swapping\n";
            return false;
        }
        
        // Load from disk if file-backed
        if (pte.file_backed) {
            char buffer[PAGE_SIZE];
            disk.read_page(pte.disk_page, buffer);
            ram.write_page(phys_page, buffer);
        } else {
            // Zero-fill anonymous page
            char buffer[PAGE_SIZE] = {0};
            ram.write_page(phys_page, buffer);
        }
        
        pte.physical_page = phys_page;
        pte.present = true;
        
        return true;
    }
    
    // Translate virtual address to physical
    char* translate_address(void* virtual_addr) {
        uintptr_t vaddr = reinterpret_cast<uintptr_t>(virtual_addr);
        vpn_t virtual_page = vaddr / PAGE_SIZE;
        size_t page_offset = vaddr % PAGE_SIZE;
        
        auto it = page_table.find(virtual_page);
        if (it == page_table.end()) {
            std::cout << "Invalid virtual address: " << virtual_addr << "\n";
            return nullptr;
        }
        
        PageTableEntry& pte = it->second;
        
        // Handle page fault
        if (!pte.present) {
            //bugs here handle page would create page, but shouldve have error of seg fault
            if (!handle_page_fault(virtual_page)) {
                return nullptr;
            }
        }
        
        char* phys_addr = ram.get_page_ptr(pte.physical_page);
        return phys_addr + page_offset;
    }
    
    // Map virtual pages (used by mmap)
    bool map_pages(vpn_t start_page, size_t num_pages, bool file_backed = false, pfn_t disk_start = 0) {
        for (size_t i = 0; i < num_pages; i++) {
            vpn_t virtual_page = start_page + i;
            PageTableEntry pte;
            pte.file_backed = file_backed;
            if (file_backed) {
                pte.disk_page = disk_start + i;
            }
            page_table[virtual_page] = pte;
        }
        std::cout << "Mapped " << num_pages << " virtual pages starting at " << start_page << "\n";
        return true;
    }
    
    // Unmap virtual pages (used by munmap)
    bool unmap_pages(vpn_t start_page, size_t num_pages) {
        for (size_t i = 0; i < num_pages; i++) {
            vpn_t virtual_page = start_page + i;
            auto it = page_table.find(virtual_page);
            if (it != page_table.end()) {
                PageTableEntry& pte = it->second;
                if (pte.present) {
                    // Write back if dirty and file-backed
                    if (pte.dirty && pte.file_backed) {
                        char buffer[PAGE_SIZE];
                        ram.read_page(pte.physical_page, buffer);
                        disk.write_page(pte.disk_page, buffer);
                    }
                    ram.free_page(pte.physical_page);
                }
                page_table.erase(it);
            }
        }
        std::cout << "Unmapped " << num_pages << " virtual pages starting at " << start_page << "\n";
        return true;
    }
    
    void print_page_table() {
        std::cout << "\n=== Page Table ===\n";
        for (const auto& entry : page_table) {
            const auto& pte = entry.second;
            std::cout << "VPN " << entry.first << " -> ";
            if (pte.present) {
                std::cout << "PFN " << pte.physical_page;
            } else {
                std::cout << "Not in RAM";
            }
            if (pte.file_backed) {
                std::cout << " (file-backed, disk page " << pte.disk_page << ")";
            }
            std::cout << "\n";
        }
        std::cout << "================\n\n";
    }
};

class VirtualMemorySystem {
private:
    RAM ram;
    Disk disk;
    MMU mmu;
    uintptr_t next_virtual_addr;
    
public:
    VirtualMemorySystem() : mmu(ram, disk), next_virtual_addr(0x10000000) {
        std::cout << "Virtual Memory System initialized\n\n";
    }
    
    // Simplified mmap implementation
    void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
        // Simplified: ignore most parameters, allocate next available virtual address
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
    
    // Simplified munmap implementation
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
    
    // Memory access simulation
    void write_memory(void* addr, const char* data, size_t size) {
        std::cout << "Writing " << size << " bytes to " << addr << "\n";
        char* phys_addr = mmu.translate_address(addr);
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
        char* phys_addr = mmu.translate_address(addr);
        if (phys_addr) {
            std::memcpy(buffer, phys_addr, size);
            std::cout << "Read successful: '" << std::string(buffer, size) << "'\n";
        } else {
            std::cout << "Read failed - invalid address\n";
        }
        std::cout << "\n";
    }
    
    void print_status() {
        mmu.print_page_table();
    }
    
    // Simulate creating a file on disk
    void create_file(const std::string& filename, const std::string& content) {
        disk.write_file(filename, content.c_str(), content.size());
    }
};

int main() {
    VirtualMemorySystem vm_system;
    
    // Create a file on disk
    vm_system.create_file("test.txt", "Hello, this is file content for mmap testing!");
    
    std::cout << "\n=== Testing Anonymous mmap ===\n";
    // Test anonymous mapping
    void* anon_mem = vm_system.mmap(nullptr, 8192, 0, 0, -1, 0);
    vm_system.print_status();
    
    // Write to anonymous memory
    vm_system.write_memory(anon_mem, "Hello World!", 12);
    vm_system.print_status();
    
    // Read back
    char buffer[32] = {0};
    vm_system.read_memory(anon_mem, buffer, 12);
    
    std::cout << "\n=== Testing File-backed mmap ===\n";
    // Test file-backed mapping
    void* file_mem = vm_system.mmap(nullptr, 4096, 0, 0, 1, 0);  // fd=1 indicates file
    vm_system.print_status();
    
    // Read from file-backed memory (triggers page fault and disk read)
    char file_buffer[64] = {0};
    vm_system.read_memory(file_mem, file_buffer, 50);
    
    std::cout << "\n=== Testing munmap ===\n";
    vm_system.munmap(anon_mem, 8192);
    vm_system.munmap(file_mem, 4096);
    vm_system.print_status();
    
    return 0;
}