/*
 * PXA270-based Clamshell PDA platforms.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "vl.h"

#define spitz_printf(format, ...)	\
    fprintf(stderr, "%s: " format, __FUNCTION__, ##__VA_ARGS__)
#undef REG_FMT
#define REG_FMT			"0x%02lx"

/* Spitz Flash */
#define FLASH_BASE		0x0c000000
#define FLASH_ECCLPLB		0x00	/* Line parity 7 - 0 bit */
#define FLASH_ECCLPUB		0x04	/* Line parity 15 - 8 bit */
#define FLASH_ECCCP		0x08	/* Column parity 5 - 0 bit */
#define FLASH_ECCCNTR		0x0c	/* ECC byte counter */
#define FLASH_ECCCLRR		0x10	/* Clear ECC */
#define FLASH_FLASHIO		0x14	/* Flash I/O */
#define FLASH_FLASHCTL		0x18	/* Flash Control */

#define FLASHCTL_CE0		(1 << 0)
#define FLASHCTL_CLE		(1 << 1)
#define FLASHCTL_ALE		(1 << 2)
#define FLASHCTL_WP		(1 << 3)
#define FLASHCTL_CE1		(1 << 4)
#define FLASHCTL_RYBY		(1 << 5)
#define FLASHCTL_NCE		(FLASHCTL_CE0 | FLASHCTL_CE1)

struct sl_nand_s {
    target_phys_addr_t target_base;
    struct nand_flash_s *nand;
    uint8_t ctl;
    struct ecc_state_s ecc;
};

static uint32_t sl_readb(void *opaque, target_phys_addr_t addr)
{
    struct sl_nand_s *s = (struct sl_nand_s *) opaque;
    int ryby;
    addr -= s->target_base;

    switch (addr) {
#define BSHR(byte, from, to)	((s->ecc.lp[byte] >> (from - to)) & (1 << to))
    case FLASH_ECCLPLB:
        return BSHR(0, 4, 0) | BSHR(0, 5, 2) | BSHR(0, 6, 4) | BSHR(0, 7, 6) |
                BSHR(1, 4, 1) | BSHR(1, 5, 3) | BSHR(1, 6, 5) | BSHR(1, 7, 7);

#define BSHL(byte, from, to)	((s->ecc.lp[byte] << (to - from)) & (1 << to))
    case FLASH_ECCLPUB:
        return BSHL(0, 0, 0) | BSHL(0, 1, 2) | BSHL(0, 2, 4) | BSHL(0, 3, 6) |
                BSHL(1, 0, 1) | BSHL(1, 1, 3) | BSHL(1, 2, 5) | BSHL(1, 3, 7);

    case FLASH_ECCCP:
        return s->ecc.cp;

    case FLASH_ECCCNTR:
        return s->ecc.count & 0xff;

    case FLASH_FLASHCTL:
        nand_getpins(s->nand, &ryby);
        if (ryby)
            return s->ctl | FLASHCTL_RYBY;
        else
            return s->ctl;

    case FLASH_FLASHIO:
        return ecc_digest(&s->ecc, nand_getio(s->nand));

    default:
        spitz_printf("Bad register offset " REG_FMT "\n", addr);
    }
    return 0;
}

static void sl_writeb(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct sl_nand_s *s = (struct sl_nand_s *) opaque;
    addr -= s->target_base;

    switch (addr) {
    case FLASH_ECCCLRR:
        /* Value is ignored.  */
        ecc_reset(&s->ecc);
        break;

    case FLASH_FLASHCTL:
        s->ctl = value & 0xff & ~FLASHCTL_RYBY;
        nand_setpins(s->nand,
                        s->ctl & FLASHCTL_CLE,
                        s->ctl & FLASHCTL_ALE,
                        s->ctl & FLASHCTL_NCE,
                        s->ctl & FLASHCTL_WP,
                        0);
        break;

    case FLASH_FLASHIO:
        nand_setio(s->nand, ecc_digest(&s->ecc, value & 0xff));
        break;

    default:
        spitz_printf("Bad register offset " REG_FMT "\n", addr);
    }
}

enum {
    FLASH_128M,
    FLASH_1024M,
};

static void sl_flash_register(struct pxa2xx_state_s *cpu, int size)
{
    int iomemtype;
    struct sl_nand_s *s;
    CPUReadMemoryFunc *sl_readfn[] = {
        sl_readb,
        sl_readb,
        sl_readb,
    };
    CPUWriteMemoryFunc *sl_writefn[] = {
        sl_writeb,
        sl_writeb,
        sl_writeb,
    };

    s = (struct sl_nand_s *) qemu_mallocz(sizeof(struct sl_nand_s));
    s->target_base = FLASH_BASE;
    s->ctl = 0;
    if (size == FLASH_128M)
        s->nand = nand_init(NAND_MFR_SAMSUNG, 0x73);
    else if (size == FLASH_1024M)
        s->nand = nand_init(NAND_MFR_SAMSUNG, 0xf1);

    iomemtype = cpu_register_io_memory(0, sl_readfn,
                    sl_writefn, s);
    cpu_register_physical_memory(s->target_base, 0x40, iomemtype);
}

/* Spitz Keyboard */

#define SPITZ_KEY_STROBE_NUM	11
#define SPITZ_KEY_SENSE_NUM	7

