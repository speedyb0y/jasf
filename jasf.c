/*
    JASF Just Another Serialization Format

    THIS IS A WORK IN PROGRESS
    THIS IS AN EXAMPLE OF AN IMPLEMENTATION; NOT A FINAL LIBRARY

    LICENSE: GPLv3

    pega todos os valores que irá escrever
        valores, não dicionários e listas, nones, falses, trues

        u64 hash;
        u32 next[2];
        u32 same;
        u32 count; // quantas ocorrências
        void* value; // o valor em si

        ORDENA INVERSAMENTE TODA A ARRAY PELO THIS->COUNT

        QUANTOSJABOTOUNOCOMEÇO = 0;

        this = array;

            if (this->count == 1) {
                do { // DAQUI PARA FRENTE TODOS OS DEMAIS TEM COUNT 1 TB
                    this->count = 0;
                } while ((++this)->count);
            } else if (len(BACKWARDS+id) >= len(CODE+value)) {
                // basicamente se for nulo, ou qualquer um daqueles com 1 byte
                (this++)->count = 0;
            } else {
                escreve no começo
                (this++)->count = ++QUANTOSJABOTOUNOCOMEÇO;
            }

        agora poe o code FIM_DOS_VALORES (0)

        agora vai escrevendo a estrutura de dados

        if (this->count)
            escreve por referência -> ID é (this->count - 1)
        else
            escreve direto


    valores 0 estrutura
*/

#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define loop while(1)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int64_t i64;

typedef long long           intll;
typedef unsigned            uint;
typedef unsigned long long  uintll;

#define REPEATEDS_END 0
//                       0b????0000U //     repeated ID ??
// usar um ^SCODE_NULL ao ler o código, e ao escrever o código, assim NULL pode ser 0????
#define SCODE_BINARY    0b00000000U //  0
#define SCODE_STRING    0b00000001U //  1 UTF-8
#define SCODE_INT64     0b00000010U //  2
#define SCODE_FLOAT64   0b00000011U //  3 IEEE 754-2008
#define SCODE_NULL      0b00000100U //  4
#define SCODE_INVALID   0b00000101U //  5
#define SCODE_FALSE     0b00000110U //  6
#define SCODE_TRUE      0b00000111U //  7
#define SCODE_DICT      0b00001000U //  8
#define SCODE_LIST      0b00001001U //  9             9.223.372.036.854.775.808
#define SCODE_EOF       0b00001010U // 10             -18.446.744.073.709.551.616 18 quintilhões suportados
#define SCODE_LD_END    0b00001011U // 11
#define SCODE_LD_END1   0b00001100U // 12 followed by 1 byte count of ends
#define SCODE_REPEATED1 0b00001101U // 13 repeated ID 1  bytes
#define SCODE_REPEATED2 0b00001110U // 14 repeated ID 2  bytes
#define SCODE_REPEATED3 0b00001111U // 15 repeated ID 3  bytes
   //                            ||
   //                            TIPOS QUE PODEM ENCODAR VALORES (code <= SCODE_FLOAT64)
   // São arrastados << 6 ---5
   //                0b11000000U
   //                  ||||||||
   //                 TYPE|||||
   //                    ||||||
   //                    LEN|||
   //                       RBITS
#define SCODE_UNEXPECTED_END 0x100U // NÃO APARECE NA STREAM, É UM EVENTO D EERRO

// Se retornar NULL - encontrou o EOF, explícito
// Se retornar end - chegou ao fim do buffer, consumiu tudo o que tem nele, mas ainda não terminou a stream
// Se retornar qualquer outro valor - ainda não dá para ler este item por completo, precisa carregar mais do buffer (após o end)
typedef intll DeserializeRet;

typedef uint SCODE;
typedef u64 SWORD;

typedef struct DeserializeRepeated DeserializeRepeated;

struct DeserializeRepeated { //  TODO: FIXME: ---> no ctx->repeateds, ja salvar o code, value
    SCODE code;
    u64 value;
    void* data; // onde começam os dados
};

typedef struct DeserializeContext DeserializeContext;

struct DeserializeContext {
    u8* pos;
    u8* end;
    u8* start;
    DeserializeRet (*readen)(DeserializeContext*, SCODE, SWORD, void* data); // called om each value readen
    DeserializeRepeated* repeated;
    DeserializeRepeated* repeatedsEnd;
    DeserializeRepeated repeateds[]; // st->start + st->repeateds[ID] aponta para o offset do primeiro
};

