#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define loop while(1)

#define elif else if

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int64_t i64;

typedef long long           intll;
typedef unsigned            uint;
typedef unsigned long long  uintll;

typedef struct encode_s encode_s;
typedef struct decode_s decode_s;

#define CACHE_SIZE 0xFFFF
#define HEAD_N 512
#define HEAD_MASK 0b111111111U
#define HEAD_BITS 9
#define CHILDS_N 4
#define CHILDS_MASK 0b11U
#define CHILDS_BITS 2

struct encode_s {
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32 level;
    u32 code;
    encode_s* childs[CHILDS_N]; // na hash tree
    encode_s** ptr;
};

struct decode_s {
    u16 type;
};

static uint pos;
static encode_s* indexes[CACHE_SIZE];
static encode_s cache[CACHE_SIZE];
static encode_s* strs[HEAD_N];

// a soma d etodos os indexes
// a soma de todos os estrutura->codigo
static uint lookupstr(const char* restrict str, uint len) {

    u64 hash  = (u64)len;
    u64 hash1 = (u64)len << 32;
    u64 hash2 = (u64)len << 58;

    while (len) {
        u64 chr = *(u8*)str++;
        hash <<= 3;
        hash  += chr;
        hash  += hash >> 32;
        hash1 += chr;
        hash1 += (chr + len) & 0b1111;
        hash1 += chr << (chr & 0b1111);
        hash1 += hash1 >> 32;
        hash1 += chr;
        hash2 += chr << (len & 0b1111);
        hash2 += len << (chr & 0b1111);
        hash2 += len << (len & 0b1111);
        hash2 += hash2 >> 32;
        hash2 += len--;
    }

    hash += hash1;
    hash += hash2;

    //printf("%016llX %016llX %016llX %u%u%u%u%u%u%u%u\n", (uintll)hash, (uintll)hash1, (uintll)hash2,
        //(uint)((hash >> 7) & 1U),
        //(uint)((hash >> 6) & 1U),
        //(uint)((hash >> 5) & 1U),
        //(uint)((hash >> 4) & 1U),
        //(uint)((hash >> 3) & 1U),
        //(uint)((hash >> 2) & 1U),
        //(uint)((hash >> 1) & 1U),
        //(uint)((hash     ) & 1U)
        //);

    uint level;
    uint index;
    encode_s* this;
    encode_s** ptr;

    ptr = &strs[hash1 & HEAD_MASK];
    level = 0;

    while ((this = *ptr)) {
        if (this->hash  == hash  &&
            this->hash1 == hash1 &&
            this->hash2 == hash2)
            return this->code;
        ptr = &this->childs[(hash >> level) & CHILDS_MASK];
        // TODO: FIXME: vai dar certo isso?
        if ((level += CHILDS_BITS) >= (64 - CHILDS_BITS))
            level = 0;
    }

    // Sobrescreve o mais antigo
    this = indexes[(index = pos++ % CACHE_SIZE)];

    if (((void*)this <= (void*)ptr) && ((void*)ptr < ((void*)this + sizeof(encode_s))))
        // O escolhido foi o último do path que seguiu
        ptr = this->ptr;
    elif (this->ptr) {
        // Estamos em outra hash tree
        // Para o caso deste ser o último
        *this->ptr = NULL;
        uint slot = CHILDS_N;
        // Acha o primeiro child
        while (slot--) { encode_s* child0;
            if ((child0 = this->childs[slot])) {
                // O passa para o parent
                child0->level = this->level;
                child0->ptr = this->ptr;
                *child0->ptr = child0;
                // Os demais viram filhos do primeiro, a partir do mesmo slot
                while (slot--) { encode_s* child; encode_s* w;
                    if ((child = this->childs[slot])) {
                        encode_s** ptr2 = &child0->childs[slot];
                        u64 hash = child->hash;
                        uint level = child->level;
                        // Vai até o fim
                        while ((w = *ptr2)) {
                            ptr2 = &w->childs[(hash >> level) & CHILDS_MASK];
                            level += CHILDS_BITS;
                            if ((level += CHILDS_BITS) >= (64 - CHILDS_BITS))
                                level = 0;
                        }
                        child->level = level;
                        child->ptr = ptr2;
                        *child->ptr = child;
                    }
                }
                break;
            }
        }
    }

    // Encontra um novo ptr
    this->hash  = hash;
    this->hash1 = hash1;
    this->hash2 = hash2;
    this->level = level;
    this->childs[0] = NULL;
    this->childs[1] = NULL;
    this->childs[2] = NULL;
    this->childs[3] = NULL;
    this->ptr = ptr;
    *this->ptr = this;

    if (this->childs[0] != NULL ||
        this->childs[1] != NULL ||
        this->childs[2] != NULL ||
        this->childs[3] != NULL) {
        //printf("?? %llu %llu = %llu %llu\n", (uintll)this, (uintll)ptr, ((uintll)ptr - (uintll)this),
            //(uintll)sizeof(encode_s));
        abort();
    }

    return this->code;
}

static inline u64 rdtscp(void) {
    uint lo;
    uint hi;
    __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi));
    return ((u64)hi << 32) | lo;
}

typedef struct str_ {
    uint len;
    char str[];
} str_;

int main (void) {

    pos = 0;

    { uint id = 0; while (id != HEAD_N) strs[id++] = NULL; }

    { uint id = 0;
        do {
            encode_s* const cached = &cache[id];

            cached->hash  = 0;
            cached->hash1 = 0;
            cached->hash2 = 0;
            cached->level = 0;
            cached->code = id;
            cached->childs[0] = NULL;
            cached->childs[1] = NULL;
            cached->childs[2] = NULL;
            cached->childs[3] = NULL;
            cached->ptr = NULL;

            indexes[id] = cached;

        } while (++id != CACHE_SIZE);
    }

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count, (uintll)(rdtscp()));
            lookupstr(str, strlen(str));
        } while (++count != (1000*CACHE_SIZE));
    }

    char* coisas[] = {

         "A", "B", "C", "D", "E", "F", "000", "001", "0001", "0102", "00002",
         "ewgwegwegweqwiowq iohf32,",
         "ewgop ew goewugewiog ewi yewy r32 hdsjknkjlew 06.56",
         "ewgwgwegwe",
         "BANANA",
          NULL };

    { char** coisa = coisas;
        do {
            printf("COISA %s %u\n", *coisa, lookupstr(*coisa, strlen(*coisa)));
        } while (*++coisa);
        printf("\n");
    }

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count, (uintll)(rdtscp()));
            lookupstr(str, strlen(str));
        } while (++count != (100*CACHE_SIZE));
    }

    { char** coisa = coisas;
        do {
            printf("COISA %s %u\n", *coisa, lookupstr(*coisa, strlen(*coisa)));
        } while (*++coisa);
        printf("\n");
    }

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count, (uintll)(rdtscp()));
            lookupstr(str, strlen(str));
        } while (++count != (4000));
    }

    { char** coisa = coisas;
        do {
            printf("COISA %s %u\n", *coisa, lookupstr(*coisa, strlen(*coisa)));
        } while (*++coisa);
        printf("\n");
    }

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count, (uintll)(rdtscp()));
            lookupstr(str, strlen(str));
        } while (++count != (4000));
    }

    { char** coisa = coisas;
        do {
            printf("COISA %s %u\n", *coisa, lookupstr(*coisa, strlen(*coisa)));
        } while (*++coisa);
        printf("\n");
    }

    return 0;
}
