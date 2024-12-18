#include "hal.h"
#include "asmUtil.h"
#include "eeprom.h"
#include "printf.h"
#include <stdlib.h>
#include "dma.h"
#include "delay.h"
#include "wdt.h"
#include "settings.h"

// port P1 pins
#define EEPROM_MOSI 6
#define EEPROM_MISO 7
#define EEPROM_CLK 5
#define EEPROM_CS 4

static uint32_t __xdata mEepromSize;
static uint8_t __xdata mOpcodeErz4K = 0, mOpcodeErz32K = 0, mOpcodeErz64K = 0;

uint32_t eepromGetSize(void) {
    return mEepromSize;
}

uint8_t eepromByte(uint8_t b) {
    U1DBUF = b;
    while (U1CSR & 0x01);
    return U1DBUF;
}

void eepromPrvSelect(void) {
    P1 &= ~(1 << EEPROM_CS);
}

void eepromPrvDeselect(void) {
    P1 |= 1 << EEPROM_CS;
}

void eepromReadStart(uint32_t addr) __reentrant {
    eepromPrvSelect();
    eepromByte(0x03);
    eepromByte(addr >> 16);
    eepromByte(addr >> 8);
    eepromByte(addr & 0xff);
}

void eepromRead(uint32_t addr, __xdata void *dstP, uint16_t len) __reentrant {
    __xdata uint8_t *dst = (__xdata uint8_t *)dstP;

    eepromPrvSelect();
    eepromByte(0x03);
    eepromByte(addr >> 16);
    eepromByte(addr >> 8);
    eepromByte(addr & 0xff);

    dmaSPIRead(dst, len);
    eepromPrvDeselect();
}

static void eepromPrvSimpleCmd(uint8_t cmd) {
    eepromPrvSelect();
    eepromByte(cmd);
    eepromPrvDeselect();
}

static bool eepromPrvBusyWait(void) {
    uint8_t val;

    eepromPrvSelect();
    eepromByte(0x05);
    while ((val = eepromByte(0x00)) & 1) {
        ;
    }
    eepromPrvDeselect();

    return true;
}

static bool eepromWriteLL(uint32_t addr, const __xdata void *srcP, uint16_t len) __reentrant {
    const __xdata uint8_t *src = (const __xdata uint8_t *)srcP;

    eepromPrvSimpleCmd(0x06);

    eepromPrvSelect();
    eepromByte(0x02);
    eepromByte(addr >> 16);
    eepromByte(addr >> 8);
    eepromByte(addr & 0xff);

    while (len--)
        eepromByte(*src++);
    eepromPrvDeselect();

    return eepromPrvBusyWait();
}

void eepromDeepPowerDown(void) {
    eepromPrvSimpleCmd(0xb9);
}

static void eepromPrvWakeFromPowerdown(void) {
#ifdef DEBUGEEPROM
    pr("EEPROM: Waking eeprom...\n");
#endif
    eepromPrvSimpleCmd(0xab);
#ifdef DEBUGEEPROM
    pr("EEPROM: Eeprom Awake\n");
#endif
}

// #pragma callee_saves eepromPrvSfdpRead
static void eepromPrvSfdpRead(uint16_t ofst, __xdata uint8_t *dst, uint8_t len) __reentrant {
    eepromPrvSelect();
    eepromByte(0x5a);  // cmd
    eepromByte(0);     // addr
    eepromByte(ofst >> 8);
    eepromByte(ofst);
    eepromByte(0x00);  // dummy
    dmaSPIRead(dst, len);
    eepromPrvDeselect();
}

void eepromSetupPins(bool enable) {
    if (enable) {
        // enable pins
        U1CSR &= ~(UCSR_MODE);
        U1BAUD = 0;
        U1GCR |= 16;
        U1GCR |= (1 << 5);   // msb first
        PERCFG |= (1 << 1);  // USART1 alt location
        eepromPrvDeselect();
        P1DIR |= (1 << EEPROM_MOSI) | (1 << EEPROM_CLK) | (1 << EEPROM_CS);
        P1SEL |= (1 << EEPROM_CLK) | (1 << EEPROM_MISO) | (1 << EEPROM_MOSI);
    } else {
        // shutdown pins
        P1DIR &= ~((1 << EEPROM_MOSI) | (1 << EEPROM_CLK) | (1 << EEPROM_CS));
        P1 &= ~((1 << EEPROM_MOSI) | (1 << EEPROM_CLK) | (1 << EEPROM_CS));
        P1SEL &= ~((1 << EEPROM_CLK) | (1 << EEPROM_MISO) | (1 << EEPROM_MOSI));
    }
}

