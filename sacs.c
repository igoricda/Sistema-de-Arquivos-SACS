#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "sacs.h"


// --- FUNÇÕES AUXILIARES DE BITS ---

void set_bit(unsigned char *bitmap_buffer, int block_index) {
    bitmap_buffer[block_index / 8] |= (1 << (block_index % 8));
}

int get_bit(unsigned char *bitmap, int index) {
    return (bitmap[index / 8] >> (index % 8)) & 1;
}

void unset_bit(unsigned char *bitmap_buffer, int block_index) {
    int byte_offset = block_index / 8;
    int bit_offset  = block_index % 8;
    bitmap_buffer[byte_offset] &= ~(1 << bit_offset);
}


// ALOCAÇÃO 
long int contiguous_alloc(FILE *fp, unsigned file_size, unsigned real_block_size, 
                          unsigned bitmap_start, unsigned total_blocks) {
    long old_pos = ftell(fp);

    unsigned blocks_needed = (file_size + real_block_size - 1) / real_block_size;
    if (blocks_needed == 0) blocks_needed = 1;
//
    unsigned long bitmap_start_offset = (unsigned long)bitmap_start * real_block_size;
    unsigned long total_bitmap_bytes = (total_blocks + 7) / 8;


    unsigned int chunk_size_bytes = real_block_size;
    unsigned int bits_per_chunk = chunk_size_bytes * 8;
    
    unsigned char *chunk = (unsigned char *)malloc(chunk_size_bytes);
    if (!chunk) { return -1; }

    // --- FASE 1: SCAN ---
    int best_start = -1;
    unsigned int best_len = UINT_MAX; 
    
    int current_start = -1;
    unsigned int current_len = 0;
    
    unsigned int global_bit_index = 0;
    unsigned long bytes_processed = 0;
    int search_complete = 0;

    fseek(fp, bitmap_start_offset, SEEK_SET);

    while (bytes_processed < total_bitmap_bytes) {
        // Lê 1 bloco de bitmap por vez (ou o resto)
        size_t read_size = chunk_size_bytes;
        if (total_bitmap_bytes - bytes_processed < read_size) {
            read_size = total_bitmap_bytes - bytes_processed;
        }
        
        memset(chunk, 0, chunk_size_bytes); 
        fread(chunk, 1, read_size, fp);

        int bits_to_check = read_size * 8;
        
        for (int local_bit = 0; local_bit < bits_to_check; local_bit++) {
            if (global_bit_index >= total_blocks) break;

            if (!get_bit(chunk, local_bit)) { // Livre
                if (current_start == -1) current_start = global_bit_index;
                current_len++;
            } else { // Ocupado
                if (current_start != -1) {
                    if (current_len >= blocks_needed) {
                        if (current_len < best_len) {
                            best_len = current_len;
                            best_start = current_start;
                            if (best_len == blocks_needed) {
                                search_complete = 1;
                                break;
                            }
                        }
                    }
                    current_start = -1;
                    current_len = 0;
                }
            }
            global_bit_index++;
        }
        if (search_complete) break;
        bytes_processed += read_size;
    }
    
    if (!search_complete && current_start != -1 && current_len >= blocks_needed) {
        if (current_len < best_len) best_start = current_start;
    }

    // --- FASE 2: COMMIT ---
    if (best_start != -1) {
        if (best_start == 0) {
             printf("ERRO: Tentativa de alocar Superbloco.\n");
             free(chunk); fseek(fp, old_pos, SEEK_SET); return -1;
        }

        unsigned int start_bit = best_start;
        unsigned int end_bit = best_start + blocks_needed - 1;

        unsigned long start_chunk_idx = start_bit / bits_per_chunk;
        unsigned long end_chunk_idx = end_bit / bits_per_chunk;

        for (unsigned long c = start_chunk_idx; c <= end_chunk_idx; c++) {
            unsigned long chunk_offset_disk = bitmap_start_offset + (c * chunk_size_bytes);
            
            fseek(fp, chunk_offset_disk, SEEK_SET);
            memset(chunk, 0, chunk_size_bytes);
            fread(chunk, 1, chunk_size_bytes, fp);

            unsigned int chunk_start_global = c * bits_per_chunk;
            unsigned int chunk_end_global = (c + 1) * bits_per_chunk - 1;

            unsigned int mark_start = (start_bit > chunk_start_global) ? start_bit : chunk_start_global;
            unsigned int mark_end = (end_bit < chunk_end_global) ? end_bit : chunk_end_global;

            unsigned int local_start = mark_start % bits_per_chunk;
            unsigned int local_end = mark_end % bits_per_chunk;

            for (unsigned int i = local_start; i <= local_end; i++) {
                set_bit(chunk, i);
            }

            fseek(fp, chunk_offset_disk, SEEK_SET);
            fwrite(chunk, 1, chunk_size_bytes, fp);
        }
    }

    free(chunk); 
    fseek(fp, old_pos, SEEK_SET);
    return best_start;
}