#define DESERIALIZE_SUCCESS              0LL
#define DESERIALIZE_CONTINUE             0LL
#define DESERIALIZE_END                  LLONG_MAX
#define DESERIALIZE_ERR                  -1
#define DESERIALIZE_ERR_TOOBIG           (LLONG_MIN + 6)
#define DESERIALIZE_ERR_MALLOC           (LLONG_MIN + 5)
#define DESERIALIZE_ERR_NO_REPEATS_END   (LLONG_MIN + 4)  // unexpected end before REPEATEDS_END
#define DESERIALIZE_ERR_TOO_MANY_REPEATS (LLONG_MIN + 3)
#define DESERIALIZE_ERR_UNEXPECTED_END   (LLONG_MIN + 2) // reached end but not EOF
#define DESERIALIZE_ERR_NO_REPEATED      (LLONG_MIN + 1) // TODO: FIXME: quando há um REPEAT ID em que o ID não existe
#define DESERIALIZE_ERR_INCOMPLETE       (LLONG_MIN    ) // TODO :FIXME:  when this value is past the end

static DeserializeRet deserialize_dict(DeserializeContext* restrict const ctx) {
    (void)ctx;
    return DESERIALIZE_CONTINUE;
}

static DeserializeRet deserialize_list(DeserializeContext* restrict const ctx) {

    loop {

        DeserializeRet ret;
        DeserializeRepeated* repeated;
        uint repeatedID;
        uint code;
        SWORD value;

        if (ctx->pos == ctx->end)     // TODO: FIXME: nao tem mais nada, mas o cara diz DESERIALIZE_CONTINUE -> 0 -1 - -1 -< ou seja, seg uem frente com o erro
            return ctx->readen(ctx, SCODE_UNEXPECTED_END, 0, NULL) - 1;

        switch ((code = *(ctx->pos++))) {
            // TODO: FIXME: uma versão não otimizada aqui, que lê primeiro ese length para determinar s eprecisa ler mais???

            case SCODE_LIST:

                if ((ret = ctx->readen(ctx, code, 0, NULL)))
                    return ret - 1;

                if ((ret = deserialize_list(ctx)))
                    return ret - 1; // tem mais um para sair

                break;

            case SCODE_DICT:

                if ((ret = ctx->readen(ctx, code, 0, NULL)))
                    return ret - 1;

                if ((ret = deserialize_dict(ctx)))
                    return ret - 1;

                break;

            case SCODE_EOF:

                if ((ret = ctx->readen(ctx, code, 0, NULL)))
                    return ret - 1;

                return DESERIALIZE_END;

            case SCODE_NULL ... SCODE_TRUE:

                if ((ret = ctx->readen(ctx, code, 0, NULL)))
                    return ret - 1;

                break;

            case SCODE_LD_END:

                if ((ret = ctx->readen(ctx, code, 0, NULL)))
                    return ret - 1;

                return 0;

            case SCODE_LD_END1:

                code = (ctx->pos++)[0] + 1; // termina este level e mais esses N + 1 aí

                if ((ret = ctx->readen(ctx, SCODE_LD_END, code, NULL)))
                    return ret - 1;

                return code - 1;

            case SCODE_REPEATED1:

                if      (code == SCODE_REPEATED1) repeatedID =                                             ctx->pos[0];
                else if (code == SCODE_REPEATED2) repeatedID =                       (ctx->pos[1] <<  8) | ctx->pos[0];
                else if (code == SCODE_REPEATED3) repeatedID = (ctx->pos[2] << 16) | (ctx->pos[1] << 16) | ctx->pos[0];

                // TODO:  FIXME: esse ID existe mesmo? :O
                repeated = ctx->repeateds + repeatedID;

                ctx->pos += code - SCODE_REPEATED1 + 1;

                if ((ret = ctx->readen(ctx, repeated->code, repeated->value, repeated->data))) //, TODO: FIXME: repeated->data
                    return ret - 1;

                break;

            default:
#if 0
                value  = ctx->pos[7]; value <<= 8;
                value |= ctx->pos[6]; value <<= 8;
                value |= ctx->pos[5]; value <<= 8;
                value |= ctx->pos[4]; value <<= 8;
                value |= ctx->pos[3]; value <<= 8;
                value |= ctx->pos[2]; value <<= 8;
                value |= ctx->pos[1]; value <<= 8;
                value |= ctx->pos[0];
#else
                value = *(u64*)ctx->pos; //__builtin_bswap64
#endif
                if (code > 0b1111) {
                    uint len = (code >> 3) & 0b111U;
                    value &= ~(0xFFFFFFFFFFFFFFFFULL << (len*8));
                    value <<= 3;
                    value |= code & 0b111U;
                    code >>= 6;
                    ctx->pos += len;
                } else
                    ctx->pos += 8;

                // se pos > end, então desconsiderar o resultado (ou precisa ler mais)

                if ((ret = ctx->readen(ctx, code, value, ctx->pos)))
                    return ret - 1;
        }
    }
}


