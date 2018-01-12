#ifndef _COMMON_H_
#define _COMMON_H_

#define DRY_RUN 1
#define REAL_RUN 0

extern uint16_t status_word;
extern uint16_t status_error;
extern uint16_t prev_status_error;

////////////////////////////////////////////////////////////////////////////////

void irq_init();

void software_reset();
void wdt_int_enable();
void wdt_int_disable();

void set_error_bit(uint8_t bit);
uint16_t any_error();

void set_status_bit(uint8_t bit, uint8_t val);
uint8_t get_status_bit(uint8_t bit);

void adc_init();
void adc_enable();
void adc_disable();
void adc_run();
uint8_t adc_running();
uint16_t adc_read();

////////////////////////////////////////////////////////////////////////////////

void irq_init()
{
  // Table 9-2. Interrupt 0 Sense Control
  // 2.2 Pin functions
  // nRF24L01+ IRQ - Active low.
  // By default the low level of INT0 generates an interrupt request.
  // This is exactly what required.

  // 9.3.2 GIMSK – General Interrupt Mask Register
  // Enable INT0
  GIMSK |= _BV(INT0);
}

// http://www.atmel.com/webdoc/avrlibcreferencemanual/FAQ_1faq_softreset.html
// NOTE Do not call it directly, e.g. during setup!

void wdt_init(void) __attribute__((naked)) __attribute__((section(".init3")));
void wdt_init(void)
{
  // TODO Handle WDRF and BORF flags!
  // reset_reason = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

void software_reset()
{
  wdt_enable(WDTO_15MS);
  wdt_int_disable();

  for (;;);
}

void wdt_int_enable()
{
  WDTCSR |= _BV(WDIE);
}

void wdt_int_disable()
{
  WDTCSR &= ~_BV(WDIE);
}

void set_error_bit(uint8_t bit)
{
  // NOTE CM*S_ERROR bit must be updated separately!
  status_error |= _BV(bit);
}

uint16_t any_error()
{
  return status_error | prev_status_error;
}

void set_status_bit(uint8_t bit, uint8_t val)
{
  if (val == 0)
    status_word &= ~_BV(bit);
  else
    status_word |= _BV(bit);
}

uint8_t get_status_bit(uint8_t bit)
{
  return (status_word >> bit) & 1;
}

// TODO Configure it with config.h

void adc_init()
{
  // [1] Table 16-6. ADC Prescaler Selections.
  // Skip resetting all ADPSx bits as they are initially zeroed,
  // but set only required one for 8x division factor.
  ADCSRA |= _BV(ADPS1) | _BV(ADPS0);

  // [1] Table 16-3. Voltage Reference Selections for ADC
  // By default VCC is used as analog reference. This is exactly what required.

  // [1] Table 16-4. Single-Ended Input channel Selections
  // By default ADC0 (PA0) is used as ADC input channel. This is exactly what required.
}

void adc_enable()
{
  power_adc_enable();
  ADCSRA |= _BV(ADEN);
}

void adc_disable()
{
  ADCSRA &= ~_BV(ADEN);
  power_adc_disable();
}

void adc_run()
{
  ADCSRA |= _BV(ADSC);
}

uint8_t adc_running()
{
  return (ADCSRA & _BV(ADSC)) != 0;
}

uint16_t adc_read()
{
  return ADC;
}

#endif
