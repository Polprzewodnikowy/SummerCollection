#include <libdragon.h>

#include "boot.h"
#include "crc32.h"
#include "n64_regs.h"

cart_header_t global_cart_header __attribute__((aligned(8)));

cart_header_t *boot_load_cart_header(void) {
    cart_header_t *cart_header_pointer = &global_cart_header;

    data_cache_hit_writeback_invalidate(cart_header_pointer, sizeof(cart_header_t));
    dma_read(cart_header_pointer, CART_BASE, sizeof(cart_header_t));
    data_cache_hit_invalidate(cart_header_pointer, sizeof(cart_header_t));

    return cart_header_pointer;
}

cic_type_t boot_get_cic_type(cart_header_t *cart_header) {
    switch (crc32_calculate(cart_header->boot_code, sizeof(cart_header->boot_code))) {
        case BOOT_CRC32_6101:
        case BOOT_CRC32_7102:
            return E_CIC_TYPE_6101;
        case BOOT_CRC32_X102:
            return E_CIC_TYPE_6102;
        case BOOT_CRC32_X103:
            return E_CIC_TYPE_6103;
        case BOOT_CRC32_X105:
            return E_CIC_TYPE_6105;
        case BOOT_CRC32_X106:
            return E_CIC_TYPE_6106;
        default:
            return E_CIC_TYPE_UNKNOWN;
    }
}

tv_type_t boot_get_tv_type(cart_header_t *cart_header) {
    switch (cart_header->country_code) {
        case 'D':
        case 'F':
        case 'H':
        case 'I':
        case 'P':
        case 'S':
        case 'W':
        case 'X':
        case 'Y':
            return E_TV_TYPE_PAL;
        case '7':
        case 'A':
        case 'C':
        case 'E':
        case 'J':
        case 'K':
        case 'N':
        case 'U':
            return E_TV_TYPE_NTSC;
        case 'B':
            return E_TV_TYPE_MPAL;
        default:
            return E_TV_TYPE_UNKNOWN;
    }
}