static const int spitz_gpio_key_sense[SPITZ_KEY_SENSE_NUM] = {
    12, 17, 91, 34, 36, 38, 39
};

static const int spitz_gpio_key_strobe[SPITZ_KEY_STROBE_NUM] = {
    88, 23, 24, 25, 26, 27, 52, 103, 107, 108, 114
};

/* Eighth additional row maps the special keys */
static int spitz_keymap[SPITZ_KEY_SENSE_NUM + 1][SPITZ_KEY_STROBE_NUM] = {
    { 0x1d, 0x02, 0x04, 0x06, 0x07, 0x08, 0x0a, 0x0b, 0x0e, 0x3f, 0x40 },
    {  -1 , 0x03, 0x05, 0x13, 0x15, 0x09, 0x17, 0x18, 0x19, 0x41, 0x42 },
    { 0x0f, 0x10, 0x12, 0x14, 0x22, 0x16, 0x24, 0x25,  -1 ,  -1 ,  -1  },
    { 0x3c, 0x11, 0x1f, 0x21, 0x2f, 0x23, 0x32, 0x26,  -1 , 0x36,  -1  },
    { 0x3b, 0x1e, 0x20, 0x2e, 0x30, 0x31, 0x34,  -1 , 0x1c, 0x2a,  -1  },
    { 0x44, 0x2c, 0x2d, 0x0c, 0x39, 0x33,  -1 , 0x48,  -1 ,  -1 , 0x3d },
    { 0x37, 0x38,  -1 , 0x45, 0x57, 0x58, 0x4b, 0x50, 0x4d,  -1 ,  -1  },
    { 0x52, 0x43, 0x01, 0x47, 0x49,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1  },
};

#define SPITZ_GPIO_AK_INT	13	/* Remote control */
#define SPITZ_GPIO_SYNC		16	/* Sync button */
#define SPITZ_GPIO_ON_KEY	95	/* Power button */
#define SPITZ_GPIO_SWA		97	/* Lid */
#define SPITZ_GPIO_SWB		96	/* Tablet mode */

/* The special buttons are mapped to unused keys */
static const int spitz_gpiomap[5] = {
    SPITZ_GPIO_AK_INT, SPITZ_GPIO_SYNC, SPITZ_GPIO_ON_KEY,
    SPITZ_GPIO_SWA, SPITZ_GPIO_SWB,
};
static int spitz_gpio_invert[5] = { 0, 0, 0, 0, 0, };

struct spitz_keyboard_s {
    struct pxa2xx_state_s *cpu;
    int keymap[0x80];
    uint16_t keyrow[SPITZ_KEY_SENSE_NUM];
    uint16_t strobe_state;
    uint16_t sense_state;

    uint16_t pre_map[0x100];
    uint16_t modifiers;
    uint16_t imodifiers;
    uint8_t fifo[16];
    int fifopos, fifolen;
    QEMUTimer *kbdtimer;
};

static void spitz_keyboard_sense_update(struct spitz_keyboard_s *s)
{
    int i;
    uint16_t strobe, sense = 0;
    for (i = 0; i < SPITZ_KEY_SENSE_NUM; i ++) {
        strobe = s->keyrow[i] & s->strobe_state;
        if (strobe) {
            sense |= 1 << i;
            if (!(s->sense_state & (1 << i)))
                pxa2xx_gpio_set(s->cpu->gpio, spitz_gpio_key_sense[i], 1);
        } else if (s->sense_state & (1 << i))
            pxa2xx_gpio_set(s->cpu->gpio, spitz_gpio_key_sense[i], 0);
    }

    s->sense_state = sense;
}

static void spitz_keyboard_strobe(int line, int level,
                struct spitz_keyboard_s *s)
{
    int i;
    for (i = 0; i < SPITZ_KEY_STROBE_NUM; i ++)
        if (spitz_gpio_key_strobe[i] == line) {
            if (level)
                s->strobe_state |= 1 << i;
            else
                s->strobe_state &= ~(1 << i);

            spitz_keyboard_sense_update(s);
            break;
        }
}

static void spitz_keyboard_keydown(struct spitz_keyboard_s *s, int keycode)
{
    int spitz_keycode = s->keymap[keycode & 0x7f];
    if (spitz_keycode == -1)
        return;

    /* Handle the additional keys */
    if ((spitz_keycode >> 4) == SPITZ_KEY_SENSE_NUM) {
        pxa2xx_gpio_set(s->cpu->gpio, spitz_gpiomap[spitz_keycode & 0xf],
                        (keycode < 0x80) ^
                        spitz_gpio_invert[spitz_keycode & 0xf]);
        return;
    }

    if (keycode & 0x80)
        s->keyrow[spitz_keycode >> 4] &= ~(1 << (spitz_keycode & 0xf));
    else
        s->keyrow[spitz_keycode >> 4] |= 1 << (spitz_keycode & 0xf);

    spitz_keyboard_sense_update(s);
}

#define SHIFT	(1 << 7)
#define CTRL	(1 << 8)
#define FN	(1 << 9)

#define QUEUE_KEY(c)	s->fifo[(s->fifopos + s->fifolen ++) & 0xf] = c

