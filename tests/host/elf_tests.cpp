#include "fake_fs.hpp"
// Include the real implementation to reach its internal validation function.
// No copied parser, production edit, or private-to-public macro is involved.
#include "proc/elf/elf.cpp"

static Elf64_Ehdr valid_header() {
    Elf64_Ehdr h={}; test_copy(h.e_ident,"\177ELF",4);
    h.e_ident[EI_CLASS]=ELFCLASS64; h.e_ident[EI_DATA]=ELFDATA2LSB;
    h.e_ident[EI_VERSION]=EV_CURRENT; h.e_type=ET_EXEC; h.e_machine=EM_X86_64;
    h.e_version=EV_CURRENT; h.e_ehsize=sizeof h; h.e_phentsize=sizeof(Elf64_Phdr); return h;
}
TEST(elf_valid_identity) { auto h=valid_header(); CHECK(elf_check_file(&h)==1); return 0; }
TEST(elf_rejects_bad_identity) {
    CHECK(elf_check_file(nullptr)==0);
    for(unsigned field=0;field<7;++field) {
        auto h=valid_header();
        switch(field) {
            case 0:h.e_ident[0]=0;break; case 1:h.e_ident[EI_CLASS]=1;break;
            case 2:h.e_ident[EI_DATA]=2;break; case 3:h.e_type=1;break;
            case 4:h.e_machine=3;break; case 5:h.e_version=0;break;
            case 6:h.e_ident[EI_VERSION]=0;break;
        } CHECK(elf_check_file(&h)==0);
    } return 0;
}
TEST(elf_load_via_filesystem) {
    FakeFS fs; auto h=valid_header(); test_copy(fs.contents,&h,sizeof h);
    fs.file.length=sizeof h; InitialiseFilesystems(); filesystems[0]=&fs;
    uint64_t base=0; auto result=LoadProgram("~/FILE",&base);
    CHECK(result!=nullptr); CHECK((uint64_t)result==base); CHECK(fs.closed); kfree(result); return 0;
}
TEST(elf_rejects_truncated_header) {
    FakeFS fs; auto h=valid_header(); test_copy(fs.contents,&h,sizeof h);
    fs.file.length=7; InitialiseFilesystems(); filesystems[0]=&fs;
    uint64_t base=0; CHECK(LoadProgram("~/FILE",&base)==nullptr); return 0;
}
TEST(elf_rejects_outside_program_headers) {
    FakeFS fs; auto h=valid_header(); h.e_phoff=4096; h.e_phnum=1;
    test_copy(fs.contents,&h,sizeof h); fs.file.length=sizeof h;
    InitialiseFilesystems(); filesystems[0]=&fs; uint64_t base=0;
    CHECK(LoadProgram("~/FILE",&base)==nullptr); return 0;
}
TEST(elf_copies_segment_and_zeros_bss) {
    struct Image { Elf64_Ehdr header; Elf64_Phdr segment; unsigned char payload[4]; } image={};
    image.header=valid_header(); image.header.e_phoff=sizeof(Elf64_Ehdr); image.header.e_phnum=1;
    unsigned char destination[16]; test_fill(destination,0xa5,16);
    image.segment.p_type=PT_LOAD; image.segment.p_paddr=(uint64_t)destination;
    image.segment.p_offset=sizeof(Elf64_Ehdr)+sizeof(Elf64_Phdr);
    image.segment.p_filesz=4; image.segment.p_memsz=16;
    test_copy(image.payload,"DATA",4); test_mapping_count=0;
    PrepareProgram(&image.header,(uint64_t)&image);
    CHECK(test_mapping_count==1); CHECK(test_equal(destination,"DATA",4));
    for(unsigned i=4;i<16;++i)CHECK(destination[i]==0); return 0;
}
