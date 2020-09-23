//
// Created by caesar kekxv on 2020/9/15.
//

#include <cstdio>
#include "lfs.h"
#include "lfs_util.h"

// variables used by the filesystem
lfs_t lfs;
lfs_file_t file;

uint8_t _file_Buff[2][1024];

int user_provided_block_device_read(const struct lfs_config *c, lfs_block_t block,
                                    lfs_off_t off, void *buffer, lfs_size_t size) {
    memcpy(buffer, &_file_Buff[block][off], size);
    return 0;
}

int user_provided_block_device_prog(const struct lfs_config *c, lfs_block_t block,
                                    lfs_off_t off, const void *buffer, lfs_size_t size) {
    memcpy(&_file_Buff[block][off], buffer, size);
    return 0;
}

int user_provided_block_device_erase(const struct lfs_config *c, lfs_block_t block) {
    memset(_file_Buff[block], 0x00, sizeof(_file_Buff[block]));
    return 0;
}

int user_provided_block_device_sync(const struct lfs_config *c) {
    return 0;
}

// configuration of the filesystem is provided by this struct
struct lfs_config cfg;

void test_mkdir() {
    int err = lfs_mkdir(&lfs, "/");
    err = lfs_mkdir(&lfs, "/root/data");
}

void test_dir_change(const char *path) {
    lfs_dir_t dir;
    int err = 0;
    struct lfs_info info;
    err = lfs_dir_open(&lfs, &dir, path);
    if (0 != err) {
        return;
    }
    err = lfs_dir_read(&lfs, &dir, &info);
    lfs_dir_close(&lfs, &dir);
}

void test_write_boot_count() {
    // read current count
    uint32_t boot_count = 0;
    lfs_file_open(&lfs, &file, "boot_count", LFS_O_RDWR | LFS_O_CREAT);
    lfs_file_read(&lfs, &file, &boot_count, sizeof(boot_count));

    // update boot count
    boot_count += 1;
    lfs_file_rewind(&lfs, &file);
    lfs_file_write(&lfs, &file, &boot_count, sizeof(boot_count));

    printf("lfs_file_size: %d\n", lfs_file_size(&lfs, &file));
    // remember the storage is not updated until the file is closed successfully
    lfs_file_close(&lfs, &file);


    // print the boot count
    printf("boot_count: %d\n", boot_count);
}

// entry point
int main(void) {
    {
        cfg.context = NULL;
        // block device operations
        cfg.read = user_provided_block_device_read;
        cfg.prog = user_provided_block_device_prog;
        cfg.erase = user_provided_block_device_erase;
        cfg.sync = user_provided_block_device_sync;
        // block device configuration
        cfg.read_size = 16;
        cfg.prog_size = 16;
        cfg.block_size = 1024;
        cfg.block_count = 2;
        cfg.block_cycles = 500;
        cfg.cache_size = 16;
        cfg.lookahead_size = 16;
        cfg.read_buffer = NULL;
        cfg.prog_buffer = NULL;
        cfg.lookahead_buffer = NULL;
        cfg.name_max = 0;
        cfg.file_max = 0;
        cfg.attr_max = 0;
    };
    // mount the filesystem
    int err = lfs_mount(&lfs, &cfg);
    // reformat if we can't mount the filesystem
    // this should only happen on the first boot
    if (err) {
        lfs_format(&lfs, &cfg);
        err = lfs_mount(&lfs, &cfg);
    }
    test_mkdir();
    test_dir_change("/root");
    test_write_boot_count();
    test_write_boot_count();
    // release any resources we were using
    lfs_unmount(&lfs);
    return 0;
}