static void spitz_keyboard_handler(struct spitz_keyboard_s *s, int keycode)
{
    uint16_t code;
    int mapcode;
    switch (keycode) {
    case 0x2a:	/* Left Shift */
        s->modifiers |= 1;
        break;
    case 0xaa:
        s->modifiers &= ~1;
        break;
    case 0x36:	/* Right Shift */
        s->modifiers |= 2;
        break;
    case 0xb6:
        s->modifiers &= ~2;
        break;
    case 0x1d:	/* Control */
        s->modifiers |= 4;
        break;
    case 0x9d:
        s->modifiers &= ~4;
        break;
    case 0x38:	/* Alt */
        s->modifiers |= 8;
        break;
    case 0xb8:
        s->modifiers &= ~8;
        break;
    }

    code = s->pre_map[mapcode = ((s->modifiers & 3) ?
            (keycode | SHIFT) :
            (keycode & ~SHIFT))];

    if (code != mapcode) {
#if 0
        if ((code & SHIFT) && !(s->modifiers & 1))
            QUEUE_KEY(0x2a | (keycode & 0x80));
        if ((code & CTRL ) && !(s->modifiers & 4))
            QUEUE_KEY(0x1d | (keycode & 0x80));
        if ((code & FN   ) && !(s->modifiers & 8))
            QUEUE_KEY(0x38 | (keycode & 0x80));
        if ((code & FN   ) && (s->modifiers & 1))
            QUEUE_KEY(0x2a | (~keycode & 0x80));
        if ((code & FN   ) && (s->modifiers & 2))
            QUEUE_KEY(0x36 | (~keycode & 0x80));
#else
        if (keycode & 0x80) {
            if ((s->imodifiers & 1   ) && !(s->modifiers & 1))
                QUEUE_KEY(0x2a | 0x80);
            if ((s->imodifiers & 4   ) && !(s->modifiers & 4))
                QUEUE_KEY(0x1d | 0x80);
            if ((s->imodifiers & 8   ) && !(s->modifiers & 8))
                QUEUE_KEY(0x38 | 0x80);
            if ((s->imodifiers & 0x10) && (s->modifiers & 1))
                QUEUE_KEY(0x2a);
            if ((s->imodifiers & 0x20) && (s->modifiers & 2))
                QUEUE_KEY(0x36);
            s->imodifiers = 0;
        } else {
            if ((code & SHIFT) && !((s->modifiers | s->imodifiers) & 1)) {
                QUEUE_KEY(0x2a);
                s->imodifiers |= 1;
            }
            if ((code & CTRL ) && !((s->modifiers | s->imodifiers) & 4)) {
                QUEUE_KEY(0x1d);
                s->imodifiers |= 4;
            }
            if ((code & FN   ) && !((s->modifiers | s->imodifiers) & 8)) {
                QUEUE_KEY(0x38);
                s->imodifiers |= 8;
            }
            if ((code & FN   ) && (s->modifiers & 1) &&
                            !(s->imodifiers & 0x10)) {
                QUEUE_KEY(0x2a | 0x80);
                s->imodifiers |= 0x10;
            }
            if ((code & FN   ) && (s->modifiers & 2) &&
                            !(s->imodifiers & 0x20)) {
                QUEUE_KEY(0x36 | 0x80);
                s->imodifiers |= 0x20;
            }
        }
#endif
    }

    QUEUE_KEY((code & 0x7f) | (keycode & 0x80));
}

static void spitz_keyboard_tick(void *opaque)
{
    struct spitz_keyboard_s *s = (struct spitz_keyboard_s *) opaque;

    if (s->fifolen) {
        spitz_keyboard_keydown(s, s->fifo[s->fifopos ++]);
        s->fifolen --;
        if (s->fifopos >= 16)
            s->fifopos = 0;
    }

    qemu_mod_timer(s->kbdtimer, qemu_get_clock(vm_clock) + ticks_per_sec / 32);
}

