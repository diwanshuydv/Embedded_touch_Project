#include <stdint.h>
#include <stdbool.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <stdbool.h>

extern volatile bool dma_rx_complete;
extern volatile bool dma_rx_error;

void usart_clock_setup(void);
void usart_setup(void);
void gpio_setup(void);
char usart_read_char(void);
void usart_send_string(const char *str);
void usart_dma_receive(uint8_t *buffer, uint16_t size);