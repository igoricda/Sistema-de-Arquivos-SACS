#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// --- CONFIGURAÇÕES DO SACS ---
#define SACS_MAGIC 0x53414353
#define ENTRY_SIZE 32
#define TYPE_DIR 0x0002
#define TYPE_FILE 0x0003
#define STATUS_FREE 0
#define STATUS_VALID 1
#define STATUS_DELETED 3

// --- ESTRUTURAS (Baseadas no PDF) ---

struct __attribute__((__packed__)) superblock {
    uint32_t sysid;           // 0
    uint16_t sector_size;     // 4
    uint32_t sector_count;    // 6
    uint32_t total_blocks;    // 10
    uint16_t block_size;      // 14
    uint32_t sectors_per_block; // 16
    uint32_t bitmap_start;    // 20
    uint32_t bitmap_size;     // 24
    uint32_t root_start;      // 28
    uint32_t root_size;       // 32
    uint32_t data_start;      // 36
    char reserved[32];        // 40
};

struct __attribute__((__packed__)) dir_entry {
    int8_t status;
    char file_name[17];
    uint16_t file_type;
    uint32_t start_block;
    uint32_t size;
    uint32_t length; // length_in_blocks
};

// --- FUNÇÕES AUXILIARES DE BITS ---

void set_bit(unsigned char *bitmap_buffer, int block_index) {
    bitmap_buffer[block_index / 8] |= (1 << (block_index % 8));
}

int get_bit(unsigned char *bitmap, int index) {
    return (bitmap[index / 8] >> (index % 8)) & 1;
}

// Função para marcar um bloco como LIVRE (0)
void unset_bit(unsigned char *bitmap_buffer, int block_index) {
    int byte_offset = block_index / 8;
    int bit_offset  = block_index % 8;
    bitmap_buffer[byte_offset] &= ~(1 << bit_offset);
}

// ALOCAÇÃO 
long int contiguous_alloc(FILE *fp, unsigned file_size, unsigned block_size, 
                          unsigned bitmap_start, unsigned total_blocks) {
    
    long old_pos = ftell(fp); // SAVE

    unsigned blocks_needed = (file_size + block_size - 1) / block_size;
    if (blocks_needed == 0) blocks_needed = 1;

    unsigned bitmap_byte_count = (total_blocks + 7) / 8;
    unsigned char *bitmap = (unsigned char *)calloc(1, bitmap_byte_count);

    long bitmap_offset = (long)bitmap_start * block_size;
    fseek(fp, bitmap_offset, SEEK_SET);
    fread(bitmap, 1, bitmap_byte_count, fp);

    // Best Fit
    int best_start = -1;
    unsigned int best_len = UINT_MAX;
    int current_start = -1;
    unsigned int current_len = 0;

    for (unsigned int i = 0; i < total_blocks; i++) {
        if (get_bit(bitmap, i) == 0) { // Livre
            if (current_start == -1) current_start = i;
            current_len++;
        } else { // Ocupado
            if (current_start != -1) {
                if (current_len >= blocks_needed && current_len < best_len) {
                    best_len = current_len;
                    best_start = current_start;
                    if (best_len == blocks_needed) break;
                }
                current_start = -1;
                current_len = 0;
            }
        }
    }
    if (current_start != -1 && current_len >= blocks_needed && current_len < best_len) 
        best_start = current_start;

    if (best_start != -1) {
        // Erro Crítico: Tentativa de alocar metadados
        if (best_start < bitmap_start) {
             printf("ERRO: Tentativa de alocar em área reservada (%d).\n", best_start);
             free(bitmap); fseek(fp, old_pos, SEEK_SET); return -1;
        }

        for (unsigned int i = 0; i < blocks_needed; i++) set_bit(bitmap, best_start + i);
        fseek(fp, bitmap_offset, SEEK_SET);
        fwrite(bitmap, 1, bitmap_byte_count, fp);
    }

    free(bitmap);
    fseek(fp, old_pos, SEEK_SET); // RESTORE
    return best_start;
}