static void spitz_keyboard_pre_map(struct spitz_keyboard_s *s)
{
    int i;
    for (i = 0; i < 0x100; i ++)
        s->pre_map[i] = i;
    s->pre_map[0x02 | SHIFT	] = 0x02 | SHIFT;	/* exclam */
    s->pre_map[0x28 | SHIFT	] = 0x03 | SHIFT;	/* quotedbl */
    s->pre_map[0x04 | SHIFT	] = 0x04 | SHIFT;	/* numbersign */
    s->pre_map[0x05 | SHIFT	] = 0x05 | SHIFT;	/* dollar */
    s->pre_map[0x06 | SHIFT	] = 0x06 | SHIFT;	/* percent */
    s->pre_map[0x08 | SHIFT	] = 0x07 | SHIFT;	/* ampersand */
    s->pre_map[0x28		] = 0x08 | SHIFT;	/* apostrophe */
    s->pre_map[0x0a | SHIFT	] = 0x09 | SHIFT;	/* parenleft */
    s->pre_map[0x0b | SHIFT	] = 0x0a | SHIFT;	/* parenright */
    s->pre_map[0x29 | SHIFT	] = 0x0b | SHIFT;	/* asciitilde */
    s->pre_map[0x03 | SHIFT	] = 0x0c | SHIFT;	/* at */
    s->pre_map[0xd3		] = 0x0e | FN;		/* Delete */
    s->pre_map[0x3a		] = 0x0f | FN;		/* Caps_Lock */
    s->pre_map[0x07 | SHIFT	] = 0x11 | FN;		/* asciicircum */
    s->pre_map[0x0d		] = 0x12 | FN;		/* equal */
    s->pre_map[0x0d | SHIFT	] = 0x13 | FN;		/* plus */
    s->pre_map[0x1a		] = 0x14 | FN;		/* bracketleft */
    s->pre_map[0x1b		] = 0x15 | FN;		/* bracketright */
    s->pre_map[0x27		] = 0x22 | FN;		/* semicolon */
    s->pre_map[0x27 | SHIFT	] = 0x23 | FN;		/* colon */
    s->pre_map[0x09 | SHIFT	] = 0x24 | FN;		/* asterisk */
    s->pre_map[0x2b		] = 0x25 | FN;		/* backslash */
    s->pre_map[0x2b | SHIFT	] = 0x26 | FN;		/* bar */
    s->pre_map[0x0c | SHIFT	] = 0x30 | FN;		/* underscore */
    s->pre_map[0x35		] = 0x33 | SHIFT;	/* slash */
    s->pre_map[0x35 | SHIFT	] = 0x34 | SHIFT;	/* question */
    s->pre_map[0x49		] = 0x48 | FN;		/* Page_Up */
    s->pre_map[0x51		] = 0x50 | FN;		/* Page_Down */

    s->modifiers = 0;
    s->imodifiers = 0;
    s->fifopos = 0;
    s->fifolen = 0;
    s->kbdtimer = qemu_new_timer(vm_clock, spitz_keyboard_tick, s);
    spitz_keyboard_tick(s);
}

#undef SHIFT
#undef CTRL
#undef FN

static void spitz_keyboard_register(struct pxa2xx_state_s *cpu)
{
    int i, j;
    struct spitz_keyboard_s *s;

    s = (struct spitz_keyboard_s *)
            qemu_mallocz(sizeof(struct spitz_keyboard_s));
    memset(s, 0, sizeof(struct spitz_keyboard_s));
    s->cpu = cpu;

    for (i = 0; i < 0x80; i ++)
        s->keymap[i] = -1;
    for (i = 0; i < SPITZ_KEY_SENSE_NUM + 1; i ++)
        for (j = 0; j < SPITZ_KEY_STROBE_NUM; j ++)
            if (spitz_keymap[i][j] != -1)
                s->keymap[spitz_keymap[i][j]] = (i << 4) | j;

    for (i = 0; i < SPITZ_KEY_STROBE_NUM; i ++)
        pxa2xx_gpio_handler_set(cpu->gpio, spitz_gpio_key_strobe[i],
                        (gpio_handler_t) spitz_keyboard_strobe, s);

    spitz_keyboard_pre_map(s);
    qemu_add_kbd_event_handler((QEMUPutKBDEvent *) spitz_keyboard_handler, s);
}

/* SCOOP devices */

struct scoop_info_s {
    target_phys_addr_t target_base;
    uint16_t status;
    uint16_t power;
    uint32_t gpio_level;
    uint32_t gpio_dir;
    uint32_t prev_level;
    struct {
        gpio_handler_t fn;
        void *opaque;
    } handler[16];

    uint16_t mcr;
    uint16_t cdr;
    uint16_t ccr;
    uint16_t irr;
    uint16_t imr;
    uint16_t isr;
    uint16_t gprr;
};

#define SCOOP_MCR	0x00
#define SCOOP_CDR	0x04
#define SCOOP_CSR	0x08
#define SCOOP_CPR	0x0c
#define SCOOP_CCR	0x10
#define SCOOP_IRR_IRM	0x14
#define SCOOP_IMR	0x18
#define SCOOP_ISR	0x1c
#define SCOOP_GPCR	0x20
#define SCOOP_GPWR	0x24
#define SCOOP_GPRR	0x28

static inline void scoop_gpio_handler_update(struct scoop_info_s *s) {
    uint32_t level, diff;
    int bit;
    level = s->gpio_level & s->gpio_dir;

    for (diff = s->prev_level ^ level; diff; diff ^= 1 << bit) {
        bit = ffs(diff) - 1;
        if (s->handler[bit].fn)
            s->handler[bit].fn(bit, (level >> bit) & 1,
                            s->handler[bit].opaque);
    }

    s->prev_level = level;
}

static uint32_t scoop_readb(void *opaque, target_phys_addr_t addr)
{
    struct scoop_info_s *s = (struct scoop_info_s *) opaque;
    addr -= s->target_base;

    switch (addr) {
    case SCOOP_MCR:
        return s->mcr;
    case SCOOP_CDR:
        return s->cdr;
    case SCOOP_CSR:
        return s->status;
    case SCOOP_CPR:
        return s->power;
    case SCOOP_CCR:
        return s->ccr;
    case SCOOP_IRR_IRM:
        return s->irr;
    case SCOOP_IMR:
        return s->imr;
    case SCOOP_ISR:
        return s->isr;
    case SCOOP_GPCR:
        return s->gpio_dir;
    case SCOOP_GPWR:
        return s->gpio_level;
    case SCOOP_GPRR:
        return s->gprr;
    default:
        spitz_printf("Bad register offset " REG_FMT "\n", addr);
    }

    return 0;
}

