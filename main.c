#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sacs.h"

// MAIN
int main() {

    char device_path[100];
    int opcao;
    
    printf("SACS - Sistema de Arquivos\n");
    printf("Dispositivo: ");
    scanf("%99s", device_path);

    // Abre uma vez para validar e carregar a Raiz
    FILE *fp = fopen(device_path, "r+b");
    while (!fp) {
        int sub_opt;
        printf("\nERRO: Falha ao abrir '%s'. O arquivo nao existe ou esta bloqueado.\n", device_path);
        printf("1. Digitar outro caminho\n");
        printf("2. Formatar/Criar este dispositivo agora\n");
        printf("0. Sair do programa\n");
        printf("Escolha: ");
        scanf("%d", &sub_opt);

        if (sub_opt == 0) {
            printf("Saindo...\n");
            return 0;
        } 
        else if (sub_opt == 1) {
            printf("Novo caminho: ");
            scanf("%99s", device_path);
            // Tenta abrir o novo caminho
            fp = fopen(device_path, "r+b");
        } 
        else if (sub_opt == 2) {
            unsigned int setores;
            unsigned short block_size;
            unsigned int root_size;
            printf("Setores (ex: 2048): ");
            scanf("%u", &setores);
            printf("Tamanho dos blocos em relação aos setores (ex: 2 = Setores ^ 2 ^ 2): ");
            scanf("%hu", &block_size);
            printf("Quantidade de blocos no diretório raiz: ");
            scanf("%u", &root_size);
            format_sacs(device_path, SACS, setores, 9, block_size, root_size);
            // Tenta abrir novamente agora que o arquivo existe
            fp = fopen(device_path, "r+b");
            
            if (fp) {
                printf("Dispositivo formatado e montado com sucesso!\n");
                
            }
        } 
        else {
            printf("Opcao invalida.\n");
        }
    }
    struct superblock sup;
    struct dir_entry current_dir; // Mantém o estado da pasta atual
    unsigned int real_block_size = 0;

    // Se o arquivo abriu, carregamos o Superbloco e vamos para a Raiz
    if (fp) {
        fseek(fp, 0, SEEK_SET);
        fread(&sup, sizeof(struct superblock), 1, fp);
        real_block_size = (1 << sup.sector_size) << sup.block_size;
        
        // Carrega Raiz inicialmente
        fseek(fp, sup.root_start * real_block_size, SEEK_SET);
        fread(&current_dir, ENTRY_SIZE, 1, fp);
        // Garante nome "Raiz" ou "/" para exibição
        strcpy(current_dir.file_name, "/"); 
    }

    while(1) {
        // Mostra em qual pasta estamos
        printf("\n=== SACS: %s [Bloco %u] ===\n", 
               (fp) ? current_dir.file_name : "?", 
               (fp) ? current_dir.start_block : 0);
        
        printf("1. Formatar\n");
        printf("2. Listar Arquivos (ls)\n");
        printf("3. Importar Arquivo (para pasta atual)\n"); 
        printf("4. Exportar Arquivo (da pasta atual)\n");
        printf("5. Remover Item\n");
        printf("6. Criar Diretorio (mkdir)\n");
        printf("7. Mudar Diretorio (cd)\n"); 
        printf("0. Sair\n");
        printf("Escolha: ");
        scanf("%d", &opcao);

        if (opcao == 0) break;

        if (opcao == 1) {
            if (fp) fclose(fp); // Fecha para formatar
            unsigned int setores;
            unsigned short block_size;
            unsigned int root_size;
            printf("Setores (ex: 2048): ");
            scanf("%u", &setores);
            printf("Tamanho dos blocos em relação aos setores (ex: 2 = Setores ^ 2 ^ 2): ");
            scanf("%hu", &block_size);
            printf("Quantidade de blocos no diretório raiz: ");
            scanf("%u", &root_size);
            format_sacs(device_path, SACS, setores, 9, block_size, root_size);
            // Reabre e recarrega raiz
            fp = fopen(device_path, "r+b");
            fseek(fp, 0, SEEK_SET);
            fread(&sup, sizeof(struct superblock), 1, fp);
            real_block_size = (1 << sup.sector_size) << sup.block_size;
            fseek(fp, sup.root_start * real_block_size, SEEK_SET);
            fread(&current_dir, ENTRY_SIZE, 1, fp);
            strcpy(current_dir.file_name, "/");
            continue;
        }

        if (!fp) continue; // Segurança

        switch (opcao) {
            case 2: // Listar 
                list_recursive(fp, &current_dir, &sup, 0);
                break;
            case 3: // Importar
                {
                    char path[200];
                    printf("Arquivo PC: ");
                    scanf("%199s", path);
                    // Passa current_dir como pai
                    import_file(fp, &current_dir, &sup, path);
                }
                break;
            case 4: // Exportar
                {
                    char name[20], dest[200];
                    printf("Arquivo SACS: "); scanf("%19s", name);
                    printf("Destino PC: "); scanf("%199s", dest);
                    export_file(fp, &current_dir, &sup, name, dest);
                }
                break;
            case 5: // Remover
                {
                    char name[20];
                    printf("Nome: "); scanf("%19s", name);
                    delete_item(fp, &current_dir, &sup, name);
                }
                break;
            case 6: // Criar Dir
                {
                    char name[20];
                    printf("Nome Pasta: "); scanf("%19s", name);
                    create_dir(fp, &current_dir, &sup, name);
                }
                break;
            case 7: // CD - Mudar Diretório
                {
                    char target[20];
                    printf("Ir para (.. para voltar): ");
                    scanf("%19s", target);
                    change_directory(fp, &current_dir, &sup, target);
                }
                break;
            default: printf("Invalido.\n");
        }
    }

    if (fp) fclose(fp);
    return 0;
}