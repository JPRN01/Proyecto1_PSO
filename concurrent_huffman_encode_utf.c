#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define MAX_UNICODE_POINTS 0x110000 // Punto de código Unicode máximo + 1
#define MAX_CODE_LENGTH 32 // Longitud máxima esperada para un código Huffman
#define MAX_FILENAME_LENGTH 256

// Estructura de nodo para el árbol de Huffman
typedef struct node_t {
    struct node_t *left, *right;
    int freq;
    unsigned int codePoint; // Usa un unsigned int para almacenar puntos de código Unicode
} *node;

// Estructura para almacenar el código Huffman de un punto de código Unicode
typedef struct {
    unsigned int codePoint; // Punto de código Unicode
    char code[MAX_CODE_LENGTH]; // Cadena que representa el código Huffman
} HuffmanCode;

// Estructura para pasar argumentos a los hilos
typedef struct {
    char filePath[MAX_FILENAME_LENGTH]; // Ruta del archivo a procesar
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



void import_file(FILE *fp_in, ThreadArgs *threadArgs) {
    unsigned int bytesRead;
    int codePoint;

    printf("File Read:\n");
    while ((codePoint = decodeUTF8(fp_in, &bytesRead)) != -1) {
        if (codePoint < MAX_UNICODE_POINTS) {
            threadArgs->unicodeFreq[codePoint]++;
        }
    }
    for (int i = 0; i < MAX_UNICODE_POINTS; ++i) {
        if (threadArgs->unicodeFreq[i] > 0) {
            pq_insert(new_node(threadArgs->unicodeFreq[i], i, NULL, NULL, threadArgs), threadArgs);
        }
    }
    build_huffman_tree(threadArgs);
    char code[MAX_CODE_LENGTH];
    build_code(threadArgs->huffmanTreeRoot, code, 0, threadArgs);
}


void encodeUnicode(FILE *fp_in, FILE *fp_out, ThreadArgs *threadArgs) {
    unsigned int bytesRead, codePoint;
    unsigned char outputByte = 0;
    int bitCount = 0;
    int totalBits = 0;

    printf("Iniciando la codificación y escritura en el archivo .huffman...\n");

    rewind(fp_in);
    fseek(fp_out, sizeof(int), SEEK_SET);

    while ((codePoint = decodeUTF8(fp_in, &bytesRead)) != -1) {
        for (int i = 0; i < threadArgs->codeTableSize; i++) {
            if (threadArgs->codeTable[i].codePoint == codePoint) {
                for (char *ptr = threadArgs->codeTable[i].code; *ptr; ptr++) {
                    outputByte = outputByte * 2 + (*ptr == '1');
                    bitCount++;
                    if (bitCount == 8) {
                        fwrite(&outputByte, 1, 1, fp_out);
                        bitCount = 0;
                        outputByte = 0;
                    }
                    totalBits++;
                }
                break;
            }
        }
    }

    if (bitCount > 0) {
        outputByte <<= (8 - bitCount);
        fwrite(&outputByte, 1, 1, fp_out);
    }

    rewind(fp_out);
    printf("Escribiendo longitud del flujo de bits al archivo: %d\n", totalBits);
    fwrite(&totalBits, sizeof(int), 1, fp_out);

    printf("Codificación y escritura en el archivo .huffman completadas. Total de bits: %d\n", totalBits);
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

// Función que será el punto de entrada para los hilos
void *processFile(void *args) {
    ThreadArgs *threadArgs = (ThreadArgs *)args;

    // Inicializa las estructuras de datos para este hilo
    memset(threadArgs->unicodeFreq, 0, sizeof(threadArgs->unicodeFreq));
    threadArgs->pqSize = 0;
    threadArgs->huffmanTreeRoot = NULL;
    threadArgs->codeTableSize = 0;

    // Abre el archivo de entrada
    FILE *fp_in = fopen(threadArgs->filePath, "r");
    if (!fp_in) {
        perror("Error al abrir archivo de entrada");
        free(threadArgs);
        pthread_exit(NULL);
    }

    // Procesa el archivo para construir el árbol de Huffman y generar la tabla de códigos
    import_file(fp_in, threadArgs);

    // Prepara el nombre del archivo de salida para el archivo comprimido
    char huffmanFilePath[MAX_FILENAME_LENGTH];
    snprintf(huffmanFilePath, sizeof(huffmanFilePath), "%s.huffman", threadArgs->filePath);
    FILE *fp_out_huffman = fopen(huffmanFilePath, "wb");
    if (!fp_out_huffman) {
        perror("Error al abrir archivo de salida (.huffman)");
        fclose(fp_in);
        free(threadArgs);
        pthread_exit(NULL);
    }

    // Codifica y escribe en el archivo .huffman
    encodeUnicode(fp_in, fp_out_huffman, threadArgs);

    // Cierra el archivo .huffman
    fclose(fp_out_huffman);

    // Prepara el nombre del archivo de salida para la tabla de códigos
    char tableFilePath[MAX_FILENAME_LENGTH];
    snprintf(tableFilePath, sizeof(tableFilePath), "%s.huffman.table", threadArgs->filePath);
    FILE *fp_out_table = fopen(tableFilePath, "w");
    if (!fp_out_table) {
        perror("Error al abrir archivo de salida (.table)");
        fclose(fp_in);
        free(threadArgs);
        pthread_exit(NULL);
    }

    // Escribe el archivo .table
    for (int i = 0; i < MAX_UNICODE_POINTS; i++) {
        if (threadArgs->unicodeFreq[i] > 0) {
            fprintf(fp_out_table, "U+%04X %d\n", i, threadArgs->unicodeFreq[i]);
        }
    }

    // Cierra el archivo .table
    fclose(fp_out_table);

    // Cierra el archivo de entrada y libera recursos
    fclose(fp_in);
    free_huffman_tree(&(threadArgs->huffmanTreeRoot));
    free(threadArgs);
    threadsProcessed++;

    pthread_exit(NULL);
}



int main() {
    struct timespec start, end;
    long long elapsed_time; 

    // Obtiene el tiempo antes de la ejecución del código
    clock_gettime(CLOCK_MONOTONIC, &start);
    DIR *d;
    struct dirent *dir;
    char directoryPath[MAX_FILENAME_LENGTH] = "./";
    int MAX_THREADS = sysconf(_SC_NPROCESSORS_ONLN);
    pthread_t threads[MAX_THREADS];
    int threadCount = 0;
    

    d = opendir(directoryPath);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char *fileExtension = strrchr(dir->d_name, '.');
            if (fileExtension && strcmp(fileExtension, ".txt") == 0) {
                if (threadCount >= MAX_THREADS) {
                    // Esperar a que uno de los hilos termine
                    if (pthread_join(threads[threadCount % MAX_THREADS], NULL) != 0) {
                        perror("Error joining thread");
                    }
                }

                // Crear argumentos para el nuevo hilo
                ThreadArgs *args = (ThreadArgs *)malloc(sizeof(ThreadArgs));
                snprintf(args->filePath, sizeof(args->filePath), "%s%s", directoryPath, dir->d_name);

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
    compressFiles(".", "compressed_files.bin");
    // Obtiene el tiempo después de la ejecución del código
    clock_gettime(CLOCK_MONOTONIC, &end);
    // Calcula y muestra el tiempo de ejecución en nanosegundos
    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    printf("Tiempo de ejecución: %ld nanosegundos\n", elapsed_time);
    return 0;
}

