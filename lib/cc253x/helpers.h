// borrowed this from TI DN113

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned char UINT8;
#define HIBYTE(a) (BYTE)((WORD)(a) >> 8)
#define LOBYTE(a) (BYTE)(WORD)(a)

#define SET_WORD(regH, regL, word) \
    do {                           \
        (regH) = HIBYTE(word);     \
        (regL) = LOBYTE(word);     \
    } while (0)

