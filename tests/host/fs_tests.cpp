#include "fake_fs.hpp"
#include "fs/vfs.h"
#include "fs/fat32/fat32.h"

TEST(vfs_selects_volume_and_translates_path)
{
    FakeFS fs;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    CHECK(fopen("fs0:/FILE", O_READ) == &fs.file);
    CHECK(test_name_equal(fs.path, "~/FILE"));
    CHECK(fs.file.fs_index == 0);
    return 0;
}

TEST(vfs_two_digit_index)
{
    FakeFS fs;
    InitialiseFilesystems();
    filesystems[12] = &fs;
    CHECK(fopen("fs12:/FILE", O_READ) == &fs.file);
    CHECK(fs.file.fs_index == 12);
    return 0;
}

TEST(vfs_missing_file)
{
    FakeFS fs;
    fs.missing = true;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    CHECK(fopen("fs0:/MISSING", O_READ) == nullptr);
    return 0;
}

TEST(vfs_absent_volume)
{
    InitialiseFilesystems();
    CHECK(fopen("fs9:/FILE", O_READ) == nullptr);
    return 0;
}

TEST(vfs_rejects_out_of_range_volume)
{
    InitialiseFilesystems();
    CHECK(fopen("fs99:/FILE", O_READ) == nullptr);
    return 0;
}

TEST(vfs_create)
{
    FakeFS fs;
    fs.missing = true;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    CHECK(fopen("fs0:/NEW", O_CREAT) == &fs.file);
    CHECK(fs.created);
    CHECK(test_name_equal(fs.path, "~"));
    CHECK(test_name_equal(fs.name, "NEW"));
    return 0;
}

TEST(vfs_io_dispatch_and_close)
{
    FakeFS fs;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    auto file = fopen("fs0:/FILE", O_READ);
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(fwrite(b, 2, 4, file) == 0);
    CHECK(fs.transferred == 8);
    CHECK(test_equal(fs.contents, b, 8));
    test_fill(b, 0, 8);
    CHECK(fread(b, 2, 4, file) == 0);
    CHECK(b[0] == 1 && b[7] == 8);
    fclose(file);
    CHECK(fs.closed);
    return 0;
}

TEST(vfs_io_propagates_error)
{
    FakeFS fs;
    fs.io_result = -7;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    auto file = fopen("fs0:/FILE", O_READ);
    char b[2];
    CHECK(fread(b, 1, 2, file) == -7);
    return 0;
}

TEST(vfs_null_handle)
{
    char b[1];
    CHECK(fread(b, 1, 1, nullptr) < 0);
    return 0;
}

TEST(vfs_rejects_path_without_volume)
{
    InitialiseFilesystems();
    CHECK(fopen("FILE", O_READ) == nullptr);
    return 0;
}

TEST(vfs_invalid_handle_index)
{
    FakeFile file;
    file.fs_index = -1;
    char b[1];
    CHECK(fread(b, 1, 1, &file) < 0);
    CHECK(fwrite(b, 1, 1, &file) < 0);
    fclose(&file);
    return 0;
}

TEST(vfs_handle_for_unmounted_volume)
{
    InitialiseFilesystems();
    FakeFile file;
    file.fs_index = 5;
    char b[1];
    CHECK(fread(b, 1, 1, &file) < 0);
    CHECK(fwrite(b, 1, 1, &file) < 0);
    fclose(&file);
    return 0;
}

TEST(filesystem_registration_reuses_empty_slot)
{
    FakeFS a, b, c;
    InitialiseFilesystems();
    RegisterFilesystem(&a);
    RegisterFilesystem(&b);
    CHECK(filesystems[0] == &a && filesystems[1] == &b);
    UnregisterFilesystem(&a);
    CHECK(filesystems[0] == nullptr && filesystems[1] == &b);
    RegisterFilesystem(&c);
    CHECK(filesystems[0] == &c);
    return 0;
}

class MemoryDisk : public Device
{
  public:
    unsigned char* bytes;
    size_t size;

    MemoryDisk()
    {
        bytes = (unsigned char*)test_disk_load(&size);
    }

    ~MemoryDisk() override
    {
        test_free(bytes);
    }

    DeviceType GetType() const override
    {
        return DeviceType::ATA_PIO;
    }

    void Read(uint64_t lba, void* p, uint64_t count) override
    {
        bounds(lba, count);
        test_copy(p, bytes + lba * 512, count * 512);
    }

    void Write(uint64_t lba, void* p, uint64_t count) override
    {
        bounds(lba, count);
        test_copy(bytes + lba * 512, p, count * 512);
    }

