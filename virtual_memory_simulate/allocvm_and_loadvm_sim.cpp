#include <iostream>
#include <vector>
#include <map>
#include <iomanip>
#include <memory>
#include <cstring>

// Constants
const uint32_t PAGE_SIZE = 4096;
const uint32_t PAGE_SHIFT = 12;
const uint32_t PAGE_MASK = 0xFFF;

// Page table constants
const uint32_t PTE_ENTRIES = 1024;
const uint32_t PDE_ENTRIES = 1024;

// Page table entry flags
const uint32_t PTE_PRESENT = 0x001;
const uint32_t PTE_WRITE   = 0x002;
const uint32_t PTE_USER    = 0x004;

// Extract indices and addresses
#define PDX(va) (((va) >> 22) & 0x3FF)
#define PTX(va) (((va) >> 12) & 0x3FF)
#define PG_OFFSET(va) ((va) & 0xFFF)
#define PTE_ADDR(pte) ((pte) & ~0xFFF)
#define PGROUNDUP(sz) (((sz) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PGROUNDDOWN(sz) ((sz) & ~(PAGE_SIZE - 1))

// Simulate physical to virtual address conversion (kernel addressing)
#define KERNBASE 0x80000000
#define V2P(a) ((uint32_t)(a) - KERNBASE)
#define P2V(a) ((void*)((uint32_t)(a) + KERNBASE))

// ELF structures
struct elfhdr {
    uint32_t magic;
    uint32_t entry;       // Entry point (where program starts)
    uint32_t phoff;       // Program header offset
    uint32_t phnum;       // Number of program headers
};

struct proghdr {
    uint32_t type;        // Segment type
    uint32_t off;         // File offset
    uint32_t vaddr;       // Virtual address
    uint32_t paddr;       // Physical address (usually ignored)
    uint32_t filesz;      // Size in file
    uint32_t memsz;       // Size in memory
    uint32_t flags;       // Permissions
    uint32_t align;       // Alignment
};

#define ELF_MAGIC 0x464C457F
#define ELF_PROG_LOAD 1

// ==================== DISK SIMULATION ====================
class Disk {
private:
    std::map<std::string, std::vector<uint8_t>> files;
    
public:
    // Create a file on disk
    void create_file(const std::string& filename, const std::vector<uint8_t>& data) {
        files[filename] = data;
        std::cout << "[DISK] Created file '" << filename << "' with " 
                  << data.size() << " bytes" << std::endl;
    }
    
    // Read from file at offset
    int read_file(const std::string& filename, uint8_t* buffer, 
                  uint32_t offset, uint32_t size) {
        if (files.find(filename) == files.end()) {
            std::cout << "[DISK] ERROR: File '" << filename << "' not found" << std::endl;
            return -1;
        }
        
        const auto& file = files[filename];
        if (offset + size > file.size()) {
            std::cout << "[DISK] ERROR: Read beyond file size" << std::endl;
            return -1;
        }
        
        std::memcpy(buffer, file.data() + offset, size);
        std::cout << "[DISK] Read " << size << " bytes from '" << filename 
                  << "' at offset " << offset << std::endl;
        return size;
    }
    
    bool file_exists(const std::string& filename) {
        return files.find(filename) != files.end();
    }
    
    size_t file_size(const std::string& filename) {
        if (files.find(filename) == files.end()) return 0;
        return files[filename].size();
    }
};

// ==================== RAM SIMULATION ====================
class PhysicalMemory {
private:
    std::map<uint32_t, std::vector<uint8_t>> pages;
    uint32_t next_free_page;
    
public:
    PhysicalMemory() : next_free_page(0x100000) {}
    
    // Allocate a physical page (like kalloc())
    uint32_t kalloc() {
        uint32_t page_addr = next_free_page;
        pages[page_addr] = std::vector<uint8_t>(PAGE_SIZE, 0);
        next_free_page += PAGE_SIZE;
        std::cout << "  [RAM] kalloc() allocated physical page at 0x" 
                  << std::hex << page_addr << std::dec << std::endl;
        return page_addr;
    }
    
