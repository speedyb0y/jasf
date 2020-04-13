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

#define elif else if

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int64_t i64;

typedef long long           intll;
typedef unsigned            uint;
typedef unsigned long long  uintll;

//                       0b????0000U //     repeated ID ??
// usar um ^SCODE_NULL ao ler o código, e ao escrever o código, assim NULL pode ser 0????
#define SCODE_BINARY    0b00000000U //  0
#define SCODE_STRING    0b00000001U //  1 UTF-8
#define SCODE_INT64     0b00000010U //  2 9.223.372.036.854.775.808, 9 quintilhões suportados
#define SCODE_FLOAT64   0b00000011U //  3 IEEE 754-2008
#define SCODE_NULL      0b00000100U //  4
#define SCODE_INVALID   0b00000101U //  5
#define SCODE_FALSE     0b00000110U //  6
#define SCODE_TRUE      0b00000111U //  7
#define SCODE_DICT      0b00001000U //  8
#define SCODE_LIST      0b00001001U //  9
#define SCODE_EOF       0b00001010U // 10   // quando a última mensagem seria um SCODE_LD_END ou SCODE_LD_END1 N
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

typedef uint SCODE;
typedef u64 SWORD;

typedef struct PyObject PyObject;

struct PyObject { //  TODO: FIXME: ---> no decoding->repeateds, ja salvar o code, value
    SCODE code;
    SWORD word;
    void* data;
};

static inline PyObject* py_list_new(void) { return NULL; }
static inline PyObject* py_dict_new(void) { return NULL; }
static inline void py_free(PyObject* obj) { (void)obj; }
static inline void py_list_append(PyObject* restrict list, PyObject* obj) { (void)list; (void)obj; }
static inline void py_dict_set(PyObject* const restrict dict, PyObject* const restrict key, PyObject* const restrict value) { (void)dict; (void)key; (void)value; }

#define DECODE_CONTINUE          0
#define DECODE_EOF              -1// EOF quebra todos os níveis independente de quantos forem
#define DECODE_ERR              -2
#define DECODE_TOOBIG           -3
#define DECODE_MALLOC           -4
#define DECODE_TOO_MANY_REPEATS -5
#define DECODE_BAD_REPEAT_ID    -6 // TODO: FIXME: quando há um REPEAT ID em que o ID não existe
#define DECODE_INCOMPLETE       -7 // TODO :FIXME:  when this value is past the end
#define DECODE_JUNK_AT_END      -8
#define DECODE_NO_VALUE         -9
#define DECODE_UNEXPECTED_LD_END -10  // lista/dicionário sem fim
#define DECODE_UNEXPECTED_TOKEN -11

    // TODO: FIXME: pré gerar esses exceptions, e depois exceptions[-(ret+1)]

// se retornou DECODE_EOF, mantém ele
// se retornou >  0 é para retornar mais esse N número de níveis; então retorna já descontando 1
// se retornou == 0 (DECODE_CONTINUE) (, simplesment eterminou o nível de cima, e continua neste
// se retornou <  0 é um erro, e deve ser mantido
#define DECODE_ENTER_LEVEL(x) if ((ret = (x))) return ret - (ret > 0); break;

static PyObject* None;
static PyObject* Invalid;
static PyObject* False;
static PyObject* True;

typedef struct Decoding Decoding;

struct Decoding {
    const u8* start;
    const u8* pos;
    const u8* end;
    PyObject** repeatedsUnknown;
    PyObject* repeateds[];
};

#define started (decoding->pos)
#define pos (decoding->pos)
#define end (decoding->end)
#define repeateds (decoding->repeateds)
#define repeatedsEnd (decoding->repeatedsUnknown)

// TODO: FIXME:  e o que acontece com reys repetidas?
// e com os valores de keys repetidas?
//   e com os sub valores de keys/valores  repetidos?
//      " ao dar erros?
static int decode_dict (Decoding* const restrict decoding, PyObject* const restrict dict) {
    (void)decoding;
    (void)dict;
    return 0;
}

