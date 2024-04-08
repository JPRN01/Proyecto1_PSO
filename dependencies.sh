#!/bin/bash

echo "Instalando gcc"
sudo dnf install gcc -y

echo "Actualizando paquetes del sistema"
sudo dnf update -y

echo "Compilando archivos"
gcc parallel_huffman_encode_utf.c -o parallel_encode
gcc parallel_huffman_decode_utf.c -o parallel_decode
gcc huffman_encode_utf.c -o encode
gcc huffman_decode_utf.c -o decode
gcc concurrent_huffman_encode_utf.c -o concurrent_encode -lpthread
gcc concurrent_huffman_decode_utf.c -o concurrent_decode -lpthread
echo "Proceso finalizado"