//Atualiza quantidade de bytes nas pastas acima na hierarquia (igual windows)
void update_hierarchy_size(FILE *fp, unsigned start_block, int delta, unsigned block_size) {
    long old_pos = ftell(fp);
    unsigned current_block = start_block;
    
    // Loop para subir a árvore até a raiz
    while (1) {
        unsigned long current_offset = (unsigned long)current_block * block_size;
        struct dir_entry dot, dotdot;

        // Atualiza o . do diretório atual 
        fseek(fp, current_offset, SEEK_SET);
        fread(&dot, ENTRY_SIZE, 1, fp); // Lê entrada 0 (.)
        
        int new_size = (int)dot.size + delta;
        if (new_size < 0) new_size = 0; // Proteção contra valor negativo
        dot.size = (unsigned)new_size;

        fseek(fp, current_offset, SEEK_SET);
        fwrite(&dot, ENTRY_SIZE, 1, fp); // Grava . atualizado

        // Lê o PAI ..
        fseek(fp, current_offset + ENTRY_SIZE, SEEK_SET); // Lê entrada 1 (..)
        fread(&dotdot, ENTRY_SIZE, 1, fp);

        // Se raiz . == ..
        if (dotdot.start_block == current_block) {
            // Atualiza o ".." da raiz também para ficar igual ao "."
            dotdot.size = dot.size;
            fseek(fp, current_offset + ENTRY_SIZE, SEEK_SET);
            fwrite(&dotdot, ENTRY_SIZE, 1, fp);
            break; 
        }

        // Atualiza a entrada que representa ESTE diretório no PAI 
        unsigned parent_block = dotdot.start_block;
        unsigned long parent_offset = (unsigned long)parent_block * block_size;
        
        struct dir_entry temp;

        // Precisamos varrer o pai para encontrar a entrada que tem 'current_block'
        fseek(fp, parent_offset, SEEK_SET);
        fread(&temp, ENTRY_SIZE, 1, fp); 
        unsigned int max = (temp.length * block_size) / ENTRY_SIZE;

        int found = 0;
        for(unsigned int i=0; i < max; i++) {
            unsigned long entry_addr = parent_offset + (i * ENTRY_SIZE);
            fseek(fp, entry_addr, SEEK_SET);
            fread(&temp, ENTRY_SIZE, 1, fp);
            
            // Se esta entrada aponta para o diretório que acabamos de atualizar
            if (temp.status == 1 && temp.start_block == current_block) {
                temp.size = dot.size; // Copia o tamanho acumulado do filho
                fseek(fp, entry_addr, SEEK_SET);
                fwrite(&temp, ENTRY_SIZE, 1, fp);
                found = 1;
                break; 
            }
        }

        if (!found) {
            printf("DEBUG: Erro de consistência. Não achei o filho %u no pai %u.\n", current_block, parent_block);
            break;
        }

        // Sobe um nível
        current_block = parent_block;
    }

    fseek(fp, old_pos, SEEK_SET);
}


// Retorna 1 se já existe, 0 se não existe
int check_duplicate(FILE *fp, struct dir_entry *parent, char *name, unsigned block_size) {
    struct dir_entry temp_entry;
    unsigned long parent_start_pos = (unsigned long)parent->start_block * block_size;
    unsigned int max_entries = (parent->length * block_size) / ENTRY_SIZE;

    long old_pos = ftell(fp);

    for (unsigned int i = 0; i < max_entries; i++) {
        unsigned long current_pos = parent_start_pos + (i * ENTRY_SIZE);
        
        fseek(fp, current_pos, SEEK_SET);
        if (fread(&temp_entry, ENTRY_SIZE, 1, fp) != 1) break;

        // Verifica apenas arquivos VÁLIDOS 
        if (temp_entry.status == STATUS_VALID) {
            // Compara os nomes
            if (strncmp(temp_entry.file_name, name, 16) == 0) {
                fseek(fp, old_pos, SEEK_SET); // Restaura posição
                return 1; // Encontrou duplicata
            }
        }
    }

    fseek(fp, old_pos, SEEK_SET); // Restaura posição
    return 0; // Não encontrou
}


