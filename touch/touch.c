#include "touch.h"
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/i2c.h>
#include <stdint.h>
#include <stddef.h>

#define STMPE811_ADDR 0x41

/* Registers */
#define STMPE811_SYS_CTRL1 0x03
#define STMPE811_SYS_CTRL2 0x04
#define STMPE811_SPI_CFG 0x08
#define STMPE811_INT_CTRL 0x0A
#define STMPE811_INT_EN 0x09
#define STMPE811_INT_STA 0x0B
#define STMPE811_GPIO_EN 0x27
#define STMPE811_ADC_CTRL1 0x20
#define STMPE811_ADC_CTRL2 0x21
#define STMPE811_TSC_CTRL 0x40
#define STMPE811_TSC_CFG 0x41
#define STMPE811_FIFO_TH 0x4A
#define STMPE811_FIFO_STA 0x4B
#define STMPE811_FIFO_SIZE 0x4C
#define STMPE811_TSC_DATA_X 0x4D
#define STMPE811_TSC_DATA_Y 0x4F
#define STMPE811_TSC_DATA_Z 0x51
#define STMPE811_TSC_FRACTION_Z 0x56
#define STMPE811_TSC_I_DRIVE 0x58

// Read a single register
static uint8_t toc_read_reg(uint8_t reg) {
    uint8_t val = 0;
    i2c_transfer7(I2C3, STMPE811_ADDR, &reg, 1, &val, 1);
    return val;
}

// Write a single register
static void toc_write_reg(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    i2c_transfer7(I2C3, STMPE811_ADDR, b, 2, NULL, 0);
}

void touch_init(void) {
    // 1. Setup I2C GPIOs
    rcc_periph_clock_enable(RCC_I2C3);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOC);

    // SCL -> PA8 (AF4)
    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO8);
    gpio_set_af(GPIOA, GPIO_AF4, GPIO8);
    
    // SDA -> PC9 (AF4)
    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9);
    gpio_set_af(GPIOC, GPIO_AF4, GPIO9);

    // 2. Setup I2C3 peripheral
    i2c_peripheral_disable(I2C3);
    i2c_set_clock_frequency(I2C3, 42); // 42 MHz APB1
    i2c_set_ccr(I2C3, 210);            // 100 kHz @ 42MHz
    i2c_set_trise(I2C3, 43);
    i2c_peripheral_enable(I2C3);

    // Simple delay for hardware stabilization
    for (int i = 0; i < 100000; i++) __asm__("nop");

    // 3. Reset STMPE811
    toc_write_reg(STMPE811_SYS_CTRL1, 0x02);
    for (int i = 0; i < 100000; i++) __asm__("nop");
    toc_write_reg(STMPE811_SYS_CTRL1, 0x00);
    for (int i = 0; i < 100000; i++) __asm__("nop");

    // 4. Initialize Touch settings
    // Turn on clocks for ADC and TSC
    toc_write_reg(STMPE811_SYS_CTRL2, 0x00);
    
    // Configure ADC: 12-bit, 80 clocks per conversion, internal ref
    toc_write_reg(STMPE811_ADC_CTRL1, 0x49);

    // ADC Clock 3.25 MHz
    toc_write_reg(STMPE811_ADC_CTRL2, 0x01);

    // Configure Touch controller
    // Ave 4, touch delay 1ms, settling time 1ms
    toc_write_reg(STMPE811_TSC_CFG, 0x9A);
    
    // FIFO Threshold = 1
    toc_write_reg(STMPE811_FIFO_TH, 0x01);
    
    // Reset FIFO
    toc_write_reg(STMPE811_FIFO_STA, 0x01);
    toc_write_reg(STMPE811_FIFO_STA, 0x00);
    
    // Fractional Z & Drive current
    toc_write_reg(STMPE811_TSC_FRACTION_Z, 0x07);
    toc_write_reg(STMPE811_TSC_I_DRIVE, 0x01);
    
    // Enable Touch functionality (TSC_CTRL: Enable = bit 0, XY tracking = bit 1, Z = bit 2 - so write 0x03 for XY)
    toc_write_reg(STMPE811_TSC_CTRL, 0x03);

    // Enable interrupts
    toc_write_reg(STMPE811_INT_EN, 0x01);
    toc_write_reg(STMPE811_INT_CTRL, 0x01);
    toc_write_reg(STMPE811_FIFO_STA, 0x01);
    toc_write_reg(STMPE811_FIFO_STA, 0x00);
}

void touch_clear_fifo(void) {
    toc_write_reg(STMPE811_FIFO_STA, 0x01);
    toc_write_reg(STMPE811_FIFO_STA, 0x00);
}

bool touch_read(int *x, int *y) {
    uint8_t ctrl = toc_read_reg(STMPE811_TSC_CTRL);
    if ((ctrl & 0x80) == 0) {
        // Touch detect bit is 0, no touch currently.
        touch_clear_fifo();
        return false;
    }

    uint8_t fifo_size = toc_read_reg(STMPE811_FIFO_SIZE);
    if (fifo_size == 0) {
        return false;
    }

    // Read X, Y
    // Can read 4 bytes starting from TSC_DATA_X, thanks to auto-increment
    uint8_t reg = STMPE811_TSC_DATA_X;
    uint8_t data[4];
    i2c_transfer7(I2C3, STMPE811_ADDR, &reg, 1, data, 4);

    uint16_t rx = (data[0] << 8) | data[1];
    uint16_t ry = (data[2] << 8) | data[3];

    // Clear FIFO if we have accumulated points, or let's read the latest
    // Let's clear FIFO to prevent stale touch data in continuous drawing mode
    touch_clear_fifo();

    // Map 12-bit ADC data (min approx 200, max approx 3800) to 320x240 LCD.
    // The exact boundaries depend on the resistor values of the screen.
    int min_x = 250, max_x = 3800;
    int min_y = 250, max_y = 3800;
    
    int tx = 319 - ((ry - min_y) * 320 / (max_y - min_y));
    int ty = (rx - min_x) * 240 / (max_x - min_x);

    if (tx < 0) tx = 0;
    if (tx > 319) tx = 319;
    if (ty < 0) ty = 0;
    if (ty > 239) ty = 239;

    // The touch panel axes are rotated relative to the LCD framebuffer.
    *x = tx; 
    *y = ty;

    return true;
}