    // Free a physical page
    void kfree(uint32_t page_addr) {
        if (pages.find(page_addr) != pages.end()) {
            pages.erase(page_addr);
            std::cout << "  [RAM] kfree() freed physical page at 0x" 
                      << std::hex << page_addr << std::dec << std::endl;
        }
    }
    
    // Read byte from physical address
    uint8_t read_byte(uint32_t phys_addr) {
        uint32_t page_addr = phys_addr & ~PAGE_MASK;
        uint32_t offset = phys_addr & PAGE_MASK;
        
        if (pages.find(page_addr) == pages.end()) {
            std::cout << "  [RAM] ERROR: Read from unmapped physical page 0x" 
                      << std::hex << page_addr << std::dec << std::endl;
            return 0;
        }
        return pages[page_addr][offset];
    }
    
    // Write byte to physical address
    void write_byte(uint32_t phys_addr, uint8_t value) {
        uint32_t page_addr = phys_addr & ~PAGE_MASK;
        uint32_t offset = phys_addr & PAGE_MASK;
        
        if (pages.find(page_addr) == pages.end()) {
            std::cout << "  [RAM] ERROR: Write to unmapped physical page 0x" 
                      << std::hex << page_addr << std::dec << std::endl;
            return;
        }
        pages[page_addr][offset] = value;
    }
    
    // Write block of data to physical address
    void write_block(uint32_t phys_addr, const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; i++) {
            write_byte(phys_addr + i, data[i]);
        }
    }
    
    // Read block of data from physical address
    void read_block(uint32_t phys_addr, uint8_t* buffer, size_t size) {
        for (size_t i = 0; i < size; i++) {
            buffer[i] = read_byte(phys_addr + i);
        }
    }
    
    // Write uint32 (for page table entries)
    void write_uint32(uint32_t phys_addr, uint32_t value) {
        for (int i = 0; i < 4; i++) {
            write_byte(phys_addr + i, (value >> (i * 8)) & 0xFF);
        }
    }
    
    // Read uint32 (for page table entries)
    uint32_t read_uint32(uint32_t phys_addr) {
        uint32_t value = 0;
        for (int i = 0; i < 4; i++) {
            value |= (read_byte(phys_addr + i) << (i * 8));
        }
        return value;
    }
    
    void print_stats() {
        std::cout << "\n=== Physical RAM Stats ===" << std::endl;
        std::cout << "Allocated pages: " << pages.size() << std::endl;
        std::cout << "Memory used: " << pages.size() * PAGE_SIZE / 1024 << " KB" << std::endl;
    }
    
    // Print page contents (for debugging)
    void print_page_contents(uint32_t phys_addr, size_t bytes = 64) {
        uint32_t page_addr = phys_addr & ~PAGE_MASK;
        if (pages.find(page_addr) == pages.end()) {
            std::cout << "Page not allocated" << std::endl;
            return;
        }
        
        std::cout << "Physical page 0x" << std::hex << page_addr << " contents:" << std::dec << std::endl;
        for (size_t i = 0; i < bytes && i < PAGE_SIZE; i++) {
            if (i % 16 == 0) std::cout << "  ";
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << (int)pages[page_addr][i] << " ";
            if (i % 16 == 15) std::cout << std::dec << std::endl;
        }
        std::cout << std::dec << std::endl;
    }
};

// ==================== PAGE TABLE MANAGER ====================
class PageTableManager {
private:
    PhysicalMemory& phys_mem;
    uint32_t page_directory_phys;
    
public:
    PageTableManager(PhysicalMemory& pm) : phys_mem(pm) {
        page_directory_phys = phys_mem.kalloc();
        std::cout << "[PGT] Created page directory at physical 0x" 
                  << std::hex << page_directory_phys << std::dec << std::endl;
    }
    
    uint32_t get_page_directory() const {
        return page_directory_phys;
    }
    
