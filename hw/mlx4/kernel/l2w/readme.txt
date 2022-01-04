This library is intended for drivers ported from Linux.
It contains mostly wrappers for Linux kernel/compiler tools.
To use it one needs to include l2w.h file and to link with l2w.lib.
l2w.lib uses in turn complib.lib library.

Here are the services, l2w provides:

//
// l2w_atomic.h - dealing with atomics
//

// data types
atomic_t							- 32-bit atomic variable

// methods
atomic_read							- read atomic value
atomic_set							- set atomic value
atomic_inc							- increment atomic value
atomic_dec							- decrement atomic value
atomic_inc_and_test 				- increment and test atomic value
atomic_dec_and_test 				- decrement and test atomic value


//
// l2w_bit.h	- dealing with bit dwords and maps
//

// methods
fls									- find last set bit in a dword
ffs									- find first set bit in a dword
ffz									- find first zero bit in a dword
find_first_bit						- find first set bit in a map
find_first_zero_bit					- find first zero bit in a map
find_next_zero_bit					- find the next zero bit in a map
DECLARE_BITMAP						- declare a bit map
atomic_set_bit						- set atomically a bit in a bit map
atomic_clear_bit					- clear atomically a bit in a bit map
set_bit								- set atomically a bit in a dword
clear_bit							- clear atomically a bit in a dword
test_bit 							- test a bit in a dword
bitmap_zero							- zero a bit map
bitmap_full							- returns TRUE if bit map is full (all bits are set)
bitmap_empty						- returns TRUE if bit map is empty (all bits are clear)
bitmap_fill							- fill a map with ones
ilog2								- find log2 of the value, stored in dword
is_power_of_2						- return TRUE if the value, stored in dword is a power of 2
roundup_pow_of_two					- round a dword value to an upper power of 2 (e.g., 5-->8)

//
// l2w_list.h	- dealing with double-linked lists
//

// data types
list_head							- a list header/link

// methods
LIST_HEAD							- define and initialize a list header
INIT_LIST_HEAD						- initialize a list header
list_entry							- get to the beginning of the structure for the given list entry
list_for_each_entry					- iterate over a list of 'list_els' of given 'type'
list_for_each_entry_reverse			- iterate backwards over a list of 'list_els' of given 'type'
list_for_each_entry_safe			- iterate over a list of given type safe against removal of list entry
list_add							- insert a new entry after the specified head
list_add_tail						- insert a new entry before the specified head
list_del							- deletes an entry from a list
list_empty							- tests whether a list is empty
list_splice_init					- insert src_list into dst_list and reinitialise the emptied src_list

//
// l2w_memory.h - dealing with memory allocations
//

// data types
dma_addr_t							- implementatin of Linux dma_addr_t type, describing DMA address
struct scatterlist					- implementatin of Linux struct scatterlist

// methods
get_order							- returns log of number of pages (i.e for size <= 4096 ==> 0, for size <= 8192 ==> 1)
kmalloc								- allocate kernel memory
kzalloc								- allocate kernel memory and zero it
kcalloc								- allocate and array of elements in kernel memory and zero it
kfree								- free kernel memory
ioremap								- map bus memory into CPU space
iounmap								- unmap bus memory
lowmem_page_address					- get virtual address of dma_addr_t 
__get_free_page						- allocate a page and zero it
dma_sync_single						- flush DMA buffers (not implemented)
sg_dma_addr							- returns of dma_addr_t of SC list
sg_page								- returns of dma_addr_t of SC list
sg_dma_address						- returns physical address of SC list
sg_dma_address_inc					- increment physical address in SC list
sg_dma_len							- returns the size of SG list
sg_init_table						- zero an array of SG list
sg_set_buf							- set offset in SG list
sg_set_page							- set offset and buffer address in SG list


//
// l2w_pci.h - work with PCI bus
//

// methods
pci_resource_start					- get BAR physical address
pci_resource_len					- get BAR size
readq								- read a word from IO space, mapped to system memory
readl								- read a dword from IO space, mapped to system memory
reads								- read a word from IO space, mapped to system memory
readb								- read a byte from IO space, mapped to system memory
writeq								- write a word from IO space, mapped to system memory
writel								- write a dword from IO space, mapped to system memory
writes								- write a word from IO space, mapped to system memory
writeb								- write a byte from IO space, mapped to system memory

//
// 
// 