__bit eepromInit(void) __reentrant {
    static bool __xdata fullInit = true;
    if (!fullInit) {
        eepromPrvWakeFromPowerdown();
        return true;
    }
    fullInit = false;

    __xdata uint8_t *buf = malloc(8);
    uint8_t i, nParamHdrs;
    __xdata uint8_t *tempBufferE = malloc(320);

    eepromPrvWakeFromPowerdown();

    // process SFDP

    eepromPrvSfdpRead(0, buf, 8);
    if (buf[0] != 0x53 || buf[1] != 0x46 || buf[2] != 0x44 || buf[3] != 0x50 || buf[7] != 0xff) {
#ifdef DEBUGEEPROM
        pr("SFDP: header not found\n");
#endif
        __bit valid = false;

        // try manual ID for chips we know of
        eepromPrvSelect();
        eepromByte(0x90);
        eepromByte(0x00);
        eepromByte(0x00);
        eepromByte(0x00);
        uint8_t c = eepromByte(0);
        switch (c) {
            case 0xc2:  // old macronix chips
                valid = true;
                mOpcodeErz4K = 0x20;
                switch (eepromByte(0)) {
                    case 0x05:  // MX25V512
                        mEepromSize = 0x00010000ul;
                        break;

                    case 0x12:  // MX25V4005
                        mEepromSize = 0x00080000ul;
                        break;

                    default:
                        valid = false;
                        break;
                }
                break;
            case 0xEF:  // winbond
                valid = true;
                mOpcodeErz4K = 0x20;
                switch (eepromByte(0)) {
                    case 0x13:  // W25Q80DV without SFDP
                        mEepromSize = 0x00080000ul;
                        break;
                    default:
                        valid = false;
                }
                break;
            case 0xC8:  // gigadevice
                valid = true;
                mOpcodeErz4K = 0x20;
                c = eepromByte(0);
                switch (c) {
                    case 0x12:
                        mEepromSize = 0x00080000ul;
                        break;
                    case 0x13:
                        mEepromSize = 0x00100000ul;
                        break;
                    default:
                        break;
                }
                break;
            default:
                pr("Unknown EEPROM type:  0x%02X-", c);
                mOpcodeErz4K = 0x20;
                mEepromSize = 0x00080000ul;
                c = eepromByte(0);
                pr("0x%02X\n", c);
                pr("Assuming 512k with 4k sectors\n");
                break;
        }
        eepromPrvDeselect();
        free(tempBufferE);
        return valid;
    }
    if (buf[5] != 0x01) {
#ifdef DEBUGEEPROM
        pr("SFDP: version wrong: %u.%d\n", buf[5], buf[4]);
#endif
        return false;
    }
    nParamHdrs = buf[6];
    if (nParamHdrs == 0xff)  // that case is very unlikely and we just do not care
        nParamHdrs--;

    // now we need to find the JEDEC parameter table header
    for (i = 0; i <= nParamHdrs; i++) {
        eepromPrvSfdpRead(mathPrvMul8x8(i, 8) + 8, buf, 8);
        if (buf[0] == 0x00 && buf[2] == 0x01 && buf[3] >= 9) {
            uint8_t j;

            eepromPrvSfdpRead(*(__xdata uint16_t *)(buf + 4), tempBufferE, 9 * 4);
            if ((tempBufferE[0] & 3) != 1) {
#ifdef DEBUGEEPROM
                pr("SFDP: no 4K ERZ\n");
#endif
                break;
            }
            if (!(tempBufferE[0] & 0x04)) {
#ifdef DEBUGEEPROM
                pr("SFDP: no large write buf\n");
#endif
                break;
            }
            if ((tempBufferE[2] & 0x06)) {
#ifdef DEBUGEEPROM
                pr("SFDP: addr.len != 3\n");
#endif
                break;
            }

            if (!tempBufferE[1] || tempBufferE[1] == 0xff) {
#ifdef DEBUGEEPROM
                pr("SFDP: 4K ERZ opcode invalid\n");
#endif
                break;
            }
            mOpcodeErz4K = tempBufferE[1];

            if (tempBufferE[7] & 0x80) {
#ifdef DEBUGEEPROM
                pr("SFDP: device too big\n");
#endif
                break;
            } else {
                uint8_t t;

                if (t = tempBufferE[7])
                    mEepromSize = 0x00200000UL;
                else if (t = tempBufferE[6])
                    mEepromSize = 0x00002000UL;
                else if (t = tempBufferE[5])
                    mEepromSize = 0x00000020UL;
                else {
#ifdef DEBUGEEPROM
                    pr("SFDP: device so small?!\n");
#endif
                    break;
                }

                while (t) {
                    mEepromSize <<= 1;
                    t >>= 1;
                }
            }

            // get erase opcodes
            for (j = 0x1c; j < 0x24; j += 2) {
                uint8_t instr = tempBufferE[j + 1];

                if (!instr || instr == 0xff)
                    continue;

                switch (tempBufferE[j]) {
                    case 0x0c:
                        if (mOpcodeErz4K != instr) {
#ifdef DEBUGEEPROM
                            pr("4K ERZ opcode disagreement\n");
#endif
                            return false;
                        }
                        break;

                    case 0x0f:  // 32K erase
                        mOpcodeErz32K = instr;
                        break;

                    case 0x10:  // 64K erase
                        mOpcodeErz64K = instr;
                        break;
                }
            }

            pr("EEPROM accepted\n");
            pr(" ERZ opcodes: \n");
            if (mOpcodeErz4K)
                pr(" 4K:  %02xh\n", mOpcodeErz4K);
            if (mOpcodeErz32K)
                pr(" 32K: %02xh\n", mOpcodeErz32K);
            if (mOpcodeErz64K)
                pr(" 64K: %02xh\n", mOpcodeErz64K);
            pr(" Size: 0x%*08lx\n", (uint16_t)&mEepromSize);

            free(tempBufferE);
            free(buf);
            return true;
        }
    }
#ifdef DEBUGEEPROM
    pr("SFDP: no JEDEC table of expected version found\n");
#endif
    return false;
}

