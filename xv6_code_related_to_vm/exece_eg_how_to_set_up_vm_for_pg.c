#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
    char *s, *last;
    int i, off;
    uint argc, sz, sp, ustack[3+MAXARG+1];
    struct elfhdr elf;
    struct inode *ip;
    struct proghdr ph;
    pde_t *pgdir, *oldpgdir;
    struct proc *curproc = myproc();
    
    // ========== PHASE 1: OPEN AND VALIDATE ELF FILE ==========
    
    // Acquire filesystem log lock for crash consistency
    begin_op();
    
    // Convert path to inode (file system lookup)
    // namei walks directory tree to find file
    if((ip = namei(path)) == 0){
        end_op();  // Release lock before returning
        cprintf("exec: fail\n");
        return -1;
    }
    
    // Lock the inode to prevent concurrent modifications
    // Multiple processes might try to exec the same file
    ilock(ip);
    pgdir = 0;  // Initialize page directory pointer
    
    // Read ELF header from beginning of file
    // ELF header contains metadata: entry point, segment count, etc.
    if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
        goto bad;
    
    // Verify ELF magic number (0x7F454C46 = "\x7FELF")
    // Ensures this is actually an ELF executable
    if(elf.magic != ELF_MAGIC)
        goto bad;
    
    // Create new page directory with kernel mappings
    // setupkvm() maps kernel space (0x80000000+) into upper half
    // This is the NEW page directory for the new program
    if((pgdir = setupkvm()) == 0)
        goto bad;
    
    // ========== PHASE 2: LOAD PROGRAM SEGMENTS ==========
    
    // sz tracks end of virtual address space (starts at 0)
    // This is VIRTUAL address, not physical!
    sz = 0;
    
    // Loop through all program headers (segments)
    // elf.phoff = offset in file where program headers start
    // elf.phnum = number of program headers
    for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
        
        // Read one program header from file
        // Program header describes one segment (code, data, etc.)
        if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
            goto bad;
        
        // Only process LOAD segments (loadable into memory)
        // Skip other types: DYNAMIC, NOTE, GNU_STACK, etc.
        if(ph.type != ELF_PROG_LOAD)
            continue;
        
        // VALIDATION: Memory size must be >= file size
        // filesz = bytes of data in file
        // memsz = bytes needed in memory (includes BSS)
        // Example: .data+.bss segment:
        //   filesz = 100 (initialized data in file)
        //   memsz = 500 (initialized + uninitialized)
        // Invalid if filesz > memsz (can't fit file data in memory)
        if(ph.memsz < ph.filesz)
            goto bad;
        
        // VALIDATION: Check for integer overflow
        // If vaddr + memsz wraps around, it's an attack or corruption
        // Example overflow:
        //   vaddr = 0xFFFFFFFF
        //   memsz = 0x100
        //   vaddr + memsz = 0xFF (wrapped around!)
        if(ph.vaddr + ph.memsz < ph.vaddr)
            goto bad;
        
        // ALLOCATE VIRTUAL MEMORY for this segment
        // pgdir: page directory (for page table entries)
        // sz: current end of virtual address space
        // ph.vaddr + ph.memsz: new end of virtual address space
        // 
        // allocuvm:
        // 1. Allocates physical pages (random locations via kalloc)
        // 2. Creates page table entries mapping virtual → physical
        // 3. Returns new sz (end of virtual address space)
        //
        // KEY: sz tracks VIRTUAL addresses, physical are random!
        // After this call, virtual addresses [sz to ph.vaddr+ph.memsz] are mapped
        if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
            goto bad;
        
        // VALIDATION: Virtual address must be page-aligned
        // Required by hardware (page tables work on 4KB boundaries)
        if(ph.vaddr % PGSIZE != 0)
            goto bad;
        
        // LOAD SEGMENT DATA from disk to memory
        // pgdir: page directory (for virtual → physical translation)
        // ph.vaddr: virtual address where to load (destination)
        // ip: inode (file on disk)
        // ph.off: offset in file where segment data starts
        // ph.filesz: how many bytes to copy
        //
        // loaduvm:
        // 1. Uses page tables to find physical addresses
        // 2. Reads from disk file into physical pages
        // 3. Only loads filesz bytes (not memsz)
        // 4. Remaining bytes (memsz - filesz) are BSS, already zeroed by allocuvm
        if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
            goto bad;
    }
    
    // All segments loaded, unlock file and release log
    iunlockput(ip);
    end_op();
    ip = 0;  // Mark as closed
    
    // ========== PHASE 3: SETUP STACK ==========
    
    // Round sz up to next page boundary
    // Ensures stack starts at clean page boundary
    // Example: sz = 0x0804A123 → 0x0804B000
    sz = PGROUNDUP(sz);
    
    // Allocate TWO pages for stack
    // Page 1 (sz to sz+PGSIZE): Guard page (inaccessible)
    // Page 2 (sz+PGSIZE to sz+2*PGSIZE): Actual stack
    //
    // Guard page catches stack overflow:
    // - If stack grows too big, hits guard page
    // - MMU generates page fault (no user permission)
    // - OS catches and kills process with stack overflow error
    if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
        goto bad;
    
    // Remove user access permission from guard page
    // This makes it a "trip wire" for stack overflow
    // Any access to this page → page fault → SIGSEGV
    clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
    
    // sp = stack pointer (VIRTUAL ADDRESS!)
    // Points to top of stack (high address)
    // Process will use this virtual address for its stack
    sp = sz;
    
    // ========== PHASE 4: BUILD INITIAL STACK CONTENT ==========
    
    // Push argument strings onto stack
    // argv[0] = "ls", argv[1] = "-l", etc. (in kernel memory)
    // We copy these strings to user stack (at virtual addresses)
    for(argc = 0; argv[argc]; argc++) {
        if(argc >= MAXARG)
            goto bad;
        
        // Calculate space needed for this string
        // strlen(argv[argc]) = length without null terminator
        // +1 = include null terminator '\0'
        // & ~3 = align to 4-byte boundary (for performance)
        sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
        
        // Copy string from KERNEL memory to USER virtual address
        // pgdir: for virtual → physical translation
        // sp: destination VIRTUAL address (where in user stack)
        // argv[argc]: source in kernel memory (char* kernel pointer)
        // strlen+1: copy string INCLUDING null terminator
        //
        // copyout:
        // 1. Takes virtual address sp (e.g., 0xBFFFFFF0)
        // 2. Uses pgdir to translate to physical address
        // 3. Copies from kernel memory to that physical location
        //
        // KEY: sp is a VIRTUAL ADDRESS (uint), not a kernel pointer!
        // Process will later access this string at virtual address sp
        if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
            goto bad;
        
        // Save this virtual address in ustack array
        // This will become argv[argc] pointer in user space
        ustack[3+argc] = sp;  // sp is VIRTUAL address where string is
    }
    
    // NULL-terminate argv array
    // C standard requires argv[argc] == NULL
    ustack[3+argc] = 0;
    
    // Build stack frame for main(int argc, char **argv)
    // ustack[0] = fake return address (if main returns, trap)
    // ustack[1] = argc (number of arguments)
    // ustack[2] = pointer to argv array (virtual address)
    // ustack[3..3+argc-1] = argv[0], argv[1], ... (pointers to strings)
    // ustack[3+argc] = NULL
    //
    // All pointers are VIRTUAL ADDRESSES!
    // Process knows nothing about physical addresses
    ustack[0] = 0xffffffff;           // Fake return PC
    ustack[1] = argc;                 // Argument count
    ustack[2] = sp - (argc+1)*4;      // Pointer to argv array
    
    // Make room on stack for entire ustack array
    // (3 + argc + 1) entries × 4 bytes each
    sp -= (3+argc+1) * 4;
    
    // Copy ustack array from KERNEL to USER stack
    // pgdir: for virtual → physical translation
    // sp: destination VIRTUAL address (where in user stack)
    // ustack: source in kernel memory (uint[] kernel array)
    // (3+argc+1)*4: size in bytes
    //
    // This is the SECOND copyout:
    // First copyout: copied argument STRINGS
    // Second copyout: copies argument METADATA (argc, argv pointers, etc.)
    //
    // Why separate?
    // - First: Copy strings (need to know where they are)
    // - Then: Build pointer array (now we know string locations)
    // - Finally: Copy pointer array to stack
    if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
        goto bad;
    
    // ========== PHASE 5: FINALIZE PROCESS STATE ==========
    
    // Extract program name from path for debugging (ps, top, etc.)
    // "/bin/ls" → "ls"
    for(last=s=path; *s; s++)
        if(*s == '/')
            last = s+1;
    safestrcpy(curproc->name, last, sizeof(curproc->name));
    
    // COMMIT to the new program (point of no return!)
    oldpgdir = curproc->pgdir;        // Save old page directory
    curproc->pgdir = pgdir;           // Switch to NEW page directory
    curproc->sz = sz;                 // Update process size (virtual)
    
    // Set up CPU registers for new program
    // tf = trap frame (saved registers from interrupt)
    // eip = instruction pointer (where to start executing)
    // esp = stack pointer (virtual address of stack top)
    //
    // KEY: Both eip and esp are VIRTUAL ADDRESSES!
    // When process runs, CPU uses these virtual addresses
    // MMU translates them using pgdir (page tables)
    curproc->tf->eip = elf.entry;     // Start at main()
    curproc->tf->esp = sp;            // Stack pointer (virtual!)
    
    // Load new page directory into CR3 register
    // From now on, MMU uses new page tables for translation
    // All virtual addresses are translated using new pgdir
    switchuvm(curproc);
    
    // Free old program's memory
    // Old page directory and all its pages returned to free pool
    freevm(oldpgdir);
    
    // Success! Process now runs new program
    // Everything process sees is VIRTUAL ADDRESSES:
    // - Code at virtual addresses (e.g., 0x08048000)
    // - Data at virtual addresses
    // - Stack at virtual address sp
    // - All pointers are virtual addresses
    // - Process never sees physical addresses!
    //
    // Physical reality:
    // - Physical pages scattered randomly in RAM
    // - Page tables (pgdir) map virtual → physical
    // - MMU does translation on every memory access
    // - Process is "living in" the virtual space we created
    return 0;
    
