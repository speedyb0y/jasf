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

#define PY_SSIZE_T_CLEAN
#include <Python.h>

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

#define SCODE_NULL         0b00000000U //   0
#define SCODE_INVALID      0b00000001U //   1
// ENCODED 1 BIT
#define SCODE_BINARY       0b00000010U //   2
#define SCODE_STRING       0b00000100U //   4  UTF-8
#define SCODE_REPEAT_N     0b00000110U //   6
#define SCODE_REPEAT_ID    0b00001000U //   8
#define SCODE_RET_N        0b00001010U //  10
#define SCODE_INT64        0b00001100U //  12  9.223.372.036.854.775.808, 9 quintilhões suportados
#define SCODE_FLOAT64      0b00001110U //  14  IEEE 754-2008
//                               ||||
//                              CODE|
//                                  RBIT
// REMAINING CONSTANTS
#define SCODE_FALSE        0b00010000U //  16
#define SCODE_TRUE         0b00010001U //  17
//                         0b00010010U //  18
//                         0b00010011U //  19   WE MAY ADD MORE CO NSTANTS IN THE FUTURE
//                         0b00010100U //  20
#define SCODE_LIST         0b00010101U //  21
#define SCODE_DICT         0b00010110U //  22
//                         0b00010111U //  23   quando a última mensagem seria um SCODE_LD_END
//                         0b00011000U //  24
#define SCODE_RET_ALL      0b00011111U //  31
// ENCODED 2 BITS AND EXTRA BYTES
#define SCODE_E_BINARY     0b00100000U //  32
#define SCODE_E_STRING     0b01000000U //  64 UTF-8
#define SCODE_E_REPEAT_N   0b01100000U //  96
#define SCODE_E_REPEAT_ID  0b10000000U // 128
#define SCODE_E_RET_N      0b10100000U // 196
#define SCODE_E_INT64      0b11000000U // 192 9.223.372.036.854.775.808, 9 quintilhões suportados
#define SCODE_E_FLOAT64    0b11100000U // 224 IEEE 754-2008
//                           ||||||||
//                          CODE|||||  CODE <<= 4; |= RBITS << 3; |= LEN
//                           RBITS|||
//                                LEN

typedef uint SCODE;
typedef u64 SWORD;

static inline void show (SCODE code, SWORD word, void* const restrict buff) {


    (void)buff;

    union { u64 u64_; double double_; } v;

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
            printf("EOF\n");
            break;
        default:
            printf("??????????? ? %02X \n", code);
            break;
    }
    // TODO: FIXME: ao chamar a função, pos tem q ue estar após o CODE+word -> tem que estar no começo da string/binary
}

#define DECODE_CONTINUE          0
#define DECODE_RET_ALL          -1 // Quebra todos os níveis independente de quantos forem
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
#define DECODE_BAD_CODE         -12

    // TODO: FIXME: pré gerar esses exceptions, e depois exceptions[-(ret+1)]

static PyObject* PyInvalid;

typedef struct Decoding Decoding;

struct Decoding {
    const u8* start;
    const u8* pos;
    const u8* end;
    uint repeatedsN;
    PyObject* objLast;
    PyObject* repeateds[];
};

#define dbg_decode(fmt, ...) fprintf(stderr, "DECODING: " fmt "\n", ##__VA_ARGS__)
#define dbg_decoding(fmt, ...) fprintf(stderr, "DECODING AT OFFSET %llu: " fmt "\n", (uintll)(pos - start), ##__VA_ARGS__)

