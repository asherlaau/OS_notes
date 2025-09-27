# Complete Guide to Multi-Level Page Tables

## Table of Contents
1. [The Problem: Virtual Memory Translation](#the-problem-virtual-memory-translation)
2. [Single-Level Page Tables: The Naive Approach](#single-level-page-tables-the-naive-approach)
3. [Multi-Level Page Tables: The Solution](#multi-level-page-tables-the-solution)
4. [Virtual Address Translation Process](#virtual-address-translation-process)
5. [Memory Management Hardware (MMU)](#memory-management-hardware-mmu)
6. [Page Directory Storage and Growth](#page-directory-storage-and-growth)
7. [Process Isolation and Context Switching](#process-isolation-and-context-switching)
8. [Real-World Examples](#real-world-examples)
9. [Memory Efficiency Analysis](#memory-efficiency-analysis)
10. [Advanced Topics](#advanced-topics)

---

## The Problem: Virtual Memory Translation

### What is Virtual Memory?
Virtual memory gives each process the illusion of having a large, contiguous address space. In reality, the physical memory (RAM) is limited and shared among all processes.

### The Translation Challenge
Every memory access by a program uses a **virtual address** that must be translated to a **physical address**:

```
Program says: "Read from address 0x08048000"
MMU thinks: "Which physical memory location does 0x08048000 map to?"
```

### Key Requirements
- **Fast translation**: Every memory access needs translation
- **Process isolation**: Process A cannot access Process B's memory
- **Sparse memory support**: Processes use small portions of their virtual space
- **Dynamic growth**: Processes can allocate/deallocate memory at runtime

---

## Single-Level Page Tables: The Naive Approach

### How It Would Work
A single flat table with one entry per virtual page:

```c
// 32-bit system with 4KB pages
#define VIRTUAL_SPACE_SIZE (4UL * 1024 * 1024 * 1024)  // 4GB
#define PAGE_SIZE (4 * 1024)                           // 4KB
#define NUM_PAGES (VIRTUAL_SPACE_SIZE / PAGE_SIZE)     // 1M pages

struct page_table_entry {
    uint32_t physical_page : 20;  // Physical page number
    uint32_t present : 1;         // Page is in memory
    uint32_t writable : 1;        // Page is writable
    uint32_t user : 1;            // User accessible
    uint32_t flags : 9;           // Other flags
};

// Single-level page table
struct page_table_entry flat_table[NUM_PAGES];  // 1M entries = 4MB
```

### The Problems

#### Memory Waste
```
Every process needs: 1,048,576 entries × 4 bytes = 4MB
For 100 processes: 100 × 4MB = 400MB just for page tables!
```

#### Sparse Memory Usage
```
Typical process memory layout:
- Code: 4KB at 0x08048000
- Data: 4KB at 0x08049000  
- Heap: 1MB starting at 0x10000000
- Stack: 8KB at 0xBFFFF000

Total used: ~1MB out of 4GB virtual space (0.025% utilization)
But still need 4MB page table with 99.975% empty entries!
```

#### Scalability Issues
```
64-bit systems: 2^48 virtual addresses = 256TB
Pages: 256TB / 4KB = 64 billion pages
Flat table: 64 billion × 8 bytes = 512GB per process!
```

---

## Multi-Level Page Tables: The Solution

### Core Concept
Instead of one giant table, use a **tree structure** with multiple levels:

```
Virtual Address → Directory Index → Table Index → Physical Page
```

### Two-Level Page Tables (x86-32)

#### Virtual Address Structure
```
32-bit Virtual Address:
┌────────────┬────────────┬──────────────────┐
│ Dir Index  │Table Index │     Offset       │
│ (10 bits)  │ (10 bits)  │    (12 bits)     │
│  0-1023    │  0-1023    │     0-4095       │
└────────────┴────────────┴──────────────────┘
     22-31        12-21          0-11
```

#### Tree Structure
```
                   CR3 Register
                       │
                       ▼
              ┌─────────────────┐
              │ Page Directory  │ ← 1024 entries (4KB)
              │ (Array of PTEs) │
              ├─ Entry 0        │ → Page Table 0 (or NULL)
              ├─ Entry 1        │ → Page Table 1 (or NULL)
              ├─ Entry 2        │ → NULL (unused region)
              ├─    ...         │
              ├─ Entry 32       │ → Page Table 32 (code/data)
              ├─    ...         │
              ├─ Entry 64       │ → Page Table 64 (heap)
              ├─    ...         │
              ├─ Entry 767      │ → Page Table 767 (stack)
              ├─    ...         │
              └─ Entry 1023     │ → NULL (unused region)
                                │
    ┌───────────────────────────┼───────────────────────────┐
    │                           │                           │
    ▼                           ▼                           ▼
┌─────────┐               ┌─────────┐               ┌─────────┐
│Page Tbl │               │Page Tbl │               │Page Tbl │
│   32    │               │   64    │               │  767    │
├Entry 0  │ → Phys Page   ├Entry 0  │ → Phys Page   ├Entry 0  │ → Phys Page
├Entry 1  │ → Phys Page   ├Entry 1  │ → Phys Page   ├Entry 1  │ → Phys Page
├Entry 2  │ → NULL        ├Entry 2  │ → Phys Page   ├Entry 2  │ → NULL
├  ...    │               ├  ...    │               ├  ...    │
└Entry1023│               └Entry1023│               └Entry1023│
```

### Address Coverage
```
Page Directory Entry Coverage:
- Each entry covers: 2^22 = 4MB of virtual space
- Entry 0: Virtual 0x00000000 - 0x003FFFFF (4MB)
- Entry 1: Virtual 0x00400000 - 0x007FFFFF (4MB)
- Entry 32: Virtual 0x08000000 - 0x083FFFFF (4MB) ← Typical code/data
- Entry 64: Virtual 0x10000000 - 0x103FFFFF (4MB) ← Typical heap
- Entry 767: Virtual 0xBFC00000 - 0xBFFFFFFF (4MB) ← Typical stack

Page Table Entry Coverage:
- Each entry covers: 2^12 = 4KB (one page)
- Entry 0: Virtual page offset 0x000
- Entry 1: Virtual page offset 0x001
- ...
- Entry 1023: Virtual page offset 0x3FF
```

---

## Virtual Address Translation Process

### Step-by-Step Translation

#### Example: Translate Virtual Address 0x12345678

##### Step 1: Parse Virtual Address
```c
uint32_t virtual_addr = 0x12345678;

// Extract indices
uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;    // Bits 31-22: 0x48 = 72
uint32_t table_index = (virtual_addr >> 12) & 0x3FF;  // Bits 21-12: 0x345 = 837
uint32_t offset = virtual_addr & 0xFFF;                // Bits 11-0: 0x678 = 1656

printf("Virtual 0x%08X → Dir[%d] Table[%d] Offset[%d]\n", 
       virtual_addr, dir_index, table_index, offset);
// Output: Virtual 0x12345678 → Dir[72] Table[837] Offset[1656]
```

##### Step 2: Look Up Page Directory Entry
```c
// Get page directory physical address from CR3
uint32_t page_dir_phys = read_cr3();
uint32_t *page_directory = (uint32_t*)page_dir_phys;

// Look up directory entry
uint32_t pde = page_directory[dir_index];  // page_directory[72]

if (!(pde & 0x001)) {  // Present bit
    // Page table doesn't exist!
    printf("PAGE FAULT: Page table not present\n");
    trigger_page_fault();
    return;
}

// Extract page table physical address
uint32_t page_table_phys = pde & 0xFFFFF000;  // Clear flag bits
```

##### Step 3: Look Up Page Table Entry
```c
uint32_t *page_table = (uint32_t*)page_table_phys;
uint32_t pte = page_table[table_index];  // page_table[837]

if (!(pte & 0x001)) {  // Present bit
    // Page not in memory!
    printf("PAGE FAULT: Page not present\n");
    trigger_page_fault();
    return;
}

// Extract physical page address
uint32_t phys_page = pte & 0xFFFFF000;  // Clear flag bits
```

##### Step 4: Calculate Final Physical Address
```c
uint32_t physical_addr = phys_page + offset;
printf("Physical address: 0x%08X\n", physical_addr);

// Now access the actual data
return memory[physical_addr];
```

### Translation Example Walkthrough

```
Virtual Address: 0x12345678

Step 1: Parse
├─ Directory Index: 72 (covers VA 0x12000000-0x123FFFFF)
├─ Table Index: 837 (specific 4KB page within that 4MB region)
└─ Offset: 1656 (specific byte within that 4KB page)

Step 2: Page Directory Lookup
├─ Read CR3 → Page Directory at physical 0x1000000
├─ Read page_directory[72] → 0x2005007
├─ Extract page table address: 0x2005000
└─ Check present bit: ✓

Step 3: Page Table Lookup  
├─ Read page_table[837] at physical 0x2005000 + 837*4
├─ Get value: 0x3008007
├─ Extract physical page: 0x3008000
└─ Check present bit: ✓

Step 4: Final Address
├─ Physical page: 0x3008000
├─ Add offset: 0x3008000 + 1656
└─ Final physical address: 0x3008678
```

---

## Memory Management Hardware (MMU)

### What is the MMU?
The **Memory Management Unit** is hardware built into the CPU that automatically performs virtual-to-physical address translation.

### MMU Components

#### Translation Lookaside Buffer (TLB)
```c
// Hardware cache inside CPU
struct tlb_entry {
    uint32_t virtual_page;    // Virtual page number
    uint32_t physical_page;   // Physical page number
    uint8_t flags;            // Permissions, valid bit
} tlb_cache[64-512];  // Typical size varies
```

#### Page Table Walker
- Hardware logic that traverses page table structures
- Automatically walks multi-level page tables
- Updates TLB with new translations

#### Control Registers (x86)
```c
CR0: Contains paging enable bit
CR2: Stores faulting virtual address during page faults
CR3: Points to page directory physical address
CR4: Various paging extension flags
```

### MMU vs OS Responsibilities

#### MMU (Hardware) Does:
- **Fast translation**: Translates every memory access using TLB/page tables
- **Page fault detection**: Detects when pages aren't present
- **Automatic page table walking**: Traverses multi-level structures
- **TLB management**: Caches recent translations

#### OS (Software) Does:
- **Page table creation**: Allocates and initializes page tables
- **Page fault handling**: Loads pages from disk, allocates memory
- **Process management**: Maintains separate page tables per process
- **Memory allocation**: Manages physical memory and virtual address spaces

### Translation Process Flow
```
1. CPU generates virtual address
2. MMU checks TLB for cached translation
3. If TLB hit: Use cached physical address (fast path)
4. If TLB miss: Walk page tables in hardware
5. If page not present: Generate page fault interrupt
6. OS handles page fault: Load page, update page tables
7. MMU retries translation
8. Access physical memory
```

---

## Page Directory Storage and Growth

### Where Page Directories Are Stored

#### Kernel Memory Space
Page directories and page tables are stored in **kernel-controlled physical memory**:

```
Physical Memory Layout:
┌─────────────────────────┐ High Memory
│                         │
│ KERNEL MEMORY SPACE     │ ← Page directories stored here
│ ┌─────────────────────┐ │
│ │ Process A Page Dir  │ │ ← 4KB
│ │ Process A Page Tbls │ │ ← Variable size
│ │ Process B Page Dir  │ │ ← 4KB  
│ │ Process B Page Tbls │ │ ← Variable size
│ │ Kernel Data         │ │
│ │ Device Drivers      │ │
│ └─────────────────────┘ │
├─────────────────────────┤
│                         │
│ USER MEMORY SPACE       │ ← User process data stored here
│ ┌─────────────────────┐ │
│ │ Process A Pages     │ │
│ │ Process B Pages     │ │
│ │ Process C Pages     │ │
│ └─────────────────────┘ │
└─────────────────────────┘ Low Memory
```

#### Process Control Block Structure
```c
// Linux task_struct (simplified)
struct task_struct {
    int pid;                    // Process ID
    struct mm_struct *mm;       // Memory management info
    // ... other process state
};

struct mm_struct {
    pgd_t *pgd;                 // Page directory (physical address)
    unsigned long start_code;   // Code segment start
    unsigned long end_code;     // Code segment end
    unsigned long start_data;   // Data segment start
    unsigned long end_data;     // Data segment end
    unsigned long start_brk;    // Heap start
    unsigned long brk;          // Current heap end
    unsigned long start_stack;  // Stack start
    // ... other memory info
};
```

### How Page Tables Grow

#### Initial Process Creation
```c
// Simplified Linux fork() implementation
int do_fork() {
    struct task_struct *child = copy_task_struct(current);
    
    // Child gets its own memory space
    child->mm = copy_mm(current->mm);
    
    return child->pid;
}

struct mm_struct *copy_mm(struct mm_struct *parent_mm) {
    struct mm_struct *mm = allocate_mm();
    
    // Allocate new page directory in KERNEL memory
    mm->pgd = (pgd_t*)get_free_page(GFP_KERNEL);
    
    // Copy parent's mappings (copy-on-write)
    copy_page_tables(parent_mm, mm);
    
    return mm;
}
```

#### On-Demand Page Table Creation
```c
// Page fault handler creates page tables as needed
int handle_page_fault(unsigned long virtual_addr) {
    struct mm_struct *mm = current->mm;
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd; 
    pte_t *pte;
    
    // Walk page table hierarchy
    pgd = pgd_offset(mm, virtual_addr);
    
    if (pgd_none(*pgd)) {
        // Allocate new page table in KERNEL memory
        pud = (pud_t*)get_free_page(GFP_KERNEL);
        pgd_populate(mm, pgd, pud);
        printk("Allocated new page table for VA region\n");
    }
    
    // Continue walking...
    pud = pud_offset(pgd, virtual_addr);
    // ... similar logic for other levels
    
    // Handle the actual page fault
    return allocate_page_and_map(mm, virtual_addr);
}
```

#### Memory Layout Growth Examples

##### Heap Growth (malloc/brk)
```c
// When malloc() is called:
void *malloc(size_t size) {
    return sbrk(size);  // System call to extend heap
}

// In kernel:
long sys_brk(unsigned long brk) {
    struct mm_struct *mm = current->mm;
    
    if (brk > mm->brk) {
        // Extend heap VMA (Virtual Memory Area)
        mm->brk = brk;
        // Page tables created on-demand during page faults
    }
    
    return mm->brk;
}
```

##### Stack Growth (automatic)
```c
// Stack grows automatically on page faults
int handle_stack_page_fault(unsigned long addr) {
    struct vm_area_struct *vma = current->mm->mmap;
    
    if (addr < vma->vm_start && 
        addr > vma->vm_start - MAX_STACK_SIZE) {
        // This is valid stack growth
        return expand_stack(vma, addr);
    }
    
    // Invalid access
    return -EFAULT;
}
```

##### Memory Mapping (mmap)
```c
// mmap() creates new virtual memory regions
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;
    
    // Find free virtual address space
    addr = get_unmapped_area(mm, addr, length);
    
    // Create VMA descriptor
    vma = vm_area_alloc(mm);
    vma->vm_start = addr;
    vma->vm_end = addr + length;
    vma->vm_flags = prot;
    
    // Insert into process's VMA list
    insert_vm_struct(mm, vma);
    
    // Page tables will be created on first access
    return addr;
}
```

---

## Process Isolation and Context Switching

### Each Process Has Its Own Page Directory

#### Process Creation and Isolation
```c
// Multiple processes with separate memory spaces
Process A (PID 100):
├─ task_struct.mm.pgd = 0x1000000  (Page Directory A)
├─ Virtual 0x08048000 → Physical 0x2000000
├─ Virtual 0x08049000 → Physical 0x2001000
└─ Virtual 0x10000000 → Physical 0x2002000

Process B (PID 200):  
├─ task_struct.mm.pgd = 0x1100000  (Page Directory B)
├─ Virtual 0x08048000 → Physical 0x3000000  ← Same VA, different PA!
├─ Virtual 0x08049000 → Physical 0x3001000
└─ Virtual 0x10000000 → Physical 0x3002000
```

#### Virtual Address Space Isolation
```
Both processes can use identical virtual addresses:

Process A sees:                Process B sees:
┌─────────────────┐           ┌─────────────────┐
│ VA 0x08048000   │           │ VA 0x08048000   │
│ ↓ (via PGD A)   │           │ ↓ (via PGD B)   │
│ PA 0x2000000    │           │ PA 0x3000000    │
└─────────────────┘           └─────────────────┘

Same virtual address, different physical memory!
Complete isolation between processes.
```

### Context Switching Mechanism

#### How OS Switches Between Processes
```c
// Linux context switch (simplified)
void context_switch(struct task_struct *prev, struct task_struct *next) {
    // Save previous process state
    save_processor_state(prev);
    
    // Switch memory space
    if (prev->mm != next->mm) {
        switch_mm(prev->mm, next->mm, next);
    }
    
    // Switch CPU registers, stack pointer, etc.
    switch_to(prev, next);
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
               struct task_struct *tsk) {
    if (prev != next) {
        // THE KEY OPERATION: Switch page directory
        write_cr3(__pa(next->pgd));  // Load new page directory
        
        // Flush TLB to remove old translations
        flush_tlb_mm(prev);
    }
}
```

#### The CR3 Register
```c
// CR3 (Control Register 3) contains the physical address 
// of the current process's page directory

// When OS switches processes:
Previous process: CR3 = 0x1000000  (Process A's page directory)
Context switch occurs...
New process: CR3 = 0x1100000       (Process B's page directory)

// Now MMU uses Process B's page tables for all translations
```

#### Timeline of Context Switch
```
Time T0: Process A running
├─ CR3 = 0x1000000 (Process A's page directory)
├─ Virtual 0x08048000 → Physical 0x2000000 (via A's page tables)
└─ Process A executing normally

Time T1: Timer interrupt
├─ Save Process A's state (registers, etc.)
├─ Scheduler decides to run Process B
└─ Call context_switch(A, B)

Time T2: Memory space switch
├─ write_cr3(0x1100000)  ← Switch to Process B's page directory
├─ flush_tlb()           ← Clear old address translations
└─ CR3 now points to Process B's page tables

Time T3: Process B running
├─ CR3 = 0x1100000 (Process B's page directory)
├─ Virtual 0x08048000 → Physical 0x3000000 (via B's page tables)
└─ Process B executing normally
```

### Memory Protection and Security

#### Protection Mechanisms
```c
// Page table entries contain permission bits
struct page_table_entry {
    uint32_t physical_addr : 20;
    uint32_t user : 1;           // 0=kernel only, 1=user accessible
    uint32_t writable : 1;       // 0=read-only, 1=read-write
    uint32_t present : 1;        // 0=not in memory, 1=in memory
    // ... other flags
};
```

#### Kernel vs User Memory
```c
// Typical memory layout with protection
Virtual Memory Layout (per process):
┌─────────────────────────┐ 0xFFFFFFFF
│     Kernel Space        │ ← user=0 (kernel only)
│  - Kernel code/data     │
│  - Page tables          │
│  - Device drivers       │
├─────────────────────────┤ 0xC0000000
│                         │
│     User Space          │ ← user=1 (user accessible)
│  - User program         │
│  - User data            │
│  - User heap/stack      │
│                         │
└─────────────────────────┘ 0x00000000
```

---

## Real-World Examples

### x86-32 (Two-Level Page Tables)

#### Address Space Layout
```c
// Standard x86-32 process layout
Virtual Memory Layout:
┌─────────────────────────┐ 0xFFFFFFFF
│   Kernel Space (1GB)    │ ← Shared by all processes
├─────────────────────────┤ 0xC0000000
│                         │
│   User Stack            │ ← Grows downward
│   (~8MB)               │
├─────────────────────────┤ 0xBF800000
│                         │
│   Unused Space          │
│                         │ 
├─────────────────────────┤ 0x40000000
│   Memory Mappings       │ ← mmap(), shared libraries
│   (Libraries, etc.)     │
├─────────────────────────┤ 0x10000000
│   Heap                  │ ← Grows upward (malloc)
├─────────────────────────┤ 0x08100000
│   BSS Segment           │ ← Uninitialized data
├─────────────────────────┤ 0x08050000
│   Data Segment          │ ← Initialized data
├─────────────────────────┤ 0x08049000
│   Text Segment          │ ← Program code
├─────────────────────────┤ 0x08048000
│   Unused                │
└─────────────────────────┘ 0x00000000
```

#### Page Directory Usage
```c
// Typical page directory entries for above layout
Page Directory Analysis:
├─ Entries 0-31: Unused (Virtual 0x00000000-0x07FFFFFF)
├─ Entry 32: Code/Data/BSS (Virtual 0x08000000-0x083FFFFF) 
├─ Entry 33: More Data/BSS (Virtual 0x08400000-0x087FFFFF)
├─ Entries 34-63: Unused
├─ Entry 64: Heap start (Virtual 0x10000000-0x103FFFFF)
├─ Entries 65-255: More heap (as needed)
├─ Entries 256-767: Memory mappings (as needed)
├─ Entries 768-1023: Stack region (Virtual 0xC0000000+)
└─ Only ~5-10 page tables needed out of 1024 possible!
```

### x86-64 (Four-Level Page Tables)

#### Extended Address Space
```c
// x86-64 uses 48-bit virtual addresses (256TB)
Virtual Address (48 bits used):
┌─────┬─────┬─────┬─────┬──────────────┐
│ PML4│ PDP │ PMD │ PTE │   Offset     │
│ (9) │ (9) │ (9) │ (9) │    (12)      │
└─────┴─────┴─────┴─────┴──────────────┘

Four-Level Hierarchy:
CR3 → PML4 (512 entries, each covers 512GB)
        ↓
      PDP (512 entries, each covers 1GB)  
        ↓
      PMD (512 entries, each covers 2MB)
        ↓
      PTE (512 entries, each covers 4KB)
        ↓
      Physical Page (4KB)
```

#### Massive Scalability
```c
// Address space coverage
Each Level Covers:
├─ PTE: 512 pages × 4KB = 2MB
├─ PMD: 512 × 2MB = 1GB
├─ PDP: 512 × 1GB = 512GB  
└─ PML4: 512 × 512GB = 256TB (full virtual space)

// Sparse allocation still works
Typical 64-bit Process:
├─ Uses: ~100MB out of 256TB (0.000004% utilization)
├─ Page table overhead: ~20KB (4 levels × 4KB + a few tables)
└─ vs Flat table: Would need 512GB!
```

### ARM Architecture

#### Different Page Sizes
```c
// ARM supports multiple page sizes
ARM Page Size Options:
├─ 4KB pages (most common)
├─ 16KB pages  
├─ 64KB pages
└─ Variable page sizes within same system

// Two-level page tables (ARM32)
Virtual Address (32-bit, 4KB pages):
┌──────────────┬──────────────┬──────────────────┐
│ L1 Index     │ L2 Index     │     Offset       │
│ (12 bits)    │ (8 bits)     │    (12 bits)     │
└──────────────┴──────────────┴──────────────────┘

// More sparse than x86!
L1 Table: 4096 entries, each covers 1MB
L2 Table: 256 entries, each covers 4KB
```

---

## Memory Efficiency Analysis

### Sparse Memory Comparison

#### Flat Page Table Approach
```c
// Every process needs full page table
Single Process Memory Usage:
├─ Virtual Address Space: 4GB
├─ Page Size: 4KB  
├─ Number of Pages: 1M
├─ Page Table Size: 1M entries × 4 bytes = 4MB
└─ Must allocate entire 4MB even for tiny programs!

100 Processes:
└─ Page Table Memory: 100 × 4MB = 400MB
```

#### Multi-Level Page Table Approach
```c
// Only allocate page tables for used regions
Tiny Process (Hello World):
├─ Used Memory: 12KB (3 pages)
├─ Page Directory: 4KB (always needed)
├─ Page Tables: 1 table × 4KB = 4KB
└─ Total Overhead: 8KB vs 4MB (500x improvement!)

Typical Web Browser:
├─ Used Memory: ~100MB (25,000 pages across ~25 regions)
├─ Page Directory: 4KB
├─ Page Tables: ~25 tables × 4KB = 100KB  
└─ Total Overhead: 104KB vs 4MB (38x improvement!)

Large Database Server:
├─ Used Memory: 2GB (500,000 pages across ~100 regions)
├─ Page Directory: 4KB
├─ Page Tables: ~100 tables × 4KB = 400KB
└─ Total Overhead: 404KB vs 4MB (10x improvement!)
```

### Growth Pattern Analysis

#### Memory Allocation Patterns
```c
// How processes typically allocate memory
Process Memory Growth:
├─ Initial: Code + Data (2-3 page tables)
├─ Early: Small heap allocation (1-2 more page tables)
├─ Runtime: Heap growth (gradual page table expansion)
├─ Peak: Memory mappings, libraries (more page tables)
└─ Page tables grow ONLY as virtual space is used

// vs Flat approach: Always 4MB from start
```

#### Virtual Memory Sparsity
```c
// Real-world memory usage patterns
Typical Desktop Process:
├─ Code: 0x08000000-0x08100000 (1MB)     → 1 page table
├─ Data: 0x08100000-0x08200000 (1MB)     → 0 additional (same region)
├─ Heap: 0x10000000-0x12000000 (32MB)    → 8 page tables  
├─ Stack: 0xBF000000-0xBF800000 (8MB)    → 2 page tables
├─ Libs: 0x40000000-0x45000000 (80MB)    → 20 page tables
└─ Total: 31 page tables out of 1024 possible (97% sparse!)

Mobile App Process
