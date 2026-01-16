#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include <errno.h> 

#define ENTRY_SIZE 32
#define directory_entry 0x0001
#define file_entry 0x0002
#define deleted_entry 0x0003

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
    char reserved[24];
};

struct __attribute__((__packed__)) dir_entry{
    int8_t status;
    char file_name[17];
    unsigned short file_type;
    unsigned start_block;
    unsigned size;
    unsigned length_in_blocks;
};


void set_bit(unsigned char *bitmap_buffer, int block_index) {
    int byte_offset = block_index / 8; // Acha a posição do byte no vetor
    int bit_offset  = block_index % 8; // Acha a posição do bit dentro desse byte
    
    // Operação OR bitwise para ligar o bit sem mexer nos outros
    // (1 << bit_offset) cria uma máscara. Ex: bit 2 vira 00000100
    bitmap_buffer[byte_offset] |= (1 << bit_offset);
}

// Função para ler o estado do bit (0 ou 1)
int get_bit(unsigned char *bitmap, int index) {
    int byte_offset = index / 8;
    int bit_offset  = index % 8;
    return (bitmap[byte_offset] >> bit_offset) & 1;
}


void print_sup(struct superblock *sup){
    
    printf("Sysid = %u\n", sup->sysid);
    printf("Sector_size = %hu = %u \n", sup->sector_size, 1 << (sup->sector_size));
    printf("Sector_Count = %u\n", sup->sector_count);
    printf("Sectors per block = %u\n", sup->sectors_per_block);
    printf("Total Blocks = %u\n", sup->total_blocks);
    printf("Block size = %hu = %u\n", sup->block_size, (1 << (sup->sector_size)) << sup->block_size);
    printf("Bitmap Start = %u\n", sup->bitmap_start);
    printf("Bitmap Size = %u\n", sup->bitmap_size);
    printf("Root Start = %u\n", sup->root_start);
    printf("Root Size = %u\n", sup->root_size);
    printf("Data Start = %u\n", sup->data_start);
}



struct superblock read_super_block(FILE *fp){
    struct superblock  superblock;
    fseek(fp, 0, SEEK_SET);
    fread(&superblock, sizeof(superblock),1, fp);
    return superblock;
}




unsigned contiguous_alloc(unsigned file_size, unsigned block_size, unsigned bitmap_start,
                      unsigned total_blocks){
    unsigned blocks_needed = (file_size + block_size - 1) / block_size;
    if(blocks_needed == 0) return -1;
    

    FILE *fp;
    fp= fopen("./sacs.img", "r+b");

    unsigned bitmap_byte_count = (total_blocks + 7) / 8;
    unsigned char *bitmap = (unsigned char *)malloc(bitmap_byte_count);

    if (!bitmap) return -1;
    
    long bitmap_offset = (long)bitmap_start * block_size;
    fseek(fp, bitmap_offset, SEEK_SET);
    fread(bitmap, 1, bitmap_byte_count, fp);
    
    int best_start = -1;
    unsigned int best_len = UINT_MAX;

    int current_start = -1;
    unsigned int current_len = 0;
    
    for (unsigned int i = 0; i < total_blocks; i++) {
        // Verifica o bit i (0 = Livre, 1 = Ocupado)
        if (get_bit(bitmap, i) == 0) {
            // É um bloco livre
            if (current_start == -1) current_start = i;
            current_len++;
        } else {
            // Encontrou um bloco ocupado, fim da sequência livre atual
            if (current_start != -1) {
                // A sequência que acabou serve?
                if (current_len >= blocks_needed) {
                    // É melhor (menor sobra) que a anterior?
                    if (current_len < best_len) {
                        best_len = current_len;
                        best_start = current_start;
                        
                        // Otimização: Se o tamanho for exato, não vai achar melhor
                        if (best_len == blocks_needed) break;
                    }
                }
                // Reseta para procurar a próxima
                current_start = -1;
                current_len = 0;
            }
        }
    }

    if (current_start != -1 && current_len >= blocks_needed) {
        if (current_len < best_len) {
            best_start = current_start;
        }
    }

    if (best_start != -1) {
        for (unsigned int i = 0; i < blocks_needed; i++) {
            set_bit(bitmap, best_start + i);
        }

        // Volta o ponteiro para o início do bitmap e escreve as alterações
        fseek(fp, bitmap_offset, SEEK_SET);
        fwrite(bitmap, 1, bitmap_byte_count, fp);
    }

    free(bitmap);
    fclose(fp);

    return best_start;
}

void create_dir_entry(struct dir_entry *entry, char *file_name, unsigned short file_type, 
                      unsigned size, unsigned start_block, unsigned block_size){
    
    entry->status = 1;
    strcpy(entry->file_name, file_name);
    entry->file_type = file_type;
    entry->size = size;
    entry->start_block = start_block;
    entry->length_in_blocks = (size + block_size - 1) / block_size;
}

void create_dir(FILE *fp, unsigned parent_start, unsigned parent_size, struct superblock *sup, char *file_name, unsigned size){
    struct dir_entry entry;
    memset(&entry, 0, sizeof(struct dir_entry));
    unsigned real_block_size = 1 >> sup->sector_size >> sup->block_size;
    unsigned file_start = contiguous_alloc(size, real_block_size, sup->bitmap_start, sup->total_blocks);
    
    if(file_start == -1){
        fprintf(stderr, "Erro na alocação contigua\n");
        exit(EXIT_FAILURE);

    }


    create_dir_entry(&entry, file_name, directory_entry, size, file_start, real_block_size);
    fwrite(&entry, 1, ENTRY_SIZE, fp);  
    memset(&entry, 0, sizeof(struct dir_entry));
    //Setar fp no inicio de onde tem que escrever
    fseek(fp, real_block_size * file_start, SEEK_SET);
    
    //criar entradas ./ e ../ nos blocos de dados indicados no bitmap
    create_dir_entry(&entry, ".", directory_entry, size, file_start, real_block_size);
    fwrite(&entry, 1, ENTRY_SIZE, fp);  
    memset(&entry, 0, sizeof(struct dir_entry));

    create_dir_entry(&entry, "..", directory_entry, parent_size, parent_start, real_block_size);
    fwrite(&entry, 1, ENTRY_SIZE, fp);  
    


}


void create_file(){}

void main(){

    FILE *fp;

    fp= fopen("./sacs.img", "r+b");
    struct superblock  sup = read_super_block(fp);
    print_sup(&sup);
    unsigned real_block_size = 1 << sup.sector_size << sup.block_size;
    //Criar diretorio raiz com ./ e ../ levando a si mesmo
 
    fclose(fp);



    

}