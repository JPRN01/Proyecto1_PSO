#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_UNICODE_POINTS 0x110000 // Punto de código Unicode máximo + 1
#define MAX_CODE_LENGTH 32 // Longitud máxima esperada para un código Huffman
#define MAX_FILENAME_LENGTH 256


typedef struct node_t {
    struct node_t *left, *right;
    int freq;
    unsigned int codePoint; // Usa un unsigned int para almacenar puntos de código Unicode
} *node;

// Estructura para almacenar el código Huffman de un punto de código Unicode.
typedef struct {
    unsigned int codePoint; // Punto de código Unicode
    char code[MAX_CODE_LENGTH]; // Cadena que representa el código Huffman
} HuffmanCode;

unsigned int unicodeFreq[MAX_UNICODE_POINTS] = {0};
node priorityQueue[MAX_UNICODE_POINTS];
int pqSize = 0;
node huffmanTreeRoot = NULL; 
HuffmanCode codeTable[MAX_UNICODE_POINTS]; // Tabla para almacenar los códigos Huffman
int codeTableSize = 0; // Número de códigos almacenados en codeTable



node new_node(int freq, unsigned int codePoint, node a, node b) {
    // Asignación de memoria para un nuevo nodo
    node n = malloc(sizeof(struct node_t));
    // Verifica si la asignación de memoria fue exitosa
    if (n != NULL) {
        // Limpia la memoria asignada
        memset(n, 0, sizeof(struct node_t));

        if (freq != 0) {
            n->codePoint = codePoint;
            n->freq = freq;
        } else {
            n->left = a;
            n->right = b;
            n->freq = a->freq + b->freq;
        }
    } else {
        // Manejo de error si malloc falla
        fprintf(stderr, "Error al asignar memoria para un nuevo nodo\n");
    }

    return n;
}

