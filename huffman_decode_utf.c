#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h> 
#include <errno.h>
#include <time.h>
#include <unistd.h>

#define MAX_FILENAME_LENGTH 256


// Simula un mapa para los puntos de código Unicode y sus frecuencias.
#define MAX_UNICODE_POINTS 0x110000 // Punto de código Unicode máximo + 1
#define MAX_CODE_LENGTH 32 // Longitud máxima esperada para un código Huffman


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
    printf("Insertando nodo en cola: CodePoint: U+%04X, Freq: %d\n", n->codePoint, n->freq); // Mensaje de depuración
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

    priorityQueue[i] = lastNode; 
    return removedNode; 
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




void import_table(FILE *fp_table) {
    unsigned int codePoint;
    int freq;
    char buffer[20]; 

    
    while (fscanf(fp_table, "%s %d", buffer, &freq) == 2) {
        
        sscanf(buffer, "U+%X", &codePoint);

        if (codePoint < MAX_UNICODE_POINTS) {
            unicodeFreq[codePoint] = freq; 
            pq_insert(new_node(freq, codePoint, NULL, NULL)); 
        }
    }

    // Reconstruir el árbol de Huffman a partir de la cola de prioridad actualizada
    build_huffman_tree();

    
}


// Función auxiliar para escribir un punto de código Unicode como UTF-8 en un archivo
void writeUTF8(FILE* fp, unsigned int codePoint) {
    if (codePoint < 0x80) {
        // 1 byte
        fputc(codePoint, fp);
    } else if (codePoint < 0x800) {
        // 2 bytes
        fputc(0xC0 | (codePoint >> 6), fp);
        fputc(0x80 | (codePoint & 0x3F), fp);
    } else if (codePoint < 0x10000) {
        // 3 bytes
        fputc(0xE0 | (codePoint >> 12), fp);
        fputc(0x80 | ((codePoint >> 6) & 0x3F), fp);
        fputc(0x80 | (codePoint & 0x3F), fp);
    } else {
        // 4 bytes
        fputc(0xF0 | (codePoint >> 18), fp);
        fputc(0x80 | ((codePoint >> 12) & 0x3F), fp);
        fputc(0x80 | ((codePoint >> 6) & 0x3F), fp);
        fputc(0x80 | (codePoint & 0x3F), fp);
    }
}

// Decodifica el archivo Huffman y escribe el resultado en el archivo de salida
void decode(FILE* fp_in, FILE* fp_out, node root) {
    int totalBits, currentBit = 0, bitCount = 0;
    unsigned char byte;
    node current = root;

    // Leer la longitud total del flujo de bits
    fread(&totalBits, sizeof(int), 1, fp_in);
    printf("Total de bits a decodificar: %d\n", totalBits);

    while (currentBit < totalBits) {
        if (bitCount == 0) {
            fread(&byte, 1, 1, fp_in);
            bitCount = 8;
        }

        if (byte & 0x80) { // 0x80 = b1000 0000, verifica si el bit más significativo es 1
            current = current->right;
        } else {
            current = current->left;
        }

        // Mover al siguiente bit
        byte <<= 1;
        bitCount--;
        currentBit++;

        if (!current->left && !current->right) { // Nodo hoja encontrado
            writeUTF8(fp_out, current->codePoint);
            current = root; // Reiniciar para el próximo punto de código
        }
    }
}


void decompressFiles(const char *inputFile) {
    FILE *in = fopen(inputFile, "rb");
    if (!in) {
        perror("Error al abrir el archivo combinado");
        return;
    }

    int fileCount;
    fread(&fileCount, sizeof(int), 1, in);

    for (int i = 0; i < fileCount; i++) {
        char fileName[MAX_FILENAME_LENGTH] = {0};
        int j = 0;
        do {
            fread(&fileName[j], sizeof(char), 1, in);
        } while (fileName[j++] != '\0');

        long fileSize;
        fread(&fileSize, sizeof(long), 1, in);

        char *buffer = (char *)malloc(fileSize);
        fread(buffer, fileSize, 1, in);

        FILE *out = fopen(fileName, "wb");
        if (!out) {
            perror("Error al crear un archivo de salida");
            free(buffer);
            continue;
        }

        fwrite(buffer, fileSize, 1, out);
        fclose(out);
        free(buffer);
    }

    fclose(in);
    remove(inputFile);
}


int main() {
    struct timespec start, end;
    long long elapsed_time;
    clock_gettime(CLOCK_MONOTONIC, &start);
    decompressFiles("compressed_files.bin");
    DIR *d;
    struct dirent *dir;
    char directoryPath[256] = "./";
    char decodedFolderPath[256] = "./decoded/";
    struct stat st = {0};

    if(stat(decodedFolderPath, &st) == -1) {
        mkdir(decodedFolderPath, 0700);
    }

    d = opendir(directoryPath);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char *fileExtension = strrchr(dir->d_name, '.');
            if (fileExtension && strcmp(fileExtension, ".huffman") == 0) {
                char baseFileName[512];
                char huffmanFilePath[512];
                char tableFilePath[512];
                char outputFilePath[512];

                strncpy(baseFileName, dir->d_name, strlen(dir->d_name) - strlen(".huffman"));
                baseFileName[strlen(dir->d_name) - strlen(".huffman")] = '\0';

                snprintf(huffmanFilePath, sizeof(huffmanFilePath), "%s%s", directoryPath, dir->d_name);
                snprintf(tableFilePath, sizeof(tableFilePath), "%s%s.huffman.table", directoryPath, baseFileName);

                if (strstr(baseFileName, ".txt")) {
                    snprintf(outputFilePath, sizeof(outputFilePath), "%s%s", decodedFolderPath, baseFileName);
                } else {
                    snprintf(outputFilePath, sizeof(outputFilePath), "%s%s.txt", decodedFolderPath, baseFileName);
                }

                printf("Procesando: %s y %s\n", huffmanFilePath, tableFilePath);

                FILE *fp_huffman = fopen(huffmanFilePath, "rb");
                FILE *fp_table = fopen(tableFilePath, "r");
                FILE *fp_out = fopen(outputFilePath, "w");

                if (!fp_huffman || !fp_table || !fp_out) {
                    printf("Error al abrir los archivos necesarios para: %s\n", dir->d_name);
                    if (fp_huffman) fclose(fp_huffman);
                    if (fp_table) fclose(fp_table);
                    if (fp_out) fclose(fp_out);
                    continue;
                }

                memset(unicodeFreq, 0, sizeof(unicodeFreq));
                pqSize = 0;
                huffmanTreeRoot = NULL;
                codeTableSize = 0;

                import_table(fp_table);
                decode(fp_huffman, fp_out, huffmanTreeRoot);

                fclose(fp_huffman);
                fclose(fp_table);
                remove(huffmanFilePath);
                remove(tableFilePath);
                fclose(fp_out);

                printf("Decodificación completada y guardada en: %s\n", outputFilePath);
            }
        }
        closedir(d);
    } else {
        perror("No se pudo abrir el directorio.");
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    printf("Tiempo de ejecución: %ld nanosegundos\n", elapsed_time);

    return 0;
}
