#include <iostream>
#include <vector>
#include <map>
#include <iomanip>
#include <memory>
#include <cassert>

// Page size and related constants
const uint32_t PAGE_SIZE = 4096;  // 4KB pages
const uint32_t PAGE_SHIFT = 12;   // log2(4096) = 12
const uint32_t PAGE_MASK = 0xFFF; // 12 bits for offset

// Page table constants (10 bits each for 32-bit addresses)
const uint32_t PTE_ENTRIES = 1024;  // 2^10 = 1024 entries per table
const uint32_t PDE_ENTRIES = 1024;  // 2^10 = 1024 entries per directory

// Flags for page table entries
const uint32_t PTE_PRESENT = 0x001;  // Page is present in memory
const uint32_t PTE_WRITE   = 0x002;  // Page is writable
const uint32_t PTE_USER    = 0x004;  // User accessible

// Extract indices from virtual address
#define PDX(va) (((va) >> 22) & 0x3FF)  // Directory index (bits 31-22)
#define PTX(va) (((va) >> 12) & 0x3FF)  // Table index (bits 21-12)
#define PG_OFFSET(va) ((va) & 0xFFF)    // Page offset (bits 11-0)

// Extract physical address from page table entry
#define PTE_ADDR(pte) ((pte) & ~0xFFF)

class PhysicalMemory {
private:
    std::map<uint32_t, std::vector<uint8_t>> pages;
    uint32_t next_free_page;
    
public:
    PhysicalMemory() : next_free_page(0x100000) {} // Start at 1MB
    
    // Allocate a new physical page
    uint32_t allocate_page() {
        uint32_t page_addr = next_free_page;
        pages[page_addr] = std::vector<uint8_t>(PAGE_SIZE, 0);
        next_free_page += PAGE_SIZE;
        std::cout << "  [PHYS] Allocated physical page at 0x" 
                  << std::hex << page_addr << std::dec << std::endl;
        return page_addr;
    }
    
    // Read from physical memory
    uint8_t read_byte(uint32_t phys_addr) {
        uint32_t page_addr = phys_addr & ~PAGE_MASK;
        uint32_t offset = phys_addr & PAGE_MASK;
        
        if (pages.find(page_addr) == pages.end()) {
            std::cout << "  [PHYS] ERROR: Access to unmapped physical page 0x" 
                      << std::hex << page_addr << std::dec << std::endl;
            return 0;
        }
        
        return pages[page_addr][offset];
    }
    
    // Write to physical memory
    void write_byte(uint32_t phys_addr, uint8_t value) {
        uint32_t page_addr = phys_addr & ~PAGE_MASK;
        uint32_t offset = phys_addr & PAGE_MASK;
        
        if (pages.find(page_addr) == pages.end()) {
            std::cout << "  [PHYS] ERROR: Write to unmapped physical page 0x" 
                      << std::hex << page_addr << std::dec << std::endl;
            return;
        }
        
        pages[page_addr][offset] = value;
    }
    
    // Write a 32-bit value (for page table entries)
    void write_uint32(uint32_t phys_addr, uint32_t value) {
        for (int i = 0; i < 4; i++) {
            write_byte(phys_addr + i, (value >> (i * 8)) & 0xFF);
        }
    }
    
    // Read a 32-bit value (for page table entries)
    uint32_t read_uint32(uint32_t phys_addr) {
        uint32_t value = 0;
        for (int i = 0; i < 4; i++) {
            value |= (read_byte(phys_addr + i) << (i * 8));
        }
        return value;
    }
    
    void print_stats() {
        std::cout << "\n=== Physical Memory Stats ===" << std::endl;
        std::cout << "Total allocated pages: " << pages.size() << std::endl;
        std::cout << "Memory used: " << pages.size() * PAGE_SIZE / 1024 << " KB" << std::endl;
    }
};

class PageTableManager {
private:
    PhysicalMemory& phys_mem;
    uint32_t page_directory_phys;  // Physical address of page directory
    std::map<uint32_t, uint32_t> allocated_page_tables; // Track allocated page tables
    
public:
    PageTableManager(PhysicalMemory& pm) : phys_mem(pm) {
        // Allocate page directory in "kernel memory"
        page_directory_phys = phys_mem.allocate_page();
        std::cout << "[PGT] Created page directory at KERNEL physical 0x" 
                  << std::hex << page_directory_phys << std::dec << std::endl;
        std::cout << "[PGT] This page directory is stored in KERNEL memory space" << std::endl;
    }
    