// TODO: FIXME: serialize( buff, buffEnd, code, word, data )

// code é um SCODE_BINARY / SCODE_STRING / SCODE_INT64 / SCODE_INT64 / SCODE_FLOAT_POS / SCODE_FLOAT_NEG
static uint serialize_code_value (u8* const restrict buff, uint code, u64 value) {

    uint len;

    if (value >> 58) /* value >> (7*8 + 2) - se sobrar algo, é porque não caberá nem em 2 bits + 7 bytes */
        len = 9; /* O length é 8 bytes, separados */
    else { /* Coloca os 2 últimos bits no código, e o length nele também */
        code <<= 6;
        code |= value & 0b111U;
        value >>= 3;
             if (value == 0)                     len = 0;
        else if (value <= 0x00000000000000FFULL) len = 1;
        else if (value <= 0x000000000000FFFFULL) len = 2;
        else if (value <= 0x0000000000FFFFFFULL) len = 3;
        else if (value <= 0x00000000FFFFFFFFULL) len = 4;
        else if (value <= 0x000000FFFFFFFFFFULL) len = 5;
        else if (value <= 0x0000FFFFFFFFFFFFULL) len = 6;
        else                                     len = 7;
        code |= len << 3;
        len += 1; /* Tem o código */
    }

    buff[0] = code;
#if 0
    buff[1] = value; value >>= 8;
    buff[2] = value; value >>= 8;
    buff[3] = value; value >>= 8;
    buff[4] = value; value >>= 8;
    buff[5] = value; value >>= 8;
    buff[6] = value; value >>= 8;
    buff[7] = value; value >>= 8;
    buff[8] = value;
#elif 0
    buff[1] = value;
    buff[2] = value >>  8;
    buff[3] = value >> 16;
    buff[4] = value >> 24;
    buff[5] = value >> 32;
    buff[6] = value >> 40;
    buff[7] = value >> 48;
    buff[8] = value >> 56;
#else
    *(u64*)(buff+1) = value; // NOTE: on other architectures, use __builtin_bswap64
#endif

    return len;
}

// TODO: FIXME: se repeatedsMax for 0, vai analisar primeiro e computar quantos precisa
static DeserializeRet deserialize(u8* const start, u8* const end, DeserializeRet (*readen)(DeserializeContext*, SCODE, SWORD, void* data), uint repeatedsMax) {

    DeserializeContext* ctx;

    // NOTA: suporta até 4GB -
    if ((end - start) > 0xFFFFFFFFU)
        return DESERIALIZE_ERR_TOOBIG;

    if ((ctx = malloc(sizeof(DeserializeContext) + repeatedsMax*sizeof(DeserializeRepeated))) == NULL)
        return DESERIALIZE_ERR_MALLOC;

    ctx->start = start;
    ctx->pos = start;
    ctx->end = end;
    ctx->readen = readen;
    ctx->repeated = ctx->repeateds;
    ctx->repeatedsEnd = ctx->repeateds + repeatedsMax;

    DeserializeRet ret = DESERIALIZE_SUCCESS;

    loop { /* anda até o final dos repeateds, construindo os ids */

        if (ctx->pos == ctx->end) {
            ret = DESERIALIZE_ERR_NO_REPEATS_END;
            break;
        }

        SCODE code = *(ctx->pos++);

        if (code == REPEATEDS_END) {
            ret = deserialize_list(ctx);
            break;
        }

        if (ctx->repeated == ctx->repeatedsEnd) {
            ret = DESERIALIZE_ERR_TOO_MANY_REPEATS; // too many
            break;
        }

        // TODO: FIXME: uma versão não otimizada aqui, que lê primeiro ese length para determinar s eprecisa ler mais???

        u64 value;
#if 0
        value  = ctx->pos[7]; value <<= 8;
        value |= ctx->pos[6]; value <<= 8;
        value |= ctx->pos[5]; value <<= 8;
        value |= ctx->pos[4]; value <<= 8;
        value |= ctx->pos[3]; value <<= 8;
        value |= ctx->pos[2]; value <<= 8;
        value |= ctx->pos[1]; value <<= 8;
        value |= ctx->pos[0];
#else
        value = *(u64*)ctx->pos; //__builtin_bswap64
#endif
        if (code > 0b1111) {
            uint len = (code >> 3) & 0b111U;
            value &= ~(0xFFFFFFFFFFFFFFFFULL << (len*8));
            value <<= 3;
            value |= code & 0b111U;
            code >>= 6;
            ctx->pos += len;
        } else
            ctx->pos += 8;

        // se pos > end, então desconsiderar o resultado (ou precisa ler mais)

        ctx->repeated->value = value;
        ctx->repeated->code = code;
        ctx->repeated->data = ctx->pos; //?????????????????????????????
        ctx->repeated++;
    }

    free(ctx);

    return ret;
}

