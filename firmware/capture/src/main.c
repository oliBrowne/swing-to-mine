/*
 * Swing to Mine: data-capture firmware (STM32F4 Discovery + MPU-6050).
 *
 * What it does:
 *   - Reads the MPU-6050's accelerometer and gyro (6 channels) over I2C1
 *     (PB6 = SCL, PB9 = SDA) at 100 Hz.
 *   - Sends one text line per sample out of USART2 (PA2 = TX, PA3 = RX) at
 *     115200 baud, in the format from docs/architecture.md:
 *
 *       # swing-to-mine v1 rate_hz=100 accel_fs_g=16 gyro_fs_dps=2000
 *       t_ms,ax,ay,az,gx,gy,gz
 *       12034,-112,2051,48,-3,12,7
 *
 *     Values are raw int16 counts: accel 2048 counts per g, gyro 16.4
 *     counts per deg/s. Lines that start with '#' are info or errors.
 *   - LEDs: green blinks while samples are flowing, red is on while the
 *     MPU-6050 is not answering.
 *
 * It talks to the chip's registers directly (no ST HAL), so the only thing
 * needed to build it is the Arm GCC compiler. The CPU runs on its 16 MHz
 * internal clock, which is plenty for this.
 *
 * Wiring: docs/wiring.md. Not yet tested on hardware.
 */
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Register addresses (from the STM32F407 reference manual, RM0090)          */
/* ------------------------------------------------------------------------ */
#define REG(addr) (*(volatile uint32_t *)(addr))

#define RCC_BASE      0x40023800u
#define RCC_AHB1ENR   REG(RCC_BASE + 0x30)
#define RCC_APB1RSTR  REG(RCC_BASE + 0x20)
#define RCC_APB1ENR   REG(RCC_BASE + 0x40)

#define GPIOA_BASE    0x40020000u
#define GPIOB_BASE    0x40020400u
#define GPIOD_BASE    0x40020C00u
#define GPIO_MODER(p)   REG((p) + 0x00)
#define GPIO_OTYPER(p)  REG((p) + 0x04)
#define GPIO_OSPEEDR(p) REG((p) + 0x08)
#define GPIO_PUPDR(p)   REG((p) + 0x0C)
#define GPIO_IDR(p)     REG((p) + 0x10)
#define GPIO_BSRR(p)    REG((p) + 0x18)
#define GPIO_AFRL(p)    REG((p) + 0x20)
#define GPIO_AFRH(p)    REG((p) + 0x24)

#define USART2_BASE   0x40004400u
#define USART2_SR     REG(USART2_BASE + 0x00)
#define USART2_DR     REG(USART2_BASE + 0x04)
#define USART2_BRR    REG(USART2_BASE + 0x08)
#define USART2_CR1    REG(USART2_BASE + 0x0C)

#define I2C1_BASE     0x40005400u
#define I2C1_CR1      REG(I2C1_BASE + 0x00)
#define I2C1_CR2      REG(I2C1_BASE + 0x04)
#define I2C1_DR       REG(I2C1_BASE + 0x10)
#define I2C1_SR1      REG(I2C1_BASE + 0x14)
#define I2C1_SR2      REG(I2C1_BASE + 0x18)
#define I2C1_CCR      REG(I2C1_BASE + 0x1C)
#define I2C1_TRISE    REG(I2C1_BASE + 0x20)

#define SYST_CSR      REG(0xE000E010u)
#define SYST_RVR      REG(0xE000E014u)
#define SYST_CVR      REG(0xE000E018u)

/* I2C status/control bits */
#define I2C_CR1_PE    (1u << 0)
#define I2C_CR1_START (1u << 8)
#define I2C_CR1_STOP  (1u << 9)
#define I2C_CR1_ACK   (1u << 10)
#define I2C_CR1_SWRST (1u << 15)
#define I2C_SR1_SB    (1u << 0)
#define I2C_SR1_ADDR  (1u << 1)
#define I2C_SR1_BTF   (1u << 2)
#define I2C_SR1_RXNE  (1u << 6)
#define I2C_SR1_TXE   (1u << 7)
#define I2C_SR1_AF    (1u << 10)

