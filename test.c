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
#define CHILDS_N 4
#define CHILDS_MASK 0b11U
#define CHILDS_BITS 2
#define HEAD_SIZE 512
#define HEAD_MASK 0b111111111U
#define HEAD_BITS 9

struct encode_s {
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32 level;
    u32 code; // indexes[this->code] -> this - cache
    encode_s* childs[CHILDS_N]; // na hash tree
    encode_s** ptr;
};

struct decode_s {
    u16 type;
};

static uint pos;
static encode_s* indexes[CACHE_SIZE];
static encode_s cache[CACHE_SIZE];
static encode_s* strs[HEAD_SIZE];

#define X(x) ((uintll)((x) ? ((void*)(x) - (void*)cache) : (99999999)))

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

        //se estiver no último nível, entra no da esquerda
        // TODO: FIXME: vai dar certo isso?
        ptr = &this->childs[(hash >> level) & CHILDS_MASK];

        level += CHILDS_BITS;

        printf("AT LEVEL %u ptr %8llu\n", level, X(ptr));
    }

    printf("NOT FOUND AT THIS %8llu LEVEL %u ptr %8llu\n", X(this), level, X(ptr));

    // Sobrescreve o mais antigo
    this = indexes[(index = pos++ % CACHE_SIZE)];

    if (this->ptr) {
        printf("REUSING THIS %8llu %u this->ptr %8llu\n", X(this), index, X(this->ptr));
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
                printf("child0 %8llu child0->ptr %8llu child0->level %8llu child0->childs %8llu %8llu | %8llu %8llu | %8llu %8llu | %8llu %8llu | slot %u\n",
                    X(child0),
                    X(child0->ptr),
                    (uintll)child0->level,
                    X(&child0->childs[0]), X(child0->childs[0]),
                    X(&child0->childs[1]), X(child0->childs[1]),
                    X(&child0->childs[2]), X(child0->childs[2]),
                    X(&child0->childs[3]), X(child0->childs[3]), slot
                    );
                // Os demais viram filhos do primeiro, a partir do mesmo slot
                while (slot--) { encode_s* child; encode_s* w;
                    if ((child = this->childs[slot])) {
                        printf("child  %8llu child ->ptr %8llu child ->level %8llu child ->childs %8llu %8llu | %8llu %8llu | %8llu %8llu | %8llu %8llu | slot %u\n",
                            X(child),
                            X(child->ptr),
                            (uintll)child->level,
                            X(&child->childs[0]), X(child->childs[0]),
                            X(&child->childs[1]), X(child->childs[1]),
                            X(&child->childs[2]), X(child->childs[2]),
                            X(&child->childs[3]), X(child->childs[3]), slot
                            );
                        encode_s** ptr = &child0->childs[slot];
                        u64 hash = child->hash;
                        uint level = child->level;
                        // Vai até o fim
                        while ((w = *ptr)) {
                            ptr = &w->childs[(hash >> level) & CHILDS_MASK];
                            level += CHILDS_BITS;
                        }
                        child->level = level;
                        child->ptr = ptr;
                        *child->ptr = child;
                    }
                }
                break;
            }
        }
    }

    printf("this %8llu | childs %8llu %8llu %8llu %8llu | ptr %8llu\n",
        X(this),
        X(this->childs[0]),
        X(this->childs[1]),
        X(this->childs[2]),
        X(this->childs[3]),
        X(this->ptr)
        );

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

    if (this->code != index) {
        printf("??\n");
        abort();
    }

    if (this->childs[0] != NULL ||
        this->childs[1] != NULL ||
        this->childs[2] != NULL ||
        this->childs[3] != NULL) {
        printf("this %8llu | childs %8llu %8llu %8llu %8llu | ptr %8llu | *ptr %8llu\n",
            X(this),
            X(this->childs[0]),
            X(this->childs[1]),
            X(this->childs[2]),
            X(this->childs[3]),
            X(this->ptr),
            X(*this->ptr));
        abort();
    }


    //this  2864664 | childs   810864 99999999 99999999 99999999 | ptr  3553816
    //AT LEVEL 2 ptr  3933552
    //AT LEVEL 4 ptr   767560
    //NOT FOUND AT THIS 99999999 LEVEL 4 ptr   767560
    //REUSING THIS  2864736 39788 this->ptr  2181416
    //child0   687024 child0->ptr  2181416 child0->level        6 child0->childs   687056 99999999 |   687064 99999999 |   687072 99999999 |   687080 99999999 | slot 1
    //child   4037256 child ->ptr  2864768 child ->level        6 child ->childs  4037288 99999999 |  4037296 99999999 |  4037304  3311208 |  4037312 99999999 | slot 0
    //this  2864736 | childs  4037256   687024 99999999 99999999 | ptr  2181416

    //AT LEVEL 2 ptr   358960
    //AT LEVEL 4 ptr  1151472
    //AT LEVEL 6 ptr  3495776
    //AT LEVEL 8 ptr  4389376
    //AT LEVEL 10 ptr  3209952
    //AT LEVEL 12 ptr  2864856
    //NOT FOUND AT THIS 99999999 LEVEL 12 ptr  2864856
    //REUSING THIS  2864808 39789 this->ptr  3209952
    //child0    77760 child0->ptr  3209952 child0->level        4 child0->childs    77792 99999999 |    77800 99999999 |    77808 99999999 |    77816 99999999 | slot 3
    //child   4240224 child ->ptr  2864840 child ->level        6 child ->childs  4240256 99999999 |  4240264 99999999 |  4240272    67464 |  4240280   534024 | slot 0
    //this  2864808 | childs  4240224 99999999 99999999    77760 | ptr  3209952
    //this  2864808 | childs 99999999 99999999  2864808 99999999 | ptr  2864856 | *ptr  2864808
    //Aborted

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

    { uint id = 0; while (id != HEAD_SIZE) strs[id++] = NULL; }

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

    str_* buff = malloc(512*1024*1024);
    str_* end = (void*)buff + 512*1024*1024 - 256;
    str_* cur = buff;

    do { u64 count = (u64)cur;
        cur->len = sprintf(cur->str, "%llX%llX%llu",
            (uintll)(rdtscp() - (count << 32)),
            (uintll)(rdtscp() + (count << 16)),
            (uintll)(rdtscp() >> (count & 0b11111))
            );
        cur = (void*)cur->str + cur->len + 1;
    } while (cur < end);

    end = cur;

    cur = buff;

    while (cur < end) {
        lookupstr(cur->str, cur->len);
        cur = (void*)cur->str + cur->len + 1;
    }

    //VERIFY_CACHE();

    char* coisas[] = {

         "A", "B", "C", "D", "E", "F", "000", "001", "0001", "0102", "00002",
         "ewgwegwegweqwiowq iohf32,",
         "ewgop ew goewugewiog ewi yewy r32 hdsjknkjlew 06.56",
         "ewgwgwegwe",
         "BANANA",
          NULL };

    char** coisa = coisas;

    do {
        printf("COISA %s %u\n", *coisa, lookupstr(*coisa, strlen(*coisa)));
    } while (*++coisa);

    return 0;
}
