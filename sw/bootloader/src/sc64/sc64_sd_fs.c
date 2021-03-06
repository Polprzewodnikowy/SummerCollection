#include "ff.h"
#include "diskio.h"

#include "sc64.h"
#include "sc64_sd.h"
#include "sc64_sd_fs.h"


static DRESULT sc64_sd_fs_load_rom_with_dma(BYTE pdrv, FSIZE_t offset, LBA_t sector, UINT count) {
    if ((pdrv > 0) || (count == 0)) {
        return RES_PARERR;
    }

    if (!sc64_sd_get_status()) {
        return RES_NOTRDY;
    }

    if (sc64_sd_read_sectors_dma(sector, count, SC64_BANK_SDRAM, offset) != E_OK) {
        return RES_ERROR;
    }

    return RES_OK;
}

sc64_sd_fs_error_t sc64_sd_fs_load_rom(const char *path) {
    FRESULT fresult;
    FATFS fatfs;

    sc64_sd_fs_error_t error = SC64_SD_FS_OK;

    do {
        fresult = f_mount(&fatfs, "", 1);
        if (fresult != FR_OK) {
            switch (fresult) {
                case FR_DISK_ERR:
                case FR_NOT_READY:
                    error = SC64_SD_FS_NO_CARD;
                    break;
                case FR_NO_FILESYSTEM:
                    error = SC64_SD_FS_NO_FILESYSTEM;
                    break;
                default:
                    error = SC64_SD_FS_OTHER_ERROR;
                    break;
            }
            break;
        }

        fresult = fe_load(path, SC64_SDRAM_SIZE, sc64_sd_fs_load_rom_with_dma);
        if (fresult) {
            switch (fresult) {
                case FR_DISK_ERR:
                case FR_NOT_READY:
                    error = SC64_SD_FS_READ_ERROR;
                    break;
                case FR_NO_FILE:
                case FR_NO_PATH:
                    error = SC64_SD_FS_NO_FILE;
                    break;
                default:
                    error = SC64_SD_FS_OTHER_ERROR;
                    break;
            }
            break;
        }
    } while (0);

    f_unmount("");

    sc64_sd_deinit();

    return error;
}