static void scoop_writeb(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct scoop_info_s *s = (struct scoop_info_s *) opaque;
    addr -= s->target_base;
    value &= 0xffff;

    switch (addr) {
    case SCOOP_MCR:
        s->mcr = value;
        break;
    case SCOOP_CDR:
        s->cdr = value;
        break;
    case SCOOP_CPR:
        s->power = value;
        if (value & 0x80)
            s->power |= 0x8040;
        break;
    case SCOOP_CCR:
        s->ccr = value;
        break;
    case SCOOP_IRR_IRM:
        s->irr = value;
        break;
    case SCOOP_IMR:
        s->imr = value;
        break;
    case SCOOP_ISR:
        s->isr = value;
        break;
    case SCOOP_GPCR:
        s->gpio_dir = value;
        scoop_gpio_handler_update(s);
        break;
    case SCOOP_GPWR:
        s->gpio_level = value & s->gpio_dir;
        scoop_gpio_handler_update(s);
        break;
    case SCOOP_GPRR:
        s->gprr = value;
        break;
    default:
        spitz_printf("Bad register offset " REG_FMT "\n", addr);
    }
}

CPUReadMemoryFunc *scoop_readfn[] = {
    scoop_readb,
    scoop_readb,
    scoop_readb,
};
CPUWriteMemoryFunc *scoop_writefn[] = {
    scoop_writeb,
    scoop_writeb,
    scoop_writeb,
};

static inline void scoop_gpio_set(struct scoop_info_s *s, int line, int level)
{
    if (line >= 16) {
        spitz_printf("No GPIO pin %i\n", line);
        return;
    }

    if (level)
        s->gpio_level |= (1 << line);
    else
        s->gpio_level &= ~(1 << line);
}

static inline void scoop_gpio_handler_set(struct scoop_info_s *s, int line,
                gpio_handler_t handler, void *opaque) {
    if (line >= 16) {
        spitz_printf("No GPIO pin %i\n", line);
        return;
    }

    s->handler[line].fn = handler;
    s->handler[line].opaque = opaque;
}

static struct scoop_info_s *spitz_scoop_init(struct pxa2xx_state_s *cpu,
                int count) {
    int iomemtype;
    struct scoop_info_s *s;

    s = (struct scoop_info_s *)
            qemu_mallocz(sizeof(struct scoop_info_s) * 2);
    memset(s, 0, sizeof(struct scoop_info_s) * count);
    s[0].target_base = 0x10800000;
    s[1].target_base = 0x08800040;

    /* Ready */
    s[0].status = 0x02;
    s[1].status = 0x02;

    iomemtype = cpu_register_io_memory(0, scoop_readfn,
                    scoop_writefn, &s[0]);
    cpu_register_physical_memory(s[0].target_base, 0xfff, iomemtype);

    if (count < 2)
        return s;

    iomemtype = cpu_register_io_memory(0, scoop_readfn,
                    scoop_writefn, &s[1]);
    cpu_register_physical_memory(s[1].target_base, 0xfff, iomemtype);

    return s;
}

/* LCD backlight controller */

#define LCDTG_RESCTL	0x00
#define LCDTG_PHACTRL	0x01
#define LCDTG_DUTYCTRL	0x02
#define LCDTG_POWERREG0	0x03
#define LCDTG_POWERREG1	0x04
#define LCDTG_GPOR3	0x05
#define LCDTG_PICTRL	0x06
#define LCDTG_POLCTRL	0x07

static int bl_intensity, bl_power;

static void spitz_bl_update(struct pxa2xx_state_s *s)
{
    if (bl_power && bl_intensity)
        spitz_printf("LCD Backlight now at %i/63\n", bl_intensity);
    else
        spitz_printf("LCD Backlight now off\n");
}

static void spitz_bl_bit5(int line, int level, void *opaque)
{
    int prev = bl_intensity;

    if (level)
        bl_intensity &= ~0x20;
    else
        bl_intensity |= 0x20;

    if (bl_power && prev != bl_intensity)
        spitz_bl_update((struct pxa2xx_state_s *) opaque);
}

static void spitz_bl_power(int line, int level, void *opaque)
{
    bl_power = !!level;
    spitz_bl_update((struct pxa2xx_state_s *) opaque);
}

static void spitz_lcdtg_dac_put(void *opaque, uint8_t cmd)
{
    int addr, value;
    addr = cmd >> 5;
    value = cmd & 0x1f;

    switch (addr) {
    case LCDTG_RESCTL:
        if (value)
            spitz_printf("LCD in QVGA mode\n");
        else
            spitz_printf("LCD in VGA mode\n");
        break;

    case LCDTG_DUTYCTRL:
        bl_intensity &= ~0x1f;
        bl_intensity |= value;
        if (bl_power)
            spitz_bl_update((struct pxa2xx_state_s *) opaque);
        break;

    case LCDTG_POWERREG0:
        /* Set common voltage to M62332FP */
        break;
    }
}

/* SSP devices */

#define CORGI_SSP_PORT		2

#define SPITZ_GPIO_LCDCON_CS	53
#define SPITZ_GPIO_ADS7846_CS	14
#define SPITZ_GPIO_MAX1111_CS	20
#define SPITZ_GPIO_TP_INT	11

