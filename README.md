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
