#include "fake_fs.hpp"
#include "fs/vfs.h"
#include "fs/fat32/fat32.h"

TEST(vfs_selects_volume_and_translates_path) {
    FakeFS fs; InitialiseFilesystems(); filesystems[0]=&fs;
    CHECK(fopen("fs0:/FILE",O_READ)==&fs.file);
    CHECK(test_name_equal(fs.path,"~/FILE")); CHECK(fs.file.fs_index==0); return 0;
}
TEST(vfs_two_digit_index) {
    FakeFS fs; InitialiseFilesystems(); filesystems[12]=&fs;
    CHECK(fopen("fs12:/FILE",O_READ)==&fs.file); CHECK(fs.file.fs_index==12); return 0;
}
TEST(vfs_missing_file) {
    FakeFS fs; fs.missing=true; InitialiseFilesystems(); filesystems[0]=&fs;
    CHECK(fopen("fs0:/MISSING",O_READ)==nullptr); return 0;
}
TEST(vfs_absent_volume) { InitialiseFilesystems(); CHECK(fopen("fs9:/FILE",O_READ)==nullptr); return 0; }
TEST(vfs_rejects_out_of_range_volume) {
    InitialiseFilesystems(); CHECK(fopen("fs99:/FILE",O_READ)==nullptr); return 0;
}
TEST(vfs_create) {
    FakeFS fs; fs.missing=true; InitialiseFilesystems(); filesystems[0]=&fs;
    CHECK(fopen("fs0:/NEW",O_CREAT)==&fs.file); CHECK(fs.created);
    CHECK(test_name_equal(fs.path,"~")); CHECK(test_name_equal(fs.name,"NEW")); return 0;
}
TEST(vfs_io_dispatch_and_close) {
    FakeFS fs; InitialiseFilesystems(); filesystems[0]=&fs;
    auto file=fopen("fs0:/FILE",O_READ); unsigned char b[8]={1,2,3,4,5,6,7,8};
    CHECK(fwrite(b,2,4,file)==0); CHECK(fs.transferred==8); CHECK(test_equal(fs.contents,b,8));
    test_fill(b,0,8); CHECK(fread(b,2,4,file)==0); CHECK(b[0]==1&&b[7]==8);
    fclose(file); CHECK(fs.closed); return 0;
}
TEST(vfs_io_propagates_error) {
    FakeFS fs; fs.io_result=-7; InitialiseFilesystems(); filesystems[0]=&fs;
    auto file=fopen("fs0:/FILE",O_READ); char b[2]; CHECK(fread(b,1,2,file)==-7); return 0;
}
TEST(vfs_null_handle) { char b[1]; CHECK(fread(b,1,1,nullptr)<0); return 0; }
TEST(vfs_rejects_path_without_volume) {
    InitialiseFilesystems(); CHECK(fopen("FILE",O_READ)==nullptr); return 0;
}
TEST(vfs_invalid_handle_index) {
    FakeFile file; file.fs_index=-1; char b[1];
    CHECK(fread(b,1,1,&file)<0); CHECK(fwrite(b,1,1,&file)<0); fclose(&file); return 0;
}
TEST(vfs_handle_for_unmounted_volume) {
    InitialiseFilesystems(); FakeFile file; file.fs_index=5; char b[1];
    CHECK(fread(b,1,1,&file)<0); CHECK(fwrite(b,1,1,&file)<0); fclose(&file); return 0;
}
TEST(filesystem_registration_reuses_empty_slot) {
    FakeFS a,b,c; InitialiseFilesystems(); RegisterFilesystem(&a); RegisterFilesystem(&b);
    CHECK(filesystems[0]==&a && filesystems[1]==&b); UnregisterFilesystem(&a);
    CHECK(filesystems[0]==nullptr && filesystems[1]==&b); RegisterFilesystem(&c);
    CHECK(filesystems[0]==&c); return 0;
}