static int lcd_en, ads_en, max_en;
static struct max111x_s *max1111;
static struct ads7846_state_s *ads7846;

/* "Demux" the signal based on current chipselect */
static uint32_t corgi_ssp_read(void *opaque)
{
    if (lcd_en)
        return 0;
    if (ads_en)
        return ads7846_read(ads7846);
    if (max_en)
        return max111x_read(max1111);
    return 0;
}

static void corgi_ssp_write(void *opaque, uint32_t value)
{
    if (lcd_en)
        spitz_lcdtg_dac_put(opaque, value);
    if (ads_en)
        ads7846_write(ads7846, value);
    if (max_en)
        max111x_write(max1111, value);
}

static void corgi_ssp_gpio_cs(int line, int level, struct pxa2xx_state_s *s)
{
    if (line == SPITZ_GPIO_LCDCON_CS)
        lcd_en = !level;
    else if (line == SPITZ_GPIO_ADS7846_CS)
        ads_en = !level;
    else if (line == SPITZ_GPIO_MAX1111_CS)
        max_en = !level;
}

#define MAX1111_BATT_VOLT	1
#define MAX1111_BATT_TEMP	2
#define MAX1111_ACIN_VOLT	3

#define SPITZ_BATTERY_TEMP	0xe0	/* About 2.9V */
#define SPITZ_BATTERY_VOLT	0xd0	/* About 4.0V */
#define SPITZ_CHARGEON_ACIN	0x80	/* About 5.0V */

static void spitz_adc_temp_on(int line, int level, void *opaque)
{
    if (!max1111)
        return;

    if (level)
        max111x_set_input(max1111, MAX1111_BATT_TEMP, SPITZ_BATTERY_TEMP);
    else
        max111x_set_input(max1111, MAX1111_BATT_TEMP, 0);
}

static void spitz_pendown_set(void *opaque, int line, int level)
{
    struct pxa2xx_state_s *cpu = (struct pxa2xx_state_s *) opaque;
    pxa2xx_gpio_set(cpu->gpio, SPITZ_GPIO_TP_INT, level);
}

static void spitz_ssp_attach(struct pxa2xx_state_s *cpu)
{
    lcd_en = ads_en = max_en = 0;

    ads7846 = ads7846_init(qemu_allocate_irqs(spitz_pendown_set, cpu, 1)[0]);

    max1111 = max1111_init(0);
    max111x_set_input(max1111, MAX1111_BATT_VOLT, SPITZ_BATTERY_VOLT);
    max111x_set_input(max1111, MAX1111_BATT_TEMP, 0);
    max111x_set_input(max1111, MAX1111_ACIN_VOLT, SPITZ_CHARGEON_ACIN);

    pxa2xx_ssp_attach(cpu->ssp[CORGI_SSP_PORT - 1], corgi_ssp_read,
                    corgi_ssp_write, cpu);

    pxa2xx_gpio_handler_set(cpu->gpio, SPITZ_GPIO_LCDCON_CS,
                    (gpio_handler_t) corgi_ssp_gpio_cs, cpu);
    pxa2xx_gpio_handler_set(cpu->gpio, SPITZ_GPIO_ADS7846_CS,
                    (gpio_handler_t) corgi_ssp_gpio_cs, cpu);
    pxa2xx_gpio_handler_set(cpu->gpio, SPITZ_GPIO_MAX1111_CS,
                    (gpio_handler_t) corgi_ssp_gpio_cs, cpu);

    bl_intensity = 0x20;
    bl_power = 0;
}

/* CF Microdrive */

static void spitz_microdrive_attach(struct pxa2xx_state_s *cpu)
{
    struct pcmcia_card_s *md;
    BlockDriverState *bs = bs_table[0];

    if (bs && bdrv_is_inserted(bs) && !bdrv_is_removable(bs)) {
        md = dscm1xxxx_init(bs);
        pxa2xx_pcmcia_attach(cpu->pcmcia[0], md);
    }
}

/* Other peripherals */

static void spitz_charge_switch(int line, int level, void *opaque)
{
    spitz_printf("Charging %s.\n", level ? "off" : "on");
}

static void spitz_discharge_switch(int line, int level, void *opaque)
{
    spitz_printf("Discharging %s.\n", level ? "on" : "off");
}

static void spitz_greenled_switch(int line, int level, void *opaque)
{
    spitz_printf("Green LED %s.\n", level ? "on" : "off");
}

static void spitz_orangeled_switch(int line, int level, void *opaque)
{
    spitz_printf("Orange LED %s.\n", level ? "on" : "off");
}

#define SPITZ_SCP_LED_GREEN		1
#define SPITZ_SCP_JK_B			2
#define SPITZ_SCP_CHRG_ON		3
#define SPITZ_SCP_MUTE_L		4
#define SPITZ_SCP_MUTE_R		5
#define SPITZ_SCP_CF_POWER		6
#define SPITZ_SCP_LED_ORANGE		7
#define SPITZ_SCP_JK_A			8
#define SPITZ_SCP_ADC_TEMP_ON		9
#define SPITZ_SCP2_IR_ON		1
#define SPITZ_SCP2_AKIN_PULLUP		2
#define SPITZ_SCP2_BACKLIGHT_CONT	7
#define SPITZ_SCP2_BACKLIGHT_ON		8
#define SPITZ_SCP2_MIC_BIAS		9

