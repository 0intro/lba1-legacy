#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The ports of a Sound Blaster Pro 2's or 16's OPL3 and mixer, as the
 * OPL3 driver's detection (yamaha.c) sees them. Its register writes
 * otherwise go to the chip directly (oplwrite). Every access takes
 * a microsecond of the ports' own time, on which the chip's timers
 * count: timer 1 every 80 us, timer 2 every 320, from their registers
 * to 0x100, raising their flags in the status unless masked, the
 * interrupt's flag with them, all cleared by register 4's bit 7. The
 * mixer is a file of registers that read back as written. Each
 * register written through the ports is the chip's too.
 */

static uint32_t base;
static uint64_t us;
static uint32_t addr;		/* the register addressed, its array in bit 8 */
static uint8_t t1, t2, ctl, flags;
static uint64_t t1over, t2over;	/* the timers' next overflows */
static uint8_t mixindex, mixregs[256];

/* oplioreset: the card at base, its timers stopped, its flags clear. */
void
oplioreset(uint32_t b)
{
	base = b;
	us = 0;
	addr = 0;
	t1 = t2 = ctl = flags = 0;
	t1over = t2over = 0;
	mixindex = 0;
	memset(mixregs, 0, sizeof mixregs);
}

static uint32_t
status(void)
{
	if ((ctl & 0x41) == 0x01 && us >= t1over)
		flags |= 0x40;
	if ((ctl & 0x22) == 0x02 && us >= t2over)
		flags |= 0x20;
	return flags != 0 ? 0x80 | flags : 0;
}

/* A register written through the ports: the timers' kept, every one given the chip. */
static void
reg(uint32_t r, uint32_t val)
{
	switch (r) {
	case 2:
		t1 = val;
		break;
	case 3:
		t2 = val;
		break;
	case 4:
		if (val & 0x80) {
			flags = 0;
			while (t1over <= us)
				t1over += (256 - t1) * 80;
			while (t2over <= us)
				t2over += (256 - t2) * 320;
			break;
		}
		if ((val & 1) != 0 && (ctl & 1) == 0)
			t1over = (us/80 + 1)*80 + (255 - t1)*80;
		if ((val & 2) != 0 && (ctl & 2) == 0)
			t2over = (us/320 + 1)*320 + (255 - t2)*320;
		ctl = val;
		break;
	}
	oplwrite(r, val);
}

/*
 * oplportout: a byte to a port of the card: the OPL3's address (base
 * and base+8 the first array's, base+2 the second's) and data, the
 * mixer's address and data at base+4 and +5.
 */
void
oplportout(uint32_t port, uint32_t val)
{
	us++;
	val &= 0xff;
	switch (port - base) {
	case 0:
	case 8:
		addr = val;
		break;
	case 2:
		addr = 0x100 | val;
		break;
	case 1:
	case 3:
	case 9:
		reg(addr, val);
		break;
	case 4:
		mixindex = val;
		break;
	case 5:
		mixregs[mixindex] = val;
		break;
	}
}

/* oplportin: a byte from a port of the card: the OPL3's status, a mixer register, else 0xff. */
uint32_t
oplportin(uint32_t port)
{
	us++;
	switch (port - base) {
	case 0:
	case 8:
		return status();
	case 5:
		return mixregs[mixindex];
	}
	return 0xff;
}