    // Get page directory physical address (like reading CR3)
    uint32_t get_page_directory() const {
        return page_directory_phys;
    }
    
    // Map a virtual page to a physical page (with growth simulation)
    bool map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
        uint32_t dir_index = PDX(virtual_addr);
        uint32_t table_index = PTX(virtual_addr);
        
        std::cout << "\n[PGT] Mapping virtual 0x" << std::hex << virtual_addr 
                  << " to physical 0x" << physical_addr << std::dec << std::endl;
        std::cout << "      Directory index: " << dir_index 
                  << ", Table index: " << table_index << std::endl;
        
        // Get page directory entry
        uint32_t pde_addr = page_directory_phys + dir_index * 4;
        uint32_t pde = phys_mem.read_uint32(pde_addr);
        
        uint32_t page_table_phys;
        
        if (!(pde & PTE_PRESENT)) {
            // PAGE TABLE GROWTH: Allocate new page table in KERNEL memory
            page_table_phys = phys_mem.allocate_page();
            allocated_page_tables[dir_index] = page_table_phys;
            
            std::cout << "  [PGT] *** GROWTH *** Created new page table " << allocated_page_tables.size() 
                      << " at KERNEL physical 0x" << std::hex << page_table_phys << std::dec << std::endl;
            std::cout << "  [PGT] This covers virtual address range 0x" << std::hex 
                      << (dir_index << 22) << " - 0x" << ((dir_index + 1) << 22) - 1 << std::dec << std::endl;
            
            // Update page directory entry (stored in kernel memory)
            pde = page_table_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
            phys_mem.write_uint32(pde_addr, pde);
        } else {
            // Page table exists
            page_table_phys = PTE_ADDR(pde);
            std::cout << "  [PGT] Using existing page table at KERNEL physical 0x" 
                      << std::hex << page_table_phys << std::dec << std::endl;
        }
        
        // Set page table entry (also in kernel memory)
        uint32_t pte_addr = page_table_phys + table_index * 4;
        uint32_t pte = physical_addr | flags | PTE_PRESENT;
        phys_mem.write_uint32(pte_addr, pte);
        
        std::cout << "  [PGT] Set PTE at KERNEL physical 0x" << std::hex << pte_addr 
                  << " = 0x" << pte << std::dec << std::endl;
        