#define start (decoding->start)
#define pos (decoding->pos)
#define end (decoding->end)
#define objLast (decoding->objLast)
#define repeateds (decoding->repeateds)
#define repeatedsN (decoding->repeatedsN)

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
    int ret = DECODE_CONTINUE;

    loop {

        PyObject* obj;
        SCODE code;
        SWORD word;
        char word_[sizeof(SWORD)];
        uint len;

        if (pos >= end)
            /* UÉ, CADÊ O FIM DESSA LISTA? */
            return DECODE_UNEXPECTED_LD_END;

        switch ((code = *pos++)) {
            // TODO: FIXME: uma versão não otimizada aqui, que lê primeiro ese length para determinar s eprecisa ler mais???

            case SCODE_NULL:

                dbg_decoding("NULL");

                obj = Py_None;

                break;

            case SCODE_INVALID:

                dbg_decoding("INVALID");

                obj = Py_Invalid;

                break;

            case SCODE_FALSE:

                dbg_decoding("FALSE");

                obj = Py_False;

                break;

            case SCODE_TRUE:

                dbg_decoding("TRUE");

                obj = Py_True;

                break;

            case SCODE_LIST:

                dbg_decoding("LIST");

                if ((obj = PyList_New(0)))
                    ret = decode_list(decoding, obj);
                else
                    ret = DECODE_ERR;

                break;

            case SCODE_DICT:

                dbg_decoding("DICT");

                if ((obj = PyDict_New()))
                    ret = decode_dict(decoding, obj);
                else
                    ret = DECODE_ERR;

                break;

            case SCODE_RET_ALL:

                dbg_decoding("RET ALL");

                ret = DECODE_RET_ALL;

                break;

            default:

                dbg_decoding("ENCODED");

                if (code >= 32) {
                    // versão extended
                    if ((pos + (len = (code & 0b111U))) <= end) {
                        ret = DECODE_INCOMPLETE;
                        break;
                    }

                } else {
                    word  = code;
                    word &= 0b0001U;
                    code &= 0b1110U;
                }

                if () {
#if 0
                    word  = *pos++; word <<= 8;
                    word |= *pos++; word <<= 8;
                    word |= *pos++; word <<= 8;
                    word |= *pos++; word <<= 8;
                    word |= *pos++; word <<= 8;
                    word |= *pos++; word <<= 8;
                    word |= *pos++; word <<= 8;
                    word |= *pos++;
#else
                    word = *((u64*)pos)++; // pos += 8; //__builtin_bswap64
#endif
                    // descarta os bits que não deveria ter lido
                    word &= ~(0xFFFFFFFFFFFFFFFFULL << (len*8));

                    word <<= 2; // abre espaço para recolocar os 2 bits
                    code >>= 3; // esquece o len
                    word |= code & 0b11U; // coloca os 2 bits

                    code >>= 4; // restaura o code da versão extended
                    code &= 0b1110U; // deixa só o tipo mesmo

                    /* Volta o que não era para ter andado */
                    pos -= 7 - len;

                switch (code) {
                    case SCODE_BINARY:
                        if ((pos + word) > end)
                            ret = DECODE_INCOMPLETE;
                        elif ((obj = PyUnicode_FromStringAndSize((const char*)pos, (Py_ssize_t)word) == NULL)
                            ret = DECODE_ERR;
                    break; case SCODE_STRING:
                        if ((pos + word) > end)
                            ret = DECODE_INCOMPLETE;
                        elif ((obj = PyBytes_FromStringAndSize((const char*)pos, (Py_ssize_t)word) == NULL)
                            ret = DECODE_ERR;
                    break; case SCODE_INT64:
                        if ((obj = PyLong_FromLongLong((intll)word)) == NULL)
                            ret = DECODE_ERR;
                    break; case SCODE_REPEAT_ID:
                        if (word < repeatedsUnknown)
                            obj = repeateds[word];
                        else
                            ret = DECODE_BAD_REPEAT_ID;
                    break; case SCODE_REPEAT_LAST:
                        if ((obj = objLast))
                            objRepetitions = word;
                        else
                            ret = DECODE_ERR;
                    break; case SCODE_RET:
                        dbg_decoding("RET");
                        ret = word;
                    break; default: /* SCODE_FLOAT64 */
                        *(SWORD*)word_ = word;
                        if ((obj = PyFloat_FromDouble(*(double*)word_)) == NULL)
                            ret = DECODE_ERR;
                    break;
                }

        }

        if (obj) {
            do {
                if (PyList_Append(list, obj))
                    abort();
            } while (objRepetitions--); // TODO: FIXME: caution: DOS here
            objLast = obj;
            obj = NULL;
            objRepetitions = 0;
        }

        // se retornou DECODE_RET_ALL, mantém ele
        // se retornou >  0 é para retornar mais esse N número de níveis; então retorna já descontando 1
        // se retornou == 0 (DECODE_CONTINUE) (, simplesment eterminou o nível de cima, e continua neste
        // se retornou <  0 é um erro, e deve ser mantido
        if (ret)
            return ret - (ret > 0);
    }
}

#undef start
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
#define value_set(x) \
    if ((this = (x)) == NULL) { \
        dbg_decoding("ROOT - NEW OBJECT FAILED"); \
        ret = DECODE_MALLOC; \
        break; \
    } \
    if (value) { \
        /* viu um valor antes deste, então ele pertence à repeateds list */ \
        if (repeateds == repeatedsEnd) { /* não cabe mais nenhum nela */ \
            dbg_decoding("ROOT - TOO MANY REPEATS"); \
            ret = DECODE_TOO_MANY_REPEATS; /* too many */ \
            break; \
        } /* cabe mais um, coloca ele */ \
        *repeateds++ = value; \
    } /* agora este será o último visto */ \
    value = this; \
    this = NULL; \

// TODO: FIXME: se repeatedsMax for 0, vai analisar primeiro e computar quantos precisa
static PyObject* decode (void* start_, void*  end_, uint max_) {
    (void)max_;

    int ret = DECODE_CONTINUE;

    PyObject* value = NULL;
    PyObject* this = NULL; // TODO: FIXME: está limpando ele?

    u8* start = start_;
    u8* pos = start_;
    u8* end = end_; //start +size
    uint repeatedsMax = 65536;

    // NOTA: suporta até 4GB -
    if ((end - start) > 0xFFFFFFFF) {
        dbg_decode("DECODE_TOOBIG");
        ret = DECODE_TOOBIG;
        goto RET;
    }

    Decoding* decoding = malloc(sizeof(Decoding) + repeatedsMax*sizeof(PyObject*));

    if (decoding == NULL) {
        dbg_decode("DECODE_MALLOC");
        ret = DECODE_MALLOC;
        goto RET;
    }

    PyObject** repeateds = decoding->repeateds;
    PyObject** repeatedsEnd = decoding->repeateds + repeatedsMax; /* Por enquanto o limite que tepos é de onde termina o buffer */

    loop {

        if (ret && ret != DECODE_JUNK_AT_END) {
            dbg_decoding("VISH");
            break; /* ENCONTROU UM ERRO */
        }

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

        if (ret == DECODE_JUNK_AT_END) {
            dbg_decoding("HUAHUAA");
            break; /* JÁ LEU OVALOR FINAL E AINDA NÃO CHEGOU AO FIM */
        }

        SCODE code;
        SWORD word;

        switch ((code = *pos++)) {

            /* NULL | INVALID | FALSE | TRUE | BINARY | STRING | INT64 | FLOAT64 a gente ainda não sabe se é o valor final ou só mais um na lista de repeateds; então vai pegando e colocando na lista, e lembrando sempre do último */
            case SCODE_BINARY:
            case SCODE_STRING:
            case SCODE_INT64: // TODO: FIXME: uma versão não otimizada aqui, que lê primeiro ese length para determinar s eprecisa ler mais???
            case SCODE_FLOAT64:
                dbg_decoding("ROOT - VISH");
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

                show(code, word, pos);


                // se pos > end, então desconsiderar o resultado (ou precisa ler mais)
                value_set(malloc(sizeof(PyObject)));

                //////value->code = code;
                //////value->word = word;
                //////value->data = pos; //?????????????????????????????


                break;

            /* NULL | INVALID | FALSE | TRUE não eram para aparecer no repeated, mas suporta, para facilitar o algorítimo */
            case SCODE_NULL:
                dbg_decoding("ROOT - NULL");
                value_set(Py_None);
                break;

            case SCODE_INVALID:
                dbg_decoding("ROOT - INVALID");
                value_set(Py_Invalid);
                break;

            case SCODE_FALSE:
                dbg_decoding("ROOT - FALSE");
                value_set(Py_False);
                break;

            case SCODE_TRUE:
                dbg_decoding("ROOT - TRUE");
                value_set(Py_True);
                break;

            case SCODE_LIST: /* Listas e dicionários não podem ser repeateds; Se agora um deles aparecer, eles se tornam o valor final. Não poderá haver nada depois deles. */

                dbg_decoding("ROOT - LIST");

                value_set(py_list_new());

                decoding->start = start;
                decoding->pos = pos;
                decoding->end = end;
                decoding->repeatedsN = repeateds - decoding->repeateds;
                decoding->objLast = NULL; // TODO: FIXME: NÃO DEVERIA LEMBRAR O ÚLTIMO? :S

                /* SE FOR ERRO PRESERVA O ERRO */
                if ((ret = decode_list(decoding, value)) == DECODE_CONTINUE)
                    ret = DECODE_JUNK_AT_END; /* ESTE NÓS SABEMOS QUE FOI O ÚLTIMO */
                elif (ret > 0)
                    ret = DECODE_ERR; /* AINDA ESTÁ QUEBRANDO NÍVELS, MAS ESTAMOS NO ROOT */

                break;

            case SCODE_DICT:

                dbg_decoding("ROOT - DICT");

                value_set(py_dict_new());

                decoding->start = start;
                decoding->pos = pos;
                decoding->end = end;
                decoding->repeatedsN = repeateds - decoding->repeateds;
                decoding->objLast = NULL; // TODO: FIXME: NÃO DEVERIA LEMBRAR O ÚLTIMO? :S

                /* SE FOR ERRO PRESERVA O ERRO */
                if ((ret = decode_dict(decoding, value)) == DECODE_CONTINUE)
                    ret = DECODE_JUNK_AT_END; /* ESTE NÓS SABEMOS QUE FOI O ÚLTIMO */
                elif (ret > 0)
                    ret = DECODE_ERR; /* AINDA ESTÁ QUEBRANDO NÍVELS, MAS ESTAMOS NO ROOT */

                break;

            default:

                dbg_decoding("UNEXPECTED TOKEN %02X", *pos);

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

//
static inline u8* encode_code_word (u8* restrict buff, uint code, u64 word) {

    if (code > 1) {
        /* Tem mais de um bit; transforma em modo extended */
        code <<= 4;
        /* Coloca os 2 últimos bits no código */
        code |= (word & 0b11U) << 3;
        /* Tira eles do word */
        word >>= 2;
        /* Coloca o length no código */
        if (word) // __builtin_clz(x) -> IF x IS 0, THE RESULT IS UNDEFINED
            code |= 7 - __builtin_clz(word)/8;
        /* Põe o código com os rbits e length */
        *buff++ = code;
        /* Põe o restante da word */
#if 0
        *buff++ = word; word >>= 8;
        *buff++ = word; word >>= 8;
        *buff++ = word; word >>= 8;
        *buff++ = word; word >>= 8;
        *buff++ = word; word >>= 8;
        *buff++ = word; word >>= 8;
        *buff++ = word; word >>= 8;
        *buff   = word;
        /* Relembra o length */
        code &= 0b111U;
        /* desanda os que andou a mais - TODO: FIXME:  esse "buff -= 7 - 8" (subtrair negativo = adicionar) funciona com ponteiros? :S */
        buff -= 7 - code;
#elif 0
        *buff++ = word;
        *buff++ = word >>  8;
        *buff++ = word >> 16;
        *buff++ = word >> 24;
        *buff++ = word >> 32;
        *buff++ = word >> 40;
        *buff++ = word >> 48;
        *buff   = word >> 56;
        /* Relembra o length */
        code &= 0b111U;
        /* desanda os que andou a mais */
        buff -= 7 - code;
#else
        *(u64*)buff = word; // NOTE: on other architectures, use __builtin_bswap64
        /* Relembra o length */
        code &= 0b111U;
        code += 1;
        buff += code; /* Anda só o que colocou */
#endif
    } else
        *buff++ = code | word;

    return buff;
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
    buff_++[0] = SCODE_LIST;
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0000000000000000LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0000000000000000LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0000000000000001LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0000000000000001LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0000000000000002LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0000000000000002LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0000000000000003LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0000000000000003LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0000000000000004LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0000000000000004LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0000000000000005LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0000000000000005LL);

    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x00000000000000AALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x00000000000000AALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x000000000000BBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x000000000000BBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0000000000CCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0000000000CCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x00000000DDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x00000000DDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x000000EEDDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x000000EEDDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x000077EEDDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x000077EEDDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x008877EEDDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x008877EEDDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x998877EEDDCCBBAALL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x998877EEDDCCBBAALL);

    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)  1000);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) -1000);

    union {
        double double_;
        u64 w;
    } x;

    x.double_ = -1.3;

    buff_ = encode_code_word(buff_, SCODE_FLOAT64, (SWORD)x.w);

    printf("egeg|%llu|\n", (uintll)sizeof (x));

    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0102030405060708LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0102030405060708LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD) 0x0807060504030201LL);
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)-0x0807060504030201LL);

    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000003FLL);  // 0b111111
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000007FLL);  // 0b1111111
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x000000000000000FFLL);  // 0b11111111
    buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x00000000000001FFULL);  // 0b111111111 0x1ff
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);
    //buff_ = encode_code_word(buff_, SCODE_INT64, (SWORD)0x0000000000000000LL);

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


PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),
PyExc_ValueError(""),

char errStr[512];

PyErr_SetString(PyExc_ValueError, errStr)

#define MODULE_NAME "jasf"

PyMODINIT_FUNC PyInit_spam(void)
{
    PyObject *m = PyModule_Create(&spammodule);

    if (m == NULL)
        return NULL;

    static const char exceptionsToCreate[2][] = {
        { "DecodeErrJunkAtEnd",   MODULE_NAME ".DecodeErrJunkAtEnd" },
        { "DecodeErrIncomplete",  MODULE_NAME ".DecodeErrIncomplete" },
        { "DecodeErrRuntime",     MODULE_NAME ".DecodeErrRuntime" },
        { "DecodeErrNoValue",     MODULE_NAME ".DecodeErrNoValue" },
        { "DecodeErrBadRepeatID", MODULE_NAME ".DecodeErrBadRepeatID" },
        { "DecodeErr",            MODULE_NAME ".DecodeErr" },
        { NULL, NULL }
    };

    const char[2]* exceptionToCreate = exceptionsToCreate;

    do {
        /* Cria a classe da exceção */
        exc = PyErr_NewException(exceptionToCreate[1], NULL, NULL); // PyExc_BaseException

        /* Guarda ela */
        Py_XINCREF(exc);
        Py_INCREF(exc);

        /* Coloca ela acessível pelo módulo */
        if (PyModule_AddObject(m, exceptionToCreate[0], exc))
            abort();

    } while (*++exceptionToCreate);

    /* Já que não tem essa constante no Python */
    Py_Invalid = Py_None;  // TODO: FIXME: o mesmo que "Invalid = object()"

    Py_XINCREF(Py_Invalid);
    Py_INCREF(Py_Invalid);

    /* Expõe ele */
    if (PyModule_AddObject(m, "Invalid", Py_Invalid))
        abort();

    /* Guarda eles */
    Py_INCREF(Py_None);
    Py_INCREF(Py_False);
    Py_INCREF(Py_True);

    return m;
}



typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    } CustomObject;

static PyTypeObject CustomType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "custom.Custom",
    .tp_doc = "Custom objects",
    .tp_basicsize = sizeof(CustomObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
};

static PyModuleDef custommodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "custom",
    .m_doc = "Example module that creates an extension type.",
    .m_size = -1,
};


//gcc -Wall -Wextra -std=gnu11 -fPIC -O2 -I/usr/include/python3.8 s2.c -shared -lpython3.8 -o s2.so

/*
dicionário:
    DICT_DEF  ID| ID|I D |D  END    ----> lembrar o ID deste dicionario definido
    todos os keys tem qu e estar no cache de repeateds

agora pode usar
DICT_DEFINED_NEW |DICT ID| VALUE |VALUE|VALUE|VALUE|
    basta usar SCODE_REPEATED1 :D
             ao vermos que o ID se refere a um dict def, já sabemos que é para criar baseado em um






SENÃO ESTAMOS EM UM DICT OU LIST AINDA , OS OPCODES
    SCODE_LD_END
    SCODE_LD_END ETC PODEM SER USADOS
    então temos ess emarcador SCODE_LD_END como DICT_DEF
    não é necess ario e SCODE_LD_END1 DICT_DEF_END <- necessário só se o próximo for o primeiro valor?
    DICT_DEF_END seria o ANOTHER_KEY :O
*/

//N O REPEATED LIST DE NÚMEROS, IR USANDO DIFF quando opróximo valor for ficar menor =D


//!!!!!!!!!CREATE A STREAMING VERSION
//SAVE THE CONTEXT
//LIST OD IDS
//add/remove/rename ids etc
//a cache of names
//a cache of dicts



// REPETIÇÕES SEGUIDAS, NÃO CONTAM AO PESAR A PARADA DOS IDS
//  pois vão  usar um REPEAT_LAST
// ou sseja, sempre qu e for dar um repeat_last, não conta com useds += 1;
