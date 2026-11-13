/* MPU-6050 driver: the I2C and sensor code from firmware/capture/src/main.c,
 * changed for the 42 MHz APB1 clock this firmware runs at (capture runs the
 * whole chip at 16 MHz). */
#include "imu.h"

#include "board.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

#define RCC_AHB1ENR   REG(0x40023830u)
#define RCC_APB1RSTR  REG(0x40023820u)
#define RCC_APB1ENR   REG(0x40023840u)

#define GPIOB_BASE    0x40020400u
#define GPIO_MODER(p)   REG((p) + 0x00)
#define GPIO_OTYPER(p)  REG((p) + 0x04)
#define GPIO_OSPEEDR(p) REG((p) + 0x08)
#define GPIO_PUPDR(p)   REG((p) + 0x0C)
#define GPIO_BSRR(p)    REG((p) + 0x18)
#define GPIO_AFRL(p)    REG((p) + 0x20)
#define GPIO_AFRH(p)    REG((p) + 0x24)

#define I2C1_CR1      REG(0x40005400u)
#define I2C1_CR2      REG(0x40005404u)
#define I2C1_DR       REG(0x40005410u)
#define I2C1_SR1      REG(0x40005414u)
#define I2C1_SR2      REG(0x40005418u)
#define I2C1_CCR      REG(0x4000541Cu)
#define I2C1_TRISE    REG(0x40005420u)

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

#define APB1_MHZ      42u

#define MPU_ADDR         0x68
#define MPU_SMPLRT_DIV   0x19
#define MPU_CONFIG       0x1A
#define MPU_GYRO_CONFIG  0x1B
#define MPU_ACCEL_CONFIG 0x1C
#define MPU_INT_ENABLE   0x38
#define MPU_INT_STATUS   0x3A
#define MPU_ACCEL_XOUT_H 0x3B
#define MPU_PWR_MGMT_1   0x6B
#define MPU_WHO_AM_I     0x75

/* A reset in the middle of a read can leave the MPU-6050 holding SDA low.
 * Clocking SCL 9 times by hand lets it finish and let go. */
static void i2c_unjam_bus(void)
{
    GPIO_OTYPER(GPIOB_BASE) |= (1u << 6) | (1u << 9);
    GPIO_BSRR(GPIOB_BASE) = (1u << 6) | (1u << 9);
    GPIO_MODER(GPIOB_BASE) &= ~((3u << 12) | (3u << 18));
    GPIO_MODER(GPIOB_BASE) |= (1u << 12) | (1u << 18);
    for (int i = 0; i < 9; i++) {
        GPIO_BSRR(GPIOB_BASE) = (1u << (6 + 16)); board_delay_ms(1);
        GPIO_BSRR(GPIOB_BASE) = (1u << 6);        board_delay_ms(1);
    }
}

static void i2c_init(void)
{
    RCC_AHB1ENR |= (1u << 1);                       /* GPIOB clock */
    RCC_APB1ENR |= (1u << 21);                      /* I2C1 clock */
    RCC_APB1RSTR |= (1u << 21);
    RCC_APB1RSTR &= ~(1u << 21);

    i2c_unjam_bus();

    /* PB6 = SCL, PB9 = SDA: AF4, open-drain, pull-ups */
    GPIO_MODER(GPIOB_BASE)   &= ~((3u << 12) | (3u << 18));
    GPIO_MODER(GPIOB_BASE)   |=  (2u << 12) | (2u << 18);
    GPIO_OTYPER(GPIOB_BASE)  |=  (1u << 6) | (1u << 9);
    GPIO_OSPEEDR(GPIOB_BASE) |=  (2u << 12) | (2u << 18);
    GPIO_PUPDR(GPIOB_BASE)   &= ~((3u << 12) | (3u << 18));
    GPIO_PUPDR(GPIOB_BASE)   |=  (1u << 12) | (1u << 18);
    GPIO_AFRL(GPIOB_BASE)    &= ~(0xFu << 24);
    GPIO_AFRL(GPIOB_BASE)    |=  (4u << 24);
    GPIO_AFRH(GPIOB_BASE)    &= ~(0xFu << 4);
    GPIO_AFRH(GPIOB_BASE)    |=  (4u << 4);

    /* 100 kHz standard mode with a 42 MHz peripheral clock */
    I2C1_CR1 = I2C_CR1_SWRST;
    I2C1_CR1 = 0;
    I2C1_CR2 = APB1_MHZ;
    I2C1_CCR = APB1_MHZ * 5u;     /* 42 MHz / (2 * 100 kHz) = 210 */
    I2C1_TRISE = APB1_MHZ + 1u;   /* 1000 ns max rise time */
    I2C1_CR1 = I2C_CR1_PE;
}