    void bounds(uint64_t lba, uint64_t count)
    {
        if (lba > size / 512 || count > size / 512 - lba)
        {
            test_abort("FAT driver accessed outside disk");
        }
    }
};

TEST(fat_read_fixture)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 13);
    char b[13];
    CHECK(fs.Read(file, b, 13) == 0);
    CHECK(test_equal(b, "Hello HackOS\n", 13));
    fs.CloseFile(file);
    return 0;
}

TEST(fat_read_across_clusters)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 8193);
    auto b = (unsigned char*)test_alloc(8193);
    CHECK(fs.Read(file, b, 8193) == 0);
    for (unsigned i = 0; i < 8193; ++i)
    {
        CHECK(b[i] == (unsigned char)(i * 17 + 3));
    }

    test_free(b);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_missing_file)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/DOESNOT.EXI") == nullptr);
    return 0;
}

TEST(fat_directory_enumeration)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    CHECK(fs.GetDirectoryEntryCount("~/TESTS") >= 2);
    CHECK(fs.GetDirectoryEntryCount("~/TESTS/HELLO.TXT") == 0);
    CHECK(fs.GetFile("~/TESTS/HELLO.TXT", 0) == nullptr);
    return 0;
}

TEST(fat_rewrite_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
        CHECK(file != nullptr);
        CHECK(fs.Write(file, (void*)"Changed data\n", 13) == 0);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_read_long_filename)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/Long file name.txt");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 10);
    char b[10];
    CHECK(fs.Read(file, b, 10) == 0);
    CHECK(test_equal(b, "Long name\n", 10));
    fs.CloseFile(file);
    return 0;
}

TEST(fat_zero_length_read)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/EMPTY.TXT");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 0);
    char b = 42;
    CHECK(fs.Read(file, &b, 0) == 0);
    CHECK(b == 42);
    CHECK(file->seek_position == 0);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_seeked_read)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file != nullptr);
    file->seek_position = 511;
    unsigned char b[1025];
    CHECK(fs.Read(file, b, sizeof b) == 0);
    CHECK(file->seek_position == 1536);
    for (unsigned i = 0; i < sizeof b; ++i)
    {
        CHECK(b[i] == (unsigned char)((i + 511) * 17 + 3));
    }

    fs.CloseFile(file);
    return 0;
}

TEST(fat_create_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.CreateFile("~/TESTS", "NEW.TXT", ARCHIVE);
        CHECK(file != nullptr);
        fs.ResizeFile(file, 13);
        CHECK(fs.Write(file, (void*)"Created data\n", 13) == 0);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_shrink_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
        CHECK(file != nullptr);
        fs.ResizeFile(file, 5);
        CHECK(file->GetSize() == 5);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_delete_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
        CHECK(file != nullptr);
        fs.DeleteFile(file);
        CHECK(fs.OpenFile("~/TESTS/HELLO.TXT") == nullptr);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_rejects_zero_sectors_per_cluster)
{
    MemoryDisk disk;
    disk.bytes[13] = 0;
    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/TESTS/HELLO.TXT") == nullptr);
    return 0;
}

TEST(fat_partial_write_preserves_neighbors)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file);
    file->seek_position = 511;
    unsigned char patch[3] = {21, 22, 23};
    CHECK(fs.Write(file, patch, 3) == 0);
    file->seek_position = 0;
    unsigned char bytes[8193];
    CHECK(fs.Read(file, bytes, sizeof bytes) == 0);
    for (unsigned i = 0; i < sizeof bytes; ++i)
    {
        CHECK(bytes[i] == (i >= 511 && i < 514 ? patch[i - 511] : (unsigned char)(i * 17 + 3)));
    }

    fs.CloseFile(file);
    return 0;
}

TEST(fat_grow_empty_and_zero_gap)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/EMPTY.TXT");
    CHECK(file);
    file->seek_position = 1025;
    CHECK(fs.Write(file, (void*)"OK", 2) == 0);
    CHECK(file->GetSize() == 1027);
    file->seek_position = 0;
    unsigned char bytes[1027];
    CHECK(fs.Read(file, bytes, sizeof bytes) == 0);
    for (unsigned i = 0; i < 1025; ++i)
    {
        CHECK(bytes[i] == 0);
    }

    CHECK(bytes[1025] == 'O' && bytes[1026] == 'K');
    fs.CloseFile(file);
    return 0;
}