    // Walk page directory to find/create page table entry
    uint32_t* walkpgdir(uint32_t virtual_addr, bool alloc) {
        uint32_t dir_index = PDX(virtual_addr);
        uint32_t table_index = PTX(virtual_addr);
        
        // Get page directory entry
        uint32_t pde_addr = page_directory_phys + dir_index * 4;
        uint32_t pde = phys_mem.read_uint32(pde_addr);
        
        uint32_t page_table_phys;
        
        if (!(pde & PTE_PRESENT)) {
            if (!alloc) {
                return nullptr;
            }
            
            // Allocate new page table
            page_table_phys = phys_mem.kalloc();
            std::cout << "    [PGT] walkpgdir: Created page table at 0x" 
                      << std::hex << page_table_phys << std::dec << std::endl;
            
            // Update page directory entry
            pde = page_table_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
            phys_mem.write_uint32(pde_addr, pde);
        } else {
            page_table_phys = PTE_ADDR(pde);
        }
        
        // Return address of page table entry
        uint32_t pte_addr = page_table_phys + table_index * 4;
        static uint32_t pte_storage;
        pte_storage = phys_mem.read_uint32(pte_addr);
        return &pte_storage;
    }
    
    // Map pages (used by allocuvm)
    int mappages(uint32_t va, uint32_t size, uint32_t pa, int perm) {
        uint32_t a = PGROUNDDOWN(va);
        uint32_t last = PGROUNDDOWN(va + size - 1);
        
        for (;;) {
            uint32_t dir_index = PDX(a);
            uint32_t table_index = PTX(a);
            
            // Get/create page table entry
            uint32_t pde_addr = page_directory_phys + dir_index * 4;
            uint32_t pde = phys_mem.read_uint32(pde_addr);
            
            uint32_t page_table_phys;
            if (!(pde & PTE_PRESENT)) {
                page_table_phys = phys_mem.kalloc();
                pde = page_table_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
                phys_mem.write_uint32(pde_addr, pde);
            } else {
                page_table_phys = PTE_ADDR(pde);
            }
            
            // Set page table entry
            uint32_t pte_addr = page_table_phys + table_index * 4;
            uint32_t pte = phys_mem.read_uint32(pte_addr);
            
            if (pte & PTE_PRESENT) {
                std::cout << "    [PGT] ERROR: Remap attempted" << std::endl;
                return -1;
            }
            
            pte = pa | perm | PTE_PRESENT;
            phys_mem.write_uint32(pte_addr, pte);
            
            std::cout << "    [PGT] mappages: Virtual 0x" << std::hex << a 
                      << " → Physical 0x" << pa << std::dec << std::endl;
            
            if (a == last)
                break;
                
            a += PAGE_SIZE;
            pa += PAGE_SIZE;
        }
        return 0;
    }
    
    // Allocate virtual memory (like allocuvm)
    uint32_t allocuvm(uint32_t oldsz, uint32_t newsz) {
        if (newsz < oldsz)
            return oldsz;
            
        std::cout << "\n[ALLOCUVM] Allocating virtual memory from 0x" << std::hex 
                  << oldsz << " to 0x" << newsz << std::dec << std::endl;
        
        uint32_t a = PGROUNDUP(oldsz);
        for (; a < newsz; a += PAGE_SIZE) {
            // Allocate physical page
            uint32_t mem = phys_mem.kalloc();
            if (mem == 0) {
                std::cout << "  [ALLOCUVM] Out of memory!" << std::endl;
                return 0;
            }
            
            // Map virtual to physical
            if (mappages(a, PAGE_SIZE, V2P(mem), PTE_WRITE | PTE_USER) < 0) {
                phys_mem.kfree(mem);
                return 0;
            }
        }
        
        std::cout << "[ALLOCUVM] Completed. New size: 0x" << std::hex 
                  << newsz << std::dec << std::endl;
        return newsz;
    }
    