void boot(cic_type_t cic_type, tv_type_t tv_type) {
    tv_type_t os_tv_type = tv_type == E_TV_TYPE_UNKNOWN ? OS_BOOT_CONFIG->tv_type : tv_type;

    volatile uint64_t gpr_regs[32];

    const uint8_t cic_seeds[] = {
        BOOT_SEED_X102,
        BOOT_SEED_X101,
        BOOT_SEED_X102,
        BOOT_SEED_X103,
        BOOT_SEED_X105,
        BOOT_SEED_X106,
    };

    while (!(SP->status & SP_STATUS_HALT));

    SP->status = SP_STATUS_CLEAR_INTERRUPT | SP_STATUS_SET_HALT;

    while (SP->dma_busy & SP_DMA_BUSY);

    PI->status = PI_STATUS_CLEAR_INTERRUPT | PI_STATUS_RESET_CONTROLLER;
    VI->v_intr = 0x3FF;
    VI->h_limits = 0;
    VI->current_line = 0;
    AI->dram_addr = 0;
    AI->len = 0;

    while (SP->status & SP_STATUS_DMA_BUSY);

    if (DP_CMD->status & DP_CMD_STATUS_XBUS_DMEM_DMA) {
        while (DP_CMD->status & DP_CMD_STATUS_PIPE_BUSY);
    }

    DP_CMD->status = DP_CMD_STATUS_CLEAR_FLUSH | DP_CMD_STATUS_CLEAR_FREEZE | DP_CMD_STATUS_CLEAR_XBUS_DMEM_DMA;

    for (size_t i = 0; i < ARRAY_ITEMS(SP_MEM->dmem); i++) {
        SP_MEM->dmem[i] = CART[i];
    }

    while (PI->status & (PI_STATUS_IO_BUSY | PI_STATUS_DMA_BUSY));

    PI->status = PI_STATUS_CLEAR_INTERRUPT | PI_STATUS_RESET_CONTROLLER;

    if (cic_type == E_CIC_TYPE_6105) {
        OS_BOOT_CONFIG->mem_size_6105 = OS_BOOT_CONFIG->mem_size;
    }

    for (size_t i = 0; i < ARRAY_ITEMS(SP_MEM->imem); i++) {
        SP_MEM->imem[i] = 0;
    }

    if (cic_type == E_CIC_TYPE_6105) {
        SP_MEM->imem[0] = 0x3C0DBFC0;
        SP_MEM->imem[1] = os_tv_type == E_TV_TYPE_PAL ? 0xBDA807FC : 0x8DA807FC;
        SP_MEM->imem[2] = 0x25AD07C0;
        SP_MEM->imem[3] = 0x31080080;
        SP_MEM->imem[4] = 0x5500FFFC;
        SP_MEM->imem[5] = 0x3C0DBFC0;
        SP_MEM->imem[6] = 0x8DA80024;
        SP_MEM->imem[7] = 0x3C0BB000;
    }

    for (size_t i = 0; i < ARRAY_ITEMS(gpr_regs); i++) {
        gpr_regs[i] = 0;
    }

    gpr_regs[CPU_REG_T3] = (0xFFFFFFFFLL << 32) | ((uint32_t) &SP_MEM->dmem[16]);
    gpr_regs[CPU_REG_S4] = os_tv_type;
    gpr_regs[CPU_REG_S6] = cic_seeds[cic_type];
    if (os_tv_type == E_TV_TYPE_PAL) {
        gpr_regs[CPU_REG_S7] = 6;
    }
    gpr_regs[CPU_REG_SP] = (0xFFFFFFFFLL << 32) | ((uint32_t) &SP_MEM->imem[ARRAY_ITEMS(SP_MEM->imem) - 4]);
    gpr_regs[CPU_REG_RA] = (0xFFFFFFFFLL << 32) | ((uint32_t) &SP_MEM->imem[(os_tv_type == E_TV_TYPE_PAL) ? 341 : 340]);

    __asm__ (
        ".set noat \n\t"
        ".set noreorder \n\t"
        "li $t0, 0x34000000 \n\t"
        "mtc0 $t0, $12 \n\t"
        "nop \n\t"
        "li $t0, 0x0006E463 \n\t"
        "mtc0 $t0, $16 \n\t"
        "nop \n\t"
        "li $t0, 0x00005000 \n\t"
        "mtc0 $t0, $9 \n\t"
        "nop \n\t"
        "li $t0, 0x0000005C \n\t"
        "mtc0 $t0, $13 \n\t"
        "nop \n\t"
        "li $t0, 0x007FFFF0 \n\t"
        "mtc0 $t0, $4 \n\t"
        "nop \n\t"
        "li $t0, 0xFFFFFFFF \n\t"
        "mtc0 $t0, $14 \n\t"
        "nop \n\t"
        "mtc0 $t0, $30 \n\t"
        "nop \n\t"
        "lui $t0, 0x0000 \n\t"
        "mthi $t0 \n\t"
        "nop \n\t"
        "mtlo $t0 \n\t"
        "nop \n\t"
        "ctc1 $t0, $31 \n\t"
        "nop \n\t"
        "add $ra, $zero, %[gpr_regs] \n\t"
        "ld $at, 0x08($ra) \n\t"
        "ld $v0, 0x10($ra) \n\t"
        "ld $v1, 0x18($ra) \n\t"
        "ld $a0, 0x20($ra) \n\t"
        "ld $a1, 0x28($ra) \n\t"
        "ld $a2, 0x30($ra) \n\t"
        "ld $a3, 0x38($ra) \n\t"
        "ld $t0, 0x40($ra) \n\t"
        "ld $t1, 0x48($ra) \n\t"
        "ld $t2, 0x50($ra) \n\t"
        "ld $t3, 0x58($ra) \n\t"
        "ld $t4, 0x60($ra) \n\t"
        "ld $t5, 0x68($ra) \n\t"
        "ld $t6, 0x70($ra) \n\t"
        "ld $t7, 0x78($ra) \n\t"
        "ld $s0, 0x80($ra) \n\t"
        "ld $s1, 0x88($ra) \n\t"
        "ld $s2, 0x90($ra) \n\t"
        "ld $s3, 0x98($ra) \n\t"
        "ld $s4, 0xA0($ra) \n\t"
        "ld $s5, 0xA8($ra) \n\t"
        "ld $s6, 0xB0($ra) \n\t"
        "ld $s7, 0xB8($ra) \n\t"
        "ld $t8, 0xC0($ra) \n\t"
        "ld $t9, 0xC8($ra) \n\t"
        "ld $k0, 0xD0($ra) \n\t"
        "ld $k1, 0xD8($ra) \n\t"
        "ld $gp, 0xE0($ra) \n\t"
        "ld $sp, 0xE8($ra) \n\t"
        "ld $fp, 0xF0($ra) \n\t"
        "ld $ra, 0xF8($ra) \n\t"
        "jr $t3 \n\t"
        "nop"
        :
        : [gpr_regs] "r" (gpr_regs)
        : "t0"
    );

    while (1);
}