// Función para insertar un nodo en la cola de prioridad 
void pq_insert(node n) {
    int i = pqSize++;
    while (i && n->freq < priorityQueue[(i - 1) / 2]->freq) {
        priorityQueue[i] = priorityQueue[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    priorityQueue[i] = n;
    //printf("Insertando nodo en cola: CodePoint: U+%04X, Freq: %d\n", n->codePoint, n->freq); // Mensaje de depuración
}



node pq_remove() {
    if (pqSize <= 0) {
        printf("Intento de remover de una cola vacía.\n");
        return NULL; // Retorna NULL si la cola está vacía.
    }

    node removedNode = priorityQueue[0]; 
    node lastNode = priorityQueue[--pqSize]; 
    if (pqSize == 0) {
        return removedNode; 
    }

    
    int i = 0, child;
    while (i * 2 + 1 < pqSize) {
        child = i * 2 + 1; 
        
        if ((child + 1 < pqSize) && (priorityQueue[child + 1]->freq < priorityQueue[child]->freq)) {
            child++; 
        }

        if (lastNode->freq > priorityQueue[child]->freq) {
            priorityQueue[i] = priorityQueue[child]; 
        } else {
            break; 
        }
        i = child; 
    }

    priorityQueue[i] = lastNode; // Coloca el último nodo en su posición correcta.
    return removedNode; // Retorna el nodo removido.
}



// Decodifica un carácter UTF-8 del archivo y devuelve su punto de código Unicode.
// Devuelve -1 si llega al final del archivo o si encuentra un error de codificación.
int decodeUTF8(FILE *fp, unsigned int *bytesRead) {
    int firstByte = fgetc(fp);
    if (firstByte == EOF) return -1; // Fin del archivo
    *bytesRead = 1;

    if (firstByte < 0x80) {
        // Carácter de 1 byte
        return firstByte;
    } else if ((firstByte & 0xE0) == 0xC0) {
        // Carácter de 2 bytes
        int secondByte = fgetc(fp);
        if (secondByte == EOF || (secondByte & 0xC0) != 0x80) return -1; // Error de codificación o EOF
        *bytesRead = 2;
        return ((firstByte & 0x1F) << 6) | (secondByte & 0x3F);
    } else if ((firstByte & 0xF0) == 0xE0) {
        // Carácter de 3 bytes
        int secondByte = fgetc(fp), thirdByte = fgetc(fp);
        if (secondByte == EOF || thirdByte == EOF ||
            (secondByte & 0xC0) != 0x80 || (thirdByte & 0xC0) != 0x80) return -1; // Error de codificación o EOF
        *bytesRead = 3;
        return ((firstByte & 0x0F) << 12) | ((secondByte & 0x3F) << 6) | (thirdByte & 0x3F);
    } else if ((firstByte & 0xF8) == 0xF0) {
        // Carácter de 4 bytes
        int secondByte = fgetc(fp), thirdByte = fgetc(fp), fourthByte = fgetc(fp);
        if (secondByte == EOF || thirdByte == EOF || fourthByte == EOF ||
            (secondByte & 0xC0) != 0x80 || (thirdByte & 0xC0) != 0x80 || (fourthByte & 0xC0) != 0x80) return -1; // Error de codificación o EOF
        *bytesRead = 4;
        return ((firstByte & 0x07) << 18) | ((secondByte & 0x3F) << 12) | ((thirdByte & 0x3F) << 6) | (fourthByte & 0x3F);
    }

    return -1; 
}

void build_huffman_tree() {
    while (pqSize > 1) {
        node left = pq_remove();
        node right = pq_remove();
        node parent = new_node(0, 0, left, right);
        pq_insert(parent);
    }
    huffmanTreeRoot = priorityQueue[0];
}

// Función para generar y almacenar el código Huffman para cada punto de código Unicode en el árbol
void build_code(node n, char *s, int len) {
    if (!n) return;

    // Si es un nodo hoja, almacena el código
    if (!n->left && !n->right) {
        s[len] = '\0'; // Termina la cadena

        // Encuentra o crea una entrada en codeTable para este punto de código
        codeTable[codeTableSize].codePoint = n->codePoint;
        strcpy(codeTable[codeTableSize].code, s);
        //printf("Asignando Código Huffman: Punto de Código U+%04X, Código: %s\n", n->codePoint, s);

        codeTableSize++;
    } else {
        // Continúa con el hijo izquierdo
        if (n->left) {
            s[len] = '0';
            build_code(n->left, s, len + 1);
        }
        // Continúa con el hijo derecho
        if (n->right) {
            s[len] = '1';
            build_code(n->right, s, len + 1);
        }
    }
}



void import_file(FILE *fp_in) {
    unsigned int bytesRead;
    int codePoint;

    printf("File Read:\n");
    while ((codePoint = decodeUTF8(fp_in, &bytesRead)) != -1) {
        if (codePoint < MAX_UNICODE_POINTS) { // Se asegura de que el punto de código esté dentro del rango esperado.
            unicodeFreq[codePoint]++;
        }
    }

    // Crear un nodo para cada caracter con frecuencia no nula y agregarlo a la cola de prioridad
    for (int i = 0; i < MAX_UNICODE_POINTS; ++i) {
        if (unicodeFreq[i] > 0) {
            pq_insert(new_node(unicodeFreq[i], i, NULL, NULL));
        }
    }

    // Imprimir estado final de la cola de prioridad
    printf("\nCola de prioridad finalizada. Tamaño: %d\n", pqSize);
    /*
    for (int i = 0; i < pqSize; ++i) {
        printf("Nodo %d: CodePoint: U+%04X, Freq: %d\n", i, priorityQueue[i]->codePoint, priorityQueue[i]->freq);
    }
    */

    build_huffman_tree();


    char code[MAX_CODE_LENGTH];
    build_code(huffmanTreeRoot, code, 0);


}


void encodeUnicode(FILE* fp_in, FILE* fp_out) {
    unsigned int bytesRead, codePoint;
    unsigned char outputByte = 0; // Byte de salida actual
    int bitCount = 0; // Número de bits usados en el byte de salida actual
    int totalBits = 0; // Total de bits escritos

    printf("Iniciando la codificación y escritura en el archivo .huffman...\n");

    rewind(fp_in); // Asegúrate de que el archivo de entrada se lea desde el principio

    // Deja espacio al comienzo del archivo para la longitud del flujo de bits, que se escribirá después
    fseek(fp_out, sizeof(int), SEEK_SET);

    while ((codePoint = decodeUTF8(fp_in, &bytesRead)) != -1) {
        //printf("Leyendo Punto de Código: U+%04X, Bytes Leídos: %u\n", codePoint, bytesRead);
        // Busca el código Huffman para el punto de código actual
        for (int i = 0; i < codeTableSize; i++) {
            if (codeTable[i].codePoint == codePoint) {
                //printf("Usando Código Huffman para U+%04X: %s\n", codePoint, codeTable[i].code);
                // Recorre el código Huffman y escribe los bits en el archivo de salida
                for (char* ptr = codeTable[i].code; *ptr; ptr++) {
                    outputByte = outputByte * 2 + (*ptr == '1');
                    bitCount++;
                    if (bitCount == 8) {
                        fwrite(&outputByte, 1, 1, fp_out);
                        //printf("Escribiendo byte: 0x%X\n", outputByte);
                        bitCount = 0;
                        outputByte = 0;
                    }
                    totalBits++;
                }
                break;
            }
        }
    }

    // Completa el último byte si no está completo
    if (bitCount > 0) {
        outputByte <<= (8 - bitCount); // Rellena con ceros a la derecha
        fwrite(&outputByte, 1, 1, fp_out);
        //printf("Escribiendo último byte: 0x%X\n", outputByte);
    }

    // Vuelve al principio del archivo y escribe la longitud total del flujo de bits
    rewind(fp_out);
    printf("Escribiendo longitud del flujo de bits al archivo: %d\n", totalBits);
    fwrite(&totalBits, sizeof(int), 1, fp_out);

    printf("Codificación y escritura en el archivo .huffman completadas. Total de bits: %d\n", totalBits);
}


void print_code() {
    printf("\n---------CODE TABLE---------\n----------------------------\nUNICODE POINT  FREQ\n----------------------------\n");
    for (int i = 0; i < MAX_UNICODE_POINTS; i++) {
        if (unicodeFreq[i] > 0) {
            printf("U+%04X         %-4d\n", i, unicodeFreq[i]);
        }
    }
    printf("----------------------------\n");
}


void print_tree_detailed(node n) {
    if (!n) return; // Si el nodo es NULL, termina la función.

    // Verifica si el nodo es un nodo interno
    if (n->left || n->right) {
        // Imprime la información del nodo interno
        printf("Nodo interno Freq: %d", n->freq);
    } else {
        // Si ambos hijos son NULL, este es un nodo hoja
        printf("Nodo '%c' Freq: %d", (char)n->codePoint, n->freq); // Asume que el codePoint se puede imprimir como un char
    }

    // Imprime información sobre el hijo izquierdo
    printf(" hijo izq: ");
    if (n->left) {
        if (!(n->left->left || n->left->right)) { // Si el hijo izquierdo es un nodo hoja
            printf("'%c'", (char)n->left->codePoint);
        } else { // Si el hijo izquierdo es un nodo interno
            printf("interno Freq: %d", n->left->freq);
        }
    } else {
        printf("NULL");
    }

    // Imprime información sobre el hijo derecho
    printf(" hijo der: ");
    if (n->right) {
        if (!(n->right->left || n->right->right)) { // Si el hijo derecho es un nodo hoja
            printf("'%c'", (char)n->right->codePoint);
        } else { // Si el hijo derecho es un nodo interno
            printf("interno Freq: %d", n->right->freq);
        }
    } else {
        printf("NULL");
    }

    printf("\n");

    
    print_tree_detailed(n->left);
    print_tree_detailed(n->right);
}




void free_huffman_tree(node *n) {
    if (!*n) return;
    //printf("Liberando nodo con codePoint: U+%04X\n", (*n)->codePoint);
    free_huffman_tree(&(*n)->left);
    free_huffman_tree(&(*n)->right);
    free(*n);
    *n = NULL;
}


int fileAlreadyProcessed(char *fileName) {
    // Construye los nombres de archivo esperados para los archivos procesados
    char huffmanFileName[512];
    char tableFileName[512];
    
    snprintf(huffmanFileName, sizeof(huffmanFileName), "%s.huffman", fileName);
    snprintf(tableFileName, sizeof(tableFileName), "%s.huffman.table", fileName);

    // Intenta abrir los archivos para ver si existen
    FILE *huffmanFile = fopen(huffmanFileName, "r");
    FILE *tableFile = fopen(tableFileName, "r");

    if (huffmanFile) fclose(huffmanFile);
    if (tableFile) fclose(tableFile);

    // Si ambos archivos existen, asumimos que el archivo ya ha sido procesado
    return huffmanFile && tableFile;
}

void compressFiles(const char *directory, const char *outputFile) {
    DIR *dir;
    struct dirent *entry;
    FILE *out = fopen(outputFile, "wb");
    if (!out) {
        perror("Error opening the output file\n");
        return;
    }

    dir = opendir(directory);
    if (!dir) {
        perror("Error opening the directory\n");
        fclose(out);
        return;
    }

    int fileCount = 0;
    // Primero, contar cuántos archivos .huffman y .table hay
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".huffman") || strstr(entry->d_name, ".table")) {
            fileCount++;
        }
    }
    rewinddir(dir); // Volver al inicio del directorio

    // Escribir el número de archivos al inicio del archivo de salida
    fwrite(&fileCount, sizeof(int), 1, out);

    // Ahora, escribir la información de cada archivo
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".huffman") || strstr(entry->d_name, ".table")) {
            char filePath[MAX_FILENAME_LENGTH];
            snprintf(filePath, sizeof(filePath), "%s/%s", directory, entry->d_name);

            FILE *in = fopen(filePath, "rb");
            if (!in) {
                perror("Error opening an input file\n");
                continue;
            }

            // Escribir el nombre del archivo
            fwrite(entry->d_name, sizeof(char), strlen(entry->d_name) + 1, out);

            // Obtener y escribir el tamaño del archivo
            fseek(in, 0, SEEK_END);
            long fileSize = ftell(in);
            rewind(in);
            fwrite(&fileSize, sizeof(long), 1, out);

            // Escribir el contenido del archivo
            char *buffer = (char *)malloc(fileSize);
            fread(buffer, fileSize, 1, in);
            fwrite(buffer, fileSize, 1, out);

            free(buffer);
            fclose(in);
            remove(filePath);
        }
    }

    closedir(dir);
    fclose(out);
}