    // Load data from disk into virtual memory (like loaduvm)
    int loaduvm(uint32_t va, Disk& disk, const std::string& filename, 
                uint32_t offset, uint32_t sz) {
        std::cout << "\n[LOADUVM] Loading " << sz << " bytes from disk to virtual 0x" 
                  << std::hex << va << std::dec << std::endl;
        std::cout << "[LOADUVM] Reading from file '" << filename 
                  << "' at offset " << offset << std::endl;
        
        uint32_t i, pa, n;
        uint8_t buffer[PAGE_SIZE];
        
        for (i = 0; i < sz; i += PAGE_SIZE) {
            // Find page table entry for this virtual address
            uint32_t dir_index = PDX(va + i);
            uint32_t table_index = PTX(va + i);
            
            uint32_t pde_addr = page_directory_phys + dir_index * 4;
            uint32_t pde = phys_mem.read_uint32(pde_addr);
            
            if (!(pde & PTE_PRESENT)) {
                std::cout << "  [LOADUVM] ERROR: Page table doesn't exist for VA 0x" 
                          << std::hex << (va + i) << std::dec << std::endl;
                return -1;
            }
            
            uint32_t page_table_phys = PTE_ADDR(pde);
            uint32_t pte_addr = page_table_phys + table_index * 4;
            uint32_t pte = phys_mem.read_uint32(pte_addr);
            
            if (!(pte & PTE_PRESENT)) {
                std::cout << "  [LOADUVM] ERROR: Page not present for VA 0x" 
                          << std::hex << (va + i) << std::dec << std::endl;
                return -1;
            }
            
            // Get physical address from PTE
            pa = PTE_ADDR(pte);
            std::cout << "  [LOADUVM] Virtual 0x" << std::hex << (va + i) 
                      << " → Physical 0x" << pa << std::dec << std::endl;
            
            // Read from disk
            n = (sz - i < PAGE_SIZE) ? (sz - i) : PAGE_SIZE;
            if (disk.read_file(filename, buffer, offset + i, n) != (int)n) {
                std::cout << "  [LOADUVM] ERROR: Failed to read from disk" << std::endl;
                return -1;
            }
            
            // Write to physical memory
            phys_mem.write_block(pa, buffer, n);
            std::cout << "  [LOADUVM] Copied " << n << " bytes to physical 0x" 
                      << std::hex << pa << std::dec << std::endl;
        }
        
        std::cout << "[LOADUVM] Completed successfully" << std::endl;
        return 0;
    }
};

