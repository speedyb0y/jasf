#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define loop while(1)

#define elif else if

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int64_t i64;

typedef long long           intll;
typedef unsigned            uint;
typedef unsigned long long  uintll;

typedef struct encode_s encode_s;
typedef struct decode_s decode_s;

#define HEAD_SIZE 512
#define HEAD_MASK 0b111111111U
#define HEAD_BITS 9

#define CHILDS_SIZE 4
#define CHILDS_MASK 0b11U
#define CHILDS_BITS 2

struct encode_s {
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32 level;
    u32 index;
    u32 childs[CHILDS_SIZE];
    u32* ptr;
};

typedef struct encode_context_s_i {
    uint pos;
    uint size;
    u32* heads;
    u32* indexes;
    encode_s cache[];
} encode_context_s_i;

typedef struct encode_context_s {
    uint pos;
    const uint size;
    u32* const heads;
    u32* const indexes;
    encode_s cache[];
} encode_context_s;

struct decode_s {
    u16 type;
};

/* Means no code; it was unknown and hashed instead */
#define CODE_NEW 0xFFFFFFFFU
#define CODE_SAME 0xFFFFFFFEU

#define FAZ(var, id) (((var) = ctx->cache + (id)) != (encode_s*)ctx->indexes)

#define FOLLOW(_node, level, ptr) ({ \
    (ptr) = &(_node)->childs[(hash >> (level * (level < (64 - CHILDS_BITS)))) & CHILDS_MASK]; \
    level += CHILDS_BITS; \
    })

#define SET_LEVEL_LINK(_node, _level, _ptr) ({ \
    (_node)->level = (_level); \
    (_node)->ptr = (_ptr); \
    *(_ptr) = (_node) - ctx->cache; \
    })

// a soma d etodos os indexes
// a soma de todos os estrutura->codigo
static uint lookup(encode_context_s* const restrict ctx, const void* restrict str, uint len) {

    u64 hash  = (u64)len;
    u64 hash1 = (u64)len << 32;
    u64 hash2 = (u64)len << 48;

    while (len >= sizeof(u64)) {
        const u64 word = *(u64*)str; str += sizeof(u64);
        hash  += word & 0xFFFFFFFFULL;
        hash1 += word >> 32;
        hash1 += hash;
        hash2 += hash1 & 0xFFFFFFFFULL;
        hash  += hash;
        hash += len;
        len -= sizeof(u64);
    }

    while (len) {
        const u64 chr = *(u8*)str++;
        hash  += hash >> 32;
        hash  += chr;
        hash1 += hash  << (chr & 0b11111U);
        hash2 += hash1 << (chr & 0b11111U);
        hash1 += hash2 << (len & 0b11111U);
        hash2 += hash1 << (len & 0b11111U);
        hash2 += len--;
        hash  += hash2;
    }

    uint level = 0;
    u32* ptr = &ctx->heads[hash1 & HEAD_MASK];
    encode_s* this;

    while FAZ(this, *ptr) {
        if (this->hash  == hash  &&
            this->hash1 == hash1 &&
            this->hash2 == hash2) {
            int code = ctx->pos - this->index;
            if (code == 0) {
                return CODE_SAME;
            }
            if (code == 1) {
                return code;
            }
            // TODO: FIXME: mover ele para frente
            // e se não foi o último , coloca roúltimo sobre ele
            return code;
        } FOLLOW(this, level, ptr); // TODO: FIXME: vai dar certo isso?
    }

    // Sobrescreve o mais antigo
    this = ctx->cache + ctx->indexes[ctx->pos++ % ctx->size];

    if (((void*)this > (void*)ptr) || (((void*)this + sizeof(encode_s)) <= (void*)ptr)) {
        // O escolhido não foi o último do path que seguiu
        if (this->ptr) {
            // Estamos em uma hash tree, podendo ser outra
            // Para o caso deste ser o último
            *this->ptr = ctx->size;
            uint slot = CHILDS_SIZE;
            // Acha o primeiro child
            while (slot--) { encode_s* child0;
                if FAZ(child0, this->childs[slot]) {
                    // O passa para o parent
                    SET_LEVEL_LINK(child0, this->level, this->ptr);
                    // Os demais viram filhos do primeiro, a partir do mesmo slot
                    while (slot--) { encode_s* child;
                        if FAZ(child, this->childs[slot]) { encode_s* this;
                            u32* ptr = &child0->childs[slot];
                            u64 hash = child->hash;
                            uint level = child->level;
                            // Vai até o fim
                            while FAZ(this, *ptr)
                                FOLLOW(this, level, ptr);
                            SET_LEVEL_LINK(child, level, ptr);
                        }
                    } break;
                }
            }
        } *(this->ptr = ptr) = this - ctx->cache;
    }

    this->hash  = hash;
    this->hash1 = hash1;
    this->hash2 = hash2;
    this->level = level;
    this->childs[0] = ctx->size;
    this->childs[1] = ctx->size;
    this->childs[2] = ctx->size;
    this->childs[3] = ctx->size;

    return CODE_NEW;
}

