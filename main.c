#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define ENTRY_SIZE 32

struct superblock {
    unsigned sysid;
    unsigned short sector_size;
    unsigned total_blocks;
    unsigned short block_size;
    unsigned bitmap_start;
    unsigned bitmap_size;
    unsigned root_start;
    unsigned root_size;
    unsigned data_start;
    char reserved[32];
};

struct dir_entry{
    int8_t status;
    char file_name[17];
    unsigned short file_type;
    unsigned start_block;
    unsigned size;
    unsigned length;
};

void main(){
    struct superblock sup;
    sup.sysid = 1;
    unsigned sector_count;
    scanf("%u", &sector_count);
    sup.sector_size = 9;
    sup.block_size = 2;
    unsigned blocks_per_sector = (sup.sector_size << sup.block_size) / sup.sector_size;
    sup.total_blocks = ceil(sector_count / blocks_per_sector);  
    sup.bitmap_start = 1;
    sup.bitmap_size = (sup.total_blocks/(2<<(sup.sector_size-1)<<sup.block_size))+1;
    sup.root_start = sup.bitmap_start + sup.bitmap_size;
    sup.root_size = 4;
    sup.data_start = sup.root_start + sup.root_size;

    printf("Sysid = %u\n", sup.sysid);
    printf("Sector_size = %hu =  %u \n", sup.sector_size, 2 << (sup.sector_size-1));
    printf("Total Blocks = %u\n", sup.total_blocks);
    printf("Block size = %hu = %u\n", sup.block_size, (2 << (sup.sector_size-1)) << sup.block_size);
    printf("Bitmap Start = %u\n", sup.bitmap_start);
    printf("Bitmap Size = %u\n", sup.bitmap_size);
    printf("Root Start = %u\n", sup.root_start);
    printf("Root Size = %u\n", sup.root_size);
    printf("Data Start = %u\n", sup.data_start);
    
    


    


}