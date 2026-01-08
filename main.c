#include <stdio.h>
#include <stdlib.h>

struct superblock {
    unsigned sysid;
    unsigned short sector_size;
    unsigned total_blocks;
    unsigned short block_size;
    unsigned bitmap_start;
    unsigned bitmap_size;
    unsigned root_start;
    unsigned root_size;
    unsigned data_size;
    char reserved[32];
};

struct dir_entry{
    bool status;
    char file_name[17];
    unsigned short file_type;
    unsigned start_block;
    unsigned size;
    unsigned length;
};