static void spitz_scoop_gpio_setup(struct pxa2xx_state_s *cpu,
                struct scoop_info_s *scp, int num)
{
    scoop_gpio_handler_set(&scp[0], SPITZ_SCP_CHRG_ON,
                    spitz_charge_switch, cpu);
    scoop_gpio_handler_set(&scp[0], SPITZ_SCP_JK_B,
                    spitz_discharge_switch, cpu);
    scoop_gpio_handler_set(&scp[0], SPITZ_SCP_LED_GREEN,
                    spitz_greenled_switch, cpu);
    scoop_gpio_handler_set(&scp[0], SPITZ_SCP_LED_ORANGE,
                    spitz_orangeled_switch, cpu);

    if (num >= 2) {
        scoop_gpio_handler_set(&scp[1], SPITZ_SCP2_BACKLIGHT_CONT,
                        spitz_bl_bit5, cpu);
        scoop_gpio_handler_set(&scp[1], SPITZ_SCP2_BACKLIGHT_ON,
                        spitz_bl_power, cpu);
    }

    scoop_gpio_handler_set(&scp[0], SPITZ_SCP_ADC_TEMP_ON,
                    spitz_adc_temp_on, cpu);
}

#define SPITZ_GPIO_HSYNC		22
#define SPITZ_GPIO_SD_DETECT		9
#define SPITZ_GPIO_SD_WP		81
#define SPITZ_GPIO_ON_RESET		89
#define SPITZ_GPIO_BAT_COVER		90
#define SPITZ_GPIO_CF1_IRQ		105
#define SPITZ_GPIO_CF1_CD		94
#define SPITZ_GPIO_CF2_IRQ		106
#define SPITZ_GPIO_CF2_CD		93

int spitz_hsync;

static void spitz_lcd_hsync_handler(void *opaque)
{
    struct pxa2xx_state_s *cpu = (struct pxa2xx_state_s *) opaque;
    pxa2xx_gpio_set(cpu->gpio, SPITZ_GPIO_HSYNC, spitz_hsync);
    spitz_hsync ^= 1;
}

static void spitz_mmc_coverswitch_change(void *opaque, int in)
{
    struct pxa2xx_state_s *cpu = (struct pxa2xx_state_s *) opaque;
    pxa2xx_gpio_set(cpu->gpio, SPITZ_GPIO_SD_DETECT, in);
}

static void spitz_mmc_writeprotect_change(void *opaque, int wp)
{
    struct pxa2xx_state_s *cpu = (struct pxa2xx_state_s *) opaque;
    pxa2xx_gpio_set(cpu->gpio, SPITZ_GPIO_SD_WP, wp);
}

static void spitz_pcmcia_cb(void *opaque, int line, int level)
{
    struct pxa2xx_state_s *cpu = (struct pxa2xx_state_s *) opaque;
    static const int gpio_map[] = {
        SPITZ_GPIO_CF1_IRQ, SPITZ_GPIO_CF1_CD,
        SPITZ_GPIO_CF2_IRQ, SPITZ_GPIO_CF2_CD,
    };
    pxa2xx_gpio_set(cpu->gpio, gpio_map[line], level);
}

static void spitz_gpio_setup(struct pxa2xx_state_s *cpu, int slots)
{
    qemu_irq *pcmcia_cb;
    /*
     * Bad hack: We toggle the LCD hsync GPIO on every GPIO status
     * read to satisfy broken guests that poll-wait for hsync.
     * Simulating a real hsync event would be less practical and
     * wouldn't guarantee that a guest ever exits the loop.
     */
    spitz_hsync = 0;
    pxa2xx_gpio_read_notifier(cpu->gpio, spitz_lcd_hsync_handler, cpu);
    pxa2xx_lcd_vsync_cb(cpu->lcd, spitz_lcd_hsync_handler, cpu);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc, cpu, spitz_mmc_writeprotect_change,
                    spitz_mmc_coverswitch_change);

    /* Battery lock always closed */
    pxa2xx_gpio_set(cpu->gpio, SPITZ_GPIO_BAT_COVER, 1);

    /* Handle reset */
    pxa2xx_gpio_handler_set(cpu->gpio, SPITZ_GPIO_ON_RESET, pxa2xx_reset, cpu);

    /* PCMCIA signals: card's IRQ and Card-Detect */
    pcmcia_cb = qemu_allocate_irqs(spitz_pcmcia_cb, cpu, slots * 2);
    if (slots >= 1)
        pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[0], pcmcia_cb[0], pcmcia_cb[1]);
    if (slots >= 2)
        pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[1], pcmcia_cb[2], pcmcia_cb[3]);

    /* Initialise the screen rotation related signals */
    spitz_gpio_invert[3] = 0;	/* Always open */
    if (graphic_rotate) {	/* Tablet mode */
        spitz_gpio_invert[4] = 0;
    } else {			/* Portrait mode */
        spitz_gpio_invert[4] = 1;
    }
    pxa2xx_gpio_set(cpu->gpio, SPITZ_GPIO_SWA, spitz_gpio_invert[3]);
    pxa2xx_gpio_set(cpu->gpio, SPITZ_GPIO_SWB, spitz_gpio_invert[4]);
}

/* Write the bootloader parameters memory area.  */

