#ifndef _MEINRADIO_CONST_H
#define _MEINRADIO_CONST_H


typedef unsigned radio_param_t;

#define CC2530_RF_CHANNEL_MIN     11
#define CC2530_RF_CHANNEL_MAX     26
#define CC2530_RF_CHANNEL_SPACING  5

#define CHECKSUM_LEN 2

typedef enum {
  RADIO_RESULT_OK,
  RADIO_RESULT_NOT_SUPPORTED,
  RADIO_RESULT_INVALID_VALUE,
  RADIO_RESULT_ERROR
} radio_result_t;

#define COMMS_RX_ERR_NO_PACKETS			(-1)
#define COMMS_RX_ERR_INVALID_PACKET		(-2)
#define COMMS_RX_ERR_MIC_FAIL			(-3)

/* Radio return values for transmissions. */
enum {
  RADIO_TX_OK,
  RADIO_TX_ERR,
  RADIO_TX_COLLISION,
  RADIO_TX_NOACK,
};

/*---------------------------------------------------------------------------*/
#define CCA_THRES_USER_GUIDE          0xF8
#define CCA_THRES_ALONE               0xFC   /* -4-76=-80dBm when CC2530 operated alone or with CC2591 in LGM */
#define CCA_THR_HGM                   0x06   /* 6-76=-70dBm when CC2530 operated with CC2591 in HGM */
#define CORR_THR                      0x14
/*---------------------------------------------------------------------------*/
#define CC2530_RF_MAX_PACKET_LEN      127
#define CC2530_RF_MIN_PACKET_LEN        4
/*---------------------------------------------------------------------------*/
#define CC2530_RF_CCA_CLEAR             1
#define CC2530_RF_CCA_BUSY              0

/* Wait for RSSI to be valid. */
#define CC2530_RF_CCA_VALID_WAIT()  while(!(RSSISTAT & RSSIST))
/*---------------------------------------------------------------------------
 * Command Strobe Processor
 *---------------------------------------------------------------------------*/
/* OPCODES */
#define CSP_OP_ISRXON                0xE3
#define CSP_OP_ISTXON                0xE9
#define CSP_OP_ISTXONCCA             0xEA
#define CSP_OP_ISRFOFF               0xEF
#define CSP_OP_ISFLUSHRX             0xED
#define CSP_OP_ISFLUSHTX             0xEE

#define CC2530_CSP_ISRXON()    do { RFST = CSP_OP_ISRXON; } while(0)
#define CC2530_CSP_ISTXON()    do { RFST = CSP_OP_ISTXON; } while(0)
#define CC2530_CSP_ISTXONCCA() do { RFST = CSP_OP_ISTXONCCA; } while(0)
#define CC2530_CSP_ISRFOFF()   do { RFST = CSP_OP_ISRFOFF; } while(0)

/* OP x 2 for flushes */
#define CC2530_CSP_ISFLUSHRX()  do { \
  RFST = CSP_OP_ISFLUSHRX; \
  RFST = CSP_OP_ISFLUSHRX; \
} while(0)
#define CC2530_CSP_ISFLUSHTX()  do { \
  RFST = CSP_OP_ISFLUSHTX; \
  RFST = CSP_OP_ISFLUSHTX; \
} while(0)


/* Local RF Flags */
#define RX_ACTIVE 0x80
#define WAS_OFF 0x10
#define RF_ON 0x01

/* Bit Masks for the last byte in the RX FIFO */
#define CRC_BIT_MASK 0x80
#define LQI_BIT_MASK 0x7F
/* RSSI Offset */
#define RSSI_OFFSET 73

enum {

    /* Radio power mode determines if the radio is on
      (RADIO_POWER_MODE_ON) or off (RADIO_POWER_MODE_OFF). */
    RADIO_PARAM_POWER_MODE,

    /*
     * Channel used for radio communication. The channel depends on the
     * communication standard used by the radio. The values can range
     * from RADIO_CONST_CHANNEL_MIN to RADIO_CONST_CHANNEL_MAX.
     */
    RADIO_PARAM_CHANNEL,

    /* Personal area network identifier, which is used by the address filter. */
    RADIO_PARAM_PAN_ID,

    /* Short address (16 bits) for the radio, which is used by the address
       filter. */
    RADIO_PARAM_16BIT_ADDR,

    /*
     * Radio receiver mode determines if the radio has address filter
     * (RADIO_RX_MODE_ADDRESS_FILTER) and auto-ACK (RADIO_RX_MODE_AUTOACK)
     * enabled. This parameter is set as a bit mask.
     */
    RADIO_PARAM_RX_MODE,

    /*
     * Radio transmission mode determines if the radio has send on CCA
     * (RADIO_TX_MODE_SEND_ON_CCA) enabled or not. This parameter is set
     * as a bit mask.
     */
    RADIO_PARAM_TX_MODE,

    /*
     * Transmission power in dBm. The values can range from
     * RADIO_CONST_TXPOWER_MIN to RADIO_CONST_TXPOWER_MAX.
     *
     * Some radios restrict the available values to a subset of this
     * range.  If an unavailable TXPOWER value is requested to be set,
     * the radio may select another TXPOWER close to the requested
     * one. When getting the value of this parameter, the actual value
     * used by the radio will be returned.
     */
    RADIO_PARAM_TXPOWER,

    /*
     * Clear channel assessment threshold in dBm. This threshold
     * determines the minimum RSSI level at which the radio will assume
     * that there is a packet in the air.
     *
     * The CCA threshold must be set to a level above the noise floor of
     * the deployment. Otherwise mechanisms such as send-on-CCA and
     * low-power-listening duty cycling protocols may not work
     * correctly. Hence, the default value of the system may not be
     * optimal for any given deployment.
     */
    RADIO_PARAM_CCA_THRESHOLD,

    /* Received signal strength indicator in dBm. */
    RADIO_PARAM_RSSI,

    /* RSSI of the last received packet */
    RADIO_PARAM_LAST_RSSI,

    /* Link quality of the last received packet */
    RADIO_PARAM_LAST_LINK_QUALITY,

    /*
     * Long (64 bits) address for the radio, which is used by the address filter.
     * The address is specified in network byte order.
     *
     * Because this parameter value is larger than what fits in radio_value_t,
     * it needs to be used with radio.get_object()/set_object().
     */
    RADIO_PARAM_64BIT_ADDR,

    /* Last packet timestamp, of type rtimer_clock_t.
     * Because this parameter value mat be larger than what fits in radio_value_t,
     * it needs to be used with radio.get_object()/set_object(). */
    RADIO_PARAM_LAST_PACKET_TIMESTAMP,

    /* Constants (read only) */

    /* The lowest radio channel. */
    RADIO_CONST_CHANNEL_MIN,
    /* The highest radio channel. */
    RADIO_CONST_CHANNEL_MAX,

    /* The minimum transmission power in dBm. */
    RADIO_CONST_TXPOWER_MIN,
    /* The maximum transmission power in dBm. */
    RADIO_CONST_TXPOWER_MAX
};

/* Radio power modes */
enum {
    RADIO_POWER_MODE_OFF,
    RADIO_POWER_MODE_ON
};

#endif