        return true;
    }
    
    // Translate virtual address to physical address (MMU simulation)
    uint32_t translate_address(uint32_t virtual_addr) {
        uint32_t dir_index = PDX(virtual_addr);
        uint32_t table_index = PTX(virtual_addr);
        uint32_t offset = PG_OFFSET(virtual_addr);
        
        std::cout << "\n[MMU] Translating virtual address 0x" << std::hex << virtual_addr << std::dec << std::endl;
        std::cout << "      Dir[" << dir_index << "] Table[" << table_index << "] Offset[" << offset << "]" << std::endl;
        
        // Step 1: Read page directory entry
        uint32_t pde_addr = page_directory_phys + dir_index * 4;
        uint32_t pde = phys_mem.read_uint32(pde_addr);
        
        std::cout << "  [MMU] PDE at 0x" << std::hex << pde_addr << " = 0x" << pde << std::dec << std::endl;
        
        if (!(pde & PTE_PRESENT)) {
            std::cout << "  [MMU] PAGE FAULT: Page table not present!" << std::endl;
            return 0xFFFFFFFF; // Invalid address
        }
        
        // Step 2: Read page table entry
        uint32_t page_table_phys = PTE_ADDR(pde);
        uint32_t pte_addr = page_table_phys + table_index * 4;
        uint32_t pte = phys_mem.read_uint32(pte_addr);
        
        std::cout << "  [MMU] PTE at 0x" << std::hex << pte_addr << " = 0x" << pte << std::dec << std::endl;
        
        if (!(pte & PTE_PRESENT)) {
            std::cout << "  [MMU] PAGE FAULT: Page not present!" << std::endl;
            return 0xFFFFFFFF; // Invalid address
        }
        
        // Step 3: Calculate final physical address
        uint32_t page_phys = PTE_ADDR(pte);
        uint32_t phys_addr = page_phys + offset;
        
        std::cout << "  [MMU] Physical address: 0x" << std::hex << phys_addr << std::dec << std::endl;
        
        return phys_addr;
    }
    
    void print_page_directory_array() {
        std::cout << "\n=== Page Directory Array Structure ===" << std::endl;
        std::cout << "Page Directory at KERNEL physical 0x" << std::hex << page_directory_phys << std::dec << std::endl;
        std::cout << "Size: 1024 entries × 4 bytes = 4096 bytes (4KB)" << std::endl;
        std::cout << "Each entry covers 4MB of virtual address space" << std::endl;
        
        std::cout << "\nArray contents (showing non-zero entries only):" << std::endl;
        for (uint32_t i = 0; i < PDE_ENTRIES; i++) {
            uint32_t pde_addr = page_directory_phys + i * 4;
            uint32_t pde = phys_mem.read_uint32(pde_addr);
            
            if (pde != 0) {
                uint32_t va_start = i << 22;                // Start of 4MB region
                uint32_t va_end = ((i + 1) << 22) - 1;      // End of 4MB region
                uint32_t page_table_phys = pde & 0xFFFFF000; // Extract page table address
                
                std::cout << "  Array[" << std::setw(3) << i << "] = 0x" << std::hex << pde << std::dec;
                std::cout << " → Page Table at 0x" << std::hex << page_table_phys << std::dec;
                std::cout << " (covers VA 0x" << std::hex << va_start << "-0x" << va_end << std::dec << ")" << std::endl;
                
                // Show how much of this 4MB region is actually used
                uint32_t used_pages = 0;
                for (uint32_t j = 0; j < PTE_ENTRIES; j++) {
                    uint32_t pte_addr = page_table_phys + j * 4;
                    uint32_t pte = phys_mem.read_uint32(pte_addr);
                    if (pte & PTE_PRESENT) {
                        used_pages++;
                    }
                }
                std::cout << "    This 4MB region uses " << used_pages << "/1024 pages (";
                std::cout << std::fixed << std::setprecision(1) 
                          << (used_pages * 100.0 / 1024) << "% utilized)" << std::endl;
            }
        }
        
        std::cout << "\nUnused entries (empty slots in array): ";
        int unused_count = 0;
        for (uint32_t i = 0; i < PDE_ENTRIES; i++) {
            uint32_t pde_addr = page_directory_phys + i * 4;
            uint32_t pde = phys_mem.read_uint32(pde_addr);
            if (pde == 0) unused_count++;
        }
        std::cout << unused_count << "/1024 entries" << std::endl;
        std::cout << "Unused virtual space: " << unused_count << " × 4MB = " 
                  << (unused_count * 4) << "MB" << std::endl;
    }
};

// Simulation of multiple processes with separate page tables
class ProcessManager {
private:
    PhysicalMemory& phys_mem;
    std::map<int, std::unique_ptr<PageTableManager>> processes;  // PID -> PageTableManager
    int current_pid;
    
public:
    ProcessManager(PhysicalMemory& pm) : phys_mem(pm), current_pid(-1) {}
    
    // Create a new process (like fork())
    int create_process(int pid) {
        std::cout << "\n[PROC_MGR] Creating process " << pid << " (like fork())" << std::endl;
        processes[pid] = std::make_unique<PageTableManager>(phys_mem);
        std::cout << "[PROC_MGR] Process " << pid << " has its own page directory at 0x" 
                  << std::hex << processes[pid]->get_page_directory() << std::dec << std::endl;
        return pid;
    }
    
    // Context switch to different process
    void switch_to_process(int pid) {
        if (processes.find(pid) == processes.end()) {
            std::cout << "[PROC_MGR] ERROR: Process " << pid << " doesn't exist!" << std::endl;
            return;
        }
        
        std::cout << "\n[PROC_MGR] *** CONTEXT SWITCH *** from PID " << current_pid 
                  << " to PID " << pid << std::endl;
        
        if (current_pid != -1) {
            std::cout << "[PROC_MGR] Saving CR3 = 0x" << std::hex 
                      << processes[current_pid]->get_page_directory() << std::dec << std::endl;
        }
        
        current_pid = pid;
        uint32_t new_pgd = processes[pid]->get_page_directory();
        
        std::cout << "[PROC_MGR] Loading CR3 = 0x" << std::hex << new_pgd << std::dec 
                  << " (switch to process " << pid << "'s page tables)" << std::endl;
        std::cout << "[PROC_MGR] MMU now uses process " << pid << "'s virtual address mappings" << std::endl;
    }
    