// DESALOCAR
void contiguous_dealloc(FILE *fp, unsigned start_block, unsigned length_in_blocks, 
                        unsigned bitmap_start, unsigned real_block_size,
                        unsigned data_start) {
    
    // Segurança
    if (start_block < data_start) {
        printf("ERRO CRÍTICO: Tentativa de desalocar metadados (%u).\n", start_block);
        return;
    }
    if (length_in_blocks == 0) return;

    long old_pos = ftell(fp);
    unsigned long bitmap_start_offset = (unsigned long)bitmap_start * real_block_size;

    // Chunk dinamico
    unsigned int chunk_size_bytes = real_block_size;
    unsigned int bits_per_chunk = chunk_size_bytes * 8;

    // Aloca buffer dinamicamente
    unsigned char *chunk = (unsigned char *)malloc(chunk_size_bytes);
    if (!chunk) {
        printf("Erro de memória no dealloc.\n");
        fseek(fp, old_pos, SEEK_SET);
        return;
    }

    // Intervalo Global
    unsigned int start_bit = start_block;
    unsigned int end_bit = start_block + length_in_blocks - 1;

    // Identificar quais "Blocos de Bitmap" afetam a operação
    unsigned long start_chunk_idx = start_bit / bits_per_chunk;
    unsigned long end_chunk_idx = end_bit / bits_per_chunk;

    // Iterar sobre os chunks
    for (unsigned long c = start_chunk_idx; c <= end_chunk_idx; c++) {
        
        unsigned long chunk_offset_disk = bitmap_start_offset + (c * chunk_size_bytes);
        
        // Carrega o chunk 
        fseek(fp, chunk_offset_disk, SEEK_SET);
        memset(chunk, 0, chunk_size_bytes);
        fread(chunk, 1, chunk_size_bytes, fp);

        // Define limites Globais deste chunk
        unsigned int chunk_start_global = c * bits_per_chunk;
        unsigned int chunk_end_global = (c + 1) * bits_per_chunk - 1;

        // Interseção
        unsigned int mark_start = (start_bit > chunk_start_global) ? start_bit : chunk_start_global;
        unsigned int mark_end = (end_bit < chunk_end_global) ? end_bit : chunk_end_global;

        // Coordenadas Locais
        unsigned int local_start = mark_start % bits_per_chunk;
        unsigned int local_end = mark_end % bits_per_chunk;

        // Modifica
        for (unsigned int i = local_start; i <= local_end; i++) {
            unset_bit(chunk, i);
        }

        // Salva
        fseek(fp, chunk_offset_disk, SEEK_SET);
        fwrite(chunk, 1, chunk_size_bytes, fp);
    }

    free(chunk); 
    fflush(fp);
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

    if (check_duplicate(fp, parent_dir, file_name, real_block_size)) {
        printf("Erro: O arquivo '%s' ja existe neste diretorio.\n", file_name);
        return; // Aborta imediatamente
    }

    unsigned blocks_needed = (size + real_block_size - 1) / real_block_size;
    if (blocks_needed == 0) blocks_needed = 1;
    long int file_start = contiguous_alloc(fp, size, real_block_size, sup->bitmap_start, sup->total_blocks);
    
    if (file_start == -1) {
        printf("Erro: Disco cheio p/ arquivo '%s'.\n", file_name);
        contiguous_dealloc(fp, file_start, blocks_needed, sup->bitmap_start, 
                            real_block_size, sup->data_start);
        return;
    }

    struct dir_entry new_entry;
    prepare_dir_entry(&new_entry, file_name, TYPE_FILE, size, file_start, real_block_size);

    if (add_entry_to_parent(fp, parent_dir, &new_entry, real_block_size)) {
        if (size > 0 && data != NULL) {
            fseek(fp, file_start * real_block_size, SEEK_SET);
            fwrite(data, 1, size, fp);
        }
        update_hierarchy_size(fp, parent_dir->start_block, (int)size, real_block_size);
        parent_dir->size += size;
        printf("Arquivo '%s' criado no bloco %ld.\n", file_name, file_start);
    } else {
        printf("Erro: Diretorio pai cheio.\n");
        contiguous_dealloc(fp, file_start, blocks_needed, sup->bitmap_start, 
                            real_block_size, sup->data_start);
    }
}