bool eepromWrite(uint32_t addr, const __xdata void *srcP, uint16_t len) __reentrant {
    const __xdata uint8_t *src = (const __xdata uint8_t *)srcP;

    while (len) {
        uint16_t lenNow = EEPROM_WRITE_PAGE_SZ - (addr & (EEPROM_WRITE_PAGE_SZ - 1));

        if (lenNow > len)
            lenNow = len;

        if (!eepromWriteLL(addr, src, lenNow))
            return false;

        addr += lenNow;
        src += lenNow;
        len -= lenNow;
    }
    return true;
}

bool eepromErase(uint32_t addr, uint16_t nSec) __reentrant {
    uint8_t now;

    if (((uint16_t)addr) & 0x0fff) {
        pr("EEPROM: unaligned?\n");
        return false;
    }

    for (; nSec; nSec -= now) {
        eepromPrvSimpleCmd(0x06);
        eepromPrvSelect();

        if (nSec >= 16 && !(uint16_t)addr && mOpcodeErz64K) {  // erase 64K

            eepromByte(mOpcodeErz64K);
            now = 16;
        } else if (nSec >= 8 && !(((uint16_t)addr) & 0x7fff) && mOpcodeErz32K) {  // erase 32K

            eepromByte(mOpcodeErz32K);
            now = 8;
        } else {  // erase 4K

            eepromByte(mOpcodeErz4K);
            now = 1;
        }

        eepromByte(addr >> 16);
        eepromByte(addr >> 8);
        eepromByte(addr);
        wdtPet();
        eepromPrvDeselect();
        wdtPet();
        if (!eepromPrvBusyWait()) {
            pr("EEPROM: timeout?");
            return false;
        }

        addr += mathPrvMul16x8(EEPROM_ERZ_SECTOR_SZ, now);
    }
    return true;
}

void eepromOtpModeEnter(void) {
    eepromPrvSimpleCmd(0xb1);
}

void eepromOtpModeExit(void) {
    eepromPrvSimpleCmd(0xc1);
}