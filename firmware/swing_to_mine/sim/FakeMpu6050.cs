// A minimal MPU-6050 for the Renode simulator (Renode has no model of it).
// Answers WHO_AM_I = 0x68, says a new sample is ready every 10 ms of
// simulated time (100 Hz, as the real sensor is set up), and reports the
// board held upright and still: 1 g on -y, a small gyro bias.
// Replay(path) instead plays back a recording, one row per sample read
// (lines of "ax,ay,az,gx,gy,gz" raw counts, from sim/make_replay.py), and
// keeps reporting the last row once it runs out.
// Loaded by the .resc files here; Renode compiles this file itself.
using System;
using System.Collections.Generic;
using System.IO;
using Antmicro.Renode.Core;
using Antmicro.Renode.Peripherals.I2C;

namespace Antmicro.Renode.Peripherals.Sensors
{
    public class FakeMpu6050 : II2CPeripheral
    {
        public FakeMpu6050(IMachine machine)
        {
            this.machine = machine;
            Reset();
        }

        public void Reset()
        {
            reg = 0;
            regs = new byte[128];
            regs[0x75] = 0x68;   // WHO_AM_I
        }

        public short Ax = 0, Ay = -2048, Az = 0, Gx = 20, Gy = -12, Gz = 7;
        public int Played { get { return next; } }

        public void Replay(string path)
        {
            rows = new List<short[]>();
            foreach (var line in File.ReadAllLines(path))
            {
                var f = line.Split(',');
                if (f.Length != 6) continue;
                var r = new short[6];
                for (int i = 0; i < 6; i++) r[i] = short.Parse(f[i]);
                rows.Add(r);
            }
            next = 0;
        }

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
            if (reg == 0x3B && rows != null && rows.Count > 0)
            {
                var r = rows[Math.Min(next, rows.Count - 1)];
                Ax = r[0]; Ay = r[1]; Az = r[2]; Gx = r[3]; Gy = r[4]; Gz = r[5];
                if (next < rows.Count) next++;
            }
            if (reg == 0x3B) lastSampleMs = NowMs();
            var b = new byte[count];
            for (int i = 0; i < count; i++) b[i] = Value((reg + i) & 0x7F);
            reg = (reg + count) & 0x7F;
            return b;
        }

        public void FinishTransmission() { }

        private byte Value(int r)
        {
            short[] v = { Ax, Ay, Az, 0, Gx, Gy, Gz };   // accel, temperature, gyro
            // INT_STATUS: like the real chip, a new sample every 10 ms on a fixed
            // grid, flagged until it is read
            if (r == 0x3A) return (byte)(Math.Floor(NowMs() / 10.0) > Math.Floor(lastSampleMs / 10.0) ? 1 : 0);
            if (r >= 0x3B && r <= 0x48)
            {
                short s = v[(r - 0x3B) / 2];
                return (byte)(((r - 0x3B) % 2 == 0) ? (s >> 8) : (s & 0xFF));
            }
            return regs[r];
        }

        private int reg;
        private byte[] regs;
        private List<short[]> rows;
        private int next;
        private double lastSampleMs = -10.0;
        private readonly IMachine machine;

        private double NowMs() { return machine.ElapsedVirtualTime.TimeElapsed.TotalMilliseconds; }
    }
}