/* ------------------------------------------------------------------------ */
/* Settings                                                                 */
/* ------------------------------------------------------------------------ */
#define CPU_HZ        16000000u   /* internal HSI oscillator, the reset default */
#define BAUD          115200u

#define MPU_ADDR      0x68        /* AD0 low or unconnected */
#define MPU_SMPLRT_DIV   0x19
#define MPU_CONFIG       0x1A
#define MPU_GYRO_CONFIG  0x1B
#define MPU_ACCEL_CONFIG 0x1C
#define MPU_INT_ENABLE   0x38
#define MPU_INT_STATUS   0x3A
#define MPU_ACCEL_XOUT_H 0x3B     /* start of the 14 data bytes */
#define MPU_PWR_MGMT_1   0x6B
#define MPU_WHO_AM_I     0x75

/* LEDs on port D */
#define LED_GREEN  12
#define LED_ORANGE 13
#define LED_RED    14
#define LED_BLUE   15

/* ------------------------------------------------------------------------ */
/* 1 ms clock                                                               */
/* ------------------------------------------------------------------------ */
static volatile uint32_t ms_ticks;

void SysTick_Handler(void) { ms_ticks++; }

static void systick_init(void)
{
    SYST_RVR = CPU_HZ / 1000 - 1;  /* interrupt every 1 ms */
    SYST_CVR = 0;
    SYST_CSR = 7;                  /* use CPU clock, enable interrupt, start */
}

static uint32_t millis(void) { return ms_ticks; }

static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while (millis() - start < ms) { }
}

/* ------------------------------------------------------------------------ */
/* Pins and LEDs                                                            */
/* ------------------------------------------------------------------------ */
static void led_set(int pin, int on)
{
    GPIO_BSRR(GPIOD_BASE) = on ? (1u << pin) : (1u << (pin + 16));
}

static void gpio_init(void)
{
    RCC_AHB1ENR |= (1u << 0) | (1u << 1) | (1u << 3);  /* clocks for GPIOA, B, D */

    /* PD12-15 (LEDs) as outputs */
    for (int pin = 12; pin <= 15; pin++) {
        GPIO_MODER(GPIOD_BASE) &= ~(3u << (pin * 2));
        GPIO_MODER(GPIOD_BASE) |=  (1u << (pin * 2));
    }

    /* PA2 = USART2 TX, PA3 = USART2 RX: alternate function 7 */
    GPIO_MODER(GPIOA_BASE) &= ~((3u << 4) | (3u << 6));
    GPIO_MODER(GPIOA_BASE) |=  (2u << 4) | (2u << 6);
    GPIO_AFRL(GPIOA_BASE)  &= ~((0xFu << 8) | (0xFu << 12));
    GPIO_AFRL(GPIOA_BASE)  |=  (7u << 8) | (7u << 12);
    GPIO_PUPDR(GPIOA_BASE) |=  (1u << 6);               /* pull-up on RX */
}

/* ------------------------------------------------------------------------ */
/* UART (text out to the PC)                                                */
/* ------------------------------------------------------------------------ */
static void uart_init(void)
{
    RCC_APB1ENR |= (1u << 17);                     /* USART2 clock on */
    USART2_BRR = (CPU_HZ + BAUD / 2) / BAUD;       /* 16 MHz / 115200 = 139 */
    USART2_CR1 = (1u << 13) | (1u << 3);           /* enable USART, enable TX */
}