void contiguous_dealloc(FILE *fp, unsigned start_block, unsigned length_in_blocks, 
                        unsigned bitmap_start, unsigned total_blocks, unsigned block_size) {
    
    long old_pos = ftell(fp);

    // Carrega o Bitmap
    unsigned bitmap_byte_count = (total_blocks + 7) / 8;
    unsigned char *bitmap = (unsigned char *)calloc(1, bitmap_byte_count);
    
    long bitmap_offset = (long)bitmap_start * block_size;
    fseek(fp, bitmap_offset, SEEK_SET);
    fread(bitmap, 1, bitmap_byte_count, fp);

    // Desliga os bits
    for (unsigned int i = 0; i < length_in_blocks; i++) {
        unset_bit(bitmap, start_block + i);
    }

    // Salva o Bitmap alterado no disco
    fseek(fp, bitmap_offset, SEEK_SET);
    fwrite(bitmap, 1, bitmap_byte_count, fp);

    printf("ROLLBACK: Blocos %u a %u desalocados com sucesso.\n", 
           start_block, start_block + length_in_blocks - 1);

    free(bitmap);
    fseek(fp, old_pos, SEEK_SET);
}

//  ATUALIZAR TAMANHO DO PAI 
void update_parent_size(FILE *fp, struct dir_entry *parent, unsigned block_size) {
    // Atualiza a struct em memória primeiro
    parent->size += ENTRY_SIZE; 
    
    long old_pos = ftell(fp);
    unsigned long parent_start_offset = (unsigned long)parent->start_block * block_size;
    struct dir_entry entry;

    // Atualizar PONTO (.) 
    // O ponto sempre reflete o estado atual do próprio diretório
    fseek(fp, parent_start_offset, SEEK_SET);
    fread(&entry, ENTRY_SIZE, 1, fp);

    if (strncmp(entry.file_name, ".", 1) == 0) {
        entry.size = parent->size; 
        
        // Grava de volta (Offset 0 do diretório)
        fseek(fp, parent_start_offset, SEEK_SET);
        fwrite(&entry, ENTRY_SIZE, 1, fp);
    }

    // Atualizar PONTO-PONTO (..) - Apenas se for Raiz ---
    // A entrada ".." é sempre a segunda (Offset 32)
    unsigned long dotdot_offset = parent_start_offset + ENTRY_SIZE;
    
    fseek(fp, dotdot_offset, SEEK_SET);
    fread(&entry, ENTRY_SIZE, 1, fp);

    if (strncmp(entry.file_name, "..", 2) == 0) {
        if (entry.start_block == parent->start_block) {
            entry.size = parent->size; 
            fseek(fp, dotdot_offset, SEEK_SET);
            fwrite(&entry, ENTRY_SIZE, 1, fp);
 
        }
    }

    // Restaura o ponteiro do arquivo para não quebrar o fluxo principal
    fseek(fp, old_pos, SEEK_SET);
}
// PREPARAR STRUCT
void prepare_dir_entry(struct dir_entry *entry, char *file_name, unsigned short file_type, 
                      unsigned size, unsigned start_block, unsigned block_size){
    memset(entry, 0, sizeof(struct dir_entry));
    entry->status = STATUS_VALID;
    strncpy(entry->file_name, file_name, 16);
    entry->file_type = file_type;
    entry->start_block = start_block;
    entry->size = size;
    entry->length = (block_size > 0) ? (size + block_size - 1) / block_size : 0;
}

// ADICIONAR AO PAI 
int add_entry_to_parent(FILE *fp, struct dir_entry *parent, struct dir_entry *new_entry, unsigned block_size) {
    struct dir_entry temp_entry;
    unsigned long parent_start_pos = (unsigned long)parent->start_block * block_size;
    unsigned int max_entries = (parent->length * block_size) / ENTRY_SIZE;

    for (unsigned int i = 0; i < max_entries; i++) {
        unsigned long current_pos = parent_start_pos + (i * ENTRY_SIZE);
        
        fseek(fp, current_pos, SEEK_SET);
        if (fread(&temp_entry, ENTRY_SIZE, 1, fp) != 1) break;

        if (temp_entry.status == STATUS_FREE) {
            fseek(fp, current_pos, SEEK_SET);
            fwrite(new_entry, ENTRY_SIZE, 1, fp);
            return 1; // Sucesso
        }
    }
    return 0; // Pai cheio
}

// CRIAR ARQUIVO E DIRETÓRIO 