static DeserializeRet show (DeserializeContext* restrict const st, SCODE code, SWORD word, void* data) {

    (void)st;
    (void)data;

    union {
        u64 u64_;
        double double_;

    } v;

    v.u64_ = word;

    switch (code) {
        case SCODE_NULL            : //typeName = "NULL";
            break;
        case SCODE_INVALID         : //typeName = "INVALID";
            break;
        case SCODE_FALSE           : //typeName = "BOOL FALSE";
            break;
        case SCODE_TRUE            : //typeName = "BOOL TRUE";
            break;
        case SCODE_INT64           : //typeName = "INT64";
            printf("TYPE 0x%02X INT64 0x%016llX % 24lld\n", code, (uintll)word, (uintll)word);
            break;
        case SCODE_FLOAT64         : //typeName = "FLOAT64" ;
            printf("TYPE 0x%02X FLOAT64 0x%016llX %f\n", code, (uintll)v.double_, v.double_);
            break;
        case SCODE_EOF             : //typeName = "END OF STREAM" ;
            break;
        case SCODE_UNEXPECTED_END  : //typeName = "UNEXPECTED END OF STREAM";
            break;
        default:
            break;
    }

    // TODO: FIXME: ao chamar a função, pos tem q ue estar após o CODE+word -> tem que estar no começo da string/binary


    return DESERIALIZE_CONTINUE;
}

int main (void) {

    u8 buff[4096];
    u8* buff_;

    memset(buff, 0, sizeof(buff));

    buff_ = buff;

    *buff_++ = 0; // the end of repeated values

    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000000LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000000LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000001LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000001LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000002LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000002LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000003LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000003LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000004LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000004LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000005LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000005LL);

    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x00000000000000AALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x00000000000000AALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x000000000000BBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x000000000000BBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000CCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000CCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x00000000DDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x00000000DDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x000000EEDDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x000000EEDDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x000077EEDDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x000077EEDDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x008877EEDDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x008877EEDDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x998877EEDDCCBBAALL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x998877EEDDCCBBAALL);

    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)  1000);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) -1000);

    union {
        double double_;
        u64 w;
    } x;

    x.double_ = -1.3;

    printf("!!!!!!!FLOAT64 0x%016llX %f\n", (uintll)x.w, (float)x.double_);

    buff_ += serialize_code_value(buff_, SCODE_FLOAT64, (SWORD)x.w);

    printf("egeg|%llu|\n", (uintll)sizeof (x));

    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0102030405060708LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0102030405060708LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD) 0x0807060504030201LL);
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)-0x0807060504030201LL);

    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000003FLL);  // 0b111111
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000007FLL);  // 0b1111111
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x000000000000000FFLL);  // 0b11111111
    buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x00000000000001FFULL);  // 0b111111111 0x1ff
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += serialize_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);

    buff_[0] = SCODE_EOF;
    buff_[1] = 0;
    buff_[2] = 0;
    buff_[3] = 0;
    buff_[4] = 0;
    buff_[5] = 0;
    buff_[6] = 0;
    buff_[7] = 0;
    buff_[8] = 0;
    buff_[9] = 0;

    uint size = 512;
    uint offset = 0;
    while (offset != size)
        printf("%02X", buff[offset++]);
    printf("\n");

    // le t udo até encontrar o primeiro NULLO
    deserialize(buff, buff + sizeof(buff), show, 65535); // AUTO DETECTAR QUANTOS REPEATS VAI PRECISAR? :O

    return 0;
}