#define MAGIC_CHG(a, b, c, d)	((d << 24) | (c << 16) | (b << 8) | a)

struct __attribute__ ((__packed__)) sl_param_info {
    uint32_t comadj_keyword;
    int32_t comadj;

    uint32_t uuid_keyword;
    char uuid[16];

    uint32_t touch_keyword;
    int32_t touch_xp;
    int32_t touch_yp;
    int32_t touch_xd;
    int32_t touch_yd;

    uint32_t adadj_keyword;
    int32_t adadj;

    uint32_t phad_keyword;
    int32_t phadadj;
} spitz_bootparam = {
    .comadj_keyword	= MAGIC_CHG('C', 'M', 'A', 'D'),
    .comadj		= 125,
    .uuid_keyword	= MAGIC_CHG('U', 'U', 'I', 'D'),
    .uuid		= { -1 },
    .touch_keyword	= MAGIC_CHG('T', 'U', 'C', 'H'),
    .touch_xp		= -1,
    .adadj_keyword	= MAGIC_CHG('B', 'V', 'A', 'D'),
    .adadj		= -1,
    .phad_keyword	= MAGIC_CHG('P', 'H', 'A', 'D'),
    .phadadj		= 0x01,
};

static void sl_bootparam_write(uint32_t ptr)
{
    memcpy(phys_ram_base + ptr, &spitz_bootparam,
                    sizeof(struct sl_param_info));
}

#define SL_PXA_PARAM_BASE	0xa0000a00

/* Board init.  */
enum spitz_model_e { spitz, akita, borzoi, terrier };

static void spitz_common_init(int ram_size, int vga_ram_size,
                DisplayState *ds, const char *kernel_filename,
                const char *kernel_cmdline, const char *initrd_filename,
                const char *cpu_model, enum spitz_model_e model, int arm_id)
{
    uint32_t spitz_ram = 0x04000000;
    uint32_t spitz_rom = 0x00800000;
    struct pxa2xx_state_s *cpu;
    struct scoop_info_s *scp;

    if (!cpu_model)
        cpu_model = (model == terrier) ? "pxa270-c5" : "pxa270-c0";
    cpu = pxa270_init(ds, cpu_model);

    /* Setup memory */
    if (ram_size < spitz_ram + spitz_rom) {
        fprintf(stderr, "This platform requires %i bytes of memory\n",
                        spitz_ram + spitz_rom);
        exit(1);
    }
    cpu_register_physical_memory(PXA2XX_RAM_BASE, spitz_ram, IO_MEM_RAM);

    sl_flash_register(cpu, (model == spitz) ? FLASH_128M : FLASH_1024M);

    cpu_register_physical_memory(0, spitz_rom, spitz_ram | IO_MEM_ROM);

    /* Setup peripherals */
    spitz_keyboard_register(cpu);

    spitz_ssp_attach(cpu);

    scp = spitz_scoop_init(cpu, (model == akita) ? 1 : 2);

    spitz_scoop_gpio_setup(cpu, scp, (model == akita) ? 1 : 2);

    spitz_gpio_setup(cpu, (model == akita) ? 1 : 2);

    if (model == terrier)
        /* A 6.0 GB microdrive is permanently sitting in CF slot 0.  */
        spitz_microdrive_attach(cpu);
    else if (model != akita)
        /* A 4.0 GB microdrive is permanently sitting in CF slot 0.  */
        spitz_microdrive_attach(cpu);

    /* Setup initial (reset) machine state */
    cpu->env->regs[15] = PXA2XX_RAM_BASE;

    arm_load_kernel(cpu->env, ram_size, kernel_filename, kernel_cmdline,
                    initrd_filename, arm_id, PXA2XX_RAM_BASE);
    sl_bootparam_write(SL_PXA_PARAM_BASE - PXA2XX_RAM_BASE);
}

static void spitz_init(int ram_size, int vga_ram_size, int boot_device,
                DisplayState *ds, const char **fd_filename, int snapshot,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    spitz_common_init(ram_size, vga_ram_size, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, spitz, 0x2c9);
}

static void borzoi_init(int ram_size, int vga_ram_size, int boot_device,
                DisplayState *ds, const char **fd_filename, int snapshot,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    spitz_common_init(ram_size, vga_ram_size, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, borzoi, 0x33f);
}

static void akita_init(int ram_size, int vga_ram_size, int boot_device,
                DisplayState *ds, const char **fd_filename, int snapshot,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    spitz_common_init(ram_size, vga_ram_size, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, akita, 0x2e8);
}

static void terrier_init(int ram_size, int vga_ram_size, int boot_device,
                DisplayState *ds, const char **fd_filename, int snapshot,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    spitz_common_init(ram_size, vga_ram_size, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, terrier, 0x33f);
}

QEMUMachine akitapda_machine = {
    "akita",
    "Akita PDA (PXA270)",
    akita_init,
};

QEMUMachine spitzpda_machine = {
    "spitz",
    "Spitz PDA (PXA270)",
    spitz_init,
};

QEMUMachine borzoipda_machine = {
    "borzoi",
    "Borzoi PDA (PXA270)",
    borzoi_init,
};

QEMUMachine terrierpda_machine = {
    "terrier",
    "Terrier PDA (PXA270)",
    terrier_init,
};
