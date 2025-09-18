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


