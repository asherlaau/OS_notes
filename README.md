# OS_notes

# virtual mem

the simulation i make, is just for reference how mmap and page swapping doing, 

https://elixir.bootlin.com/linux/v6.16.7/source

some linux kernel code is worth to read
```C++
sys_mmap_pgoff()     // System call entry point
do_mmap()           // Core mapping logic
find_vma()          // VMA search
handle_mm_fault()   // Page fault handler
```









# notes on loading program from disk to ram

1. when we try to run a program, the program is a file, it needs to be in memory to run.
2. To load to memory, we need to reserve the place for the program.
3. allocuvm() helps to set up the virtual memory space, it will help to map the virtual address and physical address, store in the page table entry. Now, we reserve the " place " in the memory, but the memory is empty.
4. Now, with the program file address (inode) and the physical address in mem (page table entry), we can copy the program from disk to mem,  this is whhat loaduvm() doing. 

# CR3 and MMU
```/ CPU executes program instructions
int main() {
    int x = 42;              // CPU: "Write to virtual address 0x08049000"
    printf("%d\n", x);       // CPU: "Read from virtual address 0x08049000"
}

// CPU only knows about virtual addresses
// Every memory access uses virtual addresses```
MMU helps to converts the virtual address to phy one, CR3 is a register that stores the physical page directory
┌─────────────────────────────────────────────────────────┐
│                         CPU                             │
│  - Executes instructions                                │
│  - Generates VIRTUAL addresses                          │
│  - Knows nothing about physical memory                  │
└─────────────────────────────────────────────────────────┘
                        ↓ Virtual 0x12345678
┌─────────────────────────────────────────────────────────┐
│                         MMU                             │
│  - Reads CR3 register (0x00100000)                      │
│  - Walks page directory at physical 0x00100000          │
│  - Walks page table at physical 0x00205000              │
│  - Translates: Virtual 0x12345678 → Physical 0x00308678 │
│  - Caches translation in TLB                            │
└─────────────────────────────────────────────────────────┘
                        ↓ Physical 0x00308678
┌─────────────────────────────────────────────────────────┐
│                    Physical RAM                         │
│  - Address 0x00100000: Page Directory                  │
│  - Address 0x00205000: Page Table                      │
│  - Address 0x00308678: Actual Data (0x42)              │
└─────────────────────────────────────────────────────────┘
