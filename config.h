#ifndef _CONFIG_H_
#define _CONFIG_H_

#define NRF_CFG_CE_PIN PA2
#define NRF_CFG_CSN_PIN PA3
#define NRF_CFG_CHANNEL 1

#define NRF_CFG_CONFIG 0x0C // 16bit CRC
#define NRF_CFG_RF_SETUP 0x0E // 2Mbps 0dBm
#define NRF_CFG_SETUP_RETR 0x3F // ARD: 0x3 (1000 us), ARC: 0xF (15x)

uint8_t nrf_rx_addr[] = { 0x01, 0xAA, 0xAA, 0xAA, 0xAA };
uint8_t nrf_tx_addr[] = { 0x00, 0xAA, 0xAA, 0xAA, 0xAA };

// This device unique id.
#define RADIO_DEVICE_ID 0x01

// This is a common case power_down() timeout:
// it is used in main loop and some intermediate sleeps!
#define POWER_DOWN_TIMEOUT WDTO_8S

// Watchdog hardware reset timeout!
#define RESET_WDT_TIMEOUT WDTO_2S

// Battery low voltage warning threshold (in mV).
#define BAT_WARNING_MV 1800

// Battery voltage check period (in loop~seconds). Max: 2040 (34 min)!
#define BAT_CHECK_PERIOD ((300) / 8)

// Run-time dump period (in loop~seconds). Max: 2040 (34 min)!
#define RUNTIME_SEND_PERIOD ((8) / 8)

// Humidity and Temperature send period (in loop~seconds). Max: 2040 (34 min)!
#define HT_SEND_PERIOD ((300) / 8)

// Error loops timeout (in a row, in loop~seconds) until MCU reset. Max: 524280 (6 days)!
#define ERROR_CYCLES_RESET ((3600LL * 24) / 8)

// Maximum loop depth for comm_with_server().
#define MAX_PACKETS_LOOP 2

#endif