static int decode_list (Decoding* const restrict decoding, PyObject* const restrict list) {


    // TODO: FIXME: cada objeto criado, seja repeated, o unão, coloca numa lista de objetos_criados
    // ao sair com erro, limpa tudo
    // ao sair com sucesso, owna só o value

    loop {

        int ret;
        PyObject* list2;
        PyObject* dict2;
        SCODE code;
        SWORD word;
        uint repeatedID;

        if (pos >= end)
            /* UÉ, CADÊ O FIM DESSA LISTA? */
            return DECODE_UNEXPECTED_LD_END;

        switch ((code = *pos++)) {
            // TODO: FIXME: uma versão não otimizada aqui, que lê primeiro ese length para determinar s eprecisa ler mais???

            case SCODE_EOF:

                return DECODE_EOF;

            case SCODE_LIST:

                // TODO: FIXME: suportar também tuple =]
                list2 = py_list_new();

                py_list_append(list, (PyObject*)list2);

                DECODE_ENTER_LEVEL(decode_list(decoding, list2));

            case SCODE_DICT:

                // cria o dicionário
                // adiciona o dicionáiro à lista
                dict2 = py_dict_new();

                py_list_append(list, (PyObject*)dict2);

                DECODE_ENTER_LEVEL(decode_dict(decoding, dict2));

            case SCODE_NULL:

                // adiciona o NULL à lista

                break;

            case SCODE_INVALID:

                // adiciona o INVALID à lista
                // INVALID será um objeto constante :/

                break;

            case SCODE_FALSE:

                // adiciona o False à lista

                break;

            case SCODE_TRUE:

                // adiciona o True à lista

                break;

            case SCODE_LD_END:

                return DECODE_CONTINUE;

            case SCODE_LD_END1:

                // termina este level e mais esses N + 1 aí
                // SCODE_LD_END - quebra 1
                // SCODE_LD_END1 N - quebra 1 + N
                // senão "SCODE_LD_END1 0x01" seria o mesmo que SCODE_LD_END;
                // assim um SCODE_LD_END1 aguenta 256 quebras, e não somente 255 xD
                return *pos++;

            case SCODE_REPEATED1:

                repeatedID = *pos++;

                // TODO:  FIXME: esse ID existe mesmo? :O
                py_list_append(list, repeateds[repeatedID]);

                break;

            case SCODE_REPEATED2:

                repeatedID  = *pos++;
                repeatedID <<= 8;
                repeatedID |= *pos++;

                // TODO:  FIXME: esse ID existe mesmo? :O
                py_list_append(list, repeateds[repeatedID]);

                break;

            case SCODE_REPEATED3:

                repeatedID  = *pos++;
                repeatedID <<= 8;
                repeatedID |= *pos++;
                repeatedID <<= 8;
                repeatedID |= *pos++;

                // TODO:  FIXME: esse ID existe mesmo? :O
                py_list_append(list, repeateds[repeatedID]);

                break;

            default:
#if 0
                word  = pos[7]; word <<= 8;
                word |= pos[6]; word <<= 8;
                word |= pos[5]; word <<= 8;
                word |= pos[4]; word <<= 8;
                word |= pos[3]; word <<= 8;
                word |= pos[2]; word <<= 8;
                word |= pos[1]; word <<= 8;
                word |= pos[0];
#else
                word = *(u64*)pos; //__builtin_bswap64
#endif
                if (code > 0b1111) {
                    uint len = (code >> 3) & 0b111U;
                    word &= ~(0xFFFFFFFFFFFFFFFFULL << (len*8));
                    word <<= 3;
                    word |= code & 0b111U;
                    code >>= 6;
                    pos += len;
                } else
                    pos += 8;

                // se pos > end, então desconsiderar o resultado (ou precisa ler mais)
        }
    }
}

#undef started
#undef pos
#undef end
#undef repeateds
#undef repeatedsEnd

/* SE JÁ TINHA ALGUM VALUE, EMPURRA ELE PARA A LISTA DE REPEATEDS */
/* TORNA O THIS O NOVO VALUE FINAL */
/* ESQUECE O THIS */
/* PASSA A USAR O VALUE */
// ao sair daqui:
//      THIS INALTERADO
//      VALUE INALTERADO
//      RET = DECODE_TOO_MANY_REPEATS
//      BREAK
// ou
//      THIS = NULL
//      VALUE = THIS
//      SEGUE EM FRENTE
#define value_set(x) ({ \
        this = (x); \
        if ((this) == NULL) { \
            ret = DECODE_MALLOC; \
            break; \
        } \
        if (value) { \
            /* viu um valor antes deste, então ele pertence à repeateds list */ \
            if (repeateds == repeatedsEnd) { /* não cabe mais nenhum nela */ \
                ret = DECODE_TOO_MANY_REPEATS; /* too many */ \
                break; \
            } /* cabe mais um, coloca ele */ \
            *repeateds++ = value; \
        } /* agora este será o último visto */ \
        value = this; \
        this = NULL; \
    })

