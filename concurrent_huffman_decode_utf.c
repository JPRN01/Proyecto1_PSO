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
#include <pthread.h>


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

// Estructura para pasar argumentos a los hilos
typedef struct {
    char filePath[MAX_FILENAME_LENGTH]; // Ruta del archivo a procesar
    char decodedFolderPath[MAX_FILENAME_LENGTH]; // Ruta del directorio para archivos decodificados
    unsigned int unicodeFreq[MAX_UNICODE_POINTS];
    node priorityQueue[MAX_UNICODE_POINTS];
    int pqSize;
    node huffmanTreeRoot;
    HuffmanCode codeTable[MAX_UNICODE_POINTS];
    int codeTableSize;
} ThreadArgs;

int threadsProcessed = 0;

node new_node(int freq, unsigned int codePoint, node a, node b, ThreadArgs *threadArgs) {
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
        exit(1);
    }

    return n;
}

// Función para insertar un nodo en la cola de prioridad 
void pq_insert(node n, ThreadArgs *threadArgs) {
    int i = threadArgs->pqSize++;
    while (i && n->freq < threadArgs->priorityQueue[(i - 1) / 2]->freq) {
        threadArgs->priorityQueue[i] = threadArgs->priorityQueue[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    threadArgs->priorityQueue[i] = n;
}


node pq_remove(ThreadArgs *threadArgs) {
    if (threadArgs->pqSize <= 0) {
        printf("Intento de remover de una cola vacía.\n");
        return NULL; // Retorna NULL si la cola está vacía.
    }

    node removedNode = threadArgs->priorityQueue[0];
    node lastNode = threadArgs->priorityQueue[--threadArgs->pqSize];
    if (threadArgs->pqSize == 0) {
        return removedNode;
    }

    int i = 0, child;
    while (i * 2 + 1 < threadArgs->pqSize) {
        child = i * 2 + 1;

        if ((child + 1 < threadArgs->pqSize) && (threadArgs->priorityQueue[child + 1]->freq < threadArgs->priorityQueue[child]->freq)) {
            child++;
        }

        if (lastNode->freq > threadArgs->priorityQueue[child]->freq) {
            threadArgs->priorityQueue[i] = threadArgs->priorityQueue[child];
        } else {
            break;
        }
        i = child;
    }

    threadArgs->priorityQueue[i] = lastNode; // Coloca el último nodo en su posición correcta.
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

void build_huffman_tree(ThreadArgs *threadArgs) {
    while (threadArgs->pqSize > 1) {
        node left = pq_remove(threadArgs);
        node right = pq_remove(threadArgs);
        node parent = new_node(0, 0, left, right, threadArgs);
        pq_insert(parent, threadArgs);
    }
    threadArgs->huffmanTreeRoot = threadArgs->priorityQueue[0];
}

// Función para generar y almacenar el código Huffman para cada punto de código Unicode en el árbol
void build_code(node n, char *s, int len, ThreadArgs *threadArgs) {
    if (!n) return;

    // Si es un nodo hoja, almacena el código
    if (!n->left && !n->right) {
        s[len] = '\0'; // Termina la cadena

        // Encuentra o crea una entrada en codeTable para este punto de código
        threadArgs->codeTable[threadArgs->codeTableSize].codePoint = n->codePoint;
        strcpy(threadArgs->codeTable[threadArgs->codeTableSize].code, s);

        threadArgs->codeTableSize++;
    } else {
        // Continúa con el hijo izquierdo
        if (n->left) {
            s[len] = '0';
            build_code(n->left, s, len + 1, threadArgs);
        }
        // Continúa con el hijo derecho
        if (n->right) {
            s[len] = '1';
            build_code(n->right, s, len + 1, threadArgs);
        }
    }
}

void import_table(FILE *fp_table, ThreadArgs *threadArgs) {
    unsigned int codePoint;
    int freq;
    char buffer[20];

    while (fscanf(fp_table, "%s %d", buffer, &freq) == 2) {
        sscanf(buffer, "U+%X", &codePoint);
        if (codePoint < MAX_UNICODE_POINTS) {
            threadArgs->unicodeFreq[codePoint] = freq;
            pq_insert(new_node(freq, codePoint, NULL, NULL, threadArgs), threadArgs);
        }
    }

    build_huffman_tree(threadArgs);
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

void free_huffman_tree(node *n) {
    if (!*n) return;
    free_huffman_tree(&(*n)->left);
    free_huffman_tree(&(*n)->right);
    free(*n);
    *n = NULL;
}

void *processFile(void *args) {
    ThreadArgs *threadArgs = (ThreadArgs *)args;

    // Inicializa las estructuras de datos para este hilo
    memset(threadArgs->unicodeFreq, 0, sizeof(threadArgs->unicodeFreq));
    threadArgs->pqSize = 0;
    threadArgs->huffmanTreeRoot = NULL;
    threadArgs->codeTableSize = 0;
    char tableFilePath[512];
    snprintf(tableFilePath, sizeof(tableFilePath), "%s.huffman.table", threadArgs->filePath);
    // Abre el archivo de tabla de frecuencias
    FILE *fp_table = fopen(tableFilePath, "r");
    if (!fp_table) {
        perror("Error al abrir archivo de tabla de frecuencias");
        printf("%s\n", tableFilePath);
        free(threadArgs);
        pthread_exit(NULL);
    }

    // Importa la tabla de frecuencias y construye el árbol de Huffman
    import_table(fp_table, threadArgs);
    fclose(fp_table);

    // Prepara el nombre del archivo de entrada para el archivo Huffman
    char huffmanFilePath[512];
    snprintf(huffmanFilePath, sizeof(huffmanFilePath), "%s.huffman", threadArgs->filePath);
    FILE *fp_in = fopen(huffmanFilePath, "rb");
    if (!fp_in) {
        perror("Error al abrir archivo Huffman");
        free(threadArgs);
        pthread_exit(NULL);
    }

    // Prepara el nombre del archivo de salida para el texto decodificado
    char outputFilePath[512];
    if (strstr(threadArgs->filePath, ".txt")) {
                        snprintf(outputFilePath, sizeof(outputFilePath), "%s%s", threadArgs->decodedFolderPath, threadArgs->filePath);
                    } else {
                        snprintf(outputFilePath, sizeof(outputFilePath), "%s%s.txt", threadArgs->decodedFolderPath, threadArgs->filePath);
                    }
    FILE *fp_out = fopen(outputFilePath, "w");
    if (!fp_out) {
        perror("Error al abrir archivo de salida");
        fclose(fp_in);
        free(threadArgs);
        pthread_exit(NULL);
    }

    // Decodifica el archivo Huffman y escribe el resultado en el archivo de salida
    decode(fp_in, fp_out, threadArgs->huffmanTreeRoot);
    fclose(fp_in);
    fclose(fp_out);

    // Limpieza
    free_huffman_tree(&(threadArgs->huffmanTreeRoot));
    free(threadArgs);
    threadsProcessed++;
    remove(huffmanFilePath);
    remove(tableFilePath);

    pthread_exit(NULL);
}


int main() {
    struct timespec start, end;
    long long elapsed_time;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Descomprimir archivos combinados
    decompressFiles("compressed_files.bin");

    DIR *d;
    struct dirent *dir;
    char directoryPath[MAX_FILENAME_LENGTH] = "./";
    int MAX_THREADS = sysconf(_SC_NPROCESSORS_ONLN); // Número de hilos basado en los núcleos disponibles
    pthread_t threads[MAX_THREADS];
    int threadCount = 0;
    // Crear directorio decoded si no existe
    char decodedFolderPath[MAX_FILENAME_LENGTH] = "./decoded/";
    struct stat st = {0};
    if (stat(decodedFolderPath, &st) == -1) {
        mkdir(decodedFolderPath, 0700);
    }


    d = opendir(directoryPath);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char *fileExtension = strrchr(dir->d_name, '.');
            if (fileExtension && strcmp(fileExtension, ".huffman") == 0) {
                // Crear argumentos para el nuevo hilo
                ThreadArgs *args = (ThreadArgs *)malloc(sizeof(ThreadArgs));

                // Extraer el nombre base del archivo
                char baseFileName[MAX_FILENAME_LENGTH];
                strncpy(baseFileName, dir->d_name, strlen(dir->d_name) - strlen(".huffman"));
                baseFileName[strlen(dir->d_name) - strlen(".huffman")] = '\0';
                printf("Procesando archivo: %s\n", baseFileName);

                // Establecer la ruta del archivo y la ruta del directorio decodificado
                snprintf(args->filePath, sizeof(args->filePath), "%s", baseFileName);
                snprintf(args->decodedFolderPath, sizeof(args->decodedFolderPath), "%s", decodedFolderPath);

                // Inicializar las estructuras de datos en los argumentos del hilo
                memset(args->unicodeFreq, 0, sizeof(args->unicodeFreq));
                args->pqSize = 0;
                args->huffmanTreeRoot = NULL;
                args->codeTableSize = 0;

                // Crear un nuevo hilo para procesar el archivo
                if (pthread_create(&threads[threadCount % MAX_THREADS], NULL, processFile, args) != 0) {
                    perror("Error creating thread");
                    free(args);
                }

                threadCount++;
            }
        }

        // Esperar a que todos los hilos restantes terminen
        for (int i = 0; i < threadCount % MAX_THREADS; i++) {
            if (pthread_join(threads[i], NULL) != 0) {
                perror("Error joining thread");
            }
        }
    } else {
        perror("No se pudo abrir el directorio.");
    }
    check:
    if(threadsProcessed != 98){
        goto check;
    }
    closedir(d);
    // Medir y mostrar el tiempo de ejecución
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    printf("Tiempo de ejecución: %lld nanosegundos\n", elapsed_time);

    return 0;
}