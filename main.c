#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define ENTRY_SIZE 32

struct __attribute__((__packed__)) superblock {
    unsigned sysid;
    unsigned short sector_size;
    unsigned sector_count;
    unsigned total_blocks;
    unsigned short block_size;
    unsigned sectors_per_block;
    unsigned bitmap_size;
    unsigned bitmap_start;
    unsigned root_size;
    unsigned root_start;
    unsigned data_start;
    char reserved[32];
};

struct __attribute__((__packed__)) dir_entry{
    int8_t status;
    char file_name[17];
    unsigned short file_type;
    unsigned start_block;
    unsigned size;
    unsigned length;
};

struct __attribute__((__packed__)) superblock_disk {
    unsigned int sysid;           // Offset 0
    unsigned short sector_size;   // Offset 4
    unsigned int total_blocks;    // Offset 6
    unsigned short block_size;    // Offset 10
    unsigned int bitmap_start;    // Offset 12
    unsigned int bitmap_size;     // Offset 16
    unsigned int root_start;      // Offset 20
    unsigned int root_size;       // Offset 24
    unsigned int data_start;      // Offset 28
    char reserved[32];            // Offset 32 (Preencher com zeros)
};

void set_bit(unsigned char *bitmap_buffer, int block_index) {
    int byte_offset = block_index / 8; // Acha a posição do byte no vetor
    int bit_offset  = block_index % 8; // Acha a posição do bit dentro desse byte
    
    // Operação OR bitwise para ligar o bit sem mexer nos outros
    // (1 << bit_offset) cria uma máscara. Ex: bit 2 vira 00000100
    bitmap_buffer[byte_offset] |= (1 << bit_offset);
}

void main(){
    struct superblock sup;
    memset(&sup, 0, sizeof(struct superblock));

    sup.sysid = 1;
    //scanf("%u", &sup.sector_count);
    sup.sector_count = 80;
    sup.sector_size = 9;
    sup.block_size = 2;
    sup.sectors_per_block = (1 << sup.block_size) ;
    sup.total_blocks = (sup.sector_count + (sup.sectors_per_block-1)) / sup.sectors_per_block;  
    sup.bitmap_start = 1;
    sup.bitmap_size = (sup.total_blocks/(1<<(sup.sector_size)<<sup.block_size))+1;
    sup.root_start = sup.bitmap_start + sup.bitmap_size;
    sup.root_size = 4;
    sup.data_start = sup.root_start + sup.root_size;

    printf("Sysid = %u\n", sup.sysid);
    printf("Sector_size = %hu =  %u \n", sup.sector_size, 1 << (sup.sector_size));
    printf("Sector_Count = %u\n", sup.sector_count);
    printf("Sectors per block = %u\n", sup.sectors_per_block);
    printf("Total Blocks = %u\n", sup.total_blocks);
    printf("Block size = %hu = %u\n", sup.block_size, (1 << (sup.sector_size)) << sup.block_size);
    printf("Bitmap Start = %u\n", sup.bitmap_start);
    printf("Bitmap Size = %u\n", sup.bitmap_size);
    printf("Root Start = %u\n", sup.root_start);
    printf("Root Size = %u\n", sup.root_size);
    printf("Data Start = %u\n", sup.data_start);

    FILE *fp = fopen("sacs.img", "wb");
    if (!fp) {
        perror("Erro ao criar arquivo");
        return;
    }

    printf("Arquivo criado\n");
    unsigned int real_block_size = (1 << (sup.sector_size)) << sup.block_size;

    unsigned char *buffer = calloc(1, real_block_size);
    memcpy(buffer, &sup, sizeof(struct superblock));
    fwrite(buffer, real_block_size , 1, fp);

    printf("linha 94\n");
    unsigned int metadata_blocks = 1 + sup.bitmap_size + sup.root_size;

    // Limpa o buffer para usar no bitmap
    memset(buffer, 0, real_block_size);

    for (unsigned int i = 0; i < metadata_blocks; i++) {
        set_bit(buffer, i);
        printf("set bits\n");
    }
    
    fwrite(buffer, sup.bitmap_size, 1, fp);


    memset(buffer, 0, real_block_size);
    for (unsigned int i = 0; i < sup.root_size; i++) {
        fwrite(buffer, real_block_size, 1, fp);
        printf("write root\n");
    }

    unsigned int data_blocks = sup.total_blocks - sup.data_start;
    printf("data blocks = %d\n", data_blocks);
    memset(buffer, 0, real_block_size);
    /** */
    for (unsigned int i = 0; i < data_blocks; i++) {
        fwrite(buffer, real_block_size, 1, fp);
        printf("data write %d\n", data_blocks -i);
    }//*/

    fclose(fp);
    free(buffer);
    printf("Arquivo sacs.img criado com sucesso!\n");


    

}