// TODO: FIXME: se repeatedsMax for 0, vai analisar primeiro e computar quantos precisa
static PyObject* decode () {

    int ret = DECODE_CONTINUE;

    PyObject* value = NULL;
    PyObject* this = NULL; // TODO: FIXME: está limpando ele?

    u8* start = NULL;
    u8* pos = start;
    u8* end = start + 0;
    uint repeatedsMax = 65536;

    // NOTA: suporta até 4GB -
    if ((end - start) > 0xFFFFFFFF) {
        ret = DECODE_TOOBIG;
        goto RET;
    }

    Decoding* decoding = malloc(sizeof(Decoding) + repeatedsMax*sizeof(PyObject*));

    if (decoding == NULL) {
        ret = DECODE_MALLOC;
        goto RET;
    }

    PyObject** repeateds = decoding->repeateds;
    PyObject** repeatedsEnd = decoding->repeateds + repeatedsMax; /* Por enquanto o limite que tepos é de onde termina o buffer */

    loop {

        if (ret && ret != DECODE_JUNK_AT_END)
            break; /* ENCONTROU UM ERRO */

        if (pos == end) {
            /* CHEGOU AO FIM */
            if (value) {
                /* LEU UM VALOR */
                // TODO: FIXME: SUCCESS - agora sim refcount o value!!!
                //value.refCount++;
                /* LIMPA UMA POSSÍVEL MARCA DECODE_JUNK_AT_END */
                ret = DECODE_CONTINUE;
            } else /* MAS NÃO LEU NENHUM VALOR */
                ret = DECODE_NO_VALUE;
            break;
        } /* AINDA NÃO CHEGOU AO FIM */

        if (ret == DECODE_JUNK_AT_END)
            break; /* JÁ LEU OVALOR FINAL E AINDA NÃO CHEGOU AO FIM */

        SCODE code;
        SWORD word;

        switch ((code = *pos++)) {

            /* NULL | INVALID | FALSE | TRUE | BINARY | STRING | INT64 | FLOAT64 a gente ainda não sabe se é o valor final ou só mais um na lista de repeateds; então vai pegando e colocando na lista, e lembrando sempre do último */
            case SCODE_BINARY:
            case SCODE_STRING:
            case SCODE_INT64: // TODO: FIXME: uma versão não otimizada aqui, que lê primeiro ese length para determinar s eprecisa ler mais???
            case SCODE_FLOAT64:
#if 0
                word  = pos[7]; word <<= 8;
                word |= pos[6]; word <<= 8;
                word |= pos[5]; word <<= 8;
                word |= pos[4]; word <<= 8;
                word |= pos[3]; word <<= 8;
                word |= pos[2]; word <<= 8;
                word |= pos[1]; word <<= 8;
                word |= pos[0];
#else
                word = *(u64*)pos; //__builtin_bswap64
#endif
                if (code > 0b1111) {
                    uint len = (code >> 3) & 0b111U;
                    word &= ~(0xFFFFFFFFFFFFFFFFULL << (len*8));
                    word <<= 3;
                    word |= code & 0b111U;
                    code >>= 6;
                    pos += len;
                } else
                    pos += 8;

                // se pos > end, então desconsiderar o resultado (ou precisa ler mais)
                value_set(malloc(sizeof(PyObject)));

                value->code = code;
                value->word = word;
                value->data = pos; //?????????????????????????????

                break;

            /* NULL | INVALID | FALSE | TRUE não eram para aparecer no repeated, mas suporta, para facilitar o algorítimo */
            case SCODE_NULL:
                value_set(None);
                break;

            case SCODE_INVALID:
                value_set(Invalid);
                break;

            case SCODE_FALSE:
                value_set(False);
                break;

            case SCODE_TRUE:
                value_set(True);
                break;

            case SCODE_LIST: /* Listas e dicionários não podem ser repeateds; Se agora um deles aparecer, eles se tornam o valor final. Não poderá haver nada depois deles. */

                value_set(py_list_new());

                decoding->start = start;
                decoding->pos = pos;
                decoding->end = end;
                decoding->repeatedsUnknown = repeateds; /* O repeateds ESTÁ APONTANDO PARA DEPOIS DO ÚLTIMO; ESTE É O LIMITE DOS CONHECIDOS */

                /* SE FOR ERRO PRESERVA O ERRO */
                if ((ret = decode_list(decoding, value)) == DECODE_CONTINUE)
                    ret = DECODE_JUNK_AT_END; /* ESTE NÓS SABEMOS QUE FOI O ÚLTIMO */
                elif (ret > 0)
                    ret = DECODE_ERR; /* AINDA ESTÁ QUEBRANDO NÍVELS, MAS ESTAMOS NO ROOT */

                break;

            case SCODE_DICT:

                value_set(py_dict_new());

                decoding->start = start;
                decoding->pos = pos;
                decoding->end = end;
                decoding->repeatedsUnknown = repeateds;

                /* SE FOR ERRO PRESERVA O ERRO */
                if ((ret = decode_dict(decoding, value)) == DECODE_CONTINUE)
                    ret = DECODE_JUNK_AT_END; /* ESTE NÓS SABEMOS QUE FOI O ÚLTIMO */
                elif (ret > 0)
                    ret = DECODE_ERR; /* AINDA ESTÁ QUEBRANDO NÍVELS, MAS ESTAMOS NO ROOT */

                break;

            default:
                ret = DECODE_UNEXPECTED_TOKEN;
        }
    }

RET:
    if (value)
        py_free(value);

    if (this)
        py_free(this);

    while (repeateds != decoding->repeateds)
        py_free(*--repeateds);

    free(decoding);

    if (ret == DECODE_CONTINUE)
        return value;

    // TODO: FIXME: criar a exceção
    return NULL;
}

