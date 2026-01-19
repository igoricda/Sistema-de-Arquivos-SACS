#ifndef SACS_H
#define SACS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


// --- CONFIGURAÇÕES DO SACS ---
#define SACS 0x53414353
#define ENTRY_SIZE 32
#define TYPE_DIR 0x0002
#define TYPE_FILE 0x0003
#define STATUS_FREE 0
#define STATUS_VALID 1

// --- ESTRUTURAS ---

struct __attribute__((__packed__)) superblock {
    uint32_t sysid;           // 0
    uint16_t sector_size;     // 4
    uint32_t total_blocks;    // 10
    uint16_t block_size;      // 14
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
    uint32_t length; 
};

// --- PROTÓTIPOS DAS FUNÇÕES ---

// Auxiliares de bits
void set_bit(unsigned char *bitmap_buffer, int block_index);
int get_bit(unsigned char *bitmap, int index);
void unset_bit(unsigned char *bitmap_buffer, int block_index);

// Alocação e manipulação de disco
long int contiguous_alloc(FILE *fp, unsigned file_size, unsigned real_block_size, 
                          unsigned bitmap_start, unsigned total_blocks);
void contiguous_dealloc(FILE *fp, unsigned start_block, unsigned length_in_blocks, 
                        unsigned bitmap_start, unsigned real_block_size,
                        unsigned data_start);

// Manipulação de diretórios e arquivos
void update_hierarchy_size(FILE *fp, unsigned start_block, int delta, unsigned block_size);
int check_duplicate(FILE *fp, struct dir_entry *parent, char *name, unsigned block_size);
void prepare_dir_entry(struct dir_entry *entry, char *file_name, unsigned short file_type, 
                       unsigned size, unsigned start_block, unsigned block_size);
int add_entry_to_parent(FILE *fp, struct dir_entry *parent, struct dir_entry *new_entry, unsigned block_size);

// Operações do Sistema de Arquivos (API)
void create_file(FILE *fp, struct dir_entry *parent_dir, struct superblock *sup, 
                 char *file_name, unsigned size, char *data);
void create_dir(FILE *fp, struct dir_entry *parent_dir, struct superblock *sup, char *dir_name);
int delete_item(FILE *fp, struct dir_entry *parent, struct superblock *sup, char *name);
int change_directory(FILE *fp, struct dir_entry *current_dir, struct superblock *sup, char *target_name);
void import_file(FILE *fp_sacs, struct dir_entry *parent, struct superblock *sup, char *external_path);
void export_file(FILE *fp_sacs, struct dir_entry *parent, struct superblock *sup, 
                 char *sacs_filename, char *dest_path);
unsigned int get_real_dir_size(FILE *fp, unsigned int block_index, unsigned int block_size);
void list_recursive(FILE *fp, struct dir_entry *current_dir, struct superblock *sup, int level);

// Sistema e Formatação
void print_sup(struct superblock *sup);
void format_sacs(const char *filename, unsigned int sysid, unsigned sector_count, 
                 unsigned short sector_size, unsigned short block_size, unsigned int root_size);

#endif // SACS_H