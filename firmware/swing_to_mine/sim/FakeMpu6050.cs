// A minimal MPU-6050 for the Renode simulator (Renode has no model of it).
// Answers WHO_AM_I = 0x68, says a sample is always ready, and reports the
// board held upright and still: 1 g on -y, a small gyro bias.
// Loaded by renode_check.resc; Renode compiles this file itself.
using System;
using Antmicro.Renode.Peripherals.I2C;

namespace Antmicro.Renode.Peripherals.Sensors
{
    public class FakeMpu6050 : II2CPeripheral
    {
        public FakeMpu6050() { Reset(); }

        public void Reset()
        {
            reg = 0;
            regs = new byte[128];
            regs[0x75] = 0x68;   // WHO_AM_I
        }

        public short Ax = 0, Ay = -2048, Az = 0, Gx = 20, Gy = -12, Gz = 7;

        public void Write(byte[] data)
        {
            if (data.Length == 0) return;
            reg = data[0];
            for (int i = 1; i < data.Length; i++) regs[(reg + i - 1) & 0x7F] = data[i];
        }

        public byte[] Read(int count = 1)
        {
            // Renode's STM32F4 I2C asks for one byte and never comes back for
            // more, so a sample read gets all 14 bytes at once.
            if (reg == 0x3B && count < 14) count = 14;
            var b = new byte[count];
            for (int i = 0; i < count; i++) b[i] = Value((reg + i) & 0x7F);
            reg = (reg + count) & 0x7F;
            return b;
        }

        public void FinishTransmission() { }

        private byte Value(int r)
        {
            short[] v = { Ax, Ay, Az, 0, Gx, Gy, Gz };   // accel, temperature, gyro
            if (r == 0x3A) return 1;                     // INT_STATUS: data ready
            if (r >= 0x3B && r <= 0x48)
            {
                short s = v[(r - 0x3B) / 2];
                return (byte)(((r - 0x3B) % 2 == 0) ? (s >> 8) : (s & 0xFF));
            }
            return regs[r];
        }

        private int reg;
        private byte[] regs;
    }
}