static inline u64 rdtscp(void) {
    uint lo;
    uint hi;
    __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi));
    return ((u64)hi << 32) | lo;
}

static encode_context_s* encoder_new (const uint size) {

    encode_context_s_i* const ctx = malloc(sizeof(encode_context_s) + size*sizeof(encode_s) + size*sizeof(u32) + HEAD_SIZE*sizeof(u32*));

    ctx->pos = 0;
    ctx->size = size;
    ctx->indexes = (void*)(ctx->cache + size);
    ctx->heads = (void*)(ctx->indexes + size);

    uint count = 0;

    do {
        encode_s* const this = &ctx->cache[count];

        this->hash  = 0;
        this->hash1 = 0;
        this->hash2 = 0;
        this->level = 0;
        this->index = count;
        this->childs[0] = size;
        this->childs[1] = size;
        this->childs[2] = size;
        this->childs[3] = size;
        this->ptr = NULL;

        ctx->indexes[count] = count;

    } while (++count != size);

    count = 0;

    do {
        ctx->heads[count] = size;
    } while (++count != HEAD_SIZE);

    return (encode_context_s*)ctx;
}

#define CACHE_SIZE 0xFFFF

int main (void) {

    encode_context_s* const ctx = encoder_new(CACHE_SIZE);

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count % 3000, count % 500); // (uintll)(rdtscp())
            lookup(ctx, str, strlen(str));
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
        while (*++coisa)
            printf("COISA %s %u\n", *coisa, lookup(ctx, *coisa, strlen(*coisa)));
        printf("\n");
    }

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count % 3000, count % 500); // (uintll)(rdtscp())
            lookup(ctx, str, strlen(str));
        } while (++count != (100*CACHE_SIZE));
    }

    { char** coisa = coisas;
        while (*++coisa)
            printf("COISA %s %u\n", *coisa, lookup(ctx, *coisa, strlen(*coisa)));
        printf("\n");
    }

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count % 3000, count % 500); // (uintll)(rdtscp())
            lookup(ctx, str, strlen(str));
        } while (++count != (4000));
    }

    { char** coisa = coisas;
        while (*++coisa)
            printf("COISA %s %u\n", *coisa, lookup(ctx, *coisa, strlen(*coisa)));
        printf("\n");
    }

    { uintll count = 0;
        do {
            char str[256];
            sprintf(str, "%llu.%llu", count % 3000, count % 500); // (uintll)(rdtscp())
            lookup(ctx, str, strlen(str));
        } while (++count != (8000));
    }

    { char** coisa = coisas;
        while (*++coisa)
            printf("COISA %s %u\n", *coisa, lookup(ctx, *coisa, strlen(*coisa)));
        printf("\n");
    }

    return 0;
}
