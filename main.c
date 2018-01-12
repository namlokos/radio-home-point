#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include <util/delay.h>

#include <string.h>

#include "config.h"

// NOTE Must be after config.h
#include <tn84-nrf24l01/nRF24L01.h>

#include "radio-proto.h"
#include "common.h"

////////////////////////////////////////////////////////////////////////////////

uint32_t loop_counter;

uint8_t last_received_counter;
uint8_t last_sent_counter;

uint16_t error_cycle_counter;

uint16_t status_word;
uint16_t status_error;
uint16_t prev_status_error;
int16_t status_battery_voltage;
uint16_t status_humidity;
int16_t status_temperature;

int8_t sender_depth;

uint8_t reset_pre_cycles;

uint8_t no_nrf;

volatile uint8_t was_nrf_irq;

////////////////////////////////////////////////////////////////////////////////

uint16_t read_battery_voltage();
uint8_t collect_status(uint8_t dry_run);

void comm_with_server(uint8_t cmd, uint8_t *data, uint8_t data_len);
void handle_packet(uint8_t pkt[], uint8_t len);
uint8_t send_packet(uint8_t cmd, uint8_t *data, uint8_t data_len);

void power_down();
void sleep_or_reset();

void setup();
void loop();
int main();

////////////////////////////////////////////////////////////////////////////////

ISR(INT0_vect)
{
  ++was_nrf_irq;
}

ISR(WDT_vect)
{
  // 8.5.2 WDTCSR – Watchdog Timer Control and Status Register
  // So, WDIE should be already zero.
}

uint16_t read_battery_voltage()
{
  adc_enable();
  adc_run();

  // Figure 16-4. ADC Timing Diagram, First Conversion (Single Conversion Mode)
  // 25 ADC clock cycles is expected for the first conversion (after ADC enabled), 1 MHz MCU clock
  // and 8 Division Factor gives 250 kHz ADC clock. Thus, 200 uS conversion time is expected, what
  // is 200 CPU cycles.

  while (adc_running())
    ;

  // TODO 16.9 Noise Canceling Techniques

  uint16_t mv = (3300LL * adc_read()) / 1023;

  adc_disable();

  return mv;
}

uint8_t collect_status(uint8_t dry_run)
{
  if ((loop_counter % BAT_CHECK_PERIOD) == 0)
  {
    if (dry_run)
      return 1;

    status_battery_voltage = read_battery_voltage();

    set_status_bit(CM1S_BAT_VOLTAGE, 1);
    set_status_bit(CM1S_LOW_BATTERY, status_battery_voltage <= BAT_WARNING_MV);
  }
  else
  {
    set_status_bit(CM1S_BAT_VOLTAGE, 0);
  }

  if ((loop_counter % RUNTIME_SEND_PERIOD) == 0)
  {
    if (dry_run)
      return 1;

    set_status_bit(CM1S_RUNTIME, 1);
  }
  else
  {
    set_status_bit(CM1S_RUNTIME, 0);
  }

  if ((loop_counter % HT_SEND_PERIOD) == 0)
  {
    // TODO

    set_status_bit(CM1S_HT_DATA, 0);
  }
  else
  {
    set_status_bit(CM1S_HT_DATA, 0);
  }

  return 0;
}

void comm_with_server(uint8_t cmd, uint8_t *data, uint8_t data_len)
{
  ++sender_depth;

  if (sender_depth > MAX_PACKETS_LOOP)
  {
    set_error_bit(CM1E_COMM_DEPTH);
    --sender_depth;
    return;
  }

  // TODO
  send_packet(cmd, data, data_len);

  if (nrf_data_ready())
  {
    uint8_t packet[NRF_MAX_PAYLOAD_LENGTH];
    uint8_t len = sizeof(packet);

    nrf_get_data(packet, &len);

    if (len > 0 && len <= NRF_MAX_PAYLOAD_LENGTH)
      handle_packet(packet, len);
  }

  --sender_depth;
}

// Packet direction: Controller -> Device.
//
// Byte 0:
//    Bits 7..5: protocol version
//    Bits 4..0: target device id
// Byte 1: 8-bit wrapping counter (starts from zero, per direction)
// Byte 2: Command from Communication Model 1 Set
// Bute 3+: Command dependent...

void handle_packet(uint8_t pkt[], uint8_t len)
{
  if (len < 3)
  {
    // Too small packet. Just ignore it!
    return;
  }

  // Byte 0: protocol version and sender device id
  uint8_t p = 0;

  uint8_t target_dev_id = pkt[p] & 0x1f;

  if (target_dev_id != RADIO_DEVICE_ID && target_dev_id != RADIO_BROADCAST_DEVICE_ID)
  {
    // The packet could be addressed to another target. Just ignore it!
    return;
  }

  uint8_t proto_version = pkt[p] >> 5;

  if (proto_version != RADIO_PROTO_VERSION)
  {
    set_error_bit(CM1E_PROTO_VERSION);
    return;
  }

  // Byte 1: wrapping counter
  p += 1;

  uint8_t received_counter = pkt[p];

  ++last_received_counter;

  if (received_counter != last_received_counter)
  {
    set_error_bit(CM1E_WRONG_COUNTER);
    last_received_counter = received_counter;
  }

  // Byte 2: mandatory command
  p += 1;

  for (uint8_t i = 0; p < len; ++i)
  {
    uint8_t cmd = pkt[p];

    // Next byte: current command data or next command.
    p += 1;

    if (cmd == CM1C_NOP)
    {
      // Do nothing.
    }
    else if (cmd == CM1C_PING)
    {
      comm_with_server(CM1C_PONG, &pkt[p], 1);
      p += 1;
    }
    else if (cmd == CM1C_PONG)
    {
      set_error_bit(CM1E_UNEXPECTED_COMMAND);
      p += 1;
    }
    else if (cmd == CM1C_RESET)
    {
      reset_pre_cycles = pkt[p];
      p += 1;
    }
    else
    {
      set_error_bit(CM1E_UNKNOWN_COMMAND);
      return;
    }
  }
}