static int i2c_wait(uint32_t flag)
{
    uint32_t start = board_ms();
    while (!(I2C1_SR1 & flag)) {
        if (I2C1_SR1 & I2C_SR1_AF) return -1;      /* no ACK */
        if (board_ms() - start > 5) return -1;     /* timed out */
    }
    return 0;
}

static int i2c_fail(void)
{
    I2C1_SR1 &= ~I2C_SR1_AF;
    I2C1_CR1 |= I2C_CR1_STOP;
    return -1;
}

static int i2c_start(uint8_t addr, int read)
{
    I2C1_CR1 |= I2C_CR1_START;
    if (i2c_wait(I2C_SR1_SB)) return -1;
    I2C1_DR = (uint32_t)((addr << 1) | (read ? 1 : 0));
    if (i2c_wait(I2C_SR1_ADDR)) return -1;
    return 0;
}

static int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    if (i2c_start(addr, 0)) return i2c_fail();
    (void)I2C1_SR2;
    I2C1_DR = reg;
    if (i2c_wait(I2C_SR1_TXE)) return i2c_fail();
    I2C1_DR = value;
    if (i2c_wait(I2C_SR1_BTF)) return i2c_fail();
    I2C1_CR1 |= I2C_CR1_STOP;
    return 0;
}

static int i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, int n)
{
    if (i2c_start(addr, 0)) return i2c_fail();
    (void)I2C1_SR2;
    I2C1_DR = reg;
    if (i2c_wait(I2C_SR1_BTF)) return i2c_fail();

    if (i2c_start(addr, 1)) return i2c_fail();
    if (n == 1) I2C1_CR1 &= ~I2C_CR1_ACK;
    else        I2C1_CR1 |=  I2C_CR1_ACK;
    (void)I2C1_SR2;

    for (int i = 0; i < n; i++) {
        if (i == n - 1) {
            I2C1_CR1 &= ~I2C_CR1_ACK;
            I2C1_CR1 |= I2C_CR1_STOP;
        }
        if (i2c_wait(I2C_SR1_RXNE)) return i2c_fail();
        buf[i] = (uint8_t)I2C1_DR;
    }
    return 0;
}

int imu_init(uint8_t *who)
{
    int err = 0;
    i2c_init();
    *who = 0xFF;
    err |= i2c_write_reg(MPU_ADDR, MPU_PWR_MGMT_1, 0x80);   /* reset */
    board_delay_ms(100);
    err |= i2c_write_reg(MPU_ADDR, MPU_PWR_MGMT_1, 0x01);   /* wake, gyro clock */
    board_delay_ms(10);
    err |= i2c_write_reg(MPU_ADDR, MPU_CONFIG, 0x03);       /* low-pass ~42 Hz */
    err |= i2c_write_reg(MPU_ADDR, MPU_SMPLRT_DIV, 9);      /* 100 Hz */
    err |= i2c_write_reg(MPU_ADDR, MPU_GYRO_CONFIG, 0x18);  /* +-2000 deg/s */
    err |= i2c_write_reg(MPU_ADDR, MPU_ACCEL_CONFIG, 0x18); /* +-16 g */
    err |= i2c_write_reg(MPU_ADDR, MPU_INT_ENABLE, 0x01);   /* data-ready flag */
    uint8_t w = 0;
    if (i2c_read_regs(MPU_ADDR, MPU_WHO_AM_I, &w, 1) == 0) *who = w;
    else err = -1;
    return err ? -1 : 0;
}

int imu_data_ready(void)
{
    uint8_t status = 0;
    if (i2c_read_regs(MPU_ADDR, MPU_INT_STATUS, &status, 1)) return 0;
    return status & 0x01;
}

int imu_read(int16_t out[6])
{
    uint8_t b[14];
    if (i2c_read_regs(MPU_ADDR, MPU_ACCEL_XOUT_H, b, 14)) return -1;
    out[0] = (int16_t)((b[0] << 8) | b[1]);
    out[1] = (int16_t)((b[2] << 8) | b[3]);
    out[2] = (int16_t)((b[4] << 8) | b[5]);
    out[3] = (int16_t)((b[8] << 8) | b[9]);    /* b[6..7] = temperature */
    out[4] = (int16_t)((b[10] << 8) | b[11]);
    out[5] = (int16_t)((b[12] << 8) | b[13]);
    return 0;
}