void create_dir(FILE *fp, struct dir_entry *parent_dir, struct superblock *sup, char *dir_name) {
    
    unsigned real_block_size = (1 << sup->sector_size) << sup->block_size;

    // Nao permitir arquivos/diretorios de nomes iguais
    if (check_duplicate(fp, parent_dir, dir_name, real_block_size)) {
        printf("Erro: O diretorio/arquivo '%s' ja existe.\n", dir_name);
        return;
    }

    // Tamanho Lógico: Apenas . e .. 
    unsigned dir_logical_size = ENTRY_SIZE * 2; 
    
    // Tamanho Físico para alocar: 1 Bloco inteiro
    unsigned alloc_size = real_block_size; 

    // Aloca 
    long int dir_start = contiguous_alloc(fp, alloc_size, real_block_size, sup->bitmap_start, sup->total_blocks);
    
    if (dir_start == -1) {
        printf("Erro: Espaço insuficiente.\n");
        return;
    }

    // Prepara entrada com tamanho logico
    struct dir_entry new_dir_entry;
    prepare_dir_entry(&new_dir_entry, dir_name, TYPE_DIR, dir_logical_size, dir_start, real_block_size);

    // Adiciona ao pai
    if (add_entry_to_parent(fp, parent_dir, &new_dir_entry, real_block_size)) {
        
        // Inicializa o conteúdo do novo diretório
        unsigned char *zeros = calloc(1, real_block_size);
        fseek(fp, dir_start * real_block_size, SEEK_SET);
        fwrite(zeros, 1, real_block_size, fp);
        free(zeros);

        struct dir_entry dot, dotdot;
        
        // Ponto (.): Tamanho lógico inicial (64)
        prepare_dir_entry(&dot, ".", TYPE_DIR, dir_logical_size, dir_start, real_block_size);
        
        // Ponto-Ponto (..): Aponta para o pai (tamanho atual do pai)
        prepare_dir_entry(&dotdot, "..", TYPE_DIR, parent_dir->size, parent_dir->start_block, real_block_size);

        fseek(fp, dir_start * real_block_size, SEEK_SET);
        fwrite(&dot, ENTRY_SIZE, 1, fp);
        fwrite(&dotdot, ENTRY_SIZE, 1, fp);

        // Atualização em cascata
        update_hierarchy_size(fp, parent_dir->start_block, (int)dir_logical_size, real_block_size);
        
        // Atualiza memória local
        parent_dir->size += dir_logical_size;
        
        printf("Diretório '%s' criado (Bloco %ld, Tamanho %u).\n", dir_name, dir_start, dir_logical_size);
    } else {
        // Rollback
        printf("Erro: Diretório pai cheio. Revertendo...\n");
        contiguous_dealloc(fp, dir_start, 1, sup->bitmap_start, real_block_size, sup->data_start);
    }
}