static void uart_putc(char c)
{
    while (!(USART2_SR & (1u << 7))) { }           /* wait until TX buffer empty */
    USART2_DR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* Print a whole number (can be negative) without using printf. */
static void uart_put_int(int32_t v)
{
    char buf[12];
    int i = 0;
    uint32_t u = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    if (v < 0) uart_putc('-');
    do { buf[i++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (i) uart_putc(buf[--i]);
}

static void uart_put_hex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    uart_putc(hex[v >> 4]);
    uart_putc(hex[v & 0xF]);
}

/* ------------------------------------------------------------------------ */
/* I2C1 (talks to the MPU-6050)                                             */
/* ------------------------------------------------------------------------ */

/* If the board was reset in the middle of a read, the MPU-6050 can be left
 * holding SDA low, which jams the bus. Clocking SCL 9 times by hand lets it
 * finish and let go. */
static void i2c_unjam_bus(void)
{
    /* PB6 (SCL) and PB9 (SDA) as open-drain outputs, released (high) */
    GPIO_OTYPER(GPIOB_BASE) |= (1u << 6) | (1u << 9);
    GPIO_BSRR(GPIOB_BASE) = (1u << 6) | (1u << 9);
    GPIO_MODER(GPIOB_BASE) &= ~((3u << 12) | (3u << 18));
    GPIO_MODER(GPIOB_BASE) |=  (1u << 12) | (1u << 18);
    for (int i = 0; i < 9; i++) {
        GPIO_BSRR(GPIOB_BASE) = (1u << (6 + 16)); delay_ms(1);   /* SCL low  */
        GPIO_BSRR(GPIOB_BASE) = (1u << 6);        delay_ms(1);   /* SCL high */
    }
}

static void i2c_init(void)
{
    RCC_APB1ENR |= (1u << 21);                      /* I2C1 clock on */
    RCC_APB1RSTR |= (1u << 21);                     /* reset the I2C block */
    RCC_APB1RSTR &= ~(1u << 21);

    i2c_unjam_bus();

    /* PB6 = SCL, PB9 = SDA: alternate function 4, open-drain, pull-ups.
     * (The GY-521 board also has its own pull-up resistors.) */
    GPIO_MODER(GPIOB_BASE)   &= ~((3u << 12) | (3u << 18));
    GPIO_MODER(GPIOB_BASE)   |=  (2u << 12) | (2u << 18);
    GPIO_OTYPER(GPIOB_BASE)  |=  (1u << 6) | (1u << 9);
    GPIO_OSPEEDR(GPIOB_BASE) |=  (2u << 12) | (2u << 18);
    GPIO_PUPDR(GPIOB_BASE)   &= ~((3u << 12) | (3u << 18));
    GPIO_PUPDR(GPIOB_BASE)   |=  (1u << 12) | (1u << 18);
    GPIO_AFRL(GPIOB_BASE)    &= ~(0xFu << 24);
    GPIO_AFRL(GPIOB_BASE)    |=  (4u << 24);       /* PB6 -> AF4 */
    GPIO_AFRH(GPIOB_BASE)    &= ~(0xFu << 4);
    GPIO_AFRH(GPIOB_BASE)    |=  (4u << 4);        /* PB9 -> AF4 */

    /* 100 kHz standard mode with a 16 MHz peripheral clock */
    I2C1_CR1 = I2C_CR1_SWRST;
    I2C1_CR1 = 0;
    I2C1_CR2 = 16;          /* peripheral clock in MHz */
    I2C1_CCR = 80;          /* 16 MHz / (2 * 100 kHz) */
    I2C1_TRISE = 17;        /* 1000 ns max rise time * 16 MHz + 1 */
    I2C1_CR1 = I2C_CR1_PE;
}

/* Wait for a status bit, giving up after ~5 ms. Returns 0 on success. */
static int i2c_wait(uint32_t flag)
{
    uint32_t start = millis();
    while (!(I2C1_SR1 & flag)) {
        if (I2C1_SR1 & I2C_SR1_AF) return -1;      /* no ACK from the device */
        if (millis() - start > 5) return -1;       /* timed out */
    }
    return 0;
}

static int i2c_fail(void)
{
    I2C1_SR1 &= ~I2C_SR1_AF;
    I2C1_CR1 |= I2C_CR1_STOP;
    return -1;
}

/* Send START and the device address. read = 1 for a read. */
static int i2c_start(uint8_t addr, int read)
{
    I2C1_CR1 |= I2C_CR1_START;
    if (i2c_wait(I2C_SR1_SB)) return -1;
    I2C1_DR = (uint32_t)((addr << 1) | (read ? 1 : 0));
    if (i2c_wait(I2C_SR1_ADDR)) return -1;
    return 0;
}

/* Write one byte to a register on the device. */
static int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    if (i2c_start(addr, 0)) return i2c_fail();
    (void)I2C1_SR2;                                /* reading SR1 then SR2 clears ADDR */
    I2C1_DR = reg;
    if (i2c_wait(I2C_SR1_TXE)) return i2c_fail();
    I2C1_DR = value;
    if (i2c_wait(I2C_SR1_BTF)) return i2c_fail();
    I2C1_CR1 |= I2C_CR1_STOP;
    return 0;
}

/* Read n bytes starting at a register on the device. */
static int i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, int n)
{
    /* 1. tell the device which register we want */
    if (i2c_start(addr, 0)) return i2c_fail();
    (void)I2C1_SR2;
    I2C1_DR = reg;
    if (i2c_wait(I2C_SR1_BTF)) return i2c_fail();

    /* 2. restart in read mode */
    if (i2c_start(addr, 1)) return i2c_fail();
    if (n == 1) I2C1_CR1 &= ~I2C_CR1_ACK;          /* single byte: NACK it */
    else        I2C1_CR1 |=  I2C_CR1_ACK;          /* ACK every byte but the last */
    (void)I2C1_SR2;                                /* clear ADDR: bytes start arriving */

    for (int i = 0; i < n; i++) {
        if (i == n - 1) {                          /* before the last byte: NACK + STOP */
            I2C1_CR1 &= ~I2C_CR1_ACK;
            I2C1_CR1 |= I2C_CR1_STOP;
        }
        if (i2c_wait(I2C_SR1_RXNE)) return i2c_fail();
        buf[i] = (uint8_t)I2C1_DR;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* MPU-6050                                                                 */
/* ------------------------------------------------------------------------ */

/* Wake the chip and set it up. Returns 0 if every write was acknowledged. */
static int mpu_init(void)
{
    uint8_t who = 0;
    int err = 0;

    err |= i2c_write_reg(MPU_ADDR, MPU_PWR_MGMT_1, 0x80);  /* reset the chip */
    delay_ms(100);
    err |= i2c_write_reg(MPU_ADDR, MPU_PWR_MGMT_1, 0x01);  /* wake up, use gyro clock */
    delay_ms(10);
    err |= i2c_write_reg(MPU_ADDR, MPU_CONFIG, 0x03);      /* low-pass filter ~42 Hz */
    err |= i2c_write_reg(MPU_ADDR, MPU_SMPLRT_DIV, 9);     /* 1 kHz / (1 + 9) = 100 Hz */
    err |= i2c_write_reg(MPU_ADDR, MPU_GYRO_CONFIG, 0x18); /* gyro  +-2000 deg/s */
    err |= i2c_write_reg(MPU_ADDR, MPU_ACCEL_CONFIG, 0x18);/* accel +-16 g */
    err |= i2c_write_reg(MPU_ADDR, MPU_INT_ENABLE, 0x01);  /* raise "data ready" flag */

    /* Clone chips often answer something other than 0x68, so only print it. */
    if (i2c_read_regs(MPU_ADDR, MPU_WHO_AM_I, &who, 1) == 0) {
        uart_puts("# mpu6050 who_am_i=");
        uart_put_hex8(who);
        uart_puts("\n");
    }
    return err ? -1 : 0;
}

/* Has the chip finished a new sample? Reading INT_STATUS also clears the flag. */
static int mpu_data_ready(void)
{
    uint8_t status = 0;
    if (i2c_read_regs(MPU_ADDR, MPU_INT_STATUS, &status, 1)) return 0;
    return status & 0x01;
}

/* Read accel x/y/z, temperature, gyro x/y/z in one go (14 bytes, high byte first). */
static int mpu_read(int16_t out[6])
{
    uint8_t b[14];
    if (i2c_read_regs(MPU_ADDR, MPU_ACCEL_XOUT_H, b, 14)) return -1;
    out[0] = (int16_t)((b[0]  << 8) | b[1]);   /* ax */
    out[1] = (int16_t)((b[2]  << 8) | b[3]);   /* ay */
    out[2] = (int16_t)((b[4]  << 8) | b[5]);   /* az */
    /* b[6], b[7] = temperature, not used */
    out[3] = (int16_t)((b[8]  << 8) | b[9]);   /* gx */
    out[4] = (int16_t)((b[10] << 8) | b[11]);  /* gy */
    out[5] = (int16_t)((b[12] << 8) | b[13]);  /* gz */
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Main loop                                                                */
/* ------------------------------------------------------------------------ */
static void print_header(void)
{
    uart_puts("# swing-to-mine v1 rate_hz=100 accel_fs_g=16 gyro_fs_dps=2000\n");
    uart_puts("t_ms,ax,ay,az,gx,gy,gz\n");
}

int main(void)
{
    systick_init();
    gpio_init();
    uart_init();
    led_set(LED_BLUE, 1);                 /* blue = starting up */
    delay_ms(100);                        /* give the MPU-6050 time to power up */
    i2c_init();

    uart_puts("# booting\n");
    int mpu_ok = (mpu_init() == 0);
    if (!mpu_ok) uart_puts("# error: mpu6050 not answering, check wiring (docs/wiring.md)\n");
    led_set(LED_BLUE, 0);
    print_header();

    uint32_t last_sample_ms = millis();
    uint32_t last_header_ms = millis();
    uint32_t last_retry_ms  = millis();
    uint32_t count = 0;
    int16_t s[6];

    while (1) {
        uint32_t now = millis();

        /* If the sensor isn't answering, keep trying once a second. */
        if (!mpu_ok) {
            led_set(LED_RED, 1);
            if (now - last_retry_ms >= 1000) {
                last_retry_ms = now;
                i2c_init();
                mpu_ok = (mpu_init() == 0);
                uart_puts(mpu_ok ? "# mpu6050 ok\n" : "# error: mpu6050 not answering\n");
            }
            continue;
        }

        /* Normally we wait for the chip's "data ready" flag, so we get exactly
         * one line per new sample. If the flag never shows up (some clone
         * chips), fall back to reading every 10 ms. */
        if (!mpu_data_ready() && (now - last_sample_ms) < 11) continue;

        if (mpu_read(s) != 0) {
            uart_puts("# error: i2c read failed\n");
            mpu_ok = 0;
            continue;
        }
        led_set(LED_RED, 0);
        last_sample_ms = now;

        /* t_ms,ax,ay,az,gx,gy,gz */
        uart_put_int((int32_t)now);
        for (int i = 0; i < 6; i++) { uart_putc(','); uart_put_int(s[i]); }
        uart_putc('\n');

        /* Blink green about once a second while data is flowing. */
        count++;
        led_set(LED_GREEN, (count / 50) % 2);

        /* Repeat the info line every 10 s so a PC that connects late still sees it. */
        if (now - last_header_ms >= 10000) {
            last_header_ms = now;
            uart_puts("# swing-to-mine v1 rate_hz=100 accel_fs_g=16 gyro_fs_dps=2000\n");
        }
    }
}