bad:
    // Cleanup on error
    if(pgdir)
        freevm(pgdir);  // Free newly allocated page directory
    if(ip){
        iunlockput(ip); // Unlock and release inode
        end_op();       // Release filesystem log
    }
    return -1;
}


// We created a VIRTUAL SPACE for the program:
// 1. Read ELF file (describes virtual memory layout)
// 2. Allocate physical pages (random locations in RAM)
// 3. Create page tables (pgdir) mapping virtual → physical
// 4. Load program data into physical pages
// 5. Set up stack at virtual address sp

// When ls program runs:
// - CPU generates ONLY virtual addresses
// - Program sees: code at 0x08048000, stack at sp, etc.
// - Program never knows about physical addresses
// - All pointers (like sp) are virtual addresses
// - MMU translates every virtual address to physical

// The process "lives in" this virtual space:
int main(int argc, char **argv) {
    // argc, argv are at virtual address ESP
    // Code executing at virtual address EIP
    // All variables at virtual addresses
    // Stack grows at virtual addresses
    // malloc returns virtual addresses
    
    // Process never sees:
    // - Physical address 0x00200000 (where code really is)
    // - Physical address 0x00500000 (where stack really is)
    // - Page tables, CR3, MMU operations
    
    // Process only sees:
    // - Virtual address 0x08048000 (code)
    // - Virtual address sp (stack)
    // - All memory appears contiguous and clean
    
    // This is virtual memory magic!
}