// Remover arquivo/diretorio
int delete_item(FILE *fp, struct dir_entry *parent, struct superblock *sup, char *name) {
    unsigned int real_block_size = (1 << sup->sector_size) << sup->block_size;
    struct dir_entry temp_entry;
    
    // Cálculo da área de dados do diretório pai
    unsigned long parent_start_pos = (unsigned long)parent->start_block * real_block_size;
    unsigned int max_entries = (parent->length * real_block_size) / ENTRY_SIZE;

    long old_pos = ftell(fp); 
    int found_index = -1;

    // Procurar o arquivo pelo nome
    for (unsigned int i = 0; i < max_entries; i++) {
        unsigned long current_pos = parent_start_pos + (i * ENTRY_SIZE);
        
        fseek(fp, current_pos, SEEK_SET);
        if (fread(&temp_entry, ENTRY_SIZE, 1, fp) != 1) break;

        // Verifica se é válido e se o nome bate
        if (temp_entry.status == STATUS_VALID) {
            if (strncmp(temp_entry.file_name, name, 16) == 0) {
                found_index = i;
                break; 
            }
        }
    }

    if (found_index == -1) {
        printf("Erro: Arquivo '%s' não encontrado.\n", name);
        fseek(fp, old_pos, SEEK_SET);
        return 0;
    }

    // Validações de Segurança
    if (strcmp(temp_entry.file_name, ".") == 0 || strcmp(temp_entry.file_name, "..") == 0) {
        printf("Erro: Não é possível deletar '.' ou '..'.\n");
        fseek(fp, old_pos, SEEK_SET);
        return 0;
    }

    //Nao deve ser possivel deletar o diretorio raiz
    if (temp_entry.start_block == sup->root_start) {
        printf("ERRO CRÍTICO: Não é possível deletar o Diretório Raiz.\n");
        fseek(fp, old_pos, SEEK_SET);
        return 0;
    }

    // Se for diretório, verificar se está vazio
    if (temp_entry.file_type == TYPE_DIR) {
        if (temp_entry.size > (ENTRY_SIZE * 2)) {
            printf("Erro: O diretório '%s' não está vazio.\n", name);
            fseek(fp, old_pos, SEEK_SET);
            return 0;
        }
    }

    unsigned int size_to_remove = temp_entry.size;

    // Desalocar os blocos no Bitmap
    contiguous_dealloc(fp, temp_entry.start_block, temp_entry.length, 
                       sup->bitmap_start, real_block_size, sup->data_start);

    // Marcar a entrada como LIVRE
    temp_entry.status = STATUS_FREE; 
    
    unsigned long entry_pos = parent_start_pos + (found_index * ENTRY_SIZE);
    fseek(fp, entry_pos, SEEK_SET);
    fwrite(&temp_entry, ENTRY_SIZE, 1, fp);

    update_hierarchy_size(fp, parent->start_block, -(int)size_to_remove, real_block_size);

    // Atualizar o tamanho do Pai
    if (parent->size >= size_to_remove) parent->size -= size_to_remove;
    else parent->size = 0;

    printf("Sucesso: '%s' foi deletado e os blocos liberados.\n", name);
    
    fseek(fp, old_pos, SEEK_SET); 
    return 1;
}

// cd
int change_directory(FILE *fp, struct dir_entry *current_dir, struct superblock *sup, char *target_name) {
    unsigned int real_block_size = (1 << sup->sector_size) << sup->block_size;
    struct dir_entry entry;
    
    unsigned long parent_start = (unsigned long)current_dir->start_block * real_block_size;
    unsigned int max_entries = (current_dir->length * real_block_size) / ENTRY_SIZE;
    
    long old_pos = ftell(fp);
    int found = 0;

    // Procura o diretório alvo na pasta atual
    for (unsigned int i = 0; i < max_entries; i++) {
        fseek(fp, parent_start + (i * ENTRY_SIZE), SEEK_SET);
        if (fread(&entry, ENTRY_SIZE, 1, fp) != 1) break;

        if (entry.status == 1 && strncmp(entry.file_name, target_name, 16) == 0) {
            if (entry.file_type == TYPE_DIR) {
                found = 1;
                break;
            } else {
                printf("Erro: '%s' e um arquivo, nao um diretorio.\n", target_name);
                fseek(fp, old_pos, SEEK_SET);
                return 0;
            }
        }
    }

    if (!found) {
        printf("Erro: Diretorio '%s' nao encontrado.\n", target_name);
        fseek(fp, old_pos, SEEK_SET);
        return 0;
    }

    // Entra no diretório e le o .
    unsigned long target_block_addr = (unsigned long)entry.start_block * real_block_size;
    fseek(fp, target_block_addr, SEEK_SET);
    fread(current_dir, ENTRY_SIZE, 1, fp);

    // Mostrar o nome da pasta que está no printf
    if (strcmp(target_name, ".") != 0 && strcmp(target_name, "..") != 0) {
        strncpy(current_dir->file_name, target_name, 16);
    }

    printf("Mudou para diretorio: %s (Bloco %u)\n", current_dir->file_name, current_dir->start_block);
    
    fseek(fp, old_pos, SEEK_SET);
    return 1;
}