TEST(fat_truncate_to_zero_and_regrow)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file);
    fs.ResizeFile(file, 0);
    CHECK(file->GetSize() == 0);
    fs.ResizeFile(file, 1025);
    CHECK(file->GetSize() == 1025);
    unsigned char bytes[1025];
    CHECK(fs.Read(file, bytes, sizeof bytes) == 0);
    for (unsigned i = 0; i < sizeof bytes; ++i)
    {
        CHECK(bytes[i] == 0);
    }

    fs.CloseFile(file);
    return 0;
}

TEST(fat_eof_read_clamps_position_and_buffer)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
    CHECK(file);
    file->seek_position = 12;
    char bytes[4] = {42, 42, 42, 42};
    CHECK(fs.Read(file, bytes, 4) == 0);
    CHECK(bytes[0] == '\n' && bytes[1] == 42 && file->seek_position == 13);
    file->seek_position = 100;
    CHECK(fs.Read(file, bytes, 4) == 0);
    CHECK(file->seek_position == 100);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_long_name_create_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.CreateFile("~/TESTS", "Another long filename.txt", ARCHIVE);
        CHECK(file);
        CHECK(fs.Write(file, (void*)"Long create\n", 12) == 0);
        fs.CloseFile(file);
        file = fs.OpenFile("~/TESTS/another LONG filename.txt");
        CHECK(file);
        CHECK(file->GetSize() == 12);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_directory_extension_and_lfn_boundary)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    for (unsigned i = 0; i < 24; ++i)
    {
        char name[] = "Boundary filename 00.txt";
        name[18] = '0' + i / 10;
        name[19] = '0' + i % 10;
        auto file = fs.CreateFile("~/TESTS", name, ARCHIVE);
        CHECK(file);
        CHECK(fs.Write(file, (void*)"data", 4) == 0);
        fs.CloseFile(file);
    }

    for (unsigned i = 0; i < 24; ++i)
    {
        char path[] = "~/TESTS/Boundary filename 00.txt";
        path[26] = '0' + i / 10;
        path[27] = '0' + i % 10;
        auto file = fs.OpenFile(path);
        CHECK(file);
        char bytes[4];
        CHECK(fs.Read(file, bytes, 4) == 0);
        CHECK(test_equal(bytes, "data", 4));
        fs.CloseFile(file);
    }

    return 0;
}

TEST(fat_long_name_delete_keeps_neighbors)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/Long file name.txt");
    CHECK(file);
    fs.DeleteFile(file);
    CHECK(fs.OpenFile("~/TESTS/Long file name.txt") == nullptr);
    file = fs.OpenFile("~/TESTS/HELLO.TXT");
    CHECK(file);
    char bytes[13];
    CHECK(fs.Read(file, bytes, 13) == 0);
    CHECK(test_equal(bytes, "Hello HackOS\n", 13));
    fs.CloseFile(file);
    return 0;
}

TEST(fat_new_directory_dot_entries)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto dir = fs.CreateFile("~/TESTS", "SUBDIR", DIRECTORY);
    CHECK(dir);
    fs.CloseFile(dir);
    auto file = fs.CreateFile("~/TESTS/SUBDIR", "NESTED.TXT", ARCHIVE);
    CHECK(file);
    fs.CloseFile(file);
    file = fs.OpenFile("~/TESTS/SUBDIR/../HELLO.TXT");
    CHECK(file);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_rejects_cyclic_directory_chain)
{
    MemoryDisk disk;
    auto* boot = (FAT32_BootSector*)disk.bytes;
    auto* fat = (uint32_t*)(disk.bytes + boot->ReservedSectors * 512);
    fat[boot->RootDirStart] = boot->RootDirStart;
    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/TESTS") == nullptr);
    return 0;
}

TEST(fat_rejects_invalid_bpb_without_writing)
{
    for (unsigned variant = 0; variant < 4; ++variant)
    {
        MemoryDisk disk;
        auto* boot = (FAT32_BootSector*)disk.bytes;
        switch (variant)
        {
        case 0:
            boot->BytesPerSector = 1024;
            break;
        case 1:
            boot->SectorsPerCluster = 3;
            break;
        case 2:
            boot->NumberOfFATs = 0;
            break;
        case 3:
            boot->SectorsPerFAT32 = 1;
            break;
        }

        unsigned char before[512];
        test_copy(before, disk.bytes, 512);
        {
            FAT32 fs(&disk, 0);
            CHECK(fs.OpenFile("~/TESTS") == nullptr);
        }

        CHECK(test_equal(before, disk.bytes, 512));
    }

    return 0;
}
