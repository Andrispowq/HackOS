#include "support.hpp"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/data_structures/bitmap.h"
#include "lib/data_structures/vector.h"
#include "arch/x86_64/paging/page_map_indexer.h"
#include "arch/x86_64/paging/page_tables.h"
#include "memory/memory_map.h"

static void prepare_split_allocator() { heap_init = 1; }
static void release_split_allocation(void* p) { kfree(p); }
#define LIBRARY_CASE(name) TEST(name)
#include "memory_string_cases.hpp"
#include "split_cases.hpp"
#undef LIBRARY_CASE

TEST(bitmap_low_bits) {
    uint64_t data[2]={}; Bitmap b; b.SetBuffer(data); b.SetSize(sizeof data);
    b.Set(1,true); CHECK(b.Get(1)); CHECK(!b.Get(2)); b.Set(1,false); CHECK(!b.Get(1)); return 0;
}
TEST(bitmap_high_bits) {
    uint64_t data[2]={}; Bitmap b; b.SetBuffer(data); b.SetSize(sizeof data);
    const uint64_t indices[] = {31, 32, 63, 64, 127};
    for (uint64_t i: indices) {
        b.Set(i,true); CHECK(data[i/64]==(1UL<<(i%64))); CHECK(b.Get(i)); b.Set(i,false);
        CHECK(data[i/64]==0);
    } return 0;
}
TEST(bitmap_first_free) {
    uint64_t data[2]={0xff,0}; Bitmap b; b.SetBuffer(data); b.SetSize(sizeof data);
    CHECK(b.First()==8); return 0;
}
TEST(bitmap_full_respects_byte_size) {
    uint64_t data[2]={~0UL,~0UL}; Bitmap b; b.SetBuffer(data); b.SetSize(sizeof data);
    CHECK(b.First()==~0UL); return 0;
}
TEST(vector_growth_erase_copy) {
    vector<int> a; CHECK(a.size()==0); for(int i=0;i<17;++i) a.push_back(i);
    for(int i=0;i<17;++i) CHECK(a[i]==i);
    a.erase(8); CHECK(a.size()==16); CHECK(a[8]==9); a.erase(99); CHECK(a.size()==16);
    vector<int> b(a); a[0]=99; CHECK(b[0]==0); b.clear(); CHECK(b.size()==0);
    b.push_back(7); CHECK(b[0]==7); return 0;
}
TEST(vector_self_assignment_preserves_elements) {
    vector<int> a; a.push_back(42); a.push_back(43); a=a;
    CHECK(a.size()==2); CHECK(a[0]==42); CHECK(a[1]==43); return 0;
}
TEST(page_indices_boundaries) {
    PageMapIndexer z(0xfff); CHECK(z.index_P==0 && z.index_PT==0);
    PageMapIndexer p((511UL<<39)|(123UL<<30)|(45UL<<21)|(67UL<<12)|99);
    CHECK(p.index_PDP==511); CHECK(p.index_PD==123); CHECK(p.index_PT==45); CHECK(p.index_P==67);
    PageMapIndexer high(~0UL); CHECK(high.index_PDP==511 && high.index_P==511); return 0;
}
TEST(page_table_layout) { CHECK(sizeof(PageTableEntry)==8); CHECK(sizeof(PageTable)==4096); return 0; }
TEST(memory_map_bios_usable) {
    fromUEFI=false; MemoryMapEntry e[]={{0,4096,1,0},{4096,8192,2,0}}; MemoryMap m(e,2);
    CHECK(m.GetEntryCount()==2); CHECK(m.IsUsuableMemory(0)); CHECK(!m.IsUsuableMemory(1));
    CHECK(!m.IsUsuableMemory(2)); CHECK(m.GetTotalSystemMemory()==12288); CHECK(m.GetUsuableSystemMemory()==4096); return 0;
}
TEST(memory_map_efi_usable) {
    fromUEFI=true; MemoryMapEntry e[]={{0,4096,7,0},{4096,8192,1,0}}; MemoryMap m(e,2);
    CHECK(m.IsUsuableMemory(0)); CHECK(!m.IsUsuableMemory(1)); CHECK(m.GetUsuableSystemMemory()==4096); return 0;
}
TEST(memory_map_empty) {
    MemoryMap m(nullptr,0); CHECK(m.GetTotalSystemMemory()==0); CHECK(!m.IsUsuableMemory(0)); return 0;
}
TEST(memory_map_usable_list_per_instance) {
    fromUEFI=true; MemoryMapEntry a[]={{0,4096,7,0}},b[]={{8192,4096,7,0}};
    MemoryMap m(a,1),n(b,1); CHECK(m.GetUsuableMemory()[0]==a); CHECK(n.GetUsuableMemory()[0]==b); return 0;
}
TEST(memory_map_sparse_high_addresses) {
    fromUEFI=false; MemoryMapEntry e[]={{0x1000,4096,1,0},{0x100000000UL,8192,1,0}};
    MemoryMap m(e,2); CHECK(m.GetTotalSystemMemory()==12288);
    CHECK(m.GetEntry(1)==&e[1]); CHECK(m.GetUsuableSystemMemory()==12288); return 0;
}
extern uint64_t free_mem_addr;
TEST(bootstrap_allocator_bump_and_physical_address) {
    heap_init=0; free_mem_addr=0x1003; uint64_t physical=0;
    CHECK(kmalloc_p(5,&physical)==0x1003); CHECK(physical==0x1003);
    CHECK(kmalloc(8)==0x1008); CHECK(free_mem_addr==0x1010); return 0;
}
TEST(bootstrap_allocator_page_alignment) {
    heap_init=0; free_mem_addr=0x1003; uint64_t physical=0;
    CHECK(kmalloc_ap(16,&physical)==0x2000); CHECK(physical==0x2000);
    CHECK(kmalloc_a(1)==0x3000); return 0;
}
TEST(bootstrap_allocator_alignment_above_4gib) {
    heap_init=0; free_mem_addr=0x100001003UL;
    CHECK(kmalloc_a(16)==0x100002000UL); return 0;
}