void import_file(FILE *fp_sacs, struct dir_entry *parent, struct superblock *sup, char *external_path) {
    unsigned int real_block_size = (1 << sup->sector_size) << sup->block_size;

    // Abrir arquivo externo 
    FILE *f_ext = fopen(external_path, "rb");
    if (!f_ext) {
        printf("Erro: Arquivo externo '%s' nao encontrado.\n", external_path);
        return;
    }

    // Descobrir tamanho do arquivo externo
    fseek(f_ext, 0, SEEK_END);
    unsigned long file_size = ftell(f_ext);
    fseek(f_ext, 0, SEEK_SET); // Volta para o início

    // Extrair apenas o nome do arquivo (remove o caminho /home/user/...)
    char *filename = strrchr(external_path, '/');
    if (filename) filename++; // Pula a barra
    else filename = external_path;

    // Verificação de Duplicata
    if (check_duplicate(fp_sacs, parent, filename, real_block_size)) {
        printf("Erro: O arquivo '%s' ja existe na pasta de destino.\n", filename);
        fclose(f_ext);
        return;
    }

    // Alocar espaço no Bitmap
    long int sacs_start_block = contiguous_alloc(fp_sacs, file_size, real_block_size, 
                                                 sup->bitmap_start, sup->total_blocks);
    
    if (sacs_start_block == -1) {
        printf("Erro: Espaço insuficiente no disco para %lu bytes.\n", file_size);
        fclose(f_ext);
        return;
    }

    // Preparar e Adicionar a Entrada no Diretório Pai
    struct dir_entry new_entry;
    prepare_dir_entry(&new_entry, filename, TYPE_FILE, file_size, sacs_start_block, real_block_size); 

    if (!add_entry_to_parent(fp_sacs, parent, &new_entry, real_block_size)) {
        printf("Erro: Diretório cheio (limite de arquivos atingido). Revertendo...\n");
        
        // Rollback: Libera os blocos que acabamos de alocar
        unsigned blocks_needed = (file_size + real_block_size - 1) / real_block_size;
        contiguous_dealloc(fp_sacs, sacs_start_block, blocks_needed, 
                           sup->bitmap_start, real_block_size, sup->data_start);
        fclose(f_ext);
        return;
    }

    // Escrever os Dados
    unsigned char *buffer = malloc(real_block_size);
    if (!buffer) {
        printf("Erro fatal de memória RAM.\n");
        fclose(f_ext);
        return; 
    }

    unsigned long bytes_remaining = file_size;
    unsigned long offset = 0;

    printf("Importando '%s' para o Bloco %ld...", filename, sacs_start_block);
    
    while (bytes_remaining > 0) {
        // Lê o que der
        size_t chunk_size = (bytes_remaining < real_block_size) ? bytes_remaining : real_block_size;
        
        // Lê da fonte
        fread(buffer, 1, chunk_size, f_ext);

        // Calcula posição no SACS e escreve
        unsigned long sacs_write_pos = ((unsigned long)sacs_start_block * real_block_size) + offset;
        fseek(fp_sacs, sacs_write_pos, SEEK_SET);
        fwrite(buffer, 1, chunk_size, fp_sacs);

        bytes_remaining -= chunk_size;
        offset += chunk_size;
    }

    free(buffer);
    fclose(f_ext);

    // Atualização de tamanho em cascata
    update_hierarchy_size(fp_sacs, parent->start_block, (int)file_size, real_block_size);
    
    // Atualiza a estrutura local na memória
    parent->size += file_size; 

    printf(" Sucesso! (%lu bytes adicionados a hierarquia)\n", file_size);
}