void cleanup_directory(const char *directoryPath) {
    DIR *d;
    struct dirent *dir;
    char filePath[512];

    d = opendir(directoryPath);
    if (!d) {
        fprintf(stderr, "Error al abrir el directorio para limpieza.\n");
        return;
    }

    while ((dir = readdir(d)) != NULL) {
        // Verifica si el archivo es un .txt.huffman o .txt.huffman.table
        if (strstr(dir->d_name, ".txt.huffman") != NULL) {
            snprintf(filePath, sizeof(filePath), "%s%s", directoryPath, dir->d_name);
            if (remove(filePath) == 0) {
                printf("Archivo eliminado: %s\n", filePath);
            } else {
                fprintf(stderr, "Error al eliminar el archivo: %s\n", filePath);
            }
        }
    }

    closedir(d);
}




int main() {
    struct timespec start, end;
    long long elapsed_time; 

    // Obtiene el tiempo antes de la ejecución del código
    clock_gettime(CLOCK_MONOTONIC, &start);

    DIR *d;
    struct dirent *dir;
    char directoryPath[256] = "./"; 
    


    d = opendir(directoryPath);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char *fileExtension = strrchr(dir->d_name, '.');
            // Verifica que la extensión sea ".txt" y no haya caracteres adicionales después
            if (fileExtension && strcmp(fileExtension, ".txt") == 0 && strlen(fileExtension) == 4) {
                char filePath[512];
                snprintf(filePath, sizeof(filePath), "%s%s", directoryPath, dir->d_name);
                printf("Iniciando procesamiento de: %s\n", dir->d_name);

                // Verifica si el archivo ya ha sido procesado
                if (!fileAlreadyProcessed(dir->d_name)) {
                    // Reinicia las variables aquí
                    memset(unicodeFreq, 0, sizeof(unicodeFreq));
                    pqSize = 0;
                    huffmanTreeRoot = NULL;
                    codeTableSize = 0;

                    // Abre el archivo de entrada
                    FILE *fp_in = fopen(filePath, "r");
                    if (!fp_in) {
                        perror("Error al abrir archivo de entrada");
                        continue; // Salta al siguiente archivo en el directorio
                    }

                    // Procesa el archivo para construir el árbol de Huffman y generar la tabla de códigos
                    import_file(fp_in);

                    // Prepara el nombre del archivo de salida para el archivo comprimido
                    char huffmanFilePath[512];
                    snprintf(huffmanFilePath, sizeof(huffmanFilePath), "%s.huffman", filePath);
                    FILE *fp_out_huffman = fopen(huffmanFilePath, "wb"); // Abre en modo binario para escritura
                    if (!fp_out_huffman) {
                        perror("Error al abrir archivo de salida (.huffman)");
                        fclose(fp_in);
                        continue;
                    }

                    // Codifica y escribe en el archivo .huffman
                    encodeUnicode(fp_in, fp_out_huffman);
                    fclose(fp_out_huffman);

                    // Prepara el nombre del archivo de salida para el archivo de tabla
                    char tableFilePath[512];
                    snprintf(tableFilePath, sizeof(tableFilePath), "%s.huffman.table", filePath);
                    FILE *fp_out_table = fopen(tableFilePath, "w"); // Abre en modo de texto para escritura
                    if (!fp_out_table) {
                        perror("Error al abrir archivo de salida (.table)");
                        fclose(fp_in);
                        continue;
                    }

                    // Escribe la tabla de frecuencias en el archivo .table
                    for (int i = 0; i < MAX_UNICODE_POINTS; i++) {
                        if (unicodeFreq[i] > 0) {
                            fprintf(fp_out_table, "U+%04X %d\n", i, unicodeFreq[i]);
                        }
                    }
                    fclose(fp_out_table);

                    // Cierra el archivo de entrada
                    fclose(fp_in);

                    // Libera el árbol de Huffman
                    free_huffman_tree(&huffmanTreeRoot);

                    printf("Finalizado el procesamiento de: %s\n", filePath);

                    // Reinicia las estructuras de datos para el próximo archivo
                    memset(unicodeFreq, 0, sizeof(unicodeFreq));
                    pqSize = 0;
                    codeTableSize = 0;
                } else {
                    printf("Archivo ya procesado: %s\n", dir->d_name);
                }
            }
        }
        closedir(d);
        compressFiles(".", "compressed_files.bin");

    } else {
        perror("No se pudo abrir el directorio.");
    }

    
    
    // Obtiene el tiempo después de la ejecución del código
    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calcula y muestra el tiempo de ejecución en nanosegundos
    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    printf("Tiempo de ejecución: %ld nanosegundos\n", elapsed_time);

    return 0;
}