// TODO: FIXME: serialize( buff, buffEnd, code, word, data )

// ON SERIALIZATION, NEVER WRITE A SCODE_LD_END DIRECTLY;
// acumulate all list/dict closeds;
//  when hiting anything else, flush it

// code é um SCODE_BINARY / SCODE_STRING / SCODE_INT64 / SCODE_INT64 / SCODE_FLOAT_POS / SCODE_FLOAT_NEG
static uint encode_code_value (u8* const restrict buff, uint code, u64 value) {

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

static inline void show (PyObject* const restrict obj) {

    union { u64 u64_; double double_; } v;

    v.u64_ = obj->word;

    switch (obj->code) {
        case SCODE_NULL            : //typeName = "NULL";
            break;
        case SCODE_INVALID         : //typeName = "INVALID";
            break;
        case SCODE_FALSE           : //typeName = "BOOL FALSE";
            break;
        case SCODE_TRUE            : //typeName = "BOOL TRUE";
            break;
        case SCODE_INT64           : //typeName = "INT64";
            printf("TYPE 0x%02X INT64 0x%016llX % 24lld\n", obj->code, (uintll)obj->word, (uintll)obj->word);
            break;
        case SCODE_FLOAT64         : //typeName = "FLOAT64" ;
            printf("TYPE 0x%02X FLOAT64 0x%016llX %f\n", obj->code, (uintll)v.double_, v.double_);
            break;
        case SCODE_EOF             : //typeName = "END OF STREAM" ;
            break;
        default:
            break;
    }
    // TODO: FIXME: ao chamar a função, pos tem q ue estar após o CODE+word -> tem que estar no começo da string/binary
}

int main (void) {

    // esses são eternos
    None = malloc(sizeof(PyObject));
    Invalid = malloc(sizeof(PyObject));
    False = malloc(sizeof(PyObject));
    True = malloc(sizeof(PyObject));

    u8 buff[4096];
    u8* buff_;

    memset(buff, 0, sizeof(buff));

    buff_ = buff;

    *buff_++ = 0; // the end of repeated values

    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000000LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000000LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000001LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000001LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000002LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000002LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000003LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000003LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000004LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000004LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000000005LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000000005LL);

    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x00000000000000AALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x00000000000000AALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x000000000000BBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x000000000000BBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0000000000CCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0000000000CCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x00000000DDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x00000000DDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x000000EEDDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x000000EEDDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x000077EEDDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x000077EEDDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x008877EEDDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x008877EEDDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x998877EEDDCCBBAALL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x998877EEDDCCBBAALL);

    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)  1000);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) -1000);

    union {
        double double_;
        u64 w;
    } x;

    x.double_ = -1.3;

    printf("!!!!!!!FLOAT64 0x%016llX %f\n", (uintll)x.w, (float)x.double_);

    buff_ += encode_code_value(buff_, SCODE_FLOAT64, (SWORD)x.w);

    printf("egeg|%llu|\n", (uintll)sizeof (x));

    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0102030405060708LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0102030405060708LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD) 0x0807060504030201LL);
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)-0x0807060504030201LL);

    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000003FLL);  // 0b111111
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000007FLL);  // 0b1111111
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x000000000000000FFLL);  // 0b11111111
    buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x00000000000001FFULL);  // 0b111111111 0x1ff
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ += encode_code_value(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);

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
    decode(buff, buff + sizeof(buff), 65535); // AUTO DETECTAR QUANTOS REPEATS VAI PRECISAR? :O

    return 0;
}