// Packet direction: Device -> Controller.
//
// Byte 0:
//    Bits 7..5: protocol version
//    Bits 4..0: sender device id (up to 30 devices + 1 controller + 1 reserved)
// Byte 1: 8-bit wrapping counter (starts from zero, per direction)
// Byte 2: Command from Communication Model 1 Set
// Bute 3+: Command dependent...

uint8_t send_packet(uint8_t cmd, uint8_t *data, uint8_t data_len)
{
  // Command data + command itself (1) + header (2):
  if (data_len + 1 + 2 > NRF_MAX_PAYLOAD_LENGTH)
  {
    set_error_bit(CM1E_INT_2);
    return 1;
  }

  uint8_t pkt[NRF_MAX_PAYLOAD_LENGTH];
  uint8_t len = 0;

  pkt[len] = (RADIO_PROTO_VERSION << 5) | RADIO_DEVICE_ID;
  len += 1;

  pkt[len] = last_sent_counter;
  len += 1;

  ++last_sent_counter;

  pkt[len] = cmd;
  len += 1;

  if (cmd == CM1C_STATUS)
  {
    // Take into account previous cycle errors also!

    if (any_error())
      set_status_bit(CM1S_ERROR, 1);

    memcpy(&pkt[len], &status_word, sizeof(status_word));
    len += 2;

    if (get_status_bit(CM1S_ERROR))
    {
      uint16_t error = any_error();

      memcpy(&pkt[len], &error, sizeof(error));
      len += 2;
    }

    if (get_status_bit(CM1S_RUNTIME))
    {
      uint32_t runtime = 0; // millis

      memcpy(&pkt[len], &runtime, sizeof(runtime));
      len += 4;

      memcpy(&pkt[len], &loop_counter, sizeof(loop_counter));
      len += 4;
    }

    if (get_status_bit(CM1S_BAT_VOLTAGE))
    {
      memcpy(&pkt[len], &status_battery_voltage, sizeof(status_battery_voltage));
      len += 2;
    }

    if (get_status_bit(CM1S_HT_DATA))
    {
      memcpy(&pkt[len], &status_humidity, sizeof(status_humidity));
      len += 2;

      memcpy(&pkt[len], &status_temperature, sizeof(status_temperature));
      len += 2;
    }
  }
  else if (cmd == CM1C_PONG)
  {
    memcpy(&pkt[len], data, data_len);
    len += data_len;
  }
  else
  {
    set_error_bit(CM1E_INT_1);
    return 1;
  }

  was_nrf_irq = 0;
  nrf_send_data(pkt, len);

  uint8_t status;

  // XXX WDT RESET does not work here!

  while (nrf_sending(&status) != 0 && was_nrf_irq == 0)
    power_down();

  status = nrf_sent_ok(status);

  if (status == 0)
    set_error_bit(CM1E_CANT_DELIVER);

  return !status;
}

// Table 6-5.  Start-up Times for the Internal Calibrated RC Oscillator Clock Selection
// Start-up Time from Power-down: 6 CK

void power_down()
{
  wdt_enable(POWER_DOWN_TIMEOUT);
  wdt_int_enable();

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();

  if (was_nrf_irq == 0)
  {
    sleep_enable();
    sleep_bod_disable();
    sei();
    sleep_cpu();
    sleep_disable();
  }

  sei();

  wdt_enable(RESET_WDT_TIMEOUT);
  wdt_int_disable();
}

void sleep_or_reset()
{
  if (status_error != 0)
  {
    if (error_cycle_counter < 0xffff)
      ++error_cycle_counter;
  }
  else
  {
    error_cycle_counter = 0;
  }

  if (error_cycle_counter >= ERROR_CYCLES_RESET)
    software_reset();

  if (reset_pre_cycles > 0)
  {
    --reset_pre_cycles;

    if (reset_pre_cycles == 0)
      software_reset();
  }

  power_down();
}

void setup()
{
  wdt_enable(RESET_WDT_TIMEOUT);

  adc_init();
  adc_disable();

  // TODO millis init
  power_timer0_disable();
  power_timer1_disable();

  nrf_init();

  uint8_t buf1;

  nrf_read_register(CONFIG, &buf1, 1);
  no_nrf = (buf1 & 0x80) >> 7;

  // ... to start from zero!
  --last_received_counter;
  --loop_counter;
}

void loop()
{
  ++loop_counter;

  if (no_nrf == 0)
  {
    if (collect_status(DRY_RUN) != 0)
    {
      prev_status_error = status_error;
      status_error = 0;
      set_status_bit(CM1S_ERROR, 0);

      power_usi_enable();
      nrf_power_up_rx(1);
      _delay_ms(5);

      collect_status(REAL_RUN);
      comm_with_server(CM1C_STATUS, 0, 0);

      nrf_power_down();
      power_usi_disable();
    }
  }
  else
  {
    // Fake value for reset counter.
    status_error = 0xffff;
  }

  sleep_or_reset();
}

int main()
{
  setup();

  for (;;)
    loop();

  return 0;
}