// ==================== MAIN SIMULATION ====================
int main() {
    std::cout << "=== Program Loading Simulation: Disk → RAM ===" << std::endl;
    std::cout << "Page size: " << PAGE_SIZE << " bytes\n" << std::endl;
    
    // Create disk and RAM
    Disk disk;
    PhysicalMemory ram;
    PageTableManager page_mgr(ram);
    
    // ========== CREATE PROGRAM FILE ON DISK ==========
    std::cout << "=== Step 1: Create Program File on Disk ===" << std::endl;
    
    // Create a simple ELF file structure
    std::vector<uint8_t> program_file;
    
    // ELF header
    elfhdr elf;
    elf.magic = ELF_MAGIC;
    elf.entry = 0x08048000;
    elf.phoff = sizeof(elfhdr);
    elf.phnum = 2;
    
    program_file.insert(program_file.end(), (uint8_t*)&elf, (uint8_t*)&elf + sizeof(elf));
    
    // Program header 1: Code segment
    proghdr ph1;
    ph1.type = ELF_PROG_LOAD;
    ph1.off = 0x1000;           // Code data starts at offset 0x1000 in file
    ph1.vaddr = 0x08048000;     // Load at virtual address 0x08048000
    ph1.filesz = 256;           // 256 bytes of code in file
    ph1.memsz = 256;            // 256 bytes in memory
    ph1.flags = 0x5;            // Read + Execute
    
    program_file.insert(program_file.end(), (uint8_t*)&ph1, (uint8_t*)&ph1 + sizeof(ph1));
    
    // Program header 2: Data segment
    proghdr ph2;
    ph2.type = ELF_PROG_LOAD;
    ph2.off = 0x1100;           // Data starts at offset 0x1100 in file
    ph2.vaddr = 0x08049000;     // Load at virtual address 0x08049000
    ph2.filesz = 128;           // 128 bytes of initialized data
    ph2.memsz = 512;            // 512 bytes in memory (includes BSS)
    ph2.flags = 0x6;            // Read + Write
    
    program_file.insert(program_file.end(), (uint8_t*)&ph2, (uint8_t*)&ph2 + sizeof(ph2));
    
    // Pad to offset 0x1000
    while (program_file.size() < 0x1000) {
        program_file.push_back(0);
    }
    
    // Add code segment data (simulated machine code)
    for (int i = 0; i < 256; i++) {
        program_file.push_back(0x90 + (i % 16));  // Simulated instructions
    }
    
    // Add data segment data (simulated initialized data)
    for (int i = 0; i < 128; i++) {
        program_file.push_back(0x40 + (i % 32));  // Simulated data
    }
    
    disk.create_file("/bin/myprogram", program_file);
    
    // ========== LOAD PROGRAM (SIMULATING EXEC) ==========
    std::cout << "\n=== Step 2: Load Program (exec system call) ===" << std::endl;
    
    // Read ELF header
    uint8_t header_buf[sizeof(elfhdr)];
    disk.read_file("/bin/myprogram", header_buf, 0, sizeof(elfhdr));
    elfhdr* elf_ptr = (elfhdr*)header_buf;
    
    std::cout << "\n[EXEC] Read ELF header:" << std::endl;
    std::cout << "  Entry point: 0x" << std::hex << elf_ptr->entry << std::dec << std::endl;
    std::cout << "  Program headers: " << elf_ptr->phnum << std::endl;
    
    // Process each program header
    uint32_t sz = 0;
    for (uint32_t i = 0; i < elf_ptr->phnum; i++) {
        uint8_t ph_buf[sizeof(proghdr)];
        disk.read_file("/bin/myprogram", ph_buf, 
                      elf_ptr->phoff + i * sizeof(proghdr), sizeof(proghdr));
        proghdr* ph = (proghdr*)ph_buf;
        
        std::cout << "\n[EXEC] Processing program header " << i << ":" << std::endl;
        std::cout << "  Type: " << (ph->type == ELF_PROG_LOAD ? "LOAD" : "OTHER") << std::endl;
        std::cout << "  Virtual address: 0x" << std::hex << ph->vaddr << std::dec << std::endl;
        std::cout << "  File size: " << ph->filesz << " bytes" << std::endl;
        std::cout << "  Memory size: " << ph->memsz << " bytes" << std::endl;
        
        if (ph->type != ELF_PROG_LOAD)
            continue;
            
        // Allocate virtual memory
        sz = page_mgr.allocuvm(sz, ph->vaddr + ph->memsz);
        if (sz == 0) {
            std::cout << "[EXEC] Failed to allocate memory" << std::endl;
            return 1;
        }
        
        // Load data from disk to RAM
        if (page_mgr.loaduvm(ph->vaddr, disk, "/bin/myprogram", 
                            ph->off, ph->filesz) < 0) {
            std::cout << "[EXEC] Failed to load program data" << std::endl;
            return 1;
        }
    }
    
    // ========== VERIFY LOADED PROGRAM ==========
    std::cout << "\n=== Step 3: Verify Program Loaded Correctly ===" << std::endl;
    
    // Show some loaded code
    std::cout << "\nCode segment at virtual 0x08048000:" << std::endl;
    ram.print_page_contents(0x100000, 64);  // Physical address of first page
    
    std::cout << "\nData segment at virtual 0x08049000:" << std::endl;
    ram.print_page_contents(0x101000, 64);  // Physical address of second page
    
    // Show RAM statistics
    ram.print_stats();
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "1. Created program file on DISK with code and data" << std::endl;
    std::cout << "2. allocuvm() allocated RAM pages and created page table mappings" << std::endl;
    std::cout << "3. loaduvm() copied data from DISK to RAM using page tables" << std::endl;
    std::cout << "4. Program is now loaded in RAM and ready to execute!" << std::endl;
    
    return 0;
}