/*
soube que pelo interior Brasil afora tem muito disso, as pessoas andam armadas no campo mas não por causa de criminalidade, mas por causa dessas luzes que sobrevoam, que pousam no campo, animais aparecem mortos e ninguém sabe o que é.
e as histórias ficam ali na região, no povo, não vem à tona isso.
cada um chama de uma coisa, supõe o que é de acordo com a sua religião/conhecimento/imaginação, e vida que segue.

Pra mim esse tipo de coisa é mais comprovador do que fotos, vídeos etc. uma imagem você falsifica, uma notícia de jornal você inventa só para aparecer, mas quando pessoas  reagem de alguma forma e até alteram o modo de vida por causa de um fenômeno, sem sair por aí querendo aparecer na TV  ou ganhando likes em  youtube ou facebook... é porque É REAL.

Uma coisa é mentir para os outros, outra é mentir só para si mesmo, não faz sentido.

A única explicação para isso seria loucura.

Só que aí, seria  muita gente em lugares diferentes e isolados do mundo afora, vivendo a mesma "loucura".

Isso é o que me deixa intrigado com a ufologia: sempre que você tenta analisar, uma hora ou outra chega a um ponto em que para manter o ceticismo, é preciso acreditar no ridículo.

"pessoas vivendo a Vida delas no campo, numa região onde todo mundo conhece histórias de luzes e fenômenos estranhos = são todas loucas/mentirosas (loucas  que sofrem da mesma loucura que muita gente em outros lugares, ou um tipo de gente mentirosa que nunca tenta aparecer na TV ou ganha fama com isso)"

"luzes no céu acompanhando, perseguindo caças = ah é um fenômeno na crosta terrestre que pode provocar luzes na atmosfera (tah, sei que tem uma montanha na China que emite luz, mas outra coisa é uma luz ACOMPANHAR aviões por 30 minutos e reagir à atitude deles)"

"4 grupos de pessoas QUE NÃO SE CONHECIAM e que deram relatos, todos apontando fatos relacionados entre si, totalmente coerentes, sobre o ET de varginha = ah, ET é coisa de retardado lunático. o que aconteceu foi que o exército parou TODO UM COMBOIO, para socorrer um CASAL DE ANÕES na estrada para levar pro hospital dar à luz.
e o jovem policial que morreu, morreu de alguma doença normal. mesmo ELE SENDO TÃO JOVEM, COM SAÚDE E SE PREPARANDO PARA PRESTAR PROVA PARA VIRAR SARGENTO.
ele morreu porque pegou uma chuvinha... não tem nada a ver com os vários animais que morreram no zoológico, também sem motivo algum, sem envenenamento, animais de espécies diferetes que estavam saudáveis e simplesmente adoeceram gravemente e morreram de um dia para o outro.
(embora a irmã do policial tenha inventado que ele contou ter participado da captura de uma criatura, sem usar luvas ou qualquer proteção.
e vamos chamar de mentirosa também aquela senhora humilde já de idade que ficou abismada quando viu uma criatura horrorosa olhando para ela, naquele mesmo zoológico)"

entendem o que quero dizer?
você tem que ter MERDA na cabeça para NÃO acreditar em ETs e discos voadores.

o foda é a quantidade de merdas, mentiras, misticismo e enganos que o povo todo insere no assunto, e que fode a porra toda.

e para me deixar mais frustrado ainda, é ter que me perguntar O QUE É FATO DE VERDADE, e o que é DESINFORMAÇÃO PROPOSITAL.
(vocês realmente acham que esses documentos liberados são verdadeiros? o q ue foi coletado na Operação Prato, em Varginha etc já não está aqui no Brasil há muito tempo.
a gente só vê o que eles deixam.
só resta perguntar, o que eles nos deixam ver é a verdade que eles liberam, ou as mentiras que eles querem que tomemos por verade? .-. )
*/