void create_file(FILE *fp, struct dir_entry *parent_dir, struct superblock *sup, 
                 char *file_name, unsigned size, char *data) {
    
    unsigned real_block_size = (1 << sup->sector_size) << sup->block_size;
    unsigned blocks_needed = (size + real_block_size - 1) / real_block_size;
    if (blocks_needed == 0) blocks_needed = 1;
    long int file_start = contiguous_alloc(fp, size, real_block_size, sup->bitmap_start, sup->total_blocks);
    
    if (file_start == -1) {
        printf("Erro: Disco cheio p/ arquivo '%s'.\n", file_name);
        contiguous_dealloc(fp, file_start, blocks_needed, sup->bitmap_start, 
                           sup->total_blocks, real_block_size);
        return;
    }

    struct dir_entry new_entry;
    prepare_dir_entry(&new_entry, file_name, TYPE_FILE, size, file_start, real_block_size);

    if (add_entry_to_parent(fp, parent_dir, &new_entry, real_block_size)) {
        if (size > 0 && data != NULL) {
            fseek(fp, file_start * real_block_size, SEEK_SET);
            fwrite(data, 1, size, fp);
        }
        update_parent_size(fp, parent_dir, real_block_size);
        printf("Arquivo '%s' criado no bloco %ld.\n", file_name, file_start);
    } else {
        printf("Erro: Diretorio pai cheio.\n");
        contiguous_dealloc(fp, file_start, blocks_needed, sup->bitmap_start, 
                           sup->total_blocks, real_block_size);
    }
}

void create_dir(FILE *fp, struct dir_entry *parent_dir, struct superblock *sup, char *dir_name) {
    unsigned real_block_size = (1 << sup->sector_size) << sup->block_size;
    unsigned dir_initial_size = real_block_size; 

    long int dir_start = contiguous_alloc(fp, dir_initial_size, real_block_size, sup->bitmap_start, sup->total_blocks);
    
    if (dir_start == -1) {
        printf("Erro: Disco cheio p/ diretorio '%s'.\n", dir_name);
        return;
    }

    struct dir_entry new_entry;
    prepare_dir_entry(&new_entry, dir_name, TYPE_DIR, dir_initial_size, dir_start, real_block_size);

    if (add_entry_to_parent(fp, parent_dir, &new_entry, real_block_size)) {
        // Inicializa conteúdo com zeros
        unsigned char *zeros = calloc(1, real_block_size);
        fseek(fp, dir_start * real_block_size, SEEK_SET);
        fwrite(zeros, 1, real_block_size, fp);
        free(zeros);

        struct dir_entry dot, dotdot;
        prepare_dir_entry(&dot, ".", TYPE_DIR, dir_initial_size, dir_start, real_block_size);
        // ".." aponta para o pai com o tamanho ATUAL dele
        prepare_dir_entry(&dotdot, "..", TYPE_DIR, parent_dir->size, parent_dir->start_block, real_block_size);

        fseek(fp, dir_start * real_block_size, SEEK_SET);
        fwrite(&dot, ENTRY_SIZE, 1, fp);
        fwrite(&dotdot, ENTRY_SIZE, 1, fp);

        update_parent_size(fp, parent_dir, real_block_size);
        printf("Diretorio '%s' criado no bloco %ld.\n", dir_name, dir_start);
    }
}

// --- MAIN ---
int main() {

    //MONTAR O SISTEMA
    FILE *fp = fopen("sacs.img", "r+b");
    if (!fp) return 1;

    struct superblock sup;
    fseek(fp, 0, SEEK_SET);
    fread(&sup, sizeof(struct superblock), 1, fp);
    unsigned real_block_size = (1 << sup.sector_size) << sup.block_size;

    // PREPARAR DIRETÓRIO RAIZ (PAI)
    struct dir_entry root_dir;
    fseek(fp, sup.root_start * real_block_size, SEEK_SET);
    fread(&root_dir, ENTRY_SIZE, 1, fp);
    
    root_dir.start_block = sup.root_start; 
    root_dir.length = sup.root_size;

    // TESTES
    char *txt = "Ola Mundo SACS!";
    create_file(fp, &root_dir, &sup, "ola.txt", strlen(txt), txt);
    
    create_dir(fp, &root_dir, &sup, "fotos");
    
    create_file(fp, &root_dir, &sup, "log.txt", 10, "1234567890");

    fclose(fp);
    return 0;
}