class MemoryDisk : public Device {
public:
    unsigned char* bytes; size_t size;
    MemoryDisk() { bytes=(unsigned char*)test_disk_load(&size); }
    ~MemoryDisk() override { test_free(bytes); }
    DeviceType GetType() const override { return DeviceType::ATA_PIO; }
    void Read(uint64_t lba,void* p,uint64_t count) override { bounds(lba,count); test_copy(p,bytes+lba*512,count*512); }
    void Write(uint64_t lba,void* p,uint64_t count) override { bounds(lba,count); test_copy(bytes+lba*512,p,count*512); }
    void bounds(uint64_t lba,uint64_t count) {
        if(lba>size/512||count>size/512-lba) test_abort("FAT driver accessed outside disk");
    }
};
TEST(fat_read_fixture) {
    MemoryDisk disk; FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/HELLO.TXT");
    CHECK(file!=nullptr); CHECK(file->GetSize()==13);
    char b[13]; CHECK(fs.Read(file,b,13)==0); CHECK(test_equal(b,"Hello HackOS\n",13));
    fs.CloseFile(file); return 0;
}
TEST(fat_read_across_clusters) {
    MemoryDisk disk; FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file!=nullptr); CHECK(file->GetSize()==8193);
    auto b=(unsigned char*)test_alloc(8193); CHECK(fs.Read(file,b,8193)==0);
    for(unsigned i=0;i<8193;++i)CHECK(b[i]==(unsigned char)(i*17+3));
    test_free(b); fs.CloseFile(file); return 0;
}
TEST(fat_missing_file) {
    MemoryDisk disk; FAT32 fs(&disk,0); CHECK(fs.OpenFile("~/DOESNOT.EXI")==nullptr); return 0;
}
TEST(fat_directory_enumeration) {
    MemoryDisk disk; FAT32 fs(&disk,0); CHECK(fs.GetDirectoryEntryCount("~/TESTS")>=2); return 0;
}
TEST(fat_rewrite_and_flush) {
    MemoryDisk disk;
    { FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/HELLO.TXT"); CHECK(file!=nullptr);
      CHECK(fs.Write(file,(void*)"Changed data\n",13)==0); fs.CloseFile(file); }
    test_disk_save(disk.bytes,disk.size); return 0;
}
TEST(fat_read_long_filename) {
    MemoryDisk disk; FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/Long file name.txt");
    CHECK(file!=nullptr); CHECK(file->GetSize()==10); char b[10];
    CHECK(fs.Read(file,b,10)==0); CHECK(test_equal(b,"Long name\n",10)); fs.CloseFile(file); return 0;
}
TEST(fat_zero_length_read) {
    MemoryDisk disk; FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/EMPTY.TXT");
    CHECK(file!=nullptr); CHECK(file->GetSize()==0); char b=42;
    CHECK(fs.Read(file,&b,0)==0); CHECK(b==42); CHECK(file->seek_position==0);
    fs.CloseFile(file); return 0;
}
TEST(fat_seeked_read) {
    MemoryDisk disk; FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file!=nullptr); file->seek_position=511; unsigned char b[1025];
    CHECK(fs.Read(file,b,sizeof b)==0); CHECK(file->seek_position==1536);
    for(unsigned i=0;i<sizeof b;++i) CHECK(b[i]==(unsigned char)((i+511)*17+3));
    fs.CloseFile(file); return 0;
}
TEST(fat_create_and_flush) {
    MemoryDisk disk;
    { FAT32 fs(&disk,0); auto file=fs.CreateFile("~/TESTS","NEW.TXT",ARCHIVE);
      CHECK(file!=nullptr); fs.ResizeFile(file,13);
      CHECK(fs.Write(file,(void*)"Created data\n",13)==0); fs.CloseFile(file); }
    test_disk_save(disk.bytes,disk.size); return 0;
}
TEST(fat_shrink_and_flush) {
    MemoryDisk disk;
    { FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/HELLO.TXT");
      CHECK(file!=nullptr); fs.ResizeFile(file,5); CHECK(file->GetSize()==5); fs.CloseFile(file); }
    test_disk_save(disk.bytes,disk.size); return 0;
}
TEST(fat_delete_and_flush) {
    MemoryDisk disk;
    { FAT32 fs(&disk,0); auto file=fs.OpenFile("~/TESTS/HELLO.TXT");
      CHECK(file!=nullptr); fs.DeleteFile(file); CHECK(fs.OpenFile("~/TESTS/HELLO.TXT")==nullptr); }
    test_disk_save(disk.bytes,disk.size); return 0;
}
TEST(fat_rejects_zero_sectors_per_cluster) {
    MemoryDisk disk; disk.bytes[13]=0; FAT32 fs(&disk,0);
    CHECK(fs.OpenFile("~/TESTS/HELLO.TXT")==nullptr); return 0;
}