    PageTableManager* get_current_process() {
        if (current_pid == -1 || processes.find(current_pid) == processes.end()) {
            return nullptr;
        }
        return processes[current_pid].get();
    }
    
    int get_current_pid() const { return current_pid; }
    
    void print_all_processes() {
        std::cout << "\n=== All Process Memory Spaces ===" << std::endl;
        for (const auto& [pid, page_mgr] : processes) {
            std::cout << "\nProcess " << pid << " (Page Directory at 0x" 
                      << std::hex << page_mgr->get_page_directory() << std::dec << "):" << std::endl;
            // Print abbreviated page table info
            uint32_t pgd_addr = page_mgr->get_page_directory();
            int page_table_count = 0;
            
            for (uint32_t i = 0; i < PDE_ENTRIES; i++) {
                uint32_t pde_addr = pgd_addr + i * 4;
                uint32_t pde = phys_mem.read_uint32(pde_addr);
                if (pde & PTE_PRESENT) {
                    page_table_count++;
                }
            }
            std::cout << "  Active page tables: " << page_table_count << std::endl;
        }
    }
};

// Modified Process class to work with ProcessManager  
class MultiProcess {
private:
    ProcessManager& proc_mgr;
    PhysicalMemory& phys_mem;
    int pid;
    
public:
    MultiProcess(ProcessManager& pm, PhysicalMemory& mem, int process_id) 
        : proc_mgr(pm), phys_mem(mem), pid(process_id) {}
    
    // Read from virtual memory (uses current process's page tables)
    uint8_t read_virtual(uint32_t virtual_addr) {
        PageTableManager* current = proc_mgr.get_current_process();
        if (!current) {
            std::cout << "[PROC" << pid << "] ERROR: No current process!" << std::endl;
            return 0;
        }
        
        uint32_t phys_addr = current->translate_address(virtual_addr);
        if (phys_addr == 0xFFFFFFFF) {
            std::cout << "[PROC" << pid << "] Segmentation fault at virtual 0x" 
                      << std::hex << virtual_addr << std::dec << std::endl;
            return 0;
        }
        
        uint8_t value = phys_mem.read_byte(phys_addr);
        std::cout << "[PROC" << pid << "] Read 0x" << std::hex << (int)value 
                  << " from virtual 0x" << virtual_addr << std::dec << std::endl;
        return value;
    }
    
    // Write to virtual memory (uses current process's page tables)
    void write_virtual(uint32_t virtual_addr, uint8_t value) {
        PageTableManager* current = proc_mgr.get_current_process();
        if (!current) {
            std::cout << "[PROC" << pid << "] ERROR: No current process!" << std::endl;
            return;
        }
        
        uint32_t phys_addr = current->translate_address(virtual_addr);
        if (phys_addr == 0xFFFFFFFF) {
            std::cout << "[PROC" << pid << "] Segmentation fault at virtual 0x" 
                      << std::hex << virtual_addr << std::dec << std::endl;
            return;
        }
        
        phys_mem.write_byte(phys_addr, value);
        std::cout << "[PROC" << pid << "] Wrote 0x" << std::hex << (int)value 
                  << " to virtual 0x" << virtual_addr << std::dec << std::endl;
    }
    
    // Map memory in this process's address space
    void map_memory(uint32_t virtual_addr, uint32_t flags) {
        PageTableManager* current = proc_mgr.get_current_process();
        if (!current) {
            std::cout << "[PROC" << pid << "] ERROR: No current process!" << std::endl;
            return;
        }
        
        uint32_t physical_page = phys_mem.allocate_page();
        current->map_page(virtual_addr, physical_page, flags);
        std::cout << "[PROC" << pid << "] Mapped virtual 0x" << std::hex << virtual_addr 
                  << " in its own address space" << std::dec << std::endl;
    }
};