void export_file(FILE *fp_sacs, struct dir_entry *parent, struct superblock *sup, 
                 char *sacs_filename, char *dest_path) {
    
    unsigned int real_block_size = (1 << sup->sector_size) << sup->block_size;
    struct dir_entry entry;
    int found = 0;

    // Localizar arquivo no SACS
    unsigned long parent_start = (unsigned long)parent->start_block * real_block_size;
    unsigned int max_entries = (parent->length * real_block_size) / ENTRY_SIZE;

    long old_pos = ftell(fp_sacs); // SAVE

    for (unsigned int i = 0; i < max_entries; i++) {
        fseek(fp_sacs, parent_start + (i * ENTRY_SIZE), SEEK_SET);
        if (fread(&entry, ENTRY_SIZE, 1, fp_sacs) != 1) break;

        if (entry.status == STATUS_VALID && strncmp(entry.file_name, sacs_filename, 16) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("Erro: Arquivo '%s' nao encontrado no SACS.\n", sacs_filename);
        fseek(fp_sacs, old_pos, SEEK_SET);
        return;
    }
    
    if (entry.file_type == TYPE_DIR) {
        printf("Erro: '%s' e um diretorio.\n", sacs_filename);
        fseek(fp_sacs, old_pos, SEEK_SET);
        return;
    }

    // Preparar Destino
    FILE *f_out = fopen(dest_path, "wb");
    if (!f_out) {
        perror("Erro ao criar arquivo de destino");
        fseek(fp_sacs, old_pos, SEEK_SET);
        return;
    }

    // CÓPIA EM CHUNKS
    unsigned char *buffer = malloc(real_block_size);
    if (!buffer) { fclose(f_out); fseek(fp_sacs, old_pos, SEEK_SET); return; }

    unsigned long bytes_remaining = entry.size;
    unsigned long offset = 0;

    printf("Exportando '%s' para '%s'...", sacs_filename, dest_path);

    while (bytes_remaining > 0) {
        size_t chunk_size = (bytes_remaining < real_block_size) ? bytes_remaining : real_block_size;

        // Calcula posição de leitura no SACS
        unsigned long sacs_read_pos = ((unsigned long)entry.start_block * real_block_size) + offset;

        // Lê do SACS
        fseek(fp_sacs, sacs_read_pos, SEEK_SET);
        fread(buffer, 1, chunk_size, fp_sacs);

        // Escreve no destino
        fwrite(buffer, 1, chunk_size, f_out);

        bytes_remaining -= chunk_size;
        offset += chunk_size;
    }

    free(buffer);
    fclose(f_out);
    fseek(fp_sacs, old_pos, SEEK_SET); // RESTORE
    
    printf(" Concluido!\n");
}

// Função auxiliar para ler o tamanho real de um diretório alvo
unsigned int get_real_dir_size(FILE *fp, unsigned int block_index, unsigned int block_size) {
    struct dir_entry target_dot;
    long old_pos = ftell(fp);
    
    // Vai até o bloco do diretório e lê a primeira entrada "."
    fseek(fp, (unsigned long)block_index * block_size, SEEK_SET);
    fread(&target_dot, sizeof(struct dir_entry), 1, fp);
    
    fseek(fp, old_pos, SEEK_SET); // Restaura posição
    return target_dot.size;
}

// Listar os arquivos no FS a partir do diretório atual
void list_recursive(FILE *fp, struct dir_entry *current_dir, struct superblock *sup, int level) {
    unsigned int real_block_size = (1 << sup->sector_size) << sup->block_size;
    struct dir_entry entry;
    
    unsigned long dir_start_pos = (unsigned long)current_dir->start_block * real_block_size;
    unsigned int max_entries = (current_dir->length * real_block_size) / ENTRY_SIZE; 

    char indent[50] = "";
    for(int k=0; k<level; k++) strcat(indent, "   |");

    for (unsigned int i = 0; i < max_entries; i++) {
        fseek(fp, dir_start_pos + (i * ENTRY_SIZE), SEEK_SET);
        if (fread(&entry, ENTRY_SIZE, 1, fp) != 1) break;

        if (entry.status == STATUS_VALID) {             
            unsigned int display_size = entry.size;

            // Se for "..", ignora o tamanho gravado e busca o tamanho real do pai
            if (strncmp(entry.file_name, "..", 2) == 0) {
                 display_size = get_real_dir_size(fp, entry.start_block, real_block_size);
            }

            char type_char = (entry.file_type == TYPE_DIR) ? 'D' : 'F'; 
            printf("%s-- [%c] %s (%u bytes)\n", indent, type_char, entry.file_name, display_size);

            // Recursão
            if (entry.file_type == TYPE_DIR && strcmp(entry.file_name, ".") != 0 && strcmp(entry.file_name, "..") != 0) {
                long parent_pos = ftell(fp);
                list_recursive(fp, &entry, sup, level + 1);
                fseek(fp, parent_pos, SEEK_SET);
            }
        }
    }
}

// Printar Superbloco
void print_sup(struct superblock *sup){
    
    printf("Sysid = %u\n", sup->sysid);
    printf("Sector_size = %hu = %u \n", sup->sector_size, 1 << (sup->sector_size));
    printf("Total Blocks = %u\n", sup->total_blocks);
    printf("Block size = %hu = %u\n", sup->block_size, (1 << (sup->sector_size)) << sup->block_size);
    printf("Bitmap Start = %u\n", sup->bitmap_start);
    printf("Bitmap Size = %u\n", sup->bitmap_size);
    printf("Root Start = %u\n", sup->root_start);
    printf("Root Size = %u\n", sup->root_size);
    printf("Data Start = %u\n", sup->data_start);
}


// Formatador
void format_sacs(const char *filename, unsigned int sysid, unsigned sector_count, unsigned short sector_size, unsigned short block_size, unsigned int root_size){
    
    printf("--- FORMATANDO %s ---\n", filename);
    FILE *fp = fopen(filename, "wb"); // "wb" cria ou sobrescreve
    if (!fp) { perror("Erro ao abrir dispositivo/arquivo"); exit(1); }

    struct superblock sup;
    memset(&sup, 0, sizeof(struct superblock));

    sup.sysid = sysid;
    sup.sector_size = sector_size;
    sup.block_size = block_size;
    unsigned int real_block_size = (1 << (sup.sector_size)) << sup.block_size;
    sup.total_blocks = (sector_count + ((1 << sup.block_size)-1)) / (1 << sup.block_size);  
    sup.bitmap_start = 1;
    unsigned int bits_needed = sup.total_blocks;
    unsigned int bytes_needed = (bits_needed + 7) / 8;
    sup.bitmap_size = (bytes_needed + real_block_size - 1) / real_block_size;
    if (sup.bitmap_size == 0) sup.bitmap_size = 1;
    sup.root_start = sup.bitmap_start + sup.bitmap_size;
    sup.root_size = root_size;
    sup.data_start = sup.root_start + sup.root_size;  
    print_sup(&sup);
    printf("Size of SuperBlock = %lu\nSize of DirEntry = %lu\n",
            sizeof(struct superblock), sizeof(struct dir_entry));


    unsigned char *buffer = calloc(1, real_block_size);
    memcpy(buffer, &sup, sizeof(struct superblock));
    fwrite(buffer, real_block_size , 1, fp);

    memset(buffer, 0, real_block_size);

   // --- INICIALIZAR BITMAP ---
    printf("Inicializando Bitmap (%u blocos)...\n", sup.bitmap_size);

    unsigned long bitmap_offset_bytes = (unsigned long)sup.bitmap_start * real_block_size;
    unsigned long total_bitmap_bytes_on_disk = (unsigned long)sup.bitmap_size * real_block_size;
    
    // Zerar a área do Bitmap
    fseek(fp, bitmap_offset_bytes, SEEK_SET);
    unsigned long bytes_written = 0;

    // Escreve de 1 em 1 bloco
    while (bytes_written < total_bitmap_bytes_on_disk) {
        memset(buffer, 0, real_block_size); 
        fwrite(buffer, 1, real_block_size, fp);
        
        bytes_written += real_block_size;
    }

    // Marcar Metadados (Superbloco + Bitmap + Raiz)
    // Lê o primeiro bloco do bitmap de volta
    fseek(fp, bitmap_offset_bytes, SEEK_SET);
    fread(buffer, 1, real_block_size, fp);

    for (unsigned int i = 0; i < sup.data_start; i++) {
        set_bit(buffer, i);
    }

    // Grava de volta o primeiro bloco do bitmap atualizado
    fseek(fp, bitmap_offset_bytes, SEEK_SET);
    fwrite(buffer, 1, real_block_size, fp);
    memset(buffer, 0, real_block_size);
    // --- DIRETORIO RAIZ ---
    // Preencher com zeros
    for (unsigned int i = 0; i < sup.root_size; i++) {
        fwrite(buffer, real_block_size, 1, fp);
    }
    
    // Gravar pastas . e .. no diretorio raiz
    struct dir_entry dot, dotdot;
    memset(&dot, 0, sizeof(dot));
    dot.status = STATUS_VALID;
    strcpy(dot.file_name, ".");
    dot.file_type = TYPE_DIR;
    dot.start_block = sup.root_start;
    dot.size = ENTRY_SIZE * 2;
    dot.length = sup.root_size;

    dotdot = dot;
    strcpy(dotdot.file_name, "..");

    fseek(fp, sup.root_start * real_block_size, SEEK_SET);
    fwrite(&dot, ENTRY_SIZE, 1, fp);
    fwrite(&dotdot, ENTRY_SIZE, 1, fp);

    // Preencher Dados com zeros
    unsigned int data_blocks = sup.total_blocks - sup.data_start;
    fseek(fp, sup.data_start * real_block_size, SEEK_SET);
    for(unsigned int i=0; i < data_blocks; i++){
        fwrite(buffer, 1, real_block_size, fp);
        printf("Write datablocks\n");
    }

    memset(buffer, 0, real_block_size);
    
    printf("Disco formatado com sucesso! (Root Start: %d | Data Start: %d)\n\n", sup.root_start, sup.data_start);

    fclose(fp);
    free(buffer);
    printf("Arquivo %s criado com sucesso!\n", filename);
}

