
// todas as estruturas de mesmo tamanho e/ou //x

// 1. cacheando as coisas e jogando para o começo, mantém os IDs curtos
// 2. não faz sentido cachear números pequenos < ???.
#define NULL      0b00000000U //  0
#define INVALID   0b00010000U //  1
#define FALSE     0b00100000U //  2
#define TRUE      0b00110000U //  3
#define BINARY    0b01000000U //  4 // NOVO
#define STR       0b01010000U //  5 
#define POS       0b01100000U //  6          opcode com mais 4 bits!!!!!!!!!! 0b110000XXXX
#define NEG       0b01110000U //  7
#define FLOAT     0b10000000U //  8
#define LIST      0b10010000U //  9
#define DICT      0b10100000U // 10
#define NEGG      0b10110000U // 11   4 BITS 0 BYTES  
#define NEG       0b11000000U // 12   4 BITS 1 BYTES
#define REPEAT0   0b11010000U // 13   4 BITS 2 BYTES   
#define REPEAT1   0b11100000U // 14   4 BITS 1 BYTES
#define REPEAT2   0b11110000U // 15   0 BITS 2 BYTES   !!! então pode ficar ali em cima

// encode os key do dict de forma ordenada
// da um hash
// se se já tiver no DB, substituipelo ID

struct cached_s {
  u64 hash;
  u64 hash2; // para strings e binarios  e floats e ints ets  -> só temos 65536 valores, 128 bits de hash é suficiente contra colisões - ainda mais diferenciando binário de UTF-8 válido
  u16 next[2];
  u16 same;
  u16 type; // dict keys | str | binary | pos | neg | float
};


// quem tem que ficar lembrando do type é o decoder
// quem tem que ficar lembrando do hash e next/same é o encoder