int main() {
    std::cout << "=== Multi-Level Page Table Simulation ===" << std::endl;
    std::cout << "Page size: " << PAGE_SIZE << " bytes" << std::endl;
    std::cout << "Entries per table: " << PTE_ENTRIES << std::endl;
    
    // Create physical memory and page table manager
    PhysicalMemory phys_mem;
    PageTableManager page_mgr(phys_mem);
    Process process(page_mgr, phys_mem);
    
    // Allocate some physical pages for our process
    uint32_t code_page = phys_mem.allocate_page();
    uint32_t data_page = phys_mem.allocate_page(); 
    uint32_t heap_page = phys_mem.allocate_page();
    uint32_t stack_page = phys_mem.allocate_page();
    
    std::cout << "\n=== Setting Up Process Memory Layout ===" << std::endl;
    
    // Map virtual pages to physical pages (typical process layout)
    // Code segment at 0x08048000
    page_mgr.map_page(0x08048000, code_page, PTE_USER);
    
    // Data segment at 0x08049000  
    page_mgr.map_page(0x08049000, data_page, PTE_USER | PTE_WRITE);
    
    // Heap at 0x10000000
    page_mgr.map_page(0x10000000, heap_page, PTE_USER | PTE_WRITE);
    
    // Stack at 0xBFFFF000 (high memory)
    page_mgr.map_page(0xBFFFF000, stack_page, PTE_USER | PTE_WRITE);
    
    // Show how page tables have grown
    page_mgr.print_page_tables();
    
    std::cout << "\n--- Testing Access to Grown Memory ---" << std::endl;
    process.write_virtual(0x10001000, 0xAA);  // Second heap page
    process.write_virtual(0x40000000, 0xBB);  // mmap region
    process.read_virtual(0x10001000);
    process.read_virtual(0x40000000);
    
    std::cout << "\n=== Testing Memory Access ===" << std::endl;
    
    // Test writing and reading from different segments
    std::cout << "\n--- Testing Code Segment (0x08048000) ---" << std::endl;
    process.write_virtual(0x08048000, 0x90);  // NOP instruction
    process.read_virtual(0x08048000);
    
    std::cout << "\n--- Testing Data Segment (0x08049000) ---" << std::endl;
    process.write_virtual(0x08049000, 0x42);
    process.read_virtual(0x08049000);
    
    std::cout << "\n--- Testing Heap (0x10000000) ---" << std::endl;
    process.write_virtual(0x10000000, 0xAB);
    process.read_virtual(0x10000000);
    
    std::cout << "\n--- Testing Stack (0xBFFFF000) ---" << std::endl;
    process.write_virtual(0xBFFFF000, 0xCD);
    process.read_virtual(0xBFFFF000);
    
    std::cout << "\n--- Testing Unmapped Memory (0x20000000) ---" << std::endl;
    process.read_virtual(0x20000000);  // Should cause page fault
    
    std::cout << "\n=== Simulating Process Growth ===" << std::endl;
    
    // Simulate heap growth (malloc calls)
    std::cout << "\n--- Heap Growth Simulation ---" << std::endl;
    for (int i = 1; i <= 3; i++) {
        uint32_t heap_addr = 0x10000000 + (i * PAGE_SIZE);
        uint32_t new_page = phys_mem.allocate_page();
        page_mgr.map_page(heap_addr, new_page, PTE_USER | PTE_WRITE);
        std::cout << "[MALLOC] Allocated heap page " << i << std::endl;
    }
    
    // Simulate stack growth (function calls with large local variables)
    std::cout << "\n--- Stack Growth Simulation ---" << std::endl;
    for (int i = 1; i <= 2; i++) {
        uint32_t stack_addr = 0xBFFFF000 - (i * PAGE_SIZE);
        uint32_t new_page = phys_mem.allocate_page();
        page_mgr.map_page(stack_addr, new_page, PTE_USER | PTE_WRITE);
        std::cout << "[STACK] Stack grew down to page " << i << std::endl;
    }
    
    // Simulate memory mapping (mmap call)
    std::cout << "\n--- Memory Mapping Simulation (mmap) ---" << std::endl;
    uint32_t mmap_region = 0x40000000;  // New memory region
    for (int i = 0; i < 2; i++) {
        uint32_t mmap_addr = mmap_region + (i * PAGE_SIZE);
        uint32_t new_page = phys_mem.allocate_page();
        page_mgr.map_page(mmap_addr, new_page, PTE_USER | PTE_WRITE);
        std::cout << "[MMAP] Mapped page " << i << " in new region" << std::endl;
    }
    
    // Show memory usage statistics
    phys_mem.print_stats();
    
    std::cout << "\n=== Memory Layout Summary ===" << std::endl;
    std::cout << "Virtual Address Space: 4GB (0x00000000 - 0xFFFFFFFF)" << std::endl;
    std::cout << "Actually mapped: 4 pages = 16KB" << std::endl;
    std::cout << "Page table overhead: ~12KB (much less than 4MB flat table!)" << std::endl;
    std::cout << "Sparsity ratio: 99.999%" << std::endl;
    
